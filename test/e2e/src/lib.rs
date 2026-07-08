use nix::sys::signal::{self, Signal};
use nix::unistd::Pid;
use std::io::{BufRead, BufReader};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU16, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant};

pub mod diff;
pub use diff::{diff_get, diff_request, BodyMatch, DiffAllowlist, DiffResult, HeaderDiff};

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

/// Which binary a `SipiServer` instance wraps. Internal to the harness —
/// callers select a binary through `start_with_args` (subject) or
/// `start_pair` (subject + reference), never by naming a `ServerKind`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum ServerKind {
    /// The Rust shell under test — `//src/cli-rs:sipi`, resolved from `$SIPI_BIN`.
    Subject,
    /// The retained C++ server used as a differential oracle —
    /// `//src/cli:sipi`, resolved from `$SIPI_BIN_REF`.
    Reference,
}

impl ServerKind {
    /// The log line each binary emits once its HTTP listener is bound, used
    /// as a fast readiness hint. The startup poll falls back to a TCP /
    /// `/health` probe if it never appears (suppressed by log level, or
    /// written to a stream we don't match), so this is an optimization, not
    /// a hard requirement.
    fn ready_signal(self) -> &'static str {
        match self {
            // server-rs logs this once `sipi::run` binds the axum listener.
            ServerKind::Subject => "SIPI Rust shell listening",
            // shttps `Server::run` logs "Server listening on HTTP port %d".
            ServerKind::Reference => "Server listening on HTTP port",
        }
    }

    /// Short label for `[test-harness]` diagnostics so two interleaved
    /// spawns are distinguishable.
    fn label(self) -> &'static str {
        match self {
            ServerKind::Subject => "subject(rust)",
            ServerKind::Reference => "reference(c++)",
        }
    }
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
    /// Start the Rust shell (the subject under test) with the given config
    /// file and extra CLI arguments.
    ///
    /// `config` — path to the config file, relative to `working_dir`.
    /// `working_dir` — the directory sipi should run in (where config/, scripts/, images/ are).
    /// `extra_args` — additional CLI arguments appended after standard ones.
    ///                 CLI args override config values (CLI precedence).
    pub fn start_with_args(config: &str, working_dir: &Path, extra_args: &[&str]) -> Self {
        Self::start_env(config, working_dir, extra_args, &[])
    }

    /// Like [`Self::start_with_args`], additionally setting `extra_env` in
    /// the spawned process's environment.
    pub fn start_env(
        config: &str,
        working_dir: &Path,
        extra_args: &[&str],
        extra_env: &[(&str, &str)],
    ) -> Self {
        Self::spawn(
            sipi_bin_path(),
            ServerKind::Subject,
            config,
            working_dir,
            extra_args,
            extra_env,
        )
    }

    /// Start ONLY the C++ oracle (reference) — the reference-side counterpart
    /// to [`Self::start_with_args`]. Use when a probe needs subject and
    /// reference to access shared state (e.g. a `--cache-dir`) SEQUENTIALLY
    /// rather than concurrently, to avoid two live `SipiCache` instances
    /// racing on the same `.sipicache` index file (confirmed on CI to be a
    /// real crash, not just a benign duplicate write — plan 02 §7.7).
    pub fn start_reference_with_args(
        config: &str,
        working_dir: &Path,
        extra_args: &[&str],
    ) -> Self {
        Self::start_reference_env(config, working_dir, extra_args, &[])
    }

    /// Like [`Self::start_reference_with_args`], additionally setting
    /// `extra_env` in the spawned process's environment.
    pub fn start_reference_env(
        config: &str,
        working_dir: &Path,
        extra_args: &[&str],
        extra_env: &[(&str, &str)],
    ) -> Self {
        Self::spawn(
            sipi_oracle_bin_path(),
            ServerKind::Reference,
            config,
            working_dir,
            extra_args,
            extra_env,
        )
    }

    /// Start the Rust shell (subject) and the C++ oracle (reference) on
    /// separate port pairs from the same config, for differential testing.
    /// The subject resolves from `$SIPI_BIN`, the reference from
    /// `$SIPI_BIN_REF` (set only by the `//test/e2e:differential` target).
    /// Spawned sequentially so the two binaries' startup logs don't interleave.
    pub fn start_pair(
        config: &str,
        working_dir: &Path,
        extra_args: &[&str],
    ) -> (SipiServer, SipiServer) {
        Self::start_pair_env(config, working_dir, extra_args, &[])
    }

    /// Like [`Self::start_pair`], additionally setting `extra_env` in both
    /// spawned processes' environment (e.g. to probe a `SIPI_*` env binding
    /// per plan 02 §7.7).
    pub fn start_pair_env(
        config: &str,
        working_dir: &Path,
        extra_args: &[&str],
        extra_env: &[(&str, &str)],
    ) -> (SipiServer, SipiServer) {
        let subject = Self::spawn(
            sipi_bin_path(),
            ServerKind::Subject,
            config,
            working_dir,
            extra_args,
            extra_env,
        );
        let reference = Self::spawn(
            sipi_oracle_bin_path(),
            ServerKind::Reference,
            config,
            working_dir,
            extra_args,
            extra_env,
        );
        (subject, reference)
    }

    /// Like [`Self::start_pair`], but the subject and reference get
    /// DIFFERENT `extra_args` (e.g. each its own isolated `--cache-dir` so
    /// two independent `SipiCache` instances never race on a shared
    /// `.sipicache` index file — plan 02 §7.7's `cache-nfiles` probe).
    pub fn start_pair_split(
        config: &str,
        working_dir: &Path,
        subject_extra_args: &[&str],
        reference_extra_args: &[&str],
    ) -> (SipiServer, SipiServer) {
        let subject = Self::spawn(
            sipi_bin_path(),
            ServerKind::Subject,
            config,
            working_dir,
            subject_extra_args,
            &[],
        );
        let reference = Self::spawn(
            sipi_oracle_bin_path(),
            ServerKind::Reference,
            config,
            working_dir,
            reference_extra_args,
            &[],
        );
        (subject, reference)
    }

    /// Spawn one sipi process of `kind` from the resolved `bin`, wait until
    /// it serves `/health`, and return the handle.
    fn spawn(
        bin: String,
        kind: ServerKind,
        config: &str,
        working_dir: &Path,
        extra_args: &[&str],
        extra_env: &[(&str, &str)],
    ) -> Self {
        let (http_port, ssl_port) = allocate_ports();

        // If a stale sipi from a previous test run is still holding these
        // ports, terminate it gracefully (SIGTERM, then SIGKILL on timeout).
        // Forced SIGKILL bypasses C++ destructors and orphans static-singleton
        // heap, which ASan reports as a leak — see the asan-flake fix.
        kill_process_on_port(http_port);
        kill_process_on_port(ssl_port);

        eprintln!(
            "[test-harness] Starting {} sipi: bin={} config={} ports={}/{} cwd={} extra_args={:?} extra_env={:?}",
            kind.label(),
            bin,
            config,
            http_port,
            ssl_port,
            working_dir.display(),
            extra_args,
            extra_env
        );

        // `--drain-timeout 2` keeps every test's sipi shutdown bounded to ~2s
        // (default is 30s). With `stop()`'s 5s deadline, this leaves a 2.5×
        // margin even under ASan's runtime overhead, so SIGKILL never fires
        // on a still-draining server. Placed before `extra_args` so an
        // individual test can still override it (last-wins).
        let mut cmd = Command::new(&bin);
        cmd.arg("server")
            .arg("--config")
            .arg(config)
            .arg("--serverport")
            .arg(http_port.to_string());
        // The Rust shell parses `--sslport` but serves plain HTTP behind
        // Traefik. The C++ oracle would bind a real SSL listener, so the
        // reference is spawned HTTP-only: with `--config` its `ssl_port`
        // defaults to -1 and the SSL block is skipped when `--sslport` is
        // absent. The resulting TLS difference is an allowlisted divergence.
        if matches!(kind, ServerKind::Subject) {
            cmd.arg("--sslport").arg(ssl_port.to_string());
        }
        cmd.arg("--drain-timeout")
            .arg("2")
            .args(extra_args)
            .envs(extra_env.iter().copied())
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
        // Derive `build_dir` from the *actually-resolved* sipi binary so a
        // `$SIPI_BIN`/`$SIPI_BIN_REF` outside `repo_root/build` (static-musl CI,
        // sanitizer CI, or any custom binary) still computes the right dir.
        let build_dir = PathBuf::from(&bin)
            .parent()
            .expect("sipi binary should be in a build directory")
            .to_path_buf();
        if !build_dir.is_absolute() {
            cmd.env("GCOV_PREFIX", &build_dir)
                .env("GCOV_PREFIX_STRIP", "0");
        }

        let mut child = cmd.spawn().unwrap_or_else(|e| {
            panic!("Failed to start {} sipi at {}: {}", kind.label(), bin, e);
        });
        eprintln!(
            "[test-harness] Spawned {} sipi PID={}",
            kind.label(),
            child.id()
        );

        // Drain both child streams on background threads (prevents pipe-buffer
        // backpressure) and feed the readiness channel on the listen-log line.
        // Watching both streams keeps readiness independent of which sink a
        // given binary logs to.
        let ready_signal = kind.ready_signal();
        let (tx, rx) = std::sync::mpsc::channel();
        let stderr_buf = spawn_log_drain(
            child.stderr.take().expect("stderr captured"),
            ready_signal,
            tx.clone(),
        );
        let stdout_buf = spawn_log_drain(
            child.stdout.take().expect("stdout captured"),
            ready_signal,
            tx,
        );

        // Wait for readiness: the listen-log signal (fast hint) or a successful
        // TCP connect. Polling both avoids a 30s stall when the signal is
        // suppressed or lands on a stream we don't match (the C++ oracle's log
        // sink is configurable).
        let timeout = Duration::from_secs(30);
        let start = Instant::now();
        let mut connected = false;
        while start.elapsed() < timeout {
            if rx.try_recv().is_ok() {
                eprintln!(
                    "[test-harness] {} readiness signal after {:?}",
                    kind.label(),
                    start.elapsed()
                );
                connected = true;
                break;
            }
            if TcpStream::connect(format!("127.0.0.1:{}", http_port)).is_ok() {
                connected = true;
                break;
            }
            if let Ok(Some(status)) = child.try_wait() {
                panic!(
                    "{} sipi exited with {} during startup\nstdout:\n{}\nstderr:\n{}",
                    kind.label(),
                    status,
                    dump(&stdout_buf),
                    dump(&stderr_buf)
                );
            }
            std::thread::sleep(Duration::from_millis(100));
        }
        if !connected {
            child.kill().ok();
            panic!(
                "{} sipi failed to start within {:?} on port {}\nstdout:\n{}\nstderr:\n{}",
                kind.label(),
                timeout,
                http_port,
                dump(&stdout_buf),
                dump(&stderr_buf)
            );
        }

        // The listen log / TCP-accept precedes the event loop actually
        // processing requests. Probe /health (Rust-native, engine-independent,
        // served by both transports) to confirm the server is serving.
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
            match probe_client.get(format!("{}/health", base_url)).send() {
                Ok(resp) if resp.status().is_success() => {
                    http_ready = true;
                    eprintln!(
                        "[test-harness] {} HTTP ready after {} probes, {:?} total",
                        kind.label(),
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
            if let Ok(Some(status)) = child.try_wait() {
                panic!(
                    "{} sipi exited with {} after {} /health probes\n\
                     last probe error: {}\nstdout:\n{}\nstderr:\n{}",
                    kind.label(),
                    status,
                    probe_count,
                    last_probe_err,
                    dump(&stdout_buf),
                    dump(&stderr_buf)
                );
            }
            std::thread::sleep(Duration::from_millis(50));
        }

        if !http_ready {
            child.kill().ok();
            panic!(
                "{} sipi started but never served HTTP on port {} within {:?}\n\
                 probes attempted: {}, last error: {}\nstdout:\n{}\nstderr:\n{}",
                kind.label(),
                http_port,
                timeout,
                probe_count,
                last_probe_err,
                dump(&stdout_buf),
                dump(&stderr_buf)
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

/// Drain a child stream on a background thread: capture lines for
/// diagnostics (ring-capped at 64 KiB → 32 KiB) and signal `tx` on the
/// first line containing `ready_signal`. Returns the shared capture buffer.
fn spawn_log_drain(
    stream: impl std::io::Read + Send + 'static,
    ready_signal: &'static str,
    tx: std::sync::mpsc::Sender<()>,
) -> Arc<Mutex<String>> {
    let buf = Arc::new(Mutex::new(String::new()));
    let buf_clone = Arc::clone(&buf);
    std::thread::spawn(move || {
        let reader = BufReader::new(stream);
        for line in reader.lines() {
            let Ok(l) = line else { break };
            if let Ok(mut captured) = buf_clone.lock() {
                captured.push_str(&l);
                captured.push('\n');
                if captured.len() > 65536 {
                    let drain = captured.len() - 32768;
                    captured.drain(..drain);
                }
            }
            if l.contains(ready_signal) {
                let _ = tx.send(());
            }
        }
    });
    buf
}

/// Snapshot a capture buffer for inclusion in a panic message.
fn dump(buf: &Arc<Mutex<String>>) -> String {
    buf.lock().map(|s| s.clone()).unwrap_or_default()
}

/// Path to the sipi repository root.
///
/// Resolution order:
///   1. `$SIPI_REPO_ROOT` if set — required when this crate is built in a
///      sandbox, because `CARGO_MANIFEST_DIR` is baked at compile time
///      and points at the (now-deleted) sandbox source path. Bazel
///      `rust_test` sets it to the `:test_fixtures` `copy_to_directory`
///      output (read-only).
///   2. `CARGO_MANIFEST_DIR/../..` — the inner-loop `cargo test` path
///      (writable source tree).
///
/// **Writability — Bazel-specific shim.** Bazel's `copy_to_directory`
/// output is read-only (`-r-xr-xr-x`), but the e2e tests write under
/// `test_data_dir()` for cache-isolation subdirs (`test/cache.rs`),
/// throw-away configs (`test/config.rs`), and uploaded blobs
/// (`test/upload.rs`). The first `repo_root()` call after Bazel sets
/// `SIPI_REPO_ROOT` to a read-only directory therefore performs a
/// one-shot recursive copy into `$TEST_TMPDIR/sipi-repo-root` and
/// caches the writable path in `WRITABLE_REPO_ROOT` for subsequent
/// calls. The copy is fast (the source is already laid out — Bazel
/// just doesn't grant write perms) and per-test-binary, so the cost
/// stays bounded.
///
/// The result is canonicalised to an absolute path so subsequent
/// `Command::current_dir(repo_root().join(...))` calls don't compound
/// with sipi's `--file`/`--config` resolution into path-prefix-doubled
/// lookups.
pub fn repo_root() -> PathBuf {
    static WRITABLE_REPO_ROOT: std::sync::OnceLock<PathBuf> = std::sync::OnceLock::new();

    WRITABLE_REPO_ROOT
        .get_or_init(|| {
            let raw = if let Ok(p) = std::env::var("SIPI_REPO_ROOT") {
                PathBuf::from(p)
            } else {
                PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                    .parent()
                    .and_then(|p| p.parent())
                    .expect("e2e should be two levels below repo root")
                    .to_path_buf()
            };
            let canon = raw.canonicalize().unwrap_or_else(|_| raw.clone());

            // Probe writability with a temp-file probe rather than
            // checking permissions, so the cargo inner-loop (which
            // points at the source tree, fully writable) skips the
            // copy. The probe writes a tiny file in the target dir
            // and removes it; if that fails with PermissionDenied,
            // we know we need the TEST_TMPDIR copy.
            let probe = canon.join(".sipi-e2e-writable-probe");
            let writable = std::fs::File::create(&probe).is_ok();
            let _ = std::fs::remove_file(&probe);
            if writable {
                return canon;
            }

            // Read-only source (Bazel `copy_to_directory` output).
            // Copy into `$TEST_TMPDIR/sipi-repo-root` so tests can
            // write under `test_data_dir()` etc. `TEST_TMPDIR` is set
            // by Bazel for every test action and is per-binary, so
            // siblings can't collide.
            let tmpdir = std::env::var("TEST_TMPDIR")
                .map(PathBuf::from)
                .unwrap_or_else(|_| std::env::temp_dir());
            let dst = tmpdir.join("sipi-repo-root");
            // If a previous test in the same binary already populated
            // `dst`, the copy is a no-op (skip on `dst` exists).
            if !dst.exists() {
                copy_dir_recursive_writable(&canon, &dst).unwrap_or_else(|e| {
                    panic!(
                        "copy repo_root from {} to {} failed: {}",
                        canon.display(),
                        dst.display(),
                        e
                    );
                });
            }
            dst
        })
        .clone()
}

/// Recursive directory copy with `chmod u+w` applied to every output
/// (so the cache/config/upload tests can subsequently write under
/// it). Symlinks at the source are followed (the Bazel
/// `copy_to_directory` output already materialises everything as
/// real files, so there are no source symlinks; the cargo inner-loop
/// path doesn't enter this function in the first place).
fn copy_dir_recursive_writable(src: &Path, dst: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        let ft = entry.file_type()?;
        if ft.is_dir() {
            copy_dir_recursive_writable(&src_path, &dst_path)?;
        } else {
            std::fs::copy(&src_path, &dst_path)?;
            // Add user-write so test write operations succeed.
            // Read perms are inherited from the source (already 644+).
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let mut perms = std::fs::metadata(&dst_path)?.permissions();
                let mode = perms.mode() | 0o200;
                perms.set_mode(mode);
                std::fs::set_permissions(&dst_path, perms)?;
            }
        }
    }
    Ok(())
}

/// Default sipi binary path for the cmake inner-loop dev shell
/// (`<repo>/build/sipi`).
///
/// Test harness resolution prefers `$SIPI_BIN` (set by
/// `just bazel-test-e2e` and the Bazel `rust_test` env to
/// `$(rootpath //src/cli-rs:sipi)`); this fallback only fires when
/// `SIPI_BIN` is unset (cmake inner-loop dev shell path).
pub fn find_sipi_bin() -> PathBuf {
    repo_root().join("build").join("sipi")
}

/// Resolve a sipi binary path from `env_var`, falling back to `fallback`,
/// and canonicalise the result to an absolute path.
///
/// Why canonicalise: callers spawn sipi via `Command::new(&path).
/// current_dir(working_dir)`, and Rust resolves the binary path AFTER
/// applying `current_dir`. A relative path like `src/sipi`
/// (Bazel `$(rootpath …)` output) would be looked up under
/// `working_dir/src/sipi` — wrong. The canonicalisation runs in the
/// parent's cwd, which under Bazel `rust_test` is the runfiles
/// workspace root where `src/sipi` does resolve.
///
/// Returns the binary path as a String (kept as String rather than
/// PathBuf so callers can pass it straight to `Command::new` or to
/// derived `PathBuf::from(&bin)` arithmetic in `SipiServer::spawn`).
pub fn sipi_bin_path_from(env_var: &str, fallback: PathBuf) -> String {
    let raw = std::env::var(env_var).unwrap_or_else(|_| fallback.to_string_lossy().to_string());
    std::path::Path::new(&raw)
        .canonicalize()
        .map(|p| p.to_string_lossy().into_owned())
        // Fall back to the raw value if canonicalize fails (e.g. file
        // doesn't exist yet) so the downstream `Command::spawn` panic
        // surfaces the original user input rather than swallowing it.
        .unwrap_or(raw)
}

/// The Rust shell under test (`//src/cli-rs:sipi`), from `$SIPI_BIN` or the
/// cmake inner-loop fallback (`<repo>/build/sipi`).
pub fn sipi_bin_path() -> String {
    sipi_bin_path_from("SIPI_BIN", find_sipi_bin())
}

/// The C++ oracle (`//src/cli:sipi`) for the differential harness, from
/// `$SIPI_BIN_REF`. Set only by the `//test/e2e:differential` Bazel target;
/// under `cargo test` it is unset and the fallback path won't exist, so
/// `start_pair` panics at spawn with a clear message — the differential
/// suite is Bazel-only.
pub fn sipi_oracle_bin_path() -> String {
    sipi_bin_path_from(
        "SIPI_BIN_REF",
        repo_root().join("build").join("sipi-oracle"),
    )
}

/// Path to the test data directory.
pub fn test_data_dir() -> PathBuf {
    repo_root().join("test").join("_test_data")
}

/// Terminate any process listening on the given port (cleanup from previous
/// test runs). Sends SIGTERM first and polls for exit; falls back to SIGKILL
/// only if the process is still alive after the deadline. Forced SIGKILL
/// bypasses destructors and leaks singleton-owned heap that ASan flags.
fn kill_process_on_port(port: u16) {
    let Ok(output) = Command::new("lsof")
        .args(["-ti", &format!(":{}", port)])
        .output()
    else {
        return;
    };
    let pids: Vec<Pid> = String::from_utf8_lossy(&output.stdout)
        .split_whitespace()
        .filter_map(|s| s.parse::<i32>().ok())
        .map(Pid::from_raw)
        .collect();
    if pids.is_empty() {
        return;
    }

    // SIGTERM all candidates at once, then poll each.
    for pid in &pids {
        signal::kill(*pid, Signal::SIGTERM).ok();
    }

    let deadline = Instant::now() + Duration::from_millis(1500);
    let mut had_to_escalate = false;
    for pid in &pids {
        // Poll for exit via `kill -0` (signal None). Returns Err(ESRCH)
        // once the process is gone; Err(EPERM) means the PID is still
        // alive but owned by another user — treat as still-alive.
        loop {
            match signal::kill(*pid, None) {
                Err(nix::errno::Errno::ESRCH) => break, // gone
                _ if Instant::now() >= deadline => {
                    signal::kill(*pid, Signal::SIGKILL).ok();
                    had_to_escalate = true;
                    break;
                }
                _ => std::thread::sleep(Duration::from_millis(50)),
            }
        }
    }

    if had_to_escalate {
        // SIGKILL'd a process — give the OS a beat to release the port.
        std::thread::sleep(Duration::from_millis(200));
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

/// Poll `dir`'s file count until `stop` is satisfied or 2s elapses (a cache
/// write may lag a hair behind the HTTP response, plan 02 §7.7's
/// `cache-nfiles` row note), returning the last sampled count either way.
/// SipiCache writes flat `cache_XXXXXXXXXX` files directly under `dir` (no
/// subdirectories) and only writes its `.sipicache` index at shutdown, so a
/// plain top-level file count is an accurate on-disk proxy for cache state
/// while the server is running.
pub fn poll_cache_file_count(dir: &Path, stop: impl Fn(usize) -> bool) -> usize {
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        let count = std::fs::read_dir(dir)
            .map(|entries| {
                entries
                    .filter_map(Result::ok)
                    .filter(|e| e.path().is_file())
                    .count()
            })
            .unwrap_or(0);
        if stop(count) || Instant::now() >= deadline {
            return count;
        }
        std::thread::sleep(Duration::from_millis(50));
    }
}
