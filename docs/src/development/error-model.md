# Error Model

The image/codec layer's failure contract: what type carries an error, what
policy each failure maps to at the three seams that consume it, and which
legacy exception mechanisms remain. The decision this document reflects is
[ADR-0024](../../adr/0024-value-based-image-errors.md); this page is the living
catalog kept current as the migration lands, not the decision record itself.

## The value type: `Sipi::SipiValueError`

`src/error/cpp/SipiValueError.h` (`//src/error`) defines:

- `ErrorCode` — an `enum class`, one variant per distinct failure category a
  caller can act on (decode rejection, unsupported format, malformed/oversized
  input, encode/write failure, client-abort, shape-probe failure,
  metadata-parse failure). New failure categories get a new variant — never
  folded into an existing one.
- `SipiValueError` — the value itself: an `ErrorCode`, a diagnostic message, and
  a `std::source_location` captured at construction. Cheap to move; the
  diagnostic string is not eagerly formatted.
- `client_message()` — path-redacted, no source location. Safe to surface in an
  HTTP response body or a Lua-visible error string.
- `diagnostic_message()` — full detail (source file + line), for server-side
  logs and Sentry.
- `Result<T>` — `template <typename T> using Result = std::expected<T,
  SipiValueError>;`, defined in the same header.

## Policy as data: `policy_for(ErrorCode)`

A `constexpr ErrorPolicy policy_for(ErrorCode)` returns
`{ HttpStatusClass, SentryPolicy, MetricHint }` for a given code. This is the
single source of truth for what today's exception-type dispatch encodes as
scattered `catch` clauses. Each seam maps the returned `HttpStatusClass` to its
own vocabulary — a concrete `SipiStatus` on the HTTP seam, an emitted error
string on the Lua seam, an exit code on the CLI seam.

| `ErrorCode` | `HttpStatusClass` | `SentryPolicy` | `MetricHint` |
|---|---|---|---|
| `kDecodeFailed` | `kInternalError` | `kReport` (+ `ImageContext`) | none |
| `kUnsupportedFormat` | `kInternalError` | `kReport` | none |
| `kMalformedInput` | `kInternalError` | `kReport` | none |
| `kWriteFailed` | `kInternalError` | `kReport` (+ `ImageContext`) | none |
| `kClientAbort` | `kInternalError` | `kSkip` | `kClientDisconnected` |
| `kShapeProbeFailed` | `kInternalError` | `kReport` | none |
| `kMetadataParseFailed` | `kInternalError` | `kReport` | none |

`std::bad_alloc` is deliberately not an `ErrorCode` and never appears in this
table — it stays exception-based permanently (ADR-0024 Decision 7). Its policy
is fixed at every seam: `kInternalError`, Sentry skipped, `MetricHint::
kMemoryAllocFailure`.

## Legacy mechanisms and their disposition

| Mechanism | Package | Status |
|---|---|---|
| `Sipi::SipiError` | `iiifparser` | Unchanged. Exception-based, out of scope for this migration — a different package with its own lifetime (ADR-0021). The seams keep catching it by type for HTTP-400 dispatch. |
| `Sipi::SipiSizeError` | `iiifparser` | Unchanged, same reasoning as `SipiError`. |
| `Sipi::SipiImageError` (+ `SipiImageClientAbortError`) | `image` | Replaced for every fallible-operation throw site by `SipiValueError`. The type itself is narrowed to genuinely unrecoverable, non-fallible conditions (or removed) once no fallible call site throws it. |
| `Sipi::InfoError` (bare `enum`) | `image` | Replaced by a dedicated `ErrorCode` variant (`kShapeProbeFailed`). |
| `std::bad_alloc` | n/a | Unchanged, permanent. See above. |

## The three seams

Each seam converts `Result`/`SipiValueError` (or, before a handler's migration
lands, the exception it still throws) into its own failure vocabulary:

- **`src/ffi/cpp/serve_image.cpp`** (HTTP) — `HttpStatusClass` → `SipiStatus`;
  `SentryPolicy::kReport` populates an `ImageContext` and calls
  `report_image_error`; `MetricHint` increments the matching
  `Sipi::observability::Metrics` counter.
- **`src/ffi/cpp/image_handle.cpp`** (Lua userdata surface, behind
  `src/scripting/rust/bindings/image.rs`) — `client_message()` is emitted as the
  Lua-visible error string; there is no HTTP status or Sentry report on this
  surface.
- **CLI offline verbs** (`src/cli/cpp/cli_app.cpp`,
  `src/cli/cpp/commands/verify.cpp`, `convert_access_file.cpp`) —
  `diagnostic_message()` goes to stderr; the exit code is derived from
  `HttpStatusClass` (client-class failures and internal failures map to
  distinct non-zero codes).

## Error-variant principle

Enumerate failure variants per operation; never collapse distinct failures into
one generic code to avoid extending the `policy_for` table. A catch-all that
silently genericizes a new failure is the failure mode this model exists to
prevent.
