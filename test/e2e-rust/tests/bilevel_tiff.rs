// Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
// contributors. SPDX-License-Identifier: AGPL-3.0-or-later
//
// End-to-end IIIF coverage for 1-bit bilevel TIFF support (DEV-6249).
// Complements the unit-level `tiff_bilevel_regression_test.cpp` by
// exercising the full HTTP pipeline from request parsing through to
// pixel output.

mod common;

use common::{client, server};

fn assert_iiif_ok(path: &str) {
    let srv = server();
    let url = format!("{}{}", srv.base_url, path);
    let resp = client()
        .get(&url)
        .send()
        .unwrap_or_else(|e| panic!("GET {} failed: {}", url, e));
    assert_eq!(
        resp.status().as_u16(),
        200,
        "expected 200 OK for {}; got {}",
        url,
        resp.status().as_u16()
    );
}

#[test]
fn bilevel_tiff_info_json() {
    // info.json for a bilevel TIFF must load (was previously rejected at
    // read time by SipiIOTiff.cpp:1192).
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/bilevel/bilevel_lzw_miniswhite.tif/info.json",
            srv.base_url
        ))
        .send()
        .expect("info.json GET failed");
    assert_eq!(resp.status().as_u16(), 200, "info.json must succeed");
    let json: serde_json::Value = resp.json().expect("info.json must be JSON");
    assert_eq!(json["width"], 128);
    assert_eq!(json["height"], 128);
}

#[test]
fn bilevel_tiff_full_default_jpg() {
    // Whole-image conversion through the IIIF pipeline.
    assert_iiif_ok("/bilevel/bilevel_lzw_miniswhite.tif/full/max/0/default.jpg");
}

#[test]
fn bilevel_tiff_region_default_jpg() {
    // ROI extraction — pins the memcpy-offset fix from Phase 2.2.
    assert_iiif_ok("/bilevel/bilevel_roi_test.tif/32,32,64,64/max/0/default.jpg");
}

#[test]
fn bilevel_tiff_minisblack_full_default_jpg() {
    // Both photometric interpretations must render correctly.
    assert_iiif_ok("/bilevel/bilevel_lzw_minisblack.tif/full/max/0/default.jpg");
}

#[test]
fn bilevel_tiff_uncompressed_full_default_jpg() {
    // Both compressions must round-trip.
    assert_iiif_ok("/bilevel/bilevel_none_miniswhite.tif/full/max/0/default.jpg");
}
