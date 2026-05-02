---
status: proposed
---

# Image shape lookup is owned by format handlers, with master-format handlers reading from an Essentials-packet shape field

A SIPI module that needs the intrinsic shape of a source image — `(img_w, img_h, tile_w, tile_h, clevels, numpages, nc, bps)` — calls `SipiIO::read_shape(origpath) → ImageShape` on the appropriate format handler. The cache holds no shape data; `SipiCache::SizeRecord`, `SipiCache::sizetable`, and `SipiCache::getSize()` are removed. The two consumers today are canonical-URL computation (always — it needs `img_w`/`img_h` to resolve pixel-coord cache keys) and the decode-memory-budget peak estimator (cache-miss only — the full struct).

We accept this because shape is intrinsic to the image, not derivable from cache state, and the cache's shape memoization (`sizetable`) was parasitic — populated only as a side effect of `add()`, never persisted independently, surviving eviction (`purge()` and `remove()` don't clean it up), and absent for un-cached origpaths. The cost the parasitic cache was avoiding was a header read of a few KB, which on cache hit is dominated by the network send and on cache miss is dominated by the subsequent full decode. The right-shaped optimisation lives at the format-handler layer, not the cache layer.

The **master format** handlers — JP2 today, pyramidal TIFF after the planned migration — implement `read_shape` via a fast path: they read shape from a dedicated **image-shape field in the Essentials packet** if present, falling back to format-native header / codestream parsing if absent. CLI-mode conversion writes the field into the packet; server-mode reads benefit from the fast path. The Essentials-packet schema is format-agnostic — `(img_w, img_h, tile_w, tile_h, clevels, numpages, nc, bps)` is the same wire format for JP2 and pyramidal TIFF — but the embedding mechanism is handler-specific (JP2 box vs TIFF tag). Other format handlers (PNG, JPEG, non-pyramidal TIFF — used for IIIF output in server mode and for ingest in CLI mode) implement `read_shape` via standard header parsing only; they are not in the server hot path and do not warrant the Essentials-packet treatment.

This decision is also a re-homing step. The cache is amputated of a responsibility that was not its own; the format handlers gain a uniform shape-lookup interface that was previously absent (`SipiImage::getDim()` exists but returns a partial struct and is not surfaced through `SipiIO`).

## Considered Options

- **Keep the parasitic shape memoization in the cache** — rejected. The two structs (`SizeRecord` and `CacheRecord`) overlap by construction (same fields, different keys); `sizetable` is populated only by `add()`, never independently persisted, and is vestigial after eviction. Renaming alone (e.g. `getSize` → `lookupShapeByOrigpath`) does not fix the incompleteness — un-cached origpaths still miss, requiring the format-handler fallback path *anyway*. The cache as shape source has no architectural advantage that would not also exist for the format handler.

- **Memoize shape inside the peak-memory estimator** — rejected. The estimator is not the only consumer; canonical-URL computation also needs `img_w`/`img_h` on every request. Putting the memoization in the estimator would either duplicate the lookup at the canonical-URL site or force the canonical-URL site to depend on the estimator. Both are worse than the format-handler-as-source design.

- **JP2-specific Essentials-packet fast path only, no pyramidal TIFF** — rejected. Pyramidal TIFF is the planned successor to JP2 as the master format. Designing the Essentials-packet shape-field carrier as a JP2-specific feature would require duplicating it for TIFF immediately. Defining the field once at the Essentials-packet schema layer and letting both `SipiIOJ2k` and `SipiIOTiff` consume it costs nothing extra and keeps the migration story uniform.

- **Add the shape-read fast path to every format handler** — rejected. PNG, JPEG, and non-pyramidal TIFF handlers are used either for IIIF output (server mode) or for ingest (CLI mode). Neither path is server-hot. Format-native header parsing is sufficient and avoids carrying SIPI-specific metadata in formats SIPI does not control end-to-end.

## Consequences

- **`SipiCache` shrinks**. `SizeRecord`, `sizetable`, `getSize()`, and the bug-prone `purge()` / `remove()` non-cleanup of `sizetable` all go away. The cache becomes a pure representation cache. See [Probe 1](../deep-modules.md#probe-1--sipicache).

- **`SipiIO` gains `read_shape(origpath) → ImageShape`** as a virtual method on the base class. Each subclass (`SipiIOJ2k`, `SipiIOTiff`, `SipiIOJpeg`, `SipiIOPng`) provides an implementation. Master-format handlers (JP2 + pyramidal TIFF) get an Essentials-packet fast path with a header-parse fallback. Other handlers parse format-native headers directly.

- **The Essentials packet schema gains image-shape fields**: `img_w`, `img_h`, `tile_w`, `tile_h`, `clevels`, `numpages`, `nc`, `bps`. The packet's role broadens from "preserve cross-conversion identity" to "preserve cross-conversion identity *and* cache shape data SIPI computed at conversion time." The wire format must remain backward-readable; this is an additive schema change, not a breaking one. Older master files lacking these fields still work via the fallback path.

- **`SipiHttpServer.cpp` request flow simplifies**. The call site at line 1571 becomes `format_handler->read_shape(infile)`, returning a full `ImageShape` (no more "nc/bps remain 0" overestimation comment at line 1563). The decode-memory-budget admission check gets accurate inputs.

- **Migration path for pyramidal TIFF master files**. Existing TIFF master files lacking shape fields fall back to TIFF-tag parsing; new TIFF master files written by CLI mode include the shape fields in the Essentials packet using the same schema as JP2. The conversion tooling in CLI mode is the single point of change for both formats.

- **Backward compatibility for JP2 master files**. Existing JP2s in production lacking shape fields fall back to JP2 codestream parsing. New JP2 conversions populate the shape fields. No mass re-conversion required; the fast path activates incrementally as files are re-processed.

- **`SipiLua.cpp` is not directly affected**. The Lua admin surface does not touch `getSize` today; it touches `getCacheDir`, `loop`, `remove`, `purge`, `add`. Those are addressed by the upcoming `SipiLua` probe separately.

- **Approval-test surface is unchanged**. Shape fields are read-only metadata; the rendered image bytes are unaffected. Existing approval goldens stay valid.

- **Glossary deltas land in [`UBIQUITOUS_LANGUAGE.md`](../../UBIQUITOUS_LANGUAGE.md)** in the batched edit pass: `Image shape`, `Operating mode` / `Server mode` / `CLI mode`, `Master file`, `Master format`, `Pyramidal TIFF`, and a sharpened `Essentials packet` definition. Tracked in the [glossary delta register](../deep-modules.md#glossary-delta-register).
