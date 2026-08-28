/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Shared libFuzzer harness for the `SipiIO` codec handlers (TIFF/JPEG/PNG/JP2).
//
// `SipiIO` has no in-memory decode overload anywhere — every handler's
// `read_shape`/`read` take a filesystem path — so the harness's only option is
// to materialize each fuzzer-supplied buffer as a temp file and drive the
// handler over that path.

#pragma once

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

#include "image/SipiImage.h"

namespace sipi::fuzz {

// One fixed path per process: libFuzzer runs each process single-threaded, so
// a per-iteration temp file with a stable name (truncated and rewritten every
// call) avoids filesystem churn. The pid keeps parallel `-jobs=N` workers from
// colliding on the same path. `TEST_TMPDIR` is honoured when set (Bazel's
// `cc_fuzz_test` replay engine sets it), falling back to the platform temp dir
// otherwise (mirrors `sipi::test::tmp_dir()` in `test/test_paths.h`, which the
// fuzz package intentionally does not depend on).
inline const std::string &fuzz_temp_path(const char *suffix)
{
  static const std::string path = [suffix] {
    const char *base = std::getenv("TEST_TMPDIR");
    std::filesystem::path dir = (base != nullptr) ? std::filesystem::path{ base } : std::filesystem::temp_directory_path();
    return (dir / ("sipi_codec_fuzz_" + std::to_string(getpid()) + suffix)).string();
  }();
  return path;
}

// Writes `data`/`size` to the per-process temp file, then drives `Handler`'s
// `read_shape` and `read` entry points over it. Each call is wrapped in its
// own try/catch so a clean rejection from `read_shape` still lets `read` run.
// A thrown `SipiImageError` (or any other `std::exception`) is the codec
// correctly rejecting malformed input, not a finding — findings are what
// escapes the catch blocks entirely (SIGSEGV/SIGABRT/sanitizer reports).
// `SipiImageError` derives from `std::exception`, so it arrives through the
// `catch (const std::exception &)` clause below. Kakadu's `kdu_exception` is
// an `int`-like type, not a `std::exception`, which is why the bare
// `catch (...)` matters here.
template<typename Handler> int run_decode(const uint8_t *data, size_t size, const char *suffix)
{
  const std::string &path = fuzz_temp_path(suffix);
  {
    std::ofstream out{ path, std::ios::binary | std::ios::trunc };
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
  }

  Handler handler;

  try {
    handler.read_shape(path);
  } catch (const std::exception &) {
  } catch (...) {
  }

  try {
    Sipi::SipiImage img;
    // Qualified to reach the base class's 2-arg convenience overload
    // (`SipiIO.h:172-175`): the concrete handlers only override the 6-arg
    // `read`, which hides all base-class `read` overloads from unqualified
    // lookup on the derived type. The base overload still dispatches
    // virtually into the derived handler's 6-arg override.
    handler.Sipi::SipiIO::read(&img, path);
  } catch (const std::exception &) {
  } catch (...) {
  }

  return 0;
}

}// namespace sipi::fuzz
