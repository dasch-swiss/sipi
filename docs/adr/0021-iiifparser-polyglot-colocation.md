---
status: accepted
---

# The iiifparser module is a colocated polyglot; the Rust parser is a domain-typed standalone crate

The `iiifparser` module has two implementations of IIIF URL parsing in two
languages, and they live apart. The C++ classifier (`iiif_handler.cpp`,
`handlers::iiif_handler::parse_iiif_uri`) and the C++ value-object string parsers
(`SipiRegion`/`SipiSize`/`SipiRotation`/`SipiQualityFormat`/`SipiIdentifier`/
`SipiDecodeDims`) sit under `src/iiifparser/`. The production Rust parser is a
single 821-line file, `src/server-rs/src/iiif.rs`, buried inside the monolithic
`//src/server-rs:lib` crate. The shared regression corpus (`src/iiifparser/corpus/`,
240 IIIF-URI files, originally the retired libFuzzer seed corpus — [ADR-0020](0020-oracle-removal.md))
is wired only to the C++ side.

Two problems follow. First, the two implementations of one logical component are
not colocated, so reading them side by side during the strangler migration means
jumping between `src/iiifparser/` and a file inside `server-rs`. Second, the Rust
parser is coupled to the FFI seam: `iiif.rs` imports
`crate::ffi::{SipiFormatType, SipiIiifParams, SipiQualityType, SipiRegionType, SipiSizeType}`
and emits the flattened `#[repr(C)]` `SipiIiifParams` directly. The parser — pure
CPU URL grammar with no engine dependency — therefore cannot be built or reasoned
about without the FFI module that links the C++ engine.

## Decision

**The `iiifparser` module is packaged component-first, language-first, then the
C++ side is split by lifetime, with a language-neutral corpus:**

```
src/iiifparser/
├── corpus/   filegroup seed_corpus — owned by neither language
├── cpp/
│   ├── value_objects/   cc_library iiifparser + tests + parse_benchmark — LIVE engine code
│   └── classifier/      cc_library iiif_handler (testonly) + tests — the deletable reference
└── rust/     the production Rust parser (rust_library iiif_parser, multi-module)
```

**The Rust parser is carved into a standalone `iiif_parser` crate that emits its
own domain types** (path 3). The crate defines domain enums (`RegionKind`,
`SizeKind`, `QualityKind`, `FormatKind`) and a domain `IiifParams` struct in
idiomatic Rust (`bool` for the upscaling/mirror flags, no `#[repr(C)]`). It has no
dependency on `//src/ffi:sipi_ffi` and does not import `crate::ffi`; its only
external crate is `percent-encoding`. **The domain → seam flattening
(`From<iiif_parser::IiifParams> for SipiIiifParams`, plus the enum conversions)
lives in `server-rs`**, applied at the single site where params cross the FFI
(`routes.rs` `serve_image`). `server-rs` owns the seam; the parser does not know
the seam exists.

**The Rust side is one crate, not a mirror of the C++ split.** The C++ folders
encode divergent lifetimes; the Rust crate is uniformly production, so there is no
delete-boundary to encode and no long-term 1:1 correspondence to maintain. The crate
is broken into internal modules (`domain`, `parse`, `request`) rather than a single
monolithic file — a code-organization choice, not a package boundary. The domain
types are **not** hoisted into a separate crate: no second consumer exists (YAGNI).

**The `#[repr(C)]` discriminant static-asserts in `ffi.rs` are retained** — they
still guard `SipiIiifParams` and the FFI enums against the C++ header. The domain →
FFI `From` impls use **exhaustive `match` arms** (never `as c_int` casts), so the
crate's discriminant numbering is irrelevant and adding or removing a variant is a
compile error rather than a silent mis-tag.

**The C++ side is split by fate, not by tidiness — the two libraries have
different lifetimes, so they get different packages and deletion is `rm -rf` one of
them:**

- `cpp/value_objects/` — `cc_library iiifparser` (the `SipiRegion`/`SipiSize`/
  `SipiRotation`/`SipiQualityFormat`/`SipiIdentifier`/`SipiDecodeDims` value objects)
  is **live production engine code**, not a reference. `//src:engine` depends on it;
  `src/SipiIO.h`, `src/SipiImage.cpp`, and the format handlers pass
  `std::shared_ptr<SipiRegion>`/`SipiSize` through the decode/crop path. It is **not**
  `testonly` and **not** deletable now. Only its string-parsing *constructors* are
  off the production path (the engine reconstructs geometry from the flattened seam
  struct, not from strings) — those constructors are the reference the Rust `parse_*`
  functions port.
- `cpp/classifier/` — `cc_library iiif_handler` (the `parse_iiif_uri` classifier) is
  a **pure `testonly` reference oracle** that the Rust `parse_request` is validated
  against. It already depends only on `//src/util` (never on the value objects), so
  the split is a natural seam, not a forced one. When the Rust parser is trusted as
  the sole guarantee, retiring the reference is `rm -rf src/iiifparser/cpp/classifier`
  plus dropping the `//test/approval` edge and the corpus consumer — a clean, bounded
  deletion the folder boundary makes obvious.

## Considered Options

- **Path 1 — the carved crate depends on `//src/ffi:sipi_ffi` for the seam types.**
  Rejected: it drags the C++ stdlib link (`_CPP_STDLIB_LINK`) and the transitive
  engine into a pure-CPU parser, defeating the point of the carve and keeping the
  parser un-sanitizable and slow to build.
- **Path 2 — split the ~5 seam types into a tiny types-only module both the parser
  and `server-rs` depend on.** Rejected as the endpoint (viable as an interim):
  low-risk, but the parser still emits FFI-flavored types, so the seam concern
  still leaks into the parser's public API.
- **Path 3 — the parser emits its own domain types; `server-rs` owns the mapping.**
  Chosen. The parser's public API speaks the IIIF domain; flattening to the FFI
  struct is a seam concern owned by the seam's owner. Consistent with the domain
  model (parsing produces value objects; the flattened `SipiIiifParams` is a
  transport detail) and with treating the seam as `server-rs`'s responsibility.
- **Language-first top-level split (`src/cpp/iiifparser`, `src/rust/iiifparser`).**
  Rejected: it scatters one logical component across two distant trees, the
  opposite of what a side-by-side strangler migration needs, and it breaks the
  repo's established component-first convention (`src/util`, `src/formats`).
- **A single mixed `BUILD.bazel` holding both languages' targets.** Rejected: one
  directory is one Bazel package, and a mixed package invites `glob()`
  cross-language capture and target-name collisions. Per-language subpackages give
  self-documenting labels (`//src/iiifparser/rust:iiif_parser`,
  `//src/iiifparser/cpp/value_objects:iiifparser`) with no suffix hacks.
- **A flat `cpp/` holding both C++ libraries, separated only by `testonly`.**
  Rejected in favor of the `value_objects/` + `classifier/` split: the deletion
  boundary should be structural (a subfolder you `rm -rf`), not a flag a reader has
  to notice. Divergent lifetimes get divergent packages.
- **Mirroring the C++ role-split inside the Rust crate.** Rejected: the C++ split
  encodes two lifetimes; the Rust crate has one (all production), so the same
  structure would be shape without a reason. The crate uses internal modules for
  readability, not packages for deletion.

## Consequences

- **Clean seam ownership.** The parser is pure Rust with one external crate. It no
  longer needs `_CPP_STDLIB_LINK`, and — having no C++ dependency — it is the first
  IIIF-parser target eligible for the sanitizer build (verified on CI; macOS cannot
  link ASan locally).
- **The `From` mapping is the new coupling guard for the params flattening**, backed
  by exhaustive matches at compile time and an explicit `server-rs` unit test over
  every enum variant plus the `bool → c_int` flags. The `ffi.rs` discriminant
  asserts continue to guard the wire contract against the C++ header.
- **The corpus becomes a shared, language-neutral asset.** The existing `filegroup`
  is consumed by both the C++ regression `cc_test` and a new Rust corpus regression
  `rust_test`. The Rust test locates the corpus through the `rules_rust` runfiles
  library (a `rust_test` does not run at workspace root, unlike the `cc_test`).
- **`cpp/classifier/` is the clean strangler boundary; `cpp/value_objects/` is
  not deletable yet.** The folder split makes the boundary structural: when the Rust
  parser is trusted as the sole guarantee, retiring the reference is
  `rm -rf src/iiifparser/cpp/classifier` + dropping the `//test/approval` edge and the
  corpus consumer. The value objects go only when the C++ image engine itself is
  ported — a later, separate step.
- **No new fuzzing.** `rules_fuzzing` has no Rust rule, the fuzzer was retired
  ([ADR-0020](0020-oracle-removal.md)), and the corpus is regression fixtures only.
  A cargo-fuzz harness against `iiif_parser::parse_request` (a `fuzz/` crate outside
  Bazel, seeded from the same corpus) remains the tracked follow-up from ADR-0020;
  this ADR does not add it.
- **This realizes the colocation [ADR-0003](0003-module-co-located-source-and-tests.md)
  calls for at the module level** and continues the strangler-fig lineage of
  [ADR-0013](0013-shttps-as-internal-module.md) / [ADR-0020](0020-oracle-removal.md):
  the Rust surface stands more on its own, the C++ reference is bounded and clearly
  labeled, and its removal is a smaller, well-marked change.
