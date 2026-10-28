# macOS Inspector Feedback acceptance

Candidate row: `SEETHIS-INSPECTOR-FEEDBACK-MAILTO-20260930`.

This is an acceptance procedure, not a physical acceptance result. The document
worker starts from `663b5823f6297a04b8a7bb861a0ffaec162334c7`; that base does not
include the separate Feedback source change. Independent QA must bind results to
the exact combined candidate commit and binary. Every physical case below is
**NOT_RUN**. A successful adapter test, build, signature verification or package
checksum does not establish visible UI behavior, email delivery or public release.

## Intended behavior

The Inspector exposes **Feedback** without requiring a selected reference. At
the minimum Inspector width of **470 logical points**, the action remains
visible, enabled, readable and clickable, with no overlap or clipping. Existing
reference selection, copy, thumbnail and scroll behavior must still work.

An explicit Feedback click asks the default email client to compose a draft for
`z5866318@gmail.com`, with the subject `SeeThis Feedback`. The expected URL is
`mailto:z5866318@gmail.com?subject=SeeThis%20Feedback`: one recipient, correctly
encoded subject, no body or additional fields. SeeThis must not attach screenshots,
logs, reference links, capability URLs, clipboard contents or other private data.
The user writes any message and independently chooses whether to send it. Opening
a client is not sending an email; SeeThis must not report “sent” or claim delivery.

When no handler exists or opening fails, show a truthful failure and an explicit
**Copy email address** fallback. Opening Feedback, showing failure, dismissing it
and cancelling a draft must not write to the clipboard. Only deliberately choosing
the copy fallback writes the exact address `z5866318@gmail.com`.

## Deterministic adapter checks

The source worker and independent QA record actual commands, candidate identities,
exit codes and test artifacts separately. These expected checks are **NOT_RUN by
the document worker**; they do not launch a mail client or send mail.

| ID | Fake-adapter setup and required assertion | Document-worker result |
| --- | --- | --- |
| FA-1 | Invoke Feedback with zero selected references. The adapter receives exactly the URL above once; decoded recipient and subject match; body, cc, bcc and attachment fields are absent. | NOT_RUN |
| FA-2 | Fake opener returns true. Treat this only as an accepted open request; no send/delivery claim and zero clipboard writes. | NOT_RUN |
| FA-3 | Fake opener returns false. Show truthful failure with an explicit copy-address action; no success/sent state and zero clipboard writes before that action. | NOT_RUN |
| FA-4 | Fake handler availability is absent. Follow the same truthful fallback contract, without relying on an actual host mail application. | NOT_RUN |
| FA-5 | Choose copy explicitly after failure. Exactly one write contains only the exact email address; failure display, dismissal and repeated rendering do not copy automatically. | NOT_RUN |
| FA-6 | Construct the Inspector at 470pt with no selection and with reference metadata. Feedback is enabled and its bounds fit the header without intersecting neighboring controls; retain reference/copy/thumbnail regression checks. | NOT_RUN |

If handler absence and opener false share an implementation path, record that
relationship and the fixture actually exercised rather than inventing a separate
host observation. Constructor bounds establish deterministic geometry only;
physical rendering and hit testing still require the cases below.

## Independent physical procedure — NOT_RUN

Record the tester, timestamp, macOS build, hardware, display scale, exact candidate
commit/tree, executable SHA-256, bundle path/identifier and signature identity.
Record the existing default mail client and permission/settings state without
changing them. Use a separately accepted candidate and disposable test references;
preserve the user's installed app, settings, reference data and TCC. Do not reset
TCC, bypass Gatekeeper, install or change a mail handler, or send an email for this
procedure. If a required environment is unavailable, record **UNAVAILABLE** with
the reason. Expected results are not observed results.

| ID | Deliberate operation | Expected observation | Actual status |
| --- | --- | --- | --- |
| UI-1 | Open Inspector with no selected references; resize to 470pt. | Feedback remains visible, enabled and clickable; label and neighboring controls are neither clipped nor overlapping. | NOT_RUN |
| UI-2 | Repeat at 470pt with a selected reference, long metadata and thumbnails; scroll, select another reference and use the existing reference-copy action separately. | Feedback stays reachable; metadata scrolling, selection, thumbnail rendering and existing URL-only copy retain their behavior. Record each regression separately. | NOT_RUN |
| UI-3 | Establish a harmless clipboard sentinel, then click Feedback once in a host with an existing mail handler. Inspect the opened compose window without sending. | The existing default client opens a draft to exactly `z5866318@gmail.com` with subject `SeeThis Feedback`, blank body, no cc/bcc and no attachments. No private payload is inserted; SeeThis makes no sent/delivery claim; sentinel is unchanged. | NOT_RUN |
| UI-4 | Close/cancel that draft without sending and return to Inspector. | No mail is sent by SeeThis, no clipboard mutation occurs and Inspector/reference state is retained. A client-specific save-draft prompt is recorded separately. | NOT_RUN |
| UI-5 | On an already suitable isolated host with no mail handler, click Feedback; also exercise an accepted open-failure fixture if available. Do not alter the user's defaults to manufacture failure. | Truthful failure and an explicit copy-address fallback appear; no fake success/sent state, private payload or automatic clipboard write. Record no-handler and open-failure observations separately. | NOT_RUN |
| UI-6 | With that failure visible, first dismiss without copying; then repeat and deliberately choose Copy email address. | Dismissal leaves the sentinel unchanged. The explicit copy replaces it with only `z5866318@gmail.com`; a copy acknowledgement must describe copying, never sending. | NOT_RUN |
| UI-7 | Reopen/hide Inspector and repeat its established reference, focus and thumbnail interaction checks on the exact combined candidate. | Feedback integration introduces no regression in those interactions. Permission-dependent cases stay UNAVAILABLE/NOT_RUN until actually observed. | NOT_RUN |

For clipboard checks, establish the sentinel only as a deliberate tester action.
Record equality/change and the known test address; do not collect unrelated
clipboard contents. Retain bounded observations without exporting private screen,
reference or mail content. The compose draft must remain unsent throughout testing.

## Evidence and review boundary

Keep deterministic test results, build/signature/package results and physical
observations in separate records. Each physical row needs its own PASS, FAIL or
UNAVAILABLE result tied to the exact candidate and measured environment; the
author cannot sign off independent QA. The dedicated observer obtains independent
candidate QA before normal merge. This document neither closes the source backlog
nor authorizes a public release.

Preserve the first-launch and DMG histories, the frozen `b5c90` diagnostic ZIP and
the prior `ec1fe` review. Earlier screenshots, package checks and reviews cannot be
promoted to Feedback acceptance; affected physical captures need the exact final
candidate recheck. No existing physical gaps are changed to PASS by this checklist.
