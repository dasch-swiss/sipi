---
status: accepted
---

# Value-based image errors: `SipiValueError` replaces exception-type dispatch in the image/codec layer

The image and codec layer (`src/image/`, `src/image_processing/`,
`src/format_handlers/`, `src/metadata/`) predates cpp-style-guide §3.14 and still
signals failure by throwing across roughly 80 call sites. The failure contract is
invisible at every signature; three independent seams —
`src/ffi/cpp/serve_image.cpp` (HTTP), `src/ffi/cpp/image_handle.cpp` (the Lua
userdata surface), and the CLI offline verbs — each re-derive the same policy
question ("what HTTP status / Sentry report / metric / exit code does this
failure map to?") by dispatching on the caught exception's *type*. Four
overlapping mechanisms carry that type today:

| Mechanism | Where | What it signals |
|---|---|---|
| `Sipi::SipiError` (`src/error/cpp/SipiError.h`, `: public shttps::Error`) | thrown by the `iiifparser` value objects (`SipiRegion`, `SipiRotation`, `SipiQualityFormat`, `SipiIdentifier`, canonical-URL construction) | malformed IIIF request parameters |
| `Sipi::SipiSizeError` (`src/iiifparser/cpp/value_objects/SipiSize.h`) | thrown by `SipiSize::get_size` | malformed/disallowed IIIF size parameter, carries an HTTP code |
| `Sipi::SipiImageError` (+ `Sipi::SipiImageClientAbortError`) (`src/image/cpp/SipiImageError.h`) | thrown across the codecs, `SipiImage`, and metadata parsers | decode/encode/processing failure; the client-abort subclass specifically marks a dead peer socket |
| `Sipi::InfoError` (`enum InfoError { INFO_ERROR }`, `src/image/cpp/SipiImage.h:56`) | thrown bare (no message, no location) from `SipiImage::read_shape` failure paths | shape-probe failure, caught only in the Lua surface |

This ADR decides the replacement for the **image/codec-owned** exceptions
(`SipiImageError`, `SipiImageClientAbortError`, `InfoError`) with a single
value-returning error type, and records the mechanics of migrating the four
format handlers and the `SipiIO` vtable to it. `SipiError`/`SipiSizeError` belong
to `iiifparser`, a different package with its own lifetime (ADR-0021); they are
**out of scope** and stay exception-based — the seams keep catching them by type
exactly as today. `std::bad_alloc` also stays exception-based, permanently (see
Decision 7).

## Decision

### 1. Canonical error value type: `Sipi::SipiValueError`

The value type is named `SipiValueError`, defined in `src/error/cpp/`
(`//src/error`) — the package `SipiError.h` already occupies and the one every
consumer of the image/codec layer already depends on without a cycle.

**The name cannot be `SipiError`.** `Sipi::SipiError` already exists, is actively
thrown by `iiifparser`, and is actively caught for HTTP-400 dispatch at all three
seams. Reusing that identifier for the new value type would mean folding the
`iiifparser` exception hierarchy into this migration, which is a different
package with a different lifetime (ADR-0021) and not this ADR's scope.
`SipiValueError` is deliberately close to `SipiImageError` in spelling — it *is*
that mechanism's value-returning replacement — while `Value` unambiguously marks
it as a returned value, never thrown for control flow.

**What it unifies:** every throw site in `src/format_handlers/`, `src/image/`,
`src/image_processing/`, and `src/metadata/` that reports a fallible-operation
failure (malformed input, codec rejection, shape-probe failure, client-abort
during streamed write).

**What it replaces:**
- `Sipi::SipiImageError` for all *fallible-operation* throws. `SipiImageError`
  itself is not deleted in this decision — later work narrows it to genuinely
  unrecoverable, non-fallible conditions (a logic-bug assertion, not a decode
  failure) or removes it outright once no fallible call site throws it. This
  ADR does not commit to which; it commits to the end state that no fallible
  operation throws it.
- `Sipi::SipiImageClientAbortError` — becomes an `ErrorCode` variant carrying the
  client-abort policy (Decision 2), not a distinct type.
- `Sipi::InfoError` — becomes an `ErrorCode` variant (shape-probe failure). The
  bare enum-as-exception pattern (no message, no location) disappears entirely.

**What it leaves alone:** `Sipi::SipiError`/`Sipi::SipiSizeError` (iiifparser,
exception-based, unchanged) and `std::bad_alloc` (Decision 7).

### 2. Policy-mapping discriminant: `ErrorCode` + a `policy_for` table

The seams do not dispatch on `SipiValueError`'s C++ type — there's only one type.
They dispatch on a data field. `SipiValueError` carries:

- an `ErrorCode` (`enum class`, one variant per distinct failure category:
  decode rejection, unsupported format, malformed/oversized input, encode/write
  failure, client-abort, shape-probe failure, metadata-parse failure — the
  granularity a caller can act on, not one bucket per throw site)
- a diagnostic `std::string` (mirrors `SipiImageError::to_string()` — includes
  `std::source_location`)
- a `std::source_location` (captured at construction, formatted lazily into the
  diagnostic string on demand, not eagerly — construction stays cheap)

A `constexpr ErrorPolicy policy_for(ErrorCode)` free function returns a small
aggregate: `{ HttpStatusClass, SentryPolicy, MetricHint }`. This reproduces every
distinction today's exception-type dispatch makes, as data instead of a chain of
`catch` clauses:

| Today (exception type) | `ErrorCode` | `HttpStatusClass` | `SentryPolicy` | `MetricHint` |
|---|---|---|---|---|
| `SipiImageError` (decode/write) | `kDecodeFailed` / `kWriteFailed` / … | `kInternalError` | `kReport` (+ `ImageContext`) | none |
| `SipiImageClientAbortError` | `kClientAbort` | `kInternalError`* | `kSkip` | `kClientDisconnected` |
| `InfoError` | `kShapeProbeFailed` | `kInternalError` | `kReport` | none |
| `std::bad_alloc` (unchanged, not an `ErrorCode`) | — | `kInternalError` | `kSkip` | `kMemoryAllocFailure` |

*`kClientAbort`'s `HttpStatusClass` is moot in practice — the peer is already
gone by the time this fires — but the field is populated for completeness rather
than special-cased away.

Two message accessors preserve the split `SipiImageError` already has:
`client_message()` (path-redacted, no source location — safe for an HTTP body or
a Lua-visible string) and `diagnostic_message()` (full detail, for server logs
and Sentry). `SipiValueError` stays cheap to move (code + `std::string` +
`std::source_location`, no eager formatting), so returning it through
`std::expected` on every fallible call has no allocation cost beyond the
diagnostic string itself.

Each of the three seams maps `HttpStatusClass` to its own vocabulary:
`serve_image.cpp` to a concrete `SipiStatus`, `image_handle.cpp` to an emitted
error string (there is no HTTP status on the Lua surface), and the CLI verbs to
an exit code. The policy table is the single source of truth; the three
mappings are three small, boring `switch`es over the same three-value enum
instead of three copies of exception-catching logic.

### 3. `Result<T>` alias

```cpp
// src/error/cpp/SipiValueError.h
template <typename T>
using Result = std::expected<T, SipiValueError>;
```

Defined in the same header as `SipiValueError` itself, not a separate
`Result.h`. A type alias with no state does not earn its own translation unit,
and `//src/error` gains no new sub-target for it — one header, one `cc_library`
entry, consistent with the package's existing shape.

### 4. `SipiIO` vtable strategy: internals first, one flip

`SipiIO::read`/`read_shape`/`write` are pure virtual with a fixed signature. A
single override cannot change its return type without changing the interface —
that would make `SipiIO` briefly inconsistent (some overrides returning `bool`,
others `Result`), which is not just ugly, it does not compile against one
abstract base.

The migration is therefore staged in two kinds of change:

1. **Per-handler, internals-only.** Each format handler's *internal* decode/
   encode logic is rewritten to build and propagate `Result<T>` values. The
   override itself is unchanged in signature; its body re-wraps the internal
   `Result` back to the existing `bool`-returning / throwing contract at the
   boundary. This is a **temporary throwing adapter** — see Decision 8.
2. **One vtable-flip step, after the last handler's internals are pure.** The
   base class signatures change to return `Result`; all four overrides drop
   their wrap layer in the same change; `SipiImage`'s `read`/`read_shape`/
   `write` dispatcher starts consuming `Result` directly. This is the only
   point where the interface itself moves, and it moves once, atomically.

This keeps every handler's migration independently reviewable and bisectable
while guaranteeing the vtable is never in a mixed state.

### 5. Handler migration order: J2K → PNG → JPEG → TIFF

Ordered by ascending difficulty of the codec's existing error-signalling
mechanism, not by the original work-breakdown numbering (this order deliberately
reverses it):

- **J2K first.** Kakadu already throws real C++ exceptions (`kdu_exception`, nine
  catch sites in `SipiIOJ2k.cpp`) — there is no setjmp boundary to redesign.
  Converting a `catch (kdu_exception&) { throw SipiImageError(...); }` into
  `catch (kdu_exception&) { return std::unexpected(...); }` is close to
  mechanical.
- **PNG and JPEG next.** Both bridge a C library's error callback across a
  `setjmp`/`longjmp` boundary (three sites each) and receive the same
  treatment — see Decision 6.
- **TIFF last, and hardest.** `tiffError`/`tiffWarning`
  (`SipiIOTiff.cpp:428-444`) are commented-out no-ops today — libtiff errors are
  silently swallowed. The commented-out body forwards a `va_list` into
  `log_err`'s `...` slot: a va_list-vs-varargs ABI mismatch that misreads the
  caller's variadic state for any format string carrying conversions, not a
  UTF-8-specific defect, and it never compiled in the first place (dead code
  referencing an undeclared identifier). Neither handler has a `setjmp`
  landing site — libtiff reports failure through ordinary return codes the
  call sites already check, so both are a pure logging side-channel, which is
  why TIFF does not appear in Decision 6. TIFF's migration must re-enable the
  callbacks through the safe primitive — `log_vsformat` formats the `va_list`,
  and the result is passed as a `%s` argument, never as the format string
  itself, matching `SipiIOPng.cpp`'s `sipi_error_fn` — before or as part of
  converting them to populate a `Result`'s error; re-enabling blind, without
  that formatting discipline, would reintroduce the mismatch.

### 6. Per-codec longjmp bridge mechanism

**J2K:** no bridge needed — `kdu_exception` is caught directly at the Kakadu
edge and converted to `std::unexpected(...)`.

**PNG and JPEG: keep the setjmp landing-site pattern — neither library offers a
sanctioned throw-from-callback alternative this migration can rely on.** Both
bridge a C library's error callback across a `setjmp`/`longjmp` boundary, and
both get the identical treatment: the landing block's final control transfer
changes from `throw SipiImageError(...)` to `return std::unexpected(...)`; the
callback itself keeps calling `longjmp`.

- **PNG.** The error callback (`sipi_error_fn`,
  `src/format_handlers/cpp/SipiIOPng.cpp`) still calls `longjmp`. The landing
  block (`if (setjmp(png_jmpbuf(png_ptr))) { ... }`) changes only its final
  control transfer to `return std::unexpected(...)`. Manual cleanup in the
  landing block (`png_destroy_read_struct`/`png_destroy_write_struct`) stays
  exactly where it is.
- **JPEG.** libjpeg's error manager is a raw C struct with C function-pointer
  callbacks; there is no throw-from-callback option either. The landing block
  (`if (setjmp(jerr.error_jmp)) { ... }`) changes the same way. Manual cleanup
  in the landing block (freeing `icc_buffer_guard`, `jpeg_destroy_decompress`)
  stays exactly where it is.

Both swaps are exactly as safe as the throws they replace: the safety property
(destructors skip past a `longjmp`) is a property of `setjmp`/`longjmp` itself,
not of what the landing block does with control afterward. Two disciplines
therefore apply to both codecs:

- **RAII objects declared before the `setjmp`.** Anything that owns a resource
  needing cleanup on the error path must be constructed before the `setjmp`
  call and cleaned up manually in the landing block (as above) — its
  destructor will not run if a `longjmp` skips past it.
- **Volatile locals.** Any local variable modified between the `setjmp` call and
  a possible `longjmp`, and read after landing, must be `volatile` (or hoisted
  out of the risk window). This is unchanged by the throw→return swap and must
  not regress during the rewrite.

JPEG carries one additional rule, specific to its error-manager struct:

- **`jerr.error_message` stays `char[]`.** It must survive the jump; do not
  "modernize" it to `std::string` — a `std::string`'s heap buffer is not
  guaranteed to survive an unwind that skips its destructor path in the way a
  fixed-size array trivially does.

### 7. `std::bad_alloc` and logic-bug invariants stay exception-based, permanently

`std::bad_alloc` is deliberately **not** an `ErrorCode` variant and never
becomes one. Resource exhaustion is not malformed input — it's a systemic
condition that can occur at any allocation point in the call graph, not just at
the fallible operations this migration gives a `Result` return. Threading OOM
through every intermediate `Result` would multiply boilerplate at every
allocation site for no client-facing benefit: a request that hits OOM cannot
proceed regardless of which mechanism reports it. The seam `try/catch` for
`std::bad_alloc` (and for genuine logic-bug assertions, which throw and should
keep throwing per §3.14) therefore never fully disappears — this migration
narrows what a seam's `try/catch` catches, it does not eliminate the `catch`.

### 8. The `Result`→throw adapter is intermediate-commit-only; it never ships

The wrap-at-the-override-boundary adapter from Decision 4 step 1 exists only
between the commit that migrates a handler's internals and the commit that
performs the vtable flip. Because this whole migration lands as one PR, the
adapter is visible in the branch's commit history but is retired within the
same PR, before merge — it is never present in the tree that lands on `main`. This is worth stating explicitly because the repo's
no-backwards-compatibility-shims rule reads, out of context, like it prohibits
exactly this kind of adapter; it does not, because the adapter never reaches
shipped code. A reader auditing the diff after merge will not find it.

### 9. Style rules for `Result`-returning code

- **Early-return inside C-interop internals.** Handler-internal code that talks
  directly to a C library (`libtiff`, `libpng`, `libjpeg-turbo`, Kakadu) uses
  `if (!r) return std::unexpected(r.error());`-shaped early returns. This
  region's control flow already has to track a C API's error protocol
  one call at a time; monadic chaining would not simplify it.
- **Monadic composition at clean seams.** `and_then`/`transform` composition is
  used where a sequence of `Result`-returning calls is already free of C
  interop — `Image` construction, `image_processing`'s free functions. This is
  where C++23's `std::expected` chaining earns its keep.
- **`[[nodiscard]]` on every `Result`-returning function**, no exceptions. This
  is enforced in code review and by clang-tidy's `bugprone-unused-return-value`
  check, which covers `std::expected` in its default watched-type list and — 
  unlike `[[nodiscard]]` alone — also flags a `(void)`-cast used to silence the
  warning.

## Considered Options

- **A single global `catch (...)` at each seam, generic across all failure
  kinds.** Rejected outright — this is the anti-pattern the error-variant
  principle below exists to name, and it is strictly worse than what exists
  today (today's exception-type dispatch already distinguishes four policies;
  collapsing to one generic catch would be a regression, not a migration).
- **Per-function bespoke `E` error types** (a different error type per
  operation, LLVM/Abseil's rejected alternative). Rejected: one canonical type
  with a discriminant enum composes uniformly across every seam and every
  handler; per-function types would force each of the three seams to
  special-case every call site's distinct type, which is the exact problem
  this ADR is solving.
- **PNG adopts libpng's throw-from-error-callback (`png_set_longjmp_fn` / a
  throwing `png_set_error_fn`) instead of the landing-site pattern.** Rejected,
  on three grounds: commit `94f45a28` (2026-04-04), *"fix: eliminate C++
  exception-through-C UB in image format handlers,"* deliberately replaced a
  `throw` with `longjmp(png_jmpbuf(...))` in `sipi_error_fn` for precisely this
  reason — re-adopting the throw hook would undo a shipped fix; libpng is a BCR
  `bazel_dep` (`MODULE.bazel`, 1.6.54), so its compile flags are not ours and
  there is no guarantee its C frames carry unwind tables, and vendoring libpng
  as a native `cc_library` solely to guarantee exception propagation is not
  worth it; and JPEG carries the identical hazard (see the comment at
  `src/format_handlers/cpp/SipiIOJpeg.cpp:86`) and keeps the landing-site
  pattern, so treating the two codecs uniformly is more legible than giving
  them opposite treatments for the same hazard (Decision 6).
- **Migrate `SipiError`/`SipiSizeError` (iiifparser) into `SipiValueError` at
  the same time.** Rejected for this ADR: different package, different owning
  initiative, no throw-site inventory done for it here. Nothing in this
  decision blocks doing so later under its own ADR.

## Consequences

- **Error-variant principle.** Every operation's failure modes are enumerated as
  distinct `ErrorCode` values; a new failure category gets a new variant, never
  folded into an existing one to avoid touching the `policy_for` table. A
  generic catch-all silently genericizes future failures and is exactly what
  this decision replaces.
- **Hot-path caveat.** The happy-path cost of `std::expected` versus exceptions
  is genuinely contested in the wider C++ community: exceptions are "zero-cost"
  until thrown, while `std::expected` adds a tag check on every return that may
  or may not inline away — and is least likely to inline across a virtual call.
  The handler-internals changes (Decision 5's per-codec work) are
  error-path-dominated and not at risk; external benchmarking of comparable
  migrations shows large error-path wins there. The vtable flip (Decision 4
  step 2) is the one step that touches the happy path across a virtual
  boundary and is the one most at risk of a codegen regression. It receives
  its own dedicated before/after benchmark run (`decode_benchmark`/
  `encode_benchmark`), evaluated separately from the per-handler benchmark
  runs, per the hot-path rule in CLAUDE.md.
- **Test churn.** 14+ files under `test/unit/` currently assert on throw
  behavior (`EXPECT_THROW` and similar). Each handler's migration updates its
  share to assert on the `Result` contract instead, in the same commit that
  changes the behavior under test.
- **~80 throw sites move.** Every throw site inside `src/format_handlers/`,
  `src/image/`, `src/image_processing/`, and `src/metadata/` that signals a
  fallible-operation failure is a candidate for conversion; sites signalling a
  genuine logic-bug invariant are not, and stay exceptions per Decision 7.
- Tracked under DEV-7056.

## Related documents

- [`docs/src/development/error-model.md`](../src/development/error-model.md) —
  the living catalog: the full `ErrorCode` list, the `policy_for` table, and
  the per-seam mapping, kept current as the migration lands (this ADR records
  the decision; the error-model doc records the current state).
- [ADR-0007](0007-sipiimage-decomposition.md) — the `image`/`image_processing`
  package split this migration lands inside.
- [ADR-0021](0021-iiifparser-polyglot-colocation.md) — why `SipiError`/
  `SipiSizeError` are a separate package and out of this ADR's scope.
