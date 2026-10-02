# Google Chrome page marks

This path supports normal windows in the standalone **Google Chrome** app (`com.google.Chrome`). Browsers embedded in other apps are outside this Chrome path. Set up Input Monitoring and Screen Recording first.

## Automation and recovery

1. Open a safe page in standalone Chrome and give Chrome a real foreground activation.
2. When Chrome Automation is missing, press the configured physical capture shortcut (default **Option+A**). This explicit action requests consent asynchronously. The app’s main UI remains available while macOS decides whether to show a dialog.
3. If macOS asks whether **SeeThis may control Google Chrome**, click **Allow**. Input Monitoring and Screen Recording are separate permissions; a screen/audio or bypass-window-picker notice is not the Chrome control dialog.
4. Release the original capture key and modifiers completely, then use a **fresh capture gesture** on the safe page. Grant completion does not resume the interrupted hold or automatically create a reference. Repeats while the same key is held do not make additional requests.
5. **Connect Chrome** remains an explicit recovery action in the menu and in Inspector when recovery is needed. **Retry Chrome** refreshes permission/page readiness without requesting consent. If access was denied, use **Automation Settings…** to open **System Settings > Privacy & Security > Automation > SeeThis > Google Chrome**, change the switch yourself, then retry. macOS policy may require this recovery instead of repeating a dialog.

Starting in the background, passive page observation, opening Inspector and using non-Chrome or already-authorized Chrome do not request Automation. Chrome targets are checked against their current process identity, so stale queued requests are abandoned after target replacement. Neither a browser extension nor Chrome’s “Allow JavaScript from Apple Events” setting is required.

Denied/unavailable access, ambiguous page identity and a timeout are not success. That capture should not save a reference or copy a URL. Keep the correct standalone Chrome window in the foreground, check **Location:** for the authorized app copy, release the shortcut and retry. If the problem persists, quit duplicate copies, manually reopen the exact app and record the precise status.

## Page isolation

Create a mark in a safe tab A. When you switch to tab B, A's live marks should hide. Return to A and wait for a fresh page observation; undeleted marks should reappear. Different tabs have different identities even when their URLs match. Navigating to another page hides the original marks; returning to the original context restores them. Switching apps/windows also respects the original context.

Quitting and reopening Chrome changes its process identity, so old marks are not guaranteed to appear automatically in the new process. Valid historical references can still be copied or previewed in Inspector. The selected history item and explicit copy actions are evaluated separately from live marks' availability on the current page.

The complete tab/reload/back/window, first-permission, reopening and multi-system matrix has not been fully retested. Implementation and automated tests cover page isolation rules; those checks do not establish that the entire physical matrix passed. Use safe test content in feedback, and do not publish real tab URLs, titles or reference read-access capability URLs.
