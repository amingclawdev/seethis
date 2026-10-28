# Mark and share in SeeThis 0.1.5

Complete [installation and permissions](INSTALL.md) first. Defaults are **Option+A** for capture and **Option+D** for deletion. Settings and Inspector show customized shortcuts.

## Capture several regions

Hide private windows and notifications. Put the pointer on safe content on the target display. Hold Option+A, press and drag the left mouse button around one region, then release the mouse. Keep the shortcut held to draw more separate regions. Release A or Option to submit the shot. Empty movement does not create a reference.

**The screenshot contains the entire selected display; marks are not a privacy crop.** Drawing panels and the current Inspector are excluded, but other visible windows may be captured. Inspect the complete image before sharing.

A local JSON URL is copied immediately while the image prepares. Background completion does not replace the clipboard with an image. Open **Show Inspector…**, wait for ready, then choose **Copy marked image** to copy it explicitly.

Drawing and valid clicks renew the default five-minute idle period while the shortcut remains held; holding keys alone does not. Screenshot and saving work have separate technical deadlines. After cancellation or timeout, release all capture keys and begin a new gesture.

## Each shot keeps its scope

Inspector shows **Follows this page**, **Follows this window** or **Follows this app** near the current shot. Scope freezes from reliable context available when capture starts. Window scope follows that window; app scope can appear across windows of the same running app on the captured display. A later page observation does not rebind an earlier shot. Existing references keep their original binding.

Temporary unavailable context hides affected strokes, badges, notes and click targets without deleting saved data. A fresh matching context restores eligible marks. Chrome Automation is optional for basic capture; see [Chrome](CHROME.md). An explicit Connect Chrome permission action interrupts the hold and requires release followed by a fresh gesture.

## Append screenshots

Select a ready reference in Inspector and enable **Append screenshots**. Each later capture hold adds a separate shot to that reference, with its own image and frozen context. Mark numbers continue across shots, and existing numbers and notes stay attached to their marks.

Use Previous/Next to review shots. Copy marked image uses the selected shot; Copy JSON URL refers to the collection. Turn Append off before making a new reference. Selecting another reference also turns it off. Check Inspector's status after a failed capture or save rather than assuming it appended.

## Notes and history

Click a visible number or double-click a mark row to open its note. The editor remains bound to that mark while you inspect other content. Autosave preserves text. Use the note window's close button to save and copy the owning reference URL; clicking outside or pressing Escape does not finish-and-copy. A save/copy failure keeps the editor open with a retry action.

Inspector's dropdown selects references. New captures select the newest reference; deleting one selects the newest remaining item. Copy JSON URL, Copy marked image and View details are explicit actions. History preview/copy does not require live marks to be visible in the current app.

**Hide Marks** and **Show Marks** share one saved setting across references; hiding does not delete images, notes or links. Hovering a visible boundary thickens it. Hold Option+D and click a matching boundary to delete its entire reference; keep holding to delete others. Each click deletes at most one reference. Inspector's Delete also deletes the whole reference and its shots, revokes local read access and removes assets.

## Sharing

A `127.0.0.1` reference works only for a tool on the same Mac while SeeThis runs and the reference is valid. Web chats, remote agents and other computers need the actual image pasted or attached. Do not expose the local server through port forwarding. Deletion cannot retract attachments, clipboard copies or backups.

A single-shot link uses `delivery_schema:3`; appended collections use `delivery_schema:4` with ordered shots, per-shot images/source context and marks bound by `shot_id`. Inspector's selected preview does not change the collection returned by its link. See the [delivery contract](../tests/reference_flow_acceptance.md).

Retention defaults to seven days and at most 500 searchable references. Configure it in Open Settings… > Reference access. Feedback… opens an email draft; you decide whether to send it.

The user accepted the remaining checks on the frozen candidate, reporting one transient flicker followed by stable behavior. This is not a guarantee for every app, display or permission state. Test safe content and review [release limits](RELEASE.md).
