//! Image-root path construction + traversal validation at the request edge
//! (strangler-fig rewrite). The FFI trusts the `resolved_path` it is handed —
//! "validation owned by the Rust edge" (`sipi_ffi.h`) — so the shell reproduces
//! the C++ server's two-stage check before any serve call:
//!
//!  - **R1** ([`contains_traversal`]) is a pure string check on the URL-decoded
//!    identifier (and prefix, when `prefix_as_path`), rejecting `..` components
//!    and their single/double percent-encoded variants. Ported 1:1 from
//!    `SipiHttpServer.cpp:205-223`.
//!  - **R2** ([`validate_resolved_path`]) realpath-resolves the built path and
//!    asserts it stays within the resolved image root. Ported from
//!    `SipiHttpServer.cpp:244-266`.
//!
//! The image root + `prefix_as_path` knob come from the engine (`sipi_imgroot` /
//! `sipi_prefix_as_path`) and are cached by the shell at startup; the functions
//! here take them as arguments so they stay pure and unit-testable.

/// R1: a pure string check for path-traversal components on a URL-decoded value.
/// Rejects a bare `..`, a `../` prefix, a `/..` suffix, or an interior `/../`,
/// plus the single- (`%2e%2e`) and double-encoded (`%252e%252e`) forms in case a
/// segment was not fully decoded. Port of the C++ `contains_traversal`.
#[must_use]
pub fn contains_traversal(decoded: &str) -> bool {
    if decoded == ".." || decoded.starts_with("../") || decoded.ends_with("/..") || decoded.contains("/../") {
        return true;
    }
    let lower = decoded.to_ascii_lowercase();
    lower.contains("%2e%2e") || lower.contains("%252e%252e")
}

/// The containment predicate behind R2: a realpath-resolved file path is within
/// the resolved image root iff it is exactly the root or continues with a `/`.
/// The boundary-char test prevents `root=/foo/bar` from matching `/foo/barbaz/…`.
#[must_use]
pub fn is_within(resolved: &str, resolved_root: &str) -> bool {
    if !resolved.starts_with(resolved_root) {
        return false;
    }
    resolved.len() == resolved_root.len() || resolved.as_bytes()[resolved_root.len()] == b'/'
}

/// Build the on-disk request path the way the C++ server does
/// (`SipiHttpServer.cpp:472-475`): `imgroot/prefix/identifier` when
/// `prefix_as_path` and the prefix is non-empty, else `imgroot/identifier`.
/// `prefix` and `identifier` are the already-URL-decoded parser outputs.
#[must_use]
pub fn build_request_path(imgroot: &str, prefix: &str, identifier: &str, prefix_as_path: bool) -> String {
    if prefix_as_path && !prefix.is_empty() {
        format!("{imgroot}/{prefix}/{identifier}")
    } else {
        format!("{imgroot}/{identifier}")
    }
}

/// Outcome of R2 path validation (mirrors the C++ `PathValidation`).
#[derive(Debug, PartialEq, Eq)]
pub enum Resolved {
    /// realpath-resolved, image-root-contained absolute path.
    Ok(String),
    /// realpath() failed — the file does not exist (caller renders 404).
    NotFound,
    /// The resolved path escapes the image root (caller renders 400).
    Traversal,
}

/// R2: realpath-resolve `file_path` and verify it stays within `resolved_root`
/// (already realpath-resolved at startup). `canonicalize` is Rust's realpath:
/// it resolves symlinks and fails if the path does not exist, matching the C++
/// `realpath()` semantics (a missing file → [`Resolved::NotFound`], an escape →
/// [`Resolved::Traversal`]).
#[must_use]
pub fn validate_resolved_path(file_path: &str, resolved_root: &str) -> Resolved {
    match std::fs::canonicalize(file_path) {
        Err(_) => Resolved::NotFound,
        Ok(canon) => {
            let resolved = canon.to_string_lossy();
            if is_within(&resolved, resolved_root) {
                Resolved::Ok(resolved.into_owned())
            } else {
                Resolved::Traversal
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn traversal_literal_components() {
        for s in ["..", "../x", "x/..", "a/../b", "../../etc/passwd", "foo/../../bar"] {
            assert!(contains_traversal(s), "should reject: {s}");
        }
    }

    #[test]
    fn traversal_percent_encoded_variants() {
        // Residual single/double-encoded dot-dot after a single decode pass.
        for s in ["%2e%2e/x", "x/%2E%2E", "%252e%252e", "A%2e%2eB"] {
            assert!(contains_traversal(s), "should reject: {s}");
        }
    }

    #[test]
    fn traversal_allows_legitimate_identifiers() {
        for s in ["lena512.tif", "iiif/2/image.jpx", "3KtDiJm4XxY-1PUUCffsF4S.jpx", "a.b.c", "..hidden"] {
            assert!(!contains_traversal(s), "should allow: {s}");
        }
    }

    #[test]
    fn within_exact_and_subpath() {
        assert!(is_within("/srv/images", "/srv/images"));
        assert!(is_within("/srv/images/a/b.tif", "/srv/images"));
    }

    #[test]
    fn within_rejects_sibling_prefix_and_outside() {
        // The boundary-char guard: /srv/images must not match /srv/imagesX.
        assert!(!is_within("/srv/imagesX/b.tif", "/srv/images"));
        assert!(!is_within("/etc/passwd", "/srv/images"));
    }

    #[test]
    fn path_build_prefix_modes() {
        assert_eq!(build_request_path("/img", "iiif/2", "a.tif", true), "/img/iiif/2/a.tif");
        assert_eq!(build_request_path("/img", "iiif/2", "a.tif", false), "/img/a.tif");
        // Empty prefix never produces a double slash.
        assert_eq!(build_request_path("/img", "", "a.tif", true), "/img/a.tif");
    }

    #[test]
    fn validate_missing_is_not_found() {
        assert_eq!(validate_resolved_path("/no/such/file/here.tif", "/no/such"), Resolved::NotFound);
    }
}
