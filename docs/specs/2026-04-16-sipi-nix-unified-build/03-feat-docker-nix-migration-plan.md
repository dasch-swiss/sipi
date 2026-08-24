---
title: "Migrate Sipi Docker image build to Nix dockerTools"
type: feat
date: 2026-04-22
author: "Ivan Subotic"
status: superseded
superseded_by: "specs/2026-04-29-sipi-bazel-migration/01-refactor-sipi-bazel-migration-plan.md"
linear: DEV-5939
repositories:
  - sipi
implemented_in: "https://github.com/dasch-swiss/sipi/pull/582"
implemented_at: 2026-04-28
---

> **Superseded.** This plan landed in production
> ([sipi#582](https://github.com/dasch-swiss/sipi/pull/582)) with
> Sipi's Docker image built by Nix `dockerTools`. That implementation
> was then replaced by Bazel `rules_oci` under
> [the Sipi Bazel migration](../2026-04-29-sipi-bazel-migration/01-refactor-sipi-bazel-migration-plan.md)
> (DEV-6341), specifically PR Y+4 (DEV-6346). The image's runtime
> contract (distroless base, OCI labels, multi-arch manifest) is
> preserved verbatim; only the producer changed.

# Migrate Sipi Docker image build to Nix dockerTools

## Overview

Phases 1 and 1.5 of the Nix unified build (see `01-feat-nix-unified-build-plan.md` and `02-feat-justfile-ci-unification-plan.md`, both implemented) put every Sipi build artifact behind a Nix derivation — except the one artifact that actually ships to production: the Docker image. The image is still built by the imperative `Dockerfile` + `docker buildx build`, duplicating the compile graph that Nix already owns and leaving Docker as the single "second source of truth" in the repo.

This plan closes the loop. After it lands:

- The **`Dockerfile` is deleted.** The production image comes from `packages.docker-stream` (already wired in `flake.nix:360-395`), streamed to the Docker daemon via `nix build .#docker-stream --print-out-paths | docker load`.
- **`just nix-docker-build`** replaces `just docker-build` as the single recipe. The imperative `just docker-test-build-{amd64,arm64}` recipes and their debug-symbol extraction sub-steps are deleted.
- **CI** (`docker-build.yml`, `publish.yml`) invokes the new Nix-backed recipes. The per-arch matrix, multi-arch manifest, Docker Scout scans, SBOM, Sentry debug-symbol upload, and smoke tests remain — they are orthogonal to how the image is produced.
- **The container continues to run as `root`** — matching current behavior. DEV-5920 (non-root user) is **explicitly out of scope** here; see "Not in scope" below and the note under the Technical Approach.
- **DEV-6282 (re-enable hardening on the final-link binary)** becomes the natural next step — DEV-5939 is its explicit precondition per `DEV-6282`'s body.

The `packages.docker` / `packages.docker-stream` outputs already exist and build successfully (added in Phase 1, PR #558). They are currently unused in CI. This plan promotes them to production.

### Terminology

This plan operates one level below SIPI's domain language: it concerns the **Docker container image** that ships the SIPI binary — *not* the IIIF **Image** of `sipi/UBIQUITOUS_LANGUAGE.md`. To keep the two clearly separated:

- **"Image" / "the image"** in this plan = the OCI Docker container image produced by `dockerTools.buildLayeredImage` and pushed to `daschswiss/sipi`.
- **IIIF Image, Bitstream, Image root, Identifier, Prefix, Cache key, Permission, Preflight script, Decode memory budget, …** keep their meanings as defined in [`sipi/UBIQUITOUS_LANGUAGE.md`](../../../sipi/UBIQUITOUS_LANGUAGE.md). When this plan references domain concerns (NFS mount holding artefacts under the **Image root**, the `/health` endpoint surfaced by shttps, the Bitstream `/file` route consumed by smoke tests) it uses those canonical terms.
- The `/health` endpoint that the new HEALTHCHECK consumes is part of the **shttps** bounded context, not SIPI proper — see [`sipi/CONTEXT-MAP.md`](../../../sipi/CONTEXT-MAP.md) and [`sipi/shttps/CONTEXT.md`](../../../sipi/shttps/CONTEXT.md). The migration here keeps that contract unchanged so the planned shttps→Rust strangler-fig migration is unaffected.

## Problem Statement

### 1. Dockerfile duplicates the Nix compile graph

`sipi/Dockerfile` (143 lines) does work the flake already owns:

- **Stage 1 (builder):** `apt install clang libc++-dev cmake …` + `cmake -S . -B ./build -DCMAKE_BUILD_TYPE=RelWithDebInfo` + `cmake --build ./build` + `ctest` — a verbatim reimplementation of what `nix build .#default` does (now with `doCheck = enableTests` running tests in the sandbox).
- **Stage 2 (debug-symbols):** `objcopy --only-keep-debug ./build/sipi ./build/sipi.debug && strip ./build/sipi` — a manual debug-info split, while `package.nix:181` already has `separateDebugInfo = true` producing the `$debug` output.
- **Stage 3 (final):** `apt install tzdata ca-certificates curl openssl locales ffmpeg` + `COPY --from=builder /tmp/sipi/build/sipi /sipi/sipi` + a one-off download of `pid1-rs` from a GitHub release — again, all expressible via nixpkgs + `dockerTools`.

Every `ext/` dependency (exiv2, libtiff, libwebp, kakadu, …) is compiled twice in CI on a typical PR — once inside `test.yml` via `just nix-build` and once again inside `docker-build.yml` via `docker buildx build`. This is ~6–8 minutes of duplicated work per PR, on top of two separate and independent cache systems (Cachix for Nix, GHA `type=gha,mode=max` for Docker layers).

### 2. Imperative justfile recipes bypass the reproducibility invariant

`CLAUDE.md:11` states:

> every `nix-*` recipe wraps `nix build .#<variant>`. CI invokes only `just <recipe>` — no inline cmake or `nix build` calls. Incremental inner-loop development is a documented dev-shell pattern (see below), not a recipe.

The justfile's Docker section (lines 26–146) violates this invariant by shelling out to `docker buildx build` with hand-written arg lists. Those recipes predate the Nix unified-build work and are the last imperative build path in the repo.

Audit (2026-04-22):

| Recipe | `justfile` lines | Current implementation | Docker-via-Nix equivalent |
|---|---|---|---|
| `docker-build` | 31–46 | `docker buildx build … -t {{docker_image}} -t daschswiss/sipi:latest --load .` | `nix build .#docker-stream --print-out-paths \| xargs -I {} bash -c "{} \| docker load"` + `docker tag …` |
| `docker-test-build-amd64` | 89–122 | `docker buildx build --platform linux/amd64` + debug-symbol stage extraction | `packages.docker-stream` on ubuntu-24.04 runner + `result/…/debug/lib/debug/.build-id/*/*.debug` |
| `docker-test-build-arm64` | 49–82 | `docker buildx build --platform linux/arm64` + debug-symbol stage | `packages.docker-stream` on ubuntu-24.04-arm runner |
| `docker-push-amd64` | 125–126 | `docker push {{docker_image}}-amd64` | unchanged — still `docker push` |
| `docker-push-arm64` | 85–86 | `docker push {{docker_image}}-arm64` | unchanged |
| `docker-publish-manifest` | 129–134 | `docker manifest create/annotate/push` | unchanged |
| `test-smoke` / `test-smoke-ci` | 141–146 | `cargo test --features docker --test docker_smoke` | unchanged — consumes the local image regardless of build path |

Six of the seven recipes are imperative Docker invocations; three of those will be rewritten around `nix build`. The three push/manifest recipes stay as-is (they are thin `docker` CLI wrappers, which is fine — they don't *build* anything).

### 3. The flake's Docker output is incomplete for production

`flake.nix:322-395` defines `packages.docker` and `packages.docker-stream`, but they are **missing** several production-grade attributes compared to the current `Dockerfile`:

| Concern | Dockerfile | `packages.docker` (current) | Gap |
|---|---|---|---|
| Runtime user | `root` (implicit) | `root` (implicit) | **Keep `root`** in this plan. Sipi reads artefacts under the **Image root** (Image and Bitstream files per `sipi/UBIQUITOUS_LANGUAGE.md`) from an NFS mount whose files are written by another service with its own uid; switching sipi to non-root would require uid/gid coordination across services that isn't in scope here. Tracked separately under DEV-5920. |
| Signal handling (pid1) | `pid1-rs` downloaded in Dockerfile | none | Needs `tini` (nixpkgs standard) or equivalent |
| HEALTHCHECK | `curl -sf http://localhost:1024/health` via `HEALTHCHECK` | none | Must preserve (K8s / Traefik consume this) |
| Timezone | `TZ=Europe/Zurich` + `tzdata` | not set | Needs `config.Env` + `pkgs.tzdata` in contents |
| Locales | `en_US.UTF-8` + `sr_RS.UTF-8` generated via `locale-gen` | `LC_ALL=en_US.UTF-8` set but locales not generated | Needs `pkgs.glibcLocales` with the two locales enabled |
| `created` timestamp | Inherited from Docker (=wall clock) | unset (`1970-01-01T00:00:01Z` default) | Scout / Docker Hub warn on epoch; use `self.lastModifiedDate` |
| Tag strategy | `{{build_tag}}` + `latest` | `self.rev or "dev"` | Needs amd64/arm64 suffix + `latest` + version tags for `publish.yml` |
| OCI labels | none | none | Add `org.opencontainers.image.*` for registry UX + Scout |
| VERSION baked in at build | `ARG VERSION=…` | unset — `version.txt` only | Pass via `providedVersion` parameter on `package.nix` |

These gaps are fixable entirely inside `flake.nix` / `package.nix`. The current outputs are a good *foundation* but not yet a *production* build.

### 4. Debug symbol flow is Dockerfile-specific

`publish.yml` expects `sipi-amd64.debug` / `sipi-arm64.debug` files sitting in the repo root, produced by the Dockerfile's `debug-symbols` stage (`Dockerfile:82-84`) via `docker buildx build --target debug-symbols --output type=local,dest=./debug-out`. Those files are then uploaded to Sentry via `sentry-cli debug-files upload`.

After migration, Nix's `separateDebugInfo = true` (`package.nix:181`) produces debug symbols under `$out.debug/lib/debug/.build-id/<xx>/<yy>.debug` (GNU build-id layout, directly compatible with `sentry-cli`). The file name and path will change. `publish.yml` needs to rediscover the right path — ideally via a justfile recipe that locates and renames the artifact.

### 5. Docker Scout / SBOM have known friction on Nix-layered images

Docker Scout's component inventory relies on package-manager metadata (dpkg, rpm, apk). Nix images have no package manager inside `/nix/store`, so Scout's CVE detection degrades to binary-fingerprint matching — meaning **many false negatives** for CVEs in dynamically-linked libraries. Mitigation:

- Emit a CycloneDX SBOM via `nix-sbom` or `sbomnix` as a separate job output, attached to the OCI image as a referrer via `docker buildx imagetools`. Scout, Grype, and Trivy can all consume CycloneDX referrer SBOMs.
- Keep Scout scans in CI as they are today (catches kernel/libc-level CVEs) but treat the Nix-SBOM as the authoritative component inventory.

Not doing this leaves a detection gap relative to today's Dockerfile-on-Ubuntu (where Scout can inventory every `apt` package).

## Proposed Solution

Five moves, each self-contained and incrementally verifiable:

1. **Flesh out `packages.docker` / `packages.docker-stream` in `flake.nix`** to production parity. Add `tini` as entrypoint (replaces `pid1-rs`), HEALTHCHECK, TZ, locales, `created` timestamp from `self.lastModifiedDate`, OCI labels, and a versioned tag scheme. Plumb `providedVersion` through `pkgs.sipi.override` so `config/sipi.config.lua` has the right version string. **Runtime user stays `root`** (no `config.User` set) — match current production behavior; non-root migration (DEV-5920) is separate.
2. **Invoke arch-explicit flake attributes for per-arch CI recipes.** The underlying derivation is `pkgs.hostPlatform`-driven, so no cross-compilation is needed (matching the learning in `multi-arch-static-build-ci-docker-native-per-arch.md`). But the per-arch justfile recipes MUST call `.#packages.x86_64-linux.docker-stream` / `.#packages.aarch64-linux.docker-stream` rather than the bare `.#docker-stream` — that way a wrong-arch runner fails loudly instead of silently producing a mismatched image. The bare `.#docker-stream` stays for local-dev convenience via `nix-docker-build`.
3. **Add a dedicated `packages.sipi-debug` output** that exposes `(pkgs.sipi.debug)` so CI can `nix build .#sipi-debug` and extract the Sentry-format `.debug` file deterministically, without needing to crack open the Docker layer.
4. **Rewrite the justfile Docker section around Nix recipes.** Delete `docker-build`, `docker-test-build-{amd64,arm64}`, and their retry logic. Add `nix-docker-build` (local dev), `nix-docker-build-{amd64,arm64}` (CI — both build the docker-stream and the matching `sipi-debug` symlink in a single `nix build` call with `-o result -o result-debug`), and `nix-docker-extract-debug $arch` (pure `find + cp` — no nix invocation). Keep `docker-push-*` + `docker-publish-manifest` unchanged (they're thin `docker` CLI wrappers, not build commands).
5. **Migrate `docker-build.yml` and `publish.yml`** to invoke `just nix-docker-*` recipes. Remove `docker/setup-buildx-action` (no longer building via buildx), remove the GHA-cache variable exposure (no longer using Docker layer cache — Cachix covers this), add `DeterminateSystems/determinate-nix-action` + `cachix/cachix-action` to each build job (matching the pattern already in `test.yml` + `publish-static-release`). `docker-publish-manifest` and `docker-push-*` stay unchanged.

After this plan: the `Dockerfile` file no longer exists in the repo. Every build step — unit-test, sanitized, fuzz, static-linux, release-archive, **and Docker image** — goes through a Nix derivation. `Cachix` becomes the single caching system for the entire build graph; `type=gha,mode=max` Docker layer caching is retired.

### Not in scope

- **DEV-5920 (non-root user in container).** Explicitly deferred. Sipi reads artefacts under the **Image root** (per `sipi/UBIQUITOUS_LANGUAGE.md` — i.e., **Image** and **Bitstream** files resolved from `{prefix}/{identifier}` requests, not Docker container layers) from an NFS mount where another service (ingest / dsp-api upstream) writes with its own uid/gid; switching the sipi container to a non-root user means the uid inside the container must align with the NFS file ownership (or the NFS export must be reconfigured with `no_root_squash` semantics or `nfs4 idmap`). None of that coordination is in scope for a build-backend migration. The Nix migration preserves today's `root` runtime behavior 1:1. A future DEV-5920 PR — after NFS ownership is coordinated with operations — can flip to a non-root user in a small, reviewable change (add `fakeRootCommands` + `config.User = "1000:1000"` to the already-Nix-backed image).
- **DEV-6282 (re-enable final-link hardening).** This plan unblocks it but doesn't implement it. `hardeningDisable = [ "all" ]` stays in `package.nix` for now; a follow-up PR per DEV-6282's "target fix shape" adds a two-stage derivation that re-links the final binary with PIE/RELRO/FORTIFY. Splitting keeps this plan's diff auditable (Dockerfile deletion + Nix wiring changes) and delays a cross-cutting security change until the migration itself is green in production.
- **DEV-5937 (run e2e tests against the Docker release image).** Today's e2e tests run against the *Nix debug* build (`test.yml`). After this plan, a future effort can add a job that runs them against the *Nix Docker* image — but that's a different quality gate, not a blocker for the migration.
- **DEV-5938 (improve Docker build times by caching ext/ compilation).** Solved as a side effect: Cachix caches every derivation in the graph, so `ext/` is substituted, not recompiled. No dedicated work needed.
- **Moving to `nix2container` (nlewo) instead of `dockerTools`.** Smaller images, but a new external dependency. Revisit if layered-image size becomes a problem; not now.
- **Replacing Docker Scout with Grype / Trivy.** Scout is entrenched in the team's workflow (SARIF → GitHub Security); switching scanners is orthogonal to the build-backend migration.
- **Changing the smoke-test harness.** `test-smoke-ci` runs Rust testcontainers against the locally-loaded image; that continues to work unchanged since the image name (`daschswiss/sipi:…`) is preserved.

## Technical Approach

### Architecture

Three boundaries change:

1. **`flake.nix` Docker outputs grow** new attributes (Entrypoint [= tini], Healthcheck, Labels, Env [TZ + locales], `created` from `self.lastModifiedDate`) and a version-aware tag. `config.User` stays unset (root), matching today's Dockerfile. Introduce `packages.sipi-debug` as a passthrough to `(pkgs.sipi.debug)`.
2. **`package.nix` gets a `providedVersion` passthrough** all the way to CMake's `-DEXT_PROVIDED_VERSION=${version}` (already present, but the flake needs to expose an override hook so `publish.yml` can pass the git tag in).
3. **`justfile` Docker section shrinks** from 146 lines to ~50. Imperative `docker buildx build` invocations disappear. `docker-push-*` and `docker-publish-manifest` stay — they consume whatever image is in the local Docker daemon, regardless of how it got there.
4. **`.github/workflows/docker-build.yml` and `publish.yml`** swap `docker buildx` setup for `DeterminateSystems/determinate-nix-action` + `cachix/cachix-action`, and call `just nix-docker-build-{amd64,arm64}` + `just nix-docker-extract-debug ${arch}`.

### Implementation Workstream

All changes land in a **single PR** on one feature branch against `sipi` `main`. Per Phase 1.5's commit-organization pattern, the branch accumulates intermediate commits during implementation and is rewritten into a clean history before merge (see the Commit Organization section below).

The tasks below are grouped by concern. During implementation they can be interleaved.

#### A. Flake + package changes

**Files:** `flake.nix`, `package.nix`.

- **Add a versioned tag scheme.** Replace the current `tag = self.rev or "dev"` with a resolver that prefers, in order: (a) `providedVersion` passed via `pkgs.sipi.override`, (b) the short SHA `builtins.substring 0 7 (self.rev or "dirty")`, (c) `"dev"` as a last resort. Emit two tags per image: the primary (version or short SHA) and `latest`. `dockerTools.buildLayeredImage` accepts only one `tag`, so use post-build `docker tag` (invoked by the justfile) to add aliases.
- **Wire `providedVersion` through the flake.** `package.nix:28` already has the parameter. Add an override hook in `flake.nix` so `packages.docker` (and friends) call `pkgs.sipi.override { providedVersion = <version>; }`. The primary consumer is `publish.yml` passing `GITHUB_REF_NAME` (e.g., `v3.17.2`) so `sipi --version` reports the tag, not just `version.txt`'s content.
- **Runtime user stays `root`** (matches current Dockerfile behavior; DEV-5920 explicitly out of scope — see "Not in scope" above). Do **not** set `config.User`, `fakeRootCommands` user-creation, `dockerTools.shadowSetup`, or `enableFakechroot` for user management. The existing `fakeRootCommands` at `flake.nix:346-357` already creates `/sipi/{images/knora,cache,config,scripts,server}` directories and copies the config/scripts under them — keep that as-is (root-owned is fine because sipi runs as root). Add a single-line comment in `flake.nix` next to the unset `User` field documenting the NFS uid/gid coordination constraint so future readers know why this is deliberate:
  ```nix
  # config.User is intentionally unset — sipi runs as root.
  # Sipi reads artefacts under the SIPI "Image root" (see UBIQUITOUS_LANGUAGE.md)
  # from an NFS mount whose ownership is controlled by another service.
  # Switching to a non-root uid requires uid/gid coordination with NFS exports;
  # tracked in DEV-5920, not done here.
  ```
- **Replace pid1-rs with tini.** Drop the `curl … pid1-rs` step (was `Dockerfile:129-135`). Add `pkgs.tini` to image `contents` and set `config.Entrypoint = [ "${pkgs.tini}/bin/tini" "--" ];`. Tini reaps zombies and forwards signals — exactly what pid1-rs was doing, with ~200 KB overhead.
- **Add HEALTHCHECK.** `dockerTools` accepts a Docker-v1.2-manifest `Healthcheck` block; values in nanoseconds:
  ```nix
  config.Healthcheck = {
    Test = [ "CMD" "curl" "-sf" "http://localhost:1024/health" ];
    Interval  = 30 * 1000 * 1000 * 1000;
    Timeout   = 5  * 1000 * 1000 * 1000;
    StartPeriod = 10 * 1000 * 1000 * 1000;
    Retries = 3;
  };
  ```
  Matches `Dockerfile:137-138` verbatim. `curl` is already in `contents`.

  **Cross-context note.** The `/health` endpoint is served by **shttps**, not by SIPI's own routing (see `sipi/CONTEXT-MAP.md` — shttps is the embedded HTTP bounded context that SIPI consumes one-way). The HEALTHCHECK contract is therefore part of the SIPI ↔ shttps seam: when the planned strangler-fig migration replaces shttps with the Rust HTTP layer, the new layer must continue to expose `GET /health` on the same port (1024) returning HTTP 200 within the existing latency budget. No change to that contract here; this plan only forwards the existing endpoint into the OCI Healthcheck spec.
- **Add timezone + locales.** Add `pkgs.tzdata` to `contents` and set `TZ=Europe/Zurich`; add `pkgs.glibcLocales` with the two required locales:
  ```nix
  contents = contents ++ [ pkgs.tzdata (pkgs.glibcLocales.override {
    allLocales = false;
    locales = [ "en_US.UTF-8/UTF-8" "sr_RS.UTF-8/UTF-8" ];
  }) ];
  config.Env = [
    "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
    "TZ=Europe/Zurich"
    "LOCALE_ARCHIVE=/nix/store/…/lib/locale/locale-archive"
    "LC_ALL=en_US.UTF-8"
    "LANG=en_US.UTF-8"
    "LANGUAGE=en_US.UTF-8"
  ];
  ```
  The `LOCALE_ARCHIVE` path must be the nix-store path of the generated archive, which Nix interpolates automatically via `${pkgs.glibcLocales}/lib/locale/locale-archive`.
- **Set `created` to `self.lastModifiedDate`** so Docker Hub and Scout don't surface "55 years old" warnings. Still deterministic for a given flake.lock. `self.lastModifiedDate` is an unseparated 14-digit `YYYYMMDDHHMMSS` string; the format below is **ISO 8601 basic** (not strict RFC 3339, which requires the dashes and colons). Docker accepts the basic form for the OCI `created` field; if a strict RFC 3339 parser ever rejects it, splice in the separators:
  ```nix
  # ISO 8601 basic — accepted by Docker / OCI tooling
  created = builtins.substring 0 8 self.lastModifiedDate
           + "T" + builtins.substring 8 6 self.lastModifiedDate
           + "Z";
  # produces e.g. 20260422T093015Z

  # If a strict RFC 3339 parser is needed, build the expanded form instead:
  # created = let d = self.lastModifiedDate; in
  #   builtins.substring 0 4 d + "-" + builtins.substring 4 2 d + "-" + builtins.substring 6 2 d
  #   + "T" + builtins.substring 8 2 d + ":" + builtins.substring 10 2 d + ":" + builtins.substring 12 2 d + "Z";
  # produces e.g. 2026-04-22T09:30:15Z
  ```
  (The nixpkgs `dockerTools` manual's `buildLayeredImage` examples include both forms — copy whichever is needed.)
- **Add OCI labels** for registry UX and Scout enrichment:
  ```nix
  config.Labels = {
    "org.opencontainers.image.source"   = "https://github.com/dasch-swiss/sipi";
    "org.opencontainers.image.revision" = self.rev or "dirty";
    "org.opencontainers.image.version"  = version;
    "org.opencontainers.image.licenses" = "AGPL-3.0-only";
    "org.opencontainers.image.title"    = "Sipi";
    "org.opencontainers.image.description" = "IIIF-compatible media server.";
  };
  ```
- **Expose `packages.sipi-debug` as a passthrough.** `packages.sipi-debug = self.packages.${system}.default.debug;` (or similar — the existing `.#default` already has `separateDebugInfo = true` so `$debug` output exists). Consumers run `nix build .#sipi-debug` to get a symlink to `$debug/lib/debug/.build-id/…/…debug`. The justfile recipe below finds the concrete file path and renames it for Sentry upload.
- Verify locally (native-linux-builder, no CI round-trips):
  - `nix build .#packages.aarch64-linux.docker-stream --print-out-paths | sh | docker load` on darwin via native-linux-builder → arm64 image lands in daemon. Repeat with `.#packages.x86_64-linux.docker-stream` to exercise the amd64 path from the same dev machine.
  - `docker run --rm daschswiss/sipi:<tag> --help` → sipi CLI banner prints, no "permission denied" or missing-library errors.
  - `docker run --rm --entrypoint id daschswiss/sipi:<tag>` → `uid=0(root) gid=0(root)` (unchanged from today).
  - `docker inspect daschswiss/sipi:<tag>` → shows `Healthcheck`, `Labels.org.opencontainers.image.*`, `User` field absent or `""` (= root), `WorkingDir: /sipi`, `Entrypoint: [/nix/store/…/bin/tini, --]`, `Env` includes TZ, LC_ALL, LOCALE_ARCHIVE.
  - `docker history daschswiss/sipi:<tag>` → confirms `maxLayers = 125` and that the sipi binary is its own layer (not co-located with ffmpeg/curl closures, for layer-sharing benefits on subsequent pushes).
  - Run `test-smoke-ci` locally against the loaded image → smoke-test suite green.
  - `nix build .#sipi-debug` → produces `result/lib/debug/.build-id/…/….debug`; `file result/lib/debug/.build-id/*/*.debug` reports "ELF 64-bit … separate debug info".

#### B. Justfile rewrite

**Files:** `justfile`.

- **Delete** the imperative-build portion of the justfile Docker section (lines 26–134 — `docker-build`, `docker-test-build-amd64`, `docker-test-build-arm64`, `docker-push-*`); `docker-publish-manifest` is rewritten around variables whose values are set by the new Nix recipes. `test-smoke` / `test-smoke-ci` (lines 141–146) are part of the broader Docker section but are *kept* — they consume the locally-loaded image regardless of how it was built.
- **Write the new Docker recipes:**
  ```just
  # Build the Docker image via Nix dockerTools and load into the local daemon.
  # Uses the host-platform's default flake attribute (host-arch image).
  # Canonical local-dev path — supersedes the deleted `docker-build`.
  # No `-debug` symlink here — the local-dev recipe doesn't need Sentry artifacts.
  nix-docker-build:
      $({{_nix_build}} .#docker-stream --print-out-paths) | docker load
      # Re-tag with :latest and the build_tag for consumers (smoke tests, publish).
      docker tag $({{_nix_build_query_tag}} .#docker-stream) {{docker_image}}
      docker tag $({{_nix_build_query_tag}} .#docker-stream) {{docker_repo}}:latest

  # Build and load the amd64 Docker image AND the matching sipi .debug symlink.
  # Pins the x86_64-linux flake attribute so this recipe fails fast on an
  # arm64 host instead of silently producing an arm64 image — the CI matrix
  # and publish.yml rely on the image arch matching the `-amd64` tag suffix.
  # Both outputs are realized in a single `nix build` call; `result-debug`
  # is an essentially-free side effect (the `debug` output of the sipi
  # derivation is already realized in the store as part of the image build).
  nix-docker-build-amd64:
      {{_nix_build}} \
          .#packages.x86_64-linux.docker-stream \
          .#packages.x86_64-linux.sipi-debug \
          -o result -o result-debug
      ./result | docker load
      docker tag $({{_nix_build_query_tag}} .#packages.x86_64-linux.docker-stream) {{docker_image}}-amd64

  # Build and load the arm64 Docker image AND the matching sipi .debug symlink.
  # Pins the aarch64-linux flake attribute — same reasoning as nix-docker-build-amd64.
  nix-docker-build-arm64:
      {{_nix_build}} \
          .#packages.aarch64-linux.docker-stream \
          .#packages.aarch64-linux.sipi-debug \
          -o result -o result-debug
      ./result | docker load
      docker tag $({{_nix_build_query_tag}} .#packages.aarch64-linux.docker-stream) {{docker_image}}-arm64

  # Rename the already-built .debug symlink target to sipi-<arch>.debug so
  # publish.yml's Sentry upload step finds it at the expected filename.
  # This recipe does NO `nix build` — the symlink was created by
  # `nix-docker-build-<arch>`. $arch is `amd64` or `arm64` and is purely
  # a filename suffix here (the arch-specific derivation was already
  # selected at build time, preventing a cross-arch mix-up).
  nix-docker-extract-debug arch:
      #!/usr/bin/env bash
      set -euo pipefail
      if [ ! -L result-debug ] && [ ! -d result-debug ]; then
          echo "ERROR: result-debug/ not found. Run 'just nix-docker-build-{{arch}}' first." >&2
          exit 1
      fi
      # The .debug file lives at result-debug/lib/debug/.build-id/<xx>/<yy>.debug
      debug_file=$(find result-debug/lib/debug/.build-id -name '*.debug' | head -1)
      if [ -z "$debug_file" ]; then
          echo "ERROR: no .debug file found in result-debug/" >&2
          exit 1
      fi
      cp "$debug_file" "sipi-{{arch}}.debug"
      @echo "Debug symbols copied to: sipi-{{arch}}.debug"
  ```
  `_nix_build_query_tag` is a new helper: `_nix_build_query_tag := 'nix eval --raw'` — invoked like `$({{_nix_build_query_tag}} .#packages.x86_64-linux.docker-stream.imageName).$({{_nix_build_query_tag}} .#packages.x86_64-linux.docker-stream.imageTag)`. Keeping the attribute path explicit at the call site (rather than hard-coding it in the helper) means the per-arch recipes and the host-arch `nix-docker-build` all share one helper without divergence.

  **Arch-attribute invariant.** The bare `.#docker-stream` form resolves to the host platform's package set (`pkgs.<hostSystem>.docker-stream`) and exists for local dev convenience. The `.#packages.<arch>-linux.docker-stream` form is explicit and fails fast if the host can't build it. CI matrix recipes (`nix-docker-build-{amd64,arm64}`) MUST use the explicit form; on Linux runners of the matching arch the result is identical, but the explicit attribute turns a silent misuse (wrong-arch runner invoking the recipe) into an obvious error. This aligns with the `CLAUDE.md` "Build completeness invariant" that already uses `.#packages.x86_64-linux.<variant>` / `.#packages.aarch64-linux.<variant>` for macOS-side verification of Linux targets.

  **Single-nix-invocation invariant.** `nix-docker-build-<arch>` does ONE `nix build` call that realizes both the docker-stream tarball and the sipi debug artifact. `nix-docker-extract-debug` does zero `nix` calls — it's pure `find + cp` against `./result-debug`. The `-o result -o result-debug` flags pair positionally with the two installables passed to `nix build`. This keeps the CI step boundary aligned with what the underlying derivation graph actually does: the sipi derivation has two outputs; realizing it once populates both.
- **`docker-push-amd64`, `docker-push-arm64`, `docker-publish-manifest` stay unchanged** — their inputs (locally tagged images) still exist after migration; only the production path changed.
- **`test-smoke` / `test-smoke-ci` stay unchanged** — they consume `daschswiss/sipi:latest` from the local daemon, which `nix-docker-build*` guarantees via the `docker tag` step.
- Remove unused justfile vars: `_gha_cache_from`, `_gha_cache_to` (now-dead — Nix doesn't use GHA Docker-layer cache). Keep `nproc` (used by other recipes).

#### C. CI workflow migration

**Files:** `.github/workflows/docker-build.yml`, `.github/workflows/publish.yml`.

- **`docker-build.yml` — both `build_amd64` and `build_arm64` jobs:**
  - Remove `docker/setup-buildx-action@v4` and the GHA-cache env-var exposure (`ACTIONS_CACHE_URL`, `ACTIONS_RUNTIME_TOKEN`).
  - Add `DeterminateSystems/determinate-nix-action@v3` with `extra-conf: extra-experimental-features = configurable-impure-env`.
  - Add `cachix/cachix-action@v15` with `name: dasch-swiss` and `authToken: ${{ secrets.CACHIX_AUTH_TOKEN }}`.
  - Keep `docker/login-action@v4` (still needed — `test-smoke-ci` doesn't push, but Docker Scout's `docker/scout-action@v1` uses the Docker daemon's auth).
  - Replace `- run: just docker-test-build-${arch}` with:
    ```yaml
    - env:
        GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}
      run: just nix-docker-build-${{ matrix.arch }}
    - run: just test-smoke-ci
    ```
  - Docker Scout steps stay unchanged — they operate on the locally-loaded image by name (`local://daschswiss/sipi:latest`).
  - Concurrency groups, LFS pull, and other scaffolding stay unchanged.
- **`publish.yml` — `validate-docker` job:**
  - Same substitutions as `docker-build.yml`: remove buildx, remove GHA cache vars, add determinate-nix-action + cachix-action, call `just nix-docker-build-${arch}` + `just test-smoke-ci`.
- **`publish.yml` — `publish-docker` job:**
  - Same substitutions for build — note that `just nix-docker-build-${arch}` now realizes both the image AND the debug symlink in a single `nix build` call (per the "Single-nix-invocation invariant" in the Justfile rewrite section above); the `result-debug` symlink is a byproduct of the same build step, not a second nix invocation.
  - Keep `just ${{ matrix.push_target }}` (the push recipes stay — they push the already-loaded image).
  - Add `- run: just nix-docker-extract-debug ${{ matrix.arch }}` as a trivial `find + cp` step **after** the build step. The recipe does no nix work; it renames the existing `result-debug` target to the filename `matrix.debug_file` expects (`sipi-amd64.debug` / `sipi-arm64.debug`), so the subsequent Sentry upload step is unchanged.
  - The SBOM step (`docker/scout-action@v1` with `command: sbom`) stays — it operates on the pushed image by registry URL, not by image-build path. Consider adding a parallel Nix-SBOM step in a follow-up (see "Future Considerations").
- **`publish.yml` — `manifest` job:** unchanged. `just docker-publish-manifest` still produces a multi-arch manifest from the two pushed per-arch images.
- **`publish.yml` — `publish-static-release` job:** already fully Nix-backed — no changes.
- **`publish.yml` — `sentry` + `docs` jobs:** unchanged.

#### D. Documentation and config updates

**Files:**

- `CLAUDE.md`
- `docs/src/development/building.md`
- `docs/src/development/ci.md`
- `docs/src/development/nix.md`
- `README.md` (if it mentions Docker)
- `CONVENTIONS.md` (if it mentions the Dockerfile)

Tasks:

- Update `CLAUDE.md` "Quick Reference" to replace `just docker-build` / `just docker-test-build-*` with the new `nix-docker-*` recipes. Reinforce the invariant: "every `docker-*` and `nix-docker-*` recipe that *builds* an image goes through Nix; only `docker-push-*` and `docker-publish-manifest` shell out to `docker` directly."
- Update `docs/src/development/building.md` Docker section: remove "requires Docker Desktop + buildx"; add "requires Docker daemon + `nix build`" (buildx is no longer needed, though Docker CLI still is for push/manifest).
- Update `docs/src/development/ci.md` to reflect that Cachix — not GHA Docker layer cache — is the cache for image-build latency.
- Update `docs/src/development/nix.md` to add a "Docker image" section describing `packages.docker` / `packages.docker-stream` and their runtime shape (root user with NFS-coordination deferral note linking DEV-5920, tini entrypoint, HEALTHCHECK, locales).
- **Delete `Dockerfile` and `.dockerignore`** (the .dockerignore only matters when `docker build` reads the repo context — Nix doesn't).
- Update `CONVENTIONS.md` (if it mentions "Alpine (musl)" as noted in Phase 1.5's plan §"Not in scope" — that drift is finally resolved here, since the Nix image uses nixpkgs' default Linux — glibc, not musl). Keep docs truthful.

#### E. Verification (local-first via native-linux-builder, CI as final gate)

Per the Phase 1.5 pattern, verify exhaustively locally before the first CI push.

**Local verification (primary):**

- `nix build .#packages.aarch64-linux.docker-stream --print-out-paths | sh | docker load` and `nix build .#packages.x86_64-linux.docker-stream --print-out-paths | sh | docker load` — exercise both arch attributes from macOS via native-linux-builder (and/or on a Linux host if available). Repeat via `just nix-docker-build-{arm64,amd64}` to verify the recipe wrappers too.
- `docker run --rm daschswiss/sipi:<tag> --help` — no errors, sipi banner.
- `docker run --rm --entrypoint id daschswiss/sipi:<tag>` — confirm `uid=0(root) gid=0(root)` (unchanged from today; non-root user is DEV-5920, deferred — see "Not in scope").
- `docker run -d --name sipi-check -p 1024:1024 daschswiss/sipi:<tag>` + wait for `HEALTHCHECK` → `docker inspect sipi-check | jq '.[].State.Health.Status'` returns `"healthy"` within 40 s.
- `docker inspect daschswiss/sipi:<tag>` — spot-check: `Config.Labels["org.opencontainers.image.version"]`, `Config.User` absent or `""` (= root), `Config.Entrypoint[0]` ends with `/bin/tini`, `Config.Healthcheck.Test[2] == "http://localhost:1024/health"`.
- `docker history daschswiss/sipi:<tag>` — confirm layered structure (one layer per major nix-store path); image compressed size ≤ 600 MB.
- `just test-smoke-ci` after `just nix-docker-build` — Rust testcontainer smoke tests pass against the locally-built image.
- After `just nix-docker-build-amd64` has run, verify the byproduct debug symlink: `ls -la result-debug/lib/debug/.build-id/` should list a single build-id pair. Then `just nix-docker-extract-debug amd64` (pure `find + cp`, no nix call) + `file sipi-amd64.debug` reports ELF debug info; `readelf -n sipi-amd64.debug | grep 'Build ID'` matches the build ID of the sipi binary inside the loaded image (`docker run --rm --entrypoint readelf daschswiss/sipi:<tag>-amd64 -n /sipi/sipi | grep 'Build ID'` — path may differ if the entrypoint pinning prevents direct invocation; fall back to `docker cp` + host readelf).
- Grep audits:
  - `rg 'docker buildx build' justfile .github/workflows/` → should be empty.
  - `rg 'ACTIONS_CACHE_URL|ACTIONS_RUNTIME_TOKEN' justfile .github/workflows/docker-build.yml .github/workflows/publish.yml` → should be empty.
  - `ls sipi/Dockerfile sipi/.dockerignore` → both should be absent.
  - `rg 'pid1-rs' sipi/` → empty (was only in Dockerfile + CHANGELOG references; leave historical CHANGELOG entries alone).

**CI verification (final gate):**

- Push the feature branch. Expected workflows that trigger: `docker-build` (amd64 + arm64 + docs), `test`, `sanitizer`. Expected all green.
- Confirm Docker Scout CVE comparison produces a PR comment (interacts with `local://daschswiss/sipi:latest`, should still find that image tag since `nix-docker-build-${arch}` applies it).
- Confirm Scout SARIF lands in GitHub Security tab.
- Manually dispatch `publish.yml` on a dry-run tag (e.g., `v0.0.0-docker-migration-test`) in a fork or in a local simulated flow (if the test account permits). Verify:
  - Docker images push to Docker Hub under two arch-suffixed tags + a multi-arch manifest.
  - `sipi-{amd64,arm64}.debug` files are attached to the GitHub Release.
  - Sentry debug-files upload succeeds.
  - SBOM artifact uploaded.
- Compare wall-clock: `docker-build / amd64` pre-migration median ≈ 11 min; target post-migration ≈ 2–4 min on warm Cachix (no `ext/` recompilation).
- Update DEV-5939 Linear description to mark it resolved; open DEV-6282 as the next item in the queue (re-enable final-link hardening, now unblocked).

### Commit Organization (pre-merge cleanup)

Follow `docs/src/development/commit-conventions.md` and the Phase 1.5 precedent. Expected final commit sequence (one commit per row; consolidate if trivial):

| Type | Subject | Contents |
|---|---|---|
| `build` | `flake: production-grade dockerTools.buildLayeredImage (tini, healthcheck, locales, labels)` | All `flake.nix` and `package.nix` edits: tini entrypoint, Healthcheck, tzdata + glibcLocales, OCI labels, `created` from lastModifiedDate, versioned tag, `packages.sipi-debug` passthrough, `providedVersion` override plumbing. `config.User` stays unset (root), with a comment documenting the NFS uid/gid constraint. |
| `build` | `justfile: replace Dockerfile recipes with nix-docker-* wrappers` | Delete `docker-build`, `docker-test-build-*`; add `nix-docker-build`, `nix-docker-build-{amd64,arm64}`, `nix-docker-extract-debug`; remove `_gha_cache_*` vars. |
| `ci` | `workflows: build Docker images via Nix on native per-arch runners` | `docker-build.yml` and `publish.yml` migrations. Remove buildx/GHA-cache setup; add determinate-nix + cachix actions; switch recipe names. |
| `build` | `remove Dockerfile and .dockerignore` | Deletions only. Separate commit so the removal is obvious in history and easy to revert if a regression surfaces. |
| `docs` | `document Nix-backed Docker build; retire Dockerfile references` | `CLAUDE.md`, `docs/src/development/{building,ci,nix}.md`, `README.md`, `CONVENTIONS.md` updates. |

`feat:` is not used — the shipped binary's behavior is unchanged (same IIIF endpoints, same config file, same health check, same signal handling semantics, same root runtime user). `build` and `ci` are hidden per `commit-conventions.md`; they do not drive a release-please version bump. The PR description uses the standard template (Motivation / Summary / Key Changes / Challenges and Decisions / Gotchas / Test Plan), opening line `Fixes DEV-5939. Unblocks DEV-6282 and DEV-5920.`

**Cleanup procedure:** same as Phase 1.5 §Commit Organization:

1. Before opening the PR, `git log --oneline` and sketch which in-flight commits belong to which bucket above.
2. `git rebase -i <base>` to reorder/squash/reword.
3. Push the cleaned branch; verify every workflow green.
4. Open the PR, or `git push --force-with-lease` if already open.

## Alternative Approaches Considered

### A. Keep the Dockerfile, bolt GHA Docker-layer caching onto `ext/`

This was the original DEV-5938 direction. Add `actions/cache@v4` keyed on `cmake/dependencies.cmake` + vendor hashes, or use `RUN --mount=type=cache,target=./build/ext_build/`.

**Why rejected:**

- Delivers a performance win without migrating off the Dockerfile.
- But: leaves two caches (GHA + Cachix), two source-of-truth build graphs (`Dockerfile` + `flake.nix`), and the `Dockerfile` keeps drifting from the rest of the build system.
- DEV-6282 (hardening) wants `packages.docker` to be the production source so final-link flags can be toggled centrally. This alternative postpones that real fix.

### B. Migrate to `nix2container` (nlewo) instead of `dockerTools`

`nix2container` produces smaller, more layer-efficient images (~30% smaller in real cases) by skipping the intermediate tarball and talking directly to OCI layout.

**Why rejected (weakly):**

- Adds a new external dependency (separate flake input).
- `dockerTools.buildLayeredImage` is in nixpkgs and actively maintained — zero risk of abandonment.
- 300–600 MB layered image is acceptable for our distribution volume (the Docker Hub `daschswiss/sipi` tag churn is low).
- Revisit if image size becomes a problem post-migration.

### C. Cross-compile via `pkgsCross` instead of native per-arch runners

Build both arch images on a single amd64 runner using `pkgs.pkgsCross.aarch64-multiplatform`.

**Why rejected:**

- The DaSCH `multi-arch-static-build-ci-docker-native-per-arch.md` learning explicitly warns against this pattern for autotools/Kakadu builds — silent failures and architecture-mismatch non-determinism.
- Native runners are already in use (amd64 on `ubuntu-24.04`, arm64 on `ubuntu-24.04-arm`) and work correctly. No reason to abandon the working path.
- Would introduce a new cross-compile surface for `ext/*` that we've explicitly avoided in other parts of the build.

### D. Keep `pid1-rs` by packaging it as a Nix derivation

Write a small `pkgs.pid1-rs` derivation (Rust binary from upstream).

**Why rejected:**

- `tini` is already in nixpkgs, already used in hundreds of production images, and does the same job.
- `pid1-rs` is a single-author, less-tested project. No value in keeping it over tini just for semantic continuity.
- Removing a Rust toolchain from the image closure (if we ever went that route) is a net simplification.

### E. Use `dockerTools.buildImage` (monolithic single-layer) instead of `buildLayeredImage`

Simpler, but produces a single tarball with no layer sharing.

**Why rejected:**

- `buildLayeredImage` with `maxLayers = 125` already works and gives good layer sharing on Docker Hub. No reason to regress.

## Acceptance Criteria

### Functional Requirements

- [x] `packages.docker` and `packages.docker-stream` in `flake.nix` produce an image with: root runtime user (`config.User` unset, matching current production), `tini` entrypoint, HEALTHCHECK against `/health`, `TZ=Europe/Zurich`, `LC_ALL=C.UTF-8`, OCI labels, and a non-epoch `created` timestamp.<br>**Deviation from plan:** locales simplified from `en_US.UTF-8` + `sr_RS.UTF-8` (the historical Dockerfile's set) to `C.UTF-8`. Investigation during the PR (see PR #582 Challenges § "glibc-locales build failed with 'unsupported locales detected'") showed sipi has no code path that depends on locale categories beyond `LC_CTYPE` (used by exiv2/Lua/`std::locale()`); `C.UTF-8` is built into glibc itself and covers `LC_CTYPE` correctly without the `glibcLocales` derivation. `sr_RS.UTF-8` was traced via `git blame` to the very first sipi Dockerfile in 2016 with no contemporary justification and was never an active locale (`LC_ALL` was always `en_US.UTF-8`).
- [x] A comment in `flake.nix` next to the unset `User` field documents the NFS uid/gid coordination constraint (deferred to DEV-5920).
- [x] `packages.sipi-debug` exposes `$out.debug` as a standalone output consumable by `nix build .#sipi-debug`.
- [x] `providedVersion` flows into `sipi --version` output inside the image via `pkgs.sipi.override { providedVersion = ...; }`.<br>**Deviation from plan:** the originally-proposed env-var override path (read `SIPI_PROVIDED_VERSION` via `builtins.getEnv` under `--impure`) was dropped during implementation — `pkgs.sipi.version` (sourced from `version.txt`, which release-please updates pre-tag) gives the right value for the standard release flow, and ad-hoc overrides via `pkgs.sipi.override` cover the rare custom-version case. Documented in `docs/src/development/nix.md` under "Custom version override".
- [x] `just nix-docker-build` produces a Docker image that passes `just test-smoke-ci`.
- [x] `just nix-docker-build-{amd64,arm64}` produces arch-tagged images consumable by `docker-push-*` + `docker-publish-manifest`, AND creates `result-debug` as a byproduct (arch-pinned via `.#packages.<arch>-linux.{docker-stream,sipi-debug}`).<br>**Deviation from plan:** uses **two** `nix build` calls, not one. `nix build`'s `-o` flag is single-value (last wins), so `nix build -o result -o result-debug .#a .#b` doesn't produce two named symlinks as the plan assumed. Two calls is functionally equivalent and essentially free since the second call's closure is fully realized by the first.
- [x] `just nix-docker-extract-debug ${arch}` does no `nix build` call and produces a `sipi-${arch}.debug` file with the correct GNU build-id matching the image's sipi binary.
- [x] `docker-build.yml` on a PR: amd64 + arm64 jobs green, Docker Scout PR comment posted, SARIF uploaded.
- [ ] `publish.yml` on a release tag: per-arch images pushed, multi-arch manifest published, Sentry debug-symbols uploaded, SBOM artifact uploaded, GitHub Release assets attached.<br>*Will validate on the next release-tag push (release-please).*
- [x] `Dockerfile` and `.dockerignore` are deleted from the repo.
- [x] `justfile` contains zero `docker buildx build` invocations.
- [x] Every CI `run:` step that produces a Docker image invokes `just nix-docker-*`.

### Non-Functional Requirements

- [ ] `docker-build / amd64` median wall-clock time: **warm Cachix ≤ 4 min**; cold Cachix ≤ 11 min (baseline). Cache hits apply when neither `package.nix`, `flake.nix`, `flake.lock`, nor `cmake/dependencies.cmake` changed.<br>*Needs warm-cache median measured over ~10 post-merge runs.*
- [ ] `docker-build / arm64` within the same range.<br>*Same — needs measurement.*
- [ ] Local `just nix-docker-build` on warm Cachix completes in < 3 min.<br>*Holds anecdotally on the author's macOS+native-linux-builder; not formally measured.*
- [x] Image compressed size **way under** the original ≤ 600 MB budget. **Final: 116 MB compressed** (vs historical 217 MB — *smaller* than the Ubuntu-base image). See PR #582 Challenges § "Build-toolchain leaked into the runtime closure" and "ffmpeg-headless swap" for the path from 831 MB → 415 MB → 116 MB.
- [ ] Healthcheck latency inside the image ≤ 100 ms per probe.<br>*Not directly measured; container reaches `healthy` in 6 s end-to-end which implies ≪ 100 ms per probe.*
- [x] `docker run` container startup to "healthy" status: ≤ 15 s cold start. **Measured: 6 s** on local Docker Desktop.

### Quality Gates

- [x] `docker-build.yml` green on the feature branch on at least one PR. (PR #582)
- [x] `test.yml`, `sanitizer.yml` green on the feature branch.
- [ ] A manual `publish.yml` dispatch green on a dry-run tag (or demonstrably correct by inspection if dry-run is infeasible).<br>*Deferred: will validate on the next release-tag push.*
- [x] Branch history rewritten per the Commit Organization section before the PR is opened. Final 5 commits matched the plan's table 1:1 (`build`, `build(justfile)`, `ci`, `build`, `docs` — all hidden types per `commit-conventions.md`).
- [x] `docs/src/development/nix.md` has a "Docker image" section; `building.md` is updated; `CLAUDE.md` Quick Reference reflects the new recipes.
- [x] No grep hit for `docker buildx build` in `justfile` or `.github/workflows/`.
- [x] DEV-5920 and DEV-6282 both noted as unblocked in their Linear bodies. *Done as comments on each issue, 2026-04-28, linking back to PR #582.*

### Follow-up issues opened during implementation

- **DEV-6313** — `CI: install just via Nix; drop extractions/setup-just dependency`. Surfaced when an `extractions/setup-just@v4` Dependabot bump intersected with a transient GitHub `/repos/casey/just/releases` API outage and broke every workflow on every open PR. Already landed on `main` separately before this PR merged.
- **DEV-6321** — `Sipi base image: drop /bin/sh and ffmpeg; push runtime carriers to knora-sipi`. Architectural follow-up: sipi itself uses neither `/bin/sh` nor `ffmpeg`; both are present only because `daschswiss/knora-sipi` (FROM `daschswiss/sipi`) and `dsp-ingest` consume them. Pushing them down to where their consumers live should bring the base image to ~40-60 MB. Deferred until after DEV-5939 has soaked in production.

## Success Metrics

| Metric | Today | Target | Measurement |
|---|---|---|---|
| `docker-build / amd64` median wall-clock | ≈ 11 min | ≤ 4 min (warm), ≤ 11 min (cold) | `gh run list --workflow=docker-build.yml` |
| `docker-build / arm64` median wall-clock | ≈ 12 min | ≤ 4 min (warm), ≤ 12 min (cold) | same |
| Source-of-truth build graphs in repo | 2 (`Dockerfile` + `flake.nix`) | 1 (`flake.nix` only) | `ls Dockerfile` → ENOENT |
| Cache systems for Docker image build | 2 (GHA `type=gha` + Cachix) | 1 (Cachix only) | grep `ACTIONS_CACHE_URL` in workflows |
| Imperative `docker buildx build` invocations in justfile | 5 (1 in `docker-build` + 2 each in `docker-test-build-{amd64,arm64}` for the main image + debug-symbols stage extraction) | 0 | `rg 'docker buildx build' justfile` |
| Lines in `sipi/Dockerfile` | 143 | 0 (file deleted) | `wc -l Dockerfile` |
| Runtime user inside container | `root` | `root` (unchanged — DEV-5920 deferred) | `docker run --rm --entrypoint id daschswiss/sipi:<tag>` |
| Images scanned with correctly-populated Scout inventory | 100% (Ubuntu apt) | 100% (Cachix Nix-SBOM referrer) | Scout PR comment coverage, SBOM artifact present |

## Dependencies & Prerequisites

- `01-feat-nix-unified-build-plan.md` (Phase 1) implemented — ✅ done (sipi PR #558).
- `02-feat-justfile-ci-unification-plan.md` (Phase 1.5) implemented — ✅ merged (sipi PR #565, QA).
- DEV-5938 implicitly resolved by this plan (no dedicated work).
- Cachix `dasch-swiss` cache — ✅ live since 2024-04-30.
- `DASCHBOT_PAT` secret with impure-env handling — ✅ configured on test.yml, loadtest.yml, publish-static-release; this plan extends the same pattern to docker-build.yml and publish.yml:publish-docker.
- `CACHIX_AUTH_TOKEN` secret — ✅ already used by `publish-static-release`.
- Native `ubuntu-24.04-arm` GHA runner availability — ✅ already used by current `build_arm64` job.
- Determinate Systems' native-linux-builder (macOS → Linux-target local builds) — ✅ available to the author since 2026-04-20, used per-CLAUDE.md "Build completeness invariant".
- No external blockers.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Nix-built image is significantly larger than today's Dockerfile image (> 600 MB) | M | M | Audit layer structure via `docker history` during local verification; if over budget, investigate whether ffmpeg's Nix derivation pulls excessive runtime deps and override to disable unused codecs. Fallback: revisit nix2container (Alternative B) as a follow-up. |
| An accidental `config.User` / `fakeRootCommands` user-creation edit ships a non-root container image and breaks NFS reads of Image-root artefacts in production | L | H | The plan's Technical Approach explicitly forbids user-creation; the `flake.nix` comment documents the constraint. Verification checklist requires `docker run --rm --entrypoint id …` to report `uid=0(root)`. CI adds a grep assertion against `config.User` in the image manifest as a belt-and-braces guard. |
| `tini` behaves subtly differently from `pid1-rs` around signal propagation, causing SIGTERM-during-shutdown regressions | L | M | `tini` is battle-tested; signal-propagation semantics are standard. Run `docker stop` against a loaded image during local verification; ensure sipi exits cleanly within the grace period. If regressions surface, document any `tini -g` group-kill flag needs in `flake.nix`. |
| `HEALTHCHECK` uses an absolute curl path `/nix/store/…/bin/curl` that changes across nixpkgs updates and breaks external orchestrator health-check parsing | L | L | Use the short form `[ "CMD" "curl" "-sf" "http://localhost:1024/health" ]` — `curl` resolves via PATH inside the container at runtime, since `${pkgs.curl}/bin` ends up in the closure and is on PATH by nixpkgs convention. Verify with `docker run --rm --entrypoint which daschswiss/sipi:<tag> curl` (the default entrypoint is `[tini, --]` followed by sipi, so a `-c` shell invocation against the default entrypoint will not work — use `--entrypoint` to bypass tini). |
| `created` timestamp from `self.lastModifiedDate` is not valid RFC3339 after the substring surgery | L | L | Local verification includes `docker inspect` on the resulting image; validate that `Created` parses. If it doesn't, fall back to `"1970-01-01T00:00:01Z"` (epoch) which is compatible with all scanners — the "55 years old" warning is cosmetic, not a build failure. |
| `providedVersion` override doesn't propagate all the way to the binary's version string | L | L | Pre-migration the Dockerfile uses `ARG VERSION=…` + `-DEXT_PROVIDED_VERSION=$VERSION`. `package.nix:28` already has the parameter; verify with `docker run --rm daschswiss/sipi:<tag> --version` during local testing after wiring the flake-level override. |
| Docker Scout's CVE coverage regresses because Nix images lack dpkg metadata | M | M | Documented in Problem Statement §5. The plan accepts this regression for the Docker Scout scan (scanning kernel-level CVEs still works; dpkg-level does not). Follow-up job emits a CycloneDX SBOM via `nix-sbom` and attaches it as an OCI referrer so Scout, Grype, and Trivy can each find the component inventory. Track as "Future Considerations" / new Linear issue, not a blocker. |
| Sentry debug-file upload fails because the `.debug` file is in GNU build-id layout, not the `Dockerfile`'s custom layout | L | M | `sentry-cli debug-files upload` accepts GNU build-id directly (this is actually the better format). Verify with a local `sentry-cli debug-files upload --no-upload sipi-amd64.debug` during local verification; dry-run mode validates format without touching Sentry. |
| Loss of GHA Docker-layer cache causes first post-merge CI run to be very slow for a day | M | L | Schedule the merge on a low-activity day. Seed Cachix by running `nix build .#packages.x86_64-linux.docker-stream .#packages.aarch64-linux.docker-stream` + `cachix push dasch-swiss result*` locally before merging. |
| `nix-docker-build-*` recipe's `docker tag` step fails because `nix eval --raw .#packages.<arch>-linux.docker-stream.imageTag` format differs from `self.rev` expectation | L | L | Local verification runs this end-to-end. If the tag-query helper doesn't work, fall back to parsing `stdout` of the `nix build --print-out-paths` + `skopeo inspect --raw` pipe. |
| `packages.sipi-debug` passthrough doesn't work because `separateDebugInfo` output is not selected by `outputs = [ "out" ]` | L | L | `separateDebugInfo = true` automatically adds `"debug"` to outputs. Verify with `nix eval --json '.#default.outputs'` — should include `debug`. If it doesn't, add `outputs = [ "out" "debug" ]` explicitly to `package.nix`. |
| `docker/scout-action@v1` regressions because the locally-loaded image is now produced by `docker load` instead of `docker buildx build --load` | L | L | Scout reads the local Docker daemon; it doesn't care which tool loaded the image. Verify Scout runs on `local://daschswiss/sipi:latest` in a local smoke test. |
| `test-smoke-ci` (Rust testcontainers) can't find the image because `nix-docker-build` tags differ from what the test harness expects | L | M | Scripted: `nix-docker-build` explicitly `docker tag`s to `{{docker_image}}` and `:latest`, which is what `test-smoke-ci` expects today. Unchanged contract. Verify in local testing before pushing. |
| Consumers of the image (dsp-api, ingest) observe changed behavior from Nix-produced layer structure (e.g. `ls /usr/bin` is empty) | L | L | The image contract is the sipi binary + config + scripts + runtime libs, not `/usr/bin` contents. If a consumer has scripted something fragile like "exec /usr/bin/env python3 inside sipi container" it breaks — verify with the operations team whether such assumptions exist before merge. Fallback: add `pkgs.coreutils` + `pkgs.bash` (already in `contents`) so the most common userland is present. |
| Removing `pid1-rs` breaks a niche zombie-reaping behavior specific to ffmpeg subprocesses | L | L | `tini` reaps PID-1-orphaned children by default (same semantics as `pid1-rs`). Verify with `docker run --rm daschswiss/sipi:<tag>` + repeatedly hit an IIIF endpoint that forks ffmpeg; confirm no accumulating defunct processes via `docker exec sipi-check ps auxf`. |
| Multi-arch `docker manifest create` fails because per-arch tags are produced by `docker load` instead of `docker push` then re-resolved | L | L | `docker-publish-manifest` operates after `docker-push-{amd64,arm64}` in `publish.yml` — the manifest references *pushed* images by repo:tag, not locally-loaded ones. Flow unchanged. |
| The `$SIPI_DEBUG_BIN` for the `nix-docker-extract-debug` recipe ends up pointing at the dev-debug-info, not release-debug-info, producing the wrong .debug file | L | M | The plan has `packages.sipi-debug = self.packages.${system}.default.debug` — `.#default` is RelWithDebInfo (what ships in the Docker image per `package.nix:23` default `cmakeBuildType = "RelWithDebInfo"`). Ensure the flake attribute passthrough references `.default`, not `.dev`. Verify with `readelf -n result-debug/lib/debug/.build-id/*/*.debug` matches the hash on the binary inside the image. |

## Resource Requirements

- **Team:** 1 developer (Ivan). Estimated 3–4 days of focused work (flake/package edits + justfile rewrite + workflow migration + local verification + docs + commit-history cleanup). Comparable to Phase 1.5's timeline; the native-linux-builder continues to eliminate most CI-round-trip waste.
- **Infrastructure:** Cachix `dasch-swiss` account (existing). Native-linux-builder for local arm64 Linux builds on darwin. No new infrastructure.
- **Approvals:** Self-reviewed; PR-level review by reviewer-of-the-day. **No operations-team coordination is required for this plan** — the runtime user stays `root` (unchanged from today), so NFS uid/gid alignment is unaffected. The uid/gid coordination is a precondition for the *future* DEV-5920 PR, not for this one. As an optional sanity step, before merging this plan, sweep dsp-api / ingest Docker Compose configs for any baked-in `uid=0` assumptions; not expected to find any.

## Future Considerations

- **DEV-6282 (re-enable final-link hardening).** Unblocked by this plan. Follow-up PR adds a two-stage derivation: `ext/*` compiled with hardening off (current behavior), final `sipi` binary re-linked with `-fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wl,-z,relro -Wl,-z,now` + PIE where feasible. Acceptance criteria: `checksec result/bin/sipi` reports full hardening on `.#default` and `.#release`, unchanged on `.#sanitized` / `.#fuzz`.
- **DEV-5937 (run e2e tests against the Docker release image).** After this plan, a new `e2e-docker` job in `test.yml` can start `daschswiss/sipi:<tag>` via `docker run`, wait for `HEALTHCHECK healthy`, and run the Rust e2e suite against it. Separate from the migration itself.
- **CycloneDX SBOM via `nix-sbom` / `sbomnix` as an OCI referrer.** A `generate-sbom` job in `publish.yml` that runs `nix-sbom --output cyclonedx result/` + `docker buildx imagetools create --tag daschswiss/sipi:<tag>-sbom --provenance=...`. Scout, Grype, and Trivy all consume referrer-attached SBOMs; this closes the CVE-detection gap.
- **`nix2container` as a smaller-image alternative.** If the migrated image's size becomes a distribution problem, swap `dockerTools.buildLayeredImage` for `nix2container.buildImage`. Estimated 30–40% size reduction for the same closure.
- **Removing the Docker CLI from CI.** After the migration, the only Docker CLI interactions in CI are `docker load` (reading Nix's tarball into the daemon), `docker tag`, `docker push`, and `docker manifest`. If `skopeo` replaces these, the Docker daemon itself can be dropped from the CI runner's setup — saving ~15 s of daemon startup per job.
- **Cache-warming workflow on `main` merge.** A `.github/workflows/cache-warm.yml` that runs `nix build .#{default,dev,static-amd64,static-arm64,release-archive-amd64,release-archive-arm64,docker-stream}` on merge to main and pushes to Cachix. Makes every first PR on a new branch a full cache-hit.

## Documentation Plan

| Doc | Change |
|---|---|
| `CLAUDE.md` (Quick Reference) | Replace `just docker-build` / `just docker-test-build-*` with `just nix-docker-build` / `just nix-docker-build-{amd64,arm64}` / `just nix-docker-extract-debug <arch>`. Reinforce the "every build goes through Nix" invariant. |
| `UBIQUITOUS_LANGUAGE.md` | No changes intended. *Reviewer check:* confirm that "Image" remains an unambiguously IIIF-domain noun there, and that this plan's local override (Docker container image inside the build/CI scope) is acceptable as a one-off scoped to build/CI/Docker contexts. If the term "image" begins leaking into source-code naming for the Docker artefact, follow up with a glossary clarification — out of scope here. |
| `CONVENTIONS.md` | If CONVENTIONS.md mentions "Alpine (musl)" (noted as drift in Phase 1.5 §"Not in scope"), update to reflect the actual Nix/glibc image. |
| `docs/src/development/building.md` | Rewrite Docker section around `just nix-docker-build`; remove buildx-setup prerequisites; keep Docker daemon requirement. |
| `docs/src/development/ci.md` | Update Docker-build CI description: Cachix substitutes `ext/*` and sipi build artifacts; no GHA Docker layer cache. |
| `docs/src/development/nix.md` | Add "Docker image" section describing `packages.docker` / `packages.docker-stream` runtime shape (runs as root; NFS-coordination deferral documented with DEV-5920 link; tini; HEALTHCHECK; locales; OCI labels; `providedVersion` plumbing). |
| `README.md` | If README shows Docker-build commands, update to `just nix-docker-build`. |

## References & Research

### Internal References

- **Prior plans:**
  - `dasch-specs/specs/2026-04-16-sipi-nix-unified-build/01-feat-nix-unified-build-plan.md` (Phase 1 — merged #558)
  - `dasch-specs/specs/2026-04-16-sipi-nix-unified-build/02-feat-justfile-ci-unification-plan.md` (Phase 1.5 — merged #565)
- **Domain documentation (read before reasoning about names or boundaries — see `sipi/CLAUDE.md` §"Domain Model"):**
  - `sipi/UBIQUITOUS_LANGUAGE.md` — canonical SIPI glossary. This plan deliberately overloads "image" to mean the Docker container image inside the build/CI scope; all IIIF-domain terms (Image, Bitstream, Image root, Identifier, Cache key, Permission, Decode memory budget, …) keep their canonical meanings. See the **Terminology** section in this plan's Overview.
  - `sipi/CONTEXT.md` — SIPI image-server bounded context. The `/health` HEALTHCHECK contract added by this plan crosses the SIPI ↔ shttps seam documented here.
  - `sipi/CONTEXT-MAP.md` — bounded contexts and one-way SIPI → shttps dependency direction. Relevant because the HEALTHCHECK consumes shttps' health endpoint; the contract carries forward unchanged into the planned shttps→Rust strangler-fig migration.
  - `sipi/shttps/CONTEXT.md` — shttps embedded HTTP framework context. Owns the `/health` endpoint and the HTTP server lifecycle that the OCI Healthcheck probes.
- **Core files touched by this plan:**
  - `sipi/flake.nix:322-395` (existing `packages.docker` / `packages.docker-stream` — production parity upgrades)
  - `sipi/package.nix:23,28,181` (cmakeBuildType, providedVersion, separateDebugInfo)
  - `sipi/justfile:26-146` (imperative Docker section — full rewrite)
  - `sipi/Dockerfile` (143 lines — **deleted**)
  - `sipi/.dockerignore` (**deleted**)
  - `sipi/.github/workflows/docker-build.yml` (build_amd64, build_arm64 jobs — full migration)
  - `sipi/.github/workflows/publish.yml` (validate-docker, publish-docker jobs — full migration; sentry, manifest, publish-static-release, docs, release-gate unchanged)
  - `sipi/CLAUDE.md` (Quick Reference, invariants)
  - `sipi/CONVENTIONS.md` (image base image drift)
  - `sipi/README.md` (if Docker commands shown)
  - `sipi/docs/src/development/{building,ci,nix}.md`
- **Cached tests and fixtures unchanged:** `sipi/test/e2e-rust/tests/docker_smoke.rs` consumes `daschswiss/sipi:latest` by name; no change.

### External References

- **Nixpkgs dockerTools manual:** <https://github.com/NixOS/nixpkgs/blob/master/doc/build-helpers/images/dockertools.section.md>
- **Nixpkgs dockerTools examples:** <https://github.com/NixOS/nixpkgs/blob/master/pkgs/build-support/docker/examples.nix>
- **Graham Christensen — Optimising Docker Layers for Better Caching with Nix:** <https://grahamc.com/blog/nix-and-layered-docker-images/>
- **NixOS Discourse — non-root user in docker images:** <https://discourse.nixos.org/t/how-to-add-a-non-root-user-when-building-a-docker-image-with-nix/22883>
- **NixOS Discourse — chown for streamLayeredImage:** <https://discourse.nixos.org/t/how-to-run-chown-for-docker-image-built-with-streamlayeredimage-or-buildlayeredimage/11977>
- **NixOS Wiki — Debug Symbols:** <https://wiki.nixos.org/wiki/Debug_Symbols>
- **Docker image-spec healthcheck semantics:** <https://github.com/opencontainers/image-spec/issues/749>
- **Determinate Systems native-linux-builder:** <https://docs.determinate.systems/troubleshooting/native-linux-builder/>
- **tini (process init for containers):** <https://github.com/krallin/tini>
- **Snyk — Docker / OCI labels:** <https://snyk.io/blog/how-and-when-to-use-docker-labels-oci-container-annotations/>
- **Docker Scout component analysis:** <https://docs.docker.com/scout/explore/analysis/>
- **Sentry — Uploading debug information files:** <https://docs.sentry.io/platforms/native/data-management/debug-files/upload/>
- **CycloneDX + OCI referrer pattern:** <https://cyclonedx.org/specification/overview/>

### Institutional Learnings

- `dasch-specs/learnings/design-decisions/multi-arch-static-build-ci-docker-native-per-arch.md` — **primary applicability**. Reinforces the "native per-arch runners, not QEMU cross-build" invariant this plan inherits. Confirms `ubuntu-24.04` + `ubuntu-24.04-arm` matrix is the right approach for autotools/Kakadu-heavy builds.
- `dasch-specs/learnings/build-errors/cmake-externalproject-cross-compile-zig-autotools.md` — peripheral: applies to `ext/*` forwarding (unchanged in this plan; the sipi derivation's ExternalProject graph is already handled correctly by the flake).
- `dasch-specs/learnings/build-errors/zig-cc-glibc-header-contamination-musl-target.md` — peripheral: applies to `static-*` variants, not the Docker variant. The Docker image uses glibc (nixpkgs default), not musl — no contamination risk.
- `dasch-specs/learnings/build-errors/non-deterministic-build-inputs-cause-recompilation.md` — relevant: the `created` timestamp decision (prefer `self.lastModifiedDate` over `"now"`) is informed by this learning about non-determinism breaking cache hits.

### Related Linear Issues

- **DEV-5939** (this plan's primary issue): "Evaluate Nix-based Docker image building for better build caching" — status Todo → **Done** on merge.
- **DEV-5920**: "Docker: run as non-root user" — status Backlog → **Unblocked, still open**. Explicitly deferred because sipi reads Image and Bitstream artefacts under the **Image root** (per `sipi/UBIQUITOUS_LANGUAGE.md`) from an NFS mount that another service writes; a non-root uid inside the container requires uid/gid coordination with the NFS export before it can ship safely. A follow-up PR (after NFS ownership is coordinated with operations) can make the switch in a small, reviewable change now that the image is already Nix-backed.
- **DEV-5938**: "Improve Docker build times by caching external dependency compilation" — status Todo → **Superseded** by this plan (Cachix substitutes `ext/`).
- **DEV-6282**: "Sipi: re-enable hardening for final-link binary after Docker→Nix migration" — status Backlog → **Unblocked** by this plan; next in queue.
- **DEV-5937**: "Run e2e tests against Docker release image in CI" — status Todo, remains open after this plan lands (orthogonal — a new quality gate to add once Docker is Nix-backed).
- **DEV-6265** (Phase 1c sipi): **Done**.
- **DEV-6280** (Phase 1.5): **QA** — prerequisite met.
