//! Grammar consumers, per-segment validators, and the component parsers that
//! turn a validated `{region}/{size}/{rotation}/{quality}.{format}` into
//! [`IiifParams`]. The validators gate classification (mirror
//! `iiif_handler.cpp:52-113`); the parsers mirror the C++ value-object
//! constructors (`//src/iiifparser/cpp/value_objects`).

use crate::domain::{FormatKind, IiifParams, ParseError, QualityKind, RegionKind, SizeKind};

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

pub(crate) fn is_valid_region(s: &str) -> bool {
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
        rest = match if is_pct {
            consume_posfloat(rest)
        } else {
            consume_posint(rest)
        } {
            Some(r) => r,
            None => return false,
        };
    }
    rest.is_empty()
}

pub(crate) fn is_valid_size(s: &str) -> bool {
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

pub(crate) fn is_valid_rotation(s: &str) -> bool {
    let s = s.strip_prefix('!').unwrap_or(s);
    consume_posfloat(s).is_some_and(str::is_empty)
}

pub(crate) fn is_valid_qualform(s: &str) -> bool {
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

// ── Parsers (mirror the value-object constructors) ───────────────────────────

fn parse_region(s: &str) -> Result<(RegionKind, [f32; 4]), ParseError> {
    if s == "full" {
        return Ok((RegionKind::Full, [0.0; 4]));
    }
    if s == "square" {
        return Ok((RegionKind::Square, [0.0; 4]));
    }
    let (kind, body) = match s.strip_prefix("pct:") {
        Some(b) => (RegionKind::Percents, b),
        None => (RegionKind::Coords, s),
    };
    let nums: Vec<&str> = body.split(',').collect();
    if nums.len() != 4 {
        return Err(ParseError(format!(
            "IIIF Error reading Region parameter \"{s}\""
        )));
    }
    let mut coords = [0.0f32; 4];
    for (i, n) in nums.iter().enumerate() {
        coords[i] = n
            .parse::<f32>()
            .map_err(|_| ParseError(format!("IIIF Error reading Region parameter \"{s}\"")))?;
    }
    Ok((kind, coords))
}

/// Parsed size fields, in [`IiifParams`] shape.
struct SizeParts {
    kind: SizeKind,
    upscaling: bool,
    percent: f32,
    reduce: i32,
    nx: usize,
    ny: usize,
}

fn parse_size(s: &str) -> Result<SizeParts, ParseError> {
    let err = || ParseError(format!("Invalid IIIF size parameter: \"{s}\""));
    let mut parts = SizeParts {
        kind: SizeKind::Undefined,
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
        parts.kind = SizeKind::Full;
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
        parts.kind = SizeKind::Percents;
        parts.percent = pct;
        return Ok(parts);
    }
    // `red:` is unreachable via classification — neither is_valid_size here nor
    // the C++ validator admits it — but SipiSize.cpp carries the same branch, so
    // it is kept for constructor fidelity.
    if let Some(p) = rest.strip_prefix("red:") {
        if exclamation {
            return Err(err());
        }
        let mut red = p.parse::<i32>().map_err(|_| err())?;
        if red < 0 {
            red = 0;
        }
        parts.kind = SizeKind::Reduce;
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
            return Err(ParseError(format!(
                "IIIF size height cannot be zero: \"{s}\""
            )));
        }
        parts.kind = SizeKind::PixelsY;
        parts.ny = ny;
    } else if height_str.is_empty() {
        // "w,"
        let nx = parse_dim(width_str)?;
        if nx == 0 {
            return Err(ParseError(format!(
                "IIIF size width cannot be zero: \"{s}\""
            )));
        }
        parts.kind = SizeKind::PixelsX;
        parts.nx = nx;
    } else {
        // "w,h"
        let nx = parse_dim(width_str)?;
        let ny = parse_dim(height_str)?;
        if nx == 0 || ny == 0 {
            return Err(ParseError(format!("IIIF size cannot be zero: \"{s}\"")));
        }
        parts.kind = if exclamation {
            SizeKind::Maxdim
        } else {
            SizeKind::PixelsXy
        };
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

fn parse_quality_format(s: &str) -> Result<(QualityKind, FormatKind), ParseError> {
    if s.is_empty() {
        return Ok((QualityKind::Default, FormatKind::Jpg));
    }
    let dot = s.find('.').ok_or_else(|| {
        ParseError(format!(
            "IIIF Error reading Quality+Format parameter \"{s}\" !"
        ))
    })?;
    let quality = match &s[..dot] {
        "default" => QualityKind::Default,
        "color" => QualityKind::Color,
        "gray" => QualityKind::Gray,
        "bitonal" => QualityKind::Bitonal,
        q => {
            return Err(ParseError(format!(
                "IIIF Error reading Quality parameter \"{q}\" !"
            )))
        }
    };
    let format = match &s[dot + 1..] {
        "jpg" => FormatKind::Jpg,
        "tif" => FormatKind::Tif,
        "png" => FormatKind::Png,
        "gif" => FormatKind::Gif,
        "jp2" => FormatKind::Jp2,
        "pdf" => FormatKind::Pdf,
        "webp" => FormatKind::Webp,
        _ => FormatKind::Unsupported,
    };
    Ok((quality, format))
}

/// Build [`IiifParams`] from the four IIIF path segments.
pub(crate) fn parse_iiif_params(
    region: &str,
    size: &str,
    rotation: &str,
    qualform: &str,
) -> Result<IiifParams, ParseError> {
    let (region_kind, region) = parse_region(region)?;
    let sz = parse_size(size)?;
    let (mirror, angle) = parse_rotation(rotation)?;
    let (quality_kind, format_kind) = parse_quality_format(qualform)?;
    Ok(IiifParams {
        region_kind,
        region,
        size_kind: sz.kind,
        size_upscaling: sz.upscaling,
        size_percent: sz.percent,
        size_reduce: sz.reduce,
        size_nx: sz.nx,
        size_ny: sz.ny,
        rotation: angle,
        rotation_mirror: mirror,
        quality_kind,
        format_kind,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn region_param_values() {
        assert_eq!(parse_region("full").unwrap().0, RegionKind::Full);
        assert_eq!(parse_region("square").unwrap().0, RegionKind::Square);
        let (kind, c) = parse_region("10,20,30,40").unwrap();
        assert_eq!(kind, RegionKind::Coords);
        assert_eq!(c, [10.0, 20.0, 30.0, 40.0]);
        let (kind, c) = parse_region("pct:0,0,50,50").unwrap();
        assert_eq!(kind, RegionKind::Percents);
        assert_eq!(c, [0.0, 0.0, 50.0, 50.0]);
    }

    #[test]
    fn size_param_values() {
        assert_eq!(parse_size("max").unwrap().kind, SizeKind::Full);
        assert!(parse_size("^max").unwrap().upscaling);
        assert_eq!(parse_size("200,").unwrap().kind, SizeKind::PixelsX);
        assert_eq!(parse_size(",100").unwrap().kind, SizeKind::PixelsY);
        let s = parse_size("200,100").unwrap();
        assert_eq!((s.kind, s.nx, s.ny), (SizeKind::PixelsXy, 200, 100));
        assert_eq!(parse_size("!200,100").unwrap().kind, SizeKind::Maxdim);
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
        assert_eq!(
            parse_quality_format("default.jpg").unwrap(),
            (QualityKind::Default, FormatKind::Jpg)
        );
        assert_eq!(
            parse_quality_format("gray.png").unwrap(),
            (QualityKind::Gray, FormatKind::Png)
        );
        assert_eq!(
            parse_quality_format("color.jp2").unwrap(),
            (QualityKind::Color, FormatKind::Jp2)
        );
    }
}
