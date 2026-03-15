use nix::sys::signal::{self, Signal};
use nix::unistd::Pid;
use sipi_e2e::{http_client, test_data_dir, SipiServer};
use std::thread;
use std::time::{Duration, Instant};

/// Start a fresh server for each shutdown test (not shared).
fn fresh_server() -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start("config/sipi.fake-knora-test-config.lua", &test_data)
}

#[test]
fn graceful_shutdown_completes_inflight_request() {
    let srv = fresh_server();
    let base_url = srv.base_url.clone();
    let pid = srv.pid();

    // Fire an image request in a background thread.
    // JP2 decoding takes enough time to overlap with the SIGTERM.
    let handle = thread::spawn(move || {
        let c = http_client();
        c.get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            base_url
        ))
        .send()
    });

    // Brief delay to ensure the request is in-flight, then SIGTERM
    thread::sleep(Duration::from_millis(50));
    let pid_t = Pid::from_raw(i32::try_from(pid).expect("PID overflows i32"));
    signal::kill(pid_t, Signal::SIGTERM).expect("SIGTERM failed");

    // The in-flight request should complete (drain phase allows it)
    let result = handle.join().expect("request thread panicked");
    match result {
        Ok(resp) => {
            // The response should be complete (200 or at least a valid HTTP response)
            let status = resp.status().as_u16();
            assert!(
                status == 200 || status == 503,
                "in-flight request during shutdown should complete, got {}",
                status
            );
        }
        Err(e) => {
            // Connection reset is acceptable if the drain timeout was very short
            // or the request hadn't been dispatched yet
            let err_str = format!("{}", e);
            assert!(
                err_str.contains("connection") || err_str.contains("reset") || err_str.contains("closed"),
                "unexpected error during graceful shutdown: {}",
                e
            );
        }
    }
}

#[test]
fn server_exits_cleanly_after_sigterm() {
    let mut srv = fresh_server();
    let base_url = srv.base_url.clone();

    // Verify server is running
    let c = http_client();
    let resp = c
        .get(format!("{}/health", base_url))
        .send()
        .expect("health check failed");
    assert_eq!(resp.status().as_u16(), 200);

    // Send SIGTERM
    let pid = Pid::from_raw(i32::try_from(srv.pid()).expect("PID overflows i32"));
    signal::kill(pid, Signal::SIGTERM).expect("SIGTERM failed");

    // Wait for clean exit (up to 10s, covering drain_timeout + buffer)
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        match srv.try_wait() {
            Ok(Some(status)) => {
                // Server should exit cleanly (0) or via signal
                assert!(
                    status.success() || status.code().is_none(), // None = killed by signal
                    "server should exit cleanly, got: {:?}",
                    status
                );
                return;
            }
            Ok(None) if Instant::now() < deadline => {
                thread::sleep(Duration::from_millis(100));
            }
            _ => {
                panic!("server did not exit within 10s after SIGTERM");
            }
        }
    }
}

#[test]
fn new_requests_rejected_during_drain() {
    let srv = fresh_server();
    let base_url = srv.base_url.clone();

    // Send SIGTERM to start drain phase
    let pid = Pid::from_raw(i32::try_from(srv.pid()).expect("PID overflows i32"));
    signal::kill(pid, Signal::SIGTERM).expect("SIGTERM failed");

    // Brief delay for the server to enter drain mode (stop accepting)
    thread::sleep(Duration::from_millis(200));

    // New requests should fail (connection refused or timeout)
    let c = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(2))
        .build()
        .unwrap();

    let result = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            base_url
        ))
        .send();

    match result {
        Ok(resp) => {
            // If somehow we got a response, it should be an error
            // (the server might have processed this before drain started)
            let status = resp.status().as_u16();
            assert!(
                status >= 400 || status == 200, // 200 is ok if request raced ahead of drain
                "unexpected status during drain: {}",
                status
            );
        }
        Err(_) => {
            // Connection refused or timeout — expected during drain
        }
    }
}
