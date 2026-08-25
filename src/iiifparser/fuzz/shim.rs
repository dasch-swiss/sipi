//! The Rust half of the fuzz seam: an `extern "C"` entry point over
//! `iiif_parser::parse_request` for `fuzz_target.cc` to call.

// Fast unsafe check (CI `lint` gate): every `unsafe {}` block must carry a
// `// SAFETY:` comment. `allow`-by-default (clippy `restriction` group), so it
// is enabled here explicitly; CI's `-Dwarnings` promotes it to a hard error.
#![warn(clippy::undocumented_unsafe_blocks)]

use std::slice;

/// Parse one fuzzer-supplied input as an IIIF request URI.
///
/// Invalid UTF-8 is rejected rather than lossy-converted: every production
/// caller (axum path extraction) hands the parser a valid `&str`, so a U+FFFD
/// input would only yield findings unreachable from a real request. The parse
/// result is discarded — `Err` is a normal outcome for a malformed URI; the
/// contract under test is that no input panics.
///
/// This UTF-8-reject rule is what corpus files mean: a seed is a byte string
/// the fuzzer feeds here verbatim, and a non-UTF-8 seed is a no-op. Note that
/// `//src/iiifparser/rust:corpus_regression_test` sweeps the same files with a
/// *lossy* decode, so it may exercise a byte sequence this harness skips.
///
/// # Safety
///
/// `data` must be non-null and point to `len` initialized bytes, per
/// libFuzzer's `LLVMFuzzerTestOneInput` contract.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sipi_fuzz_parse_request(data: *const u8, len: usize) -> i32 {
    // SAFETY: libFuzzer guarantees `data`/`len` describe a live, initialized
    // buffer for the duration of the call.
    let bytes = unsafe { slice::from_raw_parts(data, len) };
    if let Ok(uri) = std::str::from_utf8(bytes) {
        // `black_box`: under `-c opt` (see `.bazelrc` §Fuzzing) the discarded
        // result would otherwise let LLVM prove the whole call dead and delete
        // the code being fuzzed.
        let _ = std::hint::black_box(iiif_parser::parse_request(uri));
    }
    0
}
