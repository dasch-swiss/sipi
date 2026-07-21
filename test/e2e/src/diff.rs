//! Differential comparison between the Rust shell (subject) and the
//! retained C++ server (reference): replay one request to both, normalise
//! transport-framing headers and the ephemeral host:port, then compare
//! status, headers, and body. Anything that differs and is neither ignored
//! nor allowlisted is a parity failure — that is the regression net (it
//! catches, e.g., someone silently dropping a CORS header).
//!
//! See [`DiffAllowlist`] below for the divergence allowlist.

use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::time::Duration;

use image::GenericImageView;
use reqwest::blocking::Client;
use reqwest::header::{HeaderMap, CONTENT_TYPE};
use reqwest::{Method, StatusCode};
use serde_json::Value;

use crate::SipiServer;

/// Header names (lowercase) excluded from every comparison: pure transport
/// framing plus `traceparent` (the Rust shell always emits it; the C++ transport
/// never does). `content-length` is framing too — image responses stream chunked
/// on both sides, so it is absent there and present on buffered JSON. See
/// `ALWAYS_IGNORE` below.
///
/// `access-control-allow-credentials` is deliberately NOT ignored: the shell now
/// sets it on the image/file/preflight routes and the gate asserts it
/// at parity, so a regression that drops it and silently breaks cookie auth is
/// caught. info.json's stray oracle `ACAC:true` (over its `ACAO:*`) is the one
/// exception — a per-case ignore, not a blanket one.
const ALWAYS_IGNORE: &[&str] = &[
    "date",
    "server",
    "connection",
    "keep-alive",
    "transfer-encoding",
    "content-length",
    "traceparent",
];

/// Tolerance for the image byte-length band (jp2 / undecodable bodies).
const LENGTH_BAND: f64 = 0.10;

/// What to ignore when diffing. Start from `default_transport()` and add
/// per-test exemptions with `ignoring()` (headers) / `masking_json()` (JSON
/// fields). Every per-test exemption is a documented divergence — explain
/// the reason at the call site.
#[derive(Clone, Debug)]
pub struct DiffAllowlist {
    ignore_headers: HashSet<String>,
    /// RFC 6901 JSON Pointers masked to null on BOTH sides before JSON
    /// comparison — for fields that legitimately differ (e.g. a deferred
    /// knora sidecar field) without suppressing the whole body.
    json_masks: Vec<String>,
    /// A pinned status pairing that is an accepted divergence rather than a
    /// failure: `Some((subject, reference))` means "the subject returning
    /// `subject` while the reference returns `reference` is the known,
    /// intentional contract for this request — assert that pairing exactly,
    /// don't silently skip it." Any other status pairing (including the two
    /// matching) still fails. Headers and body are not compared when the
    /// statuses differ (pinned or not) — a status mismatch always means the
    /// two responses are different shapes (redirect vs. rejection vs. data),
    /// so there is nothing meaningful left to diff.
    expected_status: Option<(StatusCode, StatusCode)>,
}

impl DiffAllowlist {
    /// The transport-framing always-ignore set (see `ALWAYS_IGNORE`).
    pub fn default_transport() -> Self {
        Self {
            ignore_headers: ALWAYS_IGNORE.iter().map(|h| h.to_string()).collect(),
            json_masks: Vec::new(),
            expected_status: None,
        }
    }

    /// Also ignore `name` (case-insensitive) — for a per-test allowlisted
    /// header divergence (e.g. an XFP-derived header on a specific path).
    #[must_use]
    pub fn ignoring(mut self, name: &str) -> Self {
        self.ignore_headers.insert(name.to_ascii_lowercase());
        self
    }

    /// Also mask the JSON value at `pointer` (RFC 6901, e.g. `/originalFilename`)
    /// on both sides before comparing — for an allowlisted body-field divergence.
    #[must_use]
    pub fn masking_json(mut self, pointer: &str) -> Self {
        self.json_masks.push(pointer.to_string());
        self
    }

    /// Pin an intentional status-code divergence: the subject is expected to
    /// return `subject` while the reference returns `reference` for this
    /// request. Use this instead of a bare `gap: Some(...)` skip when the
    /// divergence is understood and stable — it turns a blind skip into an
    /// assertion that fails the moment either side's status changes (headers
    /// and body are not compared, since a status mismatch means the two
    /// responses are different shapes with nothing meaningful to diff).
    #[must_use]
    pub fn expect_status_divergence(mut self, subject: StatusCode, reference: StatusCode) -> Self {
        self.expected_status = Some((subject, reference));
        self
    }

    fn is_ignored(&self, name: &str) -> bool {
        self.ignore_headers.contains(name)
    }
}

impl Default for DiffAllowlist {
    fn default() -> Self {
        Self::default_transport()
    }
}

/// Outcome of comparing the two response bodies.
#[derive(Debug)]
pub enum BodyMatch {
    /// Equal: exact bytes, JSON-equal (port-normalised), or same image dims.
    Match,
    /// Differ; the string explains how.
    Mismatch(String),
    /// Not compared — a status mismatch short-circuited the check, or the
    /// shared status is an error whose body is an allowlisted divergence.
    Skipped,
}

/// One header that differs after normalisation.
#[derive(Debug)]
pub struct HeaderDiff {
    pub name: String,
    pub subject: Vec<String>,
    pub reference: Vec<String>,
}

/// Result of a single [`diff_request`].
#[derive(Debug)]
pub struct DiffResult {
    pub method: Method,
    pub path: String,
    pub subject_status: StatusCode,
    pub reference_status: StatusCode,
    /// The allowlist's pinned status pairing, if any — see
    /// [`DiffAllowlist::expect_status_divergence`].
    pub expected_status: Option<(StatusCode, StatusCode)>,
    pub header_diffs: Vec<HeaderDiff>,
    pub body_match: BodyMatch,
}

impl DiffResult {
    /// True when the two statuses agree, or match a pinned expected
    /// divergence exactly.
    fn status_ok(&self) -> bool {
        self.subject_status == self.reference_status
            || self.expected_status == Some((self.subject_status, self.reference_status))
    }

    /// True when subject and reference agree modulo the allowlist.
    pub fn is_parity(&self) -> bool {
        self.status_ok()
            && self.header_diffs.is_empty()
            && matches!(self.body_match, BodyMatch::Match | BodyMatch::Skipped)
    }

    /// Human-readable description of every divergence (empty when parity).
    pub fn divergences(&self) -> Vec<String> {
        let mut out = Vec::new();
        if !self.status_ok() {
            match self.expected_status {
                Some((es, er)) => out.push(format!(
                    "status: subject={} reference={} (pinned divergence expected subject={es} reference={er})",
                    self.subject_status, self.reference_status
                )),
                None => out.push(format!(
                    "status: subject={} reference={}",
                    self.subject_status, self.reference_status
                )),
            }
        }
        for d in &self.header_diffs {
            out.push(format!(
                "header `{}`: subject={:?} reference={:?}",
                d.name, d.subject, d.reference
            ));
        }
        if let BodyMatch::Mismatch(why) = &self.body_match {
            out.push(format!("body: {why}"));
        }
        out
    }

    /// Panic with a full report unless subject and reference are at parity.
    pub fn assert_parity(&self) {
        let divs = self.divergences();
        assert!(
            divs.is_empty(),
            "differential parity failure on {} {}:\n  {}",
            self.method,
            self.path,
            divs.join("\n  ")
        );
    }
}

/// A captured response from one server.
struct Captured {
    status: StatusCode,
    headers: HeaderMap,
    body: Vec<u8>,
}

/// A no-redirect client: 303s are part of the differential surface, so the
/// harness must observe them rather than follow them.
fn diff_client() -> Client {
    Client::builder()
        .danger_accept_invalid_certs(true)
        .timeout(Duration::from_secs(30))
        .pool_max_idle_per_host(0)
        .redirect(reqwest::redirect::Policy::none())
        .build()
        .expect("diff client")
}

fn send(
    client: &Client,
    server: &SipiServer,
    method: &Method,
    path: &str,
    headers: &[(&str, &str)],
    body: Option<Vec<u8>>,
) -> Captured {
    let url = format!("{}{}", server.base_url, path);
    let mut req = client.request(method.clone(), &url);
    for (k, v) in headers {
        req = req.header(*k, *v);
    }
    if let Some(b) = body {
        req = req.body(b);
    }
    let resp = req
        .send()
        .unwrap_or_else(|e| panic!("{method} {url} failed: {e}"));
    let status = resp.status();
    let headers = resp.headers().clone();
    let body = resp.bytes().expect("response body").to_vec();
    Captured {
        status,
        headers,
        body,
    }
}

/// Replay `method path` (with `headers`/`body`) against both servers and
/// compare status, headers (modulo `allow`), and body.
pub fn diff_request(
    subject: &SipiServer,
    reference: &SipiServer,
    method: Method,
    path: &str,
    headers: &[(&str, &str)],
    body: Option<Vec<u8>>,
    allow: &DiffAllowlist,
) -> DiffResult {
    let client = diff_client();
    let s = send(&client, subject, &method, path, headers, body.clone());
    let r = send(&client, reference, &method, path, headers, body);

    let status_match = s.status == r.status;
    // On a shared error status the Rust shell sends a bare status (empty
    // body, no content-type) while the C++ `send_error` echoes a
    // `Bad Request: <msg>` text/plain body. That divergence is intentional
    // (the no-internal-path-leak guarantee holds on both, DEV-6062), so the
    // harness asserts only the status and framing headers on errors, not the
    // error body or its content-type.
    let shared_error = status_match && (s.status.is_client_error() || s.status.is_server_error());

    let effective = if shared_error {
        allow.clone().ignoring("content-type")
    } else {
        allow.clone()
    };
    // A status mismatch (whether an unpinned regression or a pinned
    // `expect_status_divergence`) always means the two responses are
    // different shapes entirely — a redirect vs. a rejection vs. a data
    // response never share a meaningful header set (confirmed empirically:
    // Content-Type, Location, Cache-Control, and Content-Length all differ
    // in every observed case). Comparing headers there is noise, not
    // signal, so skip both header and body comparison and let the status
    // line alone carry the pass/fail verdict.
    let header_diffs = if status_match {
        diff_headers(&s.headers, &r.headers, subject, reference, &effective)
    } else {
        Vec::new()
    };
    let body_match = if status_match && !shared_error {
        compare_bodies(&s, &r, subject, reference, &effective)
    } else {
        BodyMatch::Skipped
    };

    DiffResult {
        method,
        path: path.to_string(),
        subject_status: s.status,
        reference_status: r.status,
        expected_status: allow.expected_status,
        header_diffs,
        body_match,
    }
}

/// `GET path` against both servers with the default transport allowlist.
pub fn diff_get(subject: &SipiServer, reference: &SipiServer, path: &str) -> DiffResult {
    diff_request(
        subject,
        reference,
        Method::GET,
        path,
        &[],
        None,
        &DiffAllowlist::default_transport(),
    )
}

/// Collect a header map into lowercased name → sorted values, with the
/// server's ephemeral location scrubbed (`{BASE}`/`{HOST}`) so port-bearing
/// headers (`Location`, the canonical `Link`) compare equal across the two
/// instances.
fn collect(headers: &HeaderMap, server: &SipiServer) -> BTreeMap<String, Vec<String>> {
    let mut m: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for (k, v) in headers.iter() {
        let value = scrub_str(&String::from_utf8_lossy(v.as_bytes()), server);
        m.entry(k.as_str().to_ascii_lowercase())
            .or_default()
            .push(value);
    }
    for vals in m.values_mut() {
        vals.sort();
    }
    m
}

fn diff_headers(
    s: &HeaderMap,
    r: &HeaderMap,
    subject: &SipiServer,
    reference: &SipiServer,
    allow: &DiffAllowlist,
) -> Vec<HeaderDiff> {
    let sm = collect(s, subject);
    let rm = collect(r, reference);
    let names: BTreeSet<&String> = sm.keys().chain(rm.keys()).collect();
    let mut diffs = Vec::new();
    for name in names {
        if allow.is_ignored(name) {
            continue;
        }
        let sv = sm.get(name);
        let rv = rm.get(name);
        if sv != rv {
            diffs.push(HeaderDiff {
                name: name.clone(),
                subject: sv.cloned().unwrap_or_default(),
                reference: rv.cloned().unwrap_or_default(),
            });
        }
    }
    diffs
}

fn content_type(headers: &HeaderMap) -> String {
    headers
        .get(CONTENT_TYPE)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("")
        .to_ascii_lowercase()
}

fn compare_bodies(
    s: &Captured,
    r: &Captured,
    subject: &SipiServer,
    reference: &SipiServer,
    allow: &DiffAllowlist,
) -> BodyMatch {
    // Both empty (HEAD, 204, 304, …) → equal; never attempt to parse "" as JSON.
    if s.body.is_empty() && r.body.is_empty() {
        return BodyMatch::Match;
    }
    let ct = content_type(&s.headers);
    if ct.contains("json") {
        compare_json(&s.body, &r.body, subject, reference, allow)
    } else if ct.starts_with("image/") {
        compare_image(&s.body, &r.body, &ct)
    } else {
        // text / empty / other: exact compare after scrubbing each side's
        // ephemeral host:port (a 303 Location carries it).
        let sn = normalize(&s.body, subject);
        let rn = normalize(&r.body, reference);
        if sn == rn {
            BodyMatch::Match
        } else {
            BodyMatch::Mismatch(format!(
                "normalised text differs:\n    subject={sn}\n    reference={rn}"
            ))
        }
    }
}

/// Scrub a server's ephemeral location to stable tokens: the full base URL
/// `http://127.0.0.1:<port>` → `{BASE}` and the bare authority
/// `127.0.0.1:<port>` → `{HOST}`. Covers the scheme-qualified form (info.json
/// `id`, canonical `Link`, 303 `Location`) and any bare host:port echo, so the
/// two instances' distinct ports compare equal.
fn scrub_str(text: &str, server: &SipiServer) -> String {
    let authority = server
        .base_url
        .strip_prefix("http://")
        .unwrap_or(&server.base_url);
    text.replace(&server.base_url, "{BASE}")
        .replace(authority, "{HOST}")
}

fn normalize(body: &[u8], server: &SipiServer) -> String {
    scrub_str(&String::from_utf8_lossy(body), server)
}

fn compare_json(
    sb: &[u8],
    rb: &[u8],
    subject: &SipiServer,
    reference: &SipiServer,
    allow: &DiffAllowlist,
) -> BodyMatch {
    let sn = normalize(sb, subject);
    let rn = normalize(rb, reference);
    let mut sv: Value = match serde_json::from_str(&sn) {
        Ok(v) => v,
        Err(e) => return BodyMatch::Mismatch(format!("subject JSON parse error: {e}")),
    };
    let mut rv: Value = match serde_json::from_str(&rn) {
        Ok(v) => v,
        Err(e) => return BodyMatch::Mismatch(format!("reference JSON parse error: {e}")),
    };
    // Mask allowlisted fields on both sides, then canonicalise set-like
    // (scalar) arrays whose order is unspecified across serde_json vs jansson.
    for ptr in &allow.json_masks {
        mask_json(&mut sv, ptr);
        mask_json(&mut rv, ptr);
    }
    sort_scalar_arrays(&mut sv);
    sort_scalar_arrays(&mut rv);
    if sv == rv {
        BodyMatch::Match
    } else {
        BodyMatch::Mismatch(format!(
            "JSON differs:\n    subject={sv}\n    reference={rv}"
        ))
    }
}

/// Replace the value at an RFC 6901 pointer with null on both sides (no-op if
/// the pointer is absent), so an allowlisted field divergence doesn't fail the
/// whole-body comparison.
fn mask_json(value: &mut Value, pointer: &str) {
    if let Some(slot) = value.pointer_mut(pointer) {
        *slot = Value::Null;
    }
}

/// Recursively sort arrays whose elements are all scalars (strings/numbers/
/// bools) — these are set-like IIIF fields (extraFeatures, extraFormats, …)
/// whose order is unspecified and differs across the two JSON writers. Arrays
/// containing objects/arrays keep their order (it may be significant).
fn sort_scalar_arrays(value: &mut Value) {
    match value {
        Value::Array(items) => {
            let all_scalar = items
                .iter()
                .all(|i| !matches!(i, Value::Array(_) | Value::Object(_)));
            for item in items.iter_mut() {
                sort_scalar_arrays(item);
            }
            if all_scalar {
                items.sort_by_key(|a| a.to_string());
            }
        }
        Value::Object(map) => {
            for (_, v) in map.iter_mut() {
                sort_scalar_arrays(v);
            }
        }
        _ => {}
    }
}

fn compare_image(sb: &[u8], rb: &[u8], ct: &str) -> BodyMatch {
    // The `image` crate ships jpeg/png/tiff only; jp2 falls to a length
    // band. ICC creation timestamps make image bodies non-byte-deterministic
    // off the approval path (prod-shape binaries don't set SOURCE_DATE_EPOCH,
    // ADR-0002), so compare decoded dimensions rather than bytes.
    if ct.contains("jp2") || ct.contains("jpx") {
        return length_band(sb, rb);
    }
    match (image::load_from_memory(sb), image::load_from_memory(rb)) {
        (Ok(si), Ok(ri)) => {
            if si.dimensions() == ri.dimensions() {
                BodyMatch::Match
            } else {
                BodyMatch::Mismatch(format!(
                    "image dims subject={:?} reference={:?}",
                    si.dimensions(),
                    ri.dimensions()
                ))
            }
        }
        _ => length_band(sb, rb),
    }
}

fn length_band(a: &[u8], b: &[u8]) -> BodyMatch {
    let (la, lb) = (a.len(), b.len());
    let max = la.max(lb).max(1) as f64;
    if (la as f64 - lb as f64).abs() / max <= LENGTH_BAND {
        BodyMatch::Match
    } else {
        BodyMatch::Mismatch(format!(
            "byte-length band exceeded: subject={la} reference={lb} (tol {:.0}%)",
            LENGTH_BAND * 100.0
        ))
    }
}
