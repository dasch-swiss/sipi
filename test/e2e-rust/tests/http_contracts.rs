// HTTP-contract smoke tests ported from the (now-removed) `test/hurl/`
// suite. Most Hurl tests duplicated coverage already present in
// `server.rs`, `health.rs`, and `iiif_compliance.rs`; only the byte-count
// smoke assertions on rendered JPEG output were unique. They live here so
// the IIIF transformation path is exercised end-to-end against real bytes
// rather than only against status + content-type.

mod common;

use common::{client, server};

#[test]
fn iiif_full_max_jpeg_has_non_trivial_body() {
    // Canonical IIIF Image API 3.0 transformation: full identifier serves
    // a rendered image. Exercises the format-conversion contract
    // end-to-end (JP2 decode -> JPEG encode through
    // libjpeg/Kakadu/lcms2/exiv2).
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET full/max default.jpg failed");
    assert_eq!(resp.status().as_u16(), 200);
    let bytes = resp.bytes().expect("read body");
    assert!(
        bytes.len() > 1000,
        "JPEG body should be > 1000 bytes for a 512x512 source, got {}",
        bytes.len()
    );
}

#[test]
fn iiif_region_size_jpeg_has_non_trivial_body() {
    // Region/size/rotation/quality parameter parsing combined into a
    // single URL: crop to top-left 256x256 region, scale to width 128.
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/0,0,256,256/128,/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET region+size default.jpg failed");
    assert_eq!(resp.status().as_u16(), 200);
    let bytes = resp.bytes().expect("read body");
    assert!(
        bytes.len() > 100,
        "cropped+scaled JPEG body should be > 100 bytes, got {}",
        bytes.len()
    );
}
