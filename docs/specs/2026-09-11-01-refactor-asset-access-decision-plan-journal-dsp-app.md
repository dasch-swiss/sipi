# Journal — dsp-app phase (6)

Plan: `docs/specs/2026-09-11-01-refactor-asset-access-decision-plan.md`
Target repo: `/Users/subotic/_github.com/dasch-swiss/dsp-app`
Branch: `feature/dev-7155-image-settings-default`
Landed: PR #3453, squash `bdc9d606a`, 2026-09-17

Phase 6 only. Phase 1 (sipi) and Phases 2–5 (dsp-api) belong to other runs.

## Result

All 18 Phase 6 checkboxes done. `Off` → `Default`, `DELETE` in place of a `pct:100` POST, the
inherited-limit hint as new markup, the service method moved onto the generated client, and the
three test surfaces the plan asked for (unit spec, Storybook stories, Cypress walk) plus the CI
matrix registration. 239 unit tests, 20 suites, lint clean; all four required CI checks green.

## What the plan got right

The design section was specified tightly enough to implement without interpretation. The mockups
answered the one open question (the Default radio shows the inherited value as new markup) and the
copy was taken verbatim from them. The load-state mockup fixed the selection order — `isDefault`,
then `watermark`, then `size` — which is the order the code now uses.

The plan's prediction that the old sparse-payload handling would render the default state as a
128px restriction was correct.

## What the plan did not anticipate

**A review pass found four defects, all in error handling that the first revision added**, none in
the `Off` → `Default` change itself:

1. **A crash.** `settings.size` is optional on the wire. The rewrite kept a `!` assertion while
   deleting the guard (`else if (!settings || !settings.watermark) { return; }`) that made it safe,
   so `{watermark: false, isDefault: false}` threw. Worse, the throw is synchronous inside `next`,
   which RxJS routes to `reportUnhandledError` — the error callback added for exactly this case
   could never catch it. Now falls back to `Default`.
2. **Local `error:` callbacks suppressed the global handler.** They were added on the premise that a
   bare `.subscribe(next)` swallowed failures. It does not: RxJS reports unhandled errors to
   Angular's `ErrorHandler`, and this app's `AppErrorHandler` has specific messages for 0, 400, 403,
   404, 409 and 504. The "fix" replaced six messages with one. Both callbacks removed; both call
   sites now carry a comment so the omission is not restored.
3. **Error text in a success-styled snackbar.** `openSnackBar(msg)` defaults `type` to `'success'`.
   Dissolved with (2).
4. **A cleared percentage field left the Submit guard open.** imask leaves `''`, not `null`, so
   `isPercentageSize` stayed true while `parseInt('')/100` was `NaN`, and `NaN <= 0` is false —
   Submit stayed enabled and posted `size: "pct:"`. Found by the `@claude` PR review, not by the
   six reviewer agents.

**The lesson worth carrying:** the feature came through six reviewers clean. Every defect was in
robustness added around it, and two of them existed only because a bug was "fixed" that was never
there. Verify the premise before fixing.

## Traps a fresh session will otherwise hit

- **The generated OpenAPI client is gitignored** (`libs/vre/3rd-party-services/open-api/src/generated/`).
  Switching branches leaves the tree internally inconsistent — an old component against a
  regenerated client — and the symptom is a TypeScript error that reads like a code bug
  (`TS2305: … has no exported member 'RestrictedViewResponse'`). Regenerate or switch back.
- **`npm ci` fails with `notsup` on Node ≥ 23.** The repo pins 22.23.1 in `.nvmrc`. Since #3454
  there is a flake: `nix develop` gives the right toolchain, and `.envrc` wires direnv.
- **`SetRestrictedViewRequest` cannot be typed against the generated model.** The spec says
  `size: {value: string}`; dsp-api accepts only a bare string (DEV-7292). `getRequest` casts
  explicitly and both it and the spec assertion carry a comment saying so.
- **Cypress needs no `cy.resetDatabase()` here.** `cypress/support/e2e.ts` has a global per-spec-file
  `before()` that resets for every `system-admin/*` spec.
- **Pixeleye is advisory and was failing for infrastructure reasons** — two runs, each ~40 minutes
  to `TypeError: fetch failed` against `api.pixeleye.ops.dasch.swiss`, never reaching snapshot
  capture. It is not in branch protection's required set. This change therefore has no
  visual-regression signal; the three load states were checked by hand in Storybook.

## Closeout

`root_cause` — dsp-app encoded "no restriction wanted" as the IIIF size `pct:100`, a restriction
that restricts nothing, because the API had no way to express "inherit the platform default".

`investigation` — the design was settled in the plan; the work was implementation plus a review
pass that found more in the added error handling than in the feature.

`solution` — `Default` clears the stored setting via `DELETE`, the form can no longer express
`pct:100` by any path, and the three latent bugs the plan predicted (load-state selection, Submit
enabled on fresh load, and the percentage guard) are fixed.

`prevention` — the constraints that are not obvious from the code now carry comments naming their
reason (DEV-7292 on the request shape, the deliberate absence of error callbacks, the Cypress
reset hook), so the next agent finds the reason before the compiler or a failing test pushes it
toward the wrong fix.
