# Commit and PR Conventions

## Git Workflow

We use a **rebase workflow**. All changes are made on a branch, then
rebased onto `main` before being merged. This keeps a clean, linear
history.

The goal is a **meaningful history on `main`**: every commit on main
should be a deliberate, self-contained unit of change. Working commits
("WIP", "fix typo", "address review feedback") do not belong on main.

- **Rebase-merge**: PRs are integrated using rebase-merge (not squash or
  merge commits). Every commit on the branch lands on main verbatim — so
  the branch history _is_ the main history. There is no squash-on-merge
  safety net; whatever you leave on the branch is what ships.
- **Clean up before merging (mandatory)**: before a PR is merged, rewrite
  the branch (interactive rebase) so its commits read well on main.
  Squash working commits, reword messages, reorder as needed.
- **Default to a single commit**: almost always, a PR should end up as
  **one** clean commit. Split into multiple commits only when the work
  genuinely represents several independent, self-contained changes that
  each deserve their own line in the history — and each must stand on its
  own. When in doubt, squash to one. See [Commit
  Organization](#commit-organization).

## Commit message schema

We use [Conventional Commits](https://www.conventionalcommits.org/).
These prefixes drive [release-please](ci.md#release-automation-release-please)
to determine SemVer bumps and generate the changelog —
**using the correct prefix is required, not optional**.

    type(scope): subject
    body

**Every commit has a type and a scope** — both are mandatory (see
[Scopes](#scopes)). This is the whole table; there are exactly eight types:

| Type | Meaning | Changelog section | Version bump |
|--------|---------|-------------------|--------------|
| `feat` | New user-visible functionality | Features | minor |
| `fix` | Bug fix (see [What `fix:` means](#what-fix-means)) | Bug Fixes | patch |
| `perf` | Performance improvement | Performance Improvements | patch |
| `refactor` | Code restructuring, no behavior change | Code Refactoring | patch |
| `docs` | Documentation | Documentation | patch |
| `test` | Adding or refactoring tests | Tests | patch |
| `build` | Build system or dependencies | Build System | patch |
| `chore` | Miscellaneous maintenance | Miscellaneous Chores | patch |

Every type is visible in the release notes, in its own section, rendered in the
order above. Because the scope is mandatory, every changelog line reads
`concern: subject`. This table is the single human source for the type
vocabulary; the machine source is
[`.github/release-please/config.json`](../../../.github/release-please/config.json).

Breaking changes take a `!` suffix (or a `BREAKING CHANGE:` footer) and
bump the **major** version:

    feat(cache)!: remove the deprecated cache-purge endpoint

Example:

    feat(jwt): support more authentication methods

The release-please config sentence-cases subjects in the changelog, so
write the subject in normal prose case (`feat(cache): add dual-limit
eviction`, not `feat(cache): Add ...`).

### Removed types

`revert`, `style`, and `ci` are **not** valid types — the commit-lint gate
rejects them, and release-please hides them if one ever slips onto `main`.

- **CI/build automation** → `chore(ci): ...` (lands in Miscellaneous Chores).
  Dividing CI into concerns is not worthwhile, and a dedicated Continuous
  Integration changelog section is noise; it belongs under maintenance.
- **Formatting-only changes** → fold into the functional commit, or
  `chore(<scope>): ...`. `rustfmt`/`clang-format` are enforced gates, so a
  standalone formatting commit should be rare.
- **Reverts** → `fix(<scope>): revert ...` or `chore(<scope>): revert ...`
  depending on whether it corrects released behavior. (release-please's built-in
  revert auto-negation no longer applies; reverts are rare and handled by hand.)

### What `fix:` means

A `fix:` corrects behavior that exists on `main` — a bug a deployer could
hit today, or that a released version shipped. It earns a "Bug Fixes"
changelog line precisely because deployers need to know. The same change
filed as `chore:` or `refactor:` still appears in the release notes, but
under a section nobody scanning for a regression will read.

A bug you introduced earlier in the **same branch** is not a `fix:`. Fold
it into the commit that introduced it (`git commit --fixup=<sha>`, then
`git rebase --autosquash`) so it never lands on `main` and never generates
a changelog entry for a bug nobody ever saw.

Corollary: if you discover a genuine pre-existing `main` bug while doing
unrelated work, give it its **own** `fix:` commit — don't bury it inside
the `feat:`/`refactor:` you happened to be writing.

## Scopes

**A scope is mandatory.** Every commit is `type(scope): subject`, so every
release-notes line reads `concern: subject`. There is no scopeless form.

The scope names the **concern the change serves** — the module whose
responsibility it belongs to, not the directory the edited files happen to
sit in. Telemetry code under `src/server-rs/` is `observability`; an `ffi`
seam field whose only purpose is to carry a metric across is `observability`
too. Scope by what the change is *about*, not where it landed. The canonical
module list — which is also the scope vocabulary — lives in
[`CONVENTIONS.md` § Module Layout](../../../CONVENTIONS.md). Use one of
these names, lowercase:

- **Module scopes:** `image`, `formats`, `metadata`, `iiifparser`,
  `scripting`, `util`, `jwt`, `cache`, `memory-budget`,
  `memory`, `observability`, `logging`, `cli`, `ffi`, `lua`, `server-rs`,
  `cli-rs` (`scripting` = the C++ Lua runtime in `src/scripting/`; `lua` stays
  the Lua *scripts*/config in `scripts/` + `config/*.lua`; the retired `handlers`
  scope folded into `iiifparser` per DUNE-014)
- **Test-layer scopes** (`e2e`, `approval`) — for changes to a test
  layer's own harness or fixtures. A test *about* a specific concern takes
  that concern's scope (`test(observability): ...`), not the layer: the
  `test` type already says it is a test, so the scope is free to name the
  concern. A unit-test change takes the scope of the module under test.
- **Cross-cutting scopes** (changes not tied to one module): `deps`,
  `bazel`, `ci`, `nix`, `docker`, `docs`. Use `docs` for the agent-context /
  domain documentation that spans no single module — the root `CLAUDE.md` /
  `CONVENTIONS.md` / `CONTEXT.md` / `UBIQUITOUS_LANGUAGE.md` and the ADRs. A doc
  change about one module still takes that module's scope.

Rules:

- Lowercase, kebab-case. `ci` not `CI`; `bazel` not `bazel-build`.
- A commit that spans several modules may list them comma-separated:
  `refactor(cli-rs,server-rs): ...`.
- **Concern over location.** When code for one module's responsibility lives
  under another module's directory, scope by the responsibility, not the
  enclosing directory. `server-rs/src/metrics.rs` → `observability`.
- **No catch-all.** There is no `repo`/`all` scope. If none of the enumerated
  scopes genuinely fits, ask the maintainer before inventing one — new scopes
  are added to the canonical list in `CONVENTIONS.md` deliberately, not ad hoc.

The commit-lint gate enforces only that a scope is *present* (it does not yet
restrict *which* scope — there is no closed vocabulary). The list above is the
advisory vocabulary; keep to it.

## Enforcement

Commit messages are gated in CI by
[`commitlint-rs`](https://github.com/KeisukeYamashita/commitlint-rs) via the
`commit-lint` job in [`ci.yml`](../../../.github/workflows/ci.yml) (a composite
action at `.github/actions/commit-lint`). The rules live in
[`.commitlintrc.yml`](../../../.commitlintrc.yml): the `type` allowlist above is
mandatory, `scope-empty` makes the scope mandatory, and the subject must be
non-empty.

Run the same check locally before pushing:

    just commit-lint            # lints origin/main..HEAD
    just commit-lint <base-ref> # lints <base-ref>..HEAD

`commitlint-rs` is in the Nix dev shell (`nix develop`); CI installs it with
`cargo install`.

Merge commits are skipped by the linter (message rules do not apply to them). The
rebase workflow is enforced separately by the `require linear history`
branch-protection rule, which blocks any merge commit from landing on `main`.

## Commit Organization

### Principle

Start from the assumption that the whole PR is **one commit**. Group
commits by user-visible impact, not by implementation journey. Only split
the PR when the work genuinely divides into several independent,
self-contained changes that each stand on their own.

### Rules

1. Each `feat:` or `fix:` commit = one changelog entry in the sections
   developers deploying Sipi read first.
2. Internal work (`build:`, `ci:`, `refactor:`, `docs:`, `chore:`,
   `test:`) lands in its own changelog section further down — squash
   aggressively so those sections stay readable.
3. Ask: "would a developer deploying Sipi care about this change?"
   If yes → `feat:` or `fix:`. If no → an internal-work prefix.
4. Debugging journeys (trial-and-error, reverts of in-branch mistakes,
   iterative fixes) belong in the PR description, not the commit history.
   See [What `fix:` means](#what-fix-means).

### Where context lives

| Layer | Audience | Content |
|-------|----------|---------|
| Commit messages | Release notes readers | Every change; `feat:`/`fix:` carry the user-visible ones |
| PR description | Reviewers + future developers | Full context including challenges |
| Learnings docs | Future Claude + engineers | Structured, searchable knowledge |
| Code comments | Code readers | "Why not the obvious approach" |

## PR Description Format

The repository ships a [`.github/PULL_REQUEST_TEMPLATE.md`](../../../.github/PULL_REQUEST_TEMPLATE.md)
that pre-populates this structure when you open a PR.

### Template

```
Fixes LINEAR-ID, LINEAR-ID, ...

## Motivation
Why this work was needed. What problem it solves for users.

## Summary
1-3 bullet points of user-visible changes.

## Key Changes
### [Topic]
- change details

## Challenges and Decisions
What was tried, what failed, and key architecture decisions.
Structure as sub-sections when multiple challenges exist:

### [Challenge title]
**Problem:** description of the issue encountered
**Tried:** approaches that didn't work and why
**Solution:** what worked and why it's the right approach

## Gotchas
Things future developers should know. Each gotcha should be
actionable — not just "this is hard" but "do X instead of Y".

## Test Plan
- [ ] verification steps
```

Use `Part of LINEAR-ID` instead of `Fixes LINEAR-ID` when the PR advances
an umbrella issue it does not close.

### Why this format matters

The "Challenges and Decisions" section captures the debugging journey
that would otherwise be lost when commits are squashed. The
`/eng:workflows:compound` skill reads PR descriptions to generate
structured learnings — well-structured challenges become high-quality
learnings automatically.

### What goes where

| Information | Put it in... |
|-------------|-------------|
| New feature / breaking change | Commit message (`feat:` / `feat!:`) |
| Bug fix | Commit message (`fix:`) |
| Build/CI/refactor details | Commit message (`build:` / `ci:` / `refactor:`) |
| Why the work was needed | PR Motivation section |
| What was tried and failed | PR Challenges section |
| Architecture decisions + rationale | PR Challenges section |
| Things to watch out for | PR Gotchas section |
| Structured, searchable knowledge | Learnings doc (dasch-specs) |
