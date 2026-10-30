//! Runs the migrated Lua runtime against a verbatim copy of dsp-api's
//! production script closure (the 9 live files from
//! `dsp-api/modules/sipi/scripts/`, staged under `test/_test_data/dsp-api/`)
//! — the audit's breaks-prod findings as executable regressions: the `os.date`
//! call inside `log()` on every request (D1), lowercase `server.header`
//! lookups (D6), Lua 5.3 integer division in `send_response.lua` (D8), the
//! bare-globals inter-module contract (D9), `os.getenv` for the API host
//! (D11), and the routed-but-missing `delete_temp_file.lua` staying a
//! request-time 404 (D12).
//!
//! DSP-API itself is a canned mock: a TCP thread answering
//! `/admin/files/{shortcode}/{file}` with the permission JSON the closure
//! expects, selected by filename.

use image::RgbaImage;
use serde_json::{json, Value};
use sipi_e2e::jwt::create_jwt;
use sipi_e2e::{http_client, test_data_dir, SipiServer};
use std::io::{Read, Write};
use std::net::TcpListener;
use std::sync::OnceLock;

/// Matches `jwt_secret` in `dsp-api/sipi.dsp-api-closure-config.lua`.
const JWT_SECRET: &str = "dev-only-insecure-jwt-secret-change-me";
/// The `iss` the closure validates tokens against
/// (`KNORA_WEBAPI_KNORA_API_EXTERNAL_HOST:PORT`).
const ISSUER_HOST: &str = "0.0.0.0";
const ISSUER_PORT: &str = "3333";

/// A canned DSP-API `/admin/files` mock: permission JSON selected by the
/// requested filename. Runs for the whole test binary.
fn mock_dsp_api() -> u16 {
    static PORT: OnceLock<u16> = OnceLock::new();
    *PORT.get_or_init(|| {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind mock DSP-API");
        let port = listener.local_addr().expect("mock addr").port();
        std::thread::spawn(move || {
            for stream in listener.incoming() {
                let Ok(mut stream) = stream else { continue };
                let mut buf = [0u8; 4096];
                let n = stream.read(&mut buf).unwrap_or(0);
                let request = String::from_utf8_lossy(&buf[..n]);
                let body = if request.contains("perm2file") {
                    r#"{"permissionCode":2}"#
                } else if request.contains("perm1file") || request.contains("perm1odd") {
                    r#"{"permissionCode":1,"restrictedViewSettings":{"size":"!128,128"}}"#
                } else {
                    r#"{"permissionCode":0}"#
                };
                let _ = write!(
                    stream,
                    "HTTP/1.1 200 OK\r\ncontent-type: application/json\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{}",
                    body.len(),
                    body
                );
            }
        });
        port
    })
}

/// The shared server running dsp-api's closure, pointed at the mock via the
/// same env vars production uses.
fn server() -> &'static SipiServer {
    static SERVER: OnceLock<SipiServer> = OnceLock::new();
    SERVER.get_or_init(|| {
        let api_port = mock_dsp_api().to_string();
        SipiServer::start_env(
            "dsp-api/sipi.dsp-api-closure-config.lua",
            &test_data_dir(),
            &[],
            &[
                ("SIPI_WEBAPI_HOSTNAME", "127.0.0.1"),
                ("SIPI_WEBAPI_PORT", api_port.as_str()),
                ("KNORA_WEBAPI_KNORA_API_EXTERNAL_HOST", ISSUER_HOST),
                ("KNORA_WEBAPI_KNORA_API_EXTERNAL_PORT", ISSUER_PORT),
            ],
        )
    })
}

fn iiif_url(file: &str) -> String {
    format!("{}/0801/{file}/full/max/0/default.jpg", server().base_url)
}

/// Full view permission (code 2) from the API: the image serves. This is the
/// closure's whole anonymous happy path — `require` chain, `log()`'s
/// `os.date`, `find_file` over `server.fs.exists`, and `server.http` to the
/// API — in one request.
#[test]
fn permission_code_2_serves_the_image() {
    let resp = http_client()
        .get(iiif_url("perm2file.jp2"))
        .send()
        .expect("GET perm2");
    assert_eq!(resp.status().as_u16(), 200);
    assert!(resp.bytes().expect("body").len() > 1000);
}

/// No view permission (code 0) is a deny — 401.
#[test]
fn permission_code_0_denies() {
    let resp = http_client()
        .get(iiif_url("perm0file.jp2"))
        .send()
        .expect("GET perm0");
    assert_eq!(resp.status().as_u16(), 401);
}

/// Restricted view (code 1 with a size): the closure returns the
/// `{type='restrict', size=…}` table and the served image is the reduced one
/// — strictly smaller than the full-size render of the same source.
#[test]
fn permission_code_1_restricts_the_size() {
    let restricted = http_client()
        .get(iiif_url("perm1file.jp2"))
        .send()
        .expect("GET perm1");
    assert_eq!(restricted.status().as_u16(), 200);
    let restricted_len = restricted.bytes().expect("body").len();

    let full = http_client()
        .get(iiif_url("perm2file.jp2"))
        .send()
        .expect("GET perm2");
    let full_len = full.bytes().expect("body").len();

    assert!(
        restricted_len < full_len,
        "restricted render ({restricted_len}B) must be smaller than full ({full_len}B)"
    );
}

/// A region request on a size-restricted image resolves in the restricted
/// image's own coordinates: perm1file.jp2 is 512x512 under `!128,128`, so it
/// is served as a 128x128 image and `0,0,64,64` is the top-left quarter of
/// `full/max`, never a native-scale 64x64 cut.
#[test]
fn permission_code_1_restricts_the_size_of_a_region_request() {
    let full = get_png("perm1file.jp2", "full/max");
    assert_eq!(full.dimensions(), (128, 128));
    let region = get_png("perm1file.jp2", "0,0,64,64/max");
    assert_eq!(region.dimensions(), (64, 64));
    assert_eq!(region, crop(&full, 0, 0, 64, 64));
}

/// A non-dividing restricted image: perm1odd.jp2 is 373x496 under
/// `!128,128`, which the shell rounds to (96, 128).
const ODD: &str = "perm1odd.jp2";

fn image_url(file: &str, region_size: &str, ext: &str) -> String {
    format!(
        "{}/0801/{file}/{region_size}/0/default.{ext}",
        server().base_url
    )
}

/// GET a PNG rendering and decode it to RGBA (lossless, so pixels compare
/// exactly; never compare PNG bytes).
fn get_png(file: &str, region_size: &str) -> RgbaImage {
    let resp = http_client()
        .get(image_url(file, region_size, "png"))
        .send()
        .expect("GET png");
    assert_eq!(
        resp.status().as_u16(),
        200,
        "{file} {region_size} must serve"
    );
    image::load_from_memory(&resp.bytes().expect("body"))
        .expect("decode png")
        .to_rgba8()
}

fn crop(img: &RgbaImage, x: u32, y: u32, w: u32, h: u32) -> RgbaImage {
    image::imageops::crop_imm(img, x, y, w, h).to_image()
}

fn info_json(file: &str) -> Value {
    let resp = http_client()
        .get(format!("{}/0801/{file}/info.json", server().base_url))
        .send()
        .expect("GET info.json");
    assert_eq!(resp.status().as_u16(), 200, "{file} info.json");
    resp.json().expect("info.json body")
}

fn info_dims(info: &Value) -> (u32, u32) {
    let dim = |key: &str| u32::try_from(info[key].as_u64().expect(key)).expect(key);
    (dim("width"), dim("height"))
}

/// The restricted info.json describes the restricted image: (RW, RH) as its
/// dims and its only size, and maxWidth/maxHeight = (RW, RH) because nothing
/// larger is served. An unrestricted info.json states no maximum.
#[test]
fn restricted_info_json_states_the_restricted_dims_as_its_maximum() {
    let info = info_json(ODD);
    assert_eq!(info_dims(&info), (96, 128));
    assert_eq!(info["maxWidth"], 96);
    assert_eq!(info["maxHeight"], 128);
    assert_eq!(info["sizes"], json!([{ "width": 96, "height": 128 }]));

    let open = info_json("perm2file.jp2");
    assert!(open.get("maxWidth").is_none(), "unrestricted: {open}");
    assert!(open.get("maxHeight").is_none(), "unrestricted: {open}");
}

/// info.json width/height are exactly the `full/max` output dims.
#[test]
fn restricted_info_json_dims_equal_full_max() {
    let (w, h) = info_dims(&info_json(ODD));
    assert_eq!(get_png(ODD, "full/max").dimensions(), (w, h));
}

/// DEV-7456: tiles at size `max` over the restricted image, edge tiles
/// included, are crops of the one (RW, RH) rendering, so they stitch to
/// `full/max` pixel for pixel.
#[test]
fn restricted_tiles_at_size_max_stitch_to_full_max() {
    let (w, h) = info_dims(&info_json(ODD));
    let full = get_png(ODD, "full/max");
    for tile in [32u32, 64] {
        let mut stitched = RgbaImage::new(w, h);
        for y in (0..h).step_by(tile as usize) {
            for x in (0..w).step_by(tile as usize) {
                let (tw, th) = (tile.min(w - x), tile.min(h - y));
                let piece = get_png(ODD, &format!("{x},{y},{tw},{th}/max"));
                assert_eq!(piece.dimensions(), (tw, th), "tile {x},{y} at {tile}");
                image::imageops::replace(&mut stitched, &piece, i64::from(x), i64::from(y));
            }
        }
        assert!(
            stitched == full,
            "{tile} px tiles must stitch to full/max exactly"
        );
    }
}

/// DEV-7485: a left-half region in info.json coordinates is the left half of
/// `full/max`.
#[test]
fn restricted_left_half_region_is_the_left_half_of_full_max() {
    let (w, h) = info_dims(&info_json(ODD));
    let full = get_png(ODD, "full/max");
    let half = get_png(ODD, &format!("0,0,{},{h}/max", w / 2));
    assert_eq!(half, crop(&full, 0, 0, w / 2, h));
}

/// The canonical `Link` of a restricted request names restricted-space
/// coordinates and never the native 373x496, on GET and HEAD; a refused
/// request carries no Link.
#[test]
fn restricted_canonical_link_is_in_restricted_coordinates() {
    let link = |resp: &reqwest::blocking::Response| {
        resp.headers()
            .get("link")
            .map(|v| v.to_str().expect("link header").to_string())
    };
    // Only the path after the host: the ephemeral port may contain the digits.
    let assert_no_native = |link: &str| {
        let path = link.split_once("/0801/").map_or(link, |(_, p)| p);
        assert!(
            !path.contains("373") && !path.contains("496"),
            "canonical Link discloses the native size: {link}"
        );
    };

    let get = http_client()
        .get(image_url(ODD, "full/max", "jpg"))
        .send()
        .expect("GET full/max");
    assert_eq!(get.status().as_u16(), 200);
    let get_link = link(&get).expect("GET carries a Link");
    assert!(
        get_link.contains(&format!("/0801/{ODD}/full/max/0/default.jpg")),
        "full/max canonical: {get_link}"
    );
    assert_no_native(&get_link);

    let head = http_client()
        .head(image_url(ODD, "full/max", "jpg"))
        .send()
        .expect("HEAD full/max");
    assert_eq!(head.status().as_u16(), 200);
    assert_eq!(link(&head).as_deref(), Some(get_link.as_str()));

    let region = http_client()
        .get(image_url(ODD, "pct:0,0,50,50/40,50", "jpg"))
        .send()
        .expect("GET region");
    assert_eq!(region.status().as_u16(), 200);
    let region_link = link(&region).expect("region carries a Link");
    assert!(
        region_link.contains(&format!("/0801/{ODD}/0,0,48,64/40,50/0/default.jpg")),
        "region canonical: {region_link}"
    );
    assert_no_native(&region_link);

    let refused = http_client()
        .get(image_url(ODD, "full/^pct:200", "jpg"))
        .send()
        .expect("GET beyond full/max");
    assert_eq!(refused.status().as_u16(), 400);
    assert!(
        link(&refused).is_none(),
        "a refused request carries no Link"
    );
}

/// The `tmp` prefix is always allowed without asking the API.
#[test]
fn tmp_prefix_serves_without_the_api() {
    let resp = http_client()
        .get(format!(
            "{}/tmp/tmpfile.jp2/full/max/0/default.jpg",
            server().base_url
        ))
        .send()
        .expect("GET tmp");
    assert_eq!(resp.status().as_u16(), 200);
}

/// A system-admin token (scope `admin`, valid `exp`/`aud`/`iss`) bypasses the
/// permission lookup — the closure's own JWT validation path over the
/// hardened `server.decode_jwt`.
#[test]
fn admin_token_bypasses_the_permission_lookup() {
    let exp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .expect("clock")
        .as_secs()
        + 3600;
    let token = create_jwt(
        &json!({
            "sub": "admin-user",
            "scope": "admin",
            "aud": ["Knora", "Sipi"],
            "iss": format!("{ISSUER_HOST}:{ISSUER_PORT}"),
            "exp": exp,
        }),
        JWT_SECRET,
    );
    // perm0file would be denied by the API — the admin scope must win.
    let resp = http_client()
        .get(iiif_url("perm0file.jp2"))
        .header("Authorization", format!("Bearer {token}"))
        .send()
        .expect("GET as admin");
    assert_eq!(resp.status().as_u16(), 200);
}

/// An expired token hits the closure's own 401 path (`send_error` inside
/// `authentication.lua`) — the hardened `decode_jwt` rejects it, and the
/// script's direct response wins.
#[test]
fn expired_token_is_a_401_from_the_closure() {
    let token = create_jwt(
        &json!({
            "sub": "expired-user",
            "aud": ["Sipi"],
            "iss": format!("{ISSUER_HOST}:{ISSUER_PORT}"),
            "exp": 1000000000u64,
        }),
        JWT_SECRET,
    );
    let resp = http_client()
        .get(iiif_url("perm2file.jp2"))
        .header("Authorization", format!("Bearer {token}"))
        .send()
        .expect("GET expired");
    assert_eq!(resp.status().as_u16(), 401);
}

/// An unknown file: `find_file` returns nil, the closure answers
/// `'allow', 'file_does_not_exist'`, and serving that path fails downstream —
/// an error status, never a 200.
#[test]
fn unknown_file_fails_after_the_allow() {
    let resp = http_client()
        .get(iiif_url("nosuchfile.jp2"))
        .send()
        .expect("GET unknown");
    let status = resp.status().as_u16();
    assert!(
        status >= 400,
        "an unknown file must not serve, got {status}"
    );
}

/// dsp-api's production configs route `delete_temp_file.lua`, which does not
/// exist on disk (audit D12): the route must stay a request-time 404, never a
/// boot failure — the shared server booting at all is half the assertion.
#[test]
fn routed_but_missing_script_is_a_404() {
    let resp = http_client()
        .delete(format!("{}/delete_temp_file", server().base_url))
        .send()
        .expect("DELETE missing route");
    assert_eq!(resp.status().as_u16(), 404);
}
