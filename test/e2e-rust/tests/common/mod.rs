#![allow(dead_code)]

use reqwest::blocking::Client;
use reqwest::redirect::Policy;
use sipi_e2e::{http_client, SipiServer};
use std::sync::OnceLock;

/// Shared server instance for all tests in a test binary.
/// Stores Result so a failed start is cached (prevents repeated 30s timeouts
/// when the server can't start, e.g. dispatch bug on static musl binaries).
static SERVER: OnceLock<Result<SipiServer, String>> = OnceLock::new();

/// Shared reqwest client (follows redirects — default behavior).
/// Reused across all tests to avoid per-call overhead.
static CLIENT: OnceLock<Client> = OnceLock::new();

/// Shared reqwest client that does NOT follow redirects.
/// Used for testing 303 redirect responses (e.g., base URI → info.json).
static CLIENT_NO_REDIRECT: OnceLock<Client> = OnceLock::new();

/// Get a reference to the shared sipi server, starting it if needed.
/// If the server failed to start on the first attempt, subsequent calls
/// fail immediately instead of retrying the 30s startup timeout.
pub fn server() -> &'static SipiServer {
    SERVER
        .get_or_init(|| {
            std::panic::catch_unwind(std::panic::AssertUnwindSafe(SipiServer::start_default))
                .map_err(|e| {
                    let msg = e
                        .downcast_ref::<String>()
                        .map(|s| s.as_str())
                        .or_else(|| e.downcast_ref::<&str>().copied())
                        .unwrap_or("unknown panic");
                    format!("Server failed to start: {}", msg)
                })
        })
        .as_ref()
        .unwrap_or_else(|e| panic!("{}", e))
}

/// Get a shared reqwest client configured for self-signed certs.
/// Follows redirects (default behavior). Reused via OnceLock.
pub fn client() -> &'static Client {
    CLIENT.get_or_init(http_client)
}

/// Get a shared reqwest client that does NOT follow redirects.
/// Used for testing redirect status codes and Location headers.
pub fn client_no_redirect() -> &'static Client {
    CLIENT_NO_REDIRECT.get_or_init(|| {
        Client::builder()
            .danger_accept_invalid_certs(true)
            .timeout(std::time::Duration::from_secs(30))
            .redirect(Policy::none())
            .build()
            .expect("Failed to build no-redirect HTTP client")
    })
}
