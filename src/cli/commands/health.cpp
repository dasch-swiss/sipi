/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "cli/commands/health.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <curl/curl.h>

namespace Sipi::cli {

namespace {

// Swallow the response body so it never lands on stdout — only the exit code
// matters for a healthcheck.
size_t discard_body(char *, size_t size, size_t nmemb, void *) { return size * nmemb; }

}// namespace

int cmd_health(const HealthArgs &args)
{
  // `curl_global_init` is performed by LibraryInitialiser in main() before any
  // subcommand callback fires, so a plain `curl_easy_init` is safe here.
  const std::string url = "http://127.0.0.1:" + std::to_string(args.port) + "/health";

  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
  if (curl == nullptr) {
    std::fprintf(stderr, "health: failed to initialise HTTP client\n");
    return EXIT_FAILURE;
  }

  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 1000L);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 2000L);// fail fast; never hang the probe
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, discard_body);

  const CURLcode rc = curl_easy_perform(curl.get());
  long status = 0;
  if (rc == CURLE_OK) { curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status); }

  if (rc != CURLE_OK) {
    std::fprintf(stderr, "health: %s failed: %s\n", url.c_str(), curl_easy_strerror(rc));
    return EXIT_FAILURE;
  }
  if (status != 200) {
    std::fprintf(stderr, "health: %s returned HTTP %ld\n", url.c_str(), status);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}// namespace Sipi::cli
