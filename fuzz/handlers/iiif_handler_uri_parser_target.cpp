/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include "handlers/iiif_handler.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  // parse_iiif_uri is noexcept and returns std::expected — no crash expected.
  // Fuzzer should find memory safety issues, not logic errors.
  std::string input(reinterpret_cast<const char *>(Data), Size);
  auto result = handlers::iiif_handler::parse_iiif_uri(input);

  // Force use of result to prevent optimizer from eliminating the call
  if (result.has_value()) {
    volatile auto type = result->request_type;
    (void)type;
  }
  return 0;
}
