# Install SeeThis 0.1.5

For Apple Silicon (M-series) Macs running **macOS 14.2 or later**. SeeThis has a menu bar item and no main window; that is normal.

## Download and verify

1. Open the [v0.1.5 release](https://github.com/amingclawdev/seethis/releases/tag/v0.1.5). Download **SeeThis-0.1.5-macos-arm64.dmg** and **SHA256SUMS**, or choose **SeeThis-0.1.5-macos-arm64.zip** instead.
2. In Terminal, go to your download directory and calculate the downloaded file's checksum:

```sh
cd ~/Downloads
shasum -a 256 SeeThis-0.1.5-macos-arm64.dmg
# ZIP alternative:
shasum -a 256 SeeThis-0.1.5-macos-arm64.zip
```

3. Compare the result with SHA256SUMS from the same release. If you downloaded both packages and RELEASE-MANIFEST.json, you can run `shasum -a 256 -c SHA256SUMS`; all entries for downloaded files should report **OK**. Missing entries for files you did not download do not mean your downloaded file is damaged; check the entries you downloaded individually. If a checksum differs, stop installation and download again.

## Put the app in Applications and open it

1. Double-click the DMG. The mounted volume contains **SeeThis.app** and an Applications shortcut. In Finder, drag the app into Applications, then eject the disk. For the ZIP, extract it and put **SeeThis.app** in Applications.
2. Open SeeThis manually from Applications. If macOS asks whether to open an app downloaded from the internet, check the name and source and follow the system prompt.
3. The app and DMG are signed by **Developer ID Application: YING ZHANG (GLHUR8CC29)**, Apple notarized and stapled. Strict signature, ticket and Gatekeeper checks passed. See RELEASE-MANIFEST.json from the same release for the submission and package checks. If macOS reports damage, malware or an unexpected identity, check the source and checksum and [report the problem](TROUBLESHOOTING.md#feedback). Do not turn off system security protections.
4. Open the SeeThis menu and check **Location:** to confirm the running path. Avoid running copies from Downloads, old packages and Applications at the same time.

The final release manifest records DMG layout, signature, notarization and app-content checks. These distribution checks are separate from manual product acceptance; Finder drag installation on every machine is not guaranteed. If copying fails, record the error and try the ZIP alternative after checking its checksum.

## Input Monitoring and Screen Recording

1. Allow this copy of SeeThis in **System Settings > Privacy & Security > Input Monitoring**. The menu's **Open Input Monitoring Settings…** opens that page. After allowing access, choose **Retry Input Monitoring**. If the status is `restart_required`, choose **Quit SeeThis**, then manually reopen the exact app from Applications.
2. Allow this app in **Screen Recording** (some system versions call it Screen & System Audio Recording), and reopen it if the system asks. **Open Screen Recording Settings…** opens that page.
3. Check Input Monitoring, Screen Recording, Capture shortcut and Delete shortcut readiness separately in the menu. One permission being ready does not establish that capture works.
4. Try a [capture](USAGE.md) on safe test content and inspect the image and copying behavior in Inspector. If the app is still not ready after the system's **Quit & Reopen**, check the exact copy with **Location:** and reopen it manually.

Chrome Automation is optional. It enables page-level context in standalone Chrome; basic window/app capture can work without it. Choose **Connect Chrome** explicitly if you want page association. After a permission decision, release the original capture keys and make a fresh gesture. Accessibility permission and a browser extension are not required. Input Monitoring and Screen Recording remain required for all capture scopes. System policy, signature changes or multiple copies can require permission again; continuity cannot be guaranteed. See [Chrome](CHROME.md) and [release verification limits](RELEASE.md).
