# v0.1.0-beta.1 release provenance

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
