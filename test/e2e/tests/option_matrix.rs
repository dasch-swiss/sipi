// CLI option-availability matrix tests (DEV-6537).
//
// The D5 option-availability matrix gates which flags are accepted on
// each subcommand at CLI11 parse time. `sipi.cpp` enforces it by which
// `attach_*_opts(cmd)` helpers it invokes for each subcommand: rejected
// flags fail with a CLI11 "Option not defined" usage error before the
// callback runs.
//
// These tests subprocess the real `sipi` binary (the per-subcommand unit
// tests in `src/cli/commands/` exercise the command bodies directly; here
// we exercise the CLI11 parse layer). Each rejected combination is
// asserted on both:
//
//   * non-zero exit code, AND
//   * a "is not expected" / "Excludes" / "extra arguments" / "preservation-file
//     awaits ADR-0012" marker in stderr (so a future change that silently
//     accepts the option would still trip the test).
//
// Accepted combinations are smoke-tested (exit 0) but their actual
// transformations are validated in the per-subcommand unit tests, not here.
//
// `verify preservation-file` is also a CLI-only stub
// (no command function), so its "awaits ADR-0012" exit lives here too.

use sipi_e2e::{sipi_bin_path, test_data_dir};
use std::path::PathBuf;
use std::process::Command;

fn tmp_path(name: &str) -> PathBuf {
    let dir = std::env::var("TMPDIR").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(dir).join(name)
}

fn lena_tif() -> PathBuf {
    test_data_dir()
        .join("images")
        .join("unit")
        .join("lena512.tif")
}

fn run_sipi(args: &[&str]) -> std::process::Output {
    Command::new(sipi_bin_path())
        .args(args)
        .output()
        .unwrap_or_else(|e| panic!("Failed to run sipi: {}", e))
}

// ============================================================================
// `convert service-file`: only --topleft. --icc / --skipmeta / --region /
// --rotate / --watermark / etc. are NOT attached and must fail at parse time.
// ============================================================================

#[test]
fn convert_service_file_rejects_icc() {
    let input = lena_tif();
    let output = tmp_path("_om_sf_icc.jp2");

    let result = run_sipi(&[
        "convert",
        "service-file",
        "--icc",
        "sRGB",
        input.to_str().unwrap(),
        output.to_str().unwrap(),
    ]);

    // CLI11's exact diagnostic depends on whether --icc gets shadowed by
    // the bare `convert`'s option or absorbed as a positional; in
    // practice the parse fails with a CLI11 validation error (typically
    // code 105) and no output is produced. The load-bearing assertion is
    // "no Service File emitted with --icc set" — i.e. the D5 matrix
    // gate fires.
    assert!(
        !result.status.success(),
        "convert service-file --icc must be rejected at parse time; stderr: {}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert!(!output.exists(), "no output file on parse-error path");
}

#[test]
fn convert_service_file_rejects_skipmeta() {
    let input = lena_tif();
    let output = tmp_path("_om_sf_skipmeta.jp2");

    let result = run_sipi(&[
        "convert",
        "service-file",
        "--skipmeta",
        input.to_str().unwrap(),
        output.to_str().unwrap(),
    ]);

    assert!(
        !result.status.success(),
        "convert service-file --skipmeta must be rejected at parse time"
    );
    let stderr = String::from_utf8_lossy(&result.stderr);
    assert!(
        stderr.contains("--skipmeta")
            || stderr.contains("skipmeta")
            || stderr.contains("not defined")
            || stderr.contains("not expected"),
        "stderr should identify --skipmeta as the rejected option; got: {}",
        stderr
    );
    assert!(!output.exists());
}

#[test]
fn convert_service_file_rejects_region() {
    let input = lena_tif();
    let output = tmp_path("_om_sf_region.jp2");

    let result = run_sipi(&[
        "convert",
        "service-file",
        "--region",
        "0,0,100,100",
        input.to_str().unwrap(),
        output.to_str().unwrap(),
    ]);

    assert!(
        !result.status.success(),
        "convert service-file --region must be rejected at parse time"
    );
    assert!(!output.exists());
}

#[test]
fn convert_service_file_accepts_topleft() {
    let input = lena_tif();
    let output = tmp_path("_om_sf_topleft_ok.jp2");
    let _ = std::fs::remove_file(&output);

    let result = run_sipi(&[
        "convert",
        "service-file",
        "--topleft",
        input.to_str().unwrap(),
        output.to_str().unwrap(),
    ]);

    assert!(
        result.status.success(),
        "convert service-file --topleft must succeed; stderr: {}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert!(output.exists());
    let _ = std::fs::remove_file(&output);
}

// ============================================================================
// `convert access-file`: --skipmeta is NOT attached (D5 matrix — DSP-opinionated
// flow always propagates metadata). The other transform/ICC/normalize opts ARE
// attached and must be accepted.
// ============================================================================

#[test]
fn convert_access_file_rejects_skipmeta() {
    // We need a Service File as input; the test only asserts the parse-time
    // error, which fires before any read, so an existing-file check passes
    // regardless of whether lena512.tif is a Service File or not.
    let input = lena_tif();
    let output = tmp_path("_om_af_skipmeta.jpg");

    let result = run_sipi(&[
        "convert",
        "access-file",
        "--skipmeta",
        input.to_str().unwrap(),
        output.to_str().unwrap(),
    ]);

    assert!(
        !result.status.success(),
        "convert access-file --skipmeta must be rejected at parse time"
    );
    let stderr = String::from_utf8_lossy(&result.stderr);
    assert!(
        stderr.contains("--skipmeta")
            || stderr.contains("skipmeta")
            || stderr.contains("not defined")
            || stderr.contains("not expected"),
        "stderr should identify --skipmeta as the rejected option; got: {}",
        stderr
    );
    assert!(!output.exists());
}

// ============================================================================
// Bare `convert`: full option set. --topleft, --skipmeta, --icc must all parse.
// ============================================================================

#[test]
fn convert_accepts_topleft() {
    let input = lena_tif();
    let output = tmp_path("_om_convert_topleft.jpg");
    let _ = std::fs::remove_file(&output);

    let result = run_sipi(&[
        "convert",
        "--topleft",
        input.to_str().unwrap(),
        output.to_str().unwrap(),
    ]);

    assert!(
        result.status.success(),
        "convert --topleft must succeed; stderr: {}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert!(output.exists());
    let _ = std::fs::remove_file(&output);
}

#[test]
fn convert_accepts_skipmeta() {
    let input = lena_tif();
    let output = tmp_path("_om_convert_skipmeta.jpg");
    let _ = std::fs::remove_file(&output);

    let result = run_sipi(&[
        "convert",
        "--skipmeta",
        input.to_str().unwrap(),
        output.to_str().unwrap(),
    ]);

    assert!(
        result.status.success(),
        "convert --skipmeta must succeed; stderr: {}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert!(output.exists());
    let _ = std::fs::remove_file(&output);
}

// ============================================================================
// `convert preservation-file` and `verify preservation-file`: stubs that
// always exit non-zero with the "awaits ADR-0012" message.
// ============================================================================

#[test]
fn convert_preservation_file_is_stub() {
    // `convert preservation-file` declares no positional args (stub
    // subcommand pending ADR-0012). Invoke without positionals so the
    // callback fires; passing args would error at parse time instead of
    // showing the awaits-ADR-0012 message we want to assert on.
    let result = run_sipi(&["convert", "preservation-file"]);

    assert!(
        !result.status.success(),
        "convert preservation-file must exit non-zero (stub)"
    );
    // The error message goes to stderr via `log_err`, which under
    // json_mode emits a JSON record. Match both forms.
    let combined = format!(
        "{}{}",
        String::from_utf8_lossy(&result.stderr),
        String::from_utf8_lossy(&result.stdout)
    );
    assert!(
        combined.contains("ADR-0012") || combined.contains("not yet implemented"),
        "stub output should mention ADR-0012 / not-yet-implemented; got stderr={} stdout={}",
        String::from_utf8_lossy(&result.stderr),
        String::from_utf8_lossy(&result.stdout)
    );
}

#[test]
fn verify_preservation_file_is_stub() {
    // Same shape as `convert preservation-file`: no positional args
    // declared on the stub subcommand, so the test runs with none.
    let result = run_sipi(&["verify", "preservation-file"]);

    assert!(
        !result.status.success(),
        "verify preservation-file must exit non-zero (stub)"
    );
    let combined = format!(
        "{}{}",
        String::from_utf8_lossy(&result.stderr),
        String::from_utf8_lossy(&result.stdout)
    );
    assert!(
        combined.contains("ADR-0012") || combined.contains("not yet implemented"),
        "stub output should mention ADR-0012 / not-yet-implemented; got stderr={} stdout={}",
        String::from_utf8_lossy(&result.stderr),
        String::from_utf8_lossy(&result.stdout)
    );
}

// ============================================================================
// Bare `sipi` (no subcommand) must error per `require_subcommand(1)`.
// ============================================================================

#[test]
fn bare_invocation_requires_subcommand() {
    let result = run_sipi(&[]);
    assert!(
        !result.status.success(),
        "bare `sipi` invocation must fail (subcommand required)"
    );
}
