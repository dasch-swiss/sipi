//! Test-only JWT construction helpers, shared between `security.rs`
//! (single-server negative-path tests) and `differential.rs` (cross-binary
//! JWT parity tests) — both exercise sipi's `server.decode_jwt` Lua binding,
//! which only supports HS256 (HMAC), so that is the only algorithm modelled
//! here.

use base64::Engine;
use jsonwebtoken::{encode, Algorithm, EncodingKey, Header};

/// Create a valid HS256 JWT for `claims`, signed with `secret`.
#[must_use]
pub fn create_jwt(claims: &serde_json::Value, secret: &str) -> String {
    let header = Header::new(Algorithm::HS256);
    let key = EncodingKey::from_secret(secret.as_bytes());
    encode(&header, claims, &key).expect("JWT encode failed")
}

/// Craft an `alg:none`, unsigned JWT carrying `claims_json` — the classic
/// signature-bypass attempt (a JWT library that trusts the header's `alg`
/// blindly accepts this as valid with no verification).
#[must_use]
pub fn alg_none_token(claims_json: &str) -> String {
    let b64 = base64::engine::general_purpose::URL_SAFE_NO_PAD;
    let header = b64.encode(r#"{"alg":"none","typ":"JWT"}"#);
    let payload = b64.encode(claims_json);
    format!("{header}..{payload}")
}

/// Take a valid signed JWT and swap its payload for `tampered_claims_json`
/// without re-signing — the signature stays valid only for the original
/// payload, so a correct verifier must reject the result.
#[must_use]
pub fn tamper_payload(valid_token: &str, tampered_claims_json: &str) -> String {
    let parts: Vec<&str> = valid_token.split('.').collect();
    assert_eq!(parts.len(), 3, "JWT should have 3 parts");
    let b64 = base64::engine::general_purpose::URL_SAFE_NO_PAD;
    let tampered_payload = b64.encode(tampered_claims_json);
    format!("{}.{}.{}", parts[0], tampered_payload, parts[2])
}
