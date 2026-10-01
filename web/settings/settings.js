(() => {
  "use strict";
  const fragment = new URLSearchParams(location.hash.slice(1));
  const supplied = fragment.get("token");
  if (supplied) {
    sessionStorage.setItem("seethis.settings.capability", supplied);
    history.replaceState(null, "", location.pathname);
  }
  const capability = sessionStorage.getItem("seethis.settings.capability");
  const status = document.querySelector("#status");
  const form = document.querySelector("#settings");
  const number = id => Number(document.querySelector(`#${id}`).value);
  const checked = id => document.querySelector(`#${id}`).checked;
  const setNumber = (id, value) => { document.querySelector(`#${id}`).value = value; };

  const headers = () => ({"Authorization": `Bearer ${capability}`});
  const render = settings => {
    setNumber("shortcut_key_code", settings.shortcut_key_code);
    setNumber("delete_shortcut_key_code", settings.delete_shortcut_key_code);
    setNumber("maximum_hold_ms", settings.maximum_hold_ms);
    setNumber("crop_margin_points", settings.crop_margin_points);
    setNumber("mark_hit_radius_points", settings.mark_hit_radius_points);
    setNumber("reference_ttl_seconds", settings.reference_ttl_seconds);
    setNumber("maximum_visible_references", settings.maximum_visible_references);
    document.querySelector("#command").checked = Boolean(settings.shortcut_modifiers & 1);
    document.querySelector("#control").checked = Boolean(settings.shortcut_modifiers & 2);
    document.querySelector("#option").checked = Boolean(settings.shortcut_modifiers & 4);
    document.querySelector("#shift").checked = Boolean(settings.shortcut_modifiers & 8);
    document.querySelector("#delete_command").checked = Boolean(settings.delete_shortcut_modifiers & 1);
    document.querySelector("#delete_control").checked = Boolean(settings.delete_shortcut_modifiers & 2);
    document.querySelector("#delete_option").checked = Boolean(settings.delete_shortcut_modifiers & 4);
    document.querySelector("#delete_shift").checked = Boolean(settings.delete_shortcut_modifiers & 8);
  };
  const renderReadiness = readiness => {
    const input = document.querySelector("#input_readiness");
    input.textContent = `Input Monitoring: ${readiness.input_monitoring.state} (${readiness.input_monitoring.ready ? "ready" : "not ready"})`;
    input.className = readiness.input_monitoring.ready ? "ready" : "not-ready";
    document.querySelector("#input_guidance").textContent = readiness.input_monitoring.guidance;
    const screen = document.querySelector("#screen_readiness");
    screen.textContent = `Screen Recording: ${readiness.screen_recording.state} (${readiness.screen_recording.ready ? "ready" : "not ready"})`;
    screen.className = readiness.screen_recording.ready ? "ready" : "not-ready";
    document.querySelector("#screen_guidance").textContent = readiness.screen_recording.guidance;
    const deletion = document.querySelector("#delete_readiness");
    deletion.textContent = `Delete shortcut: ${readiness.delete_shortcut.state} (${readiness.delete_shortcut.ready ? "ready" : "not ready"})`;
    deletion.className = readiness.delete_shortcut.ready ? "ready" : "not-ready";
    document.querySelector("#delete_guidance").textContent = readiness.delete_shortcut.guidance;
    document.querySelector("#app_identity").textContent = `${readiness.app.name} (${readiness.app.bundle_identifier})`;
    document.querySelector("#app_location").textContent = readiness.app.path;
    document.querySelector("#update_policy").textContent = readiness.update_policy;
  };
  const loadReadiness = async () => {
    const response = await fetch("/v1/readiness", {headers: headers(), cache: "no-store"});
    if (!response.ok) throw new Error(`Readiness access failed (${response.status}).`);
    renderReadiness(await response.json());
  };
  const load = async () => {
    if (!capability) throw new Error("Open settings from the running SeeThis app.");
    const response = await fetch("/v1/settings", {headers: headers(), cache: "no-store"});
    if (!response.ok) throw new Error(`Settings access failed (${response.status}). Reopen settings from SeeThis.`);
    render(await response.json());
  };
  form.addEventListener("submit", async event => {
    event.preventDefault();
    status.textContent = "Saving…";
    const modifiers = (checked("command") ? 1 : 0) |
      (checked("control") ? 2 : 0) |
      (checked("option") ? 4 : 0) |
      (checked("shift") ? 8 : 0);
    const deleteModifiers = (checked("delete_command") ? 1 : 0) |
      (checked("delete_control") ? 2 : 0) |
      (checked("delete_option") ? 4 : 0) |
      (checked("delete_shift") ? 8 : 0);
    const settings = {
      schema: 2,
      shortcut_key_code: number("shortcut_key_code"),
      shortcut_modifiers: modifiers,
      delete_shortcut_key_code: number("delete_shortcut_key_code"),
      delete_shortcut_modifiers: deleteModifiers,
      maximum_hold_ms: number("maximum_hold_ms"),
      crop_margin_points: number("crop_margin_points"),
      mark_hit_radius_points: number("mark_hit_radius_points"),
      reference_ttl_seconds: number("reference_ttl_seconds"),
      maximum_visible_references: number("maximum_visible_references")
    };
    try {
      const response = await fetch("/v1/settings", {
        method: "PUT",
        headers: {...headers(), "Content-Type": "application/json"},
        body: JSON.stringify(settings)
      });
      if (!response.ok) throw new Error(`Save rejected (${response.status}).`);
      render(await response.json());
      status.textContent = "Settings saved. Permission readiness is reported separately above.";
      try {
        await loadReadiness();
      } catch (readinessError) {
        status.textContent = `Settings saved. Readiness refresh failed: ${readinessError.message}`;
      }
    } catch (error) {
      status.textContent = error.message;
    }
  });
  document.querySelector("#refresh_readiness").addEventListener("click", () => {
    loadReadiness().catch(error => { status.textContent = error.message; });
  });
  document.querySelector("#retry_input").addEventListener("click", async () => {
    status.textContent = "Requesting a bounded monitoring retry…";
    try {
      const response = await fetch("/v1/readiness/retry", {
        method: "POST",
        headers: {...headers(), "Content-Type": "application/json"},
        body: "{}"
      });
      if (!response.ok) throw new Error(`Retry rejected (${response.status}).`);
      status.textContent = "Retry requested. If readiness stays restart_required, quit and reopen the exact app shown above.";
      setTimeout(() => { loadReadiness().catch(error => { status.textContent = error.message; }); }, 750);
    } catch (error) {
      status.textContent = error.message;
    }
  });
  Promise.all([load(), loadReadiness()]).then(() => {
    status.textContent = "Connected to the running SeeThis app. Settings persistence and permission readiness are separate.";
  })
    .catch(error => { status.textContent = error.message; document.querySelectorAll("input,button").forEach(item => { item.disabled = true; }); });
})();
