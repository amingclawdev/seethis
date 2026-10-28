# SeeThis

**Mark what you mean on your Mac, then share it with your assistant.**

[Website](https://seethis.dev) · [Download v0.1.5](https://github.com/amingclawdev/seethis/releases/tag/v0.1.5) · Apple Silicon · macOS 14.2+ · [MIT](LICENSE)

SeeThis is a macOS menu bar tool. Hold **Option+A**, draw around one or more areas, then release the shortcut. It copies a local reference immediately and prepares a marked image you can paste into a conversation.

The GitHub app is **Developer ID signed, Apple notarized and stapled**. Strict signatures, Gatekeeper and DMG/ZIP app parity checks passed. Choose `SeeThis-0.1.5-macos-arm64.dmg` or the matching ZIP and verify it against `SHA256SUMS` and `RELEASE-MANIFEST.json` from the same release. The ordinary GitHub package was not launched for a separate physical UI acceptance run; it uses the frozen accepted implementation. The Mac App Store distribution is separate.

## Mark, keep context, share

- Draw several separate regions during one shortcut hold. With **Append screenshots** off, release creates a new reference; with it on, later holds add shots with continuous mark numbers.
- Each shot freezes its context when capture starts. Inspector shows **Follows this page**, **Follows this window** or **Follows this app**. App-scoped marks can appear across windows of the same running app on the captured display. Later observations do not silently change a shot's scope.
- Chrome Automation is **optional**. Reliable page context uses page scope; without it, basic window or app capture remains available. Input Monitoring and Screen Recording are required. **Connect Chrome** is an explicit permission action; release all capture keys after it and begin a fresh gesture.
- Notes stay with their numbered marks. The note window's close button saves and copies the owning reference link. Inspector lets you select references and shots, copy their images, and delete an entire reference.
- Return to the matching context to restore eligible marks. A temporary observation failure hides marks without deleting their saved data. **Hide Marks** affects marks globally.

The defaults are **Option+A** for capture and **Option+D** plus a click on a mark boundary to delete its whole reference. Settings and Inspector show any customized shortcuts. See [usage](docs/USAGE.md).

## Start safely

1. Install the verified app in Applications and open it. SeeThis lives in the menu bar and has no main window.
2. Allow **Input Monitoring** and **Screen Recording** for that exact copy. Use the menu's permission settings and retry actions, and reopen manually if macOS requires it.
3. On safe test content, hold Option+A and draw. Release to submit. Open **Show Inspector…**, wait for the image, then choose **Copy marked image**.
4. Inspect the whole image before pasting it. A `127.0.0.1` reference URL works only on the same Mac while SeeThis is running; web chats and remote tools need an attached image.

**The screenshot includes the entire selected display. Marks are not a privacy crop.** Hide private windows and notifications first. Capture metadata may include app/window information and, when authorized, the Chrome page URL. SeeThis keeps references locally and does not upload screenshots itself. [Privacy](PRIVACY.md) · [Security](SECURITY.md)

## 0.1.5 validation

The user accepted the remaining manual checks on the frozen candidate, including reporting one transient flicker followed by stable behavior. Independent source review and automated tests cover capture, context recovery, per-shot scope, Append, notes, deletion, permission handling and storage compatibility. This does not establish every macOS, display, first-download or permission configuration, or that flicker can never recur. The GitHub package's distribution checks are separate; see [release notes](docs/RELEASE.md).

## Guides and development

- [Installation and permissions](docs/INSTALL.md)
- [Capture, Inspector, notes and deletion](docs/USAGE.md)
- [Chrome and optional Automation](docs/CHROME.md)
- [Recovery, updates and uninstalling](docs/TROUBLESHOOTING.md)
- [Build and contribute](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md) · [Release provenance](docs/RELEASE.md) · [Source mapping](SOURCE-MAPPING.json) · [Third-party notices](THIRD_PARTY_NOTICES.md)

The GitHub app has no automatic updater. Intel builds are not provided. The demo below shows an earlier workflow.

https://github.com/user-attachments/assets/7a865105-26e0-4f27-844e-9c71b79335ba
