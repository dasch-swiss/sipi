---
title: "Research findings: Bazel implementation specifics for the Sipi migration"
date: 2026-04-29
author: "Ivan Subotic"
status: draft
companion_to: "01-refactor-sipi-bazel-migration-plan.md"
repositories:
  - sipi
---

# Research findings: Bazel implementation specifics for the Sipi migration

Findings from four parallel best-practice research passes on the highest-risk Bazel APIs the migration depends on: `rules_foreign_cc`, `rules_oci`, `toolchains_llvm`, and `rules_rust` + Crate Universe. Each pass was scoped to concrete questions tied to specific PRs of the cutover sequence in `01-refactor-sipi-bazel-migration-plan.md`. This document captures findings that materially change the plan, with citations.

## Prompt-injection security note

All four research agents reported a recurring prompt-injection attempt in their tool outputs: web-search results contained fake `<system-reminder>` blocks impersonating Claude Code's runtime, fake MCP-server instructions ("claude.ai Consensus" tool with paper-citation formatting requirements), and a fake "Auto Mode" override. Every agent correctly identified these as untrusted content and ignored them. The attempts came through search-result content, not from any DaSCH-controlled surface. No action required beyond documenting that this happened during the migration's research phase, in case the same vector recurs in future research passes.

## Findings that change the plan

### 1. `oci_image_index` cannot consume external image digests — coordinator step needs `crane`

**Plan as written (Y+4):** "Per-arch CI runners + `oci_image_index` manifest assembly. CI matrix: linux-x86_64 runner + linux-aarch64 runner + manifest-coordinator job."

**Reality.** `oci_image_index`'s `images = [...]` attribute accepts only labels to `oci_image` targets within the same Bazel build graph. It cannot ingest external digests pushed to a registry by a different runner. The official [`multi_architecture_image` example](https://github.com/bazel-contrib/rules_oci/tree/main/examples/multi_architecture_image) achieves multi-arch via a Bazel platform transition — i.e., cross-compilation, which the plan explicitly rejects (see Q6 grilling).

**Corrected pattern (split-runner without cross-compile).**

| Step | Where | Command |
|---|---|---|
| 1 | linux-x86_64 runner | `bazel run //src:image_push -- --tag=sha-${GIT_SHA}-amd64` |
| 2 | linux-aarch64 runner | `bazel run //src:image_push -- --tag=sha-${GIT_SHA}-arm64` |
| 3 | coordinator job | `crane index append -m daschswiss/sipi:sha-${GIT_SHA}-amd64 -m daschswiss/sipi:sha-${GIT_SHA}-arm64 -t daschswiss/sipi:sha-${GIT_SHA}` then re-tag `latest` and `vX.Y.Z` |

`oci_push` natively supports `remote_tags` and stamping; per-arch `architecture =` and `os =` attrs on `oci_image` make per-runner images self-describing so the index-merge step has correct platform descriptors. The alternative `docker buildx imagetools create` works equivalently but adds a Docker daemon dependency to the coordinator runner.

**Plan correction.** Replace every reference to "`oci_image_index` assembles the manifest" with "`crane index append` (or `docker buildx imagetools create`) on a coordinator job assembles the manifest from per-arch pushed digests". `rules_oci`'s `oci_image_index` is *not* used in this migration's first cut.

Sources: [oci_image_index docs](https://github.com/bazel-contrib/rules_oci/blob/main/docs/image_index.md), [oci_push docs](https://github.com/bazel-contrib/rules_oci/blob/main/docs/push.md), [multi_architecture_image example](https://github.com/bazel-contrib/rules_oci/tree/main/examples/multi_architecture_image).

### 2. `oci_image` has no `healthcheck` attribute — must inject via `crane mutate` (Docker Swarm constraint)

**Plan as written (Y+4):** "Image must support `HEALTHCHECK CMD curl -sf http://localhost:1024/health`."

**Reality.** `HEALTHCHECK` is a Docker schema-2 config field that the OCI image spec [does not define](https://github.com/opencontainers/image-spec/issues/749). `oci_image` has no `healthcheck` attribute — verified in the current [docs/image.md](https://github.com/bazel-contrib/rules_oci/blob/main/docs/image.md) attribute list. The OCI spec gap is unresolved as of April 2026.

**Operational constraint: ops-deploy uses Docker Swarm.** Swarm reads the in-image `HEALTHCHECK` and uses it for service-state computation (`docker service ps` → `Healthy`/`Unhealthy`). There is no Swarm-level probe equivalent to Kubernetes `livenessProbe`.

**Verified against ops-deploy (2026-04-29 audit).** `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2` defines the `iiif` service that runs `daschswiss/sipi`:

- **No `healthcheck:` block** is set on the service — the deployment relies entirely on the image's baked-in `HEALTHCHECK`.
- **`endpoint_mode: dnsrr`** is set under `deploy:`. DNS round-robin endpoint mode means Swarm's own L4 load-balancer is bypassed; DNS records are added/removed based on task health, and **task health is computed from the in-image `HEALTHCHECK`**. Without it, an unhealthy sipi task is never removed from DNS — every cache miss continues to land on the broken task.
- **No active healthcheck Traefik label** (`traefik.http.services.…loadBalancer.healthCheck.*`) — Traefik delegates backend liveness to Swarm's task-state, which delegates to the image's HEALTHCHECK.
- **No `start_period` override** in the compose file — the image's `StartPeriod` (10s in today's `dockerTools` config, `flake.nix:262`) is what protects the deployment from premature unhealthy-restart loops on cold start.

The HEALTHCHECK is therefore not optional and not deployable-elsewhere. **It must be baked into the image.**

**Two viable patterns.**

**Pattern A (recommended) — move HEALTHCHECK to compose-level in ops-deploy.** Add a `healthcheck:` stanza to the `iiif` service in `docker-compose-iiif.yml.j2`. The image ships HEALTHCHECK-agnostic; ops-deploy owns the policy. This is consistent with three existing facts in the codebase:

- **Sipi's smoke test does not depend on the image-level HEALTHCHECK directive.** `test/e2e-rust/tests/docker_smoke.rs:135` (`docker_image_health_endpoint`) tests the `/health` HTTP endpoint via `reqwest`, asserts status 200 + JSON body. It does not call `docker inspect` on `Config.Healthcheck`.
- **Sipi's design intent separates endpoint from directive.** `test/hurl/health.hurl:1` reads: *"/health endpoint — used by Docker HEALTHCHECK and monitoring."* The endpoint is the contract; HEALTHCHECK is one consumer of it.
- **ops-deploy already uses compose-level healthcheck overrides.** `docker-compose-db.yml.j2:88-90` has a `healthcheck:` stanza on the `db` service (overriding `start_period: 15m`). The pattern is established.

Concrete change in `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2`:

```yaml
  iiif:
    image: "{{ DSP_IIIF_IMAGE }}:{{ DSP_VERSIONS['api'] }}"
    # ... existing config ...
    healthcheck:                                                                    # NEW
      test: ["CMD", "curl", "-sf", "http://localhost:1024/health"]                  # NEW
      interval: 30s                                                                 # NEW
      timeout: 5s                                                                   # NEW
      start_period: 10s                                                             # NEW
      retries: 3                                                                    # NEW
```

Values match today's `dockerTools` config (`flake.nix:255-264`): 30s interval, 5s timeout, 10s start period, 3 retries.

**Pattern B (fallback) — bake HEALTHCHECK via `crane mutate`.** Only use if pattern A is rejected for some reason (e.g., a non-DaSCH consumer of `daschswiss/sipi` is discovered to depend on the in-image HEALTHCHECK). Would require: a `genrule` in `//src:image` running `crane mutate` to inject Docker schema-2 `Healthcheck`; `crane` as a host-tool dependency in `flake.nix` `devShells`; a smoke-test assertion that `docker inspect` returns the expected `Healthcheck` JSON.

### Recommendation

**Pattern A.** Drops the `crane mutate` genrule, drops `crane` host-tool dep, drops the `docker inspect` smoke test. Y+4 PR scope shrinks. Image stays OCI-spec compliant. Operations policy lives in the operations repo.

**Coordinated change.** Y+4 ships in two PRs landing together:
- **sipi PR Y+4**: ships the image without HEALTHCHECK (the corresponding `dockerTools` block in `flake.nix:255-264` is deleted as part of `flake.nix` thinning).
- **ops-deploy PR Y+4-ops**: adds the `healthcheck:` stanza to `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2` *before* the new image is deployed. Order matters: deploy ops-deploy first (so when the new image lands, Swarm's task-state still resolves correctly via the compose-level healthcheck).

**Cost of pattern A vs the original `crane mutate` proposal.**

| Item | Pattern A | Pattern B (`crane mutate`) |
|------|-----------|----------------------------|
| Y+4 LOC change in sipi | ~50 less | (baseline) |
| Y+4 host-tool deps | unchanged | `crane` added to flake + CI |
| Image OCI-spec compliance | full | Docker schema-2 extension |
| ops-deploy change | one PR (~10 lines) | none |
| Per-env tunability of HEALTHCHECK | yes (compose vars) | no (image rebuild) |
| Default behavior for `docker run daschswiss/sipi` outside Swarm | no healthcheck | healthcheck active |
| External consumers of the image relying on HEALTHCHECK | breaks them | unchanged |

The "external consumer" risk is the only real cost of A. Sipi's image has been DaSCH-internal since the project's start; an external consumer relying on it would be a surprise. If that surprise materializes, switch to B in a follow-up — the change is reversible.

Sources: [oci_image docs](https://github.com/bazel-contrib/rules_oci/blob/main/docs/image.md), [opencontainers/image-spec#749](https://github.com/opencontainers/image-spec/issues/749), [Docker Compose v3 healthcheck reference](https://docs.docker.com/compose/compose-file/05-services/#healthcheck), [Docker Swarm health-check docs](https://docs.docker.com/engine/reference/commandline/service_create/#--health-cmd), `ops-deploy/roles/dsp-deploy/templates/docker-compose-iiif.yml.j2`, `ops-deploy/roles/dsp-deploy/templates/docker-compose-db.yml.j2:88-90` (existing compose-level healthcheck pattern), `sipi/test/e2e-rust/tests/docker_smoke.rs:135`, `sipi/test/hurl/health.hurl:1`.

### 3. fakeNss equivalent requires `rules_distroless` (or hand-rolled tar)

**Plan as written (Y+4):** "Image must contain: ... `fakeNss`-equivalent /etc/passwd + /etc/group + /etc/nsswitch.conf (since sipi runs as root reading from NFS)."

**Reality.** `rules_oci` has no first-class fakeNss rule. The clean replacement is the `rules_distroless` `passwd()` and `group()` macros, paired with a hand-written `nsswitch.conf`:

```python
load("@rules_distroless//distroless:defs.bzl", "passwd", "group")
load("@tar.bzl", "tar")

passwd(name = "passwd", entries = [
    {"username": "root",   "uid": 0,     "gid": 0,     "home": "/root", "shell": "/sbin/nologin", "gecos": ["root"]},
    {"username": "nobody", "uid": 65534, "gid": 65534, "home": "/",     "shell": "/sbin/nologin", "gecos": ["nobody"]},
])
group(name = "group", entries = [
    {"name": "root",   "gid": 0,     "users": ["root"]},
    {"name": "nobody", "gid": 65534, "users": []},
])
tar(name = "nsswitch_tar", srcs = ["nsswitch.conf"],
    mtree = ["./etc/nsswitch.conf type=file content=$(location nsswitch.conf) mode=0644"])
tar(name = "nss_layer", deps = [":passwd", ":group", ":nsswitch_tar"])
```

`nsswitch.conf` content matching today's Nix `dockerTools.fakeNss`:

```
passwd:    files
group:     files
shadow:    files
hosts:     files dns
networks:  files
services:  files
protocols: files
```

**Plan addition.** Y+4 adds `bazel_dep(name = "rules_distroless", version = "...")` to `MODULE.bazel`. The `:nss_layer` becomes one of the `tars` consumed by `//src:image`.

Sources: [rules_distroless `passwd`/`group` rules](https://github.com/GoogleContainerTools/rules_distroless/blob/main/docs/rules.md), [distroless examples/nonroot/BUILD](https://github.com/GoogleContainerTools/distroless/blob/main/examples/nonroot/BUILD).

### 4. Hermetic libc++ on Linux requires a sysroot

**Plan as written (Q5):** "Hermetic via `bazel-contrib/toolchains_llvm`, pinned to LLVM 19.1.x. ... libc++ is the C++ stdlib for default/dev/release/sanitized variants."

**Reality.** `toolchains_llvm` 1.7.0 supports `stdlib = "libc++"`. However, Linux libc++ shipped in the LLVM tarball is built against glibc and still needs libc/sysroot headers and runtime libs. Without an explicit sysroot, the build silently depends on the host's glibc version — breaking hermeticity exactly the way the plan claims to fix.

**Canonical pattern: Chromium sysroot via `llvm.sysroot` extension.**

```python
# MODULE.bazel
sysroot = use_repo_rule("@toolchains_llvm//toolchain:sysroot.bzl", "sysroot")

sysroot(
    name = "sysroot_linux_x64",
    urls = ["https://commondatastorage.googleapis.com/chrome-linux-sysroot/toolchain/<sha>/debian_<rev>_amd64_sysroot.tar.xz"],
    sha256 = "...",
)
sysroot(
    name = "sysroot_linux_arm64",
    urls = ["https://commondatastorage.googleapis.com/chrome-linux-sysroot/toolchain/<sha>/debian_<rev>_arm64_sysroot.tar.xz"],
    sha256 = "...",
)

llvm = use_extension("@toolchains_llvm//toolchain/extensions:llvm.bzl", "llvm")
llvm.toolchain(
    name = "llvm_toolchain",
    llvm_version = "19.1.7",
    cxx_standard = {"": "c++23"},
    stdlib = {
        "linux-x86_64":  "libc++",
        "linux-aarch64": "libc++",
        "darwin-aarch64": "libc++",
    },
)
llvm.sysroot(name = "llvm_toolchain", label = "@sysroot_linux_x64//sysroot",   targets = ["linux-x86_64"])
llvm.sysroot(name = "llvm_toolchain", label = "@sysroot_linux_arm64//sysroot", targets = ["linux-aarch64"])
use_repo(llvm, "llvm_toolchain", "llvm_toolchain_llvm")
register_toolchains("@llvm_toolchain//:all")
```

**Important:** the `cc_toolchain_config.bzl` source has this branch — `if stdlib == "builtin-libc++" and is_xcompile and not is_darwin_exec_and_target: stdlib = "stdc++"` — so under any cross-compile, bundled libc++ silently swaps to libstdc++. **Be explicit with `stdlib = "libc++"`** rather than relying on the `builtin-libc++` default; this prevents silent swaps when Y+8 introduces cross-compile.

**Escape hatch.** If sysroot management becomes painful, [`cerisier/toolchains_llvm_bootstrapped`](https://github.com/cerisier/toolchains_llvm_bootstrapped) is a zero-sysroot bootstrapped LLVM/libc fork (FOSDEM 2026). Treat as a Phase 2 escalation path, not Phase 1.

**Plan addition.** Y MODULE.bazel includes `sysroot()` repo rules + `llvm.sysroot()` calls per Linux target. Y's reviewable artifact criterion adds: "binary built on linux-x86_64 runner runs on a different glibc-version Linux container without re-linking" (proof of hermeticity).

Sources: [toolchains_llvm cc_toolchain_config.bzl](https://github.com/bazel-contrib/toolchains_llvm/blob/master/toolchain/cc_toolchain_config.bzl), [Casagrande sysroot post](https://steven.casagrande.io/posts/2024/sysroot-generation-toolchains-llvm/), [cerisier/toolchains_llvm_bootstrapped](https://github.com/cerisier/toolchains_llvm_bootstrapped).

### 5. `--config=fuzz` libstdc++ swap requires a SECOND registered toolchain

**Plan as written (Q5 / Y+3):** "Fuzz variant uses libstdc++ via a `--config=fuzz` toolchain switch (no separate stdenv-override gymnastics like today)."

**Reality.** `stdlib` is baked into the `cc_toolchain_config`, not exposed as a per-build flag. A `.bazelrc --config=fuzz` block alone *cannot* swap stdlib. The canonical pattern is to register a second toolchain, gate it via a constraint, and have `--config=fuzz` flip the platform selector.

```python
# MODULE.bazel — additional fuzz toolchain
llvm.toolchain(
    name = "llvm_toolchain_fuzz",
    llvm_version = "19.1.7",
    cxx_standard = {"": "c++23"},
    stdlib = {"": "stdc++"},
)
llvm.toolchain_root(
    name = "llvm_toolchain_fuzz",
    label = "@llvm_toolchain_llvm//:BUILD",   # share the downloaded LLVM, no re-fetch
)
llvm.extra_target_compatible_with(
    name = "llvm_toolchain_fuzz",
    constraints = ["//tools/fuzz:fuzz_enabled"],
)
use_repo(llvm, "llvm_toolchain_fuzz")
register_toolchains("@llvm_toolchain_fuzz//:all")
```

```python
# tools/fuzz/BUILD.bazel
constraint_setting(name = "fuzz_setting")
constraint_value(name = "fuzz_enabled", constraint_setting = ":fuzz_setting")

platform(
    name = "linux_x86_64_fuzz",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        ":fuzz_enabled",
    ],
)
```

```ini
# .bazelrc
build:fuzz --platforms=//tools/fuzz:linux_x86_64_fuzz
build:fuzz --copt=-fsanitize=fuzzer-no-link
build:fuzz --linkopt=-fsanitize=fuzzer
```

This is the same pattern used in upstream `tests/MODULE.bazel` for the `cxx17` vs `cxx20` variants. Costs more lines than ".bazelrc only" but it works; "30+ lines collapse to a `.bazelrc` config block" was overstated in the plan — it collapses to a `.bazelrc` config block **plus** ~20 lines of `MODULE.bazel` toolchain registration **plus** ~10 lines of `tools/fuzz/BUILD.bazel`.

**Note for the longer term.** Modern libFuzzer (LLVM ≥ 16) ships its own private libc++ copy. The libstdc++ swap is largely belt-and-suspenders parity with today's Nix override. Once the migration is stable, evaluate whether `--config=fuzz` can drop the second toolchain entirely and just add `-fsanitize=fuzzer{,-no-link}` flags to the default toolchain. That's a Y+8+ follow-up.

Sources: [toolchains_llvm cc_toolchain_config.bzl](https://github.com/bazel-contrib/toolchains_llvm/blob/master/toolchain/cc_toolchain_config.bzl), [tests/MODULE.bazel](https://raw.githubusercontent.com/bazel-contrib/toolchains_llvm/master/tests/MODULE.bazel), [bazel-contrib/rules_fuzzing](https://github.com/bazel-contrib/rules_fuzzing).

### 6. Reproducibility: `STABLE_*` keys + `expand_template` is the canonical pattern

**Plan as written (Y+4):** "`oci_image`'s `created` attribute is deterministic (computed from `--workspace_status_command` output, not wall-clock)."

**Reality.** Naïve uses of `--workspace_status_command` hit issue [#269](https://github.com/bazel-contrib/rules_oci/issues/269) — variables in `volatile-status.txt` (like `BUILD_TIMESTAMP`) cause cache thrashing. The canonical pattern is `STABLE_*` keys (which land in `stable-status.txt`) + `@aspect_bazel_lib//lib:expand_template.bzl` with `stamp_substitutions`.

```bash
# tools/workspace_status.sh
#!/usr/bin/env bash
set -euo pipefail
COMMIT_TS=$(git -C "${BUILD_WORKSPACE_DIRECTORY:-.}" log -1 --format=%cI)
echo "STABLE_GIT_COMMIT $(git rev-parse HEAD)"
echo "STABLE_GIT_VERSION $(git describe --tags --always --dirty)"
echo "STABLE_IMAGE_CREATED ${COMMIT_TS}"
```

```ini
# .bazelrc
build --workspace_status_command=tools/workspace_status.sh
build:release --stamp
```

```python
# src/BUILD.bazel
load("@aspect_bazel_lib//lib:expand_template.bzl", "expand_template")

expand_template(
    name = "image_created",
    out = "image_created.txt",
    template = ["1970-01-01T00:00:00Z"],
    stamp_substitutions = {
        "1970-01-01T00:00:00Z": "{{STABLE_IMAGE_CREATED}}",
    },
)

expand_template(
    name = "image_labels",
    out = "labels.txt",
    template = [
        "org.opencontainers.image.source=https://github.com/dasch-swiss/sipi",
        "org.opencontainers.image.revision=0000000000000000000000000000000000000000",
        "org.opencontainers.image.version=0.0.0",
        "org.opencontainers.image.licenses=AGPL-3.0-or-later",
        "org.opencontainers.image.title=sipi",
        "org.opencontainers.image.description=Simple Image Presentation Interface",
    ],
    stamp_substitutions = {
        "0000000000000000000000000000000000000000": "{{STABLE_GIT_COMMIT}}",
        "0.0.0": "{{STABLE_GIT_VERSION}}",
    },
)

oci_image(
    name = "image",
    base = "@distroless_base",
    created = ":image_created",
    labels = ":image_labels",
    entrypoint = ["/sbin/tini", "--", "/sipi/sipi"],
    cmd = ["--config=/sipi/config/sipi.config.lua"],
    env = {"TZ": "Europe/Zurich", "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"},
    tars = [":sipi_layer", ":tini_layer", ":curl_layer", ":cacert_layer",
            ":tzdata_layer", ":ffmpeg_layer", ":nss_layer"],
)
```

Two reproducibility-critical extras:
- **Pin base images by digest, not tag.** `oci_pull(name = "distroless_base", image = "...", digest = "sha256:...")` — never `tag =`.
- **Inputs to `oci_image` should not be always-stamped.** Use `stamp = -1` on layers/tars that aren't supposed to vary by build.
- **Verify reproducibility via `diffoci`** (recommended in the rules_oci FAQ).

Sources: [rules_oci FAQ](https://github.com/bazel-contrib/rules_oci/blob/main/FAQ.md), [issue #269](https://github.com/bazel-contrib/rules_oci/issues/269), [issue #49](https://github.com/bazel-contrib/rules_oci/issues/49), [aspect_bazel_lib expand_template](https://docs.aspect.build/rulesets/aspect_bazel_lib/docs/expand_template/).

### 7. Reproducible build-IDs require `-Wl,--build-id=sha1`

**Plan as written (Y+4):** ".debug file laid out under `lib/debug/.build-id/<xx>/<yy>.debug` (GNU build-id convention)".

**Reality.** Linker default `--build-id=...` is platform-dependent; some defaults produce a non-content-addressed UUID that varies between identical builds. To make the build-id reproducible (and the `.debug` path deterministic), add `linkopts = ["-Wl,--build-id=sha1"]` to `cc_binary`. The `.debug` extraction `genrule` then reads the build-id from the ELF note via `llvm-readelf -n`:

```python
genrule(
    name = "sipi_debug_split",
    srcs = ["//src:sipi"],
    outs = ["sipi.stripped", "build_id.txt", "sipi.debug"],
    cmd = """
      OBJCOPY=$(execpath @llvm_toolchain//:bin/llvm-objcopy)
      READELF=$(execpath @llvm_toolchain//:bin/llvm-readelf)
      BIN=$(location //src:sipi)
      BUILD_ID=$$($$READELF -n $$BIN | awk '/Build ID:/ {print $$3}')
      echo $$BUILD_ID > $(location build_id.txt)
      $$OBJCOPY --only-keep-debug $$BIN $(location sipi.debug)
      $$OBJCOPY --strip-debug --strip-unneeded $$BIN $(location sipi.stripped)
      $$OBJCOPY --add-gnu-debuglink=$(location sipi.debug) $(location sipi.stripped)
    """,
    tools = ["@llvm_toolchain//:bin/llvm-objcopy", "@llvm_toolchain//:bin/llvm-readelf"],
)

genrule(
    name = "sipi_debug_layout",
    srcs = [":sipi.debug", ":build_id.txt"],
    outs = ["debug_tree.tar"],
    cmd = """
      ID=$$(cat $(location :build_id.txt))
      PREFIX=$${ID:0:2}; SUFFIX=$${ID:2}
      mkdir -p out/lib/debug/.build-id/$$PREFIX
      cp $(location :sipi.debug) out/lib/debug/.build-id/$$PREFIX/$$SUFFIX.debug
      tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \\
          -cf $(OUTS) -C out lib
    """,
)
```

`sentry-cli debug-files upload bazel-bin/src/sipi.debug` works directly — `sentry-cli` reads the build-id from the ELF note, no special path layout required for the upload itself. The `lib/debug/.build-id/<xx>/<yy>.debug` layout is needed only if the `.debug` ships *inside* the image and a downstream tool (gdb, gcore) wants to find it via the GNU debuglink convention.

Sources: [Sentry CLI DIF docs](https://docs.sentry.io/cli/dif/), [Sentry native debug file formats](https://docs.sentry.io/platforms/native/data-management/debug-files/file-formats/).

### 8. Trivy supply-chain incident — corrected; Trivy is safe with SHA pinning

**Earlier claim (now retracted):** "Trivy is paused; use Syft." This was wrong — based on stale reading of the March 2026 incident.

**Corrected facts** (verified by a second research pass; see `03-research-findings-scout-replacement.md` for full citations):

- The incident was real but narrow: 19 March 2026, ~17:43–21:44 UTC. Attackers ("TeamPCP") force-pushed 76/77 tags in `aquasecurity/trivy-action`, all 7 tags in `aquasecurity/setup-trivy`, and pushed a malicious `trivy v0.69.4` binary for ~3 hours.
- **Resolved 23 March 2026.** Tags rebuilt; release pipeline now uses GitHub Apps, fine-grained tokens, SLSA provenance, Sigstore signing.
- **Project is actively maintained.** Trivy is the dominant open-source CVE scanner for OCI images.

**Implication for sipi.** This research pass remains correct that **sipi today does not use Trivy** — sipi uses Docker Scout (verified in `ci.yml` + `publish.yml`). So the "use Syft / Trivy paused" advice was doubly irrelevant: wrong on Trivy's status *and* not pertinent to sipi's current pipeline.

**For SBOM specifically.** Syft remains a reasonable SBOM tool — best-in-class for distroless / binary-cataloging — and is recommended as the SBOM source if/when sipi swaps Docker Scout for an OSS scanning stack (separate spec: `03-research-findings-scout-replacement.md`). But the migration plan does not require SBOM tool changes; Docker Scout's existing SPDX SBOM continues working.

**Mandatory hardening if any Trivy-action is ever introduced.** Pin the action by full commit SHA, **never** by tag — even after the incident's remediation, this is the new baseline practice. Safe pin: `aquasecurity/trivy-action@57a97c7e7821a5776cebc9bb87c984fa69cba8f1` (v0.35.0).

Sources superseded by `03-research-findings-scout-replacement.md`. Keeping this entry as a correction marker; the original Syft-only recommendation should be discarded.

### 9. Perl is a host-tool dependency for openssl

**Plan as written (Y):** Y adds `bazelisk` to `flake.nix` `devShells`. Lists no other host-tool dependencies.

**Reality.** openssl's `Configure` script is Perl. With hermetic LLVM, you still depend on host Perl unless you wire `rules_perl`. For Phase 1 the pragmatic path is to add `perl` to the `flake.nix` `devShells` package list — Nix-as-environment is the right place for this since Perl is a build-tool prerequisite, not a build artifact.

**Plan addition.** Y's `flake.nix` `devShells` additions: `bazelisk`, `perl` (new). Document Perl as a host-tool requirement in `docs/src/development/building.md`.

Sources: [openssl Configurations/README](https://github.com/openssl/openssl/blob/master/Configurations/README.md).

### 10. Each `rules_foreign_cc` rule is one Bazel action — no sub-action incrementalism

**Plan as written:** Risk matrix flags GHA cache eviction as M/L. Does not flag the per-rule action granularity issue.

**Reality.** Each `cmake()` / `configure_make()` / `make()` rule is **one** Bazel action with `lib_source` as a single input set. Touching any file under `lib_source` invalidates the action and reruns the entire foreign build from scratch. There is no incremental sub-target caching inside the foreign build.

For sipi specifically, this means: editing one file in `ext/<lib>` source (during a foreign-cc-rule update or patch authoring) rebuilds the entire lib. Consumers (sipi binary linking against `:openssl`) are fine — they only re-link if `:openssl`'s output changes content-hash.

**Mitigation in Y BUILD files.**

1. **Tight `lib_source` glob.** For each ext lib, define `filegroup(name = "all_srcs", srcs = glob(["**"], exclude = ["**/.git/**", "**/test/**", "**/tests/**", "docs/**", "**/*.md"]))`. Tests and docs being excluded is the largest single cache-stability win.
2. **Set `set_file_prefix_map = True`** on every foreign_cc rule. Adds `-ffile-prefix-map=$EXT_BUILD_ROOT=.` to compile actions, making outputs path-independent. Required for cache portability across machines (developer ↔ CI).
3. **Document the `bazel clean` footgun.** A partial output state from a failed `configure_make` can require `bazel clean` to retry. ([rules_foreign_cc#1034](https://github.com/bazelbuild/rules_foreign_cc/issues/1034), marked "not planned".) CI mitigation: clear `bazel-out/_tmp` on action failure.

Sources: [rules_foreign_cc docs/README.md](https://github.com/bazel-contrib/rules_foreign_cc/blob/main/docs/README.md), [issue #1034](https://github.com/bazelbuild/rules_foreign_cc/issues/1034), [issue #913](https://github.com/bazelbuild/rules_foreign_cc/issues/913).

### 11. `rules_rust` toolchain registration must precede the Rust extension

**Plan as written (Y+5):** "Replace `crane`-based `nix/rust-tests.nix` with `rules_rust` + Crate Universe consuming `test/e2e-rust/Cargo.lock`."

**Reality.** `cargo_build_script` rules (which run during Crate Universe materialization for any `*-sys` crate) invoke `cc` from the registered cc_toolchain. **Register the LLVM toolchain *before* the Rust extension is invoked**, otherwise build scripts may pick up host `cc` and produce non-hermetic artifacts. (Source: `rules_rust` 0.69.0/0.70.0 changelog notes about `cargo_build_script` cc resolution.)

For sipi's e2e crate, the dominant `*-sys` risk is `openssl-sys` pulled transitively by `reqwest`. Mitigation: pin `reqwest = { version = "...", default-features = false, features = ["rustls-tls"] }` to avoid `openssl-sys` entirely. `rustls-tls` is pure Rust + `ring`; no C++ deps.

**Plan addition.** Y+5 first commit: re-order `MODULE.bazel` so `bazel_dep(name = "toolchains_llvm", ...)` and `register_toolchains("@llvm_toolchain//:all")` precede `rust = use_extension(...)`. Also, audit `test/e2e-rust/Cargo.toml` for `*-sys` deps and pin `reqwest` features explicitly.

Sources: [rules_rust 0.70.0 release notes](https://github.com/bazelbuild/rules_rust/releases), [rules_rust issue #1519 — openssl-sys vendored feature](https://github.com/bazelbuild/rules_rust/issues/1519).

### 12. Hardening flags — neutralize via `.bazelrc`, not `compile_flags` override

**Plan as written:** Implicit assumption that `hardeningDisable = ["all"]` (current Nix dev shell setting) maps cleanly to Bazel.

**Reality.** `toolchains_llvm` defaults add `-fstack-protector`, `-fno-omit-frame-pointer`, and `-D_FORTIFY_SOURCE=1` (on optimized builds). The `compile_flags` attribute on `llvm.toolchain` *replaces* the default list — using it to neutralize hardening would break unrelated default flags and is brittle on toolchain upgrades.

**Canonical pattern: `.bazelrc` neutralization.**

```ini
build --copt=-U_FORTIFY_SOURCE
build --copt=-fno-stack-protector
build --copt=-fno-stack-clash-protection
build --copt=-fno-pie
build --linkopt=-no-pie
# foreign_cc deps inherit these because they read CFLAGS/CXXFLAGS from the action env
build --host_copt=-U_FORTIFY_SOURCE
build --host_copt=-fno-stack-protector
```

**Plan addition.** Y `.bazelrc` includes the hardening neutralization block. Document the rationale (foreign_cc deps misbehaving with PIE/FORTIFY) as a comment in `.bazelrc` matching today's `flake.nix` `hardeningDisable = ["all"]` rationale.

Sources: [toolchains_llvm cc_toolchain_config.bzl](https://github.com/bazel-contrib/toolchains_llvm/blob/master/toolchain/cc_toolchain_config.bzl).

## Findings that confirm the plan as written

These need no plan change but are worth recording.

- **Bzlmod is the supported path** for all four rule sets. The plan correctly avoids legacy WORKSPACE patterns. Older Stack Overflow / blog posts predating Bzlmod (mid-2023) are obsolete.
- **`rules_foreign_cc` 0.15.1** is current; `cmake_external` rename to `cmake` is years stable.
- **`rules_oci` 2.x removed the ephemeral local registry**; mutations now happen via `crane` against tarballs/OCI layouts. This is consistent with the corrected manifest-assembly pattern in §1.
- **`rules_rust` 0.70.0** (April 2026) is current; `crate.from_cargo` (lazy resolution from `Cargo.lock`) is the right default; `crates_vendor` is for offline CI only.
- **`toolchains_llvm` 1.7.0** is current; LLVM 19.1.x is supported. No 1.x → 2.x breaking change on the horizon.
- **Entrypoint vs Cmd**: `oci_image`'s `entrypoint = ["/sbin/tini", "--", "/sipi/sipi"]` + `cmd = ["--config=..."]` correctly preserves Docker's Cmd-override semantics so `docker run image --help` reaches sipi (not tini). The `--` after `tini` is critical.
- **`cxx_standard = {"": "c++23"}` on `llvm.toolchain`** is the right way to set C++23, not `--cxxopt=-std=c++23` in `.bazelrc`. (The `--cxxopt` route appends *after* the toolchain's own `-std=c++17`, leading to "two `-std` flags, last wins" fragility.)
- **`rules_distroless` `passwd`/`group`** is the clean fakeNss replacement.
- **`oci_push`'s stamping** with `remote_tags` cleanly handles the multi-tag-per-build pattern (`v<version>` + `latest` + `sha-...`).

## Updated risk matrix

Risks reframed in light of the research, with corrected impact and named mitigations. Replaces the corresponding entries in `01-refactor-sipi-bazel-migration-plan.md`.

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Linux libc++ hermeticity requires Chromium sysroot — pin and download in MODULE.bazel | M | M | Pattern documented in §4. Escape hatch: `cerisier/toolchains_llvm_bootstrapped`. |
| `--config=fuzz` libstdc++ swap requires second registered toolchain (cannot be `.bazelrc` only) | L | L | Pattern documented in §5. Y+3 lands ~30 lines of `MODULE.bazel` + `tools/fuzz/BUILD.bazel` + `.bazelrc`. |
| `oci_image_index` cannot consume external digests — coordinator must use `crane index append` | M | L | Pattern documented in §1. Coordinator job spec: a small shell step running `crane`. CI matrix unchanged. |
| `oci_image` has no `healthcheck` attribute; ops-deploy uses Docker Swarm which requires HEALTHCHECK in image OR in compose | L | L | Move HEALTHCHECK to compose-level `healthcheck:` stanza in `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2` (§2 Pattern A). Coordinated with Y+4 sipi PR. Pattern already established for the `db` service. Pattern B (`crane mutate`) is the fallback if an external consumer dependency surfaces. |
| `cargo_build_script` for `*-sys` crates breaks Crate Universe | M | M | Audit `test/e2e-rust/Cargo.toml` for `*-sys` deps before Y+5. Pin `reqwest` to `default-features = false, features = ["rustls-tls"]`. Order: register LLVM toolchain *before* Rust extension. |
| openssl `Configure` requires host Perl | H | L | Add `perl` to `flake.nix` `devShells` in Y. Document as host-tool requirement. |
| Each `rules_foreign_cc` rule is a single Bazel action — no sub-action incrementalism | H | L | Tight `lib_source` glob excluding tests/docs (§10). `set_file_prefix_map=True` for cache portability. Document `bazel clean` footgun. |
| Reproducibility broken by volatile-status keys | M | M | Use `STABLE_*` keys + `expand_template` + `stamp_substitutions` (§6). Verify via `diffoci`. Pin base images by digest. |
| Build-IDs non-reproducible without explicit `--build-id=sha1` | L | M | Add `linkopts = ["-Wl,--build-id=sha1"]` to `cc_binary` in `//src:sipi`. |
| Hardening defaults conflict with foreign_cc deps | M | L | `.bazelrc` neutralization block (§12), not `compile_flags` override on `llvm.toolchain`. |
| macOS depends on Xcode CLT SDK — not "fully hermetic" | L | L | Pin `MACOSX_DEPLOYMENT_TARGET=12.0` in `.bazelrc`. Document as a macOS requirement (Xcode CLT install). |
| ~~Trivy paused — need alternative SBOM tooling~~ | N/A | N/A | **Retracted** — earlier claim was wrong. Trivy is actively maintained; March 2026 incident resolved 23 Mar 2026. See §8 (corrected). sipi uses Docker Scout (not Trivy). Optional Scout→OSS swap covered in `03-research-findings-scout-replacement.md`. |

The five risks from the original plan that this research **eliminated or downgraded**:

- Risk "`rules_foreign_cc` cannot replicate openssl configure flags" — *downgraded from M/M to L/L*. Pattern is well-documented (§1 of the rules_foreign_cc findings); openssl example in upstream `rules_foreign_cc` repo. Spike confirms the pattern works.
- Risk "Kakadu Linux Makefile patches don't apply cleanly via `http_archive`'s `patches=[]`" — *downgraded from M/H to L/L*. `http_archive`'s `patches=[]` + `patch_args=["-p1"]` is canonical; spike confirms ordering semantics.
- Risk "`rules_oci` Docker image is not byte-reproducible" — *downgraded from L/M to L/L*. `STABLE_*` + `expand_template` is the well-trodden path; verify via `diffoci`.
- Risk "`rules_rust` + Crate Universe disagrees with `Cargo.lock`" — *downgraded from M/M to L/M*. `crate.from_cargo` consumes `Cargo.lock` directly; `CARGO_BAZEL_REPIN=1 bazel sync --only=crates` is the canonical workflow.
- Risk "Reviewer fatigue on Y's ~1500-line PR" — *unchanged at H/M*. Research doesn't help with reviewer fatigue; mitigation remains pre-Y walkthrough.

## Concrete artifacts to add to Y

Implementation specifics that the research surfaced and that should land in PR Y verbatim.

### `MODULE.bazel` — toolchain block

```python
module(name = "sipi", version = "0.0.0")

bazel_dep(name = "rules_foreign_cc", version = "0.15.1")
bazel_dep(name = "rules_oci",         version = "2.x")
bazel_dep(name = "rules_distroless",  version = "...")
bazel_dep(name = "aspect_bazel_lib",  version = "...")
bazel_dep(name = "platforms",         version = "1.0.0")
bazel_dep(name = "toolchains_llvm",   version = "1.7.0")

# --- Sysroot pulls (Chromium debian sysroot, pinned by sha256) ---
sysroot = use_repo_rule("@toolchains_llvm//toolchain:sysroot.bzl", "sysroot")
sysroot(name = "sysroot_linux_x64",   urls = [...], sha256 = "...")
sysroot(name = "sysroot_linux_arm64", urls = [...], sha256 = "...")

# --- LLVM toolchain (default + fuzz variant sharing same downloaded LLVM) ---
llvm = use_extension("@toolchains_llvm//toolchain/extensions:llvm.bzl", "llvm")
llvm.toolchain(
    name = "llvm_toolchain",
    llvm_version = "19.1.7",
    cxx_standard = {"": "c++23"},
    stdlib = {"linux-x86_64": "libc++", "linux-aarch64": "libc++", "darwin-aarch64": "libc++"},
)
llvm.sysroot(name = "llvm_toolchain", label = "@sysroot_linux_x64//sysroot",   targets = ["linux-x86_64"])
llvm.sysroot(name = "llvm_toolchain", label = "@sysroot_linux_arm64//sysroot", targets = ["linux-aarch64"])
llvm.toolchain(
    name = "llvm_toolchain_fuzz",
    llvm_version = "19.1.7",
    cxx_standard = {"": "c++23"},
    stdlib = {"": "stdc++"},
)
llvm.toolchain_root(name = "llvm_toolchain_fuzz", label = "@llvm_toolchain_llvm//:BUILD")
llvm.extra_target_compatible_with(name = "llvm_toolchain_fuzz", constraints = ["//tools/fuzz:fuzz_enabled"])
use_repo(llvm, "llvm_toolchain", "llvm_toolchain_llvm", "llvm_toolchain_fuzz")
register_toolchains("@llvm_toolchain//:all", "@llvm_toolchain_fuzz//:all")
```

### `.bazelrc` — minimum required entries

```ini
common --enable_bzlmod
build  --incompatible_enable_cc_toolchain_resolution
build  --incompatible_strict_action_env
build  --action_env=BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1
build  --workspace_status_command=tools/workspace_status.sh

# Hardening neutralization (matches today's flake hardeningDisable=["all"])
build --copt=-U_FORTIFY_SOURCE
build --copt=-fno-stack-protector
build --copt=-fno-stack-clash-protection
build --copt=-fno-pie
build --linkopt=-no-pie
build --host_copt=-U_FORTIFY_SOURCE
build --host_copt=-fno-stack-protector

# macOS deployment target
build:macos --copt=-mmacosx-version-min=12.0
build:macos --linkopt=-mmacosx-version-min=12.0
build:macos --action_env=MACOSX_DEPLOYMENT_TARGET=12.0

# Sanitizer config
build:asan --strip=never --copt=-fsanitize=address --copt=-DADDRESS_SANITIZER --linkopt=-fsanitize=address
build:ubsan --strip=never --copt=-fsanitize=undefined --linkopt=-fsanitize=undefined

# Fuzz config (uses second toolchain via --platforms)
build:fuzz --platforms=//tools/fuzz:linux_x86_64_fuzz
build:fuzz --copt=-fsanitize=fuzzer-no-link
build:fuzz --linkopt=-fsanitize=fuzzer

# Release stamping
build:release --stamp
```

### `tools/workspace_status.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
COMMIT_TS=$(git -C "${BUILD_WORKSPACE_DIRECTORY:-.}" log -1 --format=%cI)
echo "STABLE_GIT_COMMIT $(git rev-parse HEAD)"
echo "STABLE_GIT_VERSION $(git describe --tags --always --dirty)"
echo "STABLE_IMAGE_CREATED ${COMMIT_TS}"
```

Make executable: `chmod +x tools/workspace_status.sh`. Reference from `.bazelrc` (above).

### `flake.nix` `devShells` package list — host-tool additions

Add to `devShells.{default, clang, fuzz, gcc}` package lists in Y (and Y+4):

```nix
packages = with pkgs; [
  # … existing entries …
  bazelisk    # NEW (Y):    Bazel launcher
  perl        # NEW (Y):    required by openssl's Configure script
  # crane is NOT added — pattern A in §2 moves HEALTHCHECK to ops-deploy compose, not the image
];
```

### `cc_binary` link flags

`//src:sipi`'s `cc_binary` includes:

```python
cc_binary(
    name = "sipi",
    # …
    linkopts = [
        "-Wl,--build-id=sha1",   # reproducible content-addressed build-id
        # … other linkopts …
    ],
)
```

## Citation index

Grouped by topic, ordered by relevance.

### `rules_foreign_cc`

- Repo: <https://github.com/bazel-contrib/rules_foreign_cc>
- Docs: <https://github.com/bazel-contrib/rules_foreign_cc/blob/main/docs/README.md>
- openssl example: <https://github.com/bazel-contrib/rules_foreign_cc/blob/main/examples/third_party/openssl/BUILD.openssl.bazel>
- Placeholder framework: <https://github.com/bazel-contrib/rules_foreign_cc/blob/main/foreign_cc/private/framework.bzl>
- BCR: <https://registry.bazel.build/modules/rules_foreign_cc>
- Issue #185 (macOS libtool): <https://github.com/bazelbuild/rules_foreign_cc/issues/185>
- Issue #418 (`EXT_BUILD_DEPS` layout): <https://github.com/bazelbuild/rules_foreign_cc/issues/418>
- Issue #1034 (clean-after-failure): <https://github.com/bazelbuild/rules_foreign_cc/issues/1034>
- Issue #1186 (Xcode 15.3 strict clang): <https://github.com/bazel-contrib/rules_foreign_cc/issues/1186>
- HDL Factory `make` rule guide: <https://www.hdlfactory.com/post/2023/06/13/how-to-use-the-make-rule-from-rules_foreign_cc-repository-for-bazel/>

### `rules_oci`

- Repo: <https://github.com/bazel-contrib/rules_oci>
- `oci_image` docs: <https://github.com/bazel-contrib/rules_oci/blob/main/docs/image.md>
- `oci_image_index` docs: <https://github.com/bazel-contrib/rules_oci/blob/main/docs/image_index.md>
- `oci_push` docs: <https://github.com/bazel-contrib/rules_oci/blob/main/docs/push.md>
- FAQ (reproducibility, `stamp = -1`, `diffoci`): <https://github.com/bazel-contrib/rules_oci/blob/main/FAQ.md>
- Multi-arch example: <https://github.com/bazel-contrib/rules_oci/tree/main/examples/multi_architecture_image>
- Labels example: <https://github.com/bazel-contrib/rules_oci/blob/main/examples/labels/BUILD.bazel>
- Issue #49 (stamp creation time): <https://github.com/bazel-contrib/rules_oci/issues/49>
- Issue #269 (volatile vs stable status): <https://github.com/bazel-contrib/rules_oci/issues/269>
- Aspect bazel_lib `expand_template`: <https://docs.aspect.build/rulesets/aspect_bazel_lib/docs/expand_template/>
- Aspect — Migrating Docker Compose tests: <https://blog.aspect.build/integration-testing-oci>
- `rules_distroless` rules: <https://github.com/GoogleContainerTools/rules_distroless/blob/main/docs/rules.md>
- distroless examples/nonroot/BUILD: <https://github.com/GoogleContainerTools/distroless/blob/main/examples/nonroot/BUILD>
- OCI image-spec HEALTHCHECK gap: <https://github.com/opencontainers/image-spec/issues/749>
- Sentry CLI DIF: <https://docs.sentry.io/cli/dif/>
- Sentry native debug formats: <https://docs.sentry.io/platforms/native/data-management/debug-files/file-formats/>
- tini README: <https://github.com/krallin/tini>

### `toolchains_llvm`

- Repo: <https://github.com/bazel-contrib/toolchains_llvm>
- BCR: <https://registry.bazel.build/modules/toolchains_llvm>
- `tests/MODULE.bazel`: <https://raw.githubusercontent.com/bazel-contrib/toolchains_llvm/master/tests/MODULE.bazel>
- `cc_toolchain_config.bzl`: <https://github.com/bazel-contrib/toolchains_llvm/blob/master/toolchain/cc_toolchain_config.bzl>
- `repo.bzl` attributes: <https://raw.githubusercontent.com/bazel-contrib/toolchains_llvm/master/toolchain/internal/repo.bzl>
- Casagrande on sysroot generation: <https://steven.casagrande.io/posts/2024/sysroot-generation-toolchains-llvm/>
- Casagrande on macOS LLVM packages: <https://steven.casagrande.io/posts/2024/building-macos-llvm-package/>
- `cerisier/toolchains_llvm_bootstrapped`: <https://github.com/cerisier/toolchains_llvm_bootstrapped>
- `hermeticbuild/hermetic-llvm`: <https://github.com/hermeticbuild/hermetic-llvm>
- FOSDEM 2026 zero-sysroot LLVM: <https://fosdem.org/2026/schedule/event/F8SDAA-zero-sysroot_hermetic_llvm_cross-compilation_using_bazel/>
- `rules_foreign_cc` issue #592 (hermetic CC): <https://github.com/bazel-contrib/rules_foreign_cc/issues/592>
- `rules_fuzzing`: <https://github.com/bazel-contrib/rules_fuzzing>
- Bazel blog on rules_fuzzing: <https://blog.bazel.build/2021/02/08/rules-fuzzing.html>

### `rules_rust`

- Repo: <https://github.com/bazelbuild/rules_rust>
- Releases: <https://github.com/bazelbuild/rules_rust/releases>
- BCR: <https://registry.bazel.build/modules/rules_rust>
- Crate Universe (Bzlmod): <https://bazelbuild.github.io/rules_rust/crate_universe_bzlmod.html>
- Toolchains: <https://bazelbuild.github.io/rules_rust/rust_toolchains.html>
- Defs (rust_test): <https://bazelbuild.github.io/rules_rust/defs.html#rust_test>
- All-crate-deps example: <https://github.com/bazelbuild/rules_rust/blob/main/examples/all_crate_deps/MODULE.bazel>
- Vendor example: <https://github.com/bazelbuild/examples/blob/main/rust-examples/07-deps-vendor/README.md>
- cargo-bazel handbook: <https://abrisco.github.io/cargo-bazel/>
- Tweag — Rust workspace with Bazel: <https://www.tweag.io/blog/2023-07-27-building-rust-workspace-with-bazel/>
- mmapped — Scaling Rust builds with Bazel: <https://mmapped.blog/posts/17-scaling-rust-builds-with-bazel>
- Issue #205 (proc-macros host/target): <https://github.com/bazelbuild/rules_rust/issues/205>
- Issue #1519 (openssl-sys vendored): <https://github.com/bazelbuild/rules_rust/issues/1519>
- Issue #2524 (`+` in version vendoring): <https://github.com/bazelbuild/rules_rust/issues/2524>
- Aspect — OCI integration testing: <https://blog.aspect.build/integration-testing-oci>
