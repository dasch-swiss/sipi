//! Distributed-trace propagation: the Lua preflight's outbound HTTP call to
//! dsp-api must carry a W3C `traceparent` in SIPI's trace, so dsp-api continues
//! the trace instead of starting a disconnected root (DEV-6104 §4).
//!
//! The `knora` `pre_flight` (`config/sipi.init-knora-test.lua`) issues
//! `server.http("GET", "http://<knora_path>:<knora_port>/v1/files/<id>", …)`.
//! We point `knora_path`/`knora_port` at a throwaway loopback listener, drive a
//! `/knora/…` request, and assert the captured outbound request carries a
//! well-formed `traceparent` whose trace-id equals the one SIPI stamped on its
//! own response (`OtelInResponseLayer`) — i.e. the outbound call is in the same
//! trace. `OTEL_EXPORTER_OTLP_ENDPOINT` is set (to a dead address) so the OTel
//! layer produces span contexts; export failures are fail-open and dropped.

use std::io::{Read, Write};
use std::net::TcpListener;
use std::sync::mpsc;
use std::time::{Duration, Instant};

use sipi_e2e::{http_client, test_data_dir, SipiServer};

/// The 32-hex trace-id out of a `traceparent` header value
/// (`00-<32hex>-<16hex>-<2hex>`), or `None` if it doesn't parse.
fn trace_id_of(traceparent: &str) -> Option<String> {
    let parts: Vec<&str> = traceparent.trim().split('-').collect();
    if parts.len() == 4 && parts[0] == "00" && parts[1].len() == 32 {
        Some(parts[1].to_string())
    } else {
        None
    }
}

/// The `traceparent` header value from a raw HTTP/1.1 request head (case-insensitive).
fn traceparent_in_request(raw: &str) -> Option<String> {
    raw.lines()
        .find(|l| l.to_ascii_lowercase().starts_with("traceparent:"))
        .map(|l| l[l.find(':').unwrap() + 1..].trim().to_string())
}

#[test]
fn preflight_propagates_traceparent_to_outbound_http() {
    let test_data = test_data_dir();

    // Throwaway "dsp-api": capture the first request's head, reply 200, exit.
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind mock dsp-api");
    let mock_port = listener.local_addr().unwrap().port();
    let (tx, rx) = mpsc::channel::<String>();
    let mock = std::thread::spawn(move || {
        listener.set_nonblocking(true).ok();
        let deadline = Instant::now() + Duration::from_secs(20);
        loop {
            match listener.accept() {
                Ok((mut stream, _)) => {
                    stream.set_nonblocking(false).ok();
                    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();
                    // Read until the end of the request head (`\r\n\r\n`) so the
                    // capture can't be truncated across TCP segments (or the read
                    // times out / EOFs).
                    let mut req = Vec::new();
                    let mut chunk = [0u8; 1024];
                    loop {
                        match stream.read(&mut chunk) {
                            Ok(0) => break,
                            Ok(n) => {
                                req.extend_from_slice(&chunk[..n]);
                                if req.windows(4).any(|w| w == b"\r\n\r\n") {
                                    break;
                                }
                            }
                            Err(_) => break,
                        }
                    }
                    let _ = stream.write_all(
                        b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}",
                    );
                    let _ = tx.send(String::from_utf8_lossy(&req).into_owned());
                    return;
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    if Instant::now() > deadline {
                        return;
                    }
                    std::thread::sleep(Duration::from_millis(20));
                }
                Err(_) => return,
            }
        }
    });

    // A knora-preflight config whose outbound dsp-api call targets the mock.
    let config = format!(
        r#"sipi = {{
    port = 1024,
    nthreads = 4,
    imgroot = './images',
    prefix_as_path = true,
    subdir_levels = 0,
    initscript = './config/sipi.init-knora-test.lua',
    cache_dir = './cache',
    cache_size = '20M',
    cache_nfiles = 8,
    scriptdir = './scripts',
    tmpdir = '/tmp',
    max_temp_file_age = 86400,
    knora_path = '127.0.0.1',
    knora_port = '{mock_port}',
    jwt_secret = '',
    loglevel = "DEBUG"
}}

fileserver = {{ docroot = './server', wwwroute = '/server' }}

routes = {{}}
"#
    );
    let config_path = test_data.join("config/sipi.tracing-test.lua");
    std::fs::write(&config_path, config).expect("write tracing config");

    // A dead OTLP endpoint: the OTel layer still produces span contexts (needed
    // to inject the traceparent); export attempts are refused + dropped.
    let srv = SipiServer::start_env(
        "config/sipi.tracing-test.lua",
        &test_data,
        &[],
        &[("OTEL_EXPORTER_OTLP_ENDPOINT", "http://127.0.0.1:1")],
    );
    let client = http_client();

    // Drive a knora request; the preflight fires the outbound call to the mock.
    // The IIIF outcome is irrelevant — the outbound call happens first.
    let resp = client
        .get(format!(
            "{}/knora/test.jpg/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("request to sipi");
    let resp_traceparent = resp
        .headers()
        .get("traceparent")
        .and_then(|v| v.to_str().ok())
        .map(str::to_owned);

    let captured = rx
        .recv_timeout(Duration::from_secs(25))
        .expect("mock dsp-api received the preflight's outbound request");
    let _ = mock.join();

    // The outbound call must carry a well-formed traceparent...
    let outbound = traceparent_in_request(&captured)
        .unwrap_or_else(|| panic!("outbound request carries no traceparent:\n{captured}"));
    let outbound_tid = trace_id_of(&outbound)
        .unwrap_or_else(|| panic!("outbound traceparent is malformed: {outbound}"));

    // ...in the same trace SIPI stamped on its own response.
    let resp_tp = resp_traceparent.expect("sipi response carries a traceparent");
    let resp_tid = trace_id_of(&resp_tp).expect("response traceparent well-formed");
    assert_eq!(
        outbound_tid, resp_tid,
        "outbound dsp-api call must be in SIPI's trace (outbound={outbound}, response={resp_tp})"
    );
}
