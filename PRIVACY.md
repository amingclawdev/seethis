# SeeThis privacy

SeeThis processes screen references locally. This version has no cloud sync, telemetry uploads or background screenshot-sharing endpoint. When you paste or attach an image in a conversation, or let a tool on the same Mac read a URL, that tool or service determines how it stores and processes the received content.

## Capture and permissions

- **Input Monitoring** observes global shortcuts, mouse input and interruptions; the native listener does not record unrelated keystroke content. **Screen Recording** captures the selected display. These permissions are checked separately.
- A reference stores the entire selected display as a static image, plus regions/paths, the source app/window, time and dimensions. Marked regions are not a privacy crop. Hide private windows and notifications before capturing, and inspect the whole image before sharing it.
- Chrome Automation uses Apple Events to read the foreground standalone Chrome tab ID and URL and derive a page identity digest. Public metadata and logs should avoid the original URL and tab token; screenshot pixels can still contain them.

## Storage, deletion and expiration

Data lives in `~/Library/Application Support/SeeThis`: `settings-v1.json` stores settings, and `references-v1` stores references and images. The defaults are a seven-day retention period and at most 500 searchable references. Change them in **Open Settings… > Reference access**. Retention starts when a reference is accepted. The app's pruning process performs cleanup; it does not keep running on a schedule after the app quits.

Deletion first records a tombstone, revokes local read access and removes assets. Image-free deleted or expired status records may remain. Cleanup failures can leave files behind, and the app reports the failure. This is not secure erasure and cannot retract backups, clipboard copies or attachments already sent. Removing the app keeps Application Support data; see [uninstalling and keeping data](docs/TROUBLESHOOTING.md#uninstalling-and-keeping-data) for manual removal. The complete uninstall and residual-data matrix has not been retested.

## Links and feedback

A reference URL grants read access: a program on the same Mac that knows the complete link can read the authorized content. The service binds to loopback `127.0.0.1`; remote assistants need an image you explicitly attach. Do not publish complete reference URLs, detail fragments or settings access addresses, or expose the local service through port forwarding.

The service has authentication, expiration/deletion handling and no-store/no-referrer policies. You still need to inspect screen content and consider the recipient. **Feedback…** only opens an email draft addressed to `z5866318@gmail.com`; it does not automatically send it or attach screenshots. You choose the content and whether to send it. See [security reporting](SECURITY.md).
