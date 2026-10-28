# Changelog

## v0.1.5 — 2026-10-28

- Add per-shot frozen page/window/app scope, explained in Inspector. Chrome Automation is optional for basic capture; explicit authorization still requires a fresh gesture.
- Support reliable running-app identity when Launch Services has no launch date, preserving old stored bindings and indexed context lookup.
- Add Append screenshots with separate shot images, continuous numbering and persistent per-mark notes.
- Keep strokes, numbers, notes and hit testing under one visibility rule, recover from temporary observation failures, and delete whole references deliberately.
- Bound asynchronous Chrome work and late results; separate drawing idle time from screenshot/save deadlines.
- Retain local reference delivery, image sharing and separate ordinary/Store distribution paths.

The user accepted the remaining manual checks, reporting one transient flicker followed by stable behavior. Independent source/offline verification passed; the separate ordinary GitHub package passed signing, Apple notarization, stapling and distribution checks recorded in its release manifest. That package was not launched for an additional physical UI acceptance run. See [release notes](docs/RELEASE.md) for limits.

## v0.1.1-beta.1 — 2026-10-02

- Request missing Chrome Automation from the configured physical capture shortcut while Chrome is foreground; keep consent work off the main UI thread and coalesce repeated holds.
- Require release of the original capture key and a fresh gesture after consent; interrupted authorization holds do not create partial references.
- Preserve explicit Connect Chrome and prompt-free retry/background observation, with stale target and lifecycle checks.
- Exclude the current Inspector window as well as drawing panels from newly captured images, including when Inspector visibility changes.
- Publish a fresh Release arm64/macOS 14.2+ build with internal version/build 0.1.1 and production Developer ID identity. Final notarization and checksum results accompany the release assets.

The original demo and v0.1.0-beta.1 assets remain unchanged.

## v0.1.0-beta.1 — 2026-10-01

First public prerelease for Apple Silicon / macOS 14.2+.

- Hold the configured capture shortcut (default Option+A) to mark multiple independent regions in one reference.
- Publish a local JSON URL immediately; prepare captured and marked images in the background. Copy marked image is an explicit action.
- Keep the Inspector at 470 × 430 points with a reference dropdown showing name and time. A new reference selects the newest item; deleting it selects the newest remaining item.
- Show Chrome permission recovery after a real foreground Chrome activation when Automation is missing or denied. Connect Chrome, Retry Chrome and Automation Settings provide explicit recovery actions.
- Keep live marks scoped to the original app/window and, for standalone Chrome, tab/page context.
- Open a feedback email draft, with a copy-address fallback; the app does not send email.
- Ship Developer ID signed, Apple-notarized DMG and ZIP, checksums, MIT license, installation and privacy guidance.

See [release notes](docs/RELEASE.md) for source/build identities, distribution checksums and known verification limits.
