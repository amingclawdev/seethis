# Mark and share

Complete [installation and permissions](INSTALL.md) first. The default Capture shortcut is **Option+A** and Delete is **Option+D**. If you changed them in **Open Settings…**, use the actual shortcuts shown in Inspector. Closing the settings browser page does not quit the app.

## Hold once to mark several regions

1. Hide private windows, notifications and account information. Place the pointer near safe content on the target display.
2. Hold **Option+A**, keeping both keys pressed. Press the left mouse button, drag around the first region, then release the mouse button.
3. Keep holding the shortcut. Move to another area and press, drag and release the left mouse button again. Moving without pressing the mouse does not draw, and separate regions should not be connected by lines.
4. Release A or Option to submit all regions from that hold as one reference. Releasing the mouse ends a region; releasing the shortcut ends a reference.
5. **Link copied — paste into chat** means the clipboard contains a plain-text JSON URL. The image continues preparing in the background. Completion does not automatically replace the clipboard with an image.

Empty movement, degenerate paths, Escape, switching apps/displays, an input interruption or reaching the maximum hold time can cancel capture. Release all keys before retrying. The entire selected display provides the reference context; marked regions are not a privacy crop.

## Inspector and reference history

Choose **Show Inspector…** from the menu. Inspector has a fixed **470 × 430-point** size and can be moved. The reference dropdown shows a name and time; a long name may be shortened while the time stays visible. A new capture selects the newest reference. Deleting the selected reference selects the newest remaining item. After hiding or closing Inspector, capturing or clicking a mark does not automatically reopen it.

- **Copy JSON URL** copies the selected reference's local read-access address.
- **Copy marked image** explicitly copies the annotated image of the entire selected display once it is ready. Inspect the whole image for private content first.
- **View details** opens local details and an image preview in the browser without changing the clipboard.
- **Delete** deletes the selected reference and all its regions, revokes local read access and removes assets. Check the status if cleanup fails.

Pending, failed, expired or deleted references may have no usable image. Wait for ready, or capture again. A normal click on a visible mark copies the same JSON URL again without taking a new screenshot. Hovering over a mark boundary makes it thicker. History operations do not require the corresponding mark to be visible on the current page.

## Share with an assistant

1. **A local tool or agent on the same Mac:** paste the JSON URL and explain which marks to inspect. SeeThis must be running and the reference must still be valid.
2. **A web chat, remote agent or another computer:** choose **Copy marked image** and paste the actual image. If the destination does not accept image pasting, use **File > New from Clipboard** in Preview, inspect the image, save it as PNG and attach it through the chat's image attachment control.
3. Before sending, confirm that an actual image attachment or preview is present. Remote services cannot read a `127.0.0.1` URL. Do not replace it with a public IP, use port forwarding or expose the settings service to share a reference.

SeeThis deletion and expiration cannot revoke remote attachments or clipboard copies.

## Delete several references

Return to the original app/window and, for Chrome, the original tab/page. Wait for the marks to appear. Hold **Option+D**, move to a mark boundary and click once with the left mouse button to delete the entire matching reference. Keep holding the shortcut to click other marks. Release D or Option to leave delete mode. At overlapping boundaries, each click deletes at most one reference, preferring the newest matching mark.

Local retention defaults to seven days and at most 500 searchable references. Change this in **Open Settings… > Reference access**. After reopening the app, valid history remains available in Inspector; the local port may change. Deleted or expired links do not restore the original image. See [privacy details](../PRIVACY.md).

**Feedback…** opens a draft in the default email app, addressed to `z5866318@gmail.com`. You edit, attach and send it. If the email app cannot open, use **Copy email address**. SeeThis does not automatically send feedback.
