//! IIIF Image API 3.0 URL parser (strangler-fig Phase C; ADR-0013).
//!
//! A Rust port of the C++ `iiifparser` (`SipiRegion` / `SipiSize` /
//! `SipiRotation` / `SipiQualityFormat`) + `handlers::iiif_handler::parse_iiif_uri`.
//! It classifies a request URI and, for an IIIF image request, parses the
//! region/size/rotation/quality.format into the flattened [`SipiIiifParams`] the
//! engine's `sipi_serve_image` consumes. This is the D+ "IIIF parser → Rust"
//! slice, pulled into Phase C so the Rust shell owns IIIF parsing (the seam has
//! no parser entry).
//!
//! Parity is gated by the unit tests below + the e2e suite (`proptest_iiif_uri`,
//! `iiif_compliance`) once it runs against the Rust binary (T11).

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

/// Classify a request URI and, for an IIIF image request, parse its params.
/// `uri` is the path portion (scheme/host already stripped), e.g.
/// `/iiif/2/image.jpg/full/200,/0/default.jpg`.
pub fn parse_request(uri: &str) -> Result<ParsedRequest, ParseError> {
    let parts: Vec<String> = uri
        .trim_start_matches('/')
        .split('/')
        .map(urldecode)
        .collect();
    if parts.is_empty() || (parts.len() == 1 && parts[0].is_empty()) {
        return Err(ParseError("No parameters/path given".to_string()));
    }
    let n = parts.len();
    let join_prefix = |upto: usize| parts[..upto].join("/");

    // IIIF image: the last four segments must all be valid IIIF grammar.
    if n >= 5
        && is_valid_region(&parts[n - 4])
        && is_valid_size(&parts[n - 3])
        && is_valid_rotation(&parts[n - 2])
        && is_valid_qualform(&parts[n - 1])
    {
        let params = parse_iiif_params(&parts[n - 4], &parts[n - 3], &parts[n - 2], &parts[n - 1])?;
        return Ok(ParsedRequest {
            kind: RequestKind::Iiif,
            prefix: join_prefix(n - 5),
            identifier: parts[n - 5].clone(),
            params: Some(params),
        });
    }

    let last = &parts[n - 1];

    // info.json / knora.json — last segment split on '.'.
    if let Some(dot) = last.find('.') {
        let (body, ext) = (&last[..dot], &last[dot + 1..]);
        if ext == "json" && (body == "info" || body == "knora") && n >= 2 {
            return Ok(ParsedRequest {
                kind: if body == "info" { RequestKind::InfoJson } else { RequestKind::KnoraJson },
                prefix: join_prefix(n - 2),
                identifier: parts[n - 2].clone(),
                params: None,
            });
        }
    } else if last == "file" && n >= 2 {
        // `…/{id}/file` — raw file download.
        return Ok(ParsedRequest {
            kind: RequestKind::FileDownload,
            prefix: join_prefix(n - 2),
            identifier: parts[n - 2].clone(),
            params: None,
        });
    }

    // Otherwise: a bare identifier → redirect to the canonical info.json.
    Ok(ParsedRequest {
        kind: RequestKind::Redirect,
        prefix: join_prefix(n - 1),
        identifier: last.clone(),
        params: None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classifies_iiif_image() {
        let r = parse_request("/iiif/2/image.jpg/full/200,/0/default.jpg").unwrap();
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
    fn classifies_info_json() {
        let r = parse_request("/iiif/2/image.jpg/info.json").unwrap();
        assert_eq!(r.kind, RequestKind::InfoJson);
        assert_eq!(r.prefix, "iiif/2");
        assert_eq!(r.identifier, "image.jpg");
        assert!(r.params.is_none());
    }

    #[test]
    fn classifies_knora_json() {
        let r = parse_request("/image.jpg/knora.json").unwrap();
        assert_eq!(r.kind, RequestKind::KnoraJson);
        assert_eq!(r.prefix, "");
        assert_eq!(r.identifier, "image.jpg");
    }

    #[test]
    fn classifies_file_download() {
        let r = parse_request("/iiif/audio.mp3/file").unwrap();
        assert_eq!(r.kind, RequestKind::FileDownload);
        assert_eq!(r.prefix, "iiif");
        assert_eq!(r.identifier, "audio.mp3");
    }

    #[test]
    fn classifies_redirect() {
        let r = parse_request("/iiif/image.jpg").unwrap();
        assert_eq!(r.kind, RequestKind::Redirect);
        assert_eq!(r.prefix, "iiif");
        assert_eq!(r.identifier, "image.jpg");
    }

    #[test]
    fn region_forms() {
        assert_eq!(parse_region("full").unwrap().0, SipiRegionType::Full);
        assert_eq!(parse_region("square").unwrap().0, SipiRegionType::Square);
        let (ty, c) = parse_region("10,20,30,40").unwrap();
        assert_eq!(ty, SipiRegionType::Coords);
        assert_eq!(c, [10.0, 20.0, 30.0, 40.0]);
        let (ty, c) = parse_region("pct:0,0,50,50").unwrap();
        assert_eq!(ty, SipiRegionType::Percents);
        assert_eq!(c, [0.0, 0.0, 50.0, 50.0]);
        assert!(parse_region("10,20,30").is_err());
    }

    #[test]
    fn size_forms() {
        assert_eq!(parse_size("max").unwrap().ty, SipiSizeType::Full);
        let s = parse_size("^max").unwrap();
        assert_eq!(s.ty, SipiSizeType::Full);
        assert!(s.upscaling);
        assert_eq!(parse_size("200,").unwrap().ty, SipiSizeType::PixelsX);
        assert_eq!(parse_size(",100").unwrap().ty, SipiSizeType::PixelsY);
        let s = parse_size("200,100").unwrap();
        assert_eq!(s.ty, SipiSizeType::PixelsXy);
        assert_eq!((s.nx, s.ny), (200, 100));
        assert_eq!(parse_size("!200,100").unwrap().ty, SipiSizeType::Maxdim);
        let s = parse_size("pct:50").unwrap();
        assert_eq!(s.ty, SipiSizeType::Percents);
        assert_eq!(s.percent, 50.0);
        assert!(parse_size("!pct:50").is_err());
        assert!(parse_size("0,100").is_err());
    }

    #[test]
    fn rotation_forms() {
        assert_eq!(parse_rotation("").unwrap(), (false, 0.0));
        assert_eq!(parse_rotation("90").unwrap(), (false, 90.0));
        assert_eq!(parse_rotation("!180").unwrap(), (true, 180.0));
    }

    #[test]
    fn quality_format_forms() {
        assert_eq!(
            parse_quality_format("default.jpg").unwrap(),
            (SipiQualityType::Default, SipiFormatType::Jpg)
        );
        assert_eq!(
            parse_quality_format("gray.png").unwrap(),
            (SipiQualityType::Gray, SipiFormatType::Png)
        );
        assert_eq!(
            parse_quality_format("color.jp2").unwrap(),
            (SipiQualityType::Color, SipiFormatType::Jp2)
        );
        assert!(parse_quality_format("bogus.jpg").is_err());
        assert!(parse_quality_format("nodot").is_err());
    }

    #[test]
    fn upscaling_full_iiif_request() {
        let r = parse_request("/p/img.tif/square/^!500,500/!90/color.png").unwrap();
        assert_eq!(r.kind, RequestKind::Iiif);
        let p = r.params.unwrap();
        assert_eq!(p.region_type, SipiRegionType::Square);
        assert_eq!(p.size_type, SipiSizeType::Maxdim);
        assert_eq!(p.size_upscaling, 1);
        assert_eq!(p.rotation_mirror, 1);
        assert_eq!(p.rotation, 90.0);
        assert_eq!(p.quality_type, SipiQualityType::Color);
        assert_eq!(p.format_type, SipiFormatType::Png);
    }
}
