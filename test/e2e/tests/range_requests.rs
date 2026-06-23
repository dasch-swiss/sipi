mod common;

use common::{client, server};
use sipi_e2e::test_data_dir;
use std::io::Write;

/// Create a test file in the server directory and return its cleanup guard.
struct TestFile {
    path: std::path::PathBuf,
}

impl TestFile {
    fn create(name: &str, content: &[u8]) -> Self {
        let server_dir = test_data_dir().join("server");
        std::fs::create_dir_all(&server_dir).expect("create server dir");
        let path = server_dir.join(name);
        let mut f = std::fs::File::create(&path).expect("create test file");
        f.write_all(content).expect("write test file");
        TestFile { path }
    }
}

impl Drop for TestFile {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
    }
}

fn small_content() -> Vec<u8> {
    b"0123456789".repeat(100) // 1000 bytes
}

fn large_content() -> Vec<u8> {
    // 10MB file — pattern-filled for verification
    let pattern = b"ABCDEFGHIJ";
    pattern.repeat(1_000_000) // 10MB
}

#[test]
fn small_file_no_range() {
    let content = small_content();
    let _file = TestFile::create("small_test_file.bin", &content);
    let srv = server();

    let resp = client()
        .get(format!("{}/server/small_test_file.bin", srv.base_url))
        .send()
        .expect("GET failed");

    assert_eq!(resp.status().as_u16(), 200);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), content.len());
}

#[test]
fn small_file_range_first_100_bytes() {
    let content = small_content();
    let _file = TestFile::create("range_first100.bin", &content);
    let srv = server();

    let resp = client()
        .get(format!("{}/server/range_first100.bin", srv.base_url))
        .header("Range", "bytes=0-99")
        .send()
        .expect("GET range failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 100);
    assert_eq!(&body[..], &content[..100]);
}

#[test]
fn small_file_range_middle_bytes() {
    let content = small_content();
    let _file = TestFile::create("range_middle.bin", &content);
    let srv = server();

    let resp = client()
        .get(format!("{}/server/range_middle.bin", srv.base_url))
        .header("Range", "bytes=200-299")
        .send()
        .expect("GET range failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 100);
    assert_eq!(&body[..], &content[200..300]);
}

#[test]
fn small_file_open_ended_from_start() {
    let content = small_content();
    let _file = TestFile::create("range_open_start.bin", &content);
    let srv = server();

    let resp = client()
        .get(format!("{}/server/range_open_start.bin", srv.base_url))
        .header("Range", "bytes=0-")
        .send()
        .expect("GET range failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), content.len());
}

#[test]
fn small_file_open_ended_from_middle() {
    let content = small_content();
    let _file = TestFile::create("range_open_mid.bin", &content);
    let srv = server();

    let resp = client()
        .get(format!("{}/server/range_open_mid.bin", srv.base_url))
        .header("Range", "bytes=500-")
        .send()
        .expect("GET range failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 500);
    assert_eq!(&body[..], &content[500..]);
}

#[test]
fn small_file_range_last_byte() {
    let content = small_content();
    let _file = TestFile::create("range_last_byte.bin", &content);
    let srv = server();
    let last = content.len() - 1;

    let resp = client()
        .get(format!("{}/server/range_last_byte.bin", srv.base_url))
        .header("Range", format!("bytes={}-{}", last, last))
        .send()
        .expect("GET range failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 1);
    assert_eq!(body[0], content[last]);
}

#[test]
fn sequential_range_requests() {
    let content = small_content();
    let _file = TestFile::create("range_sequential.bin", &content);
    let srv = server();

    let chunk_size = 200usize;
    for i in 0..3 {
        let start = i * chunk_size;
        let end = start + chunk_size - 1;

        let resp = client()
            .get(format!("{}/server/range_sequential.bin", srv.base_url))
            .header("Range", format!("bytes={}-{}", start, end))
            .send()
            .unwrap_or_else(|_| panic!("GET range chunk {} failed", i));

        assert_eq!(resp.status().as_u16(), 206, "chunk {} not 206", i);
        let body = resp.bytes().expect("read body");
        assert_eq!(body.len(), chunk_size, "chunk {} wrong size", i);
    }
}

// =============================================================================
// Large file range request tests (10MB)
// =============================================================================

#[test]
fn large_file_range_first_1mb() {
    let content = large_content();
    let _file = TestFile::create("large_range_test.bin", &content);
    let srv = server();

    let resp = client()
        .get(format!("{}/server/large_range_test.bin", srv.base_url))
        .header("Range", "bytes=0-1048575") // first 1MB
        .send()
        .expect("GET large range failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 1_048_576);
    assert_eq!(&body[..10], &content[..10]);
}

#[test]
fn large_file_range_middle_chunk() {
    let content = large_content();
    let _file = TestFile::create("large_range_mid.bin", &content);
    let srv = server();

    // Request 1MB from the middle of the 10MB file
    let start = 5_000_000;
    let end = start + 1_048_575;

    let resp = client()
        .get(format!("{}/server/large_range_mid.bin", srv.base_url))
        .header("Range", format!("bytes={}-{}", start, end))
        .send()
        .expect("GET large range middle failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 1_048_576);
    assert_eq!(&body[..10], &content[start..start + 10]);
}

#[test]
fn large_file_range_last_bytes() {
    let content = large_content();
    let _file = TestFile::create("large_range_last.bin", &content);
    let srv = server();

    // Request last 1000 bytes using suffix range
    let start = content.len() - 1000;
    let end = content.len() - 1;

    let resp = client()
        .get(format!("{}/server/large_range_last.bin", srv.base_url))
        .header("Range", format!("bytes={}-{}", start, end))
        .send()
        .expect("GET large range last failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 1000);
    assert_eq!(&body[..], &content[start..=end]);
}

#[test]
fn large_file_range_single_last_byte() {
    let content = large_content();
    let _file = TestFile::create("large_range_single_last.bin", &content);
    let srv = server();
    let last = content.len() - 1;

    let resp = client()
        .get(format!(
            "{}/server/large_range_single_last.bin",
            srv.base_url
        ))
        .header("Range", format!("bytes={}-{}", last, last))
        .send()
        .expect("GET large range single last byte failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), 1);
    assert_eq!(body[0], content[last]);
}

#[test]
fn large_file_open_ended_from_start() {
    let content = large_content();
    let _file = TestFile::create("large_range_open_start.bin", &content);
    let srv = server();

    let resp = client()
        .get(format!(
            "{}/server/large_range_open_start.bin",
            srv.base_url
        ))
        .header("Range", "bytes=0-")
        .send()
        .expect("GET large range open-ended from start failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    assert_eq!(body.len(), content.len());
}

#[test]
fn large_file_open_ended_from_middle() {
    let content = large_content();
    let _file = TestFile::create("large_range_open_mid.bin", &content);
    let srv = server();
    let start_pos = 5 * 1024 * 1024; // 5MB

    let resp = client()
        .get(format!("{}/server/large_range_open_mid.bin", srv.base_url))
        .header("Range", format!("bytes={}-", start_pos))
        .send()
        .expect("GET large range open-ended from middle failed");

    assert_eq!(resp.status().as_u16(), 206);
    let body = resp.bytes().expect("read body");
    let expected_size = content.len() - start_pos;
    assert_eq!(body.len(), expected_size);
    assert_eq!(&body[..10], &content[start_pos..start_pos + 10]);
}

#[test]
fn large_file_full_download() {
    let content = large_content();
    let _file = TestFile::create("large_full.bin", &content);
    let srv = server();

    // Full download without Range header
    let resp = client()
        .get(format!("{}/server/large_full.bin", srv.base_url))
        .send()
        .expect("GET large file failed");

    assert_eq!(resp.status().as_u16(), 200);
    let body = resp.bytes().expect("read body");
    assert_eq!(
        body.len(),
        content.len(),
        "full download should return all 10MB"
    );
}

#[test]
fn large_file_sequential_range_reassembly() {
    // Download a 10MB file in 1MB chunks via sequential Range requests,
    // reassemble, and verify integrity against original content.
    let content = large_content();
    let _file = TestFile::create("large_reassembly.bin", &content);
    let srv = server();

    let chunk_size = 1_048_576; // 1MB
    let mut reassembled = Vec::with_capacity(content.len());

    let num_chunks = content.len().div_ceil(chunk_size);
    for i in 0..num_chunks {
        let start = i * chunk_size;
        let end = std::cmp::min(start + chunk_size - 1, content.len() - 1);

        let resp = client()
            .get(format!("{}/server/large_reassembly.bin", srv.base_url))
            .header("Range", format!("bytes={}-{}", start, end))
            .send()
            .unwrap_or_else(|_| panic!("GET range chunk {} failed", i));

        assert_eq!(resp.status().as_u16(), 206, "chunk {} not 206", i);
        let body = resp.bytes().expect("read body");
        reassembled.extend_from_slice(&body);
    }

    assert_eq!(
        reassembled.len(),
        content.len(),
        "reassembled size should match original"
    );
    assert_eq!(
        reassembled, content,
        "reassembled content should match original byte-for-byte"
    );
}
