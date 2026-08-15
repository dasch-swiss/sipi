//! Request classification: tokenize an IIIF URL path and, for an image request,
//! parse its `{region}/{size}/{rotation}/{quality}.{format}` into
//! [`IiifParams`]. A faithful port of `handlers::iiif_handler::parse_iiif_uri`
//! (`//src/iiifparser/cpp/classifier`).

use crate::domain::{IiifParams, ParseError};
use crate::parse::{
    is_valid_qualform, is_valid_region, is_valid_rotation, is_valid_size, parse_iiif_params,
};

/// IIIF request classification (`handlers::iiif_handler::RequestType`).
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum RequestKind {
    /// `…/{region}/{size}/{rotation}/{quality}.{format}` — an image request.
    Iiif,
    /// `…/info.json`.
    InfoJson,
    /// `…/knora.json`.
    KnoraJson,
    /// Bare `…/{id}` — redirect to the canonical info.json.
    Redirect,
    /// `…/{id}/file` — raw file download.
    FileDownload,
}

/// A classified and (for [`RequestKind::Iiif`]) parsed request.
#[derive(Debug)]
pub struct ParsedRequest {
    pub kind: RequestKind,
    pub prefix: String,
    pub identifier: String,
    /// `Some` only for [`RequestKind::Iiif`].
    pub params: Option<IiifParams>,
}

/// Percent-decode a single path segment (the C++ urldecodes each part).
fn urldecode(s: &str) -> String {
    percent_encoding::percent_decode_str(s)
        .decode_utf8_lossy()
        .into_owned()
}

/// "IIIF url not correctly formatted" — the C++ catch-all parse error. Most
/// callers only check Ok/Err, not the message; only "No parameters/path given"
/// is asserted by a test, so the other strings need not match verbatim.
fn malformed() -> ParseError {
    ParseError("IIIF url not correctly formatted".to_string())
}

/// Tokenize the URI exactly as the C++ `parse_iiif_uri` does (iiif_handler.cpp:165-176):
/// split on '/', URL-decode each segment, **skip the leading empty segment** from a
/// leading '/', and **drop the trailing empty segment** from a trailing '/' — but keep
/// interior empty segments (so `//2/` → `["", "2"]`, which the classifier then rejects).
fn tokenize(uri: &str) -> Vec<String> {
    // The C++ tokenizer yields no parts for an empty URI (Rust's "".split('/')
    // would otherwise yield one empty segment).
    if uri.is_empty() {
        return Vec::new();
    }
    let mut segs: Vec<&str> = uri.split('/').collect();
    if uri.starts_with('/') && !segs.is_empty() {
        segs.remove(0);
    }
    if uri.ends_with('/') && segs.last().is_some_and(|s| s.is_empty()) {
        segs.pop();
    }
    segs.into_iter().map(urldecode).collect()
}

/// Join `parts` into a prefix, erroring on any empty component — the C++
/// redirect-branch prefix build (iiif_handler.cpp:288-294 / 357-363). An empty
/// slice yields an empty prefix (the no-prefix case).
fn build_prefix_strict(parts: &[String]) -> Result<String, ParseError> {
    let mut prefix = String::new();
    for p in parts {
        if p.is_empty() {
            return Err(malformed());
        }
        if !prefix.is_empty() {
            prefix.push('/');
        }
        prefix.push_str(p);
    }
    Ok(prefix)
}

/// Classify a request URI and, for an IIIF image request, parse its params.
/// Including the reject-vs-redirect logic: a URL that *looks* like a (malformed)
/// IIIF image request — a valid quality.format tail, or region+size+rotation
/// valid — is an **error**, not a redirect. `uri` is the path portion
/// (scheme/host already stripped).
pub fn parse_request(uri: &str) -> Result<ParsedRequest, ParseError> {
    let parts = tokenize(uri);
    if parts.is_empty() {
        return Err(ParseError("No parameters/path given".to_string()));
    }
    let n = parts.len();

    let qualform_ok = is_valid_qualform(&parts[n - 1]);
    let rotation_ok = n > 1 && is_valid_rotation(&parts[n - 2]);
    let size_ok = n > 2 && is_valid_size(&parts[n - 3]);
    let region_ok = n > 3 && is_valid_region(&parts[n - 4]);

    let last = &parts[n - 1];

    if let Some(dot) = last.find('.') {
        let body = &last[..dot];
        let ext = &last[dot + 1..];

        if qualform_ok && rotation_ok && size_ok && region_ok {
            // A valid IIIF image request.
            let prefix = if n >= 6 {
                parts[..n - 5].join("/")
            } else if n == 5 {
                String::new()
            } else {
                return Err(malformed());
            };
            let params =
                parse_iiif_params(&parts[n - 4], &parts[n - 3], &parts[n - 2], &parts[n - 1])?;
            Ok(ParsedRequest {
                kind: RequestKind::Iiif,
                prefix,
                identifier: parts[n - 5].clone(),
                params: Some(params),
            })
        } else if body == "info" && ext == "json" {
            let prefix = if n >= 3 {
                parts[..n - 2].join("/")
            } else if n == 2 {
                String::new()
            } else {
                return Err(malformed());
            };
            Ok(ParsedRequest {
                kind: RequestKind::InfoJson,
                prefix,
                identifier: parts[n - 2].clone(),
                params: None,
            })
        } else if body == "knora" && ext == "json" {
            let prefix = if n >= 3 {
                parts[..n - 2].join("/")
            } else if n == 2 {
                String::new()
            } else {
                return Err(malformed());
            };
            Ok(ParsedRequest {
                kind: RequestKind::KnoraJson,
                prefix,
                identifier: parts[n - 2].clone(),
                params: None,
            })
        } else {
            // A dotted last segment that is neither a valid IIIF tail nor
            // info/knora.json. If it *looks* like an IIIF request, it is
            // malformed (not a redirect): a valid quality.format tail, or the
            // other three IIIF parts valid.
            if qualform_ok {
                return Err(malformed());
            }
            if rotation_ok && size_ok && region_ok {
                return Err(ParseError(format!(
                    "IIIF url not correctly formatted: Error in quality: \"{last}\"!"
                )));
            }
            let prefix = build_prefix_strict(&parts[..n - 1])?;
            Ok(ParsedRequest {
                kind: RequestKind::Redirect,
                prefix,
                identifier: last.clone(),
                params: None,
            })
        }
    } else if last == "file" {
        // `…/{id}/file` — raw file download.
        let prefix = if n >= 3 {
            parts[..n - 2].join("/")
        } else if n == 2 {
            String::new()
        } else {
            return Err(malformed());
        };
        Ok(ParsedRequest {
            kind: RequestKind::FileDownload,
            prefix,
            identifier: parts[n - 2].clone(),
            params: None,
        })
    } else {
        // A bare (dot-less) last segment. Same reject-vs-redirect rule: if the
        // other three IIIF parts are valid it is a malformed IIIF request.
        if rotation_ok && size_ok && region_ok {
            return Err(ParseError(format!(
                "IIIF url not correctly formatted: Error in quality: \"{last}\"!"
            )));
        }
        let prefix = build_prefix_strict(&parts[..n - 1])?;
        Ok(ParsedRequest {
            kind: RequestKind::Redirect,
            prefix,
            identifier: last.clone(),
            params: None,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::{FormatKind, QualityKind, RegionKind, SizeKind};

    // Classification goldens ported 1:1 from the C++ classifier corpus
    // (//src/iiifparser/cpp/classifier). The C++ classifier is the reference, so
    // divergence here is a defect by the module's own contract.

    fn ok(uri: &str) -> ParsedRequest {
        parse_request(uri).unwrap_or_else(|e| panic!("{uri} should parse: {}", e.0))
    }
    fn iiif_form(uri: &str) {
        assert_eq!(
            parse_request(uri).map(|r| r.kind),
            Ok(RequestKind::Iiif),
            "form rejected: {uri}"
        );
    }
    fn rejected(uri: &str) {
        assert!(parse_request(uri).is_err(), "must be rejected: {uri}");
    }

    #[test]
    fn canonical_iiif_url() {
        let r = ok("/iiif/2/image.jpg/full/200,/0/default.jpg");
        assert_eq!(r.kind, RequestKind::Iiif);
        assert_eq!(r.prefix, "iiif/2");
        assert_eq!(r.identifier, "image.jpg");
        let p = r.params.unwrap();
        assert_eq!(p.region_kind, RegionKind::Full);
        assert_eq!(p.size_kind, SizeKind::PixelsX);
        assert_eq!(p.size_nx, 200);
        assert_eq!(p.size_ny, 0);
        assert_eq!(p.quality_kind, QualityKind::Default);
        assert_eq!(p.format_kind, FormatKind::Jpg);
    }

    #[test]
    fn empty_uri_is_error() {
        assert_eq!(parse_request("").unwrap_err().0, "No parameters/path given");
    }

    #[test]
    fn redirect_cases() {
        // (uri, expected prefix, expected identifier) — incl. percent-decoding.
        let cases = [
            ("/2", "", "2"),
            ("/iiif/3", "iiif", "3"),
            ("/iiif/3/image1", "iiif/3", "image1"),
            ("/prefix/12345", "prefix", "12345"),
            ("/collections/item123", "collections", "item123"),
            ("/iiif/v2/abcd1234", "iiif/v2", "abcd1234"),
            (
                "/iiif/3/4/uniqueImageIdentifier",
                "iiif/3/4",
                "uniqueImageIdentifier",
            ),
            ("/prefix/path/to/image", "prefix/path/to", "image"),
            (
                "/iiif/3/special%2Fchars%3Fhere",
                "iiif/3",
                "special/chars?here",
            ),
            (
                "/0812/3KtDiJm4XxY-1PUUCffsF4S.jpx",
                "0812",
                "3KtDiJm4XxY-1PUUCffsF4S.jpx",
            ),
        ];
        for (uri, prefix, id) in cases {
            let r = ok(uri);
            assert_eq!(r.kind, RequestKind::Redirect, "{uri}");
            assert_eq!(r.prefix, prefix, "{uri}");
            assert_eq!(r.identifier, id, "{uri}");
        }
    }

    #[test]
    fn invalid_uris_rejected() {
        // The reject-vs-redirect cases: a URL that *looks* like a malformed IIIF
        // request (or has an empty path component) must error, not redirect.
        for uri in [
            "/",
            "//2/",
            "/unit//lena512.jp2",
            "/unit/lena512.jp2/max/0/default.jpg",
            "/unit/lena512.jp2/full/max/default.jpg",
            "/unit/lena512.jp2/full/max/!/default.jpg",
            "/unit/lena512.jp2/full/max/0/jpg",
            "/knora/67352ccc-d1b0-11e1-89ae-279075081939.jp2/full/max/0/default.aN",
            "/knora/67352ccc-d1b0-11e1-89ae-279075081939.jp2/full/max/0/BFTP=w.jpg",
        ] {
            rejected(uri);
        }
    }

    #[test]
    fn region_forms_accepted() {
        for region in [
            "full",
            "square",
            "0,0,100,100",
            "10,20,300,400",
            "pct:25.5,25.5,50.0,50.0",
            "pct:0,0,100,100",
        ] {
            iiif_form(&format!("/p/img.jp2/{region}/max/0/default.jpg"));
        }
    }

    #[test]
    fn size_forms_accepted() {
        for size in [
            "max", "pct:50", "pct:50.5", "100,", ",100", "100,100", "!100,100",
        ] {
            iiif_form(&format!("/p/img.jp2/full/{size}/0/default.jpg"));
        }
        for size in [
            "^max",
            "^pct:150",
            "^200,",
            "^,200",
            "^200,200",
            "^!200,200",
        ] {
            iiif_form(&format!("/p/img.jp2/full/{size}/0/default.jpg"));
        }
    }

    #[test]
    fn rotation_forms_accepted() {
        for rot in [
            "0", "90", "180", "270", "45.5", "359.999", "!90", "!180", "!0.5",
        ] {
            iiif_form(&format!("/p/img.jp2/full/max/{rot}/default.jpg"));
        }
    }

    #[test]
    fn quality_format_combinations_accepted() {
        for q in ["color", "gray", "bitonal", "default"] {
            for f in ["jpg", "tif", "png", "jp2"] {
                iiif_form(&format!("/p/img.jp2/full/max/0/{q}.{f}"));
            }
        }
    }

    #[test]
    fn reject_unsupported_formats() {
        for f in ["gif", "webp", "pdf", "bmp", "svg"] {
            rejected(&format!("/p/img.jp2/full/max/0/default.{f}"));
        }
    }

    #[test]
    fn reject_signed_rotations() {
        // IIIF 3.0 §5.1.1: rotations are decimal digits + '.' only — no sign.
        for r in ["+0", "+90", "-90", "-180.5", "+45.5"] {
            rejected(&format!("/p/img.jp2/full/max/{r}/default.jpg"));
        }
    }

    #[test]
    fn reject_empty_size_fields() {
        for s in [",", "pct:", "^,", "^pct:", "!,"] {
            rejected(&format!("/p/img.jp2/full/{s}/0/default.jpg"));
        }
    }

    #[test]
    fn reject_malformed_posfloat_rotation() {
        for r in [".5", "0.", ".", "1..2", "abc"] {
            rejected(&format!("/p/img.jp2/full/max/{r}/default.jpg"));
        }
    }

    #[test]
    fn reject_malformed_region() {
        for r in [
            "1,2,3",
            "1,2,3,4,5",
            "pct:1,2,3",
            "pct:1,2,3,4,5",
            "abc",
            "pct:",
            ",,,",
        ] {
            rejected(&format!("/p/img.jp2/{r}/max/0/default.jpg"));
        }
    }

    #[test]
    fn info_knora_file_classification() {
        let info = ok("/p/img.jp2/info.json");
        assert_eq!(info.kind, RequestKind::InfoJson);
        assert_eq!(
            (info.prefix.as_str(), info.identifier.as_str()),
            ("p", "img.jp2")
        );
        let knora = ok("/p/img.jp2/knora.json");
        assert_eq!(knora.kind, RequestKind::KnoraJson);
        assert_eq!(
            (knora.prefix.as_str(), knora.identifier.as_str()),
            ("p", "img.jp2")
        );
        let file = ok("/p/img.jp2/file");
        assert_eq!(file.kind, RequestKind::FileDownload);
        assert_eq!(
            (file.prefix.as_str(), file.identifier.as_str()),
            ("p", "img.jp2")
        );
    }

    #[test]
    fn trailing_slash_iiif_still_serves() {
        // Regression: a trailing '/' must not demote a valid IIIF request to a redirect.
        let r = ok("/p/img.jp2/full/max/0/default.jpg/");
        assert_eq!(r.kind, RequestKind::Iiif);
        assert_eq!(r.identifier, "img.jp2");
    }

    #[test]
    fn upscaling_maxdim_mirror_full_request() {
        let r = ok("/p/img.tif/square/^!500,500/!90/color.png");
        let p = r.params.unwrap();
        assert_eq!(p.region_kind, RegionKind::Square);
        assert_eq!(p.size_kind, SizeKind::Maxdim);
        assert!(p.size_upscaling);
        assert!(p.rotation_mirror);
        assert_eq!(p.rotation, 90.0);
        assert_eq!(
            (p.quality_kind, p.format_kind),
            (QualityKind::Color, FormatKind::Png)
        );
    }
}
