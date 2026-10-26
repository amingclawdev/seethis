# SeeThis 0.1.6 Hackathon demo

This branch provides Reference runs for SeeThis and the companion seethis-runs MCP/plugin 0.4.1. It is separate from the Store release.

## Install

Download the Hackathon DMG or ZIP and SHA256SUMS from the matching release. Verify the files you downloaded with `shasum -a 256`; compare against that release's SHA256SUMS. Open the DMG, drag SeeThis.app into Applications, then open that copy. Follow [installation and permissions](INSTALL.md) for Input Monitoring, Screen Recording and optional Chrome Automation; its older beta download names do not identify this Hackathon build.

The Hackathon app and DMG are Developer ID signed, Apple notarized and stapled. Apple Silicon and macOS 14.2+ are required. The preparation did not install or launch them. First-install permission dialogs, Finder drag installation, another Mac and a full two-run recording remain unverified.

Download `seethis-runs-0.4.1-source.zip` and follow [MCP installation](HACKATHON-MCP-INSTALL.md). That guide covers the source project location, native plugin installation, individual hook trust, bridge connection and fresh Codex conversations.

## Behavior

Reference runs shows saved note changes alongside registered conversations, with English system labels. User-authored Unicode notes, titles, summaries and logs retain their original contents. Run note revisions are corroborated only by an agent report and matching delivery strictly inside the reported run interval. Unknown and unavailable states remain explicit. During-run note changes require telling Codex or handing the reference in again.

## Source and binary mapping

The existing notarized binary was built from exact commit `8b3910667622840b46d77c0e55ec70f17767c98f`. It was not rebuilt from this public export commit. Runtime, build, assets, scripts and test code are exported byte-for-byte with file modes; public documentation is maintained separately. The release's `PUBLIC-SOURCE-MAPPING.json` records every exported file hash and the public commit. No private Git ancestry is included.

Plugin package 0.4.1 uses runtime commit `53c640dbb518ab7dca88d7cac0f5df1d4a09c5c2`; installation documentation is from `d2df7dda7665eb919e0c3ed05d044122fd49027e`. See the release manifest for signatures, Apple submission IDs, hashes and verification limits.
