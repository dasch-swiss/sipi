// Only compile when the "docker" feature is enabled (or when run explicitly via --test docker_smoke).
// This prevents these tests from running in `cargo test` (used by zig-test-e2e)
// since they require a pre-built Docker image.
#![cfg(feature = "docker")]

/// Docker-image smoke tests — validates that the packaged Docker image works.
///
/// Uses docker CLI directly (no testcontainers) for simplicity and to avoid
/// async runtime issues. Each test starts a container, runs a request, and
/// cleans up via Drop.
///
/// Run with: `make test-smoke` (or `cargo test --test docker_smoke`)
/// Requires: Docker daemon running and `daschswiss/sipi:latest` image built.
use reqwest::blocking::Client;
use std::process::Command;
use std::time::{Duration, Instant};

const IMAGE: &str = "daschswiss/sipi:latest";

struct DockerContainer {
    id: String,
    port: u16,
}

impl DockerContainer {
    fn start() -> Self {
        let root = project_root();
        let output = Command::new("docker")
            .args([
                "run", "-d", "--rm",
                "-v", &format!("{}:/sipi/config", root.join("config").display()),
                "-v", &format!("{}:/sipi/images", root.join("test/_test_data/images").display()),
                "-v", &format!("{}:/sipi/scripts", root.join("scripts").display()),
                "-v", &format!("{}:/sipi/server", root.join("server").display()),
                "-P", // publish all exposed ports to random host ports
                IMAGE,
            ])
            .output()
            .expect("docker run failed");

        assert!(output.status.success(), "docker run failed: {}", String::from_utf8_lossy(&output.stderr));
        let id = String::from_utf8(output.stdout).unwrap().trim().to_string();

        // Get the mapped port for 1024
        let port_output = Command::new("docker")
            .args(["port", &id, "1024/tcp"])
            .output()
            .expect("docker port failed");
        let port_str = String::from_utf8(port_output.stdout).unwrap();
        // Format: 0.0.0.0:XXXXX -> extract port number
        let port: u16 = port_str
            .trim()
            .rsplit(':')
            .next()
            .and_then(|p| p.parse().ok())
            .expect("failed to parse mapped port");

        let container = DockerContainer { id, port };
        container.wait_ready();
        container
    }

    fn base_url(&self) -> String {
        format!("http://127.0.0.1:{}", self.port)
    }

    fn wait_ready(&self) {
        let client = Client::builder()
            .timeout(Duration::from_secs(2))
            .build()
            .unwrap();
        let deadline = Instant::now() + Duration::from_secs(30);
        let url = format!("{}/server/test.html", self.base_url());

        while Instant::now() < deadline {
            if let Ok(resp) = client.get(&url).send() {
                if resp.status().as_u16() < 500 {
                    return;
                }
            }
            std::thread::sleep(Duration::from_millis(500));
        }
        panic!("SIPI Docker container did not become ready in 30s");
    }
}

impl Drop for DockerContainer {
    fn drop(&mut self) {
        let _ = Command::new("docker")
            .args(["kill", &self.id])
            .output();
    }
}

fn project_root() -> std::path::PathBuf {
    sipi_e2e::repo_root()
}

fn smoke_client() -> Client {
    Client::builder()
        .timeout(Duration::from_secs(30))
        .build()
        .expect("HTTP client build failed")
}

#[test]
fn docker_image_serves_iiif_image() {
    let container = DockerContainer::start();
    let c = smoke_client();

    let resp = c
        .get(format!("{}/unit/lena512.jp2/full/max/0/default.jpg", container.base_url()))
        .send()
        .expect("IIIF image request failed");

    assert_eq!(resp.status().as_u16(), 200, "IIIF image should return 200");
    let body = resp.bytes().expect("read body");
    assert!(body.len() > 1000, "response should contain image data (got {} bytes)", body.len());
}

#[test]
fn docker_image_serves_static_html() {
    let container = DockerContainer::start();
    let c = smoke_client();

    let resp = c
        .get(format!("{}/server/test.html", container.base_url()))
        .send()
        .expect("static HTML request failed");

    assert_eq!(resp.status().as_u16(), 200, "static HTML should return 200");
}

#[test]
fn docker_image_health_endpoint() {
    let container = DockerContainer::start();
    let c = smoke_client();

    let resp = c
        .get(format!("{}/health", container.base_url()))
        .send()
        .expect("health endpoint request failed");

    assert_eq!(resp.status().as_u16(), 200, "health should return 200");

    let body: serde_json::Value = resp.json().expect("health should return JSON");
    assert_eq!(body["status"], "ok");
    assert!(body["version"].is_string(), "version should be present");
    assert!(body["uptime_seconds"].is_number(), "uptime_seconds should be present");
}
