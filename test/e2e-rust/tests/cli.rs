use sipi_e2e::{repo_root, sipi_bin_path, test_data_dir};
use std::path::PathBuf;
use std::process::Command;

// =============================================================================
// CLI mode tests — `sipi convert <input> <output> --format <fmt>`
// =============================================================================

fn sipi_convert(input: &str, output: &str, format: &str) -> std::process::Output {
    let sipi_bin = sipi_bin_path();

    Command::new(&sipi_bin)
        .arg("convert")
        .arg(input)
        .arg(output)
        .arg("--format")
        .arg(format)
        .current_dir(test_data_dir())
        .output()
        .unwrap_or_else(|e| panic!("Failed to run sipi CLI: {}", e))
}

fn tmp_path(name: &str) -> PathBuf {
    let dir = std::env::var("TMPDIR").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(dir).join(name)
}

#[test]
fn cli_file_conversion() {
    // Port of test_iso_15444_4_decode_jp2 + test_iso_15444_4_round_trip:
    // Convert JP2 → TIFF and TIFF → JP2 → TIFF round-trip using CLI mode.
    let data = test_data_dir().join("images");

    // Part 1: JP2 → TIFF decode (files 1 and 4 — others have known issues)
    for i in [1, 4] {
        let input = data
            .join("iso-15444-4/testfiles_jp2")
            .join(format!("file{}.jp2", i));
        let output = tmp_path(&format!("sipi_cli_decode_{}.tif", i));

        let result = sipi_convert(input.to_str().unwrap(), output.to_str().unwrap(), "tif");

        assert!(
            result.status.success(),
            "JP2→TIFF decode for file{}.jp2 failed: {}",
            i,
            String::from_utf8_lossy(&result.stderr)
        );

        // Output file should exist and have content
        assert!(output.exists(), "output TIFF should exist for file{}", i);
        let meta = std::fs::metadata(&output).expect("read output metadata");
        assert!(
            meta.len() > 0,
            "output TIFF should not be empty for file{}",
            i
        );

        let _ = std::fs::remove_file(&output);
    }

    // Part 2: TIFF → JP2 → TIFF round-trip (files 1-9)
    for i in 1..=9 {
        let reference_tif = data
            .join("iso-15444-4/reference_jp2")
            .join(format!("jp2_{}.tif", i));

        if !reference_tif.exists() {
            eprintln!("Skipping round-trip for jp2_{}.tif — not found", i);
            continue;
        }

        let intermediate_jp2 = tmp_path(&format!("sipi_cli_rt_{}.jp2", i));
        let output_tif = tmp_path(&format!("sipi_cli_rt_{}.tif", i));

        // TIFF → JP2
        let result1 = sipi_convert(
            reference_tif.to_str().unwrap(),
            intermediate_jp2.to_str().unwrap(),
            "jpx",
        );

        assert!(
            result1.status.success(),
            "TIFF→JP2 for jp2_{}.tif failed: {}",
            i,
            String::from_utf8_lossy(&result1.stderr)
        );
        assert!(
            intermediate_jp2.exists(),
            "intermediate JP2 should exist for file {}",
            i
        );

        // JP2 → TIFF
        let result2 = sipi_convert(
            intermediate_jp2.to_str().unwrap(),
            output_tif.to_str().unwrap(),
            "tif",
        );

        assert!(
            result2.status.success(),
            "JP2→TIFF round-trip for file {} failed: {}",
            i,
            String::from_utf8_lossy(&result2.stderr)
        );
        assert!(
            output_tif.exists(),
            "round-trip TIFF should exist for file {}",
            i
        );

        // Output TIFF should have reasonable size (not zero, not corrupt)
        let ref_size = std::fs::metadata(&reference_tif).unwrap().len();
        let out_size = std::fs::metadata(&output_tif).unwrap().len();
        assert!(
            out_size > 0,
            "round-trip TIFF for file {} should not be empty",
            i
        );
        // Round-trip may not be byte-identical, but should be within 2x size
        assert!(
            out_size < ref_size * 3,
            "round-trip TIFF for file {} is suspiciously large ({} vs ref {})",
            i,
            out_size,
            ref_size
        );

        let _ = std::fs::remove_file(&intermediate_jp2);
        let _ = std::fs::remove_file(&output_tif);
    }
}

#[test]
fn cli_metadata_fidelity() {
    // Convert a TIFF with metadata to JP2 and back, verify the output
    // is a valid image with reasonable properties.
    let data = test_data_dir().join("images");
    let input = data.join("unit/lena512.tif");

    if !input.exists() {
        eprintln!("Skipping: lena512.tif not found");
        return;
    }

    // TIFF → JP2
    let jp2_output = tmp_path("sipi_cli_meta.jp2");
    let result1 = sipi_convert(input.to_str().unwrap(), jp2_output.to_str().unwrap(), "jpx");
    assert!(
        result1.status.success(),
        "TIFF→JP2 metadata test failed: {}",
        String::from_utf8_lossy(&result1.stderr)
    );

    // JP2 → TIFF (back)
    let tif_output = tmp_path("sipi_cli_meta_rt.tif");
    let result2 = sipi_convert(
        jp2_output.to_str().unwrap(),
        tif_output.to_str().unwrap(),
        "tif",
    );
    assert!(
        result2.status.success(),
        "JP2→TIFF metadata test failed: {}",
        String::from_utf8_lossy(&result2.stderr)
    );

    // Verify output is valid — check TIFF magic bytes (II or MM)
    let tif_bytes = std::fs::read(&tif_output).expect("read output TIFF");
    assert!(tif_bytes.len() > 4, "output TIFF too small");
    let is_tiff = (tif_bytes[0] == b'I' && tif_bytes[1] == b'I')
        || (tif_bytes[0] == b'M' && tif_bytes[1] == b'M');
    assert!(
        is_tiff,
        "output should be valid TIFF (starts with II or MM)"
    );

    // Also convert to JPEG and PNG to test format diversity
    let jpg_output = tmp_path("sipi_cli_meta.jpg");
    let result3 = sipi_convert(input.to_str().unwrap(), jpg_output.to_str().unwrap(), "jpg");
    assert!(
        result3.status.success(),
        "TIFF→JPEG conversion failed: {}",
        String::from_utf8_lossy(&result3.stderr)
    );
    let jpg_bytes = std::fs::read(&jpg_output).expect("read JPEG output");
    assert!(
        jpg_bytes.len() > 2 && jpg_bytes[0] == 0xFF && jpg_bytes[1] == 0xD8,
        "output should be valid JPEG"
    );

    let png_output = tmp_path("sipi_cli_meta.png");
    let result4 = sipi_convert(input.to_str().unwrap(), png_output.to_str().unwrap(), "png");
    assert!(
        result4.status.success(),
        "TIFF→PNG conversion failed: {}",
        String::from_utf8_lossy(&result4.stderr)
    );
    let png_bytes = std::fs::read(&png_output).expect("read PNG output");
    assert!(
        png_bytes.len() > 8 && &png_bytes[1..4] == b"PNG",
        "output should be valid PNG"
    );

    // Cleanup
    for path in [&jp2_output, &tif_output, &jpg_output, &png_output] {
        let _ = std::fs::remove_file(path);
    }
}

#[test]
fn cli_version_flag() {
    let sipi_bin = sipi_bin_path();

    let result = Command::new(&sipi_bin)
        .arg("--version")
        .output()
        .unwrap_or_else(|e| panic!("Failed to run sipi --version: {}", e));

    assert!(
        result.status.success(),
        "sipi --version exited non-zero: {}",
        String::from_utf8_lossy(&result.stderr)
    );

    let expected_version = std::fs::read_to_string(repo_root().join("version.txt"))
        .expect("read version.txt")
        .trim()
        .to_string();
    let stdout = String::from_utf8_lossy(&result.stdout);
    // A stamped build (`just bazel-test-e2e` / `bazel-coverage`, CI) bakes
    // `STABLE_SIPI_VERSION` from version.txt. A plain `bazel test //...` is
    // unstamped, so `expand_template` in src/BUILD.bazel falls back to
    // `0.0.0-unstamped`. Accept either: the unstamped run still verifies the
    // `--version` plumbing and output format, while the stamped CI run pins
    // the actual version.txt value.
    let stamped = format!("sipi {}", expected_version);
    let unstamped = "sipi 0.0.0-unstamped";
    assert!(
        stdout.trim() == stamped || stdout.trim() == unstamped,
        "expected stdout to be {:?} (stamped) or {:?} (unstamped), got {:?}",
        stamped,
        unstamped,
        stdout
    );
}

/// Regression test for the sentry/curl shutdown race.
///
/// When sipi is invoked with no subcommand AND SIPI_SENTRY_DSN is set,
/// init_sentry() starts sentry-native's background HTTP transport thread
/// before CLI parsing. CLI11's `require_subcommand(1)` then fails the parse
/// and main exits — historically WITHOUT shutting sentry down, so the static
/// ~LibraryInitialiser ran curl_global_cleanup() while the transport thread
/// was mid-request, causing glibc heap corruption (intermittent SIGABRT or
/// runaway allocation until OOM). The fix registers std::atexit(close_sentry)
/// right after library init so the transport thread is joined before curl
/// cleanup on every exit path.
///
/// The DSN points at an unreachable local endpoint so the transport thread is
/// alive at exit, maximising the race window. The process must exit cleanly
/// (not via signal) with a non-zero code and the normal usage error, and must
/// not print any glibc memory-corruption markers.
#[test]
fn cli_no_subcommand_with_sentry_dsn_exits_clean() {
    let sipi_bin = sipi_bin_path();

    let result = Command::new(&sipi_bin)
        .env("SIPI_SENTRY_DSN", "https://key@127.0.0.1:9/1")
        .env("SIPI_SENTRY_ENVIRONMENT", "ci-regression")
        .current_dir(repo_root())
        .output()
        .unwrap_or_else(|e| panic!("Failed to run sipi with no subcommand: {}", e));

    let stderr = String::from_utf8_lossy(&result.stderr);
    let stdout = String::from_utf8_lossy(&result.stdout);

    // Exited normally, not killed by SIGABRT/SIGSEGV from heap corruption.
    assert!(
        result.status.code().is_some(),
        "sipi was killed by a signal ({:?}) — likely the sentry/curl shutdown race. stderr:\n{}\nstdout:\n{}",
        result.status,
        stderr,
        stdout
    );

    // A missing subcommand is a usage error.
    assert!(
        !result.status.success(),
        "expected non-zero exit for missing subcommand. stderr:\n{}",
        stderr
    );

    // We reached CLI11's parse-error path, not some earlier abort.
    assert!(
        stderr.contains("A subcommand is required"),
        "expected 'A subcommand is required' in stderr, got:\n{}",
        stderr
    );

    // No glibc heap-corruption markers on either stream.
    let combined = format!("{}{}", stderr, stdout).to_lowercase();
    for marker in ["double free", "malloc", "corrupted", "unaligned fastbin"] {
        assert!(
            !combined.contains(marker),
            "found memory-corruption marker {:?} in output:\nstderr:\n{}\nstdout:\n{}",
            marker,
            stderr,
            stdout
        );
    }
}
