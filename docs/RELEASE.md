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

Release attachments:

- `SeeThis-0.1.5-macos-arm64.dmg`
- `SeeThis-0.1.5-macos-arm64.zip`
- `SHA256SUMS`, `RELEASE-MANIFEST.json` and source mapping

The ordinary GitHub app uses bundle identifier `local.seethis.overlay` and the established Developer ID distribution identity, with hardened runtime. It is separate from the sandboxed Mac App Store app and its data container. No automatic data migration or permission continuity is promised.

The app and DMG are **Developer ID signed, Apple notarized and stapled**. Apple accepted the app submission `5c0bc113-fada-4b47-b8d2-981211861e49` and DMG submission `3bd21b23-2b8b-4ba4-ac81-b0055d7fed5a`. Strict signature, stapled-ticket, Gatekeeper and DMG integrity checks passed. The DMG and ZIP contain the same validated app bytes and modes.

| Download | SHA-256 |
| --- | --- |
| `SeeThis-0.1.5-macos-arm64.dmg` | `4594fcb69741f0548d522f346ca4a0f030a99aa7513913b66c1601b5ec49cc0a` |
| `SeeThis-0.1.5-macos-arm64.zip` | `556e4d5606d3e7b23305a564d81373674d75518b2bc2fea12b4760e4ca4e94a9` |

Check `RELEASE-MANIFEST.json` and `SHA256SUMS` from the same release before installation. The app's embedded build provenance describes the initial packaging stage; its initial `notarized=false` does not replace the final Apple acceptance and ticket checks.

## Validation and limits

The user accepted the remaining manual checks on the frozen candidate, reporting one transient flicker followed by stable behavior. Independent source/offline QA passed, including the 13-test suite, targeted context-scope and process-identity regressions, serialization compatibility and the final test teardown correction. The ordinary GitHub package was freshly built from the same mapped frozen implementation. Its distribution checks passed separately; it was not installed or launched for an additional physical UI acceptance run.

User acceptance is not exhaustive telemetry for individual gestures or all machines. First-download permissions, update/rollback, other macOS releases and every display/app combination are not universally certified. Earlier physical failures remain part of the development history; this release does not promise that flicker can never recur. The demo shows an earlier workflow. Inspect a safe capture and its scope before relying on it.

## Development package

```sh
bash scripts/package_macos.sh package --source "$PWD" --commit "$(git rev-parse HEAD)" --configuration Release --mode development --output /tmp/seethis-development-package
```

This builds an ad-hoc development package. It does not reproduce the Developer ID release or submit notarization. See [CONTRIBUTING](../CONTRIBUTING.md).

Earlier release assets and their manifests remain attached to their original tags: [v0.1.1-beta.1](https://github.com/amingclawdev/seethis/releases/tag/v0.1.1-beta.1) and [v0.1.0-beta.1](https://github.com/amingclawdev/seethis/releases/tag/v0.1.0-beta.1).
