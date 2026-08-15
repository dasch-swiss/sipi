//! Regression sweep: run `iiif_parser::parse_request` over every input in the
//! shared corpus (`//src/iiifparser/corpus`). New corpus inputs are exercised
//! automatically. The C++ classifier runs the same sweep
//! (`//src/iiifparser/cpp/classifier:iiif_handler_test`); this is the Rust
//! consumer of the language-neutral corpus (ADR-0021).

use std::fs;
use std::path::PathBuf;

use runfiles::{rlocation, Runfiles};

/// The runfiles MANIFEST, if one is available.
///
/// Bazel presents a test's runfiles as either a materialized symlink tree or a
/// MANIFEST that maps runfiles paths to real (possibly out-of-tree) locations.
/// The RBE cross-compile legs build remotely and test locally under
/// `--remote_download_minimal`, so the tree is *not* materialized there and only
/// the MANIFEST is present — and `RUNFILES_MANIFEST_FILE` is not always exported.
/// `Runfiles::create()` itself resolves the MANIFEST from the env var *or*
/// `<runfiles-dir>/MANIFEST`; this mirrors that discovery so the sweep does not
/// depend on the tree (or on the `_repo_mapping` file) being on local disk.
fn manifest_path() -> Option<PathBuf> {
    if let Some(m) = std::env::var_os("RUNFILES_MANIFEST_FILE") {
        if !m.is_empty() {
            return Some(PathBuf::from(m));
        }
    }
    for var in ["RUNFILES_DIR", "TEST_SRCDIR"] {
        if let Some(dir) = std::env::var_os(var) {
            let manifest = PathBuf::from(dir).join("MANIFEST");
            if manifest.is_file() {
                return Some(manifest);
            }
        }
    }
    None
}

/// Locate every corpus input in the test's runfiles, in either runfiles mode.
///
/// The `seed_corpus` filegroup globs the whole package, so `BUILD.bazel` rides
/// along — it is skipped in both modes.
fn corpus_files() -> Vec<PathBuf> {
    // Primary: the MANIFEST, which lists every corpus file with its real path and
    // is present whether or not the tree is materialized. Each manifest line is
    // `<runfiles-path> <real-path>` (a leading space + escapes only when a path
    // contains spaces — corpus hash filenames and Bazel paths do not).
    if let Some(manifest) = manifest_path() {
        let text = fs::read_to_string(&manifest).expect("could not read the runfiles manifest");
        let files: Vec<PathBuf> = text
            .lines()
            .filter_map(|line| {
                let (rf, real) = line.strip_prefix(' ').unwrap_or(line).split_once(' ')?;
                (rf.contains("/src/iiifparser/corpus/") && !rf.ends_with("/BUILD.bazel"))
                    .then(|| PathBuf::from(real))
            })
            .collect();
        if !files.is_empty() {
            return files;
        }
    }

    // Fallback: a materialized directory tree (no MANIFEST, or it carried no
    // corpus entries). The apparent repo name `sipi` is remapped to the main repo
    // by the `rlocation!` macro via the bzlmod repo mapping.
    let r = Runfiles::create().expect("runfiles unavailable");
    let dir = rlocation!(r, "sipi/src/iiifparser/corpus")
        .filter(|d| d.is_dir())
        .expect("corpus not found: no runfiles manifest and no materialized tree");
    fs::read_dir(&dir)
        .expect("could not read the corpus directory")
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.is_file() && p.file_name().is_some_and(|n| n != "BUILD.bazel"))
        .collect()
}

#[test]
fn every_corpus_input_parses() {
    let files = corpus_files();

    // A broken runfiles path would resolve an empty/wrong set and sweep nothing —
    // assert the sweep was non-empty so it cannot vacuously pass.
    assert!(!files.is_empty(), "corpus sweep found no inputs");

    for path in &files {
        // Read raw bytes and decode lossily rather than `read_to_string`, so a
        // non-UTF-8 seed (the corpus began as a libFuzzer seed corpus) is swept
        // as its real content, not silently dropped to an empty string.
        let bytes = fs::read(path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()));
        let contents = String::from_utf8_lossy(&bytes);
        // The contract is total: no corpus input may panic the parser. The
        // Ok/Err classification itself is asserted by the unit-test goldens.
        let _ = iiif_parser::parse_request(&contents);
    }
}
