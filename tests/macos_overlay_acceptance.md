# macOS overlay acceptance record

This checklist separates deterministic author checks from independent real-macOS
UI acceptance. A static screenshot is not evidence for focus, pointer routing, or
recovery behavior.

## Author-run facts

The author records commands, exit codes, host details, and artifact paths here only
after executing them. The author does not sign off independent UI acceptance.

| Check | Result | Evidence |
|---|---|---|
| Shared C++ state-machine test | PASS (author, 2026-09-14) | Apple clang 21 with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`; normal and Address/UndefinedBehavior-sanitized runs both reported `interaction_test: all checks passed` (exit 0). |
| Deleted Inspector projection | PASS (author, 2026-09-16) | `interaction_test` covers pending and ready deletion, non-selected row removal, cleanup failure, repeated deletion, startup-recovered tombstones and late capture; deleted tombstones remain in `ReferenceStore` truth while `inspector_jobs()` omits them. |
| Objective-C++ AppKit compile and link | PASS (author, 2026-09-14) | All C++/Objective-C++ units compiled with Apple clang 21 and the macOS 26.5 SDK; the arm64 AppKit executable linked, was placed in an ad-hoc-signed app bundle, and passed `codesign --verify --deep --strict` (exit 0). |
| Native launch smoke | PASS (author, 2026-09-14) | The app process remained alive for a two-second smoke interval, produced no error output, and was then terminated by the author. This makes no UI behavior claim. |
| Real foreground-focus interaction | NOT_RUN | Independent QA must exercise this below. |
| Real empty-region pointer pass-through | NOT_RUN | Independent QA must exercise this below. |
| Real mark-hit pointer consumption | NOT_RUN | Independent QA must exercise this below. |
| Real cancellation/recovery paths | NOT_RUN | Independent QA must exercise this below. |
| Real multi-display/scale behavior | NOT_RUN | Independent QA must exercise this below. |

The author build ran on macOS 26.5.1 (25F80), arm64. Its temporary evidence
directory was `/tmp/seethis-author-final.YLQsFu`; temporary paths are not durable
release artifacts. The source commit is recorded at handoff after the one author
commit is created.

## Linked secondary-origin repair

Independent QA recorded the original candidate
`dcb3ff0d4a899fca77d5a3fb1044145db184c9ed` as **FAIL**: the AppKit panel
initializer received a global `screen.frame` together with `screen:screen`, causing
the selected secondary screen origin to be applied twice. Original clauses O2 and
O3 remain **NOT_RUN** because the required host permissions were unavailable; this
repair does not revise those results or claim full original-row acceptance.

`SEETHIS-MAC-OVERLAY-SECONDARY-ORIGIN-20260914` changes the panel content rectangle
to screen-local `{0, 0, width, height}` coordinates. Author evidence for the linked
repair is recorded below after execution.

| Linked repair check | Result | Evidence |
|---|---|---|
| Declared CMake configure/build/test flow | PASS (author, 2026-09-14) | Supplied CMake 4.1.3 and Ninja 1.13.2 completed `cmake --preset macos-clang`, `cmake --build --preset macos-clang`, and `ctest --preset macos-clang`; 1/1 test passed (each command exit 0). |
| Repaired app two-display window geometry | PASS (author, 2026-09-14) | The signed repaired app created two on-screen layer-25 windows. Its secondary Quartz window bounds were `{-2560,-163,2560,1440}`; converting from Quartz top-left coordinates using the primary height of 1117 gives the selected screen’s AppKit frame `{-2560,-160,2560,1440}`. The incorrect doubled x-origin `-5120` was absent (metadata probe exit 0). A separate hidden-panel constructor probe reported exact AppKit expected/actual frames on both scale-2 and scale-1 displays (exit 0). |
| Signed repair app artifact | PASS (author, 2026-09-14) | The external CMake artifact at `/tmp/seethis-secondary-origin-20260914/seethis-build/macos-clang/seethis.app` passed ad-hoc signature verification. |
| Original O2 focus/recovery behavior | NOT_RUN | Permission-dependent independent QA remains required. |
| Original O3 pass-through/hit behavior | NOT_RUN | Permission-dependent independent QA remains required. |
| Full dynamic UI acceptance | NOT_RUN | This bounded repair supplies build and geometry evidence only. |

The candidate-bound metadata probe launched the signed repair app and read only its
window owner, layer, on-screen status, alpha, and bounds. The supporting native
constructor probe constructed but did not display its panels. Neither probe requested
screen capture, inspected input permissions, or injected input. They validate the
AppKit frame-construction defect only; independent QA remains responsible for dynamic
overlay acceptance.

## Independent real-macOS procedure

Record the macOS version, hardware, display arrangement and scale modes, tested
foreground applications, app commit, build command, and result for every case.

1. Grant the built SeeThis app Input Monitoring permission, launch it without
   activating it, and focus a text editor containing a visible caret.
2. Hold Command-Control-Option-A. Move the pointer in a circle, then release A.
   Confirm the editor remains frontmost throughout and the overlay shows the path.
3. Before and after drawing, click and drag in an empty overlay region over the
   editor. Confirm the editor receives those events. Click directly on the visible
   finished path and confirm the overlay consumes that hit without taking keyboard
   focus.
4. Start a drawing and press Escape. Repeat while releasing a required modifier,
   switching to another application, and changing display arrangement or scale.
   For each case, confirm the path is cancelled and empty regions immediately pass
   pointer input through again.
5. Exercise a lost key-up by interrupting event delivery while the shortcut is
   held, then release it. Confirm the watchdog returns the controller to idle. Also
   hold for more than 30 seconds and confirm the hard timeout releases capture.
6. With two displays, include one non-Retina scale and one Retina scale if the
   hardware supports them. Draw on each display and across their boundary. Confirm
   the stroke follows the pointer and hit regions remain aligned. Compare the
   declared display ID, logical-point positions and backing scale in a debugger or
   diagnostic build against `NSScreen` values.
7. Repeat focus and pointer-routing cases in at least a native AppKit application
   and one non-AppKit foreground application. Record failures and system permission
   prompts verbatim enough to reproduce them, without recording secrets.

Reference capture, persistence, clipboard links, an agent bridge, Windows/Linux
backends, and deployment are outside this acceptance row.

## Quiet reference inspector (QI1–QI5)

The bounded author regression row requires that pending, indexing and failed
states remain available in the metadata list while the overlay paints only the
geometry. No corner `Loading`/`Indexing` label, spinner, persistent lower-left
status, busy cursor, or non-ready pointer claim is permitted. After a successful
URL-only publication the overlay may show one brief nonactivating
`Link copied — paste into chat` acknowledgement; a failed publication remains
truthful in the inspector and can be retried from the mark. The list is
metadata-only, omits logically deleted tombstones while preserving their URL/API
truth, keeps hidden state across create, delete and background completion, and is
reopened only by the existing menu action. Physical panel
placement, focus, drag, click-through and pasteboard delivery remain **NOT_RUN**
until independent macOS QA exercises them.

## Mark inspector panel (PN1–PN5)

The deterministic source row covers the selection and presentation contract:

- multiple completed circles keep their geometry, while ready marks have no
  permanent time or `Historical` label;
- a new circle and a normal click select one reference, preserve URL-only recopy,
  and update metadata; hover thickens only;
- pending, ready, failed and no-selection states are rendered from metadata
  lookup without screenshot decode, full-record clone, history scan, or
  clipboard access; logically deleted tombstones remain absent from the list;
- the panel can be dragged, hidden and reopened through the native menu;
  hidden state leaves the overlay pass-through and background completion never
  opens it; deleting the selected mark clears or invalidates the details.

Deterministic checks cannot prove physical macOS placement, drag behavior, focus,
keyboard shortcuts, or display disconnect recovery. Independent QA must run those
cases; they remain **NOT_RUN** in this author record.

### Rework regression coverage

After the user hides the inspector, new-circle release, ordinary mark click, and
background completion update the selected metadata without reopening the panel.
Only the native menu reopen action shows it again. The metadata body is
scrollable, so long source/window/display values and the complete geometry rows
remain reachable in the compact 390×300 panel. Selection region must identify
the shared global AppKit logical-point space and upward-increasing Y; Crop pixels
must identify source-image coordinates, their top-left origin, and
downward-increasing Y. The deterministic checks reject ambiguous bare labels and
cover hidden-state persistence and scrollability; they do not prove physical
AppKit clipping, scrolling, or focus behavior.

## Multi-region overlay extension — NOT_RUN

Hold Option+A while the foreground editor has a visible caret. Create two independent
closed strokes, with button-up and cursor movement between them, and confirm only the
button-held portions render. Mouse-up must leave capture mode active; A-up or
Option-up must remove capture ownership after one submission. The overlay must consume
drawing down/drag/up without activating itself or modifying foreground text. After
exit, empty overlay locations must immediately pass through again. Verify no hit area
or visible segment spans the gap, and a boundary crossing does not move the session to
another display. This requires physical macOS observation and remains NOT_RUN.
