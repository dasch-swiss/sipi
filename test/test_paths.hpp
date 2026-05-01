// Test-data path helpers shared by SIPI's GoogleTest unit tests.
//
// The two build systems run the test binary from different working
// directories:
//   * CMake / ctest:  cwd = build/test/unit/<comp>/, so the historical
//                     `../../../..` traversal resolves to the workspace.
//   * Bazel cc_test:  cwd = workspace root in runfiles. `../../../..`
//                     escapes the runfiles tree.
//
// `SIPI_WORKSPACE_ROOT` (set by Bazel cc_test rules to ".") and
// `TEST_TMPDIR` (set by Bazel for any test run) are honoured when set,
// with the historical CMake-relative paths as fallbacks. Helpers return
// paths *without* a trailing slash; callers append `"/sub/dir/file"`.

#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace sipi::test {

// Resolve a workspace-relative path. Prefers `SIPI_WORKSPACE_ROOT` (Bazel
// sets this to "." — a runfiles-relative path that resolves correctly
// from the cc_test cwd), falls back to `../../../..` for CMake/ctest.
inline std::string workspace_path(std::string_view subpath = {})
{
  const std::string base = (std::getenv("SIPI_WORKSPACE_ROOT") != nullptr)
                             ? std::string{std::getenv("SIPI_WORKSPACE_ROOT")}
                             : std::string{"../../../.."};
  return subpath.empty() ? base : base + "/" + std::string{subpath};
}

// Path to `test/_test_data`.
inline std::string data_dir() { return workspace_path("test/_test_data"); }

// Path to `config/`.
inline std::string config_dir() { return workspace_path("config"); }

// Writable scratch directory. Prefers `TEST_TMPDIR` (Bazel sets this per
// test run). Falls back to `<data_dir>/images/thumbs` for ctest, which is
// where the existing tests already write their intermediates.
inline std::string tmp_dir()
{
  if (const char *env = std::getenv("TEST_TMPDIR")) { return std::string{env}; }
  return data_dir() + "/images/thumbs";
}

}// namespace sipi::test
