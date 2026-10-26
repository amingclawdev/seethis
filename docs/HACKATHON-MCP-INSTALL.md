# SeeThis reference runs — hackathon installation

This local demo connects SeeThis 0.1.6 references to Codex run history. The plugin
package is **0.4.1**. It supplies a stdio MCP server, a skill and four lifecycle
hooks. Python uses only its standard library; run metadata lives in local JSON.
There is no persistent plugin HTTP service or background daemon.

## Project and download source

After extracting the source ZIP, an example MCP/plugin project root is:

```text
~/Projects/seethis-runs-0.4.1
```

The entry point is `seethis_runs/mcp_server.py`; `.mcp.json` runs it using
`python3`, with the installed plugin root as its working directory. The companion source is distributed as a release asset. Use the accompanying `seethis-runs-0.4.1-source.zip` supplied by the
submitter and verify it against the accompanying `SHA256SUMS`. The source zip is
a plugin project, not an executable macOS app. The SeeThis demo app is a separate
delivery; use its own release manifest and checksum.

## Prerequisites

- macOS with Codex desktop and a signed-in Codex account. This demo was checked
  with bundled CLI `0.163.0-alpha.11.2`; other versions must expose native
  `plugin marketplace`, `plugin add`, `plugin remove` and `/hooks` support.
- Python 3.10 or later available as `python3` in Codex's execution environment.
- The separately supplied **SeeThis 0.1.6 local demo app**, with its required
  capture permissions. The App Store 0.1.5 app is not this integration build.
- A writable home directory. The plugin uses
  `~/.local/share/seethis-agent-demo/` independently of its installation cache.

Check `python3 --version` and `codex --version`. On the demo machine the native
CLI is `/Applications/ChatGPT.app/Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex`.
If `codex` is not on PATH, use the full CLI path supplied by your installation in
place of `codex` below; do not assume every installation uses the demo path.

## Install the local marketplace and plugin

Extract the source zip and change into the extracted `seethis-runs-0.4.1`
directory. Run this from that directory, not from its parent:

```sh
python3 - <<'PY'
import json, shutil
from pathlib import Path
root = Path.cwd()
market = root / 'build/marketplace'
plugin = market / 'plugins/seethis-runs'
plugin.mkdir(parents=True, exist_ok=True)
for name in ('.codex-plugin', 'seethis_runs', 'skills'):
    shutil.copytree(root / name, plugin / name, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
shutil.copy2(root / '.mcp.json', plugin / '.mcp.json')
catalog = market / '.agents/plugins/marketplace.json'
catalog.parent.mkdir(parents=True, exist_ok=True)
catalog.write_text(json.dumps({
    'name': 'seethis-agent-demo-local',
    'plugins': [{'name': 'seethis-runs',
                 'source': {'source': 'local', 'path': './plugins/seethis-runs'},
                 'policy': {'installation': 'AVAILABLE', 'authentication': 'ON_INSTALL'},
                 'category': 'Productivity'}]
}, indent=2))
print(market)
PY
codex plugin marketplace add "$PWD/build/marketplace" --json
codex plugin add seethis-runs@seethis-agent-demo-local --json
```

Keep the extracted source and marketplace directory available. Confirm native
installation reports version `0.4.1` and save its returned `installedPath`.
The observed demo path is
`~/.codex/plugins/cache/seethis-agent-demo-local/seethis-runs/0.4.1`;
use your returned path if it differs. Do not edit installed cache files.

For an existing installation, stage the new source first, then use native
`codex plugin remove seethis-runs@seethis-agent-demo-local --json` followed by the
`plugin add` command above. This preserves the independent run store. It may
remove the old helper path, so reconnect the bridge after every reinstall.

## Review the four hooks through Codex

Start `codex` in the source project directory. Open `/hooks`, or choose
**Review hooks** when Codex reports changed definitions. Open each of these event
entries and review only the hook whose source is
`seethis-runs@seethis-agent-demo-local`:

| Event | Purpose |
| --- | --- |
| UserPromptSubmit | Open a run when a reference is handed in |
| Stop | Infer stopped-without-end for a matching unfinished run |
| Interrupt | Record interruption of a matching unfinished run |
| SessionEnd | Infer interruption for remaining unfinished session runs |

Each event has one synchronous command with timeout **1 second**. Its unexpanded
source definition is:

```sh
{ [ -n "${PLUGIN_ROOT:-}" ] && python3 "${PLUGIN_ROOT}/seethis_runs/hooks.py"; } >/dev/null 2>&1 || :
```

Codex expands the helper path to the installed plugin root. Review that path and
use the native trust action for each event separately. The current CLI offers
`t` while viewing an event needing review. Confirm each becomes **Trusted** and
active. Installing or enabling a plugin does not establish trust; changed
commands require another review. Do not use a skip-trust option or edit trust
records. Leave unrelated hooks unchanged.

The shell wrapper handles missing Python or target failures before the handler
starts. The handler begins its own 0.8-second deadline before loading the store.
An error stops that invocation without a subsequent callback, state write or
retry, and silently exits zero so Codex can continue. Exit zero is fail-open
behavior, not a successful run report. Missing hook data stays missing.

## Connect the SeeThis 0.1.6 local demo

Open the separately delivered local demo app according to its package guidance.
Use the helper from the native installation, not from the extracted source:

```sh
python3 "$HOME/.codex/plugins/cache/seethis-agent-demo-local/seethis-runs/0.4.1/seethis_runs/bridge.py" --connect
```

Substitute your actual installed path if needed. This creates an owner-only
`bridge.json` next to `runs.json`. It binds the exact Python executable, helper
path and Python source digests. The app checks this binding before invoking a
bounded helper process per request. Reconnect after installation changes; no
listener needs to be started. A `connected:true` response verifies descriptor
creation, not an end-to-end reference or log rendering result.

In SeeThis Inspector, click **Reference runs…**. Keep the existing app running
while using its page. The app chooses its local port: use the URL it opens,
rather than copying a demo port. The page receives a separate ephemeral local
view capability. Closing/restarting SeeThis invalidates that URL; open it again
from Inspector. Do not publish this private URL or reference capabilities.

## Verify the installation

1. Check `plugin add` reported manifest version **0.4.1**. `/hooks` must show all
   four plugin definitions active and trusted, with a one-second timeout and the
   installed helper path. The native `hooks/list` app-server API provides the
   same metadata for an operator using that API; there is no public trust-update
   API in the checked CLI schema.
2. Check `--connect` reports `connected:true` and the 0.4.1 installed helper. To
   check the local descriptor without opening any run or log:

   ```sh
   python3 - <<'PY'
   import hashlib, json
   from pathlib import Path
   p = Path.home() / '.local/share/seethis-agent-demo/bridge.json'
   d = json.loads(p.read_text())
   print('helper:', d['helper'])
   print('sources_match:', all(Path(k).is_file() and
       hashlib.sha256(Path(k).read_bytes()).hexdigest() == v
       for k, v in d['files'].items()))
   PY
   ```

3. Start a **fresh Codex conversation** after installation. Confirm the plugin's
   MCP tools are available: `get_progress`, `run_end`, `confirm_takeover`, and
   `commit_handoff`. The server's MCP initialize compatibility version remains
   **0.4.0**; package 0.4.1 changes hook startup protection. Do not interpret that
   existing MCP version field as an installation failure.
4. For a live demonstration, create a harmless reference in SeeThis, save its
   note and hand its JSON link to that new conversation. Ask Codex to read
   `get_progress` before work and report `run_end` with `notes_revision` before
   finishing. Refresh Reference runs to inspect the run. Repeat in another
   conversation to demonstrate continuation. Actual successful run recording
   and browser display are separate acceptance checks, not implied by install.
5. Optional source regression: `python3 -m unittest discover -s tests -v`.
   The supplied suite uses synthetic temporary state. It does not require your
   original conversation logs.

Observed on the demo host: 55 source tests passed; independent hook QA detected
both reverted protections; native 0.4.1 installation matched source; four hooks
were individually trusted and read back enabled/trusted/timeout=1; the bridge
binding matched installed 0.4.1. The existing app's TCP port was reachable.
Fresh native lifecycle delivery, the watched browser workflow and the complete
two-run recording remain operator acceptance items. This is not an E2E PASS.

## Common installation problems

| Symptom | Action |
| --- | --- |
| `codex` is missing or has no `plugin` command | Use the desktop installation's supported CLI and verify its help/version. |
| No MCP tools in an existing conversation | Start a fresh conversation after native installation. Check Python availability and native plugin enable state. |
| Hooks show Modified or Untrusted | Review the four exact new definitions in native `/hooks`; old trust is not proof of new trust. |
| A run is missing after a hook error | Restore the correct installed target/Python and check trust. Fail-open intentionally does not retry or reconstruct old prompts. |
| `plugin_not_connected` or `plugin_source_changed` | Run the installed 0.4.1 helper with `--connect` again. Do not edit the descriptor or cache manually. |
| `plugin_bridge_unavailable_in_sandbox_build` | Use the separately delivered 0.1.6 local demo build, not the Store build. |
| Page link no longer works | Reopen Reference runs from the running app's Inspector. |
| Missing/truncated/unavailable log interval | Keep it unavailable; do not substitute another conversation file or claim completion. |

## Uninstall while preserving history

```sh
codex plugin remove seethis-runs@seethis-agent-demo-local --json
codex plugin marketplace remove seethis-agent-demo-local --json
```

This removes the native plugin/marketplace registration without deleting
`~/.local/share/seethis-agent-demo/` or original Codex sessions. The retained
bridge descriptor may then point to a removed helper; reconnect after a later
installation. Removing the plugin does not uninstall SeeThis. No `notify`, other
plugin, App Store 0.1.5 or original-log edits are part of this workflow.
