# Numbered notes acceptance

The native numbered-note UI is exercised by `inspector_mark_notes_test` with a
fake persistence adapter. It uses the production note panel and text view for
focus, Unicode input, one-panel toggle/switch, save status, retained drafts,
retry, conflict, and uncertainty. It also delivers synthetic AppKit mouse
events to the production `STOverlayView` and checks badge → stroke → draw
ordering and coordinate conversion. Its routing target is fake: this test does
not execute `STOverlayCoordinator::handleNoteBadgeClick` or read a physical
delete chord. Badge hit geometry is checked at 1x/2x. Core store and HTTP tests
cover stable IDs, persistence, delivery geometry, and the API contract.

Production coordinator delete-chord behavior remains **UNKNOWN** from this test
and needs independent source review plus the physical check below. Physical UI
checks remain **NOT_RUN** until observed on a Mac with the exact
composed candidate. Record the candidate commit, macOS display scale, source
application, and result for each check:

1. Draw a reference with two outer gestures, including one gesture with a
   display-transition subpath. Each outer gesture has exactly one button,
   numbered in original drawing order at its first stroke point.
2. Click a numbered button. One note panel opens with the caret in the native
   text view. Type Unicode and multiline text; the label moves from pending to
   saved only after persistence succeeds. Click the same button to close it,
   then another button to switch that same panel to its note.
3. Force a note persistence failure. The label reports failure and the typed
   text remains visible. Switch away and back; the draft is still present.
   Retry after removing the fault and confirm saved text survives app restart.
4. Click an ordinary stroke. Existing mark selection/copy still works without
   starting a new drawing. Click empty overlay space while drawing is enabled;
   a new region begins. Click outside a number or stroke while drawing is
   disabled; the underlying app receives the click.
5. Hold the configured physical delete chord and click a number, then an
   ordinary stroke. Each gesture deletes the reference according to the
   existing delete flow and never opens a note editor or begins drawing.
6. Exercise a pending/indexing reference, delete/expiry during an unsaved edit,
   and a display reconfiguration. Badge hit policy and the editor's retained
   draft remain truthful; no stale badge keeps the overlay intercepting clicks.

This document is an acceptance script, not a claim that physical checks ran.
