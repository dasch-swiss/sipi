---
title: "Research findings: Open-source replacement for Docker Scout in the rules_oci ecosystem"
date: 2026-04-30
author: "Ivan Subotic"
status: draft
companion_to: "01-refactor-sipi-bazel-migration-plan.md"
repositories:
  - sipi
---

# Research findings: Open-source replacement for Docker Scout in the rules_oci ecosystem

Best-practice research on swapping Docker Scout for an open-source container-scanning + SBOM + signing stack, integrated with `rules_oci`-built images. Companion to `01-refactor-sipi-bazel-migration-plan.md`. Findings inform an **optional post-Y+4 follow-up** (Y+9, after the build-tool migration is stable) — not bundled into the migration itself, to keep the migration's risk surface bounded.

## Executive summary

| Concern | Today (Docker Scout) | Recommended OSS replacement |
|---|---|---|
| CVE scan + SARIF | `docker/scout-action@v1` `command: cves` | `aquasecurity/trivy-action` SHA-pinned to `57a97c7e7821a5776cebc9bb87c984fa69cba8f1` |
| SARIF → GitHub Security | `github/codeql-action/upload-sarif@v4` | unchanged |
| SBOM generation | `docker/scout-action@v1` `command: sbom` (SPDX) | `anchore/sbom-action` (Syft, SPDX 2.3) |
| PR comment "compare to production" | `docker/scout-action@v1` `command: compare` | bash + `jq` diff between two Trivy SARIFs; baseline digest from `crane digest daschswiss/sipi:latest` |
| Image signing (NEW capability) | not provided by Scout | `rules_oci` `cosign_sign`, keyless OIDC |
| SBOM attestation (NEW capability) | not provided by Scout | `rules_oci` `cosign_attest --type spdx` |
| Production environment recording | `docker/scout-action@v1` `command: environment` | unneeded — replaced by cosign attestation against the released digest |

**Decision recommended:** swap Scout → OSS stack as a **post-Y+4 follow-up PR** (Y+9 in the plan's sequence numbering). Bundling into Y+4 is technically possible but multiplies the risk of an already-load-bearing PR. Defer.

**Net new capabilities** the OSS stack provides that Scout never did:
- Sigstore image signing with Rekor transparency log (audit trail for every released image).
- SBOM bound to image digest as a verifiable attestation.
- No reliance on Docker Hub as a paid-feature gateway.

## 1. Trivy supply-chain incident — verified

The earlier research-findings note in `02-research-findings-bazel-implementation.md` §8 claimed "Trivy is paused, use Syft". **This was wrong.** Verified facts as of 30 April 2026:

- **Window**: 19 March 2026, ~17:43–21:44 UTC. Attackers identifying as "TeamPCP" force-pushed 76 of 77 tags in `aquasecurity/trivy-action`, all 7 tags in `aquasecurity/setup-trivy`, and pushed a malicious `trivy v0.69.4` binary that was live for ~3 hours.
- **Resolution**: investigation closed 23 March 2026. Tags rebuilt. Release pipeline rebuilt around GitHub Apps + fine-grained tokens + SLSA provenance + Sigstore signing.
- **Status**: actively maintained.
- **Mandatory hardening**: pin actions by full commit SHA, **never** by tag — true even now that the pipeline is remediated.
- **Safe references**:
  - `trivy` ≥ v0.69.5 (or any release ≤ v0.69.3)
  - `trivy-action` v0.35.0 = `57a97c7e7821a5776cebc9bb87c984fa69cba8f1`
  - `setup-trivy` v0.2.6 = `3fb12ec`

Sources: [Trivy advisory GHSA-69fq-xp46-6x23](https://github.com/aquasecurity/trivy/security/advisories/GHSA-69fq-xp46-6x23), [Trivy discussion #10462 (incident closure)](https://github.com/aquasecurity/trivy/discussions/10462), [Aqua blog](https://www.aquasec.com/blog/trivy-supply-chain-attack-what-you-need-to-know/), [Wiz incident analysis](https://www.wiz.io/blog/trivy-compromised-teampcp-supply-chain-attack), [Microsoft Defender post](https://www.microsoft.com/en-us/security/blog/2026/03/24/detecting-investigating-defending-against-trivy-supply-chain-compromise/), [StepSecurity v0.69.4 details](https://www.stepsecurity.io/blog/trivy-compromised-a-second-time---malicious-v0-69-4-release).

## 2. CVE scanner comparison

| Tool | SARIF | Distroless / no-pkg-DB | Notes |
|---|---|---|---|
| **Trivy** (Aqua) | first-class (`--format sarif`) | scans binaries + language manifests; layer-aware | broadest coverage; recently hardened post-incident |
| **Grype** (Anchore) | first-class | excellent on Syft-generated SBOMs; fewer false positives | best when paired with Syft (SBOM-first scanning) |
| **OSV-Scanner** (Google) | first-class, layer-aware | container mode added in v2; uses `osv.dev` (Go-style ranges) | weakest on OS/binary fingerprinting in distroless |
| Clair | JSON (no native SARIF) | requires registry deployment | server-mode; overkill for sipi CI |
| Dockle | linter, not CVE | n/a | misconfig-only; complementary, not a replacement |

**Trivy and Grype overlap ~60–65% on findings.** The defensible pattern: **Syft → SBOM → Grype** for the highest signal, optionally cross-checked with Trivy. For minimal surface area in sipi CI, **Trivy alone** is the cleanest single-tool replacement for Scout's CVE scanning.

Sources: [appsecsanta Trivy vs Grype](https://appsecsanta.com/sca-tools/trivy-vs-grype), [anchore/grype README](https://github.com/anchore/grype).

## 3. SBOM generation comparison

| Tool | Distroless accuracy | Bazel integration |
|---|---|---|
| **Syft** | Best-in-class binary cataloging (Go, Rust, Python wheels, JARs); SPDX 2.3 / CycloneDX 1.6 | CLI only |
| Trivy SBOM | Good, optimised for OS packages | CLI only |
| OSV-Scanner SBOM | Weakest for binary-only layers | CLI only |
| `@rules_license//tools:write_sbom` | Only sees Bazel-tracked deps; cannot enumerate base-layer contents | native Bazel rule |
| `thesayyn/rules_bom` | Proof-of-concept, not production | native Bazel rule |
| `rules_oci` native SBOM | **does not exist as of April 2026** — only `cosign_sign` and `cosign_attest` (developer preview) | n/a |

**No production-grade Bazel rule emits a container-image SBOM into the build graph today.** `rules_license`'s `write_sbom` is useful as a complementary build-graph SBOM for the Sipi binary itself, but cannot enumerate contents of base layers (`rules_distroless`-derived debs, etc.). The realistic pattern is **Syft as a CI step** against the loaded OCI tarball.

Sources: [bazel-contrib/rules_oci](https://github.com/bazel-contrib/rules_oci), [rules_license SBOM rules](https://deepwiki.com/bazelbuild/rules_license/4.2-sbom-rules), [anchore/syft](https://github.com/anchore/syft).

## 4. PR-comment "compare to production" workflow

Native diff modes:

- **Trivy**: [issue #1365](https://github.com/aquasecurity/trivy/issues/1365) (image diff) is **still open since 2021**.
- **Grype**: no native diff.
- **OSV-Scanner**: no native diff.

OSS patterns, ranked:

1. **`jq`-diff two SARIFs** (recommended) — scan PR image and the production digest (resolved via `crane digest daschswiss/sipi:latest`), diff `runs[].results[].ruleId` sets, post via `marocchino/sticky-pull-request-comment`. ~30 lines of bash.
2. **`reviewdog/action-trivy`** — wraps Trivy with reviewdog; comments inline on PRs but does **not** compare against a baseline. ([reviewdog/action-trivy](https://github.com/reviewdog/action-trivy))
3. **`domstolene/trivy-pr-report`** — full report as PR comment, no baseline diff. ([domstolene/trivy-pr-report](https://github.com/domstolene/trivy-pr-report))
4. **Cosign attestation as baseline** — store production SBOM/CVE list as `cosign attest` against the released digest; on PR, `cosign verify-attestation` against the current `:latest` digest, extract predicate, diff in jq. Tamper-evident via Rekor.

**No drop-in OSS GitHub Action** replicates Scout's hosted environment comparison. The cleanest 2026 pattern is the jq-diff approach (option 1), with the production digest pinned via cosign attestation (option 4) for tamper-evidence.

## 5. Sigstore / cosign with `rules_oci`

`rules_oci` ships `cosign_sign` and `cosign_attest` rules ([sign.bzl](https://github.com/bazel-contrib/rules_oci/blob/main/cosign/private/sign.bzl), [attest.bzl](https://github.com/bazel-contrib/rules_oci/blob/main/cosign/private/attest.bzl)) — explicitly **developer preview, not stable API**.

Canonical pattern for sipi's GitHub-hosted CI:

```python
# src/BUILD.bazel
load("@rules_oci//cosign:defs.bzl", "cosign_attest", "cosign_sign")

cosign_sign(
    name = "sign",
    image = ":image",
    repository = "index.docker.io/daschswiss/sipi",
)

cosign_attest(
    name = "attest_sbom",
    image = ":image",
    predicate = ":sbom.spdx.json",
    type = "spdx",
    repository = "index.docker.io/daschswiss/sipi",
)
```

CI invokes `bazel run //src:sign -- --tag v$VERSION` and `bazel run //src:attest_sbom -- --tag v$VERSION`. **Use keyless OIDC** (`COSIGN_EXPERIMENTAL=1`, `id-token: write` permission). No key management, identity baked into Rekor transparency log, simplest for GitHub Actions. KMS-backed only if Docker Hub mirror integrity matters offline (not the case for sipi).

Sources: [Aspect — Announcing rules_oci 1.0](https://blog.aspect.build/rules-oci), [Chainguard — Sign SBOM with cosign](https://edu.chainguard.dev/open-source/sigstore/cosign/how-to-sign-an-sbom-with-cosign/).

**Caveat:** cosign rules being "developer preview" means the API may shift. If stability matters more than build-graph integration, drive cosign from CI shell instead of Bazel. The sipi team's preference for stability suggests CI-shell-driven cosign initially, with a future migration to Bazel rules once stabilized.

## 6. Concrete CI snippets

### `.github/workflows/ci.yml` — replaces Scout `compare` + `cves`

```yaml
- name: Build image (loaded into local docker)
  run: just bazel-docker-build-amd64

- name: Resolve production digest
  id: prod
  run: echo "digest=$(crane digest daschswiss/sipi:latest)" >> $GITHUB_OUTPUT

- name: Trivy scan PR image
  uses: aquasecurity/trivy-action@57a97c7e7821a5776cebc9bb87c984fa69cba8f1   # v0.35.0 — SHA pinned, post-incident hardening
  with:
    image-ref: daschswiss/sipi:latest
    format: sarif
    output: trivy-pr.sarif
    severity: CRITICAL,HIGH
    exit-code: '0'

- name: Trivy scan production image
  uses: aquasecurity/trivy-action@57a97c7e7821a5776cebc9bb87c984fa69cba8f1
  with:
    image-ref: daschswiss/sipi@${{ steps.prod.outputs.digest }}
    format: sarif
    output: trivy-prod.sarif
    severity: CRITICAL,HIGH
    exit-code: '0'

- name: Diff CVEs and post PR comment
  run: |
    jq -r '.runs[0].results[].ruleId' trivy-pr.sarif   | sort -u > pr.txt
    jq -r '.runs[0].results[].ruleId' trivy-prod.sarif | sort -u > prod.txt
    comm -23 pr.txt prod.txt > new-cves.txt
    if [ -s new-cves.txt ]; then
      { echo "### New CRITICAL/HIGH CVEs vs production"; echo '```'; cat new-cves.txt; echo '```'; } > body.md
    else
      echo "No new CRITICAL/HIGH CVEs vs production." > body.md
    fi

- uses: marocchino/sticky-pull-request-comment@v2
  with:
    path: body.md
    header: cve-diff

- name: Upload SARIF to GitHub Security
  uses: github/codeql-action/upload-sarif@v4
  with:
    sarif_file: trivy-pr.sarif
```

### `.github/workflows/publish.yml` — replaces Scout `environment` + `sbom`

```yaml
- name: Generate SBOM (per-arch) with Syft
  uses: anchore/sbom-action@<sha-pinned>   # pin to a current SHA before merge
  with:
    image: daschswiss/sipi:v${{ env.VERSION }}-${{ matrix.arch }}
    format: spdx-json
    output-file: sbom-${{ matrix.arch }}.spdx.json

- name: Sign image (keyless OIDC)
  env:
    COSIGN_EXPERIMENTAL: "1"
  run: just bazel-docker-sign --tag v${{ env.VERSION }}-${{ matrix.arch }}

- name: Attest SBOM
  env:
    COSIGN_EXPERIMENTAL: "1"
  run: |
    just bazel-docker-attest-sbom \
      --predicate sbom-${{ matrix.arch }}.spdx.json \
      --tag v${{ env.VERSION }}-${{ matrix.arch }}

- uses: actions/upload-artifact@v7
  with:
    name: sbom-${{ matrix.arch }}
    path: sbom-${{ matrix.arch }}.spdx.json
```

The `:latest` tag (after multi-arch manifest assembly via `crane index append`) is what the next PR's `crane digest` resolves; this replaces Scout's hosted "environment" inventory with cosign+Rekor-attested digest tracking.

## 7. Recommended sequencing for sipi

**Do not bundle into Y+4.** Y+4 is already load-bearing (Bazel-`rules_oci`-from-Nix-`dockerTools` cutover, multi-arch manifest assembly via `crane`, healthcheck-to-compose move, Sentry path layout change, `--build-id=sha1` linkopt, `STABLE_*` reproducibility plumbing). Adding Scout→OSS swap multiplies the surface area of an already-tight PR.

**Recommended PR sequence (post-launch follow-up):**

| PR | Scope |
|---|---|
| Y+9 — CVE scanning + SARIF | Replace Scout `compare` + `cves` with Trivy (SHA-pinned) + jq-diff. PR comment behaviour preserved. SARIF upload to GitHub Security unchanged. |
| Y+10 — SBOM | Replace Scout `sbom` with Syft via `anchore/sbom-action`. SPDX format preserved. Artifact upload to GitHub unchanged. |
| Y+11 — Cosign signing + attestation | Net-new capability. `cosign_sign` per-arch image. `cosign_attest --type spdx` SBOM bound to image digest. Document Rekor transparency for ops + audit. |

Each PR is independently revertible. Total: ~3 PRs, ~150 lines of CI changes + ~30 lines of `BUILD.bazel` for the cosign rules.

**Hard prerequisite for Y+9–Y+11:** Y+4 must be merged and stable. Scout integration cannot be removed until Bazel-built images are landing in production.

## 8. Bottom-line recommendation

**Swap is recommended, but as a deliberate post-launch follow-up — not bundled into the migration.**

| Concern | Replace Docker Scout with | Source |
|---|---|---|
| CVE scan (PR + release) | `aquasecurity/trivy-action` SHA-pinned `57a97c7…` | [trivy-action](https://github.com/aquasecurity/trivy-action) |
| SARIF → GitHub Security | `github/codeql-action/upload-sarif@v4` (unchanged) | unchanged |
| SBOM generation | `anchore/sbom-action` (Syft) | [syft](https://github.com/anchore/syft) |
| Image signing (NEW) | `rules_oci` `cosign_sign`, keyless OIDC | [rules_oci sign.bzl](https://github.com/bazel-contrib/rules_oci/blob/main/cosign/private/sign.bzl) |
| SBOM attestation (NEW) | `rules_oci` `cosign_attest --type spdx` | [rules_oci attest.bzl](https://github.com/bazel-contrib/rules_oci/blob/main/cosign/private/attest.bzl) |
| PR diff vs production | bash + jq diff between two Trivy SARIFs | self-hosted |
| Build-graph SBOM (optional supplementary) | `@rules_license//tools:write_sbom` | [rules_license](https://deepwiki.com/bazelbuild/rules_license/4-sbom-generation) |

This stack is fully open source, integrates cleanly with `rules_oci`, preserves all six current Scout-driven outputs (PR comment, SARIF→Security tab, per-arch SBOM artifact, production-digest baseline, multi-arch image push, vendored CVE database), and adds Sigstore signing + Rekor transparency that Docker Scout did not provide.

## Citations

- [Trivy advisory GHSA-69fq-xp46-6x23](https://github.com/aquasecurity/trivy/security/advisories/GHSA-69fq-xp46-6x23)
- [Trivy incident discussion #10425](https://github.com/aquasecurity/trivy/discussions/10425)
- [Trivy incident closure #10462](https://github.com/aquasecurity/trivy/discussions/10462)
- [Aqua blog — Trivy supply-chain attack](https://www.aquasec.com/blog/trivy-supply-chain-attack-what-you-need-to-know/)
- [Wiz — Trivy compromised by TeamPCP](https://www.wiz.io/blog/trivy-compromised-teampcp-supply-chain-attack)
- [Microsoft Security — detecting Trivy compromise](https://www.microsoft.com/en-us/security/blog/2026/03/24/detecting-investigating-defending-against-trivy-supply-chain-compromise/)
- [StepSecurity — Trivy v0.69.4 malicious release](https://www.stepsecurity.io/blog/trivy-compromised-a-second-time---malicious-v0-69-4-release)
- [bazel-contrib/rules_oci](https://github.com/bazel-contrib/rules_oci)
- [rules_oci cosign sign.bzl](https://github.com/bazel-contrib/rules_oci/blob/main/cosign/private/sign.bzl)
- [rules_oci cosign attest.bzl](https://github.com/bazel-contrib/rules_oci/blob/main/cosign/private/attest.bzl)
- [Aspect — Announcing rules_oci 1.0](https://blog.aspect.build/rules-oci)
- [Chainguard — Sign SBOM with cosign](https://edu.chainguard.dev/open-source/sigstore/cosign/how-to-sign-an-sbom-with-cosign/)
- [anchore/grype](https://github.com/anchore/grype)
- [anchore/syft](https://github.com/anchore/syft)
- [anchore/sbom-action](https://github.com/anchore/sbom-action)
- [google/osv-scanner container scanning](https://google.github.io/osv-scanner/usage/scan-image)
- [appsecsanta — Trivy vs Grype 2026](https://appsecsanta.com/sca-tools/trivy-vs-grype)
- [Trivy issue #1365 — image diff](https://github.com/aquasecurity/trivy/issues/1365)
- [domstolene/trivy-pr-report](https://github.com/domstolene/trivy-pr-report)
- [reviewdog/action-trivy](https://github.com/reviewdog/action-trivy)
- [rules_license SBOM generation](https://deepwiki.com/bazelbuild/rules_license/4-sbom-generation)
- [marocchino/sticky-pull-request-comment](https://github.com/marocchino/sticky-pull-request-comment)
