# JP2 decode-hang reproducers (DEV-7080)

Malformed JP2 inputs that wedge `SipiIOJ2k::read_shape` forever inside Kakadu's
`jp2_input_box::read_box_header` (the DEV-7080 hang class). They are pinned here,
in a directory **no fuzz corpus-replay target loads**, so `bazel test //src/...`
never hangs on them. The Phase 3 JP2 decode watchdog is validated against these
inputs by a deadline-bounded unit test (see `docs/adr`/the watchdog design note),
never by seeding them into a fuzz corpus.

**Do not move these into any `corpus/` directory or fuzz seed set.** libFuzzer
corpus replay would hang the build.

Both are LFS-tracked (`.jp2` extension).

| File | libFuzzer artifact | Source run | Notes |
|---|---|---|---|
| `dev7080_read_shape_hang.jp2` | `timeout-7e09ed207028df93676ff1568eca38c6abdcc091` | run 33254456688 (`fuzz-crashes-j2k`) | Original DEV-7080 reproducer, 5745 bytes |
| `nightly_j2k_read_shape_hang.jp2` | `timeout-a447eb9527d20db0fa86277f3f3fd7f81a1d634f` | run 33833567222 (nightly 2026-09-04, `fuzz-crashes-j2k`) | Mutation-found instance of the same hang class, 426 bytes |
