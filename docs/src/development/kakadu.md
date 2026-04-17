# Kakadu SDK and Sipi Builds

Sipi's JPEG2000 support uses the proprietary **Kakadu SDK**. The SDK is
not redistributable, so it lives in the private
[`dasch-swiss/dsp-ci-assets`](https://github.com/dasch-swiss/dsp-ci-assets)
repo as a release asset and is fetched into `vendor/` on demand. The
SHA-256 is pinned in `flake.nix`, so Nix builds are fully reproducible
and verifiable.

## One-time setup (local development)

A single command using the GitHub CLI:

```bash
just kakadu-fetch
```

Requirements:

- Membership in the `dasch-swiss` GitHub organisation
- `gh auth login` completed

This downloads `vendor/v8_5-01382N.zip` from the `kakadu-v8.5` release
on `dsp-ci-assets`. After it succeeds, `nix build .#default` (and every
other Nix build target) just works — no env vars, no flags, no `--impure`.

The recipe is idempotent: re-running it when the file is already present
is a no-op.

`vendor/v8_5-*.zip` is gitignored (Kakadu is proprietary). Nix's flake
source filter would ordinarily exclude gitignored files, so the script
also runs `git add --intent-to-add --force` to make Nix see the file
without staging it for commit. After running the script the file
appears as `A` in `git status` — that's expected; the gitignore entry
still protects against accidental `git add`.

Verify:

```bash
nix build .#default            # should succeed without --impure
```

## Updating the Kakadu version

1. Publish a new archive on `dsp-ci-assets` (see
   [its `kakadu/README.md`](https://github.com/dasch-swiss/dsp-ci-assets/blob/main/kakadu/README.md))
2. In `flake.nix`, update `kakaduAssetName` and `kakaduSha256`
3. Update the `ASSET` and `TAG` constants in `scripts/fetch-kakadu.sh`
4. Remove the old archive: `rm vendor/v8_5-*.zip`
5. Re-fetch: `just kakadu-fetch`
6. Build: `nix build .#default` — a sha256 mismatch means step 2 is wrong
7. Commit and open a PR

## Troubleshooting

| Symptom                                     | Cause                                    | Fix                                              |
|---------------------------------------------|------------------------------------------|--------------------------------------------------|
| `gh: command not found`                     | `gh` CLI not installed                   | Install GitHub CLI                               |
| `release not found` from `gh`               | not authenticated, or no org membership  | `gh auth login`; ask to be added to `dasch-swiss` |
| `gh: HTTP 404` for the asset                | asset filename or tag wrong              | Check the release on dsp-ci-assets               |
| Nix: `path … is not a file`                 | `vendor/v8_5-01382N.zip` missing         | `just kakadu-fetch`                              |
| Nix: `path … has hash X but expected Y`     | local archive content differs from pin   | Verify against `dsp-ci-assets/kakadu/README.md`; if release was replaced (shouldn't happen), update `kakaduSha256` in `flake.nix` |

## CI

Every Nix CI job runs `./scripts/fetch-kakadu.sh` after installing Nix
and before `nix build`/`nix develop`. The job exports `GH_TOKEN` from
the org-level `DASCHBOT_PAT` secret so `gh release download` can
authenticate against the private repo. See `.github/workflows/test.yml`,
`loadtest.yml`, `sanitizer.yml`, and `fuzz.yml`.

## Why not vendor it directly?

- Sipi is a public repo; Kakadu is proprietary — committing it would be a
  licence breach
- Keeping it in `dsp-ci-assets` keeps the licence-compliance boundary
  aligned with repo membership
- Hash-pinning via `builtins.path { sha256 = …; }` gives reproducible,
  cacheable, pure builds without the 12 MB re-commit churn per version bump
