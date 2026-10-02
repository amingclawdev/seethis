# v0.1.1-beta.1 release provenance

Public repository: [amingclawdev/seethis](https://github.com/amingclawdev/seethis). Distribution: [v0.1.1-beta.1 prerelease](https://github.com/amingclawdev/seethis/releases/tag/v0.1.1-beta.1), Apple Silicon / macOS 14.2+.

This patch adds Chrome Automation consent from the configured capture shortcut, asynchronous permission handling, and current Inspector exclusion from new captures. After a Chrome permission decision, release the original capture key and use a fresh gesture. Existing capture, page-scoped marks, copying, deletion and explicit Chrome recovery remain available.

## Exact source and production build

This release is rebuilt from the clean public source commit identified by the tag and `RELEASE-MANIFEST.json`; it does not reuse the previous executable. Product and automated-test changes come from the accepted implementation. The release mapping records exact source-file equivalence, with version and public-documentation changes called out separately.

- Bundle identifier: `local.seethis.overlay`.
- Internal version/build: `0.1.1` / `0.1.1`.
- Build: Release, arm64 only, minimum macOS 14.2, selected Apple clang and macOS SDK.
- Production entitlement: `com.apple.security.automation.apple-events`; this GitHub distribution is not an App Store sandbox package.
- Signature: existing Developer ID Application team `GLHUR8CC29`, hardened runtime, established production designated requirement.

The published release is gated on the serial seven-test suite, independent source/package QA, actual Apple notarization **Accepted**, App and DMG stapling, strict signatures and Gatekeeper checks. `RELEASE-MANIFEST.json` records exact commit, final hashes, toolchain and submission/check results. The packaging provenance’s `notarized=false` describes its initial signing stage; final notarization is established by those ticket and release checks.

## Downloads and validation

The release provides `SeeThis-0.1.1-beta.1-macos-arm64-release.dmg`, `SeeThis-0.1.1-beta.1-macos-arm64-release.zip`, `RELEASE-MANIFEST.json` and `SHA256SUMS`. Check the downloaded files against checksums from this same release.

The DMG contains SeeThis.app, an Applications symlink, LICENSE and THIRD_PARTY_NOTICES.md; the ZIP contains the same stapled app and notices. No custom Finder background is used. App, embedded DMG app and ZIP-extracted app parity, signatures, tickets and Gatekeeper are verified before publication.

## Verification limits

Automated tests cover interaction, references, loopback HTTP, input/Inspector state, injected native consent scheduling and capture-exclusion assembly. They do not reproduce every physical OS dialog or certify every assistant’s image handling. A user reported successful fresh-identity authorization and tab/deletion acceptance of the accepted development candidate; this is user-reported acceptance, not an independent telemetry claim for the final production artifact.

The new production build is not launched or installed during release preparation. First-download permissions, Finder drag installation, update/rollback/uninstall residues, another Mac, broader OS versions and the complete physical page/window/Inspector lifecycle matrix remain unverified. Reusing the production identifier, Developer ID team and designated requirement does not guarantee TCC grant continuity. The original demo shows an earlier workflow.

Intel/universal builds, automatic updates and App Store delivery remain outside this beta.

## Developer packaging

```sh
bash scripts/package_macos.sh package --source "$PWD" --commit "$(git rev-parse HEAD)" --configuration Release --mode development --output /tmp/seethis-development-package
```

This makes an ad-hoc development package. Production signing requires an explicitly selected Developer ID identity. This release uses the script’s fresh stable-bootstrap build path, then checks the actual production identifier, team, entitlements and designated requirement against the prior notarized app. The prior app’s initial embedded development flags are preserved. Notarization is a separate release step. CI uploads development artifacts only; it does not publish release assets. Source archives can build/test without Git metadata, while packaging requires exact Git provenance.

---

# Historical v0.1.0-beta.1 release provenance

Public repository: [amingclawdev/seethis](https://github.com/amingclawdev/seethis). Distribution: [v0.1.0-beta.1 prerelease](https://github.com/amingclawdev/seethis/releases/tag/v0.1.0-beta.1), Apple Silicon / macOS 14.2+.

## Source and build identity

The public tag identifies the source, documentation, license and development tests for this release. Runtime/build inputs (`src/`, `web/settings/`, icon assets, CMake files and the icon-generation script) are byte-identical to the original app build source:

- Original runtime build commit: `70135abec8ea9d7260646165e31ecfcd5d6328c3`.
- Original runtime build tree: `4aff6460ddf1b21f690fbf1053319a03ca5fa4c4`.
- Released executable SHA-256: `660e5b9465c382364f723fa2f950b318e339929fc5b2ec9ec2b0f6dead92a5d4`.
- Bundle identifier: `local.seethis.overlay`; internal version/build: `0.1.0` / `0.1.0`.

The release app was built from that original source, then Developer ID signed and notarized. It was **not rebuilt from the later public documentation commit**. The release attachment `RELEASE-MANIFEST.json` records the exact public commit and file hashes that map this snapshot to the original build.

The embedded `seethis-delivery-provenance.v1` records the initial packaging stage; its ad-hoc/notarized=false flags precede final signing. Current signing and notarization are established by the final signature/ticket checks below, not those historical flags. The signed app is preserved intact.

## Distribution identity

| Item | Value |
| --- | --- |
| Signature | Developer ID Application: YING ZHANG (GLHUR8CC29), hardened runtime, secure timestamp |
| Accepted app submission | `bee3ff1d-4699-4cb6-9706-a370bf3c85dc`, Accepted |
| Stapling | Final App, ZIP-extracted App, DMG and embedded-App ticket checks are recorded in RELEASE-MANIFEST.json; all must pass before publication |
| Gatekeeper | App, DMG and ZIP-extracted app accepted as Notarized Developer ID on release host |
| DMG | `SeeThis-0.1.0-beta.1-macos-arm64-release.dmg` |
| DMG SHA-256 | See the final Release attachment SHA256SUMS |
| ZIP | `SeeThis-0.1.0-beta.1-macos-arm64-release.zip` |
| ZIP SHA-256 | See the final Release attachment SHA256SUMS |

Check SHA256SUMS against the downloaded file. The DMG mounts with SeeThis.app and an Applications symlink. The embedded app and ZIP-extracted app preserve the original executable, signature and validated ticket. No custom Finder background is used.

Build toolchain: CMake 4.4.3, Apple clang 21.0.0 (clang-2100.1.1.101), macOS SDK 26.5, arm64, Release, minimum macOS 14.2. Dynamic dependencies are system macOS frameworks/libraries; see [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md).

## Verification limits

Automated tests cover reference lifecycle, interaction, HTTP authorization/images, Inspector and permission states with synthetic fixtures. Distribution checks verify signatures, tickets and app content parity. These checks do not certify every remote assistant's image handling.

Final DMG Finder drag installation, first-download/first-TCC authorization, update/rollback/uninstall residues, another Mac and broader OS versions remain unverified. Technical mounting, layout, payload/signature and Gatekeeper checks do not establish Finder drag success. The demo shows an earlier workflow.

Intel/universal distribution, automatic updates and App Store delivery are outside this beta.

## Developer packaging

A fresh development package can be made from a clean checkout at an exact commit:

```sh
bash scripts/package_macos.sh package --source "$PWD" --commit "$(git rev-parse HEAD)" --configuration Release --mode development --output /tmp/seethis-development-package
```

This builds and ad-hoc signs a new app; it does not reproduce the Developer ID/notarized release identity or submit notarization. CI likewise creates clearly labeled development artifacts. Stable signing paths require an explicitly selected Developer ID identity and the script's continuity checks. Source archives can build/test without Git metadata; packaging requires Git commit provenance.
