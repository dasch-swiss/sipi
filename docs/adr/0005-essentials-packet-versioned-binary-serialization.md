---
status: proposed
---

# Essentials packet adopts a versioned, self-describing binary wire format (CBOR)

The Essentials packet's wire format migrates from pipe-delimited text to **CBOR (RFC 8949)** with a top-level `format_version` field. New SIPI versions read every prior `format_version` they support; legacy text-format packets are detected by the absence of a CBOR-tagged byte sequence and parsed via the legacy reader indefinitely. New packets are always written in the CBOR form.

We accept this because SIPI's preservation guarantee is that conversions don't lose metadata, and SIPI's installed base is *hundreds of thousands* of master files. Mass re-encoding to flip serialization formats is not operationally feasible at that scale; the wire format must therefore be forward-evolvable forever — new fields, new optimizations, new types — without requiring coordination across the existing master-file population. The current pipe-delimited format has zero versioning, no schema, no escaping, and no field discovery; ADR-0004 alone (adding eight image-shape fields) already pushes it past its design limits, and any subsequent schema change would compound the fragility. Biting this bullet now, while the install base is smaller than it will be at any future point, is strictly cheaper than deferring.

CBOR is chosen over the obvious alternatives:

- **Protocol Buffers** — solves forward-compat via field numbering, but adds a `protoc` codegen step and a `.proto` schema file to the build. The dep + workflow friction is not justified for a packet that's a few KB at most.
- **MessagePack** — functionally equivalent to CBOR with comparable forward-compat semantics, but no IETF standard. CBOR's preservation-community traction (used by COSE / CWT, JOSE successor stacks, IoT data formats, and increasingly by IIIF-adjacent specs) is the tiebreaker.
- **Custom versioned binary** — tightest bytes per packet, but reinventing schema-evolution semantics (length-prefixing, type tagging, default-value rules for missing fields) at a 100K-master-file horizon is the kind of decision a longevity-driven codebase should not bet on. CBOR's primitives are exactly the schema-evolution semantics we would otherwise reinvent.
- **JSON** — text-based; defeats the compactness goal in image headers and reintroduces the same escaping fragility that motivated this ADR.

Forward-compatibility in CBOR works at two levels and we use both:

- **Field-level**: additive schema changes (new fields) require no `format_version` bump. Readers ignore unknown CBOR map keys; writers add new keys at will. This covers the common case (e.g. ADR-0004's image-shape fields, future colour-space hints, future provenance fields).
- **Format-level**: breaking changes (field type changes, semantic redefinition) bump `format_version`. The new SIPI version is responsible for retaining a migrating reader for every prior `format_version` it claims to support. `format_version` is the *only* thing a reader checks before dispatching to a per-version parser; everything else is field-level evolution.

## Considered Options

- **Stay pipe-delimited; add ADR-0004 fields by appending more pipe segments** — rejected. Pushes past the format's design limits when we already know the next schema change is coming. The pipe-delimited reader has no escape mechanism (an `origname` containing `|` corrupts parse — latent bug today on macOS/Linux where filesystems allow `|`), no version discriminator, and no defined rule for unknown fields. Each future schema change pays the same cost; biting the bullet later is strictly more expensive than biting it now.

- **Adopt Protocol Buffers** — rejected. The codegen + schema-file workflow is appropriate for service interfaces but heavyweight for a single embedded metadata packet. Forward-compat via field numbering is real but the tooling overhead is not.

- **Adopt MessagePack** — rejected. Functionally equivalent to CBOR but lacks IETF standardization. No technical advantage; CBOR is the conservative choice for a 10+ year preservation horizon.

- **Custom versioned binary format** — rejected. Reinvents what CBOR's spec already nails down (canonical encoding, type tagging, length-prefixing, integer/float/string/array/map primitives). The maintenance cost of an in-house format compounds across the lifetime of the master-file population.

- **Adopt CBOR but without a `format_version` field**, relying entirely on field-level forward-compat — rejected. Field-level evolution covers most cases but cannot handle semantic redefinition (e.g. if `numpages` semantics ever change for some IIIF-spec reason). A coarse-grained version field is cheap insurance against the cases field-level evolution can't handle.

## Consequences

- **`SipiEssentials::parse(bytes)` becomes a dispatcher**: probe the leading bytes for a CBOR tag → CBOR parse via the chosen library; otherwise fall back to the legacy pipe-delimited parser. The legacy parser is retained indefinitely (longevity invariant). If both parsers fail, treat as no Essentials packet present (same as today's "is_set = false" path).

- **`SipiEssentials::serialize()` always emits CBOR** with `format_version = 1` for the initial cutover. The class's in-memory representation is unchanged; only the wire format moves. The existing `operator std::string()` and `operator<<` overloads — which the [Probe 2 deep-module analysis](../deep-modules.md#probe-2--metadata) flagged as shallowness leaks anyway — are removed in favour of an explicit `serialize() → std::vector<unsigned char>`. The on-disk artefact is *bytes*, not a `std::string`; the existing API's typing was a mistake.

- **New build dep**: a CBOR library. Candidates include `tinycbor` (lightweight, C, used by IoT/embedded), `jsoncons` (header-only C++, broad format support), or an in-tree minimal encoder (CBOR is small enough that a few hundred lines covers our needs). Choice deferred to implementation time; not an architectural decision.

- **`SipiEssentials::parse()` becomes fallible in a way the current API doesn't expose** (the dispatcher needs to report which parser was used, and CBOR-parse can fail in more ways than text-parse). Consider switching the API to `std::expected<SipiEssentials, ParseError>` to match `cpp-style-guide.md`'s preference. Aligns with the Rust target.

- **Approval-test goldens for image-header bytes change**: where the test asserts on the embedded packet bytes (any test that round-trips a master file through the encoder and inspects the header), the goldens are regenerated alongside ADR-0004's image-shape-field addition. Do both changes in one PR so the approval-suite churn is single.

- **Existing master files keep working unchanged**. Their pipe-delimited packets continue to parse via the legacy reader. The CBOR fast path activates incrementally as files are re-processed (CLI conversions, format-conversion writes), without any mass re-encoding event. This is the load-bearing operational property of this ADR.

- **The pipe-delimited fragility goes away for new packets**: filenames with `|`, no schema versioning, no field discovery, no escape semantics — all replaced by CBOR's well-defined encoding rules. Old packets remain fragile but, by definition, are read-only at this point (they're already on disk; nothing new will be written in the legacy format).

- **The `metadata/` Bazel package documented in [Probe 2](../deep-modules.md#probe-2--metadata) gains a CBOR-library dep but no visibility change.** Consumers (`SipiImage`, format handlers) see no API surface change at the call sites that already use `SipiEssentials` getters/setters. Only `parse` / `serialize` change shape.

- **Future schema additions become low-friction**. Adding a new field is: (a) extend the in-memory struct, (b) write the new key in `serialize()`, (c) read the new key (with a sensible default) in `parse()`. No `format_version` bump, no migration tooling, no coordinated deploys. This is the operational property the 100K-master-file horizon requires.
