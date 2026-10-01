# Changelog

## v0.1.0-beta.1 — 2026-10-01

First public prerelease for Apple Silicon / macOS 14.2+.

- Hold the configured capture shortcut (default Option+A) to mark multiple independent regions in one reference.
- Publish a local JSON URL immediately; prepare captured and marked images in the background. Copy marked image is an explicit action.
- Keep the Inspector at 470 × 430 points with a reference dropdown showing name and time. A new reference selects the newest item; deleting it selects the newest remaining item.
- Show Chrome permission recovery after a real foreground Chrome activation when Automation is missing or denied. Connect Chrome, Retry Chrome and Automation Settings provide explicit recovery actions.
- Keep live marks scoped to the original app/window and, for standalone Chrome, tab/page context.
- Open a feedback email draft, with a copy-address fallback; the app does not send email.
- Ship Developer ID signed, Apple-notarized DMG and ZIP, checksums, MIT license, installation and privacy guidance.

The public source snapshot preserves the accepted runtime and build inputs. See [release provenance](docs/RELEASE.md) for the original build identity, source/tag distinction and remaining physical-test limits.
