//! IIIF `info.json` + SIPI `knora.json` assembly (strangler-fig rewrite).
//!
//! The seam has no serve entry for these JSON responses — the Rust shell builds
//! them from the edge-probe results (`sipi_image_dims` / `sipi_mimetype`), a
//! `stat`, and the optional `.info` sidecar. Ported field-for-field from
//! `serve_info_json_file` (`SipiHttpServer.cpp:547-794`) and
//! `serve_knora_json_file` (`:806-981`). Key order is irrelevant: both
//! `serde_json` and the e2e `insta` snapshots normalise to sorted keys.
//!
//! The builders are pure (they take already-fetched data), so they unit-test
//! without the engine; the handler does the FFI + filesystem I/O.

use serde_json::{json, Map, Value};

use crate::ffi::{SipiImageDims, SipiPermType};

const IMAGE_CONTEXT: &str = "http://iiif.io/api/image/3/context.json";
const FILE_CONTEXT: &str = "http://sipi.io/api/file/3/context.json";

/// The 17 IIIF `extraFeatures` SIPI advertises (`SipiHttpServer.cpp:741-757`).
const EXTRA_FEATURES: [&str; 17] = [
    "baseUriRedirect",
    "canonicalLinkHeader",
    "cors",
    "jsonldMediaType",
    "mirroring",
    "profileLinkHeader",
    "regionByPct",
    "regionByPx",
    "regionSquare",
    "rotationArbitrary",
    "rotationBy90s",
    "sizeByConfinedWh",
    "sizeByH",
    "sizeByPct",
    "sizeByW",
    "sizeByWh",
    "sizeUpscaling",
];

/// IIIF Image API context URL — used for the info.json `Link` header.
#[must_use]
pub const fn image_context() -> &'static str {
    IMAGE_CONTEXT
}

/// SIPI file context URL — used for the non-image info.json `Link` header.
#[must_use]
pub const fn file_context() -> &'static str {
    FILE_CONTEXT
}

/// The `sizes[]` pyramid: for reduce level `i` in `1..clevels`, the dimension is
/// `ceil(native / 2^i)` (the C++ `SipiSize::REDUCE` formula, `SipiSize.cpp:329`),
/// appending each level and breaking once both width and height are `< 128`
/// (`SipiHttpServer.cpp:698-709`). `clevels` falls back to 5 when 0.
fn size_pyramid(width: u32, height: u32, clevels: u32) -> Vec<Value> {
    let cnt = if clevels > 0 { clevels } else { 5 };
    let reduce = |dim: u32, level: u32| -> u32 {
        let sf = 1u64 << level;
        ((u64::from(dim) + sf - 1) / sf) as u32
    };
    let mut sizes = Vec::new();
    for i in 1..cnt {
        let w = reduce(width, i);
        let h = reduce(height, i);
        if w < 128 && h < 128 {
            break;
        }
        sizes.push(json!({ "width": w, "height": h }));
    }
    sizes
}

/// IIIF `info.json` for an image (`SipiHttpServer.cpp:576-764`). The auth-service
/// block (preflight-driven) is added by the handler.
#[must_use]
pub fn image_info_json(id: &str, dims: &SipiImageDims) -> Value {
    let mut root = Map::new();
    root.insert("@context".into(), json!(IMAGE_CONTEXT));
    root.insert("id".into(), json!(id));
    root.insert("type".into(), json!("ImageService3"));
    root.insert("protocol".into(), json!("http://iiif.io/api/image"));
    root.insert("profile".into(), json!("level2"));
    root.insert("width".into(), json!(dims.width));
    root.insert("height".into(), json!(dims.height));
    if dims.numpages > 0 {
        root.insert("numpages".into(), json!(dims.numpages));
    }
    root.insert(
        "sizes".into(),
        json!(size_pyramid(dims.width, dims.height, dims.clevels)),
    );
    if dims.tile_width > 0 && dims.tile_height > 0 {
        let cnt = if dims.clevels > 0 { dims.clevels } else { 5 };
        let scale_factors: Vec<u32> = (1..cnt).collect();
        root.insert(
            "tiles".into(),
            json!([{ "width": dims.tile_width, "height": dims.tile_height, "scaleFactors": scale_factors }]),
        );
    }
    root.insert("extraFormats".into(), json!(["tif", "jp2"]));
    root.insert(
        "preferredFormats".into(),
        json!(["jpg", "tif", "jp2", "png"]),
    );
    root.insert("extraFeatures".into(), json!(EXTRA_FEATURES));
    Value::Object(root)
}

/// `info.json` for a non-image file (`SipiHttpServer.cpp:578-602`): the SIPI file
/// context, the id, the detected MIME type, and the file size.
#[must_use]
pub fn file_info_json(id: &str, mime: &str, file_size: u64) -> Value {
    json!({
        "@context": FILE_CONTEXT,
        "id": id,
        "internalMimeType": mime,
        "fileSize": file_size,
    })
}

/// The optional `.info` sidecar SIPI writes next to a derivative
/// (`SipiHttpServer.cpp:857-905`). All fields are optional; a missing or
/// unparseable sidecar yields the default (everything `None`).
#[derive(Debug, Default, Clone)]
pub struct Sidecar {
    pub original_filename: Option<String>,
    pub checksum_original: Option<String>,
    pub checksum_derivative: Option<String>,
    pub duration: Option<f64>,
    pub fps: Option<f64>,
    pub height: Option<f64>,
    pub width: Option<f64>,
}

impl Sidecar {
    /// Parse a `.info` JSON document, tolerating missing keys and wrong types
    /// (matching the C++ key-by-key extraction). Invalid JSON → an empty sidecar.
    #[must_use]
    pub fn parse(text: &str) -> Self {
        let Ok(Value::Object(map)) = serde_json::from_str::<Value>(text) else {
            return Self::default();
        };
        let string = |k: &str| map.get(k).and_then(Value::as_str).map(str::to_owned);
        let number = |k: &str| map.get(k).and_then(Value::as_f64);
        Self {
            original_filename: string("originalFilename"),
            checksum_original: string("checksumOriginal"),
            checksum_derivative: string("checksumDerivative"),
            duration: number("duration"),
            fps: number("fps"),
            height: number("height"),
            width: number("width"),
        }
    }
}

/// The common `knora.json` prelude: context, id, and the sidecar checksums
/// (emitted for every file type, `SipiHttpServer.cpp:841-910`).
fn knora_base(id: &str, sidecar: &Sidecar) -> Map<String, Value> {
    let mut root = Map::new();
    root.insert("@context".into(), json!(FILE_CONTEXT));
    root.insert("id".into(), json!(id));
    if let Some(c) = &sidecar.checksum_original {
        root.insert("checksumOriginal".into(), json!(c));
    }
    if let Some(c) = &sidecar.checksum_derivative {
        root.insert("checksumDerivative".into(), json!(c));
    }
    root
}

/// `knora.json` for an image (`SipiHttpServer.cpp:913-944`). `originalMimeType` /
/// `originalFilename` (only present when `read_shape` reports an Essentials
/// packet, `success == ALL`) are deferred — `sipi_image_dims` does not surface
/// the packet metadata; the e2e requires only the fields below.
#[must_use]
pub fn image_knora_json(id: &str, mime: &str, dims: &SipiImageDims, sidecar: &Sidecar) -> Value {
    let mut root = knora_base(id, sidecar);
    root.insert("width".into(), json!(dims.width));
    root.insert("height".into(), json!(dims.height));
    if dims.numpages > 0 {
        root.insert("numpages".into(), json!(dims.numpages));
    }
    root.insert("internalMimeType".into(), json!(mime));
    Value::Object(root)
}

/// `knora.json` for `video/mp4` (`SipiHttpServer.cpp:948-967`): MIME + size, plus
/// the sidecar's filename and media metrics (each emitted only when present and
/// non-negative, as JSON reals).
#[must_use]
pub fn video_knora_json(id: &str, mime: &str, file_size: u64, sidecar: &Sidecar) -> Value {
    let mut root = knora_base(id, sidecar);
    root.insert("internalMimeType".into(), json!(mime));
    root.insert("fileSize".into(), json!(file_size));
    if let Some(name) = sidecar
        .original_filename
        .as_deref()
        .filter(|s| !s.is_empty())
    {
        root.insert("originalFilename".into(), json!(name));
    }
    for (key, value) in [
        ("duration", sidecar.duration),
        ("fps", sidecar.fps),
        ("height", sidecar.height),
        ("width", sidecar.width),
    ] {
        if let Some(v) = value.filter(|v| *v >= 0.0) {
            root.insert(key.into(), json!(v));
        }
    }
    Value::Object(root)
}

/// `knora.json` for any other file (`SipiHttpServer.cpp:968-978`): MIME, size, and
/// the original filename (empty string when there is no sidecar).
#[must_use]
pub fn generic_knora_json(id: &str, mime: &str, file_size: u64, sidecar: &Sidecar) -> Value {
    let mut root = knora_base(id, sidecar);
    root.insert("internalMimeType".into(), json!(mime));
    root.insert("fileSize".into(), json!(file_size));
    root.insert(
        "originalFilename".into(),
        json!(sidecar.original_filename.clone().unwrap_or_default()),
    );
    Value::Object(root)
}

/// Whether a permission requires the IIIF Authentication service block (and a
/// 401 response). `allow` / `restrict` / `deny` do not.
#[must_use]
pub fn is_auth_type(permission: SipiPermType) -> bool {
    matches!(
        permission,
        SipiPermType::Login
            | SipiPermType::Clickthrough
            | SipiPermType::Kiosk
            | SipiPermType::External
    )
}

/// Build the IIIF Auth API v1 `service` object for an auth-type info.json
/// (`SipiHttpServer.cpp:607-658`). Requires `cookieUrl` (and `tokenUrl`); a
/// missing required key → `Err(())` (the C++ 500). The remaining kv pairs pass
/// through except the structural keys. `logoutUrl` is optional.
pub fn auth_service(permission: SipiPermType, kv: &[(String, String)]) -> Result<Value, ()> {
    let get = |key: &str| kv.iter().find(|(k, _)| k == key).map(|(_, v)| v.as_str());
    let profile = match permission {
        SipiPermType::Login => "http://iiif.io/api/auth/1/login",
        SipiPermType::Clickthrough => "http://iiif.io/api/auth/1/clickthrough",
        SipiPermType::Kiosk => "http://iiif.io/api/auth/1/kiosk",
        SipiPermType::External => "http://iiif.io/api/auth/1/external",
        _ => return Err(()),
    };
    let cookie_url = get("cookieUrl").ok_or(())?;
    let token_url = get("tokenUrl").ok_or(())?;

    let mut service = Map::new();
    service.insert(
        "@context".into(),
        json!("http://iiif.io/api/auth/1/context.json"),
    );
    service.insert("@id".into(), json!(cookie_url));
    service.insert("profile".into(), json!(profile));
    for (k, v) in kv {
        if !matches!(
            k.as_str(),
            "cookieUrl" | "tokenUrl" | "logoutUrl" | "infile"
        ) {
            service.insert(k.clone(), json!(v));
        }
    }
    let mut sub = vec![json!({ "@id": token_url, "profile": "http://iiif.io/api/auth/1/token" })];
    if let Some(logout) = get("logoutUrl") {
        sub.push(json!({ "@id": logout, "profile": "http://iiif.io/api/auth/1/logout" }));
    }
    service.insert("service".into(), json!(sub));
    Ok(Value::Object(service))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dims(width: u32, height: u32, tile: u32, clevels: u32) -> SipiImageDims {
        SipiImageDims {
            width,
            height,
            numpages: 0,
            tile_width: tile,
            tile_height: tile,
            clevels,
        }
    }

    #[test]
    fn info_json_matches_lena512_golden() {
        // The exact shape the e2e golden snapshot pins (iiif_compliance__info-json-lena512):
        // 512x512, tile 512, clevels 8 → sizes [{256},{128}], scaleFactors [1..7].
        let v = image_info_json("http://h/unit/lena512.jp2", &dims(512, 512, 512, 8));
        assert_eq!(v["type"], "ImageService3");
        assert_eq!(v["protocol"], "http://iiif.io/api/image");
        assert_eq!(v["profile"], "level2");
        assert_eq!(v["width"], 512);
        assert_eq!(v["height"], 512);
        assert_eq!(
            v["sizes"],
            json!([{ "width": 256, "height": 256 }, { "width": 128, "height": 128 }])
        );
        assert_eq!(v["tiles"][0]["width"], 512);
        assert_eq!(v["tiles"][0]["scaleFactors"], json!([1, 2, 3, 4, 5, 6, 7]));
        assert_eq!(v["extraFeatures"].as_array().unwrap().len(), 17);
        assert_eq!(v["extraFormats"], json!(["tif", "jp2"]));
        assert!(v.get("numpages").is_none());
    }

    #[test]
    fn untiled_image_omits_tiles() {
        let v = image_info_json("http://h/id", &dims(1000, 800, 0, 0));
        assert!(v.get("tiles").is_none());
        // clevels=0 → fallback 5: levels 1..5 → 500,250,125(<128 stops at h=100<128? 800/8=100)
        assert!(v["sizes"].as_array().unwrap().len() >= 1);
    }

    #[test]
    fn file_info_json_shape() {
        let v = file_info_json("http://h/doc.pdf", "application/pdf", 1234);
        assert_eq!(v["@context"], FILE_CONTEXT);
        assert_eq!(v["internalMimeType"], "application/pdf");
        assert_eq!(v["fileSize"], 1234);
    }

    #[test]
    fn sidecar_parse_extracts_known_keys() {
        let s = Sidecar::parse(
            r#"{"originalFilename":"Dummy.mp4","fps":30,"width":320,"height":240,"duration":4.7,"checksumOriginal":"abc"}"#,
        );
        assert_eq!(s.original_filename.as_deref(), Some("Dummy.mp4"));
        assert_eq!(s.fps, Some(30.0));
        assert_eq!(s.checksum_original.as_deref(), Some("abc"));
        // Invalid JSON → empty.
        assert!(Sidecar::parse("not json").original_filename.is_none());
    }

    #[test]
    fn video_knora_json_from_sidecar() {
        let s =
            Sidecar::parse(r#"{"originalFilename":"Dummy.mp4","fps":30,"width":320,"height":240}"#);
        let v = video_knora_json("http://h/v.mp4", "video/mp4", 999, &s);
        assert_eq!(v["internalMimeType"], "video/mp4");
        assert_eq!(v["fileSize"], 999);
        assert_eq!(v["originalFilename"], "Dummy.mp4");
        assert_eq!(v["width"].as_f64().unwrap() as i64, 320);
        assert_eq!(v["fps"].as_f64().unwrap() as i64, 30);
    }

    #[test]
    fn image_knora_json_required_fields() {
        let v = image_knora_json(
            "http://h/i.jp2",
            "image/jp2",
            &dims(512, 512, 512, 8),
            &Sidecar::default(),
        );
        assert_eq!(v["@context"], FILE_CONTEXT);
        assert_eq!(v["width"], 512);
        assert_eq!(v["internalMimeType"], "image/jp2");
    }

    #[test]
    fn auth_service_block_for_login() {
        let kv = vec![
            ("cookieUrl".to_string(), "https://auth/cookie".to_string()),
            ("tokenUrl".to_string(), "https://auth/token".to_string()),
            ("logoutUrl".to_string(), "https://auth/logout".to_string()),
            ("infile".to_string(), "/srv/x.jp2".to_string()),
        ];
        let svc = auth_service(SipiPermType::Login, &kv).expect("login service");
        assert_eq!(svc["@context"], "http://iiif.io/api/auth/1/context.json");
        assert_eq!(svc["@id"], "https://auth/cookie");
        assert_eq!(svc["profile"], "http://iiif.io/api/auth/1/login");
        // infile/cookieUrl/tokenUrl/logoutUrl are structural, not passed through.
        assert!(svc.get("infile").is_none());
        let sub = svc["service"].as_array().unwrap();
        assert_eq!(sub[0]["profile"], "http://iiif.io/api/auth/1/token");
        assert_eq!(sub[1]["profile"], "http://iiif.io/api/auth/1/logout");
    }

    #[test]
    fn auth_service_missing_cookie_url_errors() {
        assert!(auth_service(SipiPermType::Login, &[]).is_err());
        assert!(!is_auth_type(SipiPermType::Allow));
        assert!(is_auth_type(SipiPermType::Kiosk));
    }
}
