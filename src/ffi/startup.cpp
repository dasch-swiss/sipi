/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/startup.h"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <string>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <curl/curl.h>

#include "shttps/util/Error.h"// shttps::Error
#include "formats/SipiIOTiff.h"// Sipi::SipiIOTiff::initLibrary
#include "metadata/xmp.h"// Sipi::xmplock_func, Sipi::xmp_mutex

#include <exiv2/exiv2.hpp>

namespace Sipi::ffi {

LibraryInitialiser &LibraryInitialiser::instance()
{
  // In C++11, initialization of this static local variable happens once and is thread-safe.
  static LibraryInitialiser instance;
  return instance;
}

LibraryInitialiser::LibraryInitialiser()
{
  // Initialise libcurl.
  curl_global_init(CURL_GLOBAL_ALL);

  // Initialise Exiv2, registering namespace sipi. Since this is not thread-safe, it must
  // be done here in the main thread.
  if (!Exiv2::XmpParser::initialize(Sipi::xmplock_func, &Sipi::xmp_mutex)) {
    throw shttps::Error("Exiv2::XmpParser::initialize failed");
  }

  // Inititalise the TIFF library.
  Sipi::SipiIOTiff::initLibrary();
}

LibraryInitialiser::~LibraryInitialiser()
{
  // Clean up libcurl.
  curl_global_cleanup();

  // Clean up Exiv2.
  Exiv2::XmpParser::terminate();
}

std::size_t detect_available_memory()
{
#ifdef __linux__
  // 1. cgroups v2: /sys/fs/cgroup/memory.max
  if (std::ifstream mem_max("/sys/fs/cgroup/memory.max"); mem_max.is_open()) {
    std::string limit_str;
    mem_max >> limit_str;
    if (limit_str != "max") {
      try {
        return static_cast<std::size_t>(std::stoll(limit_str));
      } catch (...) {
        // Parse failure — fall through
      }
    }
  }

  // 2. cgroups v1: /sys/fs/cgroup/memory/memory.limit_in_bytes
  if (std::ifstream mem_limit("/sys/fs/cgroup/memory/memory.limit_in_bytes"); mem_limit.is_open()) {
    long long limit = 0;
    mem_limit >> limit;
    // 9223372036854771712 = kernel "unlimited" sentinel
    if (limit > 0 && limit < 9223372036854771712LL) {
      return static_cast<std::size_t>(limit);
    }
  }

  // 3. /proc/meminfo fallback
  if (std::ifstream meminfo("/proc/meminfo"); meminfo.is_open()) {
    std::string line;
    while (std::getline(meminfo, line)) {
      if (line.rfind("MemTotal:", 0) == 0) {
        // Format: "MemTotal:     16384000 kB"
        long long kb = 0;
        std::sscanf(line.c_str(), "MemTotal: %lld kB", &kb);
        if (kb > 0) return static_cast<std::size_t>(kb) * 1024;
      }
    }
  }
#endif

#ifdef __APPLE__
  // macOS: sysctl hw.memsize
  int64_t memsize = 0;
  size_t len = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0 && memsize > 0) {
    return static_cast<std::size_t>(memsize);
  }
#endif

  return 0;
}

}// namespace Sipi::ffi
