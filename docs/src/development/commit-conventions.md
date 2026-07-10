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

| Prefix | Meaning | Changelog section | Version bump |
|--------|---------|-------------------|--------------|
| `feat` | New user-visible functionality | Features | minor |
| `fix` | Bug fix (see [What `fix:` means](#what-fix-means)) | Bug Fixes | patch |
| `perf` | Performance improvement | Performance Improvements | patch |
| `revert` | Revert of a previous commit | Reverts | patch |
| `refactor` | Code restructuring, no behavior change | hidden | none |
| `docs` | Documentation | hidden | none |
| `test` | Adding or refactoring tests | hidden | none |
| `build` | Build system or dependencies | hidden | none |
| `ci` | Continuous integration | hidden | none |
| `style` | Formatting, no code change | hidden | none |
| `chore` | Miscellaneous maintenance | hidden | none |

Breaking changes take a `!` suffix (or a `BREAKING CHANGE:` footer) and
bump the **major** version:

    feat!: remove the deprecated cache-purge endpoint

Example:

    feat(shttps): support more authentication methods

The release-please config sentence-cases subjects in the changelog, so
write the subject in normal prose case (`feat(cache): add dual-limit
eviction`, not `feat(cache): Add ...`).

### What `fix:` means

A `fix:` corrects behavior that exists on `main` — a bug a deployer could
hit today, or that a released version shipped. It earns a "Bug Fixes"
changelog line and a patch bump precisely because deployers need to know.

A bug you introduced earlier in the **same branch** is not a `fix:`. Fold
it into the commit that introduced it (`git commit --fixup=<sha>`, then
`git rebase --autosquash`) so it never lands on `main` and never generates
a changelog entry for a bug nobody ever saw.

Corollary: if you discover a genuine pre-existing `main` bug while doing
unrelated work, give it its **own** `fix:` commit — don't bury it inside
the `feat:`/`refactor:` you happened to be writing.

## Scopes

The scope names the **module** the change belongs to. The canonical
module list — which is also the scope vocabulary — lives in
[`CONVENTIONS.md` § Module Layout](../../../CONVENTIONS.md). Use one of
these names, lowercase:

- **Module scopes:** `image`, `formats`, `metadata`, `iiifparser`,
  `handlers`, `shttps`, `cache`, `memory-budget`, `observability`,
  `logging`, `cli`, `ffi`, `lua`, `server-rs`, `cli-rs`
- **Test-layer scopes** (test-only changes spanning a whole layer):
  `e2e`, `approval`. A unit-test change takes the scope of the module
  under test.
- **Cross-cutting scopes** (changes not tied to one module): `deps`,
  `bazel`, `ci`, `nix`, `docker`.

Rules:

- Lowercase, kebab-case. `ci` not `CI`; `bazel` not `bazel-build`.
- Scope is optional. Omit it for genuinely repo-wide changes
  (`chore: ...`, `ci: ...`).
- A commit that spans several modules may list them comma-separated:
  `refactor(cli-rs,server-rs): ...`.
- **If none of the enumerated scopes genuinely fits, ask the maintainer
  before inventing a new one.** New scopes are added to the canonical
  list in `CONVENTIONS.md` deliberately, not ad hoc.

## Commit Organization

### Principle

Start from the assumption that the whole PR is **one commit**. Group
commits by user-visible impact, not by implementation journey. Only split
the PR when the work genuinely divides into several independent,
self-contained changes that each stand on their own.

### Rules

1. Each `feat:` or `fix:` commit = one changelog entry visible to
   developers deploying Sipi.
2. Internal work (`build:`, `ci:`, `refactor:`, `docs:`, `chore:`,
   `test:`) is hidden from the changelog — squash aggressively.
3. Ask: "would a developer deploying Sipi care about this change?"
   If yes → `feat:` or `fix:`. If no → hidden type.
4. Debugging journeys (trial-and-error, reverts of in-branch mistakes,
   iterative fixes) belong in the PR description, not the commit history.
   See [What `fix:` means](#what-fix-means).

### Where context lives

| Layer | Audience | Content |
|-------|----------|---------|
| Commit messages | Release notes readers | User-visible changes only |
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
| Build/CI/refactor details | Commit message (hidden type) |
| Why the work was needed | PR Motivation section |
| What was tried and failed | PR Challenges section |
| Architecture decisions + rationale | PR Challenges section |
| Things to watch out for | PR Gotchas section |
| Structured, searchable knowledge | Learnings doc (dasch-specs) |
