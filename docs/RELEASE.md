# v0.1.0-beta.1 release provenance

Public repository: [amingclawdev/seethis](https://github.com/amingclawdev/seethis). Distribution: [v0.1.0-beta.1 prerelease](https://github.com/amingclawdev/seethis/releases/tag/v0.1.0-beta.1), Apple Silicon / macOS 14.2+.

## Source and accepted runtime

The public tag identifies a sanitized source snapshot with finalized license, documentation and CI. Runtime/build inputs (`src/`, `web/settings/`, icon assets, CMake files and the icon-generation script) are byte-identical to the accepted build source:

- Original runtime build commit: `70135abec8ea9d7260646165e31ecfcd5d6328c3`.
- Original runtime build tree: `4aff6460ddf1b21f690fbf1053319a03ca5fa4c4`.
- Released executable SHA-256: `660e5b9465c382364f723fa2f950b318e339929fc5b2ec9ec2b0f6dead92a5d4`.
- Bundle identifier: `local.seethis.overlay`; internal version/build: `0.1.0` / `0.1.0`.

The release app was built from that original source, then Developer ID signed and notarized. It was **not rebuilt from the later public documentation commit**. Private development history and control records are excluded from the new public history. The release attachment `RELEASE-MANIFEST.json` records the exact public commit and file hashes that map this snapshot to the original build.

The embedded `seethis-delivery-provenance.v1` records the initial packaging stage; its ad-hoc/notarized=false flags precede final signing. Current signing and notarization are established by the final signature/ticket checks below, not those historical flags. The signed app is preserved intact.

## Distribution identity

| Item | Value |
| --- | --- |
| Signature | Developer ID Application: YING ZHANG (GLHUR8CC29), hardened runtime, secure timestamp |
| Current Apple submission | `bee3ff1d-4699-4cb6-9706-a370bf3c85dc`, Accepted |
| Stapling | ZIP contains the stapled app; final DMG and embedded-app ticket validation is required before publication |
| Gatekeeper | App, DMG and ZIP-extracted app accepted as Notarized Developer ID on release host |
| DMG | `SeeThis-0.1.0-beta.1-macos-arm64.dmg` |
| DMG SHA-256 | See the final Release attachment SHA256SUMS |
| ZIP | `SeeThis-0.1.0-beta.1-macos-arm64.app.zip` |
| ZIP SHA-256 | `d821a2bdf4f8d164618e6e5431012e2c08969148327056f69fbcafc3aaa027d0` |

Check SHA256SUMS against the downloaded file. The DMG mounts with SeeThis.app and an Applications symlink. Publication requires its embedded app and the ZIP-extracted app to preserve the frozen executable and valid signature/ticket. No custom Finder background is used.

Build toolchain: CMake 4.4.3, Apple clang 21.0.0 (clang-2100.1.1.101), macOS SDK 26.5, arm64, Release, minimum macOS 14.2. Dynamic dependencies are system macOS frameworks/libraries; see [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md).

## Verification limits

Current source/build/signature checks and user local acceptance cover the accepted runtime on the release Mac. Automated tests cover reference lifecycle, interaction, HTTP authorization/images, Inspector and permission states, with fixtures rather than real user data. The earlier pending HTTP diagnostic is superseded by the accepted runtime and current source tests; that does not certify every remote assistant's image handling.

The complete physical acceptance matrix has not been rerun row by row. Final DMG Finder drag installation, first-download/first-TCC authorization, update/rollback/uninstall residues, another Mac and broader OS versions remain unverified. Technical mounting, layout, payload/signature and Gatekeeper checks do not establish Finder drag success. The demo shows an earlier workflow; old install/permission screenshots are omitted to avoid presenting them as current release evidence.

Intel/universal distribution, automatic updates and App Store delivery are outside this beta.

## Developer packaging

A fresh development package can be made from a clean checkout at an exact commit:

```sh
bash scripts/package_macos.sh package --source "$PWD" --commit "$(git rev-parse HEAD)" --configuration Release --mode development --output /tmp/seethis-development-package
```

This builds and ad-hoc signs a new app; it does not reproduce the Developer ID/notarized release identity or submit notarization. CI likewise creates clearly labeled development artifacts. Stable signing paths require an explicitly selected Developer ID identity and the script's continuity checks. Source archives can build/test without Git metadata; packaging requires Git commit provenance.
