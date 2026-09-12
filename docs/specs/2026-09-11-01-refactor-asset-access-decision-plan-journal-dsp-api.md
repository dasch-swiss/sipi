# Journal — dsp-api phases (2, 3, 5, 4)

Plan: `docs/specs/2026-09-11-01-refactor-asset-access-decision-plan.md`
Target repo: `/Users/subotic/_github.com/dasch-swiss/dsp-api`
Branch: `feature/dev-7155-dsp-api-default-a-restrict-size-in-sipiinitlua-before-sipi`
Base: `75414c1e71de6a959168f82c24dc07b725ecd94a`

This orchestrator owns **Phase 2, 3, 4, 5 only**. Phase 1 (sipi) and Phase 6 (dsp-app) belong to
other runs. The shared plan file's checkboxes are **not** edited here — completed items are recorded
below, quoted verbatim, and the supervising session reconciles the plan at ship time.

Execution order: Phase 2 -> Phase 3 -> Phase 5 -> Phase 4.

## Result

Three commits on the branch, none pushed:

| Commit | Phases | Subject |
|---|---|---|
| `912b7c50e` | 2 + 3 | `feat(admin)!: make the project restricted view an optional setting` |
| `ae66b62ef` | 5 | `feat(admin)!: reject pct:100 and clear the restricted view sizes nobody chose` |
| `6e06b0d2f` | 4 | `feat(admin)!: return an asset access decision instead of a permission code` |

Every checkbox in Phases 2, 3, 4 and 5 is done except one: the `MODULE.bazel` SIPI digest bump, which
is an operator action blocked on a SIPI release that does not exist yet. `MODULE.bazel` is untouched.

## Round 1

### Orientation findings (before any edit)

- `RepositoryUpdatePlan.makePluginsForVersions` ends at `versionNumber = 55`
  (`MigrateRemoveProjectStatus`), and `package.scala:17` holds `KnoraBaseVersion: Int = 55`.
  **56 is the correct next version**, as the plan assumed. No correction needed.
- The upgrade plugin is named `UpgradePluginPR4329` after this PR, per the brief.
- The nine concrete `knora-base` file value classes are already enumerated in
  `OntologyConstants.KnoraBase.FileValueClasses` — but that set also contains the **abstract**
  `FileValue` superclass, so it is not directly usable as the "restrictable kinds" set.
  `StillImageAbstractFileValue` is abstract and not in the set.
- `test_data/project_data/admin-data.ttl:84-85` and `:180-181` both store a size **and** a
  watermark, as the plan states.

### Local build workaround (not a code change)

`bazel build` fails on this machine before compiling anything: `//modules/webapi:sipi_version`
reads the pinned sipi base image's OCI label, and `rules_oci`'s credential helper shells out to
Docker Desktop's `desktop` credsStore, which is not running (the active context is colima). The
image is public, so every command in this run is prefixed with
`DOCKER_CONFIG=<scratch dir with {"auths":{}}>` to bypass the helper. Nothing in the repo is
changed by it.

### Phase 2 + Phase 3 — landed as `912b7c50e`

`feat(admin)!: make the project restricted view an optional setting`

Phase 2 checkboxes done:

- [x] "`KnoraProject.restrictedView` becomes `Option[RestrictedView]` (`KnoraProject.scala:40`)"
- [x] "`KnoraProjectRepoLive.getRestrictedView` returns `Option` — drop `.getOrElse(RestrictedView.default)` at `:100`"
- [x] "`KnoraProjectRepoLive.toTriples` emits no restricted-view triple for `None` (`:138`, emission at `:154-156`)"
- [x] "Restate the dual-triple precedence explicitly (size wins over watermark) rather than leaving it a side effect of `orElse`"
- [x] "A stored `projectRestrictedViewWatermark false` resolves to `None` …"
- [x] "`KnoraProjectRepo.builtIn.makeBuiltIn` passes `None` (`:34-51`)"
- [x] "`KnoraProjectService.createProject` stores `None` for a new project (`:81`)"
- [x] "`KnoraProjectService.setProjectRestrictedView` keeps mapping `Watermark(false)` to the default, wrapped as `Some` (`:49-56`)"
- [x] "Add `KnoraProjectService.clearProjectRestrictedView`, persisting `None`"
- [x] "Update every remaining `KnoraProject(...)` construction site, including `TestDataFactory` and the ttl fixtures"
- [x] "Update the pinned query in `KnoraProjectRepoLiveSpec:261-305` and review the diff rather than accepting it" — **see the pinned-query finding below**
- [x] "Unit test: neither triple round-trips as `None`; a size round-trips as `Some(Size)`; saving `None` writes neither predicate"
- [x] "Unit test: a project storing both a size and a watermark resolves to the size, matching `admin-data.ttl:84-85`"
- [x] "Unit test: a project storing only `watermark false` resolves to `None` and inherits the default"

Phase 3 checkboxes done:

- [x] "Add `isDefault: Boolean` to `ProjectRestrictedViewSettingsGetResponseADM` (`ProjectsMessagesADM.scala:131-139`)"
- [x] "Resolve the effective settings in `ProjectRestService.getProjectRestrictedViewSettings*` (`:228-249`) …" — **see the deviation below**
- [x] "Add the two DELETE endpoints (by IRI and shortcode) to `ProjectsEndpoints.Secured` (`:87-117`), returning the GET response shape"
- [x] "Change the two POST endpoints to return the GET response shape as well, and retire `RestrictedViewResponse` …"
- [x] "Wire them in `ProjectsServerEndpoints` (`:33-43`) and register in `AdminApiServerEndpoints`" — the aggregator composes `projectsServerEndpoints.serverEndpoints` wholesale, so no edit to `AdminApiServerEndpoints` was needed
- [x] "Add the REST-service methods behind the same `ensureSystemAdminOrProjectAdmin*` auth as the POSTs (`ProjectRestService.scala:251-261`)"
- [x] "Keep `SetRestrictedViewRequest.toRestrictedView`'s 'exactly one of' validation unchanged"
- [x] "E2E in `AdminProjectsEndpointsE2ESpec`: GET on an unset project reports `isDefault: true`; POST then GET reports `false`; DELETE then GET returns to `true`"
- [x] "E2E: DELETE is refused for a non-admin"

#### Pinned-query finding (Phase 2)

**The pinned `findByPatternQuery` SPARQL does not change, and that is correct.** The generated
CONSTRUCT/OPTIONAL shape comes from `KnoraProjectRepoLive.entityProperties`, which still lists both
`projectRestrictedViewSize` and `projectRestrictedViewWatermark` as optional properties. The
`Option` change lives entirely in the mapper (`toEntity` / `toTriples`), which the query builder
does not read. Verified by running the pinned test unchanged: it passes (`//modules/webapi:test`,
1891 tests green). So the plan's expectation that this pin would move was wrong; nothing was
blind-accepted, and the pin is left exactly as it was.

Reading a project still needs both OPTIONALs (an absent triple is what produces `None`), and
writing still needs both predicates in `entityProperties` — that is what makes `AbstractEntityRepo`'s
MODIFY delete a previously stored setting when `toTriples` emits neither. Dropping either from
`entityProperties` would silently break the clear path.

#### Deviation (Phase 3)

The plan puts `getOrElse(RestrictedView.default)` + `isDefault = restrictedView.isEmpty` in the
three `ProjectRestService.getProjectRestrictedViewSettings*` methods. That would triplicate the
resolution and then duplicate it again for POST and DELETE, five sites in all. Instead
`ProjectRestrictedViewSettingsGetResponseADM.from` now takes `Option[RestrictedView]` and does the
resolution once; all five call sites pass the Option through. Same behaviour, single source.

#### Incidental removal (Phase 2)

`ProjectService.setProjectRestrictedView` and its private helper `toKnoraProject` are unreachable —
`ProjectRestService` calls `KnoraProjectService` directly, and nothing else in the repo references
either (verified by grep over all `.scala` under `modules`). The `Option` change forced a decision
on `toKnoraProject`'s parameter, so rather than invent semantics for dead code both were deleted.

### Phase 5 — landed as `ae66b62ef`

`feat(admin)!: reject pct:100 and clear the restricted view sizes nobody chose`

- [x] "`RestrictedView.Size.from` rejects `pct:100` — the percentage pattern becomes `pct:[1-9][0-9]?$` (`RestrictedView.scala:34`)"
- [x] "Unit test in `RestrictedViewSpec`: `pct:100` rejected, `pct:99` and `!128,128` still accepted" — the property test's bound moved from `n <= 100` to `n <= 99` as well
- [x] "Add `UpgradePluginPRxxxx` deleting two classes of `knora-admin:projectRestrictedViewSize` triple …" — named `UpgradePluginPR4329`
- [x] "Plugin spec: a `\"!128,128\"` triple is removed, a `\"pct:100\"` triple is removed, and `\"!512,512\"`, `\"pct:1\"` and a watermark triple are all untouched"
- [x] "Register it in `RepositoryUpdatePlan.makePluginsForVersions:20-43` as version 56"
- [x] "Bump `KnoraBaseVersion` 55 → 56 (`package.scala:17`) and the version in `knora-base.ttl`"
- [x] "Plugin spec following `UpgradePluginPR3112Spec` …" — the plan lists the plugin spec twice with slightly different cases; one spec covers both lists
- [x] "CHANGELOG: …" — **see the CHANGELOG note below**

Version 56 confirmed free before committing: `makePluginsForVersions` ended at 55
(`MigrateRemoveProjectStatus`) and `package.scala` held 55.

The plugin spells out `"pct:100"` and `"!128,128"` as literals rather than reading
`RestrictedView.Size.default.value` (which is what `UpgradePluginPR3112` does). A migration is a
statement about what is in the store today; if the platform default ever changes, this plugin must
keep deleting the value it was written for. The reason is in a comment on the plugin.

#### CHANGELOG note (Phases 4 and 5)

`CHANGELOG.md` in this repo is **generated by release-please** from commit messages
(`.github/workflows/create-release.yml`), and `docs/development/dsp-api-commit-conventions.md`
says explicitly that a new feature or breaking change belongs in the commit message. Hand-editing
`CHANGELOG.md` would be overwritten at the next release. So the plan's "CHANGELOG entry" checkboxes
are satisfied by `feat(admin)!:` / `feat(admin)!:` subjects plus a `BREAKING CHANGE:` footer in each
commit body, which is what release-please renders into the changelog.

### Phase 4

- [x] "Add `MediaKind`, `OriginalAccess`, `DerivativeAccess` and `AssetAccess` under `slice/admin/domain/model/` …" — one file, `AssetAccess.scala`
- [x] "Map every `knora-base` file value class IRI to a `MediaKind` … an unmapped class must fail closed (deny) and be counted"
- [x] "Test that enumerates the file value classes declared in `knora-base.ttl` and asserts each maps to a `MediaKind`"
- [x] "`FileValuePermissionsQuery.build` also selects the `rdf:type` of `?currentFileValue` … and constrains it to the known file value classes"
- [x] "Update the pinned `getQueryString` assertions in `FileValuePermissionsQuerySpec`"
- [x] "`AssetPermissionsResponder` returns `AssetAccess`; delete `PermissionCodeAndProjectRestrictedViewSettings` and `buildResponse`'s code-1 branch"
- [x] "New response DTO, codecs and `FilesEndpoints` output type; the ACL code leaves the payload"
- [x] "`AssetPermissionsCache` carries the new value type, and its key is confirmed to distinguish assets"
- [x] "Rewrite `modules/sipi/scripts/sipi.init.lua`'s `pre_flight` …"
- [x] "An unrecognised `derivative` literal denies **and** increments a counter" — **see the counter deviation below**
- [x] "`sipi.init.lua` reads only `derivative` and `FetchAssetPermissions` reads only `original`" — **structurally enforced, see below**
- [x] "`FetchAssetPermissions` decodes the new shape … and `ProjectsEndpointsHandler` gates on `original == \"grant\"`"
- [ ] "Bump both per-arch SIPI `oci.pull` digests in `MODULE.bazel`" — **DEFERRED, operator action; see below**
- [x] "Integration test in `AssetPermissionsResponderSpec`, one case per `MediaKind` × {no permission, RV, V}" — **split, see below**
- [x] "Integration test: dsp-ingest's original-download gate still refuses every `RV` asset, of every media kind" — **see below**
- [x] "`SipiIT` asserts `knora.json` answers 404 for a `denied` decision"
- [x] "`SipiIT` cases for the relay itself — one per decision kind" — the two `stream` cases are `TestAspect.ignore`d until the digest bump; see below
- [x] "Update `AdminFilesE2ESpec` and `TestAdminApiClient:60` to the new shape"
- [x] "CHANGELOG under a breaking marker …" — via the commit message, per the CHANGELOG note above

#### Deferral — the `MODULE.bazel` digest bump (expected, operator action)

Not done, and deliberately not worked around: no digest guessed, no pin disabled, no pin skipped.
`MODULE.bazel` is untouched. The SIPI release that produces the two per-arch digests does not exist —
Phase 1 is only being implemented now and must merge and be released first.

Two consequences inside this repo, both of which unblock with that same bump:

1. The two `SipiIT` `stream` cases are `@@ TestAspect.ignore`d. Against the currently pinned image
   `permission_from_str` does not know `"stream"` and maps it to `Deny`, so both would answer 401. The
   comment above the suite names the bump as the gate; dropping the `ignore` belongs in the same change.
   They are *written*, not skipped: the moment the digest lands they assert "day one changes nothing".
2. Nothing else in this repo depends on the unreleased image. Every other decision kind is exercised
   against a real Sipi container (see the run log below).

#### Deviation — the "counter" for an unrecognised `derivative` literal is a log line

**Sipi exposes no metrics binding to Lua.** Checked against the sipi tree: `docs/src/lua/index.md`'s
"SIPI functions available to Lua scripts" lists `server.http`, `server.json_to_table`,
`server.generate_jwt`, `server.decode_jwt`, `server.parse_mimetype`, `server.file_mimetype`,
`server.file_mimeconsistency`, `server.systime`, `server.log`, `server.secure_equals`, `server.uuid`,
`server.print` — no counter or metric. So the hook logs at `LOG_ERR` with a fixed, greppable message
instead. It is countable in Loki and it is loud, which is what the plan's requirement is for; it is not
a Prometheus counter.

**This is a real gap worth a Phase-1 follow-up in sipi** (a `server.metric_inc`-style binding, or a
Rust-side counter for "preflight returned an unparseable permission"). It is out of this
orchestrator's scope — Phase 1 belongs to another run, in another repo.

The dsp-api side of the same requirement *is* a real counter:
`Metric.counter("asset_access_unmapped_file_value_class")` in `AssetPermissionsResponder`, incremented
when a file value class has no `MediaKind`.

#### The two channels are separated structurally, not by convention

The plan asks for this to be asserted in review. It is stronger than that:

- `FetchAssetPermissions.AssetAccessResponse` is `final case class AssetAccessResponse(original: String)`.
  It has no `derivative` field, so dsp-ingest *cannot* read the Derivative channel. (zio-json ignores
  unknown fields, so the narrow decoder is enough.)
- `sipi.init.lua`'s `_sipi_permission` reads `access.derivative`, `access.size` and `access.watermark`
  and never mentions `original`. It is the only place the payload is interpreted; both hooks call it.

#### Deviation — where the `MediaKind` × {none, RV, V} matrix lives

The plan puts the full matrix in `AssetPermissionsResponderSpec`, an integration spec. Two problems:
the shared project datasets carry item-count assertions (`ResourcesPerOntologyEndpointsE2ESpec` pins
`itemCount = 1` for `anything:ThingArchive`), so adding a fixture per media kind to one of them breaks
unrelated specs — exactly what `CLAUDE.md` § Testing Guidelines warns about; and 24 container-backed
cases to exercise a pure function is the wrong instrument.

Split instead:

- `AssetAccessSpec` (pure unit) covers the **whole** matrix — every `MediaKind` × {no permission, RV,
  every permission from View upward}, plus the stored-setting cases. This is where the policy lives.
- `AssetAccessResponseSpec` (pure unit) pins the **wire vocabulary**: all five payload shapes,
  including `size` vs `watermark` being mutually exclusive and `watermark` being a boolean.
- `AssetPermissionsResponderSpec` (integration) proves the **wiring** end to end through the real query
  — one case per derivative decision, on a **self-contained** fixture
  (`test_data/project_data/asset-access-data.ttl`: an RV archive, RV audio, a V document) plus the
  incunabula still image that was already there. This is what exercises the new `rdf:type` constraint
  and the IRI-to-`MediaKind` step the compiler cannot check.

Same for "dsp-ingest's gate refuses every `RV` asset, of every media kind": the "every media kind" half
is a property of `AssetAccess.from` (asserted exhaustively in `AssetAccessSpec`); the gate itself sees
only the literal, and `ProjectsEndpointSpec` pins that a `withhold` answer is a 403. The stub was
deliberately changed to a `stream`/`withhold` decision, because "plays, therefore downloadable" is the
inference this design exists to prevent.

#### Correction to the plan — its `AssetAccess.from` shape does not meet its own acceptance criterion

Acceptance criterion: *"Adding a `MediaKind` fails to compile until `AssetAccess.from` handles it."*

The plan's pseudo-code cannot do that. Its restricted-view arm is three guarded cases ending in a bare
`case Some(RestrictedView) => Stream`, so a tenth `MediaKind` would compile silently and inherit
`Stream` — the permissive answer, for a kind nobody has classified.

`from` is written instead with an inner `media match` that enumerates all eight kinds and has **no
wildcard**. Adding a kind makes that match non-exhaustive, and this repo compiles with `-Werror`
(`CONVENTIONS.md` § Imports & formatting), so the warning is a build failure. Verified by temporarily
adding a ninth kind and confirming the compile fails; the probe was then removed.

#### Deviation — `StillImageExternalFileValue` maps to `RasterStillImage`

The plan's enum has eight values for nine concrete classes, and says `StillImageExternalFileValue`
"never reaches SIPI". `MediaKind` names the medium, not where the bytes live, so it maps to
`RasterStillImage`; the `clamped` decision that produces is unobservable because the endpoint is never
called for such an asset. The alternative — a ninth value, or folding it in with `Vector` — would
either widen the plan's enum or state something untrue about the medium.

#### Pinned-query review (Phase 4) — what changed and why it is correct

Regenerated and reviewed line by line, not accepted blind. Exactly two changes:

1. `SELECT ?creator ?project ?permissions` becomes `SELECT ?creator ?project ?permissions ?fileValueClass`.
2. One group is **appended after** all existing patterns:

   ```sparql
   { ?currentFileValue <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> ?fileValueClass .
   FILTER ( ?fileValueClass IN ( knora-base:ArchiveFileValue, knora-base:AudioFileValue,
     knora-base:DDDFileValue, knora-base:DocumentFileValue, knora-base:MovingImageFileValue,
     knora-base:StillImageExternalFileValue, knora-base:StillImageFileValue,
     knora-base:StillImageVectorFileValue, knora-base:TextFileValue ) ) }
   ```

Why it is correct:

- The type is taken from `?currentFileValue` (the newest version), not `?fileValue`, as the plan requires.
- `?currentFileValue` is already bound by the preceding patterns, so this is a bound-subject lookup, not
  a class-extent scan. Appending it therefore does not disturb the deliberate pattern order the file's
  own comment protects (the `?fileValue ?objPred ?objObj` block that steers Jena away from resolving the
  `previousValue*` closure first, DEV-6796-adjacent).
- The nine IRIs are `MediaKind.fileValueClasses` rendered in sorted order — the query and the mapping are
  single-sourced, so they cannot drift. Note this is deliberately **not**
  `OntologyConstants.KnoraBase.FileValueClasses`, which also contains the abstract `FileValue`.
- The constraint is what stops a multi-typed value multiplying rows; `AssetPermissionsResponder` takes
  `getFirstRow`, so without it the media kind would be a nondeterministic pick.
- `rdf:type` renders as a full IRI because the query declares only the `knora-base` prefix. Correct
  SPARQL, just verbose; not worth another prefix declaration in a pinned string.

#### Not in the plan, but required — `docs/03-endpoints/api-admin/projects.md`

The plan checked that `/admin/files` has no prose documentation (true) and concluded no doc change was
needed. But `projects.md` documents the RestrictedViewSettings verbs and would have gone stale: the GET
response gains `isDefault`, POST changes shape, `pct:n` loses 100, and DELETE is new. Updated: the route
table, both response examples, the percentage bound, and a new "Clear Restricted View Settings" section.
`just markdownlint` passes.

#### Behaviour change the plan does not call out — an RV still image on `/file`

The plan names one user-visible change (an RV archive stops being served). There is a second, and it
falls straight out of "the callers stop deciding anything":

`file_pre_flight` — the Bitstream / `/file` surface — used to map permission code 1 to `allow`, with the
comment *"restricted view permission on file means full access !! Because, at the moment, this doesn't
have a meaning for files other than images."* That handed an `RV` user the **unclamped** still image
through `/file`. It now translates `clamped` to `restrict`, which Sipi refuses on that route (403,
S2-09). That is the correct outcome — an RV user must not be able to download the full-fidelity image by
switching routes — but it *is* a behaviour change for any project serving `RV` still images via `/file`,
and it should be in the release note alongside the archive denial.

#### The release coupling is wider than the plan says — it reaches `//modules/test-e2e:test`

The plan treats the SIPI-image coupling as a deploy-sequencing risk. Running the **full** `test-e2e`
suite shows it is also a CI fact for this PR, and through a path the plan does not mention: dsp-api and
its own test suite fetch assets from Sipi's `/file` route **anonymously**, and the default object access
permission for the `anything` project grants `RV knora-admin:UnknownUser`
(`test_data/project_data/permissions-data.ttl:155`). So every anonymous `/file` fetch of a freshly
created non-image asset now resolves to `stream` — which the currently pinned Sipi binary does not know.

Observed, with the image rebuilt from this tree (new hook, old Sipi binary):

| Asset | New decision | Result | Cause |
|---|---|---|---|
| still image (IIIF route) | `clamped` | 200, passes | `restrict` is understood today |
| PDF | `stream` | 500 | binary does not know `stream` |
| CSV / XML | `stream` | 500 | binary does not know `stream` |
| audio / video | `stream` | 500 | binary does not know `stream` |
| Zip / 7z | `denied` | 401 | **by design** |
| standoff XSLT (`TextFileValue`) | `stream` | 500 | binary does not know `stream` |

Two different things, and they need different answers:

1. **The `stream` rows are the deferred digest bump, nothing else.** Once Sipi understands `stream`,
   `/file` serves it exactly as `allow` (`file_access` in `routes.rs`:
   `SipiPermType::Allow | SipiPermType::Stream => Ok(...)`), so these tests are correct as written and
   go green with the bump. They are left untouched. *(Isolation experiment below.)*
2. **The archive rows are the intended behaviour change**, and the tests asserted the old behaviour.
   `KnoraSipiIntegrationV2ITSpec`'s three archive cases fetched the archive anonymously and asserted
   200. They now assert that an archive is served to a caller with view permission and **refused (401)
   to an anonymous, restricted-view caller** — which lands the acceptance criterion *"An `RV` archive is
   refused on both routes — SIPI's `/file` and dsp-ingest's `/original` — asserted by tests"* in a real
   end-to-end test rather than only in a unit spec. `TestSipiApiClient` gained an authenticated
   `getFile(uri, user)` for the served half.

One more thing worth surfacing to whoever writes the release note: **the standoff XSLT path is an
internal, unauthenticated server-to-server fetch** (`SipiServiceLive.getTextFileRequest` sends no JWT),
so it is subject to the same anonymous-RV resolution as any end user. It works today only because
`file_pre_flight` treated code 1 as full access. After the train it works again via `stream`, but it is
a dependency nobody had written down.

#### Verification run (local, this machine)

- `bazel test //modules/webapi:test` — **1902 tests pass**, including `AssetAccess.from`,
  `MediaKind` ontology coverage, `AssetAccessResponse`, the regenerated `FileValuePermissionsQuerySpec`
  pin, and the unchanged `KnoraProjectRepoLiveSpec` pin.
- `bazel test //modules/ingest:test` — 215 tests pass.
- `bazel test //modules/test-it:test --test_filter='.*AssetPermissions.*'` — 11 pass (responder +
  wired-cache integration, against a real triplestore).
- `bazel test //modules/test-it:test --test_filter='.*SipiIT.*'` — **16, all green**, against a real
  `knora-sipi` container built from this tree. Every relay case passes: `full` -> Ok, `denied` -> 401 on
  IIIF and 404 on `knora.json`, `clamped` with a size -> Ok, `clamped` with a watermark -> Ok, an
  unrecognised literal -> 401. The two `stream` cases report as ignored.
- `bazel test //modules/test-e2e:test --test_filter='.*(AdminFilesE2ESpec|AdminProjectsEndpointsE2ESpec).*'`
  — 41 pass, including the new DELETE / `isDefault` round-trip.
- `just fmt`, `just check`, `just markdownlint` — all clean.

Full suites (`bazel test //modules/test-it:test //modules/test-it:test_gravsearch_span
//modules/test-e2e:test //modules/test-ingest-integration:test`):

- `//modules/test-it:test` **PASSED** (213s)
- `//modules/test-it:test_gravsearch_span` **PASSED**
- `//modules/test-ingest-integration:test` **PASSED**
- `//modules/test-e2e:test` **FAILED** — 945 tests, 13 failing, every one of them a `stream` decision
  against the un-bumped Sipi image. Enumerated and isolated below.

#### Isolation experiment — proving the 13 e2e failures are only the unreleased Sipi

Claimed, then verified rather than inferred. `stream` is specified to serve exactly as `allow`, so
patching the hook's single line `return 'stream'` to `return 'allow'` simulates a Sipi that knows the
type, changing nothing else. Rebuilt the image, re-ran both affected specs:

```
//modules/test-e2e:test --test_filter='.*(KnoraSipiIntegrationV2ITSpec|StandoffEndpointsE2ESpec).*'
  PASSED in 24.4s
```

Green. The hook and image were then restored, and re-running `KnoraSipiIntegrationV2ITSpec` unpatched
gives exactly the expected split:

```
.EE  - create a resource with a PDF file          (stream, blocked on the digest bump)
.EE  - change a PDF file value                    (stream)
.EE  - create a resource with a CSV file          (stream)
.EE  - change a CSV file value                    (stream)
.EE  - create a resource with an XML file         (stream)
.EE  - change an XML file value                   (stream)
.EE  - create a resource with a WAV file          (stream)
.EE  - change a WAV file value                    (stream)
.EE  - create a resource with a video file        (stream)
.EE  - change a video file value                  (stream)
.  +  create a resource of type ArchiveRepresentation with a Zip file   (now asserts the refusal)
.  +  change a Zip file value                                           (now asserts the refusal)
.  +  create a resource of type ArchiveRepresentation with a 7z file    (now asserts the refusal)
```

plus three in `StandoffEndpointsE2ESpec` (the XSLT fetch), all `stream`.

**So `//modules/test-e2e:test` is red until the `MODULE.bazel` digest bump, and green the moment it
lands.** No test was weakened, skipped or deleted to hide this. The plan already requires the bump to
be in this PR ("bump the two per-arch `oci.pull` digests in dsp-api's `MODULE.bazel` **within the
dsp-api PR**"), so this is the expected state of the branch, not a defect — but it is a bigger red
surface than "two ignored SipiIT cases", and whoever performs the bump should re-run the full
`test-e2e` suite as the acceptance check.

#### Exhaustiveness verified, not asserted

Temporarily added a ninth `MediaKind` and built `//modules/webapi:webapi`:

```
-- [E029] Pattern Match Exhaustivity Warning: .../AssetAccess.scala:95:25
   |                         match may not be exhaustive.
ERROR: ... scala @@//modules/webapi:webapi failed
```

The probe was removed. Acceptance criterion *"Adding a `MediaKind` fails to compile until
`AssetAccess.from` handles it"* holds.


