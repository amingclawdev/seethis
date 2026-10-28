# Google Chrome context

SeeThis supports standalone **Google Chrome**. Input Monitoring and Screen Recording are required. Chrome **Automation is optional**: without reliable authorized page information, a new shot can follow a reliable window or the running app on the selected display. Inspector shows the frozen scope beside the shot.

## Optional page association

Activate Chrome and choose **Connect Chrome** in the menu or Inspector. If macOS asks whether SeeThis may control Google Chrome, decide in the system dialog. This explicit authorization action interrupts a capture hold: release its keys completely and begin a fresh gesture. Grant completion never replays the old gesture.

**Retry Chrome** refreshes status without prompting. If access was denied, use **Automation Settings…** to change the Chrome switch yourself, then retry. Starting SeeThis, opening Inspector and passive observation do not request Automation. Page reads are not attempted without an available authorization grant. No browser extension or “Allow JavaScript from Apple Events” setting is required.

## What follows a page, window or app

A new shot uses reliable context already available when capture starts. It does not wait for a higher precision result. A shot that starts as window or app scope keeps that scope even if page information arrives later; a later new capture may use page scope.

- **Follows this page:** marks hide on other tabs/pages and return after a fresh matching observation.
- **Follows this window:** marks can remain across pages in that window.
- **Follows this app:** marks can appear across windows of the same running app on the captured display.

A failed or unavailable observation hides affected marks while retaining their data. It does not authorize showing stale page marks on another page. Fresh healthy contexts are judged independently. Old saved references retain their original binding rather than automatically expanding to app scope.

Chrome process replacement and closed windows can make historical live marks ineligible. Valid images and notes remain available in Inspector until deletion or expiration. Use safe content to check the scope and page return before sharing. Do not publish actual tab URLs, titles or local read-access links in bug reports.
