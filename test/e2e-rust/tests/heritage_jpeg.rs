// Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
// contributors. SPDX-License-Identifier: AGPL-3.0-or-later
//
// End-to-end IIIF coverage for the heritage JPEG files unblocked by
// DEV-6250 (Photoshop-CS APP13-before-APP1 + German umlauts in IPTC +
// resilient metadata parsing).

mod common;

use common::{client, server};

#[test]
fn heritage_jpeg_o_full_default_jpg() {
    // Full IIIF pipeline read + conversion for the -o variant.
    let srv = server();
    let url = format!("{}/jpeg/35-2421d-o.jpg/full/max/0/default.jpg", srv.base_url);
    let resp = client()
        .get(&url)
        .send()
        .unwrap_or_else(|e| panic!("GET {} failed: {}", url, e));
    assert_eq!(resp.status().as_u16(), 200, "heritage JPEG must serve");
}

#[test]
fn heritage_jpeg_r_info_json() {
    // Sibling variant — info.json is the minimum contract.
    let srv = server();
    let resp = client()
        .get(format!("{}/jpeg/35-2421d-r.jpg/info.json", srv.base_url))
        .send()
        .expect("info.json GET failed");
    assert_eq!(resp.status().as_u16(), 200);
}
