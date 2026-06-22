//! IIIF Image API 3.0 URL parser (strangler-fig rewrite; ADR-0013).
//!
//! A Rust port of the C++ `iiifparser` (`SipiRegion` / `SipiSize` /
//! `SipiRotation` / `SipiQualityFormat`) + `handlers::iiif_handler::parse_iiif_uri`.
//! It classifies a request URI and, for an IIIF image request, parses the
//! region/size/rotation/quality.format into the flattened [`SipiIiifParams`] the
//! engine's `sipi_serve_image` consumes. The Rust shell owns IIIF parsing (the
//! seam has no parser entry).
//!
//! Parity is gated by the unit tests below + the e2e suite (`proptest_iiif_uri`,
//! `iiif_compliance`) once it runs against the Rust binary.

use crate::ffi::{SipiFormatType, SipiIiifParams, SipiQualityType, SipiRegionType, SipiSizeType};

/// A parse failure → HTTP 400 at the edge (mirrors the C++ `SipiError` → 400).
#[derive(Debug, PartialEq, Eq)]
pub struct ParseError(pub String);

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
    pub params: Option<SipiIiifParams>,
}

// ── Low-level grammar consumers (mirror iiif_handler.cpp:21–48) ──────────────

/// Consume `digit+`; return the remainder, or `None` if no leading digit.
fn consume_posint(s: &str) -> Option<&str> {
    let n = s.find(|c: char| !c.is_ascii_digit()).unwrap_or(s.len());
    if n == 0 {
        None
    } else {
        Some(&s[n..])
    }
}

/// Consume `digit+ ('.' digit+)?`; return the remainder, or `None`.
fn consume_posfloat(s: &str) -> Option<&str> {
    let rest = consume_posint(s)?;
    match rest.strip_prefix('.') {
        Some(after_dot) => consume_posint(after_dot),
        None => Some(rest),
    }
}

// ── Validators (gate classification; mirror iiif_handler.cpp:52–113) ─────────

fn is_valid_region(s: &str) -> bool {
    if s == "full" || s == "square" {
        return true;
    }
    let is_pct = s.starts_with("pct:");
    let mut rest = s.strip_prefix("pct:").unwrap_or(s);
    for i in 0..4 {
        if i > 0 {
            rest = match rest.strip_prefix(',') {
                Some(r) => r,
                None => return false,
            };
        }
        rest = match if is_pct { consume_posfloat(rest) } else { consume_posint(rest) } {
            Some(r) => r,
            None => return false,
        };
    }
    rest.is_empty()
}

fn is_valid_size(s: &str) -> bool {
    let s = s.strip_prefix('^').unwrap_or(s);
    if s == "max" {
        return true;
    }
    if let Some(p) = s.strip_prefix("pct:") {
        return consume_posfloat(p).is_some_and(str::is_empty);
    }
    let (has_bang, s) = match s.strip_prefix('!') {
        Some(r) => (true, r),
        None => (false, s),
    };
    if let Some(r) = s.strip_prefix(',') {
        // ",h": only valid without the "!" fit-in-box prefix.
        if has_bang {
            return false;
        }
        return consume_posint(r).is_some_and(str::is_empty);
    }
    let r = match consume_posint(s) {
        Some(r) => r,
        None => return false,
    };
    let r = match r.strip_prefix(',') {
        Some(r) => r,
        None => return false,
    };
    if r.is_empty() {
        // "w,": valid only without "!".
        return !has_bang;
    }
    consume_posint(r).is_some_and(str::is_empty)
}

fn is_valid_rotation(s: &str) -> bool {
    let s = s.strip_prefix('!').unwrap_or(s);
    consume_posfloat(s).is_some_and(str::is_empty)
}

fn is_valid_qualform(s: &str) -> bool {
    const QUALITIES: [&str; 4] = ["color", "gray", "bitonal", "default"];
    const FORMATS: [&str; 4] = ["jpg", "tif", "png", "jp2"];
    for q in QUALITIES {
        if let Some(rest) = s.strip_prefix(q) {
            return rest
                .strip_prefix('.')
                .is_some_and(|fmt| FORMATS.contains(&fmt));
        }
    }
    false
}

// ── Parsers (mirror the iiifparser constructors) ─────────────────────────────

fn parse_region(s: &str) -> Result<(SipiRegionType, [f32; 4]), ParseError> {
    if s == "full" {
        return Ok((SipiRegionType::Full, [0.0; 4]));
    }
    if s == "square" {
        return Ok((SipiRegionType::Square, [0.0; 4]));
    }
    let (ty, body) = match s.strip_prefix("pct:") {
        Some(b) => (SipiRegionType::Percents, b),
        None => (SipiRegionType::Coords, s),
    };
    let nums: Vec<&str> = body.split(',').collect();
    if nums.len() != 4 {
        return Err(ParseError(format!("IIIF Error reading Region parameter \"{s}\"")));
    }
    let mut coords = [0.0f32; 4];
    for (i, n) in nums.iter().enumerate() {
        coords[i] = n
            .parse::<f32>()
            .map_err(|_| ParseError(format!("IIIF Error reading Region parameter \"{s}\"")))?;
    }
    Ok((ty, coords))
}

/// Parsed size fields, in `SipiIiifParams` shape.
struct SizeParts {
    ty: SipiSizeType,
    upscaling: bool,
    percent: f32,
    reduce: i32,
    nx: usize,
    ny: usize,
}

fn parse_size(s: &str) -> Result<SizeParts, ParseError> {
    let err = || ParseError(format!("Invalid IIIF size parameter: \"{s}\""));
    let mut parts = SizeParts {
        ty: SipiSizeType::Undefined,
        upscaling: false,
        percent: 0.0,
        reduce: 0,
        nx: 0,
        ny: 0,
    };

    let mut rest = s;
    if let Some(r) = rest.strip_prefix('^') {
        parts.upscaling = true;
        rest = r;
    }
    let mut exclamation = false;
    if let Some(r) = rest.strip_prefix('!') {
        exclamation = true;
        rest = r;
    }

    if rest == "max" || rest.is_empty() {
        parts.ty = SipiSizeType::Full;
        return Ok(parts);
    }
    if let Some(p) = rest.strip_prefix("pct:") {
        if exclamation {
            return Err(err());
        }
        let mut pct = p.parse::<f32>().map_err(|_| err())?;
        if pct <= 0.000_000_000_001 {
            pct = 1.0;
        }
        parts.ty = SipiSizeType::Percents;
        parts.percent = pct;
        return Ok(parts);
    }
    // `red:` is unreachable via classification — neither is_valid_size here nor
    // the C++ validator admits it — but SipiSize.cpp carries the same branch, so
    // it is kept for port fidelity.
    if let Some(p) = rest.strip_prefix("red:") {
        if exclamation {
            return Err(err());
        }
        let mut red = p.parse::<i32>().map_err(|_| err())?;
        if red < 0 {
            red = 0;
        }
        parts.ty = SipiSizeType::Reduce;
        parts.reduce = red;
        return Ok(parts);
    }

    let comma = rest
        .find(',')
        .ok_or_else(|| ParseError(format!("Could not parse IIIF size parameter: \"{s}\"")))?;
    let width_str = &rest[..comma];
    let height_str = &rest[comma + 1..];
    let parse_dim = |v: &str| {
        v.parse::<usize>()
            .map_err(|_| ParseError(format!("Could not parse IIIF size parameter: \"{s}\"")))
    };

    if width_str.is_empty() {
        // ",h"
        if exclamation {
            return Err(err());
        }
        let ny = parse_dim(height_str)?;
        if ny == 0 {
            return Err(ParseError(format!("IIIF size height cannot be zero: \"{s}\"")));
        }
        parts.ty = SipiSizeType::PixelsY;
        parts.ny = ny;
    } else if height_str.is_empty() {
        // "w,"
        let nx = parse_dim(width_str)?;
        if nx == 0 {
            return Err(ParseError(format!("IIIF size width cannot be zero: \"{s}\"")));
        }
        parts.ty = SipiSizeType::PixelsX;
        parts.nx = nx;
    } else {
        // "w,h"
        let nx = parse_dim(width_str)?;
        let ny = parse_dim(height_str)?;
        if nx == 0 || ny == 0 {
            return Err(ParseError(format!("IIIF size cannot be zero: \"{s}\"")));
        }
        parts.ty = if exclamation { SipiSizeType::Maxdim } else { SipiSizeType::PixelsXy };
        parts.nx = nx;
        parts.ny = ny;
    }

    // Mirror the C++ hard cap on requested dimensions.
    parts.nx = parts.nx.min(32_000);
    parts.ny = parts.ny.min(32_000);
    Ok(parts)
}

fn parse_rotation(s: &str) -> Result<(bool, f32), ParseError> {
    if s.is_empty() {
        return Ok((false, 0.0));
    }
    let (mirror, body) = match s.strip_prefix('!') {
        Some(r) => (true, r),
        None => (false, s),
    };
    let angle = body
        .parse::<f32>()
        .map_err(|_| ParseError(format!("Could not parse IIIF rotation parameter: {s}")))?;
    Ok((mirror, angle))
}

fn parse_quality_format(s: &str) -> Result<(SipiQualityType, SipiFormatType), ParseError> {
    if s.is_empty() {
        return Ok((SipiQualityType::Default, SipiFormatType::Jpg));
    }
    let dot = s
        .find('.')
        .ok_or_else(|| ParseError(format!("IIIF Error reading Quality+Format parameter \"{s}\" !")))?;
    let quality = match &s[..dot] {
        "default" => SipiQualityType::Default,
        "color" => SipiQualityType::Color,
        "gray" => SipiQualityType::Gray,
        "bitonal" => SipiQualityType::Bitonal,
        q => return Err(ParseError(format!("IIIF Error reading Quality parameter \"{q}\" !"))),
    };
    let format = match &s[dot + 1..] {
        "jpg" => SipiFormatType::Jpg,
        "tif" => SipiFormatType::Tif,
        "png" => SipiFormatType::Png,
        "gif" => SipiFormatType::Gif,
        "jp2" => SipiFormatType::Jp2,
        "pdf" => SipiFormatType::Pdf,
        "webp" => SipiFormatType::Webp,
        _ => SipiFormatType::Unsupported,
    };
    Ok((quality, format))
}

/// Build the flattened `SipiIiifParams` from the four IIIF path segments.
fn parse_iiif_params(region: &str, size: &str, rotation: &str, qualform: &str) -> Result<SipiIiifParams, ParseError> {
    let (region_type, region) = parse_region(region)?;
    let sz = parse_size(size)?;
    let (mirror, angle) = parse_rotation(rotation)?;
    let (quality_type, format_type) = parse_quality_format(qualform)?;
    Ok(SipiIiifParams {
        region_type,
        region,
        size_type: sz.ty,
        size_upscaling: i32::from(sz.upscaling),
        size_percent: sz.percent,
        size_reduce: sz.reduce,
        size_nx: sz.nx,
        size_ny: sz.ny,
        rotation: angle,
        rotation_mirror: i32::from(mirror),
        quality_type,
        format_type,
    })
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

/// Classify a request URI and, for an IIIF image request, parse its params. A
/// faithful port of `handlers::iiif_handler::parse_iiif_uri`
/// (src/handlers/iiif_handler.cpp:148-376), including its reject-vs-redirect
/// logic: a URL that *looks* like a (malformed) IIIF image request — a valid
/// quality.format tail, or region+size+rotation valid — is an **error**, not a
/// redirect. `uri` is the path portion (scheme/host already stripped).
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
            let params = parse_iiif_params(&parts[n - 4], &parts[n - 3], &parts[n - 2], &parts[n - 1])?;
            Ok(ParsedRequest { kind: RequestKind::Iiif, prefix, identifier: parts[n - 5].clone(), params: Some(params) })
        } else if body == "info" && ext == "json" {
            let prefix = if n >= 3 {
                parts[..n - 2].join("/")
            } else if n == 2 {
                String::new()
            } else {
                return Err(malformed());
            };
            Ok(ParsedRequest { kind: RequestKind::InfoJson, prefix, identifier: parts[n - 2].clone(), params: None })
        } else if body == "knora" && ext == "json" {
            let prefix = if n >= 3 {
                parts[..n - 2].join("/")
            } else if n == 2 {
                String::new()
            } else {
                return Err(malformed());
            };
            Ok(ParsedRequest { kind: RequestKind::KnoraJson, prefix, identifier: parts[n - 2].clone(), params: None })
        } else {
            // A dotted last segment that is neither a valid IIIF tail nor
            // info/knora.json. If it *looks* like an IIIF request, it is
            // malformed (not a redirect): a valid quality.format tail, or the
            // other three IIIF parts valid.
            if qualform_ok {
                return Err(malformed());
            }
            if rotation_ok && size_ok && region_ok {
                return Err(ParseError(format!("IIIF url not correctly formatted: Error in quality: \"{last}\"!")));
            }
            let prefix = build_prefix_strict(&parts[..n - 1])?;
            Ok(ParsedRequest { kind: RequestKind::Redirect, prefix, identifier: last.clone(), params: None })
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
        Ok(ParsedRequest { kind: RequestKind::FileDownload, prefix, identifier: parts[n - 2].clone(), params: None })
    } else {
        // A bare (dot-less) last segment. Same reject-vs-redirect rule: if the
        // other three IIIF parts are valid it is a malformed IIIF request.
        if rotation_ok && size_ok && region_ok {
            return Err(ParseError(format!("IIIF url not correctly formatted: Error in quality: \"{last}\"!")));
        }
        let prefix = build_prefix_strict(&parts[..n - 1])?;
        Ok(ParsedRequest { kind: RequestKind::Redirect, prefix, identifier: last.clone(), params: None })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Classification goldens ported 1:1 from the C++ parity corpus
    // (test/unit/handlers/iiif_handler_test.cpp). The C++ is the parity target,
    // so divergence here is a defect by the module's own contract.

    fn ok(uri: &str) -> ParsedRequest {
        parse_request(uri).unwrap_or_else(|e| panic!("{uri} should parse: {}", e.0))
    }
    fn iiif_form(uri: &str) {
        assert_eq!(parse_request(uri).map(|r| r.kind), Ok(RequestKind::Iiif), "form rejected: {uri}");
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
        assert_eq!(p.region_type, SipiRegionType::Full);
        assert_eq!(p.size_type, SipiSizeType::PixelsX);
        assert_eq!(p.size_nx, 200);
        assert_eq!(p.size_ny, 0);
        assert_eq!(p.quality_type, SipiQualityType::Default);
        assert_eq!(p.format_type, SipiFormatType::Jpg);
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
            ("/iiif/3/4/uniqueImageIdentifier", "iiif/3/4", "uniqueImageIdentifier"),
            ("/prefix/path/to/image", "prefix/path/to", "image"),
            ("/iiif/3/special%2Fchars%3Fhere", "iiif/3", "special/chars?here"),
            ("/0812/3KtDiJm4XxY-1PUUCffsF4S.jpx", "0812", "3KtDiJm4XxY-1PUUCffsF4S.jpx"),
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
        for region in ["full", "square", "0,0,100,100", "10,20,300,400", "pct:25.5,25.5,50.0,50.0", "pct:0,0,100,100"] {
            iiif_form(&format!("/p/img.jp2/{region}/max/0/default.jpg"));
        }
    }

    #[test]
    fn size_forms_accepted() {
        for size in ["max", "pct:50", "pct:50.5", "100,", ",100", "100,100", "!100,100"] {
            iiif_form(&format!("/p/img.jp2/full/{size}/0/default.jpg"));
        }
        for size in ["^max", "^pct:150", "^200,", "^,200", "^200,200", "^!200,200"] {
            iiif_form(&format!("/p/img.jp2/full/{size}/0/default.jpg"));
        }
    }

    #[test]
    fn rotation_forms_accepted() {
        for rot in ["0", "90", "180", "270", "45.5", "359.999", "!90", "!180", "!0.5"] {
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
        for r in ["1,2,3", "1,2,3,4,5", "pct:1,2,3", "pct:1,2,3,4,5", "abc", "pct:", ",,,"] {
            rejected(&format!("/p/img.jp2/{r}/max/0/default.jpg"));
        }
    }

    #[test]
    fn info_knora_file_classification() {
        let info = ok("/p/img.jp2/info.json");
        assert_eq!(info.kind, RequestKind::InfoJson);
        assert_eq!((info.prefix.as_str(), info.identifier.as_str()), ("p", "img.jp2"));
        let knora = ok("/p/img.jp2/knora.json");
        assert_eq!(knora.kind, RequestKind::KnoraJson);
        assert_eq!((knora.prefix.as_str(), knora.identifier.as_str()), ("p", "img.jp2"));
        let file = ok("/p/img.jp2/file");
        assert_eq!(file.kind, RequestKind::FileDownload);
        assert_eq!((file.prefix.as_str(), file.identifier.as_str()), ("p", "img.jp2"));
    }

    #[test]
    fn trailing_slash_iiif_still_serves() {
        // Regression: a trailing '/' must not demote a valid IIIF request to a redirect.
        let r = ok("/p/img.jp2/full/max/0/default.jpg/");
        assert_eq!(r.kind, RequestKind::Iiif);
        assert_eq!(r.identifier, "img.jp2");
    }

    // ── Param-level flattening checks ──

    #[test]
    fn region_param_values() {
        assert_eq!(parse_region("full").unwrap().0, SipiRegionType::Full);
        assert_eq!(parse_region("square").unwrap().0, SipiRegionType::Square);
        let (ty, c) = parse_region("10,20,30,40").unwrap();
        assert_eq!(ty, SipiRegionType::Coords);
        assert_eq!(c, [10.0, 20.0, 30.0, 40.0]);
        let (ty, c) = parse_region("pct:0,0,50,50").unwrap();
        assert_eq!(ty, SipiRegionType::Percents);
        assert_eq!(c, [0.0, 0.0, 50.0, 50.0]);
    }

    #[test]
    fn size_param_values() {
        assert_eq!(parse_size("max").unwrap().ty, SipiSizeType::Full);
        assert!(parse_size("^max").unwrap().upscaling);
        assert_eq!(parse_size("200,").unwrap().ty, SipiSizeType::PixelsX);
        assert_eq!(parse_size(",100").unwrap().ty, SipiSizeType::PixelsY);
        let s = parse_size("200,100").unwrap();
        assert_eq!((s.ty, s.nx, s.ny), (SipiSizeType::PixelsXy, 200, 100));
        assert_eq!(parse_size("!200,100").unwrap().ty, SipiSizeType::Maxdim);
        assert_eq!(parse_size("pct:50").unwrap().percent, 50.0);
    }

    #[test]
    fn rotation_param_values() {
        assert_eq!(parse_rotation("").unwrap(), (false, 0.0));
        assert_eq!(parse_rotation("90").unwrap(), (false, 90.0));
        assert_eq!(parse_rotation("!180").unwrap(), (true, 180.0));
    }

    #[test]
    fn quality_format_param_values() {
        assert_eq!(parse_quality_format("default.jpg").unwrap(), (SipiQualityType::Default, SipiFormatType::Jpg));
        assert_eq!(parse_quality_format("gray.png").unwrap(), (SipiQualityType::Gray, SipiFormatType::Png));
        assert_eq!(parse_quality_format("color.jp2").unwrap(), (SipiQualityType::Color, SipiFormatType::Jp2));
    }

    #[test]
    fn upscaling_maxdim_mirror_full_request() {
        let r = ok("/p/img.tif/square/^!500,500/!90/color.png");
        let p = r.params.unwrap();
        assert_eq!(p.region_type, SipiRegionType::Square);
        assert_eq!(p.size_type, SipiSizeType::Maxdim);
        assert_eq!(p.size_upscaling, 1);
        assert_eq!(p.rotation_mirror, 1);
        assert_eq!(p.rotation, 90.0);
        assert_eq!((p.quality_type, p.format_type), (SipiQualityType::Color, SipiFormatType::Png));
    }
}
