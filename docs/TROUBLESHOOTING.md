# Recovery, updates and uninstalling

Open the SeeThis menu and check **App:** and **Location:** for the actual running path. Keep the version and downloaded asset's SHA-256 so feedback does not mix the state of different copies.

| Status or symptom | Next step |
| --- | --- |
| Input Monitoring not ready/denied | Allow the exact app in system Input Monitoring and choose Retry Input Monitoring. For restart_required, quit and reopen manually. |
| Screen Recording not ready | Open Screen Recording Settings, allow the exact app, reopen as prompted and try a real capture. |
| Still not ready after Quit & Reopen | Quit duplicate copies, open the authorized path manually and check Location and each readiness status. |
| Permissions ready but shortcuts do not work | Check the actual Capture and Delete configuration in Inspector/settings. Release all keys and choose Retry Input Monitoring. |
| Link copied but no image | Wait for ready, then explicitly choose Copy marked image. Background completion does not change the clipboard. |
| A remote chat cannot open the URL | Paste or attach the actual image. A same-Mac URL is not exposed remotely. |
| Marks hide after switching Chrome tabs | Return to the original app/window/tab/page, wait for page observation and check Automation. |
| Chrome has no permission entry or access is denied | Activate standalone Chrome and press the configured capture shortcut, or choose Connect Chrome for recovery. After Allow, release the original capture key and use a fresh gesture. If denied, allow access in Automation settings and choose Retry Chrome. See [Chrome](CHROME.md). |
| View details or copying is unavailable | Check pending/failed/expired/deleted status. Wait or capture safe content again. |
| Finder copy fails | Record the exact error and recheck the checksum. Try the same release's ZIP alternative. Final DMG drag installation has not been fully retested. |
| macOS reports damage, malware or an unexpected identity | Stop running the app, check official asset checksums and the source, and report a safe reproduction. Do not disable system protections. |
| A managed device cannot allow access or install the app | Contact the device administrator and follow device policy. |

## Updates

Download the new asset and SHA256SUMS and verify them with the [installation steps](INSTALL.md). Read that version's known limits. Choose **Quit SeeThis** in the old app, quit other copies and put the verified new app in the original Applications location. Check the target if prompted to replace it, and keep the old package until the new version works.

Open the exact path manually. Check Location and Input Monitoring, Screen Recording and Capture/Delete shortcut readiness. Allow access, retry or quit and reopen as needed. A normal replacement keeps Application Support settings and valid references; expired/deleted references are not restored. Signature changes, multiple copies and system policy may affect TCC, so permission continuity cannot be guaranteed. SeeThis has no automatic updater. The complete update/rollback matrix has not been retested.

## Uninstalling and keeping data

1. Choose **Quit SeeThis**. In Finder, move the intended Applications/SeeThis.app copy to Trash. Removing the app keeps user data and system permission entries.
2. To keep settings and references, keep `~/Library/Application Support/SeeThis`.
3. If you are sure you no longer want the local data, enter that path in Finder's **Go > Go to Folder…**, check it and move it to Trash yourself. `settings-v1.json` contains settings; `references-v1` contains references and screenshots. Do not delete other Application Support content.
4. You can disable or remove SeeThis permissions in Privacy & Security without resetting TCC. Emptying Trash is a separate, irreversible action and does not erase backups, clipboard copies or shared attachments.

These steps describe the data behavior. Complete physical uninstall and residual-data verification remains unfinished.

## Feedback

Record the version, downloaded asset's SHA-256, macOS/Chrome versions, precise status, steps and expected/actual results. Reproduce on a test page without private content first. For ordinary problems, use [GitHub Issues](https://github.com/amingclawdev/seethis/issues/new/choose), or choose **Feedback…** in Inspector to open an email draft to `z5866318@gmail.com`. You decide whether to send it; the app does not automatically attach information.

Share only necessary, safe screenshots. Do not publish the whole desktop, real tab URLs/titles, reference capability URLs or settings access addresses. You can inspect individual `seethis capture` / `seethis chrome` status/reason entries in Console before sharing them; do not upload entire logs. Use the [private security channel](../SECURITY.md) for security issues.

## Store variant and support requests

A sandboxed distribution, when available, keeps its own container Application Support/SeeThis data; it does not automatically migrate the GitHub app’s data. For manual data removal, use the actual registered app container path, check the exact target and preserve anything you want to keep. Do not remove unrelated containers. Store and Developer ID signatures differ, so permissions are not promised to carry over.

Use the existing support email `z5866318@gmail.com` for deletion requests. For support cases opened after this policy is published, maintainers use support information only for help and product fixes, never advertising, model training or data brokering; they manually delete private email 90 days after case closure. Public technical issue/fix records may remain with personal-information removal requests. Provider backups/caches and third-party copies may persist. Historical practices and automatic cleanup are not claimed. See [privacy](../PRIVACY.md). The current GitHub release is v0.1.1-beta.1; App Store distribution is not yet available.
