# SeeThis v0.1.5

[GitHub release](https://github.com/amingclawdev/seethis/releases/tag/v0.1.5) · Apple Silicon · macOS 14.2+

## Changes

- Append later captures as separate shots in one reference, with continuous numbering and notes bound to their marks.
- Freeze each new shot to page, window or app scope from reliable evidence available at capture start. Inspector explains the scope. Automation is optional for basic Chrome capture; page association uses it only when authorized and ready.
- Preserve saved marks through temporary context unavailability, and keep drawing, badges, notes and hit testing under the same visibility rules. App scope follows the same running app and display, including apps without a Launch Services launch date.
- Keep native Chrome work off the UI thread, bound physical operations and reject superseded or late results. Preserve explicit consent and fresh-gesture requirements.
- Separate renewable drawing idle time from screenshot/save deadlines, and retain indexed presentation lookup for large histories.

## Source provenance

The frozen implementation is `d2d9054bed1df4066f05454aaf12fd7304a86d58` (tree `ba283e5b18be1c0a715270b6c8e26b72d956ca6e`). This repository exports its runtime, build, asset, script, test and license files with identical bytes and executable modes while retaining the public repository's history. No private development history is imported. [SOURCE-MAPPING.json](../SOURCE-MAPPING.json) lists every mapped file. Public documentation is maintained separately; the final release manifest identifies the public commit used for distribution.

## Downloads and distribution checks

Expected release attachments:

- `SeeThis-0.1.5-macos-arm64-release.dmg`
- `SeeThis-0.1.5-macos-arm64-release.zip`
- `SHA256SUMS`, `RELEASE-MANIFEST.json` and source mapping

The ordinary GitHub app uses bundle identifier `local.seethis.overlay` and the established Developer ID distribution identity, with hardened runtime. It is separate from the sandboxed Mac App Store app and its data container. No automatic data migration or permission continuity is promised.

**Release preparation status:** final signing, Apple notarization, stapling, Gatekeeper and archive-parity receipts are pending at the time of this source update. This document does not claim that notarization has succeeded. Publication requires those checks to pass and their results to be recorded in `RELEASE-MANIFEST.json`. Check the final manifest and `SHA256SUMS` from the same release before installation.

## Validation and limits

The user accepted the remaining manual checks on the frozen candidate, reporting one transient flicker followed by stable behavior. Independent source/offline QA passed, including the 13-test suite, targeted context-scope and process-identity regressions, serialization compatibility and the final test teardown correction. These results are distinct from the final GitHub package's signing/notarization checks.

User acceptance is not exhaustive telemetry for individual gestures or all machines. First-download permissions, update/rollback, other macOS releases and every display/app combination are not universally certified. Earlier physical failures remain part of the development history; this release does not promise that flicker can never recur. The demo shows an earlier workflow. Inspect a safe capture and its scope before relying on it.

## Development package

```sh
bash scripts/package_macos.sh package --source "$PWD" --commit "$(git rev-parse HEAD)" --configuration Release --mode development --output /tmp/seethis-development-package
```

This builds an ad-hoc development package. It does not reproduce the Developer ID release or submit notarization. See [CONTRIBUTING](../CONTRIBUTING.md).

Earlier release assets and their manifests remain attached to their original tags: [v0.1.1-beta.1](https://github.com/amingclawdev/seethis/releases/tag/v0.1.1-beta.1) and [v0.1.0-beta.1](https://github.com/amingclawdev/seethis/releases/tag/v0.1.0-beta.1).
