// Test-data path helpers shared by SIPI's GoogleTest unit + approval tests.
//
// The two build systems run the test binary from different working
// directories:
//   * CMake / ctest:  cwd = build/test/<unit|approval>/<...>/. The depth
//                     to the workspace root differs:
//                       unit tests       (test/unit/<comp>/)  → 4 levels up
//                       approval tests   (test/approval/)     → 3 levels up
//                     Each helper takes the appropriate fallback string.
//   * Bazel cc_test:  cwd = workspace root in runfiles. `../../*` escapes
//                     the runfiles tree, so we use `SIPI_WORKSPACE_ROOT`
//                     (set to "." by the cc_test rules) instead.
//
// `SIPI_WORKSPACE_ROOT` and `TEST_TMPDIR` are honoured when set (Bazel
// sets both), with the historical CMake-relative paths as fallbacks.
// Helpers return paths *without* a trailing slash; callers append
// `"/sub/dir/file"`.

#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace sipi::test {

namespace detail {

// Internal: returns SIPI_WORKSPACE_ROOT if set, else the supplied
// fallback. Callers usually go through one of the named helpers below.
inline std::string workspace_root(std::string_view fallback)
{
  if (const char *env = std::getenv("SIPI_WORKSPACE_ROOT")) { return std::string{env}; }
  return std::string{fallback};
}

}// namespace detail

// Workspace path for **unit tests** (cwd = build/test/unit/<comp>/, so
// `../../../..` reaches the workspace root). Pass a `subpath` like
// `"test/_test_data"` to walk into a subtree.
inline std::string workspace_path_for_unit(std::string_view subpath = {})
{
  const std::string base = detail::workspace_root("../../../..");
  return subpath.empty() ? base : base + "/" + std::string{subpath};
}

// Workspace path for **approval tests** (cwd = build/test/approval/,
// so `../../..` reaches the workspace root).
inline std::string workspace_path_for_approval(std::string_view subpath = {})
{
  const std::string base = detail::workspace_root("../../..");
  return subpath.empty() ? base : base + "/" + std::string{subpath};
}

// Path to `test/_test_data`. Default is the unit-test cwd-relative form;
// approval tests override via `workspace_path_for_approval("test/_test_data")`.
inline std::string data_dir() { return workspace_path_for_unit("test/_test_data"); }

// Path to `config/`. Same comment as `data_dir()`.
inline std::string config_dir() { return workspace_path_for_unit("config"); }

// Writable scratch directory. Prefers `TEST_TMPDIR` (Bazel sets this per
// test run). Falls back to `<unit data_dir>/images/thumbs` for ctest,
// which is where the existing tests already write their intermediates.
inline std::string tmp_dir()
{
  if (const char *env = std::getenv("TEST_TMPDIR")) { return std::string{env}; }
  return data_dir() + "/images/thumbs";
}

}// namespace sipi::test
