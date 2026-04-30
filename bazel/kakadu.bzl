"""Custom repository_rule for fetching Kakadu (proprietary, GitHub-release-gated).

Mirrors the Nix FOD pattern in `flake.nix:36-78` 1:1: shells out to `gh release
download` so the auth UX is identical (local: `gh auth login`; CI:
`GH_TOKEN=$DASCHBOT_PAT`). Plain `http_archive` + `auth_patterns` is rejected
because `auth_patterns` consumes `~/.netrc`, which `gh` does not populate;
preserving today's `gh auth token` flow avoids out-of-band `~/.netrc` plumbing.

This rule replaces the FOD on the Bazel side. Nix flake's FOD remains
authoritative until DEV-6348 (Y+6) deletes `package.nix`.
"""

def _kakadu_archive_impl(ctx):
    gh = ctx.which("gh")
    if not gh:
        fail(
            "kakadu_archive: `gh` not found on PATH.\n" +
            "  - Local dev: `gh` is declared in `flake.nix` devShells.\n" +
            "  - CI: ensure the Bazel job runs inside `nix develop`.",
        )

    token = ctx.os.environ.get("GH_TOKEN") or ctx.os.environ.get("GITHUB_TOKEN")
    if not token:
        # Local dev fallback: read token from `gh`'s stored credentials.
        result = ctx.execute([gh, "auth", "token"])
        if result.return_code != 0:
            fail(
                "kakadu_archive: no GH_TOKEN/GITHUB_TOKEN in env and " +
                "`gh auth token` failed: " + result.stderr +
                "\n  - Local dev: run `gh auth login` once.\n" +
                "  - CI: set `env: GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` on the job.",
            )
        token = result.stdout.strip()

    raw_archive = "kakadu.raw.zip"
    download_result = ctx.execute(
        [
            gh,
            "release",
            "download",
            ctx.attr.tag,
            "--repo",
            ctx.attr.repo,
            "--pattern",
            ctx.attr.asset,
            "--output",
            raw_archive,
        ],
        environment = {"GH_TOKEN": token},
    )
    if download_result.return_code != 0:
        fail("kakadu_archive: `gh release download` failed: " + download_result.stderr)

    # `ctx.extract` does not enforce sha256, but `ctx.download` does — fetch via
    # gh first, then verify the downloaded blob's hash matches before extracting.
    # `ctx.download` with a `file://` URL is the canonical way to invoke the
    # checksum machinery on a local file. The output filename has to keep the
    # `.zip` suffix because `ctx.extract` infers archive type from the extension
    # (`.verified` and similar synthetic suffixes get rejected outright).
    raw_archive_path = ctx.path(raw_archive)
    archive = "kakadu.zip"
    ctx.download(
        url = "file://" + str(raw_archive_path),
        output = archive,
        sha256 = ctx.attr.sha256,
    )
    ctx.extract(archive, stripPrefix = ctx.attr.strip_prefix)

    # Bazel's `ctx.patch` requires hunks to match line numbers exactly — it
    # does not implement GNU patch's fuzz tolerance. The Linux Makefile
    # patches were generated with a couple of lines of drift relative to
    # the v8_5-01382N tarball, so `ctx.patch` rejects them at hunk #2 with
    # `CONTENT_DOES_NOT_MATCH_TARGET`. Shelling out to system `patch` (which
    # the dev shell exposes via its standard build-tool set) restores the
    # fuzz behaviour and matches what the existing Nix FOD does.
    if ctx.attr.patches:
        patch = ctx.which("patch")
        if not patch:
            fail("kakadu_archive: `patch` not found on PATH (needed for Linux Makefile patches).")
        for patch_label in ctx.attr.patches:
            patch_path = ctx.path(patch_label)
            patch_result = ctx.execute(
                [patch, "-p" + str(ctx.attr.patch_strip), "-i", str(patch_path)],
            )
            if patch_result.return_code != 0:
                fail(
                    "kakadu_archive: `patch -p%d -i %s` failed: %s" %
                    (ctx.attr.patch_strip, str(patch_path), patch_result.stderr),
                )

    ctx.symlink(ctx.attr.build_file, "BUILD.bazel")

kakadu_archive = repository_rule(
    implementation = _kakadu_archive_impl,
    doc = "Fetches the Kakadu source archive from a GitHub release via `gh`.",
    attrs = {
        "tag": attr.string(
            mandatory = True,
            doc = "Release tag, e.g. \"kakadu-v8.5\".",
        ),
        "asset": attr.string(
            mandatory = True,
            doc = "Asset filename within the release, e.g. \"v8_5-01382N.zip\".",
        ),
        "repo": attr.string(
            mandatory = True,
            doc = "GitHub repo `owner/name`, e.g. \"dasch-swiss/dsp-ci-assets\".",
        ),
        "sha256": attr.string(
            mandatory = True,
            doc = "SHA-256 of the downloaded asset; mismatch fails the rule.",
        ),
        "strip_prefix": attr.string(
            doc = "Top-level directory inside the archive to strip on extract.",
        ),
        "patches": attr.label_list(
            allow_files = True,
            doc = "Patches applied in list order after extract.",
        ),
        "patch_strip": attr.int(
            default = 1,
            doc = "Strip count passed to `patch`. Generated patches use `-p1`.",
        ),
        "build_file": attr.label(
            allow_single_file = True,
            mandatory = True,
            doc = "BUILD.bazel symlinked into the extracted root.",
        ),
    },
    # Re-fetch when the token environment changes (e.g. CI rotation) so a stale
    # cached fetch doesn't survive a credential change.
    environ = ["GH_TOKEN", "GITHUB_TOKEN"],
)
