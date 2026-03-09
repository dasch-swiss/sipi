# Commit and PR Conventions

## Commit Organization

### Principle

Group commits by user-visible impact, not by implementation journey.

### Rules

1. Each `feat:` or `fix:` commit = one changelog entry visible to developers deploying Sipi
1. Internal work (`build:`, `ci:`, `refactor:`, `docs:`, `chore:`, `test:`) is hidden from changelog — squash aggressively
1. Ask: "would a developer deploying Sipi care about this change?" If yes → `feat:` or `fix:`. If no → hidden type.
1. Debugging journeys (trial-and-error, reverts, iterative fixes) belong in the PR description, not the commit history

### Where context lives

| Layer           | Audience                      | Content                           |
| --------------- | ----------------------------- | --------------------------------- |
| Commit messages | Release notes readers         | User-visible changes only         |
| PR description  | Reviewers + future developers | Full context including challenges |
| Learnings docs  | Future Claude + engineers     | Structured, searchable knowledge  |
| Code comments   | Code readers                  | "Why not the obvious approach"    |

## PR Description Format

### Template

```
[LINEAR-ID](linear-url)

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

### Why this format matters

The "Challenges and Decisions" section captures the debugging journey that would otherwise be lost when commits are squashed. The `/eng:workflows:compound` skill reads PR descriptions to generate structured learnings — well-structured challenges become high-quality learnings automatically.

### What goes where

| Information                        | Put it in...                        |
| ---------------------------------- | ----------------------------------- |
| New feature / breaking change      | Commit message (`feat:` / `feat!:`) |
| Bug fix                            | Commit message (`fix:`)             |
| Build/CI/refactor details          | Commit message (hidden type)        |
| Why the work was needed            | PR Motivation section               |
| What was tried and failed          | PR Challenges section               |
| Architecture decisions + rationale | PR Challenges section               |
| Things to watch out for            | PR Gotchas section                  |
| Structured, searchable knowledge   | Learnings doc (dasch-specs)         |
