---
status: proposed
---

# File-role creation is intentional and gated by CLI subcommand

A SIPI-produced file's role (Preservation File, Service File, or Access File per ADR-0009) is established at creation time by the operator's explicit CLI invocation, never inferred from format and never stamped ambiently by read operations. Source files supplied as input to any SIPI operation are never mutated. Format-handler writers emit role-establishing metadata (the Essentials packet for Service Files; the future preservation-metadata schema for Preservation Files) only when invoked through the master-creation orchestrator — the dispatcher for `sipi convert service-file` (and, in the future, `sipi convert preservation-file`).

The principle has four concrete realisations:

1. **`SipiImage::readSource` (formerly `readOriginal`) does only read.** No stamping, no hash computation, no metadata mutation. The function name `readOriginal` lied — the SIPI tool cannot know whether a file is "the original"; it's just a path the operator supplied. The rename to `readSource` makes the load-bearing principle visible at every call site.

2. **Format-handler writers gate Essentials emission on the master-creation orchestrator.** The pyramidal TIFF writer and JP2 writer accept a `master_mode` parameter (default: no Essentials). The orchestrator passes `master_mode = service-file` on the `convert service-file` path. The plain `convert` path passes the default; no Essentials is written, regardless of whether the output format happens to be JP2 or pyramidal TIFF.

3. **The CLI subcommand surface makes intent explicit.** `sipi convert <in> <out>` is generic (ImageMagick-style; produces an Access File). `sipi convert service-file <in> <out>` is DSP-specific (writes Essentials; enforces preservation-chain semantics). The two are distinct subcommands, not flag variants of one. The operator's intent is expressed by which verb they invoke, not by a flag combination.

4. **Hash-verify-on-read is a corruption tripwire, not a preservation guard.** When `readSource` happens to read a file that already has an Essentials packet (e.g., a Service File), the orchestrator's prior pixel-hash-verify branch (`if (checksum != emdata.fields().data_chksum) return false;`) is repurposed: log ERROR with file path and continue. Serving operates from Service Files; a hash mismatch on read is a signal to infra that wants logging, not a hard fail that aborts a request. The active integrity-check operation is `sipi verify service-file <file>` — the operator invokes that deliberately and expects a report.

We accept this because:

**1. The previous "ambient stamping" model produced silently-wrong output.** The legacy `SipiImage::readOriginal` stamped Essentials immediately after reading. CLI workflows that transformed the image (rotation, crop, resize, ICC convert) between read and write produced output files whose Essentials packet described the *input*, not the *output* the file actually contained. DEV-6379's `read_shape` fast path — which trusts the packet's `img_w`/`img_h` as the file's shape — would have returned wrong dimensions for any transformed output. The plan caught this gap; the principle elevated here prevents the class of bug from returning.

**2. Source files are never mutated.** Phrasing this as architectural principle prevents the recurring temptation to "fix up" source files: write back a corrected ICC profile, normalize EXIF orientation, recompute a missing checksum. Each individual fix-up has a reasonable rationale; in aggregate they erode the immutability operators rely on when comparing or re-ingesting source files. Files supplied to SIPI as input remain bit-identical on disk after any read operation.

**3. Role assignment owned by the SIPI codebase.** Format-and-content alone is insufficient to discriminate role — both a Service File and an Access File in JP2 format share the same magic bytes. The Essentials packet's presence (for Service Files) and the preservation-metadata schema's presence (for Preservation Files; future) are the discriminators, and they are emitted exclusively by the SIPI tools that produce those roles. A non-SIPI tool cannot accidentally produce a Service File by writing a JP2 — it would lack the packet. This makes role queries deterministic.

**4. The CLI verb is the architectural seam.** `sipi convert service-file` and `sipi convert` look similar from the operator's keyboard but are utterly different operations in the codebase: different orchestrators, different writer paths, different metadata propagation, different acceptance criteria. Naming them as distinct subcommands instead of one verb plus a flag (`--create-service-master`, the previously rejected design) means the codebase organisation matches the operational distinction — the master-creation logic lives in `cli/service_file_orchestrator.cpp`, not behind an `if (master_mode)` branch buried in the generic convert path.

## Considered Options

- **Ambient stamping** (the previous architecture: read stamps + verifies; any write emits) — rejected. Produces silently-wrong outputs for transformed conversions; violates source-file immutability; couples read paths to write-side concerns; makes role assignment depend on which writer happens to fire rather than what the operator intended.

- **Mode flag on a single `convert` verb** (`sipi convert --create-service-master in.X out.Y`) — rejected. The flag-vs-subcommand discussion in the plan resolved this: the architectural seam between generic-conversion and master-creation deserves a CLI verb, not a parameter on a shared verb. The bodies of the two operations share very little; surfacing that as the same verb misleads operators about the actual cost and effect of the flag.

- **Infer role from output format** (`out.jp2` → Service File; `out.jpg` → Access File) — rejected. Same-format ambiguity (JP2 outputs can be either) breaks the inference. Also conflates the operator's *intent* (which is what role assignment should track) with the file's *encoding* (which is downstream of intent).

- **Auto-detect input role and pass through** (if input has Essentials packet, output gets one too) — rejected. Conflates source-file content with output intent. An operator running `sipi convert in.jp2 out.jpg` for a quick preview would inadvertently propagate Essentials into the JPEG, violating the "Access Files don't carry Essentials" rule of ADR-0009.

- **Stamp at read time, refresh at write time** — rejected. The "refresh" step would re-do the work, but the architectural property (source is never mutated) requires not stamping at read time *at all*. Refreshing-after-stamping is also harder to reason about: a stale stamp survives between read and write call points.

## Consequences

- **`SipiImage::readOriginal` is renamed to `readSource`** (tracked in DEV-6539). Both overloads. Stamping logic stripped. `htype` parameter dropped (hash type is decided by the orchestrator at write time). The hash-verify branch becomes the corruption tripwire (log ERROR, continue).

- **Master-creation orchestrator lives in `src/cli/service_file_orchestrator.{h,cpp}`** (Phase 12 of DEV-6537's plan). Responsibilities: read source via `readSource`, apply transformations (orientation, ICC convert per archival policy if applicable), compute pixel hash on post-transformation buffer, assemble `EssentialsFields` from observed source + computed hash + current `SipiImage` state, hand to format-handler writer with `master_mode = service-file`.

- **Format-handler writers (pyramidal TIFF, JP2) gate Essentials emission on a `master_mode` parameter.** Default value `master_mode = none` produces output without an Essentials packet (Access File). `master_mode = service-file` produces output with an Essentials packet (Service File). JPEG/PNG/plain-TIFF writers never emit Essentials (per DEV-6379 Phase 5); they don't carry a `master_mode` param.

- **CLI subcommand surface enforces the seam** (tracked in DEV-6540). `sipi convert <in> <out>` invokes the plain conversion path. `sipi convert service-file <in> <out>` invokes the orchestrator. The two are top-level subcommands with their own positional args and option groups. `--skipmeta` is available on plain `convert` only; `--icc` is available on `convert` and `convert access-file` (server-equivalent transformations) but not on `convert service-file` (service files preserve source color space). The option-availability matrix prevents accidentally producing a transformed Service File.

- **Existing service masters in the field are unaffected.** They were created under the previous ambient-stamping regime; their packets are accurate at creation time (no transformations happened between the previous `readOriginal`-stamp and the writer-emit, because the read happened with the final transformed image in scope). Re-encoding them via `sipi convert service-file old.jp2 new.jp2` produces a fresh Service File with a fresh hash; the previous packet is dropped (per the orchestrator design — re-conversion is intentional creation, not propagation).

- **Operators verify file integrity explicitly via `sipi verify service-file <file>`** (also part of DEV-6540). The passive hash-mismatch log from `readSource` is the *signal* during routine reads; the `verify` verb is the *report* for deliberate checks. Both exist; they complement.

- **The IIIF server's access-file path benefits from the same principle.** The server reads a Service File, transforms it per IIIF parameters, and emits an Access File. No Essentials packet is written into the access-file response (per ADR-0009). The server's "intent" is conceptually `convert access-file` — the same intent the offline CLI subcommand expresses. Code-sharing between the server's access-file emission path and the CLI's `convert access-file` orchestrator is a Phase-12 deliverable.

- **The principle generalises to future file roles.** When `sipi convert preservation-file` lands (future, awaiting ADR-0012 on preservation file specification), the same orchestrator pattern applies: an explicit subcommand, an explicit orchestrator, a `master_mode` parameter on the relevant writer. No ambient stamping; no inference from format.
