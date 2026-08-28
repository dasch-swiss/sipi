# Fuzz dictionaries

libFuzzer token dictionaries for the codec fuzz targets (DEV-7066), vendored
verbatim from AFL++.

- Source: [AFLplusplus/AFLplusplus](https://github.com/AFLplusplus/AFLplusplus),
  `dictionaries/` directory, `stable` branch.
- Fetched 2026-08-28 from:
  - `https://raw.githubusercontent.com/AFLplusplus/AFLplusplus/stable/dictionaries/tiff.dict`
  - `https://raw.githubusercontent.com/AFLplusplus/AFLplusplus/stable/dictionaries/jpeg.dict`
  - `https://raw.githubusercontent.com/AFLplusplus/AFLplusplus/stable/dictionaries/png.dict`
- License: Apache-2.0 (AFL++ project license).
- Contents are unmodified from upstream.

There is no canonical JP2/JPEG-2000 dictionary upstream, so the `j2k` fuzz
target runs without one until nightly coverage data shows it is needed.

## Why these aren't a Bazel target

`rules_fuzzing`'s `dictionary` attribute only reaches libFuzzer through its
Python launcher via `FUZZER_DICTIONARY_PATH`, and this repo executes the
`..._bin` binary directly rather than through the launcher (the sandbox would
discard corpus growth). The dictionaries are therefore passed as explicit
`-dict=` flags by the `justfile` fuzz recipes instead of wired through a BUILD
attribute.
