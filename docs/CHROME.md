# Google Chrome page marks

This path supports normal windows in the standalone **Google Chrome** app (`com.google.Chrome`). Browsers embedded in other apps are outside this Chrome path. Set up Input Monitoring and Screen Recording first.

## Automation and recovery

1. Open a safe test page in standalone Chrome. Give Chrome a real foreground activation, for example by switching to another app and then back to Chrome.
2. If SeeThis's Chrome Automation permission is missing or denied, Inspector shows a Chrome recovery section with **Connect Chrome**, **Retry Chrome** and **Automation Settings…**. It appears only when the current Chrome context needs permission recovery.
3. Choose **Connect Chrome** to make an explicit Automation request. If macOS asks whether SeeThis may control Google Chrome, check the name and allow access. If access was denied, choose **Automation Settings…** to open **System Settings > Privacy & Security > Automation > SeeThis > Google Chrome**. Enable the switch yourself, then choose **Retry Chrome**.
4. Return to the original Chrome test page, wait for page identity to be ready and capture with the configured shortcut. The recovery section disappears once permission is ready; a real capture is still needed to verify that marks work.

Starting in the background, merely opening Inspector, using a non-Chrome app or using already authorized Chrome does not trigger this recovery section. Background observation does not prompt for Automation. Opening system permission settings alone does not create a new request entry. If there is no entry, activate Chrome for real and then use Connect Chrome in the recovery section.

Denied/unavailable access, ambiguous page identity and a timeout are not success. That capture should not save a reference or copy a URL. Keep the correct standalone Chrome window in the foreground, check **Location:** for the authorized app copy, release the shortcut and retry. If the problem persists, quit duplicate copies, manually reopen the exact app and record the precise status. Neither a browser extension nor Chrome's “Allow JavaScript from Apple Events” setting is required.

## Page isolation

Create a mark in a safe tab A. When you switch to tab B, A's live marks should hide. Return to A and wait for a fresh page observation; undeleted marks should reappear. Different tabs have different identities even when their URLs match. Navigating to another page hides the original marks; returning to the original context restores them. Switching apps/windows also respects the original context.

Quitting and reopening Chrome changes its process identity, so old marks are not guaranteed to appear automatically in the new process. Valid historical references can still be copied or previewed in Inspector. The selected history item and explicit copy actions are evaluated separately from live marks' availability on the current page.

The complete tab/reload/back/window, first-permission, reopening and multi-system matrix has not been fully retested. Implementation and automated tests cover page isolation rules; those checks do not establish that the entire physical matrix passed. Use safe test content in feedback, and do not publish real tab URLs, titles or reference read-access capability URLs.
