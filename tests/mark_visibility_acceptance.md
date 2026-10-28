# Mark visibility acceptance

This is the verification script for the original SeeThis Inspector visibility
criteria **MKV-01..07**. It describes expected observations and evidence to
collect; wording here does not establish a product PASS. Run it against the
exact composed candidate and record its commit, test command/output, app build,
macOS version, display scale, source app, window, and page. Keep component
results separate from physical foreground results. An unobserved check is
**NOT_RUN/UNKNOWN**, never FAIL merely because it was not run or PASS because a
scenario is written here. The prior numbered-notes row has no claimed native
QA PASS in this document.

## Evidence map

| Original criterion | Component or integration proof to collect | Physical foreground proof to collect |
| --- | --- | --- |
| MKV-01 | Selection state drives the correct Hide or Show action for the exact selected mark. | Inspector label/action changes as selection moves between visible and hidden marks. |
| MKV-02 | Hidden marks are absent from overlay drawing, badge, editor, and hit eligibility; another mark stays visible; saved note and unsaved draft survive the presentation change. | Inspect ink/badge/editor and pointer behavior for mixed and all-hidden overlays. |
| MKV-03 | Hidden mark remains in the Inspector snapshot; Show changes only user visibility, leaving originating app/window/page gates intact. | Select hidden mark in Inspector, Show in its source context, then switch app, window, and page. |
| MKV-04 | Before/after snapshots retain reference ID, mark ID, drawing-order number, note, original trajectory, screenshot reference, and delivered image identity. | Confirm number and note after Hide/Show and restart. |
| MKV-05 | Repeated selection and Hide/Show calls are per-mark and revision-safe. | Alternate selection among marks and verify each control/action. |
| MKV-06 | Line, circle, and another supported shape retain ordered compact delivery and numbered-note behavior; default response has no raw coordinates. | Exercise each shape's ink, badge, click area, and note editor. |
| MKV-07 | Both Hide and Show persist for the same surviving mark; deletion/expiry/tombstone and failed writes cannot resurrect or falsely confirm it. | Quit and relaunch after each state; inspect hidden selection and restored presentation. |

The intended automated seams are the store tests, server/compact-delivery
tests, and a native Inspector component test using the production view or
coordinator seam. Record the exact test names and output when implemented and
run. Those tests can prove data, revision, and simulated event behavior; they
cannot by themselves prove physical foreground focus, pointer passthrough,
display geometry, or the configured delete chord.

## Scenario setup and expected observations

Create one ready reference in an originating app/window/page with at least
three **outer gestures**: a line, a circle, and a third supported shape. Give
them distinct saved notes and record ordered mark IDs, numbers, original
trajectory data, screenshot URL/hash, compact image URLs/hashes, and the
reference ID. If a gesture crosses displays or has clipped subpaths, still
expect one mark ID and number for that outer gesture. Add a second reference
in a different page or window to exercise the existing context gates. Capture
the compact JSON before each visibility change as the comparison baseline.

1. **Selected control and isolation — MKV-01, MKV-02, MKV-05.** Select the line
   in Inspector. Its action reads Hide. Hide it: only its `visible` value
   becomes `false`; its overlay ink and numbered button disappear. Select the
   circle, whose action remains Hide and whose ink/badge remain. Switch back to
   the hidden line: its action reads Show. Repeat Hide/Show and alternate
   selection; neither another mark nor another reference changes state.
2. **Hit and editor suppression — MKV-02, MKV-05, MKV-06.** Open the line's note
   editor, type an unsaved draft, then Hide the line. The editor closes or hides
   without clearing that draft or its previously saved note. The hidden line's
   stroke and former number location are no longer mark hit regions. With one
   visible mark, only that mark intercepts its own hit areas. Hide every mark:
   there is no ink, badge, editor, or hidden-mark hit region, and ordinary
   pointer input passes through the page overlay under the existing drawing
   mode policy. Re-show the line and reopen its editor; verify the saved note
   and retained draft separately. Repeat with circle and third shape.
3. **Inspector recovery and context gates — MKV-03, MKV-05.** While hidden,
   each mark remains listed and selectable in Inspector, including when all
   marks are hidden. Show a selected mark in its source app/window/page and
   verify it becomes eligible there. Move to an unrelated app, a different
   window, and a different page in turn: Show must not override those existing
   gates. Return to the originating context and verify visibility resumes.
4. **Identity and delivery — MKV-04, MKV-06.** Compare before/after ordered
   compact JSON. Mark count, order, IDs, drawing-order numbers, note text,
   anchors, `pos`, screenshot reference, and each annotated crop's URL/image
   identity remain stable after visibility-only changes. The hidden mark stays
   delivered in its original position with `visible:false`; the crop remains
   retrievable and represents the same captured intent. Default delivery has
   no expanded raw trajectory coordinates. Read local/on-demand source data
   where supported to confirm the original path is intact. Show restores
   `visible:true` without changing the same fields. No visibility action
   rewrites the immutable source screenshot or original capture.
5. **Notes composition in both directions — MKV-02, MKV-04, MKV-06, MKV-07.**
   Hide a mark with a saved note and confirm the note survives Hide, restart,
   and Show. While the mark is shown, save a new note through its supported
   editor path, then Hide it and restart. Confirm the new note and hidden state
   survive together. Show and restart again; the shown state and new note
   survive. Exercise a stale note-save revision against a hidden mark through
   the supported store or service seam; the conflict must not reset hidden
   visibility or overwrite the newer note. A stale visibility revision
   likewise must not silently overwrite a newer note. Keep any unsaved editor
   draft distinct from durable saved note evidence; Hide must retain the draft
   in the UI session, while only a confirmed save can establish restart
   persistence.
6. **Persistence and failure truthfulness — MKV-04, MKV-05, MKV-07.** Persist
   Hide, fully quit, relaunch, and select the same surviving mark in Inspector:
   it remains hidden, keeps its ID/number/note, and offers Show. Persist Show,
   quit/relaunch again, and verify it remains shown under the source-context
   gates. Exercise stale expected revision, malformed sidecar, unsupported
   sidecar version, and a failed or uncertain sidecar commit (including an
   fsync uncertainty injection where available). Conflicts and malformed or
   unsupported data must surface a truthful error/state; no path may report
   durable success from an uncertain commit. Reload from disk to distinguish
   an in-memory presentation change from a confirmed persisted change.
7. **Deletion and expiry composition — MKV-04, MKV-07.** Delete a hidden
   reference, run cleanup/retention, then attempt a delayed visibility or note
   update with the old ID/revision. It must not recreate sidecar, mark, image,
   or reference. Repeat after expiry and with a tombstone. Inspector and
   compact delivery must respect the existing deleted/expired lifecycle;
   Hide/Show does not turn those states back into ready references. Run the
   existing Delete All flow on a mixed visible/hidden set and verify cleanup
   applies to both. Do not use the physical delete chord as a proxy for this
   component check; observe that chord separately.

## Result ledger for the composed candidate

Fill this ledger with actual evidence from the exact candidate. `PASS` requires
the stated observation; use `FAIL` only for an observed mismatch and include
reproduction details. A file, test name, or scenario alone is not a result.

| Check | Result now | Evidence to attach |
| --- | --- | --- |
| Store revision, sidecar, restart, failure, and lifecycle cases | NOT_RUN/UNKNOWN | Candidate commit; command and output; fixture/fault details. |
| Compact JSON, annotated crop identity, and Notes/visibility composition | NOT_RUN/UNKNOWN | Candidate commit; response diffs and image/hash comparison. |
| Native Inspector component and simulated overlay events | NOT_RUN/UNKNOWN | Candidate commit; test command/output and seam limitations. |
| Physical Inspector label, selection, editor focus, and draft retention | NOT_RUN/UNKNOWN | App build, screen recording or observations, source context. |
| Physical ink/badge, pointer passthrough, display geometry, and delete chord | NOT_RUN/UNKNOWN | App build, display scale/layout, drawing mode, observations. |
| Physical Hide restart and Show restart | NOT_RUN/UNKNOWN | App build, clean quit/relaunch steps, identity and state comparison. |

The Notes, Compact, and Delete All interactions above are cross-feature
regression scenarios for MKV-02/04/06/07, not additional acceptance criterion
IDs or a claim that those separate rows passed their own acceptance.
