# macOS end-to-end acceptance

## 2026-10-22 0.1.4 whole-release candidate — physical UNKNOWN / NOT_RUN

Release row: `SEETHIS-VERSION-014-AND-WHOLE-RELEASE-VALIDATION-R1-20261021`.
Default marketing and build versions are both **0.1.4**. Separately configured
App Store marketing/build values remain explicit and independent of this default.
The four source workstreams below have been integrated after independent bounded
QA; their automated results do not establish final physical acceptance. This
matrix is a fresh acceptance contract, not a report of performed UI actions.
All physical outcomes below remain **UNKNOWN / NOT_RUN** until independently
observed on the same final signed candidate. Preserve every older section and
its historical outcome, including old 0.1.3 CEX HELD/UNKNOWN, child1 terminal
NO_PASS, original child2 12/13 fixture failure/native NO_PASS and later separate
fixture-successor 13/13 plus 206 checks. Retain native ENV_FAILURE exit 127 as
history. The prospective standalone operator exception relaxes native governance
prerequisites only; no native entry/graph/merge/reconcile PASS is claimed.

### Exact source, binary and artifact binding required before physical PASS

Independent QA records one complete binding alongside its per-case evidence:

| Binding | Required actual values; currently UNBOUND / NOT_RUN |
| --- | --- |
| Source | Full final integrated commit, Git tree, clean status, release diff and independent source-QA identity. A provisional worker commit is not the final integrated commit. |
| Signed app | Absolute bundle path, bundle ID, actual marketing/build versions, absolute executable path and SHA-256, complete bundle-tree SHA-256 and its measurement algorithm. |
| Signature | Actual strict/deep verification result, Developer ID signing authority, team ID, CDHash, designated requirement, entitlement bytes/hash and actual notarization/Gatekeeper status. Signing alone is not notarization. |
| Running identity | Actual PID, running executable path/hash matching that signed app, launch/run start and per-observation UTC timestamps, macOS build, display geometry/scale, independent tester identity and permission state. |
| Delivery | Exact app, ZIP and DMG paths/hashes; extracted/mounted bundle, executable, provenance and signature parity with the retained app; signed provenance binds the same final commit/tree. |
| Test custody | Disposable controlled references/settings, fixture IDs, capture/completion times and safe restore plan. Distinguish automated/source review, package inspection, actual UI and pending human acceptance. |

Do not launch, quit, overwrite the installed app, alter Launch Services/TCC or
permissions, bypass security, or use the user's reference corpus merely to fill
this matrix. If a real boundary blocks a case, preserve its exact candidate-bound
reason and minimal reviewable transaction; that case remains BLOCKED/NOT_RUN.
Old pending registration plans are not authorization. Notarization upload and
remote publishing require their own authority. Failure is never a governance
waiver or a documentation PASS. Per-case results must retain PASS/FAIL/BLOCKED/
UNAVAILABLE/NOT_RUN, actor, time, exact identity, evidence and observed limits.

### Four-item actual whole-release matrix

| Case / workstream | Required same-final physical sequence and evidence | Physical outcome |
| --- | --- | --- |
| REL14-NBC05 / mark-number-context synchronization | In actual apps and distinct native windows A/B, draw line/circle and multiple marks. Keep the pointer stationary across app and window A→B→A. With notes open/closed and global Visible/Invisible, correlate eligibility with ink, number, hit, hover and editor visibility; no stale hidden interception. Returning to A restores stable IDs/numbers and saved notes/drafts, leaves the editor closed, and explicit number click reopens the correct owner. Record original-owner draft/autosave/failure behavior and transition times. | UNKNOWN / NOT_RUN |
| REL14-TSV08 / exact captured title | Exercise new captures and existing immutable tag-absent legacy captures with a consistent native tuple, including cold-loaded compatible ready sidecars without recapture or history mutation. Actual title A→B→A hides/restores every eligibility surface and preserves IDs, numbers, saved notes and retained drafts. Measure observation times, response latency and stale-visible interval with a stationary pointer. Missing/empty/untrustworthy/inconsistent/known-truncated/denied/stale/invalid/over-limit/unsupported title evidence must hide while preserving decodable history; never backfill captured title/native provenance from current observations or downgrade explicit unavailable status. Own-note focus works; return requires explicit reopening. Chrome URL/page/tab identity remains stronger. Disclose exact-same-title plus same-native-tuple collisions, dynamic-title/rename hiding until exact return, weaker native-v0 lifetime provenance and no unique-session or PV1/PV5 guarantee. Provider-silent well-formed truncation detection remains UNKNOWN without exact-final evidence; repeated matching reads do not prove provider completeness. | UNKNOWN / NOT_RUN |
| REL14-NOC06 / outside-note dismissal | Use actual page content, blank area and Inspector controls; editor text/scroll/retry/close controls retain their normal behavior. Same-number click closes once; different-number click switches once to the correct note without visible wrong-frame/flicker. Click in another app: observe actual global-monitor delivery and the original target's normal action exactly once, with original modifiers. Use actual input-method composition and candidate-window interaction, not setMarkedText alone. Verify autosave plus failed/conflict/commit-uncertain/unavailable/pending-reference drafts after dismiss/reopen, exact original owner/text, truthful Retry versus Saved and no stale timer or generation closure. Exercise actual context/title/global/permission transitions where safely available; return does not auto-open. | UNKNOWN / NOT_RUN |
| REL14-SMI07 / one JSON plus one annotated image | Create controlled multiple outer gestures with exact empty/Unicode/multiline notes. One ready default reference read supplies schema3 compact JSON and one full annotated PNG URL: fetch one JSON plus that one image and compare real captured background, strokes, stable IDs/numbers, note bytes and captured context/time. Decode actual PNG pixels and bind digest/ETag/hash; no optional per-mark/source/crop/layer fetch is required by default. Details/viewer/explicit schema2 metadata and Bearer compatibility remain available. Observe pending/failure/deletion without an unmarked fallback and preserve security guards. Record actual GET/byte counts and readiness scope; token savings are NOT_MEASURED unless independently measured. Current source numbering is proven 1..N; unsupported noncontiguous numbering is NOT_APPLICABLE / NOT_RUN, never manufactured or reported PASS. | UNKNOWN / NOT_RUN |

Child3's native 236 coordinator checks and native marked-text simulations are
bounded automated evidence, not actual global target delivery, IME candidate
interaction or physical NOC06. Child4's independent synthetic capture established
one JSON + one image (two ready GETs), exact note/image agreement and source
numbering 1..N; its measured bytes are fixture-specific and not a packaged UI or
token benchmark. Final QA must report its own same-candidate observations.

### Preserved whole-release regressions on that same identity

| Case | Required actual observation | Physical outcome |
| --- | --- | --- |
| REL14-REFERENCE-COPY | Create/lookup controlled references, exact captured source/time, copy reference and paste through normal clipboard actions. Keep notes/IDs immutable across context changes; preserve clipboard ownership when restoring. | UNKNOWN / NOT_RUN |
| REL14-INSPECTOR-PREVIEW | Execute preserved SLP-01–04 mapping: one selector, exact stable number/note per mark, usable accurate 470×430 preview and correct selection/resize. Compare native preview, Copy marked image and actual browser /view annotated pixels to source with no missing/double number layer. Current supported 1..N domain applies; old gap-fixture scenarios remain historical. | UNKNOWN / NOT_RUN |
| REL14-GLOBAL-PERSISTENCE | Default Visible with/without selection; Invisible hides all ink/numbers/editor/hit interception. On authorized exact-candidate relaunch, Invisible then Visible persist for all references without rewriting historical per-mark bits, notes or IDs; source/window/title/page/lifecycle gates still compose. Failed/uncertain saves remain truthful. | UNKNOWN / NOT_RUN |
| REL14-DELETE-ALL | In isolated disposable data only, review exact IDs, cancel with no deletion, reopen/add a later reference and confirm only reviewed IDs disappear. Clear selection/preview/ink, revoke deleted viewer capability and retain later reference. Safe cleanup-failure retry cannot expand the set or resurrect records; unavailable branch stays NOT_RUN. | UNKNOWN / NOT_RUN |
| REL14-FIVE-MINUTE | Real default 300000 ms inactivity: record original capture and latest accepted activity, valid clicks/active left drag less than five minutes apart across total age > five minutes, then actual idle expiry. Stationary hold/hover/right motion do not renew; release completes once. No shortened timer substitutes for elapsed physical evidence; preserve MIT matrices below. | UNKNOWN / NOT_RUN |
| REL14-CHROME-MENU | Actual Chrome tab/window A/B, same-URL distinct tabs, navigation/Back/reload and unavailable/denied/stale page evidence retain stronger page identity; title cannot bypass it. Preserve compact readiness/menu/Show Inspector behavior, explicit recovery and consent limits without incidental permission requests. Record unavailable branches and real browser/version. | UNKNOWN / NOT_RUN |

The release-owned carry-forward register and child3/child4 independent QA reports
supply obligations and bounded history only. An app/ZIP/DMG build or a clean
source test suite cannot promote any physical row here. Record final source,
artifact inspection, physical outcomes, native UNKNOWN/NO_PASS/WAIVED and audit
archive disposition separately; old 0.1.3 acceptance never proves 0.1.4.


## 2026-10-21 final 0.1.3 candidate — physical UNKNOWN / NOT_RUN

Release row: `SEETHIS-VERSION-013-AND-PACKAGE-VALIDATION-R1-20261021`.
The operator approved native-window scope for this release. Original PV1/PV5
Codex task/page isolation within one native window remains **OPEN / deferred**;
this document does not close it or change product semantics. Source inference:
switching tasks within the same native window may leave earlier marks eligible.
A window title is not proof of task identity. Verify distinct native windows and
the existing Chrome page behavior below; do not report same-window task isolation
as PASS. Historical FAIL, NO-PASS, UNKNOWN and component results elsewhere in
this file retain their original meaning and do not establish final acceptance.

Before any physical result, independent QA must bind the same clean final
commit/tree, app bundle path/version/build, executable SHA-256, signature and
code requirement, exact PID, macOS/display scale, actor and timestamp to its
observations. Record artifact hashes separately from source and automated test
results. A different installed binary, source inference, component test or mock
cannot substitute for the exact candidate's physical observation. All outcomes
in the following tables are **UNKNOWN / NOT_RUN** until that observation exists.
Attach a separate per-case PASS/FAIL/UNAVAILABLE result with evidence and limits;
keep unavailable branches explicit. Independent review of this matrix and the
SLP mappings is also pending, not claimed by the documentation author.

Use controlled references and normal public app actions. Before launch or a
restart, the custody owner must preserve the original app, settings, references
and runtime identity and prove a bounded restore path. Delete All requires an
isolated disposable data root with only tracked fixture IDs; never use the
user's corpus. Preserve clipboard representations where possible and restore
only if its change count still belongs to the test. A new permission prompt or
unapproved installation/OS action stops that dependent case. This checklist
itself authorizes no permission, Launch Services, installation or data-root change.

### Inspector CASE identifiers and authoritative SLP mapping

CASE identifiers name observations; they are not replacement acceptance IDs.
The authoritative meanings are **SLP-01: one reference selector**, **SLP-02:
each stable numbered mark with its exact note**, **SLP-03: a usable accurate
preview**, and **SLP-04: independent QA with honest physical evidence**.
Menu readiness and image exports have their own final-matrix rows below; they
must not be relabeled as SLP-01 or SLP-02. Every Inspector case also requires
SLP-04 provenance; a written scenario alone cannot satisfy SLP-04.

| Case | Exact candidate physical observation | Authoritative coverage | Physical outcome |
| --- | --- | --- | --- |
| CASE-INS-01 | Open Inspector and select controlled references with the single dropdown. Verify exactly one reference selector and no duplicate References list/body; changing selection updates the same details, Marks and preview to the selected ID. | SLP-01, SLP-04 | UNKNOWN / NOT_RUN |
| CASE-INS-02 | Create several outer gestures with distinct saved Unicode/multiline notes. Compare every Marks row to its original stable drawing-order number and exact note, including empty notes and controlled fixtures with noncontiguous numbers. Switch references and return: no renumbering, missing row, truncation masquerading as full note, or note from another mark/reference. | SLP-02, SLP-04 | UNKNOWN / NOT_RUN |
| CASE-INS-03 | Resize the actual Inspector to 470×430 points. On selection the usable preview is immediately visible without scrolling past a duplicate reference body; compare captured content and marks to the controlled source, retain aspect ratio and readable mark placement, and check switching between portrait/landscape references. Record loading/unavailable states truthfully. | SLP-03, SLP-04 | UNKNOWN / NOT_RUN |
| CASE-INS-04 | With the compact Inspector, alternate references and resize. Confirm the one selector, exact numbered-note rows and usable accurate preview continue to agree on the selected ID. Record screenshot/observation, app PID/hash and independent QA identity per transition; missing physical evidence stays UNKNOWN. | SLP-01, SLP-02, SLP-03, SLP-04 | UNKNOWN / NOT_RUN |

### Same-candidate matrix for all seven workstreams and preserved regressions

The seven integrated workstreams are MUI (menu/compact readiness), SLP
(Inspector layout), IMGNUM (image numbering; SSP implementation), NCV (note editor
context), CJC (immutable captured context), PV (native-window visibility), and
GVI (global visibility). Delete All and inactivity are additional preserved
regressions. GVI's global control is the release expectation; historical
per-mark visibility scripts remain historical component criteria and do not
redefine that global behavior.

| Case / workstream | Controlled physical sequence and expected observation | Coverage / evidence boundary | Physical outcome |
| --- | --- | --- | --- |
| CASE-FINAL-MUI / MUI | Select menu Show Inspector twice: the second action leaves it visible. Inspect compact SeeThis readiness, exactly one compact Chrome row, Show SeeThis details and Quit. Both base grants determine SeeThis Ready independently of Chrome; Chrome Ready requires a verified usable page, not consent alone. Repeat available readiness states using the MUI matrix below. Opening menu/details/Inspector requests no permission or probe; long diagnostics and explicit recovery stay in details. | MUI-01–04; CRD-01–07. Record labels, both click outcomes and actual prompt/readiness state. Unavailable states remain explicit. | UNKNOWN / NOT_RUN |
| CASE-FINAL-SLP / SLP | Execute CASE-INS-01 through CASE-INS-04 on this exact app, including dropdown/no duplicate References, every exact number/note and the 470×430 preview/aspect comparison. | SLP-01–04 as explicitly mapped above; independent review of mappings and observations required. | UNKNOWN / NOT_RUN |
| CASE-FINAL-IMGNUM / IMGNUM | Draw controlled line/circle and edge/clipped gestures, including noncontiguous stable numbers. Compare native preview, Copy marked image, full/crop delivery and a real browser /view saved annotated PNG to raw source.png and the intended marks. Decode actual saved/copied PNG pixels: each stable number occurs once at its correct start anchor, with accurate strokes, crop/scale/aspect and no missing/double number layer. Resize preview and compare fractional/1×/2× display cases where available. | IMGNUM-01–04 (SSP implementation); preserve source image/hash, actual PNG bytes/hashes, pixel comparisons and physical observations separately. Underlay metadata or JSON alone does not prove numbered pixels or browser paint. | UNKNOWN / NOT_RUN |
| CASE-FINAL-NCV / NCV | Open a number's note editor, focus its own text view and type/save a distinct note. Its own focus must not falsely retire it. Switch to another app, a distinct native window and a different Chrome page, then return. Observe editor eligibility under existing context gates, saved text and retained failed draft separately, one-shot retirement/autosave, and no stale timer/callback reopening or writing the wrong mark. Exercise save/retry only with safe disposable fixtures; unavailable fault injection stays NOT_RUN. | Bind focus, transition and autosave timestamps to exact reference/mark IDs; distinguish confirmed saved content from retained unsaved draft. | UNKNOWN / NOT_RUN |
| CASE-FINAL-CJC / CJC | Capture a controlled Chrome URL at a recorded capture/image-completion time, save a distinct note, then navigate the browser and switch selected references. Inspector label and compact source context retain the captured URL/time and original source identity; current URL/title and later interaction/refresh times must not replace them. Check long/Unicode and unavailable URL labeling, pending versus completed image state, and persistence after reopen. | CJC-01–04 and linked RRG-05 captured page URL — Chrome — capture date/time labels; compare bounded output and capture/completion timestamps without publishing capabilities, private URLs or note contents. | UNKNOWN / NOT_RUN |
| CASE-FINAL-PV / native-window scope | In two actual distinct native windows A and B, create controlled marks in A, switch A→B→A, and verify paint/badge/hover/hit/copy/delete eligibility hides in B and restores only in the fresh matching A. Repeat unavailable/closed-window and app transitions where safe. Separately run existing Chrome tab A/B, same-URL distinct tabs, reload, navigation/Back and separate-window regression; stale/denied page observations must not bypass existing gates. | Record real PID/window identity and observed transitions; no title-based task claim. Codex same-native-window task/page PV1/PV5 remains OPEN / deferred and is not claimed PASS. | UNKNOWN / NOT_RUN |
| CASE-FINAL-GVI / GVI | In a fresh disposable settings root verify default Visible, including no selected reference. Toggle All references: Invisible: all eligible references lose ink, badge, editor and hidden hit interception. Select another reference while hidden, restart, verify Invisible persists, then set Visible, restart and verify it persists for all references. Notes, IDs, stable numbers and captured images remain intact; mixed historical per-mark visibility bits are not rewritten and do not defeat global Visible. Source/window/page/lifecycle gates still apply; a failed/uncertain save must not claim durable success. | Global persisted Visible/Invisible and defaultVisible, including note content and context composition. Safe fault-injection evidence is separate from physical observations. | UNKNOWN / NOT_RUN |
| CASE-FINAL-DELETE / preserved Delete All | Only in an isolated root, review the exact ordered fixture ID set, activate again while review is open, Cancel and verify nothing deleted. Reopen review, add a new controlled reference after review, Confirm and verify only the reviewed set is deleted, selection/preview/ink clear, viewer capability is revoked and the later reference remains. Exercise retry of that same reviewed set after safely induced cleanup failure, with no deletion expansion or resurrection; record unavailable retry branch NOT_RUN. Verify empty-history control and tracked cleanup. | Existing Delete All semantics; record reviewed/before/after IDs, tombstones and cleanup outcome. Follow custody isolation/restore checks; do not delete the user's references. | UNKNOWN / NOT_RUN |
| CASE-FINAL-IDLE / preserved five-minute inactivity | With default 300000 ms hold capture beyond total session age five minutes while making valid clicks less than five minutes apart. Retain earlier regions across the old deadline. Repeat with meaningful active left drag renewing inactivity; stationary hold/passive hover/right motion must not renew it. Then stop valid activity and observe genuine idle expiry, truthful cancellation and no phantom reference. Separately release before expiry and verify exactly one completion with original capture timestamp. | MIT-P1–P8/P10–P11 below; record latest accepted activity versus original capture time. Exact millisecond/order claims need deterministic evidence; real held input and elapsed time need physical evidence. | UNKNOWN / NOT_RUN |

Record source/automated verification, native governance disposition, package
inspection, physical observations and pending operator acceptance as distinct
results. No table here promotes a prior workstream NO-PASS/UNKNOWN to PASS or
asserts that the final signed app, ZIP or DMG has been built or physically run.

## 2026-10-19 marking inactivity timeout R2 — candidate acceptance, physical NOT_RUN

Candidate row: `SEETHIS-MARK-IDLE-TIMEOUT-QA-REPAIR-R2-20261019` (MIT-01–MIT-06).
This matrix incorporates the reviewed documentation input
`318d5dcf0f5d681017a21d038e327b3ce0aab1f0` into the fresh R2 candidate.
The source input `01eadf5b1be44ba66bd0897bd6dca0b3b00d5532` retains the R1 QA
failures for dual-delivery drag renewal and the browser form maximum. R1's
NO-PASS disposition and documentation-only PASS are historical provenance;
neither supplies R2 acceptance. Every result below requires fresh R2 evidence.
The approved default is **five minutes of inactivity (300000 ms)**, renewed by
accepted marking pointer-down/up events and meaningful active left-button drawing. Total
session duration is not the idle clock. This section specifies candidate checks;
it does not report that the candidate behavior or a physical run has passed.

Record the exact isolated candidate commit/tree and branch, executable/package
SHA-256, bundle path/identifier/version, signature/code requirement, PID, macOS
build, display geometry, tester/time, capture binding, timeout/schema and current
permission states. Use a disposable candidate settings/reference directory with
synthetic fixtures. Record the effective settings and fixture paths without
publishing capability secrets. Keep installed user settings, saved references,
the stable app, `main`, stable AC and the separate Input Monitoring trial intact.
This row does not accept or implement the separate UI1–UI5 or Windows work.

For the timed checks, `t0` is the original capture/session timestamp and `ta` is
the latest accepted activity timestamp. The default deadline is `ta + 300000 ms`.
Record event times separately from observations. The existing watchdog uses a
strict greater-than boundary: the session remains live exactly at the deadline,
and expires when a later watchdog evaluation observes greater elapsed inactivity.
Accepted activity processed before watchdog cancellation may renew a still-active
session, even after the former deadline; input after cancellation cannot revive it.
Exact millisecond boundary and processing-order assertions belong in the
deterministic checks below. Do not replace the original capture timestamp with a
renewed activity timestamp.

### Controlled native matrix — author result: NOT_RUN

Hold the configured capture chord for the retention cases and distinguish
unsubmitted regions in the active session from already submitted reference jobs.
Use the same exact candidate for each result; source tests, a successful build or
old physical observations cannot substitute for these observations.

| ID | Controlled observation and expected result | Coverage | Author status |
| --- | --- | --- | --- |
| MIT-P1 | With fresh/default settings, draw one region and stop interacting while the capture chord remains held. The region remains beyond the old 30-second deadline and before five minutes since `ta`. After a genuine full idle interval, the watchdog exits marking with the truthful timeout reason and clears the unsubmitted regions under the existing cancellation behavior. | MIT-01, MIT-03 | NOT_RUN |
| MIT-P2 | With an existing pending region, make a valid marking click just before the current deadline (target `ta + 299000 ms`, record actual down/up times). Verify accepted pointer-down and pointer-up each renew a full five-minute interval, including survival across the previous deadline; all earlier pending regions remain present through the renewed deadline. | MIT-02, MIT-03 | NOT_RUN |
| MIT-P3 | Make repeated valid clicks less than five minutes apart while retaining several regions, continuing beyond five minutes of total session age. Verify each click renews the interval, prior regions remain visible and the session ends only after the latest full idle interval or another existing completion/cancellation cause. | MIT-02, MIT-03 | NOT_RUN |
| MIT-P4 | Actively draw a region across the former deadline with meaningful drag movement. Verify accepted drawing activity renews the idle interval and retains earlier regions. Then stop drawing and allow a full idle interval; a stationary held pointer alone must not keep the session alive. | MIT-02, MIT-03 | NOT_RUN |
| MIT-P5 | Move the pointer passively between regions without an active draw, including hover and jitter near the deadline. Verify those movements do not renew inactivity; observe normal timeout and cleanup. Separately record invalid/stale-event coverage from deterministic tests rather than claiming physical generation of those events. | MIT-02, MIT-05 | NOT_RUN |
| MIT-P6 | After multiple renewals, release the capture shortcut before expiry. Verify exactly one completion with no duplicate reference-job admission, preserving all intended regions and the original capture/session timestamp. Check both key/modifier release orders and watchdog release recovery; later release/watchdog callbacks must not submit a duplicate. | MIT-02, MIT-03, MIT-05 | NOT_RUN |
| MIT-P7 | Explicitly cancel a renewed session containing regions, and separately cause a genuine supported interruption. Verify the correct cancellation reason, removal of the unsubmitted regions, no completion/reference job and no late resurrection. A fresh session after cancel, idle expiry or completion starts with a new idle interval and contains no old pending regions. | MIT-03, MIT-05 | NOT_RUN |
| MIT-P8 | Before and after expiry, explicit cancel and a successful completion, compare disposable pre-existing saved references and their authenticated URLs. They remain unchanged and resolve through the existing lifecycle; cancelling unsubmitted regions must neither delete saved references nor fabricate an autosaved draft. | MIT-03 | NOT_RUN |
| MIT-P9 | In an isolated candidate upgrade/reopen using synthetic schema-1 and schema-2 settings with the old default `maximum_hold_ms: 30000`, observe effective `300000` and persistence as storage schema 3. Verify a second load does not migrate again. Repeat with a valid non-default custom timeout and with an intentional `30000` in schema 3; preserve both custom choices and unrelated known settings/reference capability identity. Verify the existing web client can update via public settings API schema 2 and save/reload without resetting that intentional value. | MIT-04, MIT-06 | NOT_RUN |
| MIT-P10 | Exercise real active left drag on the exact candidate and correlate raw native and AppKit overlay delivery where observable. A new geometry point must renew once even if both paths receive it; a duplicate must neither add a second point nor extend the deadline again. Record observed delivery order and clock domain. If both orders cannot be physically observed, mark that gap explicitly and use MIT-D8 for controlled order assertions. | MIT-02, MIT-05 | NOT_RUN |
| MIT-P11 | During and between left-drawing segments, send passive hover and right/other-button motion. Pointer policy may update, but these events must not append marking geometry or renew idle time. Verify a later legitimate left drag still renews, and that unrelated motion alone allows true idle expiry. | MIT-02, MIT-05 | NOT_RUN |
| MIT-P12 | In a real browser with the exact bundled candidate settings page and an isolated settings service, inspect the inactivity label and maximum. Fresh and migrated defaults display `300000` and save through the normal form. `300001` fails browser constraint validation and cannot persist; direct API rejection is checked separately. Set deliberate `30000`, save and reload/reopen: the value and unrelated settings persist. Record browser/version, HTML/JS hashes, validation result and service persistence separately. | MIT-04, MIT-05, MIT-06 | NOT_RUN |

No app installation or launch, GUI interaction, input injection, permission/TCC
change, clipboard inspection or access to current user data was performed by the
documentation author. Every native row remains `NOT_RUN` pending a controlled
run. A tester must record a separate PASS/FAIL/UNAVAILABLE result with bounded
evidence and exact candidate identity; automated results do not promote these rows.

### Deterministic candidate checks to bind to independent test evidence

Use an explicit fake clock and fresh controller/temporary settings fixtures.
Record exact test names, commands, candidate commit, outcomes and evidence paths
for each case. The documentation lane specifies these checks; it does not certify
the source lane's implementation or independent QA result.

| ID | Required assertions | Coverage | Candidate evidence status in this document |
| --- | --- | --- | --- |
| MIT-D1 | Default controller and fresh settings use `300000`. A session survives `ta + 30000`, `ta + 299999` and exactly `ta + 300000`, then expires when watchdog evaluates `ta + 300001`; the strict greater-than boundary and existing watchdog timeout reason remain intact. | MIT-01, MIT-05 | NOT_RUN — bind exact candidate test result |
| MIT-D2 | A valid click at `ta + 299999` renews to that click plus `300000`; test accepted pointer-down and pointer-up separately. It survives the former and renewed exact deadlines, then expires on a later watchdog evaluation. Repeated clicks extend total session age beyond five minutes without losing regions. Separately process valid activity after the old deadline but before watchdog cancellation: the active session renews; reverse the order and assert input cannot revive the cancelled session. | MIT-02, MIT-03, MIT-05 | NOT_RUN — bind exact candidate test result |
| MIT-D3 | Meaningful nonduplicate movement during active left drag renews activity. Passive hover, right/other motion, stationary/no-op movement, invalid coordinates/display/scale, out-of-session input and negative or stale timestamps do not extend the deadline or rewind activity; preserve existing geometry validation. Hover/right/other delivery must not consume a geometry point before a valid left-drag event. Optional timestamps retain legacy untimed caller compatibility. Both native and overlay pointer paths use the keyboard/watchdog monotonic clock domain. | MIT-02, MIT-05 | NOT_RUN — bind exact candidate test result |
| MIT-D4 | Renewal never changes the original capture/session timestamp or captured context. Pending regions remain intact before the renewed deadline, including across the old 30-second boundary. True idle expiry, explicit cancel and genuine interruption clear only the current unsubmitted session and retain truthful reasons. | MIT-02, MIT-03 | NOT_RUN — bind exact candidate test result |
| MIT-D5 | Exercise both release orders, repeated release/watchdog callbacks, release immediately before timeout, cancellation before release and existing watchdog-observed release precedence at the boundary. Completion is emitted exactly once when release wins; cancellation emits no completion when it wins. Reset and a fresh session discard old activity/deadlines/regions. | MIT-03, MIT-05 | NOT_RUN — bind exact candidate test result |
| MIT-D6 | Validate and round-trip `300000`. Upgrade temporary schema-1 and schema-2 fixtures with default `30000` to storage schema 3/default `300000`, inspect persisted fixture bytes, then reload to prove migration is one-time. Schema-3 deliberate `30000`, other valid custom timeouts and unrelated supported fields remain preserved, including shortcuts and reference capability root. Public API schema 2 remains compatible with the existing web client; update/save/reload preserves the deliberate current-schema timeout. | MIT-04, MIT-05 | NOT_RUN — bind exact candidate test result |
| MIT-D7 | Preserve corrupt, unsupported/newer-schema and unknown-field settings byte-for-byte without destructive rewriting or generating a replacement capability. Migration and service tests use temporary fixtures only; no installed settings file is changed. Saved references remain outside active-session cancellation. | MIT-03, MIT-04, MIT-06 | NOT_RUN — bind exact candidate test result |
| MIT-D8 | Retain and rerun the original native dual-delivery failing witness on R2. For a meaningful left-drag point near expiry, exercise raw native `HandleEvent` then overlay delivery, and the reverse order. Verify production callers provide monotonic timestamps, the first accepted geometry update renews, and the duplicate does not renew even with a later callback timestamp. Assert the same resulting geometry and idle behavior in both orders; repeat beyond five minutes of total session age. A reducer-only call with a supplied timestamp does not prove production native wiring. | MIT-02, MIT-05 | NOT_RUN — bind exact candidate test result |
| MIT-D9 | Interleave hover, right/other motion, duplicates, invalid coordinates and stale/negative timestamps with the two delivery orders. Confirm rejected/non-marking events neither consume geometry nor advance activity; a later valid left point can renew. Preserve original session provenance, exact-boundary survival, release-once and cancellation cleanup after both delivery orders. | MIT-02, MIT-03, MIT-05 | NOT_RUN — bind exact candidate test result |
| MIT-D10 | Load the actual bundled HTML/JS in a real browser engine and assert `maximum_hold_ms` accepts `300000` while `300001` reports range overflow/invalidity. Verify the inactivity label, fresh and migrated default population, and normal form submit/save/reload against an isolated API schema-2 fixture with storage schema 3. A deliberate current-schema `30000` survives. Verify API rejection of `300001` without changing stored bytes. A static max-attribute check, mocked form or API-only success is insufficient browser evidence. | MIT-04, MIT-05 | NOT_RUN — bind exact candidate test result |

Retain separate evidence for deterministic tests, native compilation, automated
browser validation and physical acceptance. Bind fresh results to the R2 source,
documentation and isolated integration commits separately; a documentation-only
CTest pass cannot establish that the source repair passed. Record the exact commands,
exit codes, test/harness hashes and outcome counts, including the full native
build/CTest run, the original failing witnesses and the prior 3242-case adversarial
suite rerun on the new candidate. Retain failures and unavailable checks rather
than reusing historical PASS. Verify the production native/overlay caller relation
as well as reducer tests. Browser evidence must identify the actual bundled asset
and isolated service/fixture; never point the fixture at installed settings or data.
The deliverable stays on the isolated candidate/integration branches until
independent QA and the existing integration gates evaluate the exact commit.

## 2026-09-30 first-launch menu entry — physical NOT_RUN

Candidate row: `SEETHIS-FIRST-LAUNCH-MENU-BAR-DISCOVERABILITY-20260930`.

The second-Mac field report for the old `69a0a84` build establishes that the app
initialized and remained running while its menu entry was invisible, including
when other menu apps were cleared. A later incoming field measurement reports
actual Regular policy while the old source requested Accessory; the cause and
transition remain unresolved. Those reports do not establish a deadlock or menu
overflow, and window presence does not prove a visible/clickable status button.
The new candidate needs its own observations; old media, screenshots and the
`42ae71e` review remain support provenance with affected captures marked **final-version-recheck**.

Record exact source commit/tree, distributable ZIP and executable SHA-256, bundle
path/identifier/version, code requirement/CDHash, UUID, PID, launch method,
macOS build, permission/settings states, display frames/scale/notch/menu-bar
layout, tester and timestamp. Observe on an isolated candidate with disposable
settings/references. Preserve the user's original app, data and TCC; no TCC reset,
Accessibility request, Gatekeeper bypass or manufactured permission dialog.

| ID | Required field observation | Author field status |
| --- | --- | --- |
| ENTRY-1 | Open the exact packaged app from Finder. Record the return value of the activation-policy request and the actual application policy separately, then the status item's existence/visible flag, button/window frame and owning screen. Record the user's visible/clickable menu entry separately; `visible=true`, a live PID or plist metadata alone is insufficient. | NOT_RUN |
| ENTRY-2 | Verify the entry stays compact across ready, Input Monitoring blocked, Screen Recording blocked and both blocked states. The tooltip and menu retain the full separate permission guidance and exact app location. Test limited menu-bar room and a cleared layout; record each result without assigning an unmeasured cause. | NOT_RUN |
| ENTRY-3 | With permission missing, verify the implemented Finder startup/reopen entry UI exposes current blocked states and recovery guidance without drawing, capture, an automatic Inspector action or a Chrome Automation request. Use the normal visible entry for explicit Inspector and settings actions separately; opening Inspector remains prompt-free. No permission grant is required to discover the entry UI; record unavailable controls separately. | NOT_RUN |
| ENTRY-4 | Quit and reopen the same packaged path from Finder, then reopen while that exact process is already running. Verify the explicit readiness panel recovery, actual policy and status-item geometry again; opening Inspector remains a separate explicit action. Identify which bundle/PID macOS selected when duplicate bundle IDs exist; retain any wrong-copy failure. | NOT_RUN |
| ENTRY-5 | Verify supported settings retain saved shortcuts/history; corrupt, newer or unknown-field settings remain preserved and fail closed. Separate Input Monitoring readiness, Screen Recording readiness, settings persistence and UI discovery. No new Accessibility request or implicit TCC request from entry discovery is introduced. | NOT_RUN |
| ENTRY-LOG | Compare exact baseline, instrumentation-only snapshot and final fix separately. Bind each to source diff/archive/executable SHA-256; record final package identity separately. The four diagnostic-only lifecycle checkpoints are `activation_policy_request`, `did_finish_launching_enter`, `status_item_created`, and one delayed `initial_layout_delayed`; verify request result, actual policy, status/button visibility and frame/screen, with no repeated records during refresh. Preserve actual build failures. Logs must contain no reference capability, clipboard/page content or screen pixels. | NOT_RUN |
| ENTRY-6 | After authorized permission recovery, independently perform the existing physical shortcut/capture/clipboard/hover/delete/focus rows on this exact candidate. Recheck affected first-run, menu and Inspector screenshots against the final binary. | NOT_RUN |

The final entry design uses a fixed 32-point status item with an 18-point
viewfinder template icon (fallback `ST`, accessible name `SeeThis`) and a separate
`SeeThis` readiness panel, opened by **Show SeeThis…**. Verify the
compact outward state and detailed panel state, with these explicit actions: **Show Inspector…**,
**Open Input Monitoring Settings…**, **Open Screen Recording Settings…**,
**Retry Input Monitoring**, **Open Settings…**, and **Quit SeeThis**.
The Show SeeThis panel's Inspector toggle reads **Hide Inspector** when already shown; the direct menu **Show Inspector…** action leaves an already visible Inspector visible. Startup presents guidance only
when needed; Finder reopen and the explicit menu action present it on request. The 0.5-second refresh updates existing
details and never presents or reopens a dismissed panel. Finder reopen presents
readiness even when another SeeThis window is visible; a fully ready launch stays
quiet. Verify these labels and behavior against the final packaged source.

The instrumentation-only source is the exact `42ae71e` export plus its recorded
`input_mac.mm` patch; it has no separate native source commit and keeps the old
entry behavior. Its local arm64 Release build passed, but the raw bundle failed
strict signature verification before packaging. The separate private clean
packaging snapshot built and strictly verified successfully; it remains diagnostic
only and was not launched. Its provenance binds baseline, patch and private HEAD
separately from the final native source commit.
The final fix must be archived and packaged from its own clean exact commit, with
Release/ad-hoc, arm64 and macOS 14.2+ metadata verified. Retain diagnostic build
failures if any; neither a successful build nor four records identifies the
root cause. Compare policy/layout observations with the actual visible entry.

For an explicitly accepted physical run, collect only the candidate process
records with a finite time window and filter; bind the process to the exact
executable and do not publish unrelated log output:

```sh
log show --last 5m --style compact --predicate 'process == "seethis" AND eventMessage BEGINSWITH "seethis.entry event="'
```

Expect four records per launch and none from the 0.5-second refresh. A dismissed
readiness panel must stay dismissed through refresh; explicit Finder reopen may
show it again. An already-running reopen must not launch a second candidate or
trigger Inspector/Automation implicitly.

Source tests and package signature/checksum checks are local implementation
evidence only. Every field row requires its own PASS/FAIL/UNAVAILABLE result and
bounded evidence. Do not log page titles, URLs/capabilities, clipboard contents or
screen pixels. The first-launch fix does not promote older physical gaps, native
HOLD evidence or the full beta to PASS.

## Input reconciliation race row

Candidate row: `SEETHIS-MAC-INPUT-RECONCILIATION-RACE-20260916`.

Author tests exercise the physical poll→raw→Carbon and raw→Carbon delete orders,
repeated polls, Carbon-first pending state, release recovery, consecutive clicks,
lost key-up/flagsChanged, same-generation stale press/release provenance, and the
capture watchdog's bounded release fallback. The existing 100 ms recovery timer is
installed in common run-loop modes so it continues during AppKit event tracking.
Native diagnostics expose bounded state/reason fields only; shortcut content,
capabilities and tokens are excluded.

The rework also rejects definitely older raw input after a Carbon-first press and
passes required capture-modifier presence into watchdog recovery. Extra modifiers
while the required modifier remains held do not count as release evidence.

Physical cross-app Option+D and Option+A acceptance remains **NOT_RUN**. Independent
QA must verify event delivery, focus, foreground text, mark deletion, capture output,
release ordering and permission state against the exact candidate without changing
the field app, TCC or settings.

## Physical interaction recovery row

Candidate row: `SEETHIS-MAC-PHYSICAL-INTERACTION-RECOVERY-20260915`.

Author tests cover Quartz-to-AppKit event-point conversion, bounded per-display
hover dirty bounds, the visible 4→7 width choice, foreground pass-through while a
delete chord is candidate or draining, ordinary-copy versus armed-delete pointer
policy, raw/Carbon orderings, stale generations, long-hold repeats, interruption,
and preservation of unsupported/newer/corrupt settings files. The candidate emits
privacy-bounded transition records for delete registration/raw/Carbon state, chosen
hover identity, dirty display bounds, AppKit draw acknowledgement and click/delete
disposition. It does not log capabilities, images or unrelated keys.

Independent QA must bind every result to the exact separately signed candidate,
signature/code requirement, process, macOS build, display geometry, saved Option+A
key `0`/modifier `4`, saved Option+D key `2`/modifier `4`, and current Input
Monitoring and Screen Recording states. Do not replace another app, reset TCC,
request Accessibility, or reuse field observations as a candidate verdict.

1. **Event point and hover presentation — NOT_RUN.** In the foreground app, move
   onto and off ready, pending and overlapping marks without clicking. Correlate the
   observed CG event point, converted display point, newest chosen ID, old/new dirty
   bounds and AppKit presented generation. Verify visible 4→7→4 width changes before
   any click, including across the available displays. Empty space and circle
   interiors must pass through and hover alone must not copy or delete.
2. **Physical Option+D chain — NOT_RUN.** Hold Option+D on a visibly hovered mark
   and record exclusive Carbon registration/callback, raw down/up and modifiers,
   one armed generation, click disposition and storage result separately. One left
   click must delete exactly that mark, consume that click, remove/tombstone only
   owned assets and make its old authenticated URL truthfully gone. No foreground
   text, focus change, unrelated deletion or second deletion may occur.
3. **Negative transition matrix — NOT_RUN.** Test Carbon-before-raw and raw-before-
   Carbon, long-hold repeats, both release orders, Option release/repress, stale
   generation, wrong/extra modifiers, interrupted or disabled tap, secure input,
   app/display changes and registration conflict. Candidate/drain clicks must reach
   the foreground; chord-only, hover-only, repeats and later clicks must not copy,
   delete or reactivate.
4. **Physical Option+A — NOT_RUN.** In a native editor and a non-AppKit app, hold
   Option+A, move and release. Verify exclusive capture ownership produces no `å`,
   repeated characters or foreground command, then verify exact event-point drawing,
   unchanged focus and text, original-window capture, stable pending URL and truthful
   terminal state.
   Report Input Monitoring, drawing, Screen Recording, capture, crop and clipboard
   results separately.
5. **Settings and reopen — NOT_RUN.** In isolated copies, confirm supported schema
   updates preserve all non-target values and the reference capability root. Feed a
   newer schema and schema 2 with an unknown field; verify the original bytes and
   historical URL capability remain unchanged, settings show preserved/unusable,
   shortcuts do not run, and an attempted update does not rewrite the file. Repeat
   after a real quit/reopen of this exact candidate. Corrupt input must also remain
   preserved and fail closed without minting a replacement reference capability.
6. **URL no-regression — NOT_RUN.** Verify ordinary ready/pending/historical mark
   clicks still copy the same compact scoped local URL and resolve through the
   existing async lifecycle without a delete authority or cloud-access claim. The
   separate URL-only pasteboard-type requirements belong to the later UC row.

The source author does not launch or operate the candidate, inject input, inspect
the pasteboard, touch current settings/references, or change permissions. All real
shortcut, hover, deletion, focus, pointer, TCC and system-reopen evidence remains
`NOT_RUN` until independent QA performs these steps.

## Mark hover and deletion row

Candidate row: `SEETHIS-MAC-MARK-HOVER-DELETE-20260915`.

Deterministic author tests cover the pure delete-chord reducer, newest overlapping
stroke selection, separate ordinary copy and delete paths, durable ready/pending
deletion, post-store completion races, partial cleanup/retry, restart/nonrecycling,
unaffected history, settings migration/conflicts, readiness states and authenticated
`410` responses. Those checks do not prove Carbon delivery, focus, foreground text,
AppKit hover rendering or physical pointer consumption.

Independent QA must bind results to the exact candidate bundle, signature/code
requirement, macOS version, saved capture/delete bindings and current permission
states. Do not alter TCC or request Accessibility.

1. **Hover and ordinary copy — NOT_RUN.** Create overlapping marks across the
   available displays. Move across stroke tolerance, circle interiors and empty
   overlay regions. Verify only the newest matching stroke thickens, the prior
   bounds redraw cleanly, hover never copies, empty/interior points pass through,
   and an ordinary mark click copies the same compact scoped URL without focus loss.
2. **Physical Option+D chord — NOT_RUN.** In native and non-AppKit text editors,
   hold Option then D, hover one mark and left-click once. Verify exactly that mark
   disappears, the click does not reach the foreground app, no `∂` or other text is
   inserted, and focus remains in the editor. Hold through autorepeat before the
   first click and verify the chord stays armed for that one deletion; after that
   click, repeated deliberate clicks can delete further marks in the same hold, while key autorepeat cannot delete. This is the required evidence for
   the conditional Carbon design.
3. **Release/order/interruption matrix — NOT_RUN.** Exercise D-first, Option-first,
   both release orders, Option release/repress while D remains down, autorepeat,
   Escape, app switch, resign-active, display change, secure input and tap disable.
   Every path must exit or drain until a fresh D-down and must not stick, repeat,
   copy or delete.
4. **Binding and readiness — NOT_RUN.** Verify Option+A capture remains unchanged,
   saved custom capture/deletion bindings work separately, a conflict is rejected
   or reported unavailable, exclusive registration failure is explicit, and denied
   Input Monitoring or secure input never falls back to raw deletion.
5. **Ready cleanup and old URL — NOT_RUN.** Delete a ready mark and inspect only its
   owned Application Support files. Confirm unrelated references and exported copies
   remain, repeated deletion is harmless, cleanup failure is visible and retryable,
   and the old local viewer resolves to an authenticated deleted/gone state. The
   copied read capability must not authorize an HTTP deletion.
6. **Pending and race paths — NOT_RUN.** Delete while capture, encode and store are
   each pending, including multiple concurrent jobs and completion/delete ordering.
   Verify no late mark, file or pasteboard replacement appears and restart retains
   the tombstone while preserving unrelated history.

The source author did not launch an app, inject input, read or change the pasteboard,
or alter permissions. The previously observed PID67474 is no longer running; its
retained 6b bundle and historical user artifacts remain protected and unchanged.

## Asynchronous reference jobs row

Candidate row: `SEETHIS-MAC-ASYNC-REFERENCE-JOBS-20260915`.

Deterministic tests cover stable-ID pending/ready/failed transitions, bounded worker
and memory admission, delayed and out-of-order completion, fake-clock timeout,
durable pending restart recovery, save failure, shutdown, finite viewer polling,
lightweight status lookup, and clipboard generation ownership. They do not prove
physical key-up latency, AppKit scheduling, system pasteboard ownership, real capture
cost, focus retention, TCC, or a 500-entry native history.

Independent QA must measure 20 warmups followed by all 200 predefined attempts for
each key-load scenario. Record release-to-actual compact pasteboard publication for
every success, rejection, timeout and failure; use nearest-rank sample 190 for p95
and keep the backpressure cohort separate. The target is p95 <=100 ms. Exercise 500
mixed history entries, including several former >=27 MiB text-sized records, and
record status/hover/click responsiveness separately from slow image retrieval.

The author does not launch this candidate or touch the protected retained 6b app.
Physical shortcut/focus/TCC/real-window cases remain **NOT_RUN** by the author.

## Input-permission recovery row

Candidate row: `SEETHIS-MAC-INPUT-PERMISSION-RECOVERY-20260914`.

Input Monitoring readiness, Screen Recording readiness, settings persistence,
physical Option+A hold/move/release, circle creation, capture and clipboard output
are separate results. Neither a settings HTTP success nor a non-null event tap
proves usable keyboard monitoring. Screen Recording preflight does not prove correct
capture coordinates or content.

### Deterministic author scope

The native state model covers denied authorization, tap creation failure, an enabled
partial tap without key-down/up, disabled/interrupted monitoring, restart required,
initial granted readiness and recovered readiness. Only an authorized, enabled tap
whose observed event mask contains key-down, key-up and modifier events is ready.
Service tests cover authenticated readiness retrieval, separate input/screen facts,
exact app identity/location, update guidance, every input state, bounded retry
authorization and separation of settings persistence from readiness.
The native launch indicator has deterministic coverage for ready, Input Monitoring
only blocked, Screen Recording only blocked and both blocked states. The menu keeps
separate status and recovery rows for Input Monitoring and Screen Recording and
retains the exact displayed path and Reveal action. Multiple copies with the same
bundle identifier, system Quit & Reopen selection, and manually reopening the exact
path require physical macOS observation.

### Independent live matrix — author result: NOT_RUN / UNPROVEN

Independent QA must bind every result to the exact candidate path, bundle ID,
signature/code requirement, PID, saved shortcut and current System Settings entry.
Do not reset TCC, grant permissions or request Accessibility as part of an ordinary
readiness observation.

1. **Blocked startup discovery — NOT_RUN.** Launch with ready, Input Monitoring only
   missing, Screen Recording only missing and both permissions missing. Verify the
   compact menu entry remains discoverable in all four states; tooltip, menu and
   readiness panel expose the separate blocked states and recovery guidance. Record
   permission-dependent behavior only from the observed candidate and use the
   first-launch entry row above for Finder startup/reopen checks.
2. **Denied Input Monitoring — NOT_RUN.** Verify the menu and authenticated page
   report denied/not ready, exact app path and user-driven System Settings guidance.
   A settings read/save must remain separately successful where authorized.
3. **Tap failure and partial tap — NOT_RUN.** With controlled native probes, verify
   a creation failure and a non-null mouse/modifier-only tap remain not ready.
4. **Disabled/interrupted monitor — NOT_RUN.** Trigger a supported tap interruption,
   verify cancellation and not-ready state, then use one explicit retry. Listener
   recovery is distinct from drawing or capture success.
5. **Multiple-copy system relaunch selection — NOT_RUN.** When multiple copies with
   the same bundle identifier are present,
   observe whether macOS **Quit & Reopen** selects a different copy; quit extra
   copies and manually reopen the exact displayed path. This row stays NOT_RUN until
   that real system selection and permission-dependent behavior are observed.
6. **Restart-required update — NOT_RUN.** Exercise an actual app update against an
   old grant. Verify the new exact app/code requirement is identified and that a
   failed in-process retry asks the user to quit and reopen the same bundle without
   losing saved Option+A or stored references.
7. **Granted and recovered monitoring — NOT_RUN.** Verify the current process's
   enabled tap includes key-down, key-up and modifiers. Record initial granted and
   post-retry recovered states separately.
8. **Screen Recording — NOT_RUN.** Verify denied and ready states independently of
   Input Monitoring. A ready preflight remains distinct from correct window capture.
9. **Physical saved Option+A — NOT_RUN.** With `key_code=0` and `modifiers=4`, hold,
   move and release in a real foreground app. Record listener state, circle, focus,
   capture, crop and clipboard as independent facts.
10. **Authenticated browser and native menu — NOT_RUN.** Verify both show the same
   current states, identity/location and guidance; saving reports persistence only;
   retry requires the process capability and exact Origin. Verify normal menu entry
   directly rather than substituting debugger invocation.

### Preserved user-side history

The protected 6b app is the historical artifact at
`author-candidate-6b62f8a/artifacts/SeeThis.app`; during an earlier author session
its PID was 67474 and its saved settings remained Option+A (`key_code=0`,
`modifiers=4`). A later read-only check found no running SeeThis process. The
retained bundle was not overwritten, re-signed or replaced.

Earlier user-side evidence showed Input Monitoring recovery followed by a distinct
Screen Recording denial. Newer user evidence reports a schema-2 historical reference
from the Codex window with a 2559×1410 full PNG, a 137×146 crop, valid CRCs and a crop
SHA matching the fallback. That later evidence means capture must not be described
as never successful. It still does not establish full acceptance: the white crop
needs a coordinate/object retest, and listener recovery, physical input, focus,
circle, capture and clipboard remain individually assessed. The author did not
reproduce these live interactions.

### Signing and update policy

Development packaging is explicit and uses ad-hoc signing; it reports that a
rebuild may change identity and require exact-bundle reauthorization. Stable
packaging requires an exact available identity name or SHA-1 and one explicit
mode. `--stable-action bootstrap` accepts no prior app, makes no inherited-TCC
claim and warns that field authorization may be required.
`--stable-action update --prior-app PATH` verifies the prior app's signature and
its signature-covered provenance, then requires the same identity fingerprint,
bundle ID and designated requirement. Omitted, caller-authored or incompatible
prior evidence fails closed. This deterministic check does not prove real TCC
continuity.

Packaging accepts only a clean Git checkout at the requested exact HEAD. It
archives that commit and builds the archive in task-owned storage; arbitrary
`--bundle` and provenance-text inputs are rejected. The signed provenance
records the commit/tree, source-archive hash, toolchain hashes, actual bundle ID
and actual `CFBundleExecutable`. Retained and extracted app trees, executable
bytes, CDHash and requirement must match.

Read-only preflight requires a nonempty, parseable Launch Services inventory
containing complete application records and a nonempty, parseable process
inventory. Successful empty output is incomplete, and a candidate bundle-ID
record without its path is truncated. It resolves every absolute running
`Contents/MacOS/*` path to the enclosing app before comparing bundle identity,
even when its executable basename differs. It permits only the exact candidate, a declared canonical
update target with compatible identity, or an explicitly manifested owned app
whose canonical path, signed provenance, complete tree hash, bundle ID and
requirement all match. A name containing `staging` or `audit` grants no
exception. Empty, failed or malformed inventories, blank manifest fields, failed
hash/requirement computations, stale PIDs, unresolved registered or running
copies, invalid signatures and a running canonical target block.

Acknowledged install copies to task-owned staging on the destination filesystem,
verifies it before mutation, swaps by rename and restores the previous app after
copy, validation or registration failure. It registers the exact verified target
only after the swap. Acknowledged launch snapshots the installed executable path,
hash, CDHash, requirement and complete app tree before opening the exact path; it
also retains the complete prelaunch PID inventory. It requires one unique live
PID absent from that full baseline and rejects incomplete baselines, pre-existing
PIDs, zero, multiple, stale, wrong-path or post-open-mutated evidence. Neither path kills, unregisters or
deletes unrelated copies, resets TCC, changes settings or inspects private keys.

Candidate row: `SEETHIS-MAC-AGENT-BRIDGE-20260913`.

This matrix keeps deterministic author evidence separate from independent physical
macOS interaction. A build, unit test, launch-survival interval, HTTP fixture or
signed bundle cannot prove the cases marked NOT_RUN or UNPROVEN below.

## Author-owned deterministic scope

Author host: macOS 26.5.1 (25F80), arm64, Apple M4 Max, Apple clang 21.0.0
(`clang-2100.1.1.101`), Command Line Tools SDK. Connected displays reported during
the author session were the built-in 3456x2234 Retina panel and an XG27ACS
2560x1440 external display. No permission setting was changed and no input was
injected.

The declared CMake/Ninja workflow, exact commands, exit codes, compiler database,
sanitizer results, signed retained app and hashes are retained in the external
author-candidate evidence directory tied to the immutable commit. CTest contains
three executables: the interaction state machine, immutable reference flow, and
reference server/settings security tests. Internal assertions are reported as
checks within those three tests, not inflated into separate test cases.

The server/settings executable uses an actual ephemeral 127.0.0.1 socket and a
temporary on-disk store. It covers owner-only and atomic settings persistence,
stable scoped reference capabilities, separate ephemeral settings capability,
authorized settings GET/PUT, bad Origin/content type/schema, missing/wrong bearer,
invalid IDs and traversal, duplicate headers and transfer encoding, unsupported
methods, explicit unknown 404 and expired 410, byte-exact record/source/crop, local
clipboard URL plus remote attachment fallback, stable historical re-copy text, and
server shutdown. The reference fixture includes a multi-megabyte realistic source
and a 59 MiB stored record; its exact single URL remains below 2 KiB at maximum
port and does not include either payload. The copied `/r/<id32><cap64>` URL is a
plain JSON read URL with no query or fragment. The Inspector's separate
**View details** action retains the HTML viewer and its fragment-scrubbing, Bearer-fetch
and SHA-256 verification contract. Strict Host/Origin and no-store/no-referrer
response policy applies to both. A distinct local-tool check follows the JSON URL
and retrieves metadata and byte-exact image assets.

## Independent physical workflow — author result: NOT_RUN / UNPROVEN

Record the exact candidate commit/app hash, macOS build, hardware, display
UUID/frame/scale, permission state, foreground app/version, selected window ID,
starting pointer, pasteboard representation hashes and local-agent output for each
case. Independent QA owns the verdict.

1. **Hold-circle-release and focus — NOT_RUN.** Focus a real native AppKit editor,
   then a non-AppKit app. Hold the configured shortcut, draw, and release. Verify
   the source app remains frontmost with its caret active and receives no synthetic
   keyboard changes. Repeat after changing the shortcut and watchdog in the browser.
2. **Pointer preservation — NOT_RUN.** Verify empty overlay regions pass clicks and
   drags through before, during pending capture, after success and after every
   failure. Verify only drawing and an actual historical-mark hit consume pointer
   input. Change the hit radius and confirm the new boundary without focus theft.
3. **Real capture and crop — NOT_RUN.** Use asymmetric known pixels in overlapping
   windows. Confirm the exact selected window, original PID/title/frame, overlay and
   cursor exclusion, source dimensions, crop orientation/margin, timestamps and
   source/crop hashes. Exercise the two connected displays, negative origins,
   mixed scale if available, a spanning window and a cross-display path.
4. **URL-only pasteboard — NOT_RUN.** Copy pending, ready, failed and restarted
   historical marks and record the pasteboard change count, complete type list and
   UTF-8 bytes. Each deliberate action must publish exactly one authenticated
   `http://127.0.0.1:<port>/r/<id32><cap64>` URL, below 2 KiB, as plain text.
   The handle must be 96 lowercase-hex characters with no query or fragment.
   Title, JSON bytes, private record type, PNG, file promise,
   deferred provider and provider-specific enrichment must all be absent. With a
   former record at least 27 MiB, confirm copy latency and memory do not scale with
   the durable record or image. Let pending jobs complete both while the URL remains
   and after copying unrelated content; neither completion may change the count or
   bytes. Only a later deliberate mark click may republish the same stable URL. Run
   20 warmups followed by all 200 predefined attempts for URL publication and for
   hover as separate cohorts. Preserve original attempt order and status; compute
   nearest-rank p95 from a separate sorted copy and retain backpressure separately.
5. **Same-Mac JSON and viewer — NOT_RUN.** Open the copied URL on the same Mac.
   Record the browser/version, visible address/history and JSON response. The full
   capability URL remains in the address bar; no fragment scrubbing is expected
   for this JSON URL. Verify no-store/no-referrer headers and scoped
   `/r/<same-handle>/source.png`, `/crop.png`, `/annotated.png` asset URLs (and
   schema-1 `/record` where applicable). Separately use Inspector **View details**
   and record its legacy HTML viewer's scrubbed fragment, HTTP request targets,
   status text, displayed full-context dimensions and downloaded/displayed PNG SHA-256.
   Confirm that the preview and annotated full-context download contain only the
   original freehand subpaths, with no automatic crop/bounds rectangle, while the
   selected crop remains available through its explicit action. Read
   the historical metadata panel and record application name, window title, bundle ID,
   all four separately labelled UTC-microsecond timestamps, every display's stable ID/UUID,
   logical and backing-pixel bounds, dimensions and scale, and each region/path display
   association. Confirm the panel labels display-local logical points, region bounds as
   shared global AppKit logical points (Y-up), backing pixels as top-left/Y-down, and
   source-image crop pixels as top-left/Y-down. No legacy viewer HTTP request
   target may contain its fragment. Verify missing, wrong, expired and
   cross-ID capabilities fail without displaying an unverified image. On a host
   without JavaScript, loopback access or WebCrypto, record the explicit unsupported
   result rather than treating it as a product pass.
5a. **Inspector annotated-image copy — NOT_RUN.** On the exact candidate app,
   select a ready reference and press **Copy full-screen image with marks**. Record the
   reference ID, source PNG bytes and SHA-256, annotated PNG bytes and SHA-256,
   pasteboard type list and change count before/after, elapsed milliseconds from
   click to completion, and the displayed Inspector status. Keep byte counts,
   change counts and elapsed time in QA evidence; success text should show only
   copied, final dimensions and a downsampled note when applicable. Expected:
   one PNG image representation of the full-screen context and original freehand
   subpaths, with no crop/bounds rectangle or stale content. Its dimensions
   equal the source within the helper's size limit, or the documented downscaled
   dimensions above it. Confirm the source asset digest
   matches the selected reference's annotated asset. Change selection and make
   an unrelated pasteboard write during conversion; record whether the in-flight
   copy publishes afterward and verify stale Inspector status is suppressed.
   Repeat with pending, failed, expired, deleted and missing references; the
   action must be disabled or report unavailable without changing the pasteboard.
   Repeat with a slow/timed-out conversion and record its five-second deadline
   result, output size and elapsed time. Background decode/encode may continue
   after timeout, but must not start a late pasteboard write; a write already in
   progress may exceed the deadline. Finally click an ordinary mark and confirm
   only the compact JSON URL is published; Inspector refresh, hover and capture
   completion must not publish an image. Source size: **NOT_RUN**; output size: **NOT_RUN**;
   elapsed: **NOT_RUN**. Real TCC and browser webchat paste: **NOT_RUN**, pending
   independent candidate-field QA.
6. **Clipboard and local agent — UNPROVEN.** Paste into a real explicitly local
   agent workflow. Have its tool GET the copied 127.0.0.1 JSON URL and then the
   scoped asset URLs, keeping the capability within that local workflow. Separately
   verify old `/v1/references/<id>/...` Bearer reads. Compare every returned
   hash/identity/provenance field with the pasteboard and saved reference. On the
   old Bearer routes, verify missing token 401, wrong/cross-ID token 403, unknown
   valid-shaped ID 404, and expired known ID 410. Record loopback metadata and
   crop latency independently over repeated requests, including the
   timeout threshold and each timeout. A timeout is an observation and does not by
   itself establish its cause.
7. **Remote boundary — UNPROVEN.** Paste the same URL into a remote-agent
   workflow. Confirm it does not claim its localhost reaches the user's Mac. Attach
   a PNG obtained separately through the authorized local route or exercise an explicitly configured bridge, then verify
   the remote workflow identifies the same crop hash. Pasting the loopback URL into
   cloud chat must not be treated as making it cloud-accessible.
8. **Old-mark re-copy — NOT_RUN.** Create two marks, record the URL-only pasteboard,
   move/change/close their windows, click each mark, and verify the immutable private
   record and PNG remain retrievable locally. Verify the URL resolves the same ID with a current local
   endpoint. Restart SeeThis and repeat; verify the scoped capability still works.
9. **Browser lifetime and settings access — NOT_RUN.** Close the settings tab and
   browser, then confirm the accessory app, overlay and listener remain alive and a
   new circle still works. Verify an old settings session capability stops working
   after app restart and no capability appears in page source, browser history,
   app output or evidence. Reopen settings from the SeeThis menu-bar item and
   confirm the new session works.
10. **Declared failures — NOT_RUN.** Exercise denied/revoked Screen Recording,
   unavailable/closed target, capture error/timeout, Escape before and after release,
   app switch, focus loss, display change, input-monitor interruption,
   clipboard failure and a superseding generation. Each path must release input,
   avoid fabricated success and suppress late callbacks; clipboard failure may leave
   the exact reference persisted for retry.

Windows migration starts only after the macOS acceptance above has an independent
usable-product disposition. This row contains no Windows or Linux backend, daemon,
deployment or release.

## Agent annotated full-context PNG AAP1–AAP5 — physical NOT_RUN

Use newly created disposable references, leaving saved user references and
pasteboard contents untouched. For 1x and 2x displays including a negative
origin, place asymmetric known colors across the full captured source and draw
two disjoint regions, one with an explicit leave/re-entry subpath. Fetch the
authenticated metadata and `annotated.png` from a local agent. Record first
202 and eventual 200, response-byte SHA-256 and ETag, ready metadata digest,
dimensions, and a repeat/relaunch fetch with identical bytes. Compare pixels
outside the strokes against `source.png`; verify every freehand segment appears
at the correct source pixel and no region rectangle or connecting chord is
drawn. Confirm the existing browser viewer export, source PNG and crop route
still work, and that no implicit clipboard write occurs. Delete the disposable
reference and confirm 410 and owned-file cleanup. Physical and local-agent
results remain **NOT_RUN** until recorded against an exact candidate build.

### Marked HTTP cross-reference field recheck — NOT_RUN

The 2026-10-01 exact `62ec702` synthetic diagnostic establishes repeated A 202
under B-metadata interleaving, including after store/server reload. It does not
establish the historical acceptance-machine root cause, which remains **UNKNOWN**
without original response headers, B request traffic and PNG response bodies.
Use the bounded procedure in `reference_flow_acceptance.md` for technical
candidate tests; retain the original diagnostic and actual next-candidate results
separately. No source fix or already-passed field acceptance is asserted here.

On an independently accepted exact app/package, record app commit/tree, executable
SHA-256, signature, actual process/path, macOS build, client and fixture identity.
On the acceptance Mac, use disposable A/B references and collect the timestamped
metadata/image request sequence, 202 retry fields, 200 content type, ETag,
`X-SeeThis-PNG-SHA256`, actual PNG byte count/digest and ready metadata digest.
Include repeated/concurrent A, B-metadata/A-image interleaving, stopping B, and
quit/reopen with the same fixture. A file already on disk does not establish
client delivery. Record crop/source integrity, authentication, deletion and
retention observations independently, with unknown or untested cases left open.

Cross-Mac delivery needs a supported channel carrying actual PNG bytes; a
`127.0.0.1` reference URL addresses the originating Mac only. Record the receiving
PNG digest and decoded pixel/full-context/freehand checks rather than treating a
loopback fetch on the originating machine as remote acceptance. Keep credentials
and private data out of public evidence; do not reset TCC, change the user's app
or silently mutate the clipboard during these procedures.

Installed-app and cross-Mac observations remain **NOT_RUN**. AAP6 continues to
name exact `0e2e7afff7d5859472ea56cff875e0a2e76e02f2` for its isolated consumer
acceptance. This recheck does not replace AAP6, rewrite historical **NO-PASS** or
establish native overall delivery PASS.

## Quiet reference inspector QI1–QI5

Author deterministic checks cover quiet geometry-only presentation for pending,
indexing and failed jobs, ready-only pointer claiming, a metadata-only list of
all retained non-deleted lifecycle states, hidden-state persistence, list
selection, deleted-row suppression with retained tombstone truth, and
truthful recopy failure. A successful URL-only publication has the brief
nonactivating acknowledgement `Link copied — paste into chat`. These checks do
not establish physical focus, click-through, panel drag, or pasteboard behavior;
those cases remain **NOT_RUN** for independent QA.

## Mark inspector panel PN1–PN5

**Physical inspector acceptance — NOT_RUN.** Independent QA should create multiple
marks and verify that ready circles have no permanent timestamp or `Historical`
label, then select a circle and inspect source app/window, interaction and capture
times, location, dimensions, display/coordinate context, short identity and
lifecycle. Verify pending→ready updates without a pasteboard change, failed and
expired states are understandable, deleted rows stay absent while URL/API truth
remains available, and selected deletion clears the panel. Drag the panel, hide
it, reopen it from the native menu, and verify hidden
pass-through, focus retention, Option+D deletion and Option+A capture. Move or
disconnect the panel's display and verify it returns to a visible screen. These
physical UI, focus, pointer and display-layout observations are not proven by the
deterministic interaction test.

**Rework physical rows — NOT_RUN.** Hide the inspector, complete another circle,
click a mark, and allow a pending capture to complete; confirm it stays hidden
until the native menu explicitly reopens it. With long app/window/display names,
scroll through the compact panel and verify Selection region is labeled as shared
global AppKit logical points with Y increasing upward, while Crop pixels is
labeled as source-image coordinates with a top-left origin and Y increasing
downward. Verify both x/y/width/height rows, Source dimensions, and Crop
dimensions are reachable. These remain physical macOS observations for
independent QA.

## Capture cancellation recovery CR1–CR5

**Deterministic/source evidence.** The interaction reducer completes a valid path
once when A releases first, when Option releases first, when both release events
arrive together, and when the watchdog observes A already up before its queued
key-up arrives. Autorepeat remains inert. Escape and explicit application, focus,
display, input-monitor and maximum-hold interruptions retain named cancellation
reasons. The overlay carries that reason into `Reference cancelled: …`; invalid
paths and capture failures retain separate failure outcomes. Native capture logs
contain only bounded capture state/reason/timing and path-sample counts.

**Physical recovery matrix — NOT_RUN.** In a separately authorized candidate,
draw valid circles and exercise A-before-Option, Option-before-A and genuinely
near-simultaneous release. Confirm exactly one pending URL/reference per circle,
no silent disappearance and no duplicate after the later release or watchdog.
Then separately exercise Escape, foreground-app switch, SeeThis focus loss,
display change, input-monitor interruption and maximum-hold timeout, confirming
the inspector/status and Console `seethis capture` reason agree. Repeat with a
closed target, invalid path and failed Screen Recording capture. Inspect that log
entries contain no pointer coordinates, pixels, screenshot content, reference
URLs/capabilities, clipboard contents or unrelated keys. These event delivery,
focus, Console and user-visible results remain **NOT_RUN / UNPROVEN** here.

## ST-01 standalone candidate boundary

The standalone local candidate adds deterministic coverage for display-edge
clipping, outside release, neighboring-display re-entry, negative global origins,
mixed backing scales, explicit contiguous subpaths, persisted metadata, hit testing
and viewer projection. Physical packaged multi-display drag acceptance remains
`NOT_RUN` for this candidate.

## Multi-region capture and readiness latch — independent QA NOT_RUN

With a separately packaged MG candidate and a text editor frontmost, hold Option+A.
Draw two separated regions with distinct left-down/drag/up sequences, move through the
gap without a button, then release A and later Option. Verify the editor stays
frontmost, drawing clicks do not reach its text, the gap has no connecting line, and
exactly one URL/reference is published. Repeat Option-first release during the second
drag, and verify the active valid region closes once. Empty movement, a point-only
drag, Escape, lost key-up, app/focus/display change and tap interruption must publish
nothing and restore ordinary input.

Verify metadata and inspector show two regions over one selected-display source
image, while re-copy returns the same URL and deletion removes the whole reference.
Repeat on mixed-scale displays and attempt to cross the selected-display boundary;
the other display must contribute no sample. Relaunch and verify a schema-2 reference
retains both regions and a schema-1 reference appears as one region.

For readiness, observe healthy→disabled→healthy and incomplete-tap recovery in Console
and menu/API state. Confirm attempts are spaced and stop after the bound, denial stays
denied until permission is restored, Retry remains usable, and interruption never
replays or auto-commits capture/delete state. Record only state/reason/time/attempt
fields. These physical input, focus, TCC, overlay, display, screenshot and pasteboard
results remain **NOT_RUN / UNPROVEN** in author evidence.

## Chrome page visibility source boundary

Automated PV1-PV7 source checks cover identity redaction and round trips,
same-window different-tab mismatch, same-tab same-URL reload retention,
navigation/back restoration, browser process-start identity changes, denied and
stale observations, bounded provider completion, and the shared paint/hover/
hit/copy/delete gate. The required user path for a denied preflight is manual:
authorize SeeThis -> Google Chrome in System Settings > Privacy & Security >
Automation and retry. Opening Show Inspector only reveals the current state;
it does not request Chrome Automation permission. Use the existing explicit
Connect Chrome or capture-shortcut consent path when testing a request. No
test or package action requests permission.

For Chrome readiness recovery, launch the candidate app and keep Google Chrome
frontmost on a known normal window. First make a mark while Chrome Automation
is unavailable, release it, and record the privacy-safe `seethis chrome` log
phase, cause, OSStatus (when present), request generation, PID and window ID.
Confirm the Inspector says the current attempt failed before it was saved and
its Current operation names the observed permission or readiness category;
confirm no reference or clipboard URL was persisted. Hide and show Inspector
while Chrome remains frontmost, including after a cached non-ready observation;
confirm the Inspector reports the current non-ready state without requesting
consent. Then use an explicit connection or capture-shortcut action and record
its grant, denial or unavailable result. After granting consent,
retry a mark and confirm its page identity, app and window visibility all gate
paint, hover, hit, copy and delete. Repeat with denied, unavailable, ambiguous
and timed-out acquisition where those states can be induced. A late result
must not bind to an aborted or superseded mark. Record the exact candidate app
identity and log evidence without URL, title, pixels or tab token.

Real Chrome Apple Events, physical tab switching/navigation, TCC continuity,
and Codex task/page identity remain NOT_RUN until exercised with the candidate
app; source and build checks alone do not establish physical acceptance.

## Viewer/context author boundary

The checked-in server test exercises the authenticated full-display source route and
its PNG signature, content type, response-byte SHA-256/ETag, independent two-region
metadata/path geometry, pending/indexing and queued/deriving transitions, capability
failure, expiry and deletion. The fixed viewer marks metadata and contextual-image
timings separately, enforces a two-second fetch/body deadline, keeps the capability in
the current tab session for one explicit selected-crop download, and preserves the schema-2 RGBA
pixel-integrity contract separately from the encoded crop response hash.

Real packaged-browser open/reload, DOM/canvas decode and paint, both visible marks,
Save PNG interaction, and physical display/capture alignment are **NOT_RUN** by the
source author. Source or JavaScript harness evidence is not a real-browser PASS.

## PL physical latency matrix — NOT_RUN by source author

On the separately authorized signed candidate, record at least 20 cold and 20 warm
successful captures for each representative display class, preserving attempt order,
status, pixel dimensions, scale and build identity. Correlate the privacy-safe native
request/callback and conversion log with release-to-URL, release-to-ready, first
metadata poll, primary source response, WebCrypto verification, image decode,
freehand annotation and first visible paint. Measure an explicit crop request
separately; schema 2 must not wait for it before showing the full-context source.
Record median, nearest-rank p95, peak resident memory and persisted
bytes. Keep failures in the cohort rather than retrying them away.

Verify the clipboard contains the URL before image work completes and never changes
at completion. After relaunch, verify metadata is immediately lightweight, the first
cold crop has the persisted RGBA identity while its response SHA-256/ETag match the
actual returned bytes, no legacy record/crop file appears, and subsequent warm display
remains truthful. Repeat deletion, expiry and schema-1 historical recopy.
ScreenCaptureKit/TCC/focus, real browser decode/paint, FC full-context correctness and
installed-app peak memory remain **NOT_RUN / UNPROVEN** until this matrix is executed.

## Stable delivery identity author fixture

The bounded author fixture invokes `scripts/package_macos.sh` only with an
isolated clean repository and controlled tool mocks. Positive cases cover a
fresh lowercase-`seethis` package, explicit first-stable bootstrap, compatible
N→N+1 update, strict archive equality, read-only preflight, manifested owned
artifact classification, safe non-running update, exact registration and one
unique exact-path launch.

Negative cases assert exact reasons for prebuilt input, missing stable selector,
missing prior app, identity substring, invalid prior signature, incompatible
requirement, changed bundle ID, archive resource mismatch, invalid candidate
signature, an arbitrary path containing `staging`, a genuine duplicate, owned
manifest tree/bundle-ID mismatch with the same requirement, unavailable or
empty/malformed process snapshots, an empty or candidate-truncated Launch
Services snapshot, a running same-bundle app with a different executable name,
blank manifest hashes, failed tree/requirement hash computations, stale PID,
running canonical target, staging-copy failure, registration rollback, missing
acknowledgements, incomplete/pre-existing-PID launch baselines,
zero/multiple/stale/wrong-path launch processes, and post-open
executable/requirement mutation.
Rollback cases assert that the previous canonical sentinel survives, and package
cases assert no registration or launch side effect. No field app, TCC, settings,
keychain, certificate, user artifact, real Launch Services state or private key
is touched. Physical TCC continuity, live Launch Services behavior and field
acceptance remain **NOT_RUN / UNPROVEN**.

## Chrome same-page hover and Inspector history actions

Candidate row: `SEETHIS-CHROME-HOVER-AND-INSPECTOR-ACTIONS-R1-20260925`.
Author source tests drive a disposable ready Chrome reference through fresh exact
window/page refresh and pointer positions outside, on, and off its stroke. They
check the paint/hit projection, overlay pointer decision, and nonempty dirty
bounds together. This sequence currently passes; it does **not** reproduce the
reported disappearance or prove AppKit paint. The saved-reference retention
period is separate from the two-second freshness requirement for foreground
window and Chrome page observations. No reference expiry or source hover cause
has been demonstrated. The native `physical-hover` identity, transition, dirty,
and presented records are available for a correlated physical trace. They must
be interpreted with a visible before/hover/after outcome, not used alone as a
PASS claim. Do not relax the exact-page visibility guard on a hypothesis.

Independent QA uses only newly created synthetic references in an idle QA Chrome
window and the exact candidate app. Keep the user's saved references and browser
tabs untouched. Record candidate identity, synthetic reference IDs, observation
availability/generation/age, exact window/page match, visible mark count, hit ID,
pointer policy, dirty bounds, presented generation, and visible stroke before
approach, during hover, and after leaving. Never record a viewer capability, URL,
page title, tab token, screenshot pixels, or clipboard contents in logs.

1. **HOVER-1 — NOT_RUN.** On the originating ready QA Chrome page and window,
   approach and cross a synthetic mark without clicking. If it disappears,
   correlate the first failing transition with the window/page freshness gate,
   pointer handling and redraw records. If the identity remains fresh and exact
   while paint or hit disappears, capture that native failure separately from
   the passing core sequence.
2. **HOVER-2 — NOT_RUN.** Switch to a different app, window and tab, then navigate
   the QA tab. Verify live paint, hit, recopy and live delete refuse each mismatch
   and denied/stale observation. Return to the exact fresh QA page and verify the
   undeleted mark reappears and can be hit. Do not delete it during this check.
3. **INSPECTOR-1 — NOT_RUN.** Select a synthetic saved ready reference, move to a
   different foreground app/tab, and press **View details**. Verify the existing
   authenticated local viewer opens without a clipboard change. Expire a separate
   disposable fixture through the existing retention test setup and verify its
   viewer reports the existing expired/410 state. Missing, deleted, failed and
   unready selections report unavailable without opening a viewer.
4. **INSPECTOR-2/3 — NOT_RUN.** Select one disposable persisted ID, change the
   foreground app/tab, and press **Delete**. Verify only that ID is tombstoned,
   its ready viewer is revoked, its owned files are cleaned and the selection
   clears. Other synthetic references remain intact. Repeat with the stale ID;
   no newly selected reference may be deleted. Exercise a pending completion and
   both pre-tombstone and post-tombstone cleanup failures with isolated fixtures:
   retain selection while the record is still ready, clear it once tombstoned,
   and verify a late completion cannot resurrect it.

All four physical results remain **NOT_RUN / UNPROVEN** in author evidence.


## 2026-09-29 ad-hoc beta distributable acceptance — NOT_RUN

This section supplements historical author/source matrices above. They do not establish PASS for this beta. Start from the exact distributable ZIP and its SHA256SUMS in docs/RELEASE.md, use a fresh/isolation test context, and follow the numbered Chinese guide. Candidate source commit/tree, ZIP SHA-256, bundle version/signature, actual OS/Chrome versions, tester identity and timestamps are required before any PASS.

Author: seethis-beta-guide-worker (implementation evidence only). Independent final QA identity/result: PENDING. Candidate build: 69a0a84db323ab8648eb650d67d685fcbc4307e2; ZIP SHA-256: e79c35235178d68fdc058bd9e33f04a97f143556e164d10c879c09553e28db65. Runtime baseline: 5ff9a8f2ad79e604467c6157563d50c3eae627f6; current guide/source commit recorded in review provenance. Guide author host: macOS 26.5.1 (25F80), capture Chrome 154.0.8037.92. No historical installed app is candidate evidence.

| Check | Numbered guide / expected observation | Status |
| --- | --- | --- |
| Source/build/package | Clean checkout and source archive Release configure/build/CTest; actual arm64/minOS/signature/provenance, ZIP equality and checksum; distinguish mocks | Worker technical checks passed: Release CTest4/4, packaging regression, strict signature/arm64/archive checksum/equality; independent final QA remains separate |
| First download | Browser download actual ZIP, quarantine readback and supported Open Anyway; local ZIP creation does not count | Supporting actual local69a browser download/quarantine observed; public first-download/Open Anyway needs-human / final-version-recheck |
| Install | Checksum OK, unpack, one exact app in isolated Applications context; no replace of user's app | NOT_RUN |
| Input Monitoring | Genuine request/settings separate from Screen Recording; denied/readiness/Retry/reopen exact app | NOT_RUN |
| Screen capture | Actual OS permission label, separate grant/reopen and usable real image | NOT_RUN |
| Multi-region hold | Option+A remains held through two independent mouse drags/up boundaries; gap empty; key-up publishes exactly one URL | NOT_RUN — current author tooling cannot prove true global held-key sequence |
| Clipboard/paste | Immediate text URL, unchanged when image completes; explicit Copy marked image then genuine attachment in destination; same-Mac URL vs remote limitation | NOT_RUN |
| Inspector/history | Hover thickening/recopy, history Copy JSON URL, View details/preview, image buttons, hidden panel remains hidden until Show | NOT_RUN |
| Continuous delete | One Option+D hold, repeated disposable mark clicks, one identity per click/all its regions; release exits | NOT_RUN — physical held-key condition and scoped deletion confirmation required |
| Chrome consent | Ready standalone Chrome foreground; opening and reopening Inspector causes no request. Use explicit Connect Chrome or capture shortcut for a genuine prompt/entry, then record grant, denial, absent prompt, or page unavailable. | NOT_RUN |
| Chrome isolation | Tab A/B hide/restore, same-URL different tabs, two windows/app switch, reload, navigation/Back; embedded browser limitation | NOT_RUN |
| Restart | Quit/reopen exact candidate; effective permissions and retained valid history; copied local port may change | NOT_RUN |
| Update | Isolated N→N+1 exact source/ZIP, expected retained data, possible TCC reauthorization; no continuity promise | NOT_RUN |
| Uninstall | Quit, remove only isolated app, data remains until explicit deletion; backup/clipboard/remote images remain outside revocation | NOT_RUN |
| Privacy/public packet | Full selected display safe, no capability in screenshots or public reports; history/new packet review; license/contact decisions | PENDING |

Record PASS/FAIL/UNAVAILABLE independently per row with observed evidence; source tests, preflight switches, old acceptance, screenshots or mock tools cannot substitute. Do not reset/grant TCC, bypass Gatekeeper/SIP, edit private Chrome state or user references, or synthesize system dialogs. Capture manifest lists all12 filenames:4 actual supporting illustrations and8 needs-human; diagnostics excluded. Supporting original-app permission pages do not establish candidate first authorization. Missing physical observations remain explicit review scope gaps; no all-BETA PASS is asserted by the author.

## MUI-01–04 physical actual-menu matrix

Run these rows against the exact candidate app and bundle path. “Base granted” means both Input Monitoring and Screen Recording are granted; “base denied” means at least one is denied or unverified. Set up Chrome with normal user actions and record the actual preflight/page state; do not force a consent request by opening the menu. “Verified page” requires an existing fresh usable provider/page identity, not Automation consent alone. For each row, inspect the actual menu labels and Show SeeThis details, select the direct Show Inspector item, then select it again. The second selection must leave Inspector visible. Check that Show SeeThis, Inspector opening and menu refresh cause no permission request, and that Quit SeeThis remains present. Record actual label text, Inspector visibility before/after each click, any OS prompt, app path/PID, and a screenshot or accessibility observation before changing the state.

| Base permissions | Chrome preflight/page state | Inspector before click | Expected SeeThis entry | Expected Chrome entry | Physical result |
| --- | --- | --- | --- | --- | --- |
| Granted | Unchecked | Hidden | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Unchecked | Visible | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Not running | Hidden | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Not running | Visible | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Automation unavailable/denied | Hidden | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Automation unavailable/denied | Visible | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Automation granted, page unchecked/unavailable | Hidden | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Automation granted, page unchecked/unavailable | Visible | SeeThis — Ready | Chrome — Not ready | NOT_RUN |
| Granted | Existing verified usable page | Hidden | SeeThis — Ready | Chrome — Ready | NOT_RUN |
| Granted | Existing verified usable page | Visible | SeeThis — Ready | Chrome — Ready | NOT_RUN |
| Denied/unverified | Unchecked | Hidden | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Unchecked | Visible | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Not running | Hidden | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Not running | Visible | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Automation unavailable/denied | Hidden | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Automation unavailable/denied | Visible | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Automation granted, page unchecked/unavailable | Hidden | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Automation granted, page unchecked/unavailable | Visible | SeeThis — Not ready | Chrome — Not ready | NOT_RUN |
| Denied/unverified | Existing verified usable page | Hidden | SeeThis — Not ready | Chrome — Ready | NOT_RUN |
| Denied/unverified | Existing verified usable page | Visible | SeeThis — Not ready | Chrome — Ready | NOT_RUN |

If the candidate's existing state cannot establish a verified usable page for the outward menu, mark those rows **UNAVAILABLE** with that evidence; do not create a page probe, consent request, or new capture gate solely to produce Chrome — Ready. The matrix is a QA checklist, not a claim that these states were observed.

---

# Chrome capture-shortcut consent acceptance

This version adds consent intent to the physical, configured capture shortcut while Chrome is foreground. Existing Connect and Inspector recovery controls remain available. A denied or revoked permission follows normal macOS recovery; a repeated dialog is not promised.

Automated evidence uses local Carbon event objects, real production input/controller state, and injected external permission/foreground/physical snapshots. It does not launch SeeThis, post input, request real consent, inspect TCC, change user settings, or exercise a live dialog. Prior accepted human capture/tab/delete results are not repeated by these tests.

A user reported successful first-authorization acceptance of the development candidate. That report does not establish the full checklist below or live acceptance of the final production build. Automated and physical acceptance remain separate:

1. With Chrome foreground and Automation missing, press the effective capture shortcut (default Option+A). Consent may appear through the normal macOS policy. SeeThis should remain responsive; the interrupted hold should create no partial or phantom reference.
2. Grant or deny while the key remains held. Repeats, modifier release/repress, and returning focus should neither reopen authorization nor resume drawing. Release the original capture key and use a fresh gesture after grant.
3. Repeat with a customized effective binding, including immediate switch into Chrome. Only the exact configured physical chord should initiate authorization for the current Chrome process. A relaunch while the request is queued should abandon the obsolete request.
4. With granted Automation, non-Chrome foreground, a page that is not ready, or a provider timeout, capture should preserve ordinary behavior without an unintended consent request. Startup, Refresh/Retry, and opening Inspector should remain prompt-free.
5. Healthy/granted Chrome keeps recovery description and all three controls hidden and effective shortcut hints visible. Non-Chrome reference context does not display Chrome recovery status.

Automated results do not establish installation, physical system-dialog behavior or release approval.
