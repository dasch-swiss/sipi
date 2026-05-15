---
status: proposed-future
---

# Preservation File format and metadata schema (deferred)

A future ADR will define the **Preservation File** format and metadata schema — the long-term bit-level preservation tier sitting above the Service File baseline established by [ADR-0009](./0009-file-role-taxonomy.md) (file-role taxonomy), [ADR-0010](./0010-file-role-creation-is-intentional.md) (intentional creation), and [ADR-0011](./0011-preservation-metadata-via-xmp.md) (preservation metadata via XMP). Scope candidates include: archival container format selection (TIFF Baseline, JP2 Lossless, or PREMIS-AIP wrappers), rights / provenance / event metadata shape (PREMIS-XMP, C2PA-via-XMP, IIIF provenance event), checksumming + fixity policy, and the CLI surface for creating and verifying Preservation Files (`sipi convert preservation-file` / `sipi verify preservation-file` — both currently stubbed out at the [DEV-6537](https://linear.app/dasch/issue/DEV-6537) merge with an `awaits ADR-0012` error message).

This stub exists as a stable docs anchor so the [DEV-6537](https://linear.app/dasch/issue/DEV-6537) joint-implementation work can point at a real ADR number when discussing the architectural boundary between "Service File baseline (in scope today)" and "Preservation File tier (deferred)." No Linear ticket is associated yet; the parent will be created when the work is scheduled.
