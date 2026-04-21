# Kakadu SDK and Sipi Builds

Sipi's JPEG2000 support uses the proprietary **Kakadu SDK**. The SDK is
not redistributable, so it lives in the private
[`dasch-swiss/dsp-ci-assets`](https://github.com/dasch-swiss/dsp-ci-assets)
repo as a release asset. The SHA-256 is pinned in `flake.nix`, so
builds are fully reproducible and verifiable.

Two independent fetch paths exist — one per build system:

| Build system | Fetches Kakadu via | Trigger |
|---|---|---|
| Nix (`nix build`, `nix develop`) | Fixed-output derivation in `flake.nix` | Automatic on build |
| Docker (`Dockerfile`) / local Docker dev | `scripts/fetch-kakadu.sh` into `vendor/` | `just kakadu-fetch` (one-time) |

## Nix builds

Export a GitHub token once per shell, then build normally:

```bash
export GH_TOKEN=$(gh auth token)
just nix-build-default
```

The flake's fixed-output derivation calls `gh release download` inside
the Nix sandbox to fetch `v8_5-01382N.zip` from the `kakadu-v8.5` release
on `dsp-ci-assets`. The derivation is content-addressed by its pinned
SHA-256, so:

- A hash mismatch fails the build instead of producing a Kakadu-less
  binary.
- After the first successful build lands on Cachix, machines with
  `cachix use dasch-swiss` substitute the output path and never need
  `GH_TOKEN` again.

Requirements for the first fetch on any given machine:

- Membership in the `dasch-swiss` GitHub organisation
- `gh auth login` completed (so `gh auth token` returns a usable PAT)

Optional: put the export in a direnv `.envrc` to avoid re-running it:

```bash
# .envrc
use flake
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
  export GH_TOKEN=$(gh auth token)
fi
```

## Docker builds

Docker builds need the archive under `vendor/` before the `COPY` step:

```bash
just kakadu-fetch    # downloads vendor/v8_5-01382N.zip if absent
just docker-build
```

The recipe is idempotent; re-running it when the archive is already
present is a no-op. `vendor/v8_5-*.zip` is gitignored, so the archive
never enters the commit history.

## Updating the Kakadu version

1. Publish a new archive on `dsp-ci-assets` (see
   [its `kakadu/README.md`](https://github.com/dasch-swiss/dsp-ci-assets/blob/main/kakadu/README.md))
2. In `flake.nix`, update `kakaduVersion`, `kakaduAssetName`, and `kakaduSha256`
3. Update `ASSET` and `TAG` in `scripts/fetch-kakadu.sh`
4. Remove the local archive: `rm vendor/v8_5-*.zip`
5. Re-build: `just nix-build-default` — a SHA-256 mismatch means step 2 is wrong
6. Docker path: `just kakadu-fetch` to refresh `vendor/`
7. Commit and open a PR

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `gh: command not found` | `gh` CLI not installed | Install GitHub CLI |
| `release not found` from `gh` | not authenticated, or no org membership | `gh auth login`; ask to be added to `dasch-swiss` |
| Nix FOD fails with `GH_TOKEN or GITHUB_TOKEN must be set` | Token not exported into the Nix build sandbox | `export GH_TOKEN=$(gh auth token)` and retry |
| Nix FOD: `hash mismatch` | Release asset replaced or pin out of date | Recompute SHA-256 from the release asset; update `kakaduSha256` in `flake.nix` |
| Docker build cannot find `vendor/v8_5-01382N.zip` | `just kakadu-fetch` not run | Run it once |

## CI

Every Nix CI step sets `GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` in its
`env:` so the FOD can authenticate against the private release. The PAT
is scoped to read `dsp-ci-assets`. After each successful run, the
content-addressed FOD output is pushed to Cachix and later runs
substitute it without hitting `gh`.

Docker publish jobs still run `./scripts/fetch-kakadu.sh` before `docker
build` to populate `vendor/` for the `COPY` step in the production
`Dockerfile`.

## Why not vendor it directly?

- Sipi is a public repo; Kakadu is proprietary — committing it would be a
  licence breach
- Keeping it in `dsp-ci-assets` aligns the licence-compliance boundary
  with repo membership
- Hash-pinning in `flake.nix` gives reproducible, cacheable, pure builds
  without the ~15 MB re-commit churn per version bump
