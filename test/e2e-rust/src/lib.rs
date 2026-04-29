use nix::sys::signal::{self, Signal};
use nix::unistd::Pid;
use std::io::{BufRead, BufReader};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU16, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant};

/// Atomic port counter to avoid conflicts when tests run in parallel.
/// Start well above privileged ports (macOS restricts ports below 1024).
/// The base offset is derived from the PID so back-to-back test
/// binaries don't restart at the same port and collide on TIME_WAIT
/// state from each other's recently-closed sockets.
static NEXT_PORT: OnceLock<AtomicU16> = OnceLock::new();

fn next_port() -> &'static AtomicU16 {
    NEXT_PORT.get_or_init(|| {
        // 11024 + (PID mod 16k), keeping ports under 32k so the +2 SSL port never overflows u16.
        let offset = (std::process::id() % 16384) as u16;
        AtomicU16::new(11024 + offset)
    })
}

/// Allocate a pair of (http_port, ssl_port) for a test server instance.
pub fn allocate_ports() -> (u16, u16) {
    let http = next_port().fetch_add(2, Ordering::SeqCst);
    assert!(http < 65534, "port counter overflow");
    let ssl = http + 1;
    (http, ssl)
}

/// Manages a sipi server process for testing.
pub struct SipiServer {
    child: Child,
    pub http_port: u16,
    pub ssl_port: u16,
    pub base_url: String,
    pub ssl_base_url: String,
}

impl SipiServer {
    /// Start a sipi server with the given config file and extra CLI arguments.
    ///
    /// `config` — path to the Lua config file, relative to `working_dir`.
    /// `working_dir` — the directory sipi should run in (where config/, scripts/, images/ are).
    /// `extra_args` — additional CLI arguments appended after standard ones.
    ///                 CLI args override Lua config values (CLI11 precedence).
    pub fn start_with_args(config: &str, working_dir: &Path, extra_args: &[&str]) -> Self {
        let sipi_bin = std::env::var("SIPI_BIN")
            .unwrap_or_else(|_| find_sipi_bin().to_string_lossy().to_string());

        let (http_port, ssl_port) = allocate_ports();

        // If a stale sipi from a previous test run is still holding these ports,
        // kill it. Otherwise the new server can't bind and tests get IncompleteMessage.
        kill_process_on_port(http_port);
        kill_process_on_port(ssl_port);

        eprintln!(
            "[test-harness] Starting sipi: bin={} config={} ports={}/{} cwd={} extra_args={:?}",
            sipi_bin,
            config,
            http_port,
            ssl_port,
            working_dir.display(),
            extra_args
        );

        let mut cmd = Command::new(&sipi_bin);
        cmd.arg("--config")
            .arg(config)
            .arg("--serverport")
            .arg(http_port.to_string())
            .arg("--sslport")
            .arg(ssl_port.to_string())
            .args(extra_args)
            .current_dir(working_dir)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());

        // Coverage support: when sipi is built with --coverage (gcov/llvm-cov),
        // .gcda paths are embedded in the binary at compile time.
        //
        // CI (nix-clang): paths are absolute (/home/runner/.../build/CMakeFiles/...).
        // The gcda files write to the correct location regardless of cwd.
        // Do NOT set GCOV_PREFIX — it mangles absolute paths.
        //
        // Local (zig): paths are relative (CMakeFiles/...). When the test harness
        // changes cwd to test/_test_data/, gcda files would land there instead of
        // in build/. GCOV_PREFIX redirects them to the build dir.
        //
        // Derive `build_dir` from the *actually-resolved* sipi binary, not from
        // `find_sipi_bin()` (the cmake-inner-loop default). When `$SIPI_BIN`
        // points outside `repo_root/build` (static-musl CI, sanitizer CI, or
        // any custom binary), `find_sipi_bin()` would compute the wrong dir.
        let build_dir = PathBuf::from(&sipi_bin)
            .parent()
            .expect("sipi binary should be in a build directory")
            .to_path_buf();
        if !build_dir.is_absolute() {
            cmd.env("GCOV_PREFIX", &build_dir)
                .env("GCOV_PREFIX_STRIP", "0");
        }

        let mut child = cmd.spawn().unwrap_or_else(|e| {
            panic!("Failed to start sipi at {}: {}", sipi_bin, e);
        });
        let child_pid = child.id();
        eprintln!("[test-harness] Spawned sipi PID={}", child_pid);

        // Drain stderr in a background thread to prevent pipe buffer from filling.
        let stderr = child.stderr.take().expect("stderr captured");
        let stderr_buf = Arc::new(Mutex::new(String::new()));
        let stderr_buf_clone = Arc::clone(&stderr_buf);
        std::thread::spawn(move || {
            let mut reader = BufReader::new(stderr);
            let mut buf = String::new();
            loop {
                buf.clear();
                match reader.read_line(&mut buf) {
                    Ok(0) | Err(_) => break,
                    Ok(_) => {
                        if let Ok(mut captured) = stderr_buf_clone.lock() {
                            captured.push_str(&buf);
                            if captured.len() > 65536 {
                                let drain = captured.len() - 32768;
                                captured.drain(..drain);
                            }
                        }
                    }
                }
            }
        });

        // Drain stdout in a background thread. Capture content for diagnostics
        // AND watch for the readiness signal.
        let stdout = child.stdout.take().expect("stdout captured");
        let stdout_buf = Arc::new(Mutex::new(String::new()));
        let stdout_buf_clone = Arc::clone(&stdout_buf);
        let ready_signal_ssl = "Server listening on SSL port";
        let ready_signal_http = "Server listening on HTTP port";
        let ready_signal_no_ssl = "SSL port";// "SSL port N bind failed" also indicates startup complete
        let (tx, rx) = std::sync::mpsc::channel();

        std::thread::spawn(move || {
            let reader = BufReader::new(stdout);
            for line in reader.lines() {
                match line {
                    Ok(l) => {
                        if let Ok(mut captured) = stdout_buf_clone.lock() {
                            captured.push_str(&l);
                            captured.push('\n');
                            if captured.len() > 65536 {
                                let drain = captured.len() - 32768;
                                captured.drain(..drain);
                            }
                        }
                        // Detect readiness: SSL port listening, HTTP-only fallback, or SSL bind failure
                        if l.contains(ready_signal_ssl) || l.contains(ready_signal_no_ssl) {
                            let _ = tx.send(());
                        }
                    }
                    Err(_) => break,
                }
            }
        });

        // Wait for ready signal or TCP connectivity
        let timeout = Duration::from_secs(30);
        let start = Instant::now();

        match rx.recv_timeout(timeout) {
            Ok(()) => {
                eprintln!(
                    "[test-harness] Readiness signal received after {:?}",
                    start.elapsed()
                );
            }
            Err(_) => {
                // Fallback: poll TCP
                eprintln!("[test-harness] No readiness signal, falling back to TCP probe");
                while start.elapsed() < timeout {
                    if TcpStream::connect(format!("127.0.0.1:{}", http_port)).is_ok() {
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(100));
                }
                if TcpStream::connect(format!("127.0.0.1:{}", http_port)).is_err() {
                    let captured_stderr = stderr_buf
                        .lock()
                        .map(|s| s.clone())
                        .unwrap_or_default();
                    let captured_stdout = stdout_buf
                        .lock()
                        .map(|s| s.clone())
                        .unwrap_or_default();
                    child.kill().ok();
                    panic!(
                        "Sipi failed to start within {:?} on port {}\nstdout:\n{}\nstderr:\n{}",
                        timeout, http_port, captured_stdout, captured_stderr
                    );
                }
            }
        }

        // The readiness log is emitted before the event loop starts accepting
        // connections. Probe /metrics to confirm the server is actually processing.
        let base_url = format!("http://127.0.0.1:{}", http_port);
        let probe_client = reqwest::blocking::Client::builder()
            .danger_accept_invalid_certs(true)
            .timeout(Duration::from_secs(5))
            .build()
            .expect("probe client");

        let mut http_ready = false;
        let mut last_probe_err = String::new();
        let mut probe_count = 0u32;
        while start.elapsed() < timeout {
            probe_count += 1;
            match probe_client.get(format!("{}/metrics", base_url)).send() {
                Ok(resp) if resp.status().is_success() => {
                    http_ready = true;
                    eprintln!(
                        "[test-harness] HTTP ready after {} probes, {:?} total",
                        probe_count,
                        start.elapsed()
                    );
                    break;
                }
                Ok(resp) => {
                    last_probe_err = format!("HTTP {}", resp.status());
                }
                Err(e) => {
                    last_probe_err = format!("{}", e);
                }
            }
            // Check if child is still alive
            if let Ok(Some(status)) = child.try_wait() {
                let captured_stdout = stdout_buf
                    .lock()
                    .map(|s| s.clone())
                    .unwrap_or_default();
                let captured_stderr = stderr_buf
                    .lock()
                    .map(|s| s.clone())
                    .unwrap_or_default();
                panic!(
                    "Sipi process exited unexpectedly with {} after {} probes\n\
                     last probe error: {}\nstdout:\n{}\nstderr:\n{}",
                    status, probe_count, last_probe_err, captured_stdout, captured_stderr
                );
            }
            std::thread::sleep(Duration::from_millis(50));
        }

        if !http_ready {
            let captured_stdout = stdout_buf
                .lock()
                .map(|s| s.clone())
                .unwrap_or_default();
            let captured_stderr = stderr_buf
                .lock()
                .map(|s| s.clone())
                .unwrap_or_default();
            child.kill().ok();
            panic!(
                "Sipi started but never served HTTP on port {} within {:?}\n\
                 probes attempted: {}, last error: {}\nstdout:\n{}\nstderr:\n{}",
                http_port, timeout, probe_count, last_probe_err, captured_stdout, captured_stderr
            );
        }

        SipiServer {
            child,
            http_port,
            ssl_port,
            base_url,
            ssl_base_url: format!("https://127.0.0.1:{}", ssl_port),
        }
    }

    /// Start a sipi server with the given config file (no extra args).
    pub fn start(config: &str, working_dir: &Path) -> Self {
        Self::start_with_args(config, working_dir, &[])
    }

    /// Start with the default test config.
    /// Sipi auto-creates the cache directory if missing.
    pub fn start_default() -> Self {
        let test_data = test_data_dir();
        Self::start("config/sipi.e2e-test-config.lua", &test_data)
    }

    /// Get the OS PID of the server process.
    pub fn pid(&self) -> u32 {
        self.child.id()
    }

    /// Check if the server process has exited. Returns the exit status if so.
    pub fn try_wait(&mut self) -> std::io::Result<Option<std::process::ExitStatus>> {
        self.child.try_wait()
    }

    /// Gracefully stop the server via SIGTERM.
    pub fn stop(&mut self) {
        let pid = Pid::from_raw(i32::try_from(self.child.id()).expect("PID overflows i32"));
        signal::kill(pid, Signal::SIGTERM).ok();

        // Wait up to 5 seconds for graceful shutdown
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            match self.child.try_wait() {
                Ok(Some(_)) => return,
                Ok(None) if Instant::now() < deadline => {
                    std::thread::sleep(Duration::from_millis(100));
                }
                _ => {
                    self.child.kill().ok();
                    self.child.wait().ok();
                    return;
                }
            }
        }
    }
}

impl Drop for SipiServer {
    fn drop(&mut self) {
        self.stop();
    }
}

/// Path to the sipi repository root.
///
/// Resolution order:
///   1. `$SIPI_REPO_ROOT` if set — required when this crate is built in a
///      Nix sandbox, because `CARGO_MANIFEST_DIR` is baked at compile time
///      and points at the (now-deleted) sandbox source path.
///   2. `CARGO_MANIFEST_DIR/../..` — the inner-loop `cargo test` path.
pub fn repo_root() -> PathBuf {
    if let Ok(p) = std::env::var("SIPI_REPO_ROOT") {
        return PathBuf::from(p);
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .expect("e2e-rust should be two levels below repo root")
        .to_path_buf()
}

/// Default sipi binary path for the cmake inner-loop dev shell
/// (`<repo>/build/sipi`).
///
/// Test harness resolution prefers `$SIPI_BIN` (set by
/// `just nix-test-e2e` to `<repo>/result/bin/sipi`); this fallback
/// only fires when `SIPI_BIN` is unset.
pub fn find_sipi_bin() -> PathBuf {
    repo_root().join("build").join("sipi")
}

/// Path to the test data directory.
pub fn test_data_dir() -> PathBuf {
    repo_root().join("test").join("_test_data")
}

/// Kill any process listening on the given port (cleanup from previous test runs).
fn kill_process_on_port(port: u16) {
    // Use lsof to find the PID, then SIGKILL it
    if let Ok(output) = Command::new("lsof")
        .args(["-ti", &format!(":{}", port)])
        .output()
    {
        let pids = String::from_utf8_lossy(&output.stdout);
        for pid_str in pids.split_whitespace() {
            if let Ok(pid) = pid_str.parse::<i32>() {
                signal::kill(Pid::from_raw(pid), Signal::SIGKILL).ok();
            }
        }
        if !pids.trim().is_empty() {
            // Give the OS time to release the port
            std::thread::sleep(Duration::from_millis(500));
        }
    }
}

/// Build a reqwest blocking client that accepts self-signed certs.
///
/// Connection pooling is disabled (`pool_max_idle_per_host(0)`) so that each
/// request opens a fresh TCP connection. This prevents stale keep-alive
/// connections from accumulating in both the client pool and the server's
/// poll set — the root cause of transient "error sending request" failures
/// on fast arm64 musl builds where the server's idle-connection sweep never
/// triggers because tests run back-to-back without a 5-second idle gap.
pub fn http_client() -> reqwest::blocking::Client {
    reqwest::blocking::Client::builder()
        .danger_accept_invalid_certs(true)
        .timeout(Duration::from_secs(30))
        .pool_max_idle_per_host(0)
        .build()
        .expect("Failed to build HTTP client")
}

/// Retry a test assertion up to `max_attempts` times.
///
/// The closure should return `Ok(())` on success or `Err(message)` on failure.
/// Between attempts, sleeps for 2 seconds. Panics if all attempts fail.
///
/// Use this for assertions that depend on server-side timing (e.g., an RAII
/// guard releasing after the response is sent, so metrics don't update
/// instantly). Do NOT use this for connection-level retries — those are
/// solved by disabling connection pooling in `http_client()`.
pub fn retry_flaky<F>(max_attempts: u32, f: F)
where
    F: Fn() -> Result<(), String>,
{
    let mut last_err = String::new();
    for attempt in 1..=max_attempts {
        match f() {
            Ok(()) => return,
            Err(e) => {
                last_err = e;
                if attempt < max_attempts {
                    eprintln!(
                        "[retry_flaky] attempt {}/{} failed: {}",
                        attempt, max_attempts, last_err
                    );
                    std::thread::sleep(Duration::from_secs(2));
                }
            }
        }
    }
    panic!(
        "Test failed after {} attempts. Last error: {}",
        max_attempts, last_err
    );
}
