/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One-shot fixture generator for the crafted malformed-image regression
 * suite. Run by a developer to (re)generate the committed fixtures under
 * `test/_test_data/images/malformed/`; not invoked by the normal CI build.
 * Commit both this source file and any generated image files (via Git LFS)
 * so the fixtures are reproducible.
 *
 * Each fixture is produced by its own emit function, added to `kGenerators`
 * below as it lands. This scaffold intentionally ships with zero fixtures;
 * later changes add per-fixture emit functions here.
 *
 * Run:
 *   bazel run //test/unit/fixtures:generate_malformed_images -- \
 *     test/_test_data/images/malformed/
 */

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/*!
 * Signature every per-fixture emit function follows: write one malformed
 * fixture into `out_dir` and return true on success. Add new emit functions
 * above `main` and register them in `kGenerators`.
 */
using GeneratorFn = bool (*)(const std::filesystem::path &out_dir);

// Extension point: later changes append `{ "defect_name", &emitDefectName }`
// entries here, one per malformed fixture.
struct Generator
{
  std::string name;
  GeneratorFn emit;
};

const std::vector<Generator> kGenerators{};

}// namespace

int main(int argc, char **argv)
{
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <output_dir>\n", argv[0]);
    return 1;
  }

  const std::filesystem::path out_dir{ argv[1] };
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    std::fprintf(stderr, "failed to create %s: %s\n", out_dir.string().c_str(), ec.message().c_str());
    return 1;
  }

  std::printf("Generating %zu malformed-image fixture(s) under %s\n", kGenerators.size(), out_dir.string().c_str());
  int failures = 0;
  for (const auto &generator : kGenerators) {
    if (!generator.emit(out_dir)) {
      std::fprintf(stderr, "  failed: %s\n", generator.name.c_str());
      ++failures;
    } else {
      std::printf("  wrote %s\n", generator.name.c_str());
    }
  }
  if (failures > 0) {
    std::fprintf(stderr, "\n%d fixture(s) failed\n", failures);
    return 2;
  }

  std::printf("\nAll fixtures generated successfully.\n");
  return 0;
}
