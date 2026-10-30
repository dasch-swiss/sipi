---
title: "Advisory scanning and fix PRs over the Bazel dependency graph"
date: 2026-10-30
author: "Ivan Subotic"
status: reviewed
repositories: []
---

# Advisory scanning and fix PRs over the Bazel dependency graph

## Overview

Replace Docker Scout and the Rust-only `dependency-audit` job with one advisory
pipeline whose input is the Bazel dependency graph, not the built image. A script
derives a CycloneDX inventory of everything Vitrinli ships from the files Bazel
resolves against (`Cargo.Bazel.lock`, `MODULE.bazel.lock`, the `http_archive`
pins, `bazel/distroless/bookworm.lock.json`, the `oci.pull` digest). A scanner
matches it against advisory databases on every PR (new findings only), daily on
`main` (one rolling issue), and at release (new findings since the previous
release). Where a fix is a mechanical pin bump, the daily run opens the fix PR.

Scope is advisory-driven only: a PR opens when a known vulnerability hits a
pinned component. Routine "newer version exists" bumps are a non-goal.

## Problem Statement / Motivation

- **Image scanning sees the wrong layer.** Docker Scout scans the distroless
  image. The C/C++ libraries (libtiff, exiv2, lcms2, libpng, OpenSSL, curl,
  zlib, …) and every Rust crate are statically linked into one binary that
  carries no package metadata, so Scout cannot see them; the comment at the
  `publish.yml` CVE gate already says so. It sees only the Debian packages of
  the base image and the `rules_distroless` layer, filtered to Critical/High.
- **Dependabot never understood the graph.** Its `bazel` entry opened none of
  72 Dependabot PRs; its `cargo` entry pointed at `/test/e2e`, which has no
  manifest. It is now removed.
- **Coverage today is Rust crates only.** `just audit` (cargo-audit and
  OSV-Scanner over `Cargo.Bazel.lock`) is the only automated feed. BCR C/C++
  modules, the `http_archive` libraries, FFmpeg, the Debian packages and the
  distroless base rely on a quarterly manual review.
- **A new advisory blocks every PR.** `dependency-audit` fails on any matched
  advisory, so an advisory published against `main` turns every open PR red
  (RUSTSEC-2026-0387 did exactly this).
- **The release gate runs after the push.** The Scout CVE gate in
  `publish.yml` runs after "Push image to Docker Hub", so a failing gate
  reports on an image that is already published.
- **Pins drift silently.** The distroless digest dates from 2026-05-08 and the
  Debian snapshot from 2026-01-01; nothing reports when they carry a fixed
  vulnerability.
- **The image carries a curl stack nobody uses.** `bookworm.yaml` installs
  Debian `curl`, which pulls in 31 libraries (gnutls, krb5, ldap, ssh2, rtmp,
  sasl, libssl3), for an ops-deploy healthcheck that has since moved to
  `sipi health` (ops-deploy and dsp-api both probe with `/sbin/sipi health`).
  It is most of the image's Debian surface.
- **Ignores live in two places** (cargo-audit flags in the `justfile`,
  `[[IgnoredVulns]]` in `osv-scanner.toml`) and never expire.

## Proposed Solution

1. **Inventory from Bazel inputs.** `tools/advisories/inventory.py` emits
   `advisories.cdx.json` (CycloneDX 1.6), one component per shipped
   dependency, sorted for stable diffs:

   | Source | Component identity |
   |---|---|
   | `Cargo.Bazel.lock` | `pkg:cargo/<name>@<version>` |
   | `MODULE.bazel.lock` resolved BCR modules | upstream version (`.bcr.N` stripped) plus the CPE from the component map |
   | `http_archive` pins (exiv2, jansson, mimalloc, libexpat, jbigkit, lcms2, tiff, FFmpeg) | version and CPE from the component map |
   | `bazel/distroless/bookworm.lock.json` (both arches) | `pkg:deb/debian/<name>@<version>?arch=<arch>&distro=<distro>`, distro derived from `bookworm.yaml` |
   | `oci.pull` `distroless_base` digest | the image reference by digest, scanned without a build |

   The **component map** `tools/advisories/components.toml` is the one
   hand-maintained classification. Every BCR module, `http_archive` and custom
   repository rule appears exactly once, as `shipped` (with CPE; for
   `http_archive`, the pinned version), `not_shipped` (build tooling, dev-only,
   test-only; with reason) or `no_feed` (Kakadu: proprietary, no public
   advisory feed; with reason). The **drift check** fails on: an unclassified
   module, `http_archive` or custom repository rule; an `http_archive` URL that does not contain the mapped
   version; a map entry for a dependency no longer in MODULE.bazel; a duplicate
   entry. Adding a dependency without classifying it breaks CI.

2. **Scan.** `just advisories-scan` runs the scanner(s) chosen in Phase 3 over
   the inventory and the base image digest, **without the scanners' own ignore
   mechanisms and with JSON output**. Vitrinli's scripts then apply one
   scanner-agnostic ignore file, `tools/advisories/ignores.toml` (advisory id,
   component, reason, expiry date; an expired entry is a finding), and decide
   pass or fail themselves. That keeps one ignore list whichever scanner wins,
   and enforces expiry, which Grype's ignore rules cannot express.
   cargo-audit and `osv-scanner.toml` are dropped.

3. **Where it runs.**
   - **PR** (`advisories` job, replaces `dependency-audit`): scans the PR merge
     commit and the current `origin/main` tip without ignores (the head's tools
     run against a base worktree; base-side drift is tolerated), takes head minus
     base keyed by `(component name, advisory id)` (never by version), then
     applies `ignores.toml` from the head once. Fails on a non-empty result, on
     any scanner error, and on a drift-check failure. A PR that fixes a finding
     passes; an advisory published against `main` does not block unrelated PRs.
   - **Daily on `main`** (`.github/workflows/advisories.yml`): full scan, one
     rolling issue.
   - **Release** (`advisories-gate` job in `publish.yml`, before any push, once
     per release): the same diff, between the previous release tag and this
     tag (head tools against the tag's worktree; tags before this work have no
     `tools/advisories/`), so a release never introduces a vulnerable pin, and a backlog finding
     never blocks a hotfix. The first release without a previous tag runs a
     full scan. Uploads the inventory as the release SBOM.

4. **Fix PRs** from the daily run, opened with `DASCHBOT_PAT` so CI runs on
   them. One branch per component, `advisories/<component>`, one commit
   `fix(deps): raise <component> to <resolved> past <ids>` whose body names
   every advisory id and the resolved version.
   - Rust crate: precise repin
     (`CARGO_BAZEL_REPIN=<crate>@<locked>=<target>`, `target` the lowest
     version at or above the fix that the locked requirement allows), re-scan;
     a branch only if every advisory for the crate clears. A crate fixable only
     by a semver-major bump of a dependent is alert-only with the reason "needs
     a major bump of <nearest crate.spec dependent>", found by walking
     `Cargo.Bazel.lock` reverse dependencies.
   - BCR module: lowest non-yanked BCR version at or above the fix, compared
     with Bazel's module version ordering (not semver), lock refreshed.
   - Distroless base: current index digest of
     `gcr.io/distroless/base-debian12:latest` (the tag the pin tracks).
   - Debian packages: snapshot date moved to the scan date, lock regenerated.
   - `http_archive` libraries, FFmpeg, Kakadu, and findings with no released
     fix: **alert only**. ADR-0015 makes every native bump a re-derivation of
     `defines`, and exiv2 carries a patch; a URL and sha256 swap is not a safe
     proposal.

   Lifecycle: a fix branch is always rebuilt from `origin/main`, never rebased.
   An open PR whose branch is behind (`git merge-base --is-ancestor origin/main
   <branch>` fails) is rebuilt and pushed with `--force-with-lease`. A branch
   without a PR is deleted and rebuilt. A fix PR closed unmerged suppresses a
   new PR while the component's advisory set is unchanged; the issue then asks
   for an ignore entry. A concurrency group serializes runs.

Docker Scout leaves `ci.yml` and `publish.yml`, with the PR-comment and SARIF
permissions and the Scout Docker Hub organisation/environment use. The Docker
Hub credentials stay; the push needs them.

## Alternative Approaches Considered

- **Self-hosted Renovate** (`bazel-module` manager). Bumps `bazel_dep`,
  `oci.pull` and direct `crate.spec`, but its advisory mode
  (`osvVulnerabilityAlerts`, experimental) covers direct dependencies only.
  Every crate advisory Vitrinli has hit (rsa, crypto-bigint, block-buffer,
  rustls-webpki, quinn-proto) was transitive. It does not parse `http_archive`
  in MODULE.bazel, cannot recompute sha256s, and needs a GitHub App. It solves
  routine currency, a non-goal. Rejected; the option if routine bumps come
  into scope.
- **GitHub dependency submission API.** Submitted SBOMs raise Dependabot alerts
  only for GitHub Advisory Database ecosystems: crates yes, `pkg:generic` C
  libraries and BCR modules no, and no fix PRs for a graph Dependabot cannot
  edit. Rejected.
- **Bazel-native SBOM rules** (`rules_license` `write_sbom`,
  `eclipse-score/sbom-tool`). Young or experimental, and they still need the
  same name-to-CPE map for C libraries. Rejected for a script over the
  lockfiles.
- **Image scanning with Trivy and Syft** (the replacement direction recorded
  for Scout during the Bazel migration). Same blind spot as Scout: the image
  carries no metadata for the statically linked code. Rejected; see Prior
  decisions.
- **Scanning the linked binary** (`cve-bin-tool`). Fingerprints version
  strings, misses lcms2, Kakadu and backports, and finds nothing the inventory
  does not. The drift check covers the pin side. Rejected.

## Prior decisions

This plan reverses three recorded decisions.

- **Security hardening wave 2, finding S2-30 (Phase 9).** Kept Docker Scout as
  the image-layer gate and made the release scan fail on Critical, "scoped to
  image layers only", because "the only CVE gate in CI (Docker Scout) scans
  image layers and cannot see the statically linked C libraries" and "does not
  fail the build". The reason for keeping Scout was that nothing else saw the
  Debian layers. What changed: the inventory reads those layers from their
  Bazel inputs (`bookworm.lock.json`, the base digest), so Scout's only unique
  coverage moves into the pipeline, and the release gate moves before the push.
- **Security hardening wave 2, finding S2-41.** Pinned third-party actions by
  commit SHA because "Dependabot keeps them fresh". Dependabot is removed, so
  the freshness half no longer holds. SHA pins keep their protection against
  tag hijacking; freshness of action pins becomes a routine-currency concern,
  a non-goal here, and an advisory against a pinned action version is not
  covered (accepted gap).
- **Bazel migration plan, Future Considerations Y+9 and Y+10.** Planned to swap
  Scout for Trivy (image CVE scan, SARIF diff against production) and Syft
  (image SBOM), because Trivy was "the cleanest single-tool replacement for
  Scout's CVE scanning" and "no production-grade Bazel rule emits a
  container-image SBOM into the build graph". What changed: the target is no
  longer the image. Trivy and Syft inherit Scout's blind spot for statically
  linked code, while the lockfiles carry exact versions for everything. Y+11
  (cosign image signing and attestation) is untouched and stays a separate
  follow-up; it can attest the inventory produced here.

ADR-0029 records these reversals so they outlive this plan.

## Technical Considerations

- **Scanner choice is decided by evidence in Phase 3.** Known before the spike:
  OSV-Scanner reads CycloneDX by filename (`*.cdx.json`), matches purls, keeps
  CPE-only components but does not match them, and scans images only from an
  archive (the base digest would need `crane pull` to a tarball first); exit
  codes are 0 clean, 1 findings, 127+ error. Grype reads CycloneDX, matches
  CPE-only components by default (`match.stock.using-cpes: true`), scans
  `registry:<image>@<digest>` without a daemon, needs `--distro debian:12` (or
  a parsed purl `distro` qualifier) for deb matching, and its ignore rules have
  no expiry. Unverified and settled by the fixture: CycloneDX 1.6 parsing, the
  exact deb qualifier each scanner honours, Grype's exit codes. Rule: one
  scanner if it finds the fixture's vulnerable component in all four classes
  (cargo, deb, CPE C library, base image); otherwise OSV-Scanner for cargo and
  deb plus Grype for CPE and the base image, never more than two. If no
  candidate matches CPE C libraries, that class stays alert-only through the
  pinned-version-and-age table and the ADR records it.
- **Scanner invocation.** Each scanner runs with JSON output, without
  `--fail-on` and without its own ignore config. The wrapper interprets exit
  codes per scanner (OSV-Scanner: 0 and 1 are results, anything else is an
  error; Grype: non-zero is an error) and reads findings from the JSON.
- **Tool conventions** (`tools/advisories/`). Python 3.11 or newer (for
  `tomllib`), stdlib only, run as `python3 tools/advisories/<tool>.py`, like
  `tools/loadtest/loadgen.py`. Shared dataclasses and the version check in
  `_model.py`. `argparse`, `main(argv) -> int`. Exit codes: 0 clean, 1 findings
  or drift, 2 usage, 3 tool or scanner error; exceptions are caught only in
  `main`, mapped to 3, diagnostics to stderr, so a crash never reads as
  findings. TOML and JSON through `tomllib` and `json`; MODULE.bazel
  (Starlark) through tested regexes that exit 3 on no match. Output sorted. The
  inventory fails when it emits fewer components than a sanity floor. Tests in
  `tools/advisories/tests/test_*.py`, offline, fixtures under
  `tools/advisories/tests/fixtures/`, run by
  `python3 -m unittest discover -s tools/advisories/tests -t tools/advisories`.
- **CPE results are weak evidence.** NVD CPE ranges are noisy for C libraries
  and ignore distro backports. Expect false positives; triage is an expiring
  ignore. "No finding" for a C library is weaker than for a crate; the docs say
  so. A CPE miss produces no alert at all, so the rolling issue also lists
  every alert-only component with its pinned version and pin age, which
  replaces the quarterly review as the human glance.
- **BCR versions** carry `.bcr.N` (`curl 8.21.0.bcr.1`); the inventory strips
  it for matching. A BCR repack that backports a fix is an ignore with that
  reason. BCR candidates come from
  `https://raw.githubusercontent.com/bazelbuild/bazel-central-registry/main/modules/<name>/metadata.json`
  (`versions`, `yanked_versions`).
- **libtiff** comes from the libsdl-org mirror; its CPE stays
  `cpe:2.3:a:libtiff:libtiff`. **libexpat** is an `http_archive`, not the BCR
  module `CLAUDE.md` lists.
- **Crate repin and the lockfile.** `CARGO_BAZEL_REPIN` maps to cargo's
  update: `<crate>` updates that package, `<crate>@<locked>=<target>` pins it
  precisely (needed when a crate is locked at two versions). The repin is
  `bazelisk fetch @crates//:all` with the variable set, followed by the same
  fetch without it: the crate extension records `CARGO_BAZEL_REPIN` in
  `MODULE.bazel.lock`, and only the second fetch writes it back as unset.
  `.bazelrc` sets no `lockfile_mode`, so the default `update` applies.
- **Kakadu stays out of fix jobs.** `@crates`, `@bookworm` and `bazel mod deps`
  do not touch the Kakadu extension; Phases 8 and 9 verify each fix recipe runs
  with `GH_TOKEN` unset.
- **No `PATH` change in Bazel steps.** `action_env=PATH` puts `PATH` into every
  action key, so a changed `PATH` is a full remote-cache rebuild. Scans run in
  jobs without Bazel; the fix job downloads scanners into
  `$RUNNER_TEMP/advisories-bin` and calls them by absolute path.
- **Pinned scanner installs.** Each scanner binary is downloaded by exact
  version and verified against a sha256 recorded in the workflow, as the
  current `osv-scanner` install does.
- **`just bazel-*` invariant.** `advisories-*` recipes read files and run
  scanners; they never invoke Bazel. Recipes that do are named `bazel-*`
  (`bazel-distroless-lock`, `bazel-repin-crate`, `bazel-lock-update`).
- **GitHub mechanics.** Scheduled and dispatched runs only exist once the
  workflow file is on the default branch; pre-merge verification therefore
  runs the same Python entry points locally. Pushes and PRs made with
  `GITHUB_TOKEN` trigger no CI, so fix PRs use `DASCHBOT_PAT`. The previous
  release tag is the next entry after the current tag in
  `git tag --list 'v*' --sort=-v:refname`. The PR job checks out with
  `fetch-depth: 0` and diffs against a worktree of `origin/main`.
- **Diffing against a base without the tooling.** `origin/main` before this
  work lands, and every release tag before it, have no `tools/advisories/`. The
  diff therefore runs the head's `inventory.py`, `components.toml` and scanner
  wrapper against the base worktree's lockfiles, in `--best-effort` mode: base
  drift is tolerated, head drift still fails.
- **Permissions.** Daily scan: `contents: read`, `issues: write`, `pull-requests: read` (open and declined fix PRs in the issue). Fix job:
  `contents: read`; pushes and PRs go through `DASCHBOT_PAT`.
- **Rolling issue.** Found by `gh issue list --label advisory --state open`
  with the fixed title "Open dependency advisories"; created when absent,
  closed when the scan is clean, a new one opened next time. The body carries
  the advisory-database date, ignores expiring within 14 days, and the
  alert-only table, capped at 60 000 characters with a link to the run summary
  for the rest. A scanner or network error fails the job and leaves the issue
  untouched; only a successful clean scan closes it. `advisories.yml` creates
  the `advisory` label if missing.
- **Schedules.** `fuzz.yml` runs at 03:17 UTC; `advisories.yml` uses 05:41 UTC.
- **Notification is a GitHub issue,** not Linear: no API key secret needed.
- **Specs are a sink.** ADR-0029, the scripts, docs and comments reference
  code, the ADR and the glossary, never this plan.
- **Non-goals:** GitHub Actions pins, Nix flake inputs (dev shell only), mkdocs
  and benchmark Python requirements (not shipped), toolchain modules, routine
  version bumps.

## Implementation Phases

#### Phase 1: Drop the Debian curl from the image

- [ ] Add `just bazel-distroless-lock` (`bazel run @bookworm//:lock`)
- [ ] Remove `curl` from `packages:` in `bazel/distroless/bookworm.yaml` and run `just bazel-distroless-lock` with `GH_TOKEN` unset; the amd64 and arm64 package sets in `bazel/distroless/bookworm.lock.json` contain no `curl`, `libcurl4`, `libssl3` or `libgnutls30`; record the amd64 and arm64 package counts in the PR description
- [ ] Rewrite the comments that name the image's Debian curl as the ops-deploy healthcheck probe (`bazel/distroless/bookworm.yaml`, the `apt` section of `MODULE.bazel`, the runtime-extras block in `src/BUILD.bazel`): the container healthcheck is `sipi health`; the BCR curl statically linked into sipi is unrelated and stays
- [ ] Run `just bazel-build` and `just bazel-coverage` on macOS; both pass
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 2: Inventory from Bazel inputs

- [ ] Add `tools/advisories/components.toml` classifying every resolved BCR module and every `http_archive` and custom repository rule in MODULE.bazel as `shipped` (with CPE), `not_shipped` or `no_feed` (with reason)
- [ ] Add `tools/advisories/_model.py` (Python version check, `Component` dataclass) and `tools/advisories/inventory.py` emitting a sorted `advisories.cdx.json` from `Cargo.Bazel.lock`, `MODULE.bazel.lock`, `MODULE.bazel` (`http_archive` pins, `oci.pull` digest), the component map and `bazel/distroless/bookworm.lock.json`, with the deb distro qualifier derived from `bazel/distroless/bookworm.yaml`
- [ ] Implement the drift check in `inventory.py` (with a `--best-effort` flag that skips unclassified entries instead of failing, for scanning old bases): unclassified module, `http_archive` or custom repository rule, URL missing the mapped version, stale map entry, duplicate entry; plus the component-count sanity floor
- [ ] Add `tools/advisories/tests/__init__.py` and `tools/advisories/tests/test_inventory.py` with fixtures under `tools/advisories/tests/fixtures/`: one test per drift case, the `.bcr.N` strip, the exact deb PURL with the derived distro, golden CycloneDX compared as parsed JSON
- [ ] Add `just advisories-inventory` and `just advisories-test`; gitignore `advisories.cdx.json`
- [ ] Correct `CLAUDE.md`'s dependency list: libexpat is an `http_archive`, not a BCR module
- [ ] Run `just advisories-test`; it passes. Run `just advisories-inventory`; it emits a component for every `shipped` map entry, every crate in `Cargo.Bazel.lock`, every package (both arches) in `bookworm.lock.json` and the base-image digest
- [ ] Run `just bazel-build` and `just bazel-coverage` on macOS; both pass (justfile changed)
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 3: Scanner choice and ADR

- [ ] Add a fixture inventory `tools/advisories/tests/fixtures/spike.cdx.json` with one known-vulnerable component per class: an old crate, an old deb with the derived distro qualifier, libtiff and curl at versions with published CVEs; and record an older `distroless/base-debian12` digest for the base-image class
- [ ] Download pinned OSV-Scanner and Grype release binaries (version and sha256 checked) into a scratch directory outside the repo, and run them against the fixture and the old base digest (network required; spike evidence, not part of `just advisories-test`); record per class which scanner reports the expected advisory, the deb qualifier each honours, and Grype's exit codes; apply the decision rule
- [ ] Write `docs/adr/0029-advisory-scanning-from-the-bazel-graph.md` (re-check the number at write time): inventory over image, the chosen scanner(s) with the per-class fixture results, the alert-only classes, the reversals named in Prior decisions with their original reasons and what changed, the rejected alternatives
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 4: Scan recipe and ignore consolidation

- [ ] Add `tools/advisories/scan.py`: runs the chosen scanner(s) without ignores and with JSON output, interprets exit codes per scanner, normalizes findings to `(component name, advisory id, aliases, severity, fixed version)`, applies `ignores.toml`
- [ ] Add `tools/advisories/ignores.toml` with the four current entries (RUSTSEC-2023-0071, GHSA-w9wp-h8wv-79jx, RUSTSEC-2026-0387, GHSA-qwgh-2vcv-g2f7), each with component, reason and expiry date
- [ ] Add `tools/advisories/tests/test_scan.py`: normalization from recorded scanner JSON fixtures, alias matching, an expired ignore becomes a finding, ignore-file lint (non-empty reason, expiry date present), scanner exit codes per scanner (OSV-Scanner 0 and 1 are results, anything else exits 3; Grype non-zero exits 3)
- [ ] Add `just advisories-scan` (inventory, then `scan.py`); exit codes 0, 1 or 3 as in Technical Considerations
- [ ] In `flake.nix`, add the chosen scanner(s) to the dev shell, so `just advisories-scan` runs locally
- [ ] Run `just advisories-test` and `just advisories-scan`; the test passes and the scan reports the baseline. Put the per-class finding counts in the PR description
- [ ] Run `just bazel-build` and `just bazel-coverage` on macOS; both pass
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 5: PR gate

- [ ] Add `tools/advisories/diff.py` and `just advisories-scan-diff <base-ref>`: runs the head's inventory, map and scanner wrapper against `<base-ref>` in a temporary worktree and against the working tree, without ignores (head-side drift exits 1; base-side drift is tolerated through an `inventory.py --best-effort` mode that skips unclassified entries); keys findings by `(component name, advisory id)`, applies the working tree's `ignores.toml` to head minus base; exit 1 on a non-empty result, 3 on a scanner error
- [ ] Add `tools/advisories/tests/test_diff.py`: shared finding passes, head-only finding fails, finding fixed at head passes, version bump of an already-vulnerable component passes, ignored head-only finding passes, a base without `tools/advisories/` is scanned best-effort, head drift exits 1 while base drift does not, a scanner error on either side exits 3
- [ ] Replace the `dependency-audit` job in `.github/workflows/ci.yml` with an `advisories` job (`fetch-depth: 0`, Python 3.11 via the existing `setup-python` composite action, scanners pinned by version and sha256) running `just advisories-test` and `just advisories-scan-diff origin/main`
- [ ] Remove `just audit`, its justfile comments and the cargo-audit flags; delete `osv-scanner.toml`
- [ ] In `flake.nix`, remove `cargo-audit`, and `osv-scanner` if it was not chosen, from the dev shell
- [ ] Point the `cargo_lockfile` comment in `MODULE.bazel` at `just advisories-scan` instead of cargo-audit
- [ ] Rewrite "Dependency advisory scanning" in `docs/src/development/ci.md`: the pipeline, how to classify a component in `components.toml`, how to write an ignore with expiry, how to run the scan locally, that a clean CPE result is weaker evidence than a clean crate result; replace every mention of `dependency-audit`, `just audit` and cargo-audit
- [ ] Run `just bazel-build` and `just bazel-coverage` on macOS; both pass
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 6: Remove Docker Scout and gate releases

- [ ] Add an `advisories-gate` job to `.github/workflows/publish.yml` (`fetch-depth: 0`, Python 3.11 via `setup-python`, scanners pinned by version and sha256), needed by `publish-docker`, running once per release: resolve the previous tag from `git tag --list 'v*' --sort=-v:refname`, run `just advisories-scan-diff <previous tag>` (full `just advisories-scan` when there is none), upload `advisories.cdx.json` as the release SBOM artifact
- [ ] Delete the Scout `environment`, `cves` and `sbom` steps, the SBOM upload and the matrix `sbom_file`/`sbom_name` keys from `publish.yml`
- [ ] Delete both Scout steps, the SARIF upload, the `pull-requests: write` and `security-events: write` permissions and the Scout comments from `.github/workflows/ci.yml`
- [ ] Remove the Scout notes from `justfile` comments (image-in-daemon note, version-based steps list)
- [ ] Remove the "Docker Scout warm-cache fix" section and the Scout steps from `docs/src/development/rbe.md`
- [ ] Replace every Scout mention in `docs/src/development/ci.md` (test-matrix list, publish gate model, coverage-gap paragraph); state that the release SBOM is one CycloneDX file per release covering both arches
- [ ] Run `grep -rni scout .github justfile docs/src`; it returns nothing
- [ ] Run `just bazel-build` and `just bazel-coverage` on macOS; both pass (justfile changed)
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 7: Daily scan and rolling issue

- [ ] Add `tools/advisories/report.py` rendering the issue body: findings (component, advisory id, severity, fixed version, fix path), the advisory-database date, ignores expiring within 14 days, the alert-only table with pinned version and pin age; capped at 60 000 characters
- [ ] Add `tools/advisories/tests/test_report.py` over recorded scan fixtures, including the cap
- [ ] Add `.github/workflows/advisories.yml`: cron `41 5 * * *`, `workflow_dispatch` with a `dry_run` input, concurrency group `advisories` (no cancel-in-progress), `permissions: contents: read, issues: write, pull-requests: read`, Python 3.11 via `setup-python`, scanners pinned by version and sha256; runs `just advisories-scan` and `report.py`; finds the issue by the `advisory` label and the fixed title, creates the label if missing, creates, updates or closes the issue; `dry_run` writes the body to the job summary instead
- [ ] Update the scheduling note in the `fuzz.yml` header to name both scheduled workflows and their times
- [ ] Run `just advisories-scan` and `python3 tools/advisories/report.py` locally on the scan output; the body renders with the baseline findings
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 8: Fix PRs for Rust crates

- [ ] Add `just bazel-repin-crate <spec>`: `CARGO_BAZEL_REPIN=<spec> bazelisk fetch @crates//:all`, then the same fetch without the variable
- [ ] Add `tools/advisories/fix.py` crate path: per component, choose the target version, repin with `<crate>@<locked>=<target>`, re-scan, keep the change only when every advisory for it clears; otherwise classify alert-only (no released fix, or "needs a major bump of <dependent>" via `Cargo.Bazel.lock` reverse dependencies)
- [ ] Implement the branch lifecycle in `fix.py`: `advisories/<component>` naming, rebuild from `origin/main`, `--force-with-lease` for a behind branch with an open PR, recreate a branch without a PR, suppress a new PR after an unmerged close while the advisory set is unchanged
- [ ] Add a `fix` job to `advisories.yml` (after the scan, checkout with `fetch-depth: 0`, `ci-setup`, `GH_TOKEN: secrets.DASCHBOT_PAT` for pushes and PRs, scanners in `$RUNNER_TEMP/advisories-bin`) that pushes and opens PRs, one commit each per the Proposed Solution; `dry_run` prints branches, titles and bodies instead
- [ ] Show open fix PRs and declined ones in the rolling issue; the issue update runs after the fix job, so PRs opened in a run appear in that run's issue
- [ ] Add `tools/advisories/tests/test_fix.py` covering target-version choice, naming, title, commit body, the lifecycle decisions and the reverse-dependency walk; extend `test_report.py` for the open and declined fix-PR section; run `just advisories-test`; it passes
- [ ] Run `just bazel-repin-crate tokio` on macOS with `GH_TOKEN` unset; only `Cargo.Bazel.lock` and `MODULE.bazel.lock` change and the lock records `CARGO_BAZEL_REPIN` as unset; revert it
- [ ] Run `just bazel-build` and `just bazel-coverage` on macOS; both pass
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 9: Fix PRs for BCR modules, the distroless base and Debian packages

- [ ] Add `just bazel-lock-update` (`bazel mod deps` after a `bazel_dep` change)
- [ ] Extend `fix.py` for BCR findings: lowest non-yanked version at or above the fix from the registry's `metadata.json`, compared with Bazel's module version ordering, edit the `bazel_dep`, refresh the lock
- [ ] Extend `fix.py` for base-image findings: current index digest of `gcr.io/distroless/base-debian12:latest` (`crane digest`), replaced in `oci.pull` with the capture date in its comment
- [ ] Extend `fix.py` for deb findings: snapshot date in `bazel/distroless/bookworm.yaml` moved to the scan date, lock regenerated
- [ ] Unit-test BCR version ordering and selection (including `.bcr.N` and yanked versions) and the digest and snapshot edits in `test_fix.py`; run `just advisories-test`; it passes
- [ ] Run `just bazel-lock-update` and `just bazel-distroless-lock` on macOS with `GH_TOKEN` unset; both complete; revert any change
- [ ] Replace "Quarterly native-pin review" in `docs/src/development/ci.md` with the alert-only workflow for `http_archive` libraries, FFmpeg and Kakadu
- [ ] Name `tools/advisories/components.toml` in `CLAUDE.md`'s dependency section as the classification of record
- [ ] Run `just bazel-build` and `just bazel-coverage` on macOS; both pass
- [ ] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

## Human Actions

| Id | Action | Who | When | Why not the agent |
|----|--------|-----|------|-------------------|
| H1 | Confirm `DASCHBOT_PAT` may push `advisories/*` branches and open PRs under the current rulesets | Maintainer | before start | Token scopes and rulesets are not readable by the agent |
| H2 | After merge, run `advisories.yml` once with `dry_run`, then once without; confirm the issue appears, fix PRs open, and a fix PR's CI (including `advisories`) runs and passes | Maintainer | after ship | The workflow runs only from the default branch and mutates shared state |
| H3 | Triage the baseline findings in the rolling issue: fix PRs merged or ignores with expiry | Maintainer | after ship | Risk acceptance is the owner's decision |
| H4 | Make the `advisories` PR check required in the `main` ruleset | Maintainer | after ship | Repository ruleset is maintainer territory |
| H5 | Remove the `daschswiss` Docker Hub organisation's Scout integration and its `production` environment | Maintainer | after ship | Docker Hub organisation settings |

## Acceptance Criteria

- [ ] `bookworm.lock.json` (both arches) contains no `curl`, `libcurl4`, `libssl3` or `libgnutls30`; image comments name `sipi health` as the healthcheck
- [ ] Every resolved BCR module and every `http_archive` / custom repository rule is classified once in `components.toml`; each drift case fails `just advisories-inventory` (covered by tests)
- [ ] The inventory contains every crate in `Cargo.Bazel.lock`, every package of both arches in `bookworm.lock.json`, and every `shipped` map entry
- [ ] The chosen scanner(s) report the fixture's vulnerable component in each class, or the ADR records which class stays alert-only
- [ ] A scanner error exits 3 and never reads as a finding or a clean scan (covered by tests)
- [ ] `just advisories-scan-diff` passes when base and head share a finding and fails on a head-only finding (covered by tests)
- [ ] An expired ignore entry surfaces as a finding (covered by tests)
- [ ] `just audit`, `osv-scanner.toml` and the cargo-audit flags are gone; `ignores.toml` carries the four entries, each with reason and expiry
- [ ] The `advisories` PR job replaces `dependency-audit`
- [ ] The daily workflow keeps one rolling issue, capped at 60 000 characters; `dry_run` writes the body to the job summary
- [ ] The baseline per-class finding counts are in the Phase 4 PR description
- [ ] The release gate runs before any push, once per release, and publishes the CycloneDX SBOM
- [ ] `grep -rni scout .github justfile docs/src` returns nothing
- [ ] Fix-PR target choice, naming, titles, commit bodies and lifecycle decisions are covered by `test_fix.py`
- [ ] Each `bazel-*` fix recipe runs with `GH_TOKEN` unset
- [ ] ADR-0029 names the reversed decisions with their original reasons
- [ ] `just advisories-test`, `just bazel-build` and `just bazel-coverage` pass

## Dependencies & Risks

- **Depends on** the `fix/advisories` branch (tokio bump, current ignores, Dependabot removal) merging first.
- **CPE false positives** may make the issue noisy at first; triage with expiring ignores (H3).
- **A release diff misses a backlog finding** by design; the daily issue owns the backlog, and H3 drains it.
- **BCR fix PRs can hit MVS conflicts** (curl and OpenSSL needed a `single_version_override`) or break the build (libexpat's config header); CI on the PR catches it and the PR waits for a person.
- **Distroless bumps** have broken DNS when `/etc/nsswitch.conf` changed; the smoke test gates those PRs.
- **A CPE miss is silent** for `http_archive` libraries and FFmpeg; the pinned-version-and-age table in the issue is the remaining human signal.
- **Action pins lose freshness** with Dependabot gone (S2-41); accepted as a routine-currency non-goal.
- **The fix job runs only on Linux;** local verification is macOS. H2 covers the first real Linux run.

## Success Metrics

- Debian packages in the image: from 33 (amd64) to `tini` and its glibc dependencies (counts recorded in the Phase 1 PR).
- Classified components: from Rust crates only (449 in `Cargo.Bazel.lock`) to every shipped component class, zero unclassified.
- Baseline per-class finding counts recorded in the Phase 4 PR; the rolling issue reaches zero unignored findings after H3.
- An advisory against `main` no longer turns unrelated PRs red (today every open PR failed `dependency-audit` on RUSTSEC-2026-0387).
- A fixable crate advisory has a fix PR within one daily run of reaching the advisory database.

## References

- Current scan: `justfile` `audit` recipe; `.github/workflows/ci.yml` `dependency-audit` job; `osv-scanner.toml`
- Scout steps: `.github/workflows/ci.yml` (`test` job, linux-amd64 PR leg); `.github/workflows/publish.yml` (`publish-docker` job)
- Dependency surfaces: `MODULE.bazel` (`bazel_dep`s, `http_archive` pins, `crate.spec` / `crate.from_specs`, `oci.pull`); `bazel/kakadu_extension.bzl`; `bazel/distroless/bookworm.yaml`, `bookworm.lock.json`
- Stdlib tool precedent: `tools/loadtest/loadgen.py`
- [ADR-0015 native cc_library over foreign_cc](../adr/0015-native-cc_library-over-foreign_cc.md)
- crate_universe `CARGO_BAZEL_REPIN` values: https://raw.githubusercontent.com/bazelbuild/rules_rust/0.74.0/crate_universe/private/crates_repository.bzl
- Bazel lockfile modes: https://bazel.build/external/lockfile
- Renovate `bazel-module` manager: https://docs.renovatebot.com/modules/manager/bazel-module/
- Renovate `osvVulnerabilityAlerts`: https://docs.renovatebot.com/configuration-options/#osvvulnerabilityalerts
- OSV-Scanner configuration, output and exit codes: https://google.github.io/osv-scanner/configuration/, https://google.github.io/osv-scanner/output/
- OSV-Scanner image scanning: https://google.github.io/osv-scanner/usage/scan-image
- Grype configuration (CPE matching, ignore rules): https://oss.anchore.com/docs/reference/grype/configuration/
- Grype scan targets: https://oss.anchore.com/docs/guides/vulnerability/scan-targets/
- GitHub workflow triggers and `GITHUB_TOKEN`: https://docs.github.com/en/actions/using-workflows/triggering-a-workflow
- GitHub dependency submission API: https://docs.github.com/en/code-security/supply-chain-security/understanding-your-software-supply-chain/using-the-dependency-submission-api
- Dependabot Bazel support: https://blog.bazel.build/2026/02/20/dependabot.html
- Learning: `dasch-specs/learnings/best-practices/sipi-nix-to-bazel-migration-lessons.md` (distroless nsswitch, CI cache behaviour)
