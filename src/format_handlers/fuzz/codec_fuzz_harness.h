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
// Both entry points return a `Result`, which the harness intentionally checks
// only for presence: a codec rejecting malformed input reports that through
// the `Result` holding no value, not through an exception, so the harness's
// finding surface is what escapes the catch blocks entirely (SIGSEGV/SIGABRT/
// sanitizer reports). The try/catch blocks stay because a decoder can still
// throw en route to producing that `Result`: Kakadu's `kdu_exception` (an
// `int`-like type, not a `std::exception`, which is why the bare `catch (...)`
// matters — SipiIOJ2k.cpp's decode path calls several Kakadu accessors, e.g.
// `codestream.access_siz()`/`jpx_layer.access_colour()`, that are not
// individually try/caught) and `std::bad_alloc`, including the allocation-guard
// throws that stay exception-based by design (`checked_buf_size_or_throw`,
// `memTiffOpen`'s raw `malloc` failures). The `Iptc`/`Exif`/`Icc` metadata
// parsers are `Result`-returning factories and `Xmp`'s constructor is
// infallible (it stores the given bytes verbatim, never parsing them), so
// none of the four metadata types can still throw here.
template<typename Handler> int run_decode(const uint8_t *data, size_t size, const char *suffix)
{
  const std::string &path = fuzz_temp_path(suffix);
  {
    std::ofstream out{ path, std::ios::binary | std::ios::trunc };
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
  }

  Handler handler;

  try {
    if (const auto r = handler.read_shape(path); !r) { /* a rejection is a valid fuzz outcome */ }
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
    if (const auto r = handler.Sipi::SipiIO::read(&img, path); !r) { /* a rejection is a valid fuzz outcome */ }
  } catch (const std::exception &) {
  } catch (...) {
  }

  return 0;
}

}// namespace sipi::fuzz
