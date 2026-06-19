// Test-data path helpers shared by SIPI's GoogleTest unit + approval tests.
//
// The two build systems run the test binary from different working
// directories:
//   * CMake / ctest:  cwd = build/test/<unit|approval>/<...>/. The depth
//                     to the workspace root differs by layer:
//                       unit tests       (test/unit/<comp>/)  → 4 levels up
//                       approval tests   (test/approval/)     → 3 levels up
//                     Each call site passes the appropriate fallback
//                     traversal explicitly.
//   * Bazel cc_test:  cwd = workspace root in runfiles. `../../*` escapes
//                     the runfiles tree, so we use `SIPI_WORKSPACE_ROOT`
//                     (set to "." by the cc_test rules) instead.
//
// `SIPI_WORKSPACE_ROOT` and `TEST_TMPDIR` are honoured when set (Bazel
// sets both); the explicit fallback is the CMake-build-tree relative
// traversal. Helpers return paths *without* a trailing slash; callers
// append `"/sub/dir/file"`.
//
// Thread-safety: `std::getenv` is not thread-safe relative to
// `setenv`/`putenv`. These helpers are intended for static-init or
// single-threaded test setup only — the test binary never mutates its
// environment after main() begins.

#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace sipi::test {

// Resolve a workspace-relative path. Prefers the Bazel-set
// `SIPI_WORKSPACE_ROOT` env, falls back to `cmake_fallback` (the
// caller-supplied `../`-traversal that walks back to the workspace root
// from the test binary's CMake build directory).
//
// Examples:
//   workspace_path("test/_test_data", "../../../..")   // unit tests
//   workspace_path("test/_test_data", "../../..")      // approval tests
inline std::string workspace_path(std::string_view subpath, std::string_view cmake_fallback)
{
  const std::string root = (std::getenv("SIPI_WORKSPACE_ROOT") != nullptr)
                             ? std::string{std::getenv("SIPI_WORKSPACE_ROOT")}
                             : std::string{cmake_fallback};
  return subpath.empty() ? root : root + "/" + std::string{subpath};
}

// Path to `test/_test_data` from a unit test (cwd = build/test/unit/<comp>/).
inline std::string data_dir() { return workspace_path("test/_test_data", "../../../.."); }

// Path to `config/` from a unit test.
inline std::string config_dir() { return workspace_path("config", "../../../.."); }

// Writable scratch directory. Prefers `TEST_TMPDIR` (Bazel sets this per
// test run, scoped under `bazel-testlogs/<test>/test.outputs/`). Falls
// back to the CMake unit-test convention of writing into
// `test/_test_data/images/thumbs/`.
inline std::string tmp_dir()
{
  if (const char *env = std::getenv("TEST_TMPDIR")) { return std::string{env}; }
  return data_dir() + "/images/thumbs";
}

}// namespace sipi::test
