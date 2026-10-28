# Compact Ready entry and details — R3 acceptance

Source of truth: backlog `SEETHIS-COMPACT-READY-ENTRY-AND-DETAILS-R3-20261019`. The CRD IDs and criterion text below are copied exactly from the frozen backlog. This document records how to check each criterion; it does not redefine them.

| ID | Frozen acceptance text |
| --- | --- |
| CRD-01 | The outward SeeThis entry presents the app name and a compact Ready/Not ready summary for the two base permission settings. Chrome has a separate compact status item; the long diagnostic list remains inside Show SeeThis. |
| CRD-02 | Clicking that entry opens Show SeeThis details, including when permissions are missing or the app is Not ready. |
| CRD-03 | Detailed permission, shortcut, settings and Chrome diagnostics, recovery guidance, app identity/location information and related controls are available inside Show SeeThis. A separate compact Chrome status does not expand these details in the outward menu. |
| CRD-04 | SeeThis is Ready when both base settings are completed: Input Monitoring permission and Screen Recording permission in the current supported version. Missing, revoked or unverified completion of either yields Not ready. The summary reflects these two settings, not an AND of all diagnostic checks. |
| CRD-05 | Moving diagnostics preserves the underlying permission checks and supported recovery/settings actions. Merely displaying the summary or opening details does not automatically run a recovery action or request additional permission. |
| CRD-06 | The compact entry remains readable and usable in both states, updates with relevant state changes, and does not restore the long diagnostic list outside Show SeeThis. |
| CRD-07 | Chrome status is independent from the SeeThis Ready predicate. With both base settings completed, Chrome being unused, not running, disconnected, unauthorized or lacking page identity does not turn SeeThis into Not ready. Chrome-specific details remain truthful, and inspecting status does not force Chrome setup or permission requests. |

## Candidate checks

| ID | Automated or source-backed check | Physical observation |
| --- | --- | --- |
| CRD-01 | `input_permission_states` checks the compact permission label and the native menu construction for a separate compact Chrome item outside the Show panel. The menu source check also rejects long diagnostic rows in the outward menu. | **NOT_RUN** — observe the menu in the exact candidate app. |
| CRD-02 | Native menu source check verifies the Ready/Not ready item has the Show selector and target; the explicit-open predicate covers both permission states. | **NOT_RUN** — click the actual entry in both states. |
| CRD-03 | Native source check verifies detailed input, screen, shortcut, settings, Chrome, identity, and recovery controls are built inside Show, while the outward Chrome row stays compact. | **NOT_RUN** — inspect Show and the outward menu. |
| CRD-04 | Nine permission combinations prove only granted Input Monitoring plus ready Screen Recording yields Ready; tap and shortcut faults stay independent. Native refresh uses nonprompting preflights. | **NOT_RUN** — change TCC grants on the exact app. |
| CRD-05 | Source check verifies summary refresh and Show do not call tap retry, permission prompts, or Settings open. Existing explicit actions remain in Show. | **NOT_RUN** — watch prompts and recovery behavior while opening and dismissing. |
| CRD-06 | Source check verifies timer-driven label and Chrome updates, an enabled summary item, and a short outward menu. | **NOT_RUN** — assess clickability/readability and updates in macOS. |
| CRD-07 | Permission matrix proves Chrome is outside the Ready predicate. Source check verifies compact Chrome state derives from nonprompting availability and unknown stays explicit when unverified. | **NOT_RUN** — test absent, disconnected, unauthorized, and available Chrome without triggering setup. |

`input_permission_states` links only `seethis_core`, so its native-menu assertions read the production `src/platform/mac/input_mac.mm` source directly. They are structural checks, not a simulated AppKit click. A physical GUI/TCC/Chrome/capture pass must be recorded separately against the exact built app path and bundle identifier; no physical pass is claimed here.

The inherited timeout regression is covered by `interaction_state_machine`: `TestNativeDragIdleTimeout` drives `HandleCapturePointerMotion` at the idle boundary. `input_permission_states` also covers timeout and user-input tap interruption classification and bounded recovery. These are additional regressions; they do not replace any CRD criterion.

R2 independent QA found the prior candidate failed CRD-01, CRD-02, CRD-03, and CRD-06 because the outward summary item had `action:nil`, Chrome lacked a separate outward item, and the R2 test sheet reassigned CRD IDs. R3 keeps that failure as input evidence until new independent QA runs.

## MUI-01–04 menu follow-up

The outward menu has a direct **Show Inspector** action. Selecting it shows the Inspector even when the base permissions are missing; selecting it again while the Inspector is visible leaves it visible. **Show SeeThis** remains the detailed readiness and recovery view. **Quit SeeThis** remains available. Opening the menu, Show SeeThis, or Inspector must not automatically request Input Monitoring, Screen Recording, or Chrome Automation permission. Only the existing explicit recovery/connection actions may initiate their associated requests.

The outward labels are exactly **SeeThis — Ready** or **SeeThis — Not ready**, and **Chrome — Ready** or **Chrome — Not ready**. The SeeThis result depends only on Input Monitoring and Screen Recording grants. Chrome is optional and cannot change that result. Chrome Automation consent alone does not prove usable page/provider identity: unchecked, not running, unavailable, and consent-granted-but-page-unchecked states display **Chrome — Not ready**. The detailed reason and manual recovery stay inside Show SeeThis. **Chrome — Ready** requires the existing capability state to establish it honestly; viewing the menu or details must not cause a page probe or consent request just to change this label.

`input_permission_states` asserts the formatter's observable labels and the base permission matrix. Source inspection supports menu wiring and prompt-free entry checks, but only a physical click through the built candidate can establish AppKit behavior. The physical matrix in `macos_end_to_end_acceptance.md` remains **NOT_RUN** until independent QA records observations.
