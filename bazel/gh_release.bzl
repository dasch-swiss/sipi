"""Custom repository_rule for fetching GitHub-release-gated archives.

Used for assets on the private `dasch-swiss/dsp-ci-assets` repo (Kakadu
source, benchmark fixtures). Shells out to `gh release download`, so the
auth UX is `gh auth login` locally and `GH_TOKEN=$DASCHBOT_PAT` in CI.
Plain `http_archive` + `auth_patterns` is rejected because `auth_patterns`
consumes `~/.netrc`, which `gh` does not populate; preserving the
`gh auth token` flow avoids out-of-band `~/.netrc` plumbing.
"""

def _gh_release_archive_impl(ctx):
    gh = ctx.which("gh")
    if not gh:
        fail(
            "gh_release_archive: `gh` not found on PATH.\n" +
            "  - Local dev: `gh` is declared in `flake.nix` devShells.\n" +
            "  - CI: ensure the Bazel job runs inside `nix develop`.",
        )

    token = ctx.os.environ.get("GH_TOKEN") or ctx.os.environ.get("GITHUB_TOKEN")
    if not token:
        # Local dev fallback: read token from `gh`'s stored credentials.
        result = ctx.execute([gh, "auth", "token"])
        if result.return_code != 0:
            fail(
                "gh_release_archive: no GH_TOKEN/GITHUB_TOKEN in env and " +
                "`gh auth token` failed: " + result.stderr +
                "\n  - Local dev: run `gh auth login` once.\n" +
                "  - CI: set `env: GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` on the job.",
            )
        token = result.stdout.strip()

    # Download under the asset's own filename — `ctx.extract` infers the
    # archive type from the extension, and the asset name never collides
    # with the extracted content (`strip_prefix` differs in both callers).
    raw_archive = ctx.attr.asset
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
        fail("gh_release_archive: `gh release download` failed: " + download_result.stderr)

    # `ctx.extract` does not enforce sha256, so verify the downloaded blob
    # explicitly before extracting. (The historical trick of re-downloading
    # the local file via `ctx.download(url = "file://…", sha256 = …)` crashes
    # Bazel 9.1.0 outright: file:// URIs have a null host, and
    # HttpDownloader NPEs on `URI.getHost()`. Shelling out is plain and
    # portable: `sha256sum` on Linux, `shasum -a 256` on macOS.)
    sha_tool = ctx.which("sha256sum")
    if sha_tool:
        sha_cmd = [sha_tool, raw_archive]
    else:
        shasum = ctx.which("shasum")
        if not shasum:
            fail("gh_release_archive: neither `sha256sum` nor `shasum` found on PATH.")
        sha_cmd = [shasum, "-a", "256", raw_archive]
    sha_result = ctx.execute(sha_cmd)
    if sha_result.return_code != 0:
        fail("gh_release_archive: checksum tool failed: " + sha_result.stderr)
    got_sha256 = sha_result.stdout.split(" ")[0].strip()
    if got_sha256 != ctx.attr.sha256:
        fail("gh_release_archive: sha256 mismatch for asset '%s': expected %s, got %s" %
             (ctx.attr.asset, ctx.attr.sha256, got_sha256))
    ctx.extract(raw_archive, stripPrefix = ctx.attr.strip_prefix)

    # The verified blob is not needed after extraction — deleting it halves
    # the repository directory's footprint (the fixture archive is ~321 MB).
    ctx.delete(raw_archive)

    # Bazel's `ctx.patch` requires hunks to match line numbers exactly — it
    # does not implement GNU patch's fuzz tolerance. The Kakadu Linux Makefile
    # patches were generated with a couple of lines of drift relative to
    # the upstream tarball, so `ctx.patch` rejects them at hunk #2 with
    # `CONTENT_DOES_NOT_MATCH_TARGET`. Shelling out to system `patch` (which
    # the dev shell exposes via its standard build-tool set) restores the
    # fuzz behaviour and matches what the historical Nix FOD did.
    if ctx.attr.patches:
        patch = ctx.which("patch")
        if not patch:
            fail("gh_release_archive: `patch` not found on PATH (needed for patches).")
        for patch_label in ctx.attr.patches:
            patch_path = ctx.path(patch_label)
            patch_result = ctx.execute(
                [patch, "-p" + str(ctx.attr.patch_strip), "-i", str(patch_path)],
            )
            if patch_result.return_code != 0:
                fail(
                    "gh_release_archive: `patch -p%d -i %s` failed: %s" %
                    (ctx.attr.patch_strip, str(patch_path), patch_result.stderr),
                )

    ctx.symlink(ctx.attr.build_file, "BUILD.bazel")

gh_release_archive = repository_rule(
    implementation = _gh_release_archive_impl,
    doc = "Fetches an archive asset from a GitHub release via `gh`.",
    attrs = {
        "tag": attr.string(
            mandatory = True,
            doc = "Release tag, e.g. \"kakadu-v8.7\".",
        ),
        "asset": attr.string(
            mandatory = True,
            doc = "Asset filename within the release, e.g. \"v8_7-01727L.zip\".",
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
