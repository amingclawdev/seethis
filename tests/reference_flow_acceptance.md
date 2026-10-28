# Immutable macOS reference acceptance

Implementation rows: `SEETHIS-MAC-REFERENCES-20260913` and
`SEETHIS-MAC-ASYNC-REFERENCE-JOBS-20260915`.
This document describes author evidence and the independent acceptance still needed.
An automated PASS is never a live macOS focus/permission/capture PASS.

## Single full marked image delivery schema 3 (SMI-01–SMI-07)

This successor changes the default bare `GET /r/<handle>` response. The historical
MKC acceptance and measurements below remain historical; explicit
`GET /r/<handle>/metadata` preserves the current delivery-schema-2 compact
source/crop/position/note projection used by browser preview. Persisted
reference schemas 1 and 2, notes sidecars, copy/Inspector behavior,
`/r/<handle>/detail`, all existing assets and Bearer `/v1/references/<id>`
metadata remain compatible. No stored-data migration occurs.

A ready default response has `delivery_schema:3`, persisted `reference_schema`,
`reference_id`, response `state`, `lifecycle.state` and `expires_utc_us`,
`notes_revision`, unchanged version-1 capture-time `source_context`, and an
ordered `marks` array containing exactly each authoritative `mark_id`, `number`
and exact `note`. Empty, Unicode and multiline notes are preserved. Numbers
come from the store snapshot, never the output-array index. The source context
keeps its availability and observation/completion timestamps and is never
replaced by current foreground data.

The sole `image` descriptor has representation `full-annotated-v1`, the same
reference identity, the existing scoped `/annotated.png` URL, image state,
actual PNG-byte `sha256`, `source_sha256`, full width/height and failure or
pending `code`. The PNG contains the full captured background, all included
marks and their visible numbers. HTTP 200/ready requires canonical annotated
bytes and a nonempty digest. Cold/queued/deriving image state is HTTP 202,
response/image `pending`, null digest, `retry_ms:25`, while the persisted
reference lifecycle remains ready. Failed or unavailable canonical images
return 422 with truthful state/code and null digest; no unmarked fallback is
allowed. Whole-reference pending/indexing retain 202, failure retains 422,
and deleted/expired retain 410. Existing PNG SHA/ETag headers bind delivered
bytes; no-store/no-referrer and capability/Host/Origin restrictions apply.

Default JSON contains no raw source-image URL, layers, crop/per-mark image
URLs, geometry, anchors, bounds or trajectories. Its `details_url` points to
the bounded `/r/<handle>/details` manifest (`details_schema:1`, persisted
`reference_schema`, reference identity). Named descriptors discover explicit
legacy `/metadata`, historical `/detail`, existing Bearer metadata with
`auth:"reference_bearer"`, and the existing viewer URL. Discovery grants no
new authority: the Bearer endpoint still rejects a request without its scoped
capability. No query form or general endpoint-discovery API is introduced.

The automated service fixture persists three actual gestures (line, circle,
line), one empty note, Unicode/multiline/quoted text and a tab. It decodes the
HTTP JSON before fetching only the supplied canonical image URL on its steady
ready path. It decodes that PNG, checks full dimensions and background/source
binding, both lines and the circle, and reads all three number glyphs against
the authoritative JSON. The request counter verifies exactly one JSON GET plus
one image GET. Cold setup/retries and optional discovery are separate. It also
checks note revision conflicts, all source-context availability variants,
actual navigation freeze, persisted schema 1, actual corrupt/missing source
failures, ready cached-canonical delivery while optional raw source is missing,
legacy/browser/copy regressions, deletion/expiry and scoped security.

The synthetic 640 × 400 fixture measured 1,479 schema-3 JSON bytes and 63,145 PNG
bytes: 64,624 payload bytes and 65,162 HTTP response-transfer bytes for those two
GETs. The same fixture's explicit schema-2 JSON measured 2,331 bytes, with its
three mark images pending; this is an 852-byte JSON reduction for this fixture.
The earlier observed 2,367-byte schema-2 example is a **historical observation**
with a different capture, dimensions and notes, not a matched benchmark.
No tokenizer measurement or token saving is claimed. Exact dynamic metrics
are emitted by `reference_server_test --single-image-only`.

On this source lineage, `MarkSnapshots` visits every effective region slot,
assigns `StableMarkId(id,slot)` and authoritative number `slot+1`;
`DrawNumberBadge` draws `slot+1`. Note/visibility updates do not remove or
renumber stored regions, and deletion removes the entire reference. Thus the
supported delivery numbering domain is 1..N. The noncontiguous-number subcase
is **NOT_APPLICABLE / NOT_RUN**, never a fabricated fixture PASS. Independent
QA must recheck those immutable symbols on the exact candidate commit.
Packaged-app physical UI/capture checks remain release-owner work; these
synthetic checks do not certify native permissions or final app delivery.

## Compact per-gesture Mark delivery (MKC-01–MKC-07)

The capability URL `/r/<handle>` returns delivery schema 1. Its `marks` are
ordered by stored outer gesture, with deterministic IDs and numbers. Each item
has an empty note and `visible=true` projection default, source-screenshot
pixel `pos`, image dimensions, an annotated crop URL, and a truthful
pending/ready/failed image state and byte digest. The reference-level
`screenshot` gives the immutable source URL, digest and dimensions once. The
default JSON contains no raw path, subpath, sample or image bytes. A scoped
`/r/<handle>/detail` request and the existing Bearer `/v1/references/<id>`
reader retain the historical rich metadata and trajectories.

The crop URL `/r/<handle>/marks/<mark-id>.png` requires the same reference
capability. It composites only that outer gesture's ink onto the source pixels
within its own stored `crop_pixels`; clipped display subpaths do not create
extra marks. The crop's top-left is `pos` in the referenced screenshot, and
its PNG dimensions match the item metadata. A pending derivation returns 202
with retry guidance; a ready 200 includes a digest over the delivered bytes.
Deleted and expired references retain their existing 410 lifecycle response.

Synthetic store/service checks cover two independent gestures, source and ink
pixels in each crop, bounded dimensions, invalid mark IDs, explicit detail,
capability isolation, and pending-to-ready digest truthfulness. The service
test measured 774 default metadata bytes for both 2 and 177 samples, while
explicit detail grew from 3,699 to 32,577 metadata bytes. A two-gesture
fixture measured 1,129 default versus 3,721 detailed metadata bytes. These
measurements exclude PNG transfer; no model/tokenizer count or token saving
is claimed.
Physical 1×/2× capture alignment and packaged-app browser rendering are
**NOT_RUN** until exercised on the integrated candidate.

## Agent JSON reference URL AJ1/AJ5 — candidate acceptance

For `SEETHIS-AGENT-JSON-REFERENCE-URL-20260925`, deliberate publication and every
mark re-copy must place exactly one plain-text URL below 2 KiB on the pasteboard:
`http://127.0.0.1:<port>/r/<id32><cap64>`. The 96-character lowercase-hex handle
contains the stable reference ID and its scoped capability. There is no query,
fragment, title, JSON text, image, file promise or other pasteboard type. Pending
completion must leave the pasteboard change count and bytes alone. A same-Mac
browser opening that URL receives JSON and leaves the full URL visible in its
address bar; the response and asset routes must carry no-store/no-referrer policy.

The JSON response must expose only that reference's scoped asset URLs under
`/r/<same-handle>/source.png`, `/crop.png`, `/annotated.png` and, for schema 1,
`/record`. The Inspector's **View details** action must still open the existing
`/v1/references/<id>/view#cap=<cap64>` HTML viewer without a clipboard write.
Existing Bearer-authenticated `/v1/references/<id>/...` reads and the annotated
full-context PNG lifecycle must remain usable. Automated service and interaction
checks are candidate evidence; native pasteboard types, browser address bar,
focus and physical mark actions remain **NOT_RUN** until independent QA.

## Agent annotated full-context PNG AAP1–AAP5 — author boundary

Disposable source tests decode the derived PNG and compare individual RGBA
pixels with the immutable source. Both separate 1x regions receive freehand
ink, while the gap and other source context retain their exact pixels. A 2x
negative-origin fixture preserves two independently drawn subpaths without a
bridging chord. A cold request is pending on the bounded worker, and warm and
restarted requests return identical PNG bytes and SHA-256. Deletion removes the
owned annotated PNG and bound digest alongside the source and metadata. A valid
same-dimension replacement PNG and a missing digest each force regeneration
instead of silent cold-cache acceptance. Loopback tests check
the scoped Bearer token, pending 202, metadata URL/pending-null then ready
digest, exact response-byte ETag/hash, stable repeat, and deleted 410. Existing
source/crop/viewer/URL behavior is covered by the same regression suites.
These are synthetic source/service checks; real macOS capture orientation,
agent fetch, and browser experience remain **NOT_RUN**.

## Cross-reference marked HTTP retest — 2026-10-01

Owner: existing `SEETHIS-ANNOTATED-PNG-DELIVERY-ACCEPTANCE-20260926`.
The diagnostic source is exact `62ec70204a64ee026ac3836ca056c105f641888c`.
Its frozen author report has SHA-256
`59ccd924fbd514ae0c66ea1cb89323f4c946bbf1bb9066e7c71125ac6beaf7e6`.
It ran the actual store and loopback TCP service with deterministic synthetic
100×100 RGBA references and freehand paths; it did not launch a user app, capture
personal content or contact the historical capability.

| Diagnostic observation | Actual baseline result | Limit |
| --- | --- | --- |
| Warm A alone; four concurrent warmed A reads | 200 with matching body SHA-256, ETag and `X-SeeThis-PNG-SHA256` | Arbitrary cold/concurrent schedules were not fully characterized. |
| Four rounds of B metadata → store idle → A annotated → store idle | A returned 202 each round despite an existing, unchanged A marked PNG | B metadata initiates marked derivation and displaces the single cached result; this is a controlled schedule. |
| Reopen store/server and repeat | Same repeated cross-reference 202; stopping B interference restored A 200 with the same bytes/digest | A successful settled reload is not a cross-reference progress guarantee. |
| Source/crop, missing bearer, wrong-reference capability, deleted A | Settled source/crop 200 with matching hashes; 401, 403, then deleted 410 and owned marked-file absence | Synthetic checks do not prove real capture geometry or field delivery. |

Historical field root cause remains **UNKNOWN**. Imported request chronology is
reconstructed, original headers and complete interleaved traffic are absent, and
marked/source/crop PNG response bodies were not retained. A disk digest matching
initial metadata does not prove HTTP delivery. Sampled waiting workers do not
identify a cause. The diagnostic reproduces a conditional failure mechanism;
it does not prove those B requests happened on the acceptance Mac or rule out
other causes. Candidate implementation and independent QA results must be added
only after their real evidence is available.

For the exact next candidate, use two disposable references A and B with known
pixels, scoped credentials and isolated storage. Record source commit/tree,
binary/build identity, fixture version, retention settings, request timestamps
and the observation/retry deadline. Do not log complete capability URLs, bearer
tokens, private metadata or unrelated clipboard/screen content.

1. Fetch A metadata and annotated PNG to a settled 200. Save status, content type,
   retry fields, `ETag`, `X-SeeThis-PNG-SHA256`, body byte count and body SHA-256.
   Compare ready metadata digest with the actual body and retain the synthetic PNG.
2. Repeat warmed A reads singly and concurrently. Then reproduce the four-round
   B-metadata/idle/A-annotated schedule above, logging both requests each round.
   Also exercise overlapping A/B metadata and image retries with a finite,
   recorded observation window. An initial cold 202 can be legitimate; repeated
   starvation of already-settled A must be reported separately from generation
   failure. No unbounded retries or unexplained timeout may be reported as PASS.
3. Stop B requests and measure A's response transition. Reopen the store/server
   with the same persisted synthetic assets and repeat both control and
   cross-reference schedules. Compare served bytes to persisted marked bytes
   and metadata digest; distinguish cache reuse from a new render with bounded
   test counters where the fixture exposes them.
4. Repeat source and crop reads and validate their actual PNG bodies/header
   digests. Check missing/wrong credentials and deleted 410. Exercise the declared
   retention/expiration boundaries separately; two recent references under
   default retention do not establish high-volume eviction behavior.
5. Record each result as PASS, FAIL, NOT_RUN or UNAVAILABLE against the exact
   candidate and actual command. Keep baseline diagnostic, source-author test,
   independent technical QA and installed-app/cross-Mac acceptance separate.

This procedure has not been run by the documentation worker. Installed-app and
cross-Mac retest remain **NOT_RUN**. Original AAP6 still requires the exact clean
`0e2e7afff7d5859472ea56cff875e0a2e76e02f2` candidate in its isolated consumer
before native overall delivery PASS or close. This follow-up does not rewrite
that dependency or any historical native **NO-PASS**.

## Quiet reference inspector QI1–QI5

The shared state-machine regression verifies that only ready marks claim pointer
input and that the macOS source contains no persistent `Loading`, `Indexing`,
spinner or lower-left status presentation. It also verifies the exact brief copy
acknowledgement, metadata-only inspector listing and explicit list recopy path.
The physical inspector, focus, drag, click-through and pasteboard checks remain
**NOT_RUN**.

## Automated author checks

Run the checked-in `macos-clang` configure/build/test presets (Ninja, Apple clang,
actual macOS SDK); CTest runs `interaction_state_machine` and `reference_flow`.
The reference test contains 115 internal checks on macOS and uses owned fixture context/pixels, temporary on-disk stores and
an injected clipboard callback. It does not simulate proof of native input dispatch.
Node.js 18 or newer is a test-only prerequisite for its embedded viewer canvas
harness; the test fails loudly when node is unavailable, and the product has no
Node.js runtime dependency.

| Case | Automated evidence | Live boundary |
|---|---|---|
| Context changes after capture starts | Bound original PID/title/window geometry stays unchanged | Actual frontmost attribution NOT_RUN |
| Release before pixels/context callback | Deferred same-generation commit; no drawing while pending | Physical key-up/pass-through timing NOT_RUN |
| Release and cancellation ordering | A-first, Option-first, near-simultaneous and physical-up-before-queued-release reconciliation complete once; the bounded reducer drain preserves valid regions, while true cancel reasons, late/duplicate callbacks, failed/empty captures and timeout produce no fabricated saved/copied mark | Actual release order, Escape/watchdog/app/display/input-monitor delivery NOT_RUN |
| Durable identity | Restart reloads multiple records; initial and re-copy URLs retain the same ID/capability while durable assets remain byte exact | Real pasteboard/application paste NOT_RUN |
| Append-only publication | Same ID/bytes idempotent; different valid bytes conflict; corrupt published record rejected; unfinished staging ignored | Power-cut/filesystem fault injection NOT_RUN |
| Canonical data | Binary round trip and trailing-byte rejection; standard SHA-256 vectors; canonical envelope validation | External consuming agent is deferred |
| Image validation | Invalid PNG rejected even after rehash; unrelated crop pixels rejected; native ImageIO decodes generated PNG | Captured source pixels/vertical alignment NOT_RUN |
| Display transforms | Negative origins, mixed scale, spanning/cross-display crop/margin/clamping, stale-scale rejection | Physical heterogeneous monitors NOT_RUN |
| Selected marks and inspector | Multiple/exact/newest-overlap hit selection; immutable prior bytes; metadata-only selected state; hide on UUID/ID/geometry changes | Native panel placement and windows moving/changing NOT_RUN |
| Clipboard partial failure | Save remains durable, explicit partial result, retry copies the same URL only | System pasteboard failure/recovery NOT_RUN |
| Invalid complete records | Owner mismatch, nonfinite geometry, invalid phase times, out-of-source crop, changed clipboard bytes rejected | Native context/permission availability NOT_RUN |
| Async ordering | A delayed first worker permits the second job to complete; both persist and neither completion writes the clipboard | Native rapid-circle timing NOT_RUN |
| Bounded lifecycle | Four unfinished jobs accepted, fifth receives `busy`; fake-clock timeout, save failure, graceful shutdown and restart interruption are terminal | Power-loss timing NOT_RUN |
| Clipboard ownership | Pending URL publication is deliberate; ready/failed completion has a zero-write plan and cannot change the pasteboard count | Real pasteboard type/count inspection NOT_RUN |

Author native host: macOS 26.5.1 (25F80), arm64, Apple clang 21.0.0.21000101.
No permission settings or services were changed. No product UI interaction was run.
The signed `.app`, compiler database, full commands/status, sanitizer limitations,
and hashes are preserved in the author candidate evidence, outside the repository.

AddressSanitizer and UndefinedBehaviorSanitizer exercise the deterministic core.
LeakSanitizer is unsupported on this host; it is not claimed as passing. Strict
`-Wall -Wextra -Werror -fsyntax-only` checks cover every generated compiler command,
including AppKit/ScreenCaptureKit adapters. Native compilation and ImageIO decoding
do not establish capture correctness or input behavior.

The committed macOS reference test compiles the actual capture adapter with a
test-only owned-value bridge. Asymmetric X/Y color values and a premultiplied-alpha
corner pass through native normalization and the reference transaction; the test
asserts exact crop coordinates and PNG pixel bytes. A native Y inversion makes this
regression fail. A supplementary preserved harness also checks null-image rejection.
These pass after correcting a vertical-flip error found by a native probe. They
verify synthetic CGImage normalization, not real captured alignment.

The capture input adapter now reserves the configured chord with an exclusive
Carbon registration and uses the listen-only tap for modifier, pointer and
interruption observations. A physical-up watchdog sample records its process
session time and retains the pre-cancel region count while queued release events
drain; physical focus, text insertion and pasteboard behavior remain NOT_RUN.

## Independent live macOS matrix — all NOT_RUN by author

Record exact commit/app hash, macOS build, hardware, each display UUID/frame/scale,
permission state, app version, window IDs and starting pointer for each case. Keep
actual before/after pasteboard bytes and checksums, rather than only a screenshot.

1. **R1 original context:** with two overlapping normal windows of one app and an
   unrelated foreground app, begin on a known source. Verify original process,
   bundle/executable/window ID/title, frame and documented selection method. Start
   outside every eligible foreground window and verify explicit failure.
2. **R1/R4 exact pixels:** place known asymmetric colored content in the selected
   window and an existing SeeThis historical mark over it. Verify source pixels
   exclude SeeThis, other windows, cursor, child windows and shadows; compare source
   dimensions and crop orientation/alignment against frozen transforms. Record
   observation/request/completion/circle times and show asynchronous semantics.
3. **R2 persistence/re-copy:** complete two distinct marks, record the exact URL-only
   pasteboard representation and separately hash authorized local assets,
   move/change/close original windows, and click each mark. Compare identity before
   and after; restart the app and repeat. Test overlapping
   marks (newest hit wins), outside-hit pass-through and keyboard focus retention.
4. **R3 inspector policy:** completed marks retain only their circles and lightweight
   hover feedback; no permanent time or Historical label is drawn. Selecting one
   opens metadata-only details with source/window, interaction versus capture times,
   location, dimensions, display context and lifecycle. Window changes leave marks fixed.
   Disconnect/reconnect,
   rearrange or rescale displays: unsafe topology changes hide marks, never move
   them onto a different target. Restoring the exact recorded topology may show
   them again.
5. **R4 permissions/failure:** in a separately authorized manual session, exercise
   denied/revoked Screen Recording, unavailable target context, closing the target
   during enumeration/capture, capture error and timeout. No reference is reported
   successful, clipboard is not replaced by a fabricated reference, and input is
   immediately usable. The product must not alter permissions itself.
6. **R4 asynchronous lifecycle:** rapid key release before pixels returns input
   immediately and later commits only the same accepted generation. Exercise
   A-before-Option, Option-before-A and near-simultaneous release; each valid drawing
   must complete exactly once. Delay the queued A-up until after the watchdog observes
   the physical release and verify the same one completion. Exercise Escape, app
   switch, focus loss, display change, input-monitor interruption and maximum-hold
   timeout before release; these cancel drawing. After valid release, exercise those events and a new shortcut
   while the old capture is pending; the accepted frozen job must continue. Verify
   timeout/failure never resurrects a ready mark and out-of-order completion never
   overwrites a later circle or user pasteboard copy.
7. **R5 displays:** primary/secondary at negative X and negative Y, mixed 1x/2x,
   a source window spanning displays, a path crossing displays, and a crop partly
   outside the source window. Check real pixel alignment, pointer boundaries and
   panel placement. Hardware-dependent cases unavailable to QA remain NOT_RUN.
8. **Partial clipboard result:** induce an actual URL clipboard write failure without
   deleting the persisted record; verify explicit saved-but-not-copied status and
   byte-identical URL retry by historical mark click.

Existing overlay O2/O3 are still NOT_RUN/UNPROVEN. Process survival, screenshots,
static inspection, unit tests and a signed bundle cannot close these cases.
Application-specific semantic object IDs and agent service endpoints are deferred.

## MG/RL collection flow — author boundary

Deterministic tests establish that two paths serialize as schema 2 with separate
logical/image bounds, one source image, one aggregate crop and one identity; one
logical region split by a leave/re-entry interval serializes as explicit contiguous
subpaths without a connecting chord; sidecar
metadata, loopback JSON, inspector, rendering, hit testing, recopy and deletion retain
that collection. They also establish legacy schema-1 fallback and bounded fresh-probe
readiness transitions. They do not establish real selected-display pixels, overlay
focus/consumption, TCC recovery, physical keyboard release order, system pasteboard,
browser decode, or mixed-display alignment. Independent QA must compare the single
source PNG with the selected display and verify both regions, the empty gap, the URL,
restart/history, and whole-reference deletion. FC and PL remain open and must not be
reported PASS from this row.

## PL deterministic and physical boundary

The committed store test records five warmups and twenty ordered schema-2 pipeline
attempts with status, release-to-URL, queue, encode, validation, metadata
serialization, source persistence/readback, release-to-ready and exact artifact byte
counts. It must show no schema-2 `.stref`, crop file, whole-record pack or whole-record
readback. Restart must recover the explicit codec and RGBA crop-integrity contract
from the metadata sidecar and primary PNG, derive an equivalent crop without creating
a file, and retain schema-1 and frozen version-3 byte compatibility. Every returned
PNG body must match its response SHA-256 and ETag even when equivalent crop encodings
have different bytes. The service test separately covers immediate authenticated
schema-2 source display with selected-region geometry, explicit on-demand crop states,
schema-2 legacy-export refusal and the unchanged large historical record cohort.
The viewer contract also has executable endpoint coverage for PNG signature, content
type, response-byte SHA-256/ETag, independent two-region geometry/path metadata,
pending/indexing and queued/deriving states, scoped capability failures, expiry and
deletion and a wrong-crop fixture that has self-consistent response headers but fails
the persisted schema-2 RGBA pixel digest. The browser harness boundary covers the
two-second fetch/body deadline and session-only capability reload path. Its canvas
recording harness executes the embedded viewer JavaScript against public synthetic
metadata and asserts that full-context preview/export draws one stroke per original
freehand subpath, preserves gaps between paths and draws zero automatic crop
rectangles while retaining crop-pixel validation. Real DOM/canvas decode, reload and
crop-download interaction remain **NOT_RUN** unless observed on the packaged candidate.

The representative codec fixture is 3456×2234 at scale 2 with two regions and reports
compressed size, encode/validate/readback time and peak RSS. Malformed tests include
truncation, oversized inflate, CRC, Adler, SHA, invalid filter and trailing-stream
failures. An unversioned legacy record containing the prior extra-empty-block stream
must remain rejected through deserialize, validate and import; it cannot fall through
to the broader SDK-zlib decoder.

These deterministic runs do not measure ScreenCaptureKit callback time, raw native
conversion, system pasteboard, real browser scheduling/decode/paint, display capture
memory or physical release-to-visible-image. Record those as NOT_RUN unless observed
on the separately installed candidate with its exact display dimensions, scale, build
and permission state.

## Chrome page-scoped source milestone

The deterministic source boundary covers Chrome page identity persistence and
redaction, same-URL different-tab mismatch, same-tab same-URL reload retention,
navigation and back restoration, browser process-start identity changes, ambiguous window correlation, unavailable or
denied provider results, bounded timeout and stale completion suppression. The
same visibility predicate is exercised for paint, hover, hit, copy and delete;
Inspector history remains readable while a hidden mark is ineligible for those
actions. Legacy records without page identity retain the existing app/window
behavior.

The manual permission path is documented and prompt-free in automation: the
user grants SeeThis -> Google Chrome under System Settings > Privacy & Security
> Automation after a denied preflight; the existing Show Inspector action is
the explicit user-triggered request path. Real Chrome Apple Events,
physical tab/navigation switching, TCC continuity and Codex task/page identity
remain NOT_RUN/incomplete.
