---
title: "Asset access as a first-class decision, and restricted view as an optional setting"
date: 2026-09-11
author: "Ivan Subotic"
status: reviewed(5)
repositories:
  - dsp-api
  - dsp-app
---

# Asset access as a first-class decision, and restricted view as an optional setting

## Overview

`GET /admin/files/{shortcode}/{filename}` returns the user's ACL level as a bare
integer, and each caller invents an access policy from it. Replace that with an
explicit decision computed once in dsp-api: what may this caller receive for the
**original**, and what may it receive for the **derivative**. Introduce `stream`
as a first-class derivative decision so that "may be played, may not be
downloaded" is stated rather than approximated, and so that tightening streaming
later is a change inside SIPI alone.

Alongside it, make the project-level restricted view setting optional
(`Option[RestrictedView]`) and stop dsp-app encoding "no restriction wanted" as
the no-op IIIF size `pct:100`.

SIPI is the forcing function: wave-2
(`docs/specs/2026-09-04-01-fix-sipi-security-hardening-wave-2-plan.md`, Phase 5)
refuses a `restrict` decision that restricts nothing, so every approximation in
the current model becomes a 403 when the wave-2 image rolls.

## Problem Statement / Motivation

Three inputs determine what a caller may receive: the ACL level, **which
artifact** (original or derivative), and **what kind of media**. The response
carries only the first, so each caller reconstructs the other two:

| Caller | Policy it invented | Consequence |
|---|---|---|
| `modules/sipi/scripts/sipi.init.lua:129-141` | code 1 → `restrict`, whatever the media type | Under wave-2, `restrict` on `/file` is a 403 (`src/server/rust/src/routes.rs:713`), so audio, video and PDFs stop playing for `RV` users |
| `modules/ingest/.../ProjectsEndpointsHandler.scala:125` | `permissionCode >= 2` → hand over the original | Any change to the code's meaning silently widens original-file access |
| `image-settings.component.ts:166` | "no restriction" → `pct:100` | A restriction that restricts nothing, which wave-2 refuses |

Every defect found in this area is one instance of that pattern, so fixing them
individually leaves the pattern — and the next instance — intact.

**The streaming intent is real and currently unexpressed.** `RV` on audio and
video is used to model *stream-only* access: material DaSCH may play but may not
hand over. Today that is approximated by SIPI streaming the whole file from
`/file`, which is trivially circumvented. How strict streaming must become is an
open legal question. Expressing the intent as its own decision is what lets the
answer change inside SIPI without a cross-repo migration each time.

**"Off" is not representable, so dsp-app fakes it.**
`KnoraProject.restrictedView` (`KnoraProject.scala:40`) is non-optional and
`KnoraProjectRepoLive.getRestrictedView:93-100` resolves an absent triple to
`RestrictedView.default`. dsp-app's third radio, **Off**, writes `pct:100`
(`image-settings.component.ts:166`, read back at `:132`). Two production
projects carry that triple — `0838 geoarch` and `0848 digitalagenda`, confirmed
on prod, stage and dev. The string has never existed in dsp-api's history
(`git log -S "pct:100" --all` is empty) and dsp-tools never calls the endpoint,
so dsp-app's own radio is the only writer. The label is wrong on its own terms
too: the setting is inert unless an asset carries `RV`, so it turns nothing off.
It should read **Default**.

## Proposed Solution

### 1. The endpoint returns a decision, not an input

```scala
enum MediaKind:                        // derived from the file value's RDF class
  case RasterStillImage, Vector, MovingImage, Audio, Document, Archive, Text, ThreeD

enum OriginalAccess:                   // the Original channel — dsp-ingest reads only this
  case Withhold, Grant

enum DerivativeAccess:                 // the Derivative channel — SIPI's hook reads only this
  case Denied                          // wire "denied"  — no view permission
  case Clamped(view: RestrictedView)   // wire "clamped" — raster still image; RestrictedView is a size or a watermark, never both
  case Stream                          // wire "stream"  — consumable in place, never handed over as a file
  case Full                            // wire "full"    — full view permission

final case class AssetAccess private (original: OriginalAccess, derivative: DerivativeAccess)
```

One total function holds the entire policy:

```scala
def from(perm: Option[Permission.ObjectAccess], media: MediaKind, stored: Option[RestrictedView]): AssetAccess =
  perm match
    case None                                              => AssetAccess(Withhold, Denied)
    case Some(RestrictedView) if media == RasterStillImage => AssetAccess(Withhold, Clamped(stored.getOrElse(RestrictedView.default)))
    case Some(RestrictedView) if media == Archive          => AssetAccess(Withhold, Denied)  // see below
    case Some(RestrictedView)                              => AssetAccess(Withhold, Stream)
    case Some(_)                                           => AssetAccess(Grant,    Full)
```

This is the only place an `AssetAccess` is constructed — the constructor is
private, so no caller and no later edit can assemble an inconsistent pair such
as `Grant` with `Denied`.

Two independent channels, flat, closed, and tagged. `derivative` carries a single
string literal; the clamp parameters are siblings and appear only under
`clamped`. The dominant preflight case is one key and one string compare.

```jsonc
// V or above
{ "derivative": "full",    "original": "grant"    }
// RV on a still image, project stores a size
{ "derivative": "clamped", "size": "!128,128", "original": "withhold" }
// RV on a still image, project stores a watermark
{ "derivative": "clamped", "watermark": true,  "original": "withhold" }
// RV on a video, audio, PDF, text, SVG or 3D model
{ "derivative": "stream",  "original": "withhold" }
// no view permission, and RV on an archive
{ "derivative": "denied",  "original": "withhold" }
```

`size` is an **IIIF size string**, not a number: SIPI clamps with
`clamp_dims_to_size` (`src/iiifparser/rust/parse.rs:278`), which accepts
`!w,h` and `pct:N` alike, and `RestrictedView.Size` already stores exactly that
grammar. A `maxDimension` integer could not express `pct:30` or a non-square
box. `watermark` is a **boolean**, not a path: the watermark file is a SIPI
deployment detail the hook already supplies (`/sipi/scripts/watermark.tif` in
today's `sipi.init.lua`), and dsp-api has no business knowing SIPI's filesystem.
Exactly one of `size` or `watermark` accompanies `clamped`, mirroring
`SetRestrictedViewRequest`'s own validation.

`derivative` maps 1:1 onto a SIPI *Permission*, so the hook translates rather
than decides: `full` → `allow`, `clamped` → `restrict`, `stream` → `stream`,
`denied` → `deny`. The literals stay VRE-flavoured rather than reusing SIPI's
`allow`/`restrict`/`deny` verbatim: the payload belongs to the VRE, and a
general-purpose IIIF server's verdict vocabulary should not become the VRE's
wire contract. Every literal has a producer — there is no wire-representable
state the policy never emits. An unrecognised literal must
deny **and increment a counter** — a silent catch-all would make a future
rollout look like mysterious denials with no stack trace.

On the dsp-api side that is a real counter. In the hook it is a `LOG_ERR` line:
SIPI exposes no metrics binding to Lua, and opening one for a single defensive
branch would put metric labels under script control, which is a cardinality
hazard. The branch fires only on a vocabulary mismatch between dsp-api and the
hook, and the two ship in the same image, so an alert on the log line is the
proportionate signal. Decided 2026-09-12.

**Vocabulary.** The payload is authored by the VRE (`dsp-api`) and consumed by
`dsp-ingest` and by SIPI's hook, so it speaks VRE terms — **Original** and
**Derivative** — not the Access Area's Preservation File / Service File / Access
File. The two are a lifecycle, not a translation table: an archived Original
coexists with the Preservation File derived from it. And within the VRE the two
channels name two *routes*, not always two artifacts — only still images are
transcoded, so for every other media kind the Derivative is a byte copy of the
Original. `CONTEXT.md` § Upstream language (from the VRE) records both points for
SIPI readers.

**The two channels never merge.** `original` is read only by dsp-ingest and
`derivative` only by SIPI. Collapsing them — inferring download eligibility from
a rendering decision — is precisely the coupling that let `permissionCode >= 2`
widen original access, so the shape makes it unavailable. The invariant
`original: grant ⇒ derivative: full` cannot be expressed in JSON, so the pair is
built only by `AssetAccess.from` via smart constructors, never field by field.

The callers stop deciding anything. `sipi.init.lua` maps the `derivative` literal
onto the vocabulary SIPI speaks — `deny` / `restrict` / `stream` / `allow` —
supplying its own watermark path when `clamped` carries `watermark: true`, and
its `permission_code == 1` branch, including the contested
`restrictedViewSettings == nil` case, disappears rather than acquiring a better
default. dsp-ingest checks `original == "grant"` and its `>= 2` arithmetic goes.

**Why the ACL codes are not extended instead.** `Permission.ObjectAccess`
(RV=1, V=2, M=6, D=7, CR=8) is the vocabulary permission arithmetic is done in
across the codebase. Adding an access-decision value would leak a serving concept
into the ACL enum and break `Permission.ObjectAccess.from(code)`. The ACL level
stays exactly what it is; the decision derived from it becomes its own type.

### 2. `stream` is intent, and SIPI owns the mechanism

SIPI gains a `stream` permission type. **On day one it behaves exactly as
`allow`** — `/file` already emits `inline; filename="…"` for every response
(`content_disposition`, `routes.rs:2351-2367`; SIPI never sends `attachment`),
so there is no disposition to force and no byte served differently. The only
addition is a counter, so the `stream` population becomes observable before any
enforcement decision is taken.

That is the honest description, and it is the point: the type buys a recorded
intent and a seam, not behaviour. Every later tightening is then SIPI-local —
refuse a full-file GET without a `Range` header, segment or transcode on the
fly, bind ranges to a short-lived token, rate-limit — and none of those touch
dsp-api, dsp-ingest, the hook or dsp-app.

`stream` is **not a security boundary today** and must be documented as such.
The reason is stronger than "a capture cannot be prevented": only still images
are transcoded. `IngestService` sends `MovingImage`, `Audio`, `SvgImage` and
`OtherFiles` through `storage.copyFile(original.file, derivative)`, so for every
non-image kind the Derivative is byte-identical to the Original. `original:
withhold` + `derivative: stream` therefore withholds **no bytes** — it closes one
route and records an intent while SIPI serves the same content on another.

**Limit on "tighten it in SIPI alone".** Range-only delivery, forced inline
disposition, token-bound ranges and rate limiting are all SIPI-local and reachable
without touching this contract. But the strongest measures — pre-segmented
HLS/DASH with key rotation — require a *different derivative*, which only ingest
can produce. So the seam buys SIPI-local tightening up to the point where the
served artifact itself must change; past that, ingest is in scope again. Worth
knowing before the legal reading firms up rather than discovering it then.

**Archives are denied.** An archive has no in-place consumption — the file is
its only representation — so there is no restricted rendering to fall back on.
And because an archive is never transcoded, serving it would mean ingest refuses
`/original` while SIPI hands over a byte-identical copy at `/file`: not a narrow
gap but the entire content, through the other route. `RV` on an archive
therefore resolves to `denied`, and the two routes agree.

This is the one **user-visible behaviour change** in the plan: an `RV` archive is
served today and will stop being served. It needs the census, a release note and
an RDU warning before rollout — see Operator Actions.

### 3. Restricted view becomes an optional project setting

`KnoraProject.restrictedView` becomes `Option[RestrictedView]`; `None` means "no
project-level setting stored". The platform default stays the domain constant
`RestrictedView.default` (`!128,128`), applied when answering, never when
storing. It is deliberately not a config key: a security-relevant default that
could differ between dev, stage and prod would make a restricted-view bug
irreproducible across environments, and there is one production deployment.

**Why the default stays an absolute box, not a percentage.** An absolute cap
degrades unevenly: `!128,128` gives a 4096px scan 3.1% of its linear resolution
and a 300px image 42.7%, so the small image is far more legible relative to its
original. A `pct:N` default would equalise that. Measured on stage, the
asymmetry binds on almost nothing — of 103,231 `RV` still images:

| Long edge | ≤128 | ≤256 | ≤512 | ≤1024 | ≤2048 | ≤4096 | >4096 |
|---|---|---|---|---|---|---|---|
| Count | **0** | 14 | 6 | 274 | 5661 | 60523 | 36753 |

94% are above 2048px and only 294 sit below 1024. Zero are ≤128, so the
degenerate case where a cap cannot reduce at all — wave-2 answers 400 or 403 —
has no real asset behind it today. The guard still belongs in the code.

Equal legibility is the right goal for a *preview* and the wrong one for a
*restriction*: what is being protected against is someone obtaining a usable
reproduction, and usability is absolute. A 1000px image is usable whether it came
from 1200px or 12000px, so `pct:10` on the 36,753 images above 4096px would still
hand out a usable copy. An absolute box bounds the leak; a percentage bounds only
the ratio.

**Future refinement, out of scope here:** bound by both, `f = min(f_box, f_pct)`
— large images capped absolutely, small ones guaranteed a real reduction instead
of a near-no-op. SIPI is already shaped for it (wave-2 computes
`f = min(rest_w/img_w, rest_h/img_h)` and applies it as a percent, so this is one
more `min`). The cost is in dsp-api: `RestrictedView.Size` is `!w,h` *xor*
`pct:N`, so carrying both needs a model change, a wire change, and two UI inputs
that are currently mutually exclusive by construction. It would change the served
result for ~294 images out of 103k, none of them currently broken.

Projects that care already override in both directions — `dRHtOC…` and `Y9hvow…`
chose `pct:1` (≈41px on a 4096 scan, harsher than the default) while `0118` chose
`pct:13` (≈530px, looser) — which is evidence `!128,128` sits at a defensible
middle rather than being wrong.

`GET .../RestrictedViewSettings` keeps returning the effective settings and gains
`isDefault: Boolean`. A new `DELETE .../RestrictedViewSettings` clears the stored
setting. `POST` keeps its "exactly one of size or watermark" validation, so an
empty body stays an error rather than silently becoming a clear.

**All three verbs return the same shape.** Today `POST` answers
`RestrictedViewResponse(size, watermark)`
(`ProjectsEndpointsRequestsAndResponses.scala:66-69`) while `GET` answers
`ProjectRestrictedViewSettingsGetResponseADM`, and dsp-app assigns both into one
state field (`image-settings.component.ts:50`). Adding `isDefault` to `GET` alone
would mean the flag is present on load and absent after a save, which is exactly
the state `hasChanges` compares. `POST` and `DELETE` therefore adopt the `GET`
response shape, and `RestrictedViewResponse` is retired — collapsing three shapes
into one and letting dsp-app's union type become a single type.

`RestrictedView.Size.from` rejects `pct:100`, and an upgrade plugin deletes the
stored triples. dsp-app's third radio becomes **Default** and calls `DELETE`.

**The migration also clears the backfilled defaults.** Measured on stage, 48 of
the 62 projects store exactly `!128,128` — the value `UpgradePluginPR3112` wrote
wherever neither triple was set, not a value anyone chose. The plugin deletes
those alongside the two `pct:100` triples, so **50 of 62 projects land in
`None`** and the GUI shows them as **Default**, which is what they are. Twelve
keep an explicit setting: four watermarks, `!512,512` ×2, `pct:1` ×2,
`!1024,1024`, `pct:13`, `pct:80`, `pct:99`.

No served byte changes — `None` resolves to `RestrictedView.default`, the same
`!128,128`. Two consequences are accepted deliberately. A project that
*deliberately* chose `!128,128` is indistinguishable from a backfilled one and
loses that record. And those 50 projects now track the platform default, so if
`RestrictedView.default` ever changes they follow it rather than staying at 128
— which is the point of having a central default, and the reason it stays a
domain constant changed by a reviewed release rather than a config knob.

### 4. What the page looks like

Mockups built from the real component source — Roboto, `#336790` (the blue
palette's 700, `$primary`), 4px radius, Material fill fields, and the
component's own spacing. Copy is verbatim from `en.json`; bounds are the real
ones (percentage 1–99, absolute width 128–1024).

**Today**, in the state 48 of the 62 projects actually load in — an explicit
`!128,128`, so the radio lands on *Restrict image size*:

![The Image Settings page today](2026-09-11-01-refactor-asset-access-decision-assets/image-settings-today.png)

The preview is the component's own `app-image-display-absolute`, which measures
against a 2048px reference image. `!128,128` is the 18×12 block in the corner —
the clearest picture of how much an absolute cap removes from a large scan, and
of why a small original would be restricted far less in relative terms.

**After**, for a project with nothing stored:

![The Image Settings page after the change](2026-09-11-01-refactor-asset-access-decision-assets/image-settings-after.png)

The line under the radio is **new markup**, not a binding change: the template's
only expandable region is gated on `RestrictImageSize`, so there is no slot for
it today.

**What each project loads**, with the counts this change produces:

![Load states for the three stored shapes](2026-09-11-01-refactor-asset-access-decision-assets/image-settings-load-states.png)

**Which verb each radio issues**, including the response-shape unification:

![Save flow per radio](2026-09-11-01-refactor-asset-access-decision-assets/image-settings-save-flow.png)

## Technical Considerations

- **Release coupling.** SIPI must understand `stream` before dsp-api emits it,
  and dsp-api must stop emitting bare `restrict` for non-images before the
  wave-2 image rolls. Both constraints are satisfied by one train: release SIPI
  first, bump the two per-arch `oci.pull` digests in dsp-api's `MODULE.bazel`
  within the dsp-api PR, deploy dsp-api and the pinned image together, and sync
  ops-deploy. Deploying either alone breaks the other: an old SIPI meets an
  unknown `stream`, `valid_permission` refuses it inside the Lua runtime before
  `permission_from_str` is ever reached, and the request answers 500; a new SIPI
  meets an old hook's bare `restrict` and answers 403.
- **The contract can change atomically.** Both production consumers of
  `/admin/files` live in the dsp-api repo — `FetchAssetPermissions.scala:41` and
  `modules/sipi/scripts/sipi.init.lua:25`, the latter shipped into the SIPI image
  by `//modules/sipi`. No compatibility window and no shim is needed. It is still
  a breaking change to a published admin endpoint: `feat(admin)!` plus a
  CHANGELOG entry.
- **Clearing needs no bespoke delete.** `AbstractEntityRepo.save` is a `MODIFY`
  that deletes every `entityProperties` triple for the subject and inserts
  `toTriples(entity)` (`AbstractEntityRepo.scala:186-203`). Emitting no
  restricted-view triple for `None` therefore removes a previously stored one.
  Non-obvious enough to state, so nobody adds a redundant delete query.
- **`MediaKind` must be total.** Derive it from the file value's RDF class over
  the full `knora-base` set — `StillImageFileValue`, `StillImageExternalFileValue`,
  `StillImageVectorFileValue`, `MovingImageFileValue`, `AudioFileValue`,
  `DocumentFileValue`, `ArchiveFileValue`, `TextFileValue`, `DDDFileValue`.
  Totality is split: the compiler enforces it from `MediaKind` onward, while the
  IRI-to-`MediaKind` step is a string lookup the compiler cannot check, so it
  fails closed and is covered by a test enumerating the ontology's classes.
  Only `StillImageFileValue` is restrictable:
  `StillImageVectorFileValue` (SVG) is served raw through `/file` and cannot be
  clamped, and `StillImageExternalFileValue` never reaches SIPI.
- **`Option` in a positional constructor.** `restrictedView` is the 9th
  positional field of `KnoraProject` (`KnoraProject.scala:40`). Every
  construction site changes: `KnoraProjectService.createProject:81`,
  `KnoraProjectRepo.builtIn.makeBuiltIn:34-51`, `TestDataFactory`, and the ttl
  fixtures.
- **Pinned SPARQL.** `KnoraProjectRepoLiveSpec:261-305` pins the generated
  CONSTRUCT/OPTIONAL query and `FileValuePermissionsQuerySpec` pins
  `getQueryString` exactly. Both change here and must be reviewed, not accepted,
  per `CONVENTIONS.md:75`.
- **Dual-triple projects exist in fixtures.** `admin-data.ttl:84-85` and
  `:180-181` store both a size and a watermark; today `size.orElse(watermark)`
  silently prefers size. That precedence must be restated and tested under
  `Option`, not inherited by accident.
- **Migration ordering.** Rejecting `pct:100` in `Size.from` makes a stale triple
  unreadable. The `KnoraBaseVersion` bump (`package.scala:17`, 55 → 56) is what
  stops the repository serving before the plugin has run, so it is load-bearing.
  `UpgradePluginPR3112` is the same problem shape and the template to copy.
- **Rollback after the version bump.** Rolling the dsp-api binary back to
  pre-migration code against a version-56 triplestore is unverified; check it
  before relying on rollback as a safety net.
- **dsp-app's dual clients.** The read path is a hand-rolled `HttpClient.get`
  typed against the hand-written dsp-js model (`project-api.service.ts:64-68`);
  the write path uses the generated OpenAPI client. `isDefault` arrives only
  through the generated client, so the read path moves onto it. The dsp-js copies
  are then unused by the app but are exported from `libs/dsp-js/src/index.ts:66-67`;
  removing them is a dsp-js surface decision and is **not** in scope.
- **A denied archive needs no dsp-app change.** `ArchiveComponent` renders
  `<app-file-representation>`, which fetches SIPI's `knora.json` beside the
  `/file` URL (`file-representation.component.ts:62`). A `denied` decision
  answers that 404, and the existing `catchError` sets `failedToLoad` (`:64-69`),
  rendering the standard representation-error card instead of the filename and
  download button. This is the same path a no-view-permission asset already
  takes, so the archive denial degrades gracefully with no UI work — asserted
  rather than assumed, below.
- **The endpoint has no prose documentation.** `grep -rn "admin/files" docs/`
  in dsp-api returns nothing; the only `permissionCode` prose is
  `docs/03-endpoints/api-admin/permissions.md`, which documents the
  administrative permissions API and its ACL codes — a different concept this
  plan leaves untouched. The breaking change needs the CHANGELOG entry and
  nothing else.
- **The browser test runs unattended.** No human login is needed: `cy.login`
  POSTs to `{apiUrl}/v2/authentication` and writes the token to
  `localStorage.ACCESS_TOKEN` (`cypress/support/commands/auth-commands.ts:41-56`),
  and the credentials are committed in `cypress/fixtures/user_profiles.json`
  (`root`/`test`). The stack recipe is the one CI uses
  (`.github/workflows/ci.yml:216-231`):
  `just init-db-test` then `docker compose up -d sipi ingest api` in dsp-api,
  `./modules/webapi/scripts/wait-for-api.sh`, `npx nx run dsp-app:serve` on 4200,
  then `npx cypress run --browser chrome --spec …`. All of it is local docker and
  scriptable, so it is agent work, not an operator action.
  For the interactive pass, `/eng:test-browser` drives `agent-browser` over Bash
  (installed here, 0.36.0) — it needs no MCP browser tool. It detects Angular
  from `angular.json` and defaults to port 4200, which matches dsp-app. Its
  file-to-route mapping expects `*-routing.module.ts`, which an nx monorepo does
  not lay out predictably, so **pass the Image Settings route explicitly** as an
  argument rather than relying on derivation from `git diff`.
- **The percentage field already cannot emit `pct:100`.** Its mask is
  `minMaxInputMask(1, 99)` (`image-settings.component.html:24`), so the "Off"
  radio was the *only* path by which dsp-app could produce the value Phase 5
  rejects. Removing that radio closes the source before the API closes the door,
  which is why Phase 5's rejection cannot break the UI.
- **Deploy-gated CI.** dsp-app's `check-openapi-sync` runs against
  `api.dev.dasch.swiss` and is red by design between the dsp-api merge and the
  dev deploy. Say so in the dsp-app PR description.
- **Out of scope, deliberately.** `ViewRestrictionsService:193-199` and its
  byte-for-byte sibling `ViewRestrictionsByPropertyService:87-93` both report
  code 1 as "RestrictedView" for every item type in the project-settings audience
  matrix. They share the assumption but are a reporting surface, not a serving
  decision, and both stay untouched.

## Implementation Approach

### Implementation Phases

#### Phase 1: sipi — the `stream` permission type

_Execution: opus, medium (a new permission type is a vocabulary addition with a deliberately weak initial enforcement; the boundary it draws matters more than the code)._

- [x] Add `Stream = 7` to **both** sides of the hand-mirrored FFI enum — `SipiPermType` in `src/server/rust/src/ffi.rs:352-360` and `sipi_ffi.h:236` — and extend the drift `static_assert`s (`sipi_ffi.h:458-464`) and their paired Rust guard (`ffi.rs:362-364`). **Append; never renumber**, or an existing permission is silently misread across the seam
- [x] Add `"stream"` to `permission_from_str` (`src/server/rust/src/routes.rs:720-730`)
- [x] `/file` serves a `stream` decision exactly as it serves `allow`. Do **not** add a disposition override: `content_disposition` (`routes.rs:2351-2367`) already emits `inline` for every response and SIPI never sends `attachment`
- [x] Count `stream` decisions (a counter alongside the existing metrics) so the population is observable before any enforcement is chosen — this is the only day-one behavioural addition
- [x] `stream` joins `Allow | Restrict` in the `Iiif | KnoraJson` access predicate (`routes.rs:462-467`) so metadata is served
- [x] `stream` on the IIIF image route behaves as `allow` — it carries no pixel restriction. dsp-api never emits it for a still image, so this branch exists for generic SIPI deployments; record that so it is never mistaken for a restriction
- [x] `UBIQUITOUS_LANGUAGE.md`: add **Stream** to the permission-types table, stating that it expresses intent and is not a security boundary today
- [x] Update every place that hardcodes the count or enumerates the types — `UBIQUITOUS_LANGUAGE.md:117` ("The seven valid permission-type strings"), its **Permission** row's list at `:108`, `CONTEXT.md:54` ("the seven Permission types"), and `docs/src/lua/index.md:446`. Seven becomes eight in prose, not only in a table
- [x] `CONTEXT.md`: record the VRE's **Original** / **Derivative** vocabulary as upstream language, and that it is a lifecycle rather than a synonym for Preservation File / Service File / Access File
- [x] `docs/src/lua/index.md`: document the new return value for `pre_flight`
- [x] ADR-0027 (0026 is the highest today) recording why `stream` exists, that it changes no served byte on day one, what it guarantees (nothing against a determined capture), and which tightenings are reserved to SIPI versus which need a different derivative from ingest
- [x] e2e: a `stream` decision returns 200 on `/file` and 200 on `info.json`, and is byte-identical to what `allow` returns for the same asset — the e2e that pins "day one changes nothing" so a later tightening has to break it deliberately
- [x] Correct the wave-2 plan's Phase 5 coordination note, which claims dsp-api must default a size in `sipi.init.lua`

#### Phase 2: dsp-api — optional stored setting

_Execution: sonnet, medium (the change is named; the work is a wide but mechanical fan-out plus one pinned query)._

- [ ] `KnoraProject.restrictedView` becomes `Option[RestrictedView]` (`KnoraProject.scala:40`)
- [ ] `KnoraProjectRepoLive.getRestrictedView` returns `Option` — drop `.getOrElse(RestrictedView.default)` at `:100`
- [ ] `KnoraProjectRepoLive.toTriples` emits no restricted-view triple for `None` (`:138`, emission at `:154-156`)
- [ ] Restate the dual-triple precedence explicitly (size wins over watermark) rather than leaving it a side effect of `orElse`
- [ ] A stored `projectRestrictedViewWatermark false` resolves to `None` — an explicitly-off watermark is "nothing configured", not a watermark restriction. `UpgradePluginPR3112` should have cleared these, but the state stays representable in RDF, so the mapper must be total over it rather than trusting the earlier migration
- [ ] `KnoraProjectRepo.builtIn.makeBuiltIn` passes `None` (`:34-51`)
- [ ] `KnoraProjectService.createProject` stores `None` for a new project (`:81`)
- [ ] `KnoraProjectService.setProjectRestrictedView` keeps mapping `Watermark(false)` to the default, wrapped as `Some` (`:49-56`)
- [ ] Add `KnoraProjectService.clearProjectRestrictedView`, persisting `None`
- [ ] Update every remaining `KnoraProject(...)` construction site, including `TestDataFactory` and the ttl fixtures
- [ ] Update the pinned query in `KnoraProjectRepoLiveSpec:261-305` and review the diff rather than accepting it
- [ ] Unit test: neither triple round-trips as `None`; a size round-trips as `Some(Size)`; saving `None` writes neither predicate
- [ ] Unit test: a project storing both a size and a watermark resolves to the size, matching `admin-data.ttl:84-85`
- [ ] Unit test: a project storing only `watermark false` resolves to `None` and inherits the default

#### Phase 3: dsp-api — admin API surface for the optional setting

_Execution: sonnet, medium (endpoint shapes are specified; the three-tier split is a convention to follow, not a decision)._

- [ ] Add `isDefault: Boolean` to `ProjectRestrictedViewSettingsGetResponseADM` (`ProjectsMessagesADM.scala:131-139`)
- [ ] Resolve the effective settings in `ProjectRestService.getProjectRestrictedViewSettings*` (`:228-249`): `getOrElse(RestrictedView.default)`, with `isDefault = restrictedView.isEmpty`
- [ ] Add the two DELETE endpoints (by IRI and shortcode) to `ProjectsEndpoints.Secured` (`:87-117`), returning the GET response shape
- [ ] Change the two POST endpoints to return the GET response shape as well, and retire `RestrictedViewResponse` (`ProjectsEndpointsRequestsAndResponses.scala:66-78`) so one shape serves all three verbs
- [ ] Wire them in `ProjectsServerEndpoints` (`:33-43`) and register in `AdminApiServerEndpoints`
- [ ] Add the REST-service methods behind the same `ensureSystemAdminOrProjectAdmin*` auth as the POSTs (`ProjectRestService.scala:251-261`)
- [ ] Keep `SetRestrictedViewRequest.toRestrictedView`'s "exactly one of" validation unchanged
- [ ] E2E in `AdminProjectsEndpointsE2ESpec`: GET on an unset project reports `isDefault: true`; POST then GET reports `false`; DELETE then GET returns to `true`
- [ ] E2E: DELETE is refused for a non-admin

#### Phase 4: dsp-api — asset access as a decision

_Execution: opus, high (this replaces a cross-service contract, and the media-kind mapping and the original/derivative split each have a security consequence if they are got wrong)._

- [ ] Add `MediaKind`, `OriginalAccess`, `DerivativeAccess` and `AssetAccess` under `slice/admin/domain/model/`, with `AssetAccess.from` as the single total policy function and a private constructor so no inconsistent pair can be assembled
- [ ] Map every `knora-base` file value class IRI to a `MediaKind`. The compiler guarantees totality only on the Scala side — `MediaKind => AssetAccess` is exhaustive — but the IRI arrives as a string from the triplestore, so an unmapped class must fail closed (deny) and be counted, never fall through to a permissive default
- [ ] Test that enumerates the file value classes declared in `knora-base.ttl` and asserts each maps to a `MediaKind`; this, not the compiler, is what catches a class added to the ontology later
- [ ] `FileValuePermissionsQuery.build` also selects the `rdf:type` of `?currentFileValue` — the newest version, not `?fileValue` — and constrains it to the known file value classes so a multi-typed resource cannot multiply rows (`:32-74`). `AssetPermissionsResponder` takes `result.getFirstRow`, so extra rows would make the media kind a nondeterministic pick
- [ ] Update the pinned `getQueryString` assertions in `FileValuePermissionsQuerySpec`
- [ ] `AssetPermissionsResponder` returns `AssetAccess`; delete `PermissionCodeAndProjectRestrictedViewSettings` and `buildResponse`'s code-1 branch (`:55-72`)
- [ ] New response DTO, codecs and `FilesEndpoints` output type; the ACL code leaves the payload
- [ ] `AssetPermissionsCache` carries the new value type, and its key is confirmed to distinguish assets so a per-media-kind decision can never be served for a different file
- [ ] Rewrite `modules/sipi/scripts/sipi.init.lua`'s `pre_flight`: translate the `derivative` literal to a SIPI *Permission* — `full` → `allow`, `clamped` → `restrict` passing `size` through verbatim as the IIIF size string and substituting the hook's own configured path when `watermark` is `true`, `stream` → `stream`, `denied` → `deny` — with no permission arithmetic and no media knowledge
- [ ] An unrecognised `derivative` literal denies **and** increments a counter, so a vocabulary change never degrades to silent denials
- [ ] `sipi.init.lua` reads only `derivative` and `FetchAssetPermissions` reads only `original`; assert that separation in review, since nothing on the wire enforces it
- [ ] `FetchAssetPermissions` decodes the new shape (`:44-46`, DTO `PermissionResponse` at `:54`) and `ProjectsEndpointsHandler` gates on `original == "grant"` instead of `permissionCode >= 2` (`:125`)
- [ ] Bump both per-arch SIPI `oci.pull` digests in `MODULE.bazel` — blocked until the SIPI release named under Operator Actions exists; stop and hand back if it does not
- [ ] Integration test in `AssetPermissionsResponderSpec`, one case per `MediaKind` × {no permission, RV, V}: a still image yields `clamped` carrying its IIIF size string (or `watermark: true`); video, audio, document, text, vector and 3D yield `stream`; an archive yields `denied`; every `RV` case yields `original: withhold`
- [ ] Integration test: dsp-ingest's original-download gate still refuses every `RV` asset, of every media kind
- [ ] `SipiIT` asserts `knora.json` answers 404 for a `denied` decision — the response dsp-app's `catchError` relies on to show its representation-error card for an `RV` archive without any dsp-app change
- [ ] `SipiIT` cases for the relay itself — one per decision kind. Every existing case stubs only the equivalent of `denied` or `full`, so `restricted` and `stream` are untested today
- [ ] Update `AdminFilesE2ESpec` and `TestAdminApiClient:60` to the new shape
- [ ] CHANGELOG under a breaking marker: the endpoint returns an access decision, not a permission code, and an `RV` archive is no longer served

#### Phase 5: dsp-api — make `pct:100` unrepresentable

_Execution: sonnet, medium (`UpgradePluginPR3112` is a direct precedent for both the plugin and its spec)._

- [ ] `RestrictedView.Size.from` rejects `pct:100` — the percentage pattern becomes `pct:[1-9][0-9]?$` (`RestrictedView.scala:34`)
- [ ] Unit test in `RestrictedViewSpec`: `pct:100` rejected, `pct:99` and `!128,128` still accepted
- [ ] Add `UpgradePluginPRxxxx` deleting two classes of `knora-admin:projectRestrictedViewSize` triple in the admin named graph, following `UpgradePluginPR3112`: every `"pct:100"` (2 projects) and every `"!128,128"` (48 projects). The second is the value `UpgradePluginPR3112` backfilled; clearing it restores "nothing configured" and changes no served byte, since `RestrictedView.default` is the same `!128,128`
- [ ] Plugin spec: a `"!128,128"` triple is removed, a `"pct:100"` triple is removed, and `"!512,512"`, `"pct:1"` and a watermark triple are all untouched
- [ ] Register it in `RepositoryUpdatePlan.makePluginsForVersions:20-43` as version 56
- [ ] Bump `KnoraBaseVersion` 55 → 56 (`package.scala:17`) and the version in `knora-base.ttl`
- [ ] Plugin spec following `UpgradePluginPR3112Spec`: a `pct:100` triple is removed, a `pct:30` triple untouched, a watermark triple untouched
- [ ] CHANGELOG: the Image Settings page now shows **Default** for any project with no explicit restriction, which after the migration is 50 of 62. The effective restriction is unchanged; projects that had "Off" selected would serve the default `!128,128` to `RV` users on still images, though neither currently has any

#### Phase 6: dsp-app — "Default" replaces "Off"

_Execution: sonnet, medium (every file and line is named; the only judgement is test shape, and sibling components give the pattern)._

- [ ] Rename `ImageSettingsEnum.Off` to `Default` (`image-settings.component.ts:20-24`)
- [ ] Rewrite `getImageSettings` as a whole (`:119-145`), not only the `pct:100` test at `:132`: the new payload always carries populated `settings`, so the sparse-payload handling at `:125-131` would fall through to `setRestrictedSize` and render the default state as "Restrict image size: 128". Select from `isDefault` first, then `watermark`, then `size`
- [ ] `getSizeForRequest` no longer returns `'pct:100'`; the `Default` branch leaves the write payload entirely (`:162-168`)
- [ ] `onSubmit` calls DELETE when `Default` is selected, POST otherwise (`:91-103`)
- [ ] `onSubmit` handles the error case for both verbs — today `.subscribe()` has no error callback, so a failed save is silently swallowed
- [ ] `hasChanges` compares the tri-state against the loaded state rather than stringifying two different shapes; today a fresh load leaves `currentSettings` undefined, so Submit is enabled before the user touches anything (`:65-67`)
- [ ] Decide and implement what switching radios without saving does to a half-typed value — today only typing clears the sibling field
- [ ] Move `ProjectApiService.getRestrictedViewSettingsForProject` onto the generated client (`project-api.service.ts:64-68`) and collapse `currentSettings` from a union to the single response type all three verbs now return (`:50`)
- [ ] Template: the first radio reads the new key and shows the inherited value (`image-settings.component.html:6-8`)
- [ ] Replace `pages.project.imageSettings.off` with `.default` in `en`, `de`, `fr` and `it` (each at line 168)
- [ ] Run `npm run update-openapi` and commit the refreshed `dsp-api_spec.yaml` — blocked until the dsp-api dev deploy named under Operator Actions has landed; stop and hand back if it has not
- [ ] Add `image-settings.component.spec.ts` — none exists today: `Default` renders when `isDefault` is true, `Default` issues a DELETE, a size issues a POST, `pct:100` is never sent, Submit is disabled on a fresh load, a failed save surfaces an error
- [ ] Add `image-settings.component.stories.ts` with a `play()` assertion per the repo's story convention — none exists today
- [ ] Bring the local stack up and exercise the changed screen with `/eng:test-browser` — navigate, screenshot each radio state, check the console for errors, and confirm the DELETE and POST actually fire — before writing the spec, so the spec encodes observed behaviour rather than assumed behaviour
- [ ] Add `cypress/e2e/system-admin/image-settings.cy.ts` driving the real screen against a local stack: load a project with a stored size (radio lands on Restrict image size), switch to Default and submit (a DELETE is issued), reload (radio lands on Default, `isDefault: true`), switch to Watermark and submit, and assert no request body ever contains `pct:100`
- [ ] **Register the new spec in the CI matrix** (`.github/workflows/ci.yml:186`, the `rest` runner). System-admin specs are enumerated individually, not globbed — a spec that is not listed never runs in CI and the coverage is silently zero
- [ ] Decide what the Default radio displays: the inherited value as static text beside the label, or nothing. The template has no slot for it today — the preview children (`app-image-display-ratio` / `app-image-display-absolute`) render only under `RestrictImageSize` (`image-settings.component.html:45-48`), so "show the inherited value" is new markup, not a binding change
- [ ] State in the PR description that `check-openapi-sync` is red by design until the dsp-api dev deploy lands

## Operator Actions

**Not agent work.** Everything below needs production access, a deploy, or a
decision that is not the implementer's to make. None of it belongs in a phase
checklist, and an agent must stop and hand back rather than attempt any of it.
**Surface this section in the PR description and on the Linear issue** for the
phase it gates, so the blocking step is visible to whoever has to perform it.

### Go / no-go — answered 2026-09-12 against stage (the prod mirror)

**Result 1 — `RV` on non-still-image file values:** `AudioFileValue` 2388,
`TextFileValue` 9, `DocumentFileValue` 2, and **`ArchiveFileValue` 0**.
`MovingImageFileValue`, `StillImageVectorFileValue` and `DDDFileValue` are all
zero. So the archive denial costs nothing — no project loses access — and the
`stream` population is ~2,399 file values, overwhelmingly audio. Those are
exactly the assets that would 403 under wave-2 without this change. (The query
counts every file value version, so the audio figure is an upper bound on current
assets; it does not change either conclusion.)

**Result 2 — stored restricted-view shapes:** all **62** projects carry a stored
triple; none is unset. Two hold `pct:100`, four hold a watermark, the rest hold a
size (`!128,128` in 51 of them, plus `!512,512` ×2, `!1024,1024`, `pct:1` ×2,
`pct:13`, `pct:80`, `pct:99`). The `!128,128` majority is *stored*, not defaulted
— `UpgradePluginPR3112` backfilled a default size wherever neither triple was
set. See the open decision below.

**Result 4 — `RV` still-image dimensions:** 0 images with a long edge ≤128, 294
below 1024, 94% above 2048. Recorded under §3 above, where it settles whether
the default should be an absolute box or a percentage.

**Result 3 — which projects actually have `RV` assets, by class:** only eleven.
Ten hold `RV` still images (`dRHtOC…` 31613, `ExIK4B…` 18613, `0102` 17949,
`ZjLtCB…` 17880, `0107` 15882, `vNYc6-…` 686, `Y9hvow…` 320, `0118` 272, `0115`
14, `0111` 1, `yTerZG…` 1) — all keep working unchanged as `clamped`. The entire
non-image population is **one project, `082A`, with 2388 audio file values**,
plus 2 documents in `ExIK4B…` and 9 text files in `yTerZG…`.

Crucially, **neither `pct:100` project appears at all** — they hold no `RV` file
value of any class. Deleting their triple in Phase 5 therefore changes no served
byte; it only corrects what the GUI shows.

The queries below are kept so the counts can be refreshed before the deploy.

### How the censuses were run

Neither census changes a line of the implementation. The policy is decided, and
the code is identical whether prod holds zero such assets or thousands. They
exist to tell you how much behaviour is about to move, and whether the open
decision below needs answering first.

- **Census: non-still-image `RV` assets.** Count prod file values that are not
  `StillImageFileValue` whose `hasPermissions` grants `RV` to `UnknownUser` or
  `KnownUser`, broken down by media kind. Sizes the `stream` population, and —
  the part that actually gates the rollout — **counts the `RV` archives that will
  stop being served**. Every one is a project whose users lose access, so this
  count decides whether the release needs individual project warnings or only a
  release note.
- **Census: restricted-view triple shapes.** Count `pct:100`, bare
  `watermark=false`, and dual size+watermark projects on prod. Tells you how many
  projects the migration will move. Every one of those shapes is handled by the
  code unconditionally, so a surprising count is a reason to look again, not a
  reason to change the plugin.

`dsp vre sparql query` is the raw SPARQL passthrough (`cargo install dsp-cli`;
the binary is `dsp`). It needs a **SystemAdmin** session — `dsp auth login -s
stage` — which is why this stays an operator action: re-login does not help an
account that merely lacks the role, and the passthrough is not enabled on every
deployment. **Target stage, never prod** — stage mirrors prod, and the settings
census above matched a prod reading exactly.

```sparql
# Census 1 — RV on non-still-image file values, by class
SELECT ?cls (COUNT(?v) AS ?n) WHERE {
  ?v a ?cls ; knora-base:hasPermissions ?perm .
  FILTER(CONTAINS(STR(?cls), "FileValue") && ?cls != knora-base:StillImageFileValue)
  FILTER(CONTAINS(?perm, "RV knora-admin:UnknownUser") ||
         CONTAINS(?perm, "RV knora-admin:KnownUser"))
} GROUP BY ?cls

# Census 2 — stored restricted-view shapes, by project
SELECT ?p ?size ?wm WHERE {
  ?p a knora-admin:knoraProject .
  OPTIONAL { ?p knora-admin:projectRestrictedViewSize ?size }
  OPTIONAL { ?p knora-admin:projectRestrictedViewWatermark ?wm }
  FILTER(BOUND(?size) || BOUND(?wm))
}
```

### Blocking inputs — a named checkbox cannot run without these

- **A released SIPI image digest.** Phase 4 pins two per-arch `oci.pull` digests;
  the release that produces them is a human step after Phase 1 merges.
- **A dsp-api dev deploy.** Phase 6's `npm run update-openapi` reads the live dev
  spec, so it cannot run until the dsp-api change is deployed there.

### Verification before rollout

- Verify on stage, using the projects the census named rather than arbitrary
  ones: an `082A` audio file plays (that project holds all 2388 `RV` audio
  values), an `ExIK4B…` document opens, dsp-ingest refuses both originals, and a
  `dRHtOC…` or `0102` still image still renders clamped.
- Verify the GUI after the migration: the two former `pct:100` projects and a
  sample of the 48 former `!128,128` projects all load as **Default**, while one
  of the twelve keepers (say a `!512,512` or a watermark project) still loads
  with its explicit setting.
- Verify a still image in a cleared project that *has* `RV` assets — `0102` or
  `0107` — renders exactly as before, since `None` and a stored `!128,128`
  resolve identically.
- Verify a pre-Phase-5 dsp-api starts against a version-56 triplestore, or record
  that rollback requires a triplestore restore.

### Communication before rollout

- Release note only. The census found **zero** `RV` archives, so no project loses
  access and no per-project warning is needed. Note the change anyway, because
  the rule is new even where it currently binds on nothing.

### Deploy sequence

- **The digest bump is the wave-2 rollout.** `MODULE.bazel` pins SIPI `v8.0.0`
  today, which predates wave-2. Phase 4 moves it to the release carrying
  `stream`, so that one bump also ships every wave-2 change — including S2-09,
  which refuses a bare `restrict`. Rolling this dsp-api release *is* wave-2
  Phase 13; they are one event, not two in sequence, and wave-2's verification
  and comms belong to this train.
- Deploy dsp-api, then dsp-app, then sync ops-deploy. There is no separate SIPI
  deploy step: `knora-sipi` is a thin overlay that copies the Lua scripts onto
  the upstream image, and ops-deploy pins one dsp-api version that carries both.
- Close DEV-7155 as superseded. PR #4329 is **not** closed — its branch is reused
  for this work, reset onto `main` so the superseded commit does not ship.

### Decisions still open

- **How strictly `stream` must be enforced.** Open legal interpretation. The
  design reserves the tightening to SIPI, but the strongest measures need a
  different derivative from ingest.

## Acceptance Criteria

- [ ] `/admin/files/…` returns no ACL code, and neither consumer performs permission arithmetic
- [ ] A project that never configured a restricted view stores no restricted-view triple, and `GET .../RestrictedViewSettings` reports the default with `isDefault: true`
- [ ] `DELETE .../RestrictedViewSettings` returns a project to that state and is refused for a non-admin
- [ ] `pre_flight` never returns a `restrict` decision without a real size or watermark, and never returns one for a non-still-image asset
- [ ] `SipiIT` asserts one case per decision kind, including `stream`
- [ ] An `RV` user can play audio and video and open PDFs, including anonymously, and dsp-ingest refuses their originals — both asserted by tests, not only by the stage walkthrough
- [ ] An `RV` archive is refused on both routes — SIPI's `/file` and dsp-ingest's `/original` — asserted by tests
- [ ] A `clamped` decision carries an IIIF size string or `watermark: true`, never a bare number and never a filesystem path
- [ ] Adding a `MediaKind` fails to compile until `AssetAccess.from` handles it, and an RDF class with no `MediaKind` mapping fails closed at runtime rather than defaulting to a permissive decision
- [ ] `pct:100` is rejected by the API and absent from the triplestore, and no project stores a `projectRestrictedViewSize` equal to the platform default
- [ ] dsp-app offers Default / Watermark / Restrict image size, never sends `pct:100`, disables Submit until something changes, and reports a failed save
- [ ] A Cypress spec drives the real Image Settings screen end to end against a local stack, and is listed in the CI spec matrix so it actually runs

## Dependencies & Risks

| Risk | Severity | Mitigation |
|---|---|---|
| A new `sipi.init.lua` runs against an older SIPI binary, so `stream` is refused with a 500 | Medium | Not reachable in a deployed environment: `knora-sipi` is a thin overlay that copies the Lua scripts onto the upstream SIPI image (`modules/sipi/BUILD.bazel`), and the two `oci.pull` digests in `MODULE.bazel` are the sole source of the SIPI version, so hook and binary ship as one artifact under one dsp-api version. The exposure is dev and CI, between the hook rewrite and the digest bump inside this PR — which is where it bit: 13 `//modules/test-e2e:test` failures until the digests land |
| A stale `pct:100` triple outlives the migration and makes a project unreadable once `Size.from` rejects it | High | The `KnoraBaseVersion` bump blocks serving until the plugin runs; it must not be skipped |
| `stream` is mistaken for an enforcement guarantee | Medium | ADR-0027 and the glossary state plainly that it expresses intent and defeats no capture today, and the e2e pins `stream` as byte-identical to `allow` so the absence of enforcement is asserted, not assumed |
| A media kind is mapped wrongly and a restricted image is served as `stream` | High | `AssetAccess.from` is one total function with an exhaustive match, covered per media kind in `AssetPermissionsResponderSpec` |
| A file value class added to `knora-base` later has no `MediaKind`, and the compiler cannot catch it | Medium | Unmapped IRIs fail closed and are counted; the ontology-enumerating test fails when a class is added |
| ~~SIPI wave-2 rolls before this train lands~~ | Closed | Not separable: `MODULE.bazel` pins SIPI `v8.0.0`, so wave-2 reaches a deployment only via Phase 4's digest bump. This train *is* the wave-2 rollout |
| The `Option` change touches a positional constructor across 60+ files | Medium | The compiler catches type changes; the two pinned SPARQL tests catch mapper changes |
| Between the dsp-api and dsp-app deploys, the old UI POSTs `pct:100` and gets a 400 | Low | Ship both in one release train; the failure is a visible error on one admin action, not data loss |
| ~~`RV` archives exist in production and lose access on rollout~~ | Closed | Measured on stage 2026-09-12: zero `RV` archives. Re-run the census before the deploy to confirm it still holds |
| ~2,399 `RV` audio/text/document file values currently reach SIPI as `restrict`, 2388 of them audio in the single project `082A` | High | This is the population wave-2 would 403 today; it is the change's main justification. The `SipiIT` `stream` case proves the path, and the stage walkthrough should play an `082A` audio file specifically |

## Success Metrics

- Zero `projectRestrictedViewSize "pct:100"` triples in production
- Zero 403s on `/file` for `RV` users after the wave-2 rollout, except archives, which are denied by design
- Tightening streaming enforcement requires a change in the sipi repo only, up to the point where the served derivative itself must change
- No project and no caller can express a restriction that does not restrict

## Alternative Approaches Considered

**Keep `permissionCode` and add fields beside it.** Smaller, and it was this
plan's first shape. Rejected: both callers still derive policy from an ACL code,
so the next media type or the next artifact repeats the bug.

**Answer `permissionCode = 2` for a non-image `RV` asset.** The obvious way to
express "no restriction applies", and wrong: dsp-ingest gates original-file
download on `permissionCode >= 2` (`ProjectsEndpointsHandler.scala:125`), so it
would hand the unprocessed original to every `RV` user.

**Extend the ACL code space with a new value.** Rejected: those codes are the
permission arithmetic vocabulary; a serving concept inside them would break
`Permission.ObjectAccess.from(code)`.

**Model non-image `RV` as `full` rather than `stream`.** Preserves behaviour
identically, but every future tightening of streaming would begin with a dsp-api
change and a cross-repo deploy.

**Treat `RV` on every non-image as deny.** Fail-closed, and what wave-2 currently
does by accident. Rejected as a blanket rule: it removes anonymous audio, video
and PDF access on every project using the standard `RV knora-admin:UnknownUser`
pattern — a product tightening, not a hardening detail. Archives are the one
exception, decided on their own merits: they have no in-place consumption and no
transcode, so serving one is handing over the original by another route.

**Configurable platform default.** Rejected: a security-relevant default that can
diverge between environments makes restricted-view behaviour irreproducible, and
per-project settings already cover real variation.

**Clear the setting via an empty POST body.** Matches the existing
`setResourceSideLegalInfo` idiom. Rejected: an empty `SetRestrictedViewRequest`
is ambiguous between "clear" and "malformed".

## References

- `docs/specs/2026-09-04-01-fix-sipi-security-hardening-wave-2-plan.md` — Phase 5 (S2-04, S2-09, S2-16) and the Phase 13 rollout gate
- `src/ffi/cpp/serve_image.cpp:590` — the no-op restrict refusal
- `src/server/rust/src/routes.rs:713` — `restrict` on `/file` answers 403
- `UBIQUITOUS_LANGUAGE.md:108,117` — the permission-type table and prose count `stream` joins; `CONTEXT.md:54` and `docs/src/lua/index.md:446` repeat them
- `docs/adr/0017-extensibility-lua-and-rust.md` — Lua and Rust extensions are both first-class
- `dasch-specs/dasch-context/cross-repo-conventions.md` — deploy-gate topology
