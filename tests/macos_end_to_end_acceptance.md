# Chrome capture-shortcut consent acceptance

This version adds consent intent to the physical, configured capture shortcut while Chrome is foreground. Existing Connect and Inspector recovery controls remain available. A denied or revoked permission follows normal macOS recovery; a repeated dialog is not promised.

Automated evidence uses local Carbon event objects, real production input/controller state, and injected external permission/foreground/physical snapshots. It does not launch SeeThis, post input, request real consent, inspect TCC, change user settings, or exercise a live dialog. Prior accepted human capture/tab/delete results are not repeated by these tests.

A user reported successful first-authorization acceptance of the development candidate. That report does not establish the full checklist below or live acceptance of the final production build. Automated and physical acceptance remain separate:

1. With Chrome foreground and Automation missing, press the effective capture shortcut (default Option+A). Consent may appear through the normal macOS policy. SeeThis should remain responsive; the interrupted hold should create no partial or phantom reference.
2. Grant or deny while the key remains held. Repeats, modifier release/repress, and returning focus should neither reopen authorization nor resume drawing. Release the original capture key and use a fresh gesture after grant.
3. Repeat with a customized effective binding, including immediate switch into Chrome. Only the exact configured physical chord should initiate authorization for the current Chrome process. A relaunch while the request is queued should abandon the obsolete request.
4. With granted Automation, non-Chrome foreground, a page that is not ready, or a provider timeout, capture should preserve ordinary behavior without an unintended consent request. Startup, Refresh/Retry, and opening Inspector should remain prompt-free.
5. Healthy/granted Chrome keeps recovery description and all three controls hidden and effective shortcut hints visible. Non-Chrome reference context does not display Chrome recovery status.

Automated results do not establish installation, physical system-dialog behavior or release approval.
