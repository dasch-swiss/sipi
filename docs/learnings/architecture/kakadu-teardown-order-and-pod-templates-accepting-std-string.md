---
title: "Two codec-boundary gotchas: destroy a Kakadu codestream before its target unwinds, and never read a class type through `&val, sizeof(T)`"
date: 2026-09-08
category: "architecture"
component: "cpp_module"
module: "`SipiIOJ2k::write` (Kakadu `kdu_codestream` / `jpx_target` lifetimes), `Exif::addKeyVal<T>` in `src/metadata/cpp/exif.h`"
problem_type: "resource-lifetime-and-generic-template-misuse"
severity: "high"
symptoms: "(1) `sipi convert <tif> out.jpx` segfaulted (exit 139) on a 1x1 MinIsBlack TIFF with three samples per pixel; ASan: heap-use-after-free in `kd_compressed_output::flush_buf` called from `kdu_codestream::destroy()` in the catch of `SipiIOJ2k::write`, freed by `~jpx_target`. (2) Every EXIF ASCII tag read from a TIFF EXIF IFD (DateTimeOriginal, LensMake, LensModel, …) came out as 24 bytes of garbage; a 2025 test comment had blamed 'platform-dependent Exiv2 typing'; ASan flagged the read as container-overflow on the short-string buffer."
root_cause: "(1) The JPX target was a local inside the `try`; on a Kakadu error, unwinding destroyed it (freeing the codestream's output) before the `catch` destroyed the codestream that flushes into it. (2) A generic `addKeyVal<T>` fed every value to Exiv2 as `(unsigned char *)&val, sizeof(T)`, which is only meaningful for scalar T; for `std::string` it serialises the 24-byte string object."
tags: [kakadu, kdu_codestream, jpx_target, raii, destructor-order, use-after-free, exiv2, addKeyVal, sizeof, template, std::string, exif, format_handlers, metadata, sipi, cpp]
related:
  - ../debugging/fuzz-leg-reports-one-finding-at-a-time.md
issue: "DEV-7080"
---

# Two codec-boundary gotchas: destroy a Kakadu codestream before its target unwinds, and never read a class type through `&val, sizeof(T)`

Both from the SIPI fuzz-nightly triage of 2026-09-08 (PR #804). They are unrelated in mechanism
and share one lesson: **a vendor API's ownership and type contracts are not visible in our code,
so they must be made explicit where our code crosses into the vendor.** Both bugs were old, both
were reachable from ordinary inputs, and both were found only because a fuzz leg happened to get
past a shallower finding.

## Problem

**Gotcha 1 — Kakadu teardown order.** `SipiIOJ2k::write` looked like this (simplified):

```cpp
kdu_codestream codestream;              // outside the try "so the catch can destroy it"
try {
  jp2_family_tgt jp2_ultimate_tgt;
  jpx_target jpx_out;                    // inside the try
  jpx_out.open(&jp2_ultimate_tgt, &membroker);
  jpx_codestream_target jpx_stream = jpx_out.add_codestream();
  ...
  codestream.create(&siz, output, ...);  // output is jpx_stream's box
  ... encode ...
  codestream.destroy();
  jpx_out.close();
} catch (kdu_exception e) {
  if (codestream.exists()) { codestream.destroy(); }   // ← runs AFTER ~jpx_target
  return std::unexpected(...kWriteFailed...);
}
```

When Kakadu raised mid-write (here: a MinIsBlack image with three channels, "`jp2_channels`
indicates more colour channels than the colour space has"), the stack unwound `jpx_out` first.
`~jpx_target` → `close()` → `~jx_target` → `jx_codestream_target::destroy()` freed the codestream
target. Then the catch ran `codestream.destroy()`, whose `kd_compressed_output::flush_buf()` read
the freed target. A comment on the declaration said the JPX target was "deliberately NOT closed on
the error path", which was true of the explicit `close()` and false of the destructor.

**Gotcha 2 — a POD-shaped template accepting a class.** `Exif::addKeyVal` (since 2016):

```cpp
template<class T> void addKeyVal(uint16_t tag, const std::string &groupName, const T &val)
{
  ...
  if (typeid(T) == typeid(std::string)) v = Exiv2::Value::create(Exiv2::asciiString);
  else if (typeid(T) == typeid(uint16_t)) ...
  v->read((unsigned char *)&val, sizeof(T), Exiv2::littleEndian);   // ← 24 bytes of a std::string object
  exifData.add(key, v.get());
}
```

The `typeid` chain *knew* about `std::string` and still fed it through the scalar path. Every
`EXIF_DT_STRING` tag in `SipiIOTiff::readExif` (DateTimeOriginal, SubSecTime, LensMake, LensModel,
ImageUniqueID, …) was stored as the libc++ string object's bytes. A test comment from an earlier fix
had observed the garbage and attributed it to "Exiv2 reports the values as `unsignedByte` rather
than `asciiString`" — a plausible story that nobody checked against the code.

## Root Cause

1. **Reverse-destruction order across an ownership edge the compiler cannot see.** Kakadu's
   `kdu_codestream` is an interface object with explicit `create`/`destroy`; `jpx_target` is RAII.
   Mixing an explicit-lifetime object declared *outside* a scope with an RAII object declared
   *inside* it inverts the dependency order on the exceptional path only, which is why the success
   path (explicit `destroy()` then `close()`) never showed it.
2. **`sizeof(T)` is a statement about the object's storage, not its value.** A template that
   reinterprets `&val` is correct exactly for trivially-copyable scalar `T`; a branch that
   recognises `std::string` by `typeid` and then falls through to the same `read` is a bug the
   type system would have caught with `if constexpr`.

## Solution

**Gotcha 1** (`6cd46ff0`): an RAII guard declared **after** `jpx_out`, so it is destroyed before it:

```cpp
    jpx_target jpx_out;
    jpx_out.open(&jp2_ultimate_tgt, &membroker);
    jpx_codestream_target jpx_stream = jpx_out.add_codestream();
    jpx_layer_target jpx_layer = jpx_out.add_layer();

    struct CodestreamGuard
    {
      kdu_codestream &cs;
      ~CodestreamGuard()
      {
        if (cs.exists()) { cs.destroy(); }
      }
    } codestream_guard{ codestream };
```

The success path still destroys the codestream itself (the guard then finds none); the catch no
longer touches it. `sipi convert` on the reproducer now fails with Kakadu's channel-count error
and `kWriteFailed` instead of SIGSEGV. Fixture `tiff_minisblack_three_channel.tif`, test
`MalformedTiff.MinIsBlackThreeChannelJpxWriteFailsCleanly`.

**Gotcha 2** (`d9c7704d`): branch on the type at compile time and use Exiv2's string overload:

```cpp
    if constexpr (std::is_same_v<T, std::string>) {
      v = Exiv2::Value::create(Exiv2::asciiString);
      v->read(val);
      exifData.add(key, v.get());
      return;
    }
    // scalar T: typeid chain + v->read((unsigned char *)&val, sizeof(T), littleEndian)
```

`ExifRationalRegression.LensStringTagsRoundTrip` asserts `LensMake == "Nikon"` and the full
`LensModel` string from the existing `exif_lens_specification.tif` fixture. Red without the fix
(LensModel came back as two garbage bytes), green with it, approval goldens byte-identical.

## Prevention

- **When a vendor object is created explicitly and another is RAII, put an RAII guard for the
  explicit one *after* the RAII one in the same scope.** Reverse destruction then encodes the
  dependency on every exit path, including exceptions. `SipiIOJ2k::read_shape` already does this
  with its `kdu_teardown` struct; `write` now matches. Any new Kakadu `create()` needs the same
  treatment.
- **A comment that says "deliberately not closed on the error path" is a claim about explicit
  calls, not about destructors.** Read the destructor of the type before trusting such a comment.
- **Never write `(unsigned char *)&val, sizeof(T)` in a template without a
  `static_assert(std::is_trivially_copyable_v<T>)` or an `if constexpr` branch for the class
  types you accept.** The `typeid` chain in `addKeyVal` is the anti-pattern: runtime type
  recognition followed by a type-blind byte copy.
- **A test comment that explains away wrong output is a bug report.** "Platform-dependent Exiv2
  typing" was the previous author's hypothesis for exactly this symptom. Turn such comments into
  failing assertions rather than leaving them as prose.
- **Sanitizer reports on our own metadata code are real** even when they arrive alongside false
  positives from the fuzz link (see the container-overflow learning): this one was a
  container-overflow too, but on a stack `std::string` our code built, and it reproduced without
  ASan.

## References

- PR #804, merged 2026-09-08; commits `6cd46ff0` (JPX write guard), `d9c7704d` (EXIF strings).
- `src/format_handlers/cpp/SipiIOJ2k.cpp` (`CodestreamGuard`, and the `kdu_teardown` precedent in
  `read_shape`), `src/metadata/cpp/exif.h` (`addKeyVal`).
- Fixtures: `test/_test_data/images/malformed/tiff_minisblack_three_channel.tif`,
  `test/_test_data/images/unit/exif_lens_specification.tif`.
- Kakadu: `jpx_target::close()` → `jx_codestream_target::destroy()` (`apps/jp2/jpx.cpp`,
  `jpx_local.h`), `kd_compressed_output::flush_buf()` (`coresys/compressed/compressed_local.h`).
