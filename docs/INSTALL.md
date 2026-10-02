# Install SeeThis beta

For Apple Silicon (M-series) Macs running **macOS 14.2 or later**. SeeThis has a menu bar item and no main window; that is normal.

## Download and verify

1. Open the [v0.1.0-beta.1 release](https://github.com/amingclawdev/seethis/releases/tag/v0.1.0-beta.1). Download **SeeThis-0.1.0-beta.1-macos-arm64-release.dmg** and **SHA256SUMS**, or choose **SeeThis-0.1.0-beta.1-macos-arm64-release.zip** instead.
2. In Terminal, go to your download directory and calculate the downloaded file's checksum:

```sh
cd ~/Downloads
shasum -a 256 SeeThis-0.1.0-beta.1-macos-arm64-release.dmg
# ZIP alternative:
shasum -a 256 SeeThis-0.1.0-beta.1-macos-arm64-release.zip
```

3. Compare the result with SHA256SUMS from the same release. If you downloaded both packages and RELEASE-MANIFEST.json, you can run `shasum -a 256 -c SHA256SUMS`; all three entries should report **OK**. Missing entries for files you did not download do not mean your downloaded file is damaged; check the entries you downloaded individually. If a checksum differs, stop installation and download again.

## Put the app in Applications and open it

1. Double-click the DMG. The mounted volume contains **SeeThis.app** and an Applications shortcut. In Finder, drag the app into Applications, then eject the disk. For the ZIP, extract it and put **SeeThis.app** in Applications.
2. Open SeeThis manually from Applications. If macOS asks whether to open an app downloaded from the internet, check the name and source and follow the system prompt.
3. This release is signed by **Developer ID Application: YING ZHANG (GLHUR8CC29)**. The app and DMG are notarized and have stapled tickets. If macOS reports damage, malware or an unexpected identity, check the source and checksum and [report the problem](TROUBLESHOOTING.md#feedback). Do not turn off system security protections.
4. Open the SeeThis menu and check **Location:** to confirm the running path. Avoid running copies from Downloads, old packages and Applications at the same time.

The DMG layout, signature, notarization and mounted app contents have been technically verified. Actual Finder drag installation of this final DMG has not been retested. If copying fails, record the error and try the ZIP alternative after checking its checksum.

## Input Monitoring and Screen Recording

1. Allow this copy of SeeThis in **System Settings > Privacy & Security > Input Monitoring**. The menu's **Open Input Monitoring Settings…** opens that page. After allowing access, choose **Retry Input Monitoring**. If the status is `restart_required`, choose **Quit SeeThis**, then manually reopen the exact app from Applications.
2. Allow this app in **Screen Recording** (some system versions call it Screen & System Audio Recording), and reopen it if the system asks. **Open Screen Recording Settings…** opens that page.
3. Check Input Monitoring, Screen Recording, Capture shortcut and Delete shortcut readiness separately in the menu. One permission being ready does not establish that capture works.
4. Try a [capture](USAGE.md) on safe test content and inspect the image and copying behavior in Inspector. If the app is still not ready after the system's **Quit & Reopen**, check the exact copy with **Location:** and reopen it manually.

Chrome page identity also needs [Automation](CHROME.md). Accessibility permission and a browser extension are not required. System policy, signature changes or multiple copies can require permission again; continuity cannot be guaranteed. First-download, first-permission and additional macOS versions still need physical verification. See [release verification limits](RELEASE.md).
