#include "service/reference_server.h"
#ifdef SEETHIS_COMPILED_SETTINGS_ASSETS
#include "seethis/settings_assets.h"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <limits>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <sys/random.h>
#include <unistd.h>

#include "platform/platform.h"

namespace seethis::service {
namespace {

constexpr std::size_t kMaximumHeaderBytes = 16 * 1024;
constexpr std::size_t kMaximumBodyBytes = 16 * 1024;
constexpr std::size_t kMaximumAssetBytes = 256 * 1024;

constexpr std::string_view kReferenceViewerHtml = R"HTML(<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SeeThis reference</title>
<h1>SeeThis reference</h1>
<h2>Current local operation</h2>
<p id="status">Resolving local reference…</p>
<p>Readiness and Clipboard acknowledgements are current local operation facts;
they are not historical capture metadata.</p>
<h2>Historical capture metadata</h2>
<pre id="identity" aria-label="Historical capture metadata"></pre>
<section id="collection" aria-label="Captured shot history" hidden>
<label for="shot-select">Captured shot</label>
<select id="shot-select" aria-label="Captured shot"></select>
<p id="shot-position"></p>
<ol id="mark-notes" aria-label="Selected shot marks and notes"></ol>
</section>
<div id="image-stage"><canvas id="image" aria-label="SeeThis full-context reference with selected regions" hidden></canvas><canvas id="numbers" aria-hidden="true" hidden></canvas></div>
<p><a id="save" download="seethis-reference-context.png" hidden>Save annotated full-context PNG</a></p>
<p><a id="crop-save" download="seethis-reference-crop.png" hidden>Save selected crop PNG</a></p>
<p>Local Mac only; remote chat needs a separately exported PNG or an explicit bridge.</p>
<script src="/v1/reference-view.js"></script>
)HTML";

const std::string kReferenceViewerScript = std::string(R"JS((() => {
  const expectedGeometryVersion = ")JS")+
    std::string(core::ReferenceStore::kDisplayUnderlayGeometryVersion)+R"JS(";
  "use strict";
  const status = document.getElementById("status");
  const identity = document.getElementById("identity");
  const image = document.getElementById("image");
  const stage = document.getElementById("image-stage");
  const numbers = document.getElementById("numbers");
  const save = document.getElementById("save");
  const cropSave = document.getElementById("crop-save");
  const collectionUI = document.getElementById("collection");
  const shotSelect = document.getElementById("shot-select");
  const shotPosition = document.getElementById("shot-position");
  const markNotes = document.getElementById("mark-notes");
  const path = location.pathname.match(/^\/v1\/references\/([0-9a-f]{32})\/view$/);
  const initialHash = location.hash;
  const fragment = initialHash.match(/^#cap=([0-9a-f]{64})$/);
  const invalidFragment = initialHash && !fragment;
  const sessionKey = path ? `seethis-reference-capability:${path[1]}` : "";
  let capability = fragment ? fragment[1] : "";
  if (!capability && sessionKey) {
    try {
      const stored = sessionStorage.getItem(sessionKey);
      if (/^[0-9a-f]{64}$/.test(stored || "")) capability = stored;
    } catch (_) {}
  }
  let objectUrl = "";
  let downloadUrl = "";
  let cropDownloadUrl = "";
  const openedAt = performance.now();
  history.replaceState(null, "", location.pathname);

  let numberBinding = null;
  let disposed = false;
  let generation = 0;
  const resetNumbers = () => {
    numberBinding = null;
    numbers.dataset.layout="";
    numbers.hidden = true;
    numbers.width = numbers.height = 0;
  };
  const resetBundle = () => {
    ++generation;resetNumbers();image.hidden=true;
    image.width=image.height=0;save.hidden=cropSave.hidden=true;
    for(const value of [objectUrl,downloadUrl,cropDownloadUrl])if(value)URL.revokeObjectURL(value);
    objectUrl=downloadUrl=cropDownloadUrl="";
    save.removeAttribute?.("href");cropSave.removeAttribute?.("href");
    markNotes?.replaceChildren();
  };
  const fail = (message) => {
    resetBundle();
    image.hidden = save.hidden = cropSave.hidden = true;
    if(collectionUI)collectionUI.hidden=true;
    status.textContent = message;
    capability = "";
  };
  const historicalText = (value) =>
    typeof value === "string" && value.length > 0 ? value : "unavailable";
  const finiteNumber = (value) =>
    typeof value === "number" && Number.isFinite(value) ? value : null;
  const rectText = (value, label) => {
    if (!value || ![value.x, value.y, value.width, value.height]
        .every(Number.isFinite)) return `${label}: unavailable`;
    return `${label}: x=${value.x}, y=${value.y}, width=${value.width}, height=${value.height}`;
  };
  const timestampText = (value) => {
    if (typeof value !== "number" || !Number.isSafeInteger(value) || value <= 0)
      return "unavailable";
    const milliseconds = Math.floor(value / 1000);
    const micros = value % 1000;
    const date = new Date(milliseconds);
    if (!Number.isFinite(date.getTime())) return "unavailable";
    return `${date.toISOString().slice(0, -1)}${String(micros).padStart(3, "0")}Z`;
  };
  const displayLabel = (displayById, id) => {
    const display = displayById.get(id);
    const stableId = finiteNumber(id) === null ? "unavailable" : id;
    if (!display) return `display id=${stableId}, UUID=unavailable`;
    return `display id=${stableId}, UUID=${historicalText(display.uuid)}`;
  };
  const renderHistoricalMetadata = (metadata, id) => {
    const context = metadata && metadata.context;
    const timestamps = metadata && metadata.timestamps;
    const lines = [
      `Reference ID: ${id}`,
      "Historical capture facts (captured metadata only):",
      `Application name: ${historicalText(context && context.app_name)}`,
      `Window title: ${historicalText(context && context.window_title)}`,
      `Bundle identifier: ${historicalText(context && context.bundle_id)}`,
      `Context observed (UTC microseconds): ${timestampText(timestamps && timestamps.context_utc_us)}`,
      `Capture requested (UTC microseconds): ${timestampText(timestamps && timestamps.capture_requested_utc_us)}`,
      `Image completed (UTC microseconds): ${timestampText(timestamps && timestamps.image_completed_utc_us)}`,
      `Circle completed (UTC microseconds): ${timestampText(timestamps && timestamps.circle_completed_utc_us)}`,
    ];
    const displays = context && Array.isArray(context.displays) ? context.displays : [];
    const displayById = new Map(displays.map(display => [display && display.id, display]));
    lines.push(`Displays involved: ${displays.length || "unavailable"}`);
    displays.forEach((display, index) => {
      const stableId = finiteNumber(display && display.id);
      const scale = finiteNumber(display && display.scale);
      lines.push(`Display ${index + 1}: stable id=${stableId === null ? "unavailable" : stableId}; UUID=${historicalText(display && display.uuid)}`);
      lines.push(`  ${rectText(display && display.logical, "Logical bounds (AppKit global points, Y-up)")}`);
      lines.push(`  ${rectText(display && display.pixels, "Backing pixel bounds (top-left/Y-down)")}`);
      const pixels = display && display.pixels;
      lines.push(`  Pixel dimensions: ${pixels && Number.isFinite(pixels.width) && Number.isFinite(pixels.height) ? `${pixels.width}×${pixels.height}` : "unavailable"}; scale: ${scale === null ? "unavailable" : scale}`);
    });
    const selection = metadata && metadata.selection;
    const regions = selection && Array.isArray(selection.regions) ? selection.regions : [];
    lines.push(`Regions involved: ${regions.length || "unavailable"}`);
    regions.forEach((region, regionIndex) => {
      lines.push(`Region ${regionIndex + 1} (independent):`);
      lines.push(`  ${rectText(region && region.region, "Logical bounds (shared global AppKit logical points, Y-up)")}`);
      lines.push(`  ${rectText(region && region.crop_pixels, "Source crop pixels (top-left/Y-down)")}`);
      const subpaths = region && Array.isArray(region.subpaths) ? region.subpaths : null;
      const paths = subpaths || (region && Array.isArray(region.path) ? [region.path] : []);
      if (!paths.length) {
        lines.push("  Path: unavailable");
      } else {
        paths.forEach((pathPoints, pathIndex) => {
          lines.push(`  Contiguous subpath ${pathIndex + 1}:`);
          pathPoints.forEach((point, pointIndex) => {
            const displayId = point && point.display_id;
            const x = finiteNumber(point && point.x);
            const y = finiteNumber(point && point.y);
            const unit = historicalText(point && point.unit);
            lines.push(`    Path point ${pointIndex + 1}: ${displayLabel(displayById, displayId)}; ${unit}; display-local logical point x=${x === null ? "unavailable" : x}, y=${y === null ? "unavailable" : y}; backing scale=${finiteNumber(point && point.backing_scale) ?? "unavailable"}`);
          });
        });
      }
    });
    lines.push("Coordinate semantics: path points are display-local logical points; shared global AppKit logical points are Y-up; source-image pixels are top-left/Y-down.");
    identity.textContent = lines.join("\n");
  };
  const rememberCapability = () => {
    if (!fragment || !sessionKey) return;
    try { sessionStorage.setItem(sessionKey, capability); } catch (_) {}
  };
  const forgetCapability = () => {
    try { if (sessionKey) sessionStorage.removeItem(sessionKey); } catch (_) {}
  };
  const clearCredentialOnFailure = (response) => {
    if (response.status === 401 || response.status === 403) forgetCapability();
  };
  const readBounded = async (response, maximum) => {
    const reader = response.body && response.body.getReader();
    if (!reader) throw new Error("response streaming unavailable");
    const chunks = [];
    let size = 0;
    for (;;) {
      const result = await reader.read();
      if (result.done) break;
      size += result.value.byteLength;
      if (size > maximum) {
        await reader.cancel();
        throw new Error("response too large");
      }
      chunks.push(result.value);
    }
    const bytes = new Uint8Array(size);
    let offset = 0;
    for (const chunk of chunks) {
      bytes.set(chunk, offset);
      offset += chunk.byteLength;
    }
    return bytes;
  };
  const fetchLocal = async (url, maximum) => {
    const started = performance.now();
    const controller = new AbortController();
    let timer = 0;
    const deadline = new Promise((_, reject) => {
      timer = setTimeout(() => {
        controller.abort();
        reject(new Error("local fetch exceeded 2 seconds"));
      }, 2000);
    });
    try {
      const response = await Promise.race([fetch(url, {
        method: "GET",
        headers: {Authorization: `Bearer ${capability}`},
        credentials: "omit",
        redirect: "error",
        referrerPolicy: "no-referrer",
        signal: controller.signal
      }), deadline]);
      const bytes = await Promise.race([readBounded(response, maximum), deadline]);
      if (performance.now() - started > 2000)
        throw new Error("local fetch exceeded 2 seconds");
      return {response, bytes};
    } finally {
      clearTimeout(timer);
    }
  };
  const hex = (bytes) => Array.from(bytes, byte =>
    byte.toString(16).padStart(2, "0")).join("");
  const sameAssetUrl = (value, id, suffix) => {
    try {
      const url = new URL(value);
      return url.origin === location.origin &&
        url.pathname === `/v1/references/${id}/${suffix}` &&
        !url.search && !url.hash && !url.username && !url.password ? url : null;
    } catch (_) {
      return null;
    }
  };
  const underlayUrl = (value,id) => {
    const expected=`${location.origin}/v1/references/${id}/annotated.png?layers=strokes`;
    return typeof value==="string" && value===expected?new URL(value):null;
  };
  const sourceBinding = metadata => JSON.stringify([metadata.schema,metadata.reference_id,
    metadata.historical,metadata.capture?.source?.sha256,metadata.capture?.source?.width,
    metadata.capture?.source?.height,metadata.capture?.source?.url,metadata.selection]);
  const validateUnderlay = (descriptor,metadata,id) => {
    const source=metadata.capture?.source;
    if(!descriptor || descriptor.representation!=="strokes-only-v1" ||
       descriptor.geometry_version!==expectedGeometryVersion || descriptor.reference_id!==id ||
       descriptor.source_sha256!==source?.sha256 || !/^[0-9a-f]{64}$/.test(source?.sha256) ||
       !Number.isSafeInteger(source.width)||!Number.isSafeInteger(source.height)||
       source.width<1||source.height<1||descriptor.width!==source.width||descriptor.height!==source.height||
       !sameAssetUrl(source.url,id,"source.png")||!underlayUrl(descriptor.url,id)||!["pending","ready"].includes(descriptor.state)||
       (descriptor.state==="ready"?!/^[0-9a-f]{64}$/.test(descriptor.sha256):descriptor.sha256!==null))
      throw new Error("display underlay metadata mismatch");
    return descriptor;
  };
  const reportTiming = (name) => {
    const elapsed = Math.max(0, Math.round(performance.now() - openedAt));
    status.dataset[name] = String(elapsed);
    return elapsed;
  };
  const goneMessage = (response, fallback) => {
    try {
      const body = JSON.parse(new TextDecoder().decode(response.bytes));
      if (body.reference_id === path[1] && body.state === "deleted")
        return "reference was deleted from this Mac";
      if (body.reference_id === path[1] && body.state === "expired")
        return "reference has expired on this Mac";
    } catch (_) {}
    return fallback;
  };
  const failureMessage = (response, fallback) => {
    try {
      const body = JSON.parse(new TextDecoder().decode(response.bytes));
      if (body.reference_id === path[1] && body.state === "failed") {
        const code = historicalText(body.code);
        return code === "metadata_unavailable" ?
          "historical metadata unavailable on this Mac" :
          `reference capture failed: ${code}`;
      }
    } catch (_) {}
    return fallback;
  };

  // Collections are compact historical snapshots, not legacy capture metadata.
  // Only the selected shot is fetched; its server-owned identity and byte hash
  // must agree before its raster, download and note rows are published together.
  const renderCollection = async (snapshot, metadataUrl, id) => {
    const openingGeneration=generation;
    const rootPath=`/v1/references/${id}/view`;
    const onPage=()=>!disposed&&location.pathname===rootPath;
    const binding=value=>JSON.stringify([value.delivery_schema,value.reference_id,
      value.revision,value.notes_revision,value.lifecycle,value.shots,value.marks]);
    const shotUrl=shot=>{
      const expected=`${location.origin}/r/${id}${capability}/shots/${shot.shot_id}/annotated.png`;
      return shot.image?.url===expected?new URL(expected):null;
    };
    const validate = async value => {
      if(value?.delivery_schema!==4||value.reference_id!==id||value.state!=="ready"||
         value.lifecycle?.state!=="ready"||
         !Number.isSafeInteger(value.lifecycle.expires_utc_us)||
         value.lifecycle.expires_utc_us<=Date.now()*1000||
         !Array.isArray(value.shots)||value.shots.length<2||value.shots.length>64||
         !Number.isSafeInteger(value.revision)||value.revision!==value.shots.length||
         !Number.isSafeInteger(value.notes_revision)||value.notes_revision<0||
         !Array.isArray(value.marks)||value.marks.length<value.shots.length||value.marks.length>1024)
        throw new Error("collection identity mismatch");
      const shots=new Map();
      for(const [index,shot] of value.shots.entries()) {
        const asset=shot?.image;
        if(!/^[0-9a-f]{32}$/.test(shot?.shot_id)||shots.has(shot.shot_id)||
           (index===0&&shot.shot_id!==id)||asset?.representation!=="full-annotated-v1"||
           !/^[0-9a-f]{64}$/.test(asset?.source_sha256)||!shotUrl(shot)||
           !Number.isSafeInteger(asset.width)||!Number.isSafeInteger(asset.height)||
           asset.width<1||asset.height<1||asset.width>16384||asset.height>16384||
           asset.width*asset.height*4>128*1024*1024)
          throw new Error("collection shot identity mismatch");
        shots.set(shot.shot_id,{index,count:0});
      }
      const expectedIDs=await Promise.all(value.marks.map(async(_,slot)=>hex(new Uint8Array(
        await crypto.subtle.digest("SHA-256",new TextEncoder().encode(`${id}:${slot}`)))).slice(0,32)));
      let previous=-1;
      for(const [slot,mark] of value.marks.entries()) {
        const owner=shots.get(mark?.shot_id);
        if(!owner||owner.index<previous||mark.mark_id!==expectedIDs[slot]||
           mark.number!==slot+1||typeof mark.note!=="string")
          throw new Error("collection global mark identity mismatch");
        previous=owner.index;++owner.count;
      }
      if([...shots.values()].some(shot=>!shot.count))
        throw new Error("collection shot marks missing");
      return value;
    };
    await validate(snapshot);
    if(!onPage()||generation!==openingGeneration)return;
    const initialBinding=binding(snapshot);
    shotSelect.replaceChildren();
    snapshot.shots.forEach((shot,index)=>{
      const option=document.createElement("option");
      option.value=shot.shot_id;option.textContent=`Shot ${index+1} of ${snapshot.shots.length}`;
      shotSelect.appendChild(option);
    });
    collectionUI.hidden=false;
    const selectShot = async shotID => {
      const shot=snapshot.shots.find(value=>value.shot_id===shotID);
      if(!onPage()||!shot)return;
      resetBundle();identity.textContent="";
      const selectedGeneration=generation;
      const current=()=>onPage()&&generation===selectedGeneration&&shotSelect.value===shotID;
      let bitmap=null;
      try {
        status.textContent="Preparing captured shot…";
        const url=shotUrl(shot);
        const started=Date.now();
        let result;
        for(let attempt=0;attempt<40;++attempt) {
          result=await fetchLocal(url,192*1024*1024);
          if(!current())return;
          if(result.response.status!==202)break;
          const pending=JSON.parse(new TextDecoder().decode(result.bytes));
          if(pending.reference_id!==id||pending.shot_id!==shotID||pending.state!=="pending")
            throw new Error("captured shot pending identity mismatch");
          const retry=Number.isInteger(pending.retry_ms)?Math.max(20,Math.min(250,pending.retry_ms)):25;
          if(Date.now()-started+retry>30000)throw new Error("captured shot remained pending");
          await new Promise(resolve=>setTimeout(resolve,retry));
        }
        if(!current())return;
        clearCredentialOnFailure(result.response);
        if(result.response.status===410){forgetCapability();throw new Error(goneMessage(result,"reference unavailable"));}
        if(result.response.status!==200)throw new Error(`local shot service returned ${result.response.status}`);
        if(result.response.headers.get("content-type")!=="image/png"||
           result.response.headers.get("x-seethis-reference-id")!==id||
           result.response.headers.get("x-seethis-shot-id")!==shotID||
           result.response.headers.get("x-seethis-source-sha256")!==shot.image.source_sha256)
          throw new Error("captured shot response identity mismatch");
        const signature=[137,80,78,71,13,10,26,10],png=result.bytes;
        if(png.length<signature.length||!signature.every((value,index)=>png[index]===value))
          throw new Error("invalid captured shot PNG signature");
        const digest=hex(new Uint8Array(await crypto.subtle.digest("SHA-256",png)));
        if(!current())return;
        if(result.response.headers.get("x-seethis-png-sha256")!==digest||
           result.response.headers.get("etag")!==`"sha256-${digest}"`)
          throw new Error("captured shot image hash mismatch");
        const blob=new Blob([png],{type:"image/png"});
        bitmap=await createImageBitmap(blob);
        if(!current())return;
        if(bitmap.width!==shot.image.width||bitmap.height!==shot.image.height)
          throw new Error("decoded captured shot dimensions mismatch");
        const refreshed=await fetchLocal(metadataUrl,256*1024);
        if(!current())return;
        clearCredentialOnFailure(refreshed.response);
        if(refreshed.response.status===410){forgetCapability();throw new Error(goneMessage(refreshed,"reference unavailable"));}
        if(refreshed.response.status!==200||!refreshed.response.headers.get("content-type")?.startsWith("application/json"))
          throw new Error("collection refresh unavailable");
        const ready=await validate(JSON.parse(new TextDecoder().decode(refreshed.bytes)));
        if(!current())return;
        if(binding(ready)!==initialBinding)
          throw new Error("captured collection changed; reload to view its latest revision");
        const rows=snapshot.marks.filter(mark=>mark.shot_id===shotID).map(mark=>{
          const row=document.createElement("li");
          row.dataset.markId=mark.mark_id;row.dataset.number=String(mark.number);
          row.textContent=`#${mark.number} ${mark.note}`;return row;
        });
        image.width=bitmap.width;image.height=bitmap.height;
        const context=image.getContext("2d",{alpha:false});
        if(!context)throw new Error("canvas unavailable");
        context.drawImage(bitmap,0,0);
        downloadUrl=URL.createObjectURL(blob);
        save.href=downloadUrl;save.download="seethis-reference-context.png";
        save.textContent="Save annotated full-context PNG";
        markNotes.replaceChildren(...rows);
        const source=shot.source_context,index=snapshot.shots.indexOf(shot);
        identity.textContent=[`Reference ID: ${id}`,`Captured shot ID: ${shotID}`,
          `Collection revision: ${snapshot.revision}; notes revision: ${snapshot.notes_revision}`,
          `Application name: ${historicalText(source?.app_name)}`,
          `Window title: ${historicalText(source?.window_title)}`,
          `Bundle identifier: ${historicalText(source?.bundle_id)}`,
          `Context observed (UTC microseconds): ${timestampText(source?.context_observed_utc_us)}`,
          `Image completed (UTC microseconds): ${timestampText(source?.image_completed_utc_us)}`].join("\n");
        shotPosition.textContent=`Shot ${index+1} of ${snapshot.shots.length}`;
        image.hidden=save.hidden=false;
        status.textContent="Verified captured shot ready; global marks and notes are shown.";
        reportTiming("contextImageReadyMs");reportTiming("downloadReadyMs");
      } catch(error) {
        // Stale work owns only its local bitmap, never the newer view's bundle.
        if(current())fail(`Could not resolve local reference: ${error.message}`);
      } finally {bitmap?.close();}
    };
    shotSelect.addEventListener("change",()=>{void selectShot(shotSelect.value);});
    shotSelect.value=snapshot.shots[snapshot.shots.length-1].shot_id;
    await selectShot(shotSelect.value);
  };

  const segmentHits = (a, b, box) => {
    let low=0, high=1;
    for(const axis of ["x","y"]) {
      const delta=b[axis]-a[axis], start=a[axis];
      const minimum=box[axis], maximum=minimum+box[axis==="x"?"w":"h"];
      if(Math.abs(delta)<1e-9) {if(start<minimum||start>maximum)return false;}
      else {let p=(minimum-start)/delta,q=(maximum-start)/delta;
        if(p>q)[p,q]=[q,p];low=Math.max(low,p);high=Math.min(high,q);
        if(low>high)return false;}
    }
    return true;
  };
  const leaderEnd = (anchor,box) => ({x:Math.max(box.x,Math.min(box.x+box.w,anchor.x)),
    y:Math.max(box.y,Math.min(box.y+box.h,anchor.y))});
  const redrawNumbers = () => {
    numbers.hidden=true;numbers.dataset.layout="";
    if(disposed||!numberBinding||image.hidden)return;
    const view=stage.getBoundingClientRect(), rect=image.getBoundingClientRect();
    if(view.width<=0||view.height<=0||rect.width<=0||rect.height<=0)return;
    const scale=rect.width/image.width;
    const ratio=globalThis.devicePixelRatio||1;
    numbers.width=Math.ceil(view.width*ratio);numbers.height=Math.ceil(view.height*ratio);
    numbers.style.width=`${view.width}px`;numbers.style.height=`${view.height}px`;
    const ctx=numbers.getContext("2d");
    if(!ctx)throw new Error("number canvas unavailable");
    ctx.scale(ratio,ratio);ctx.font="bold 14px system-ui";
    const anchors=numberBinding.marks.map(mark=>({x:rect.left-view.left+mark.pos.x*scale,
      y:rect.top-view.top+mark.pos.y*rect.height/image.height}));
    const labels=[];
    const inflated=box=>({x:box.x-2,y:box.y-2,w:box.w+4,h:box.h+4});
    const intersects=(a,b)=>a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
    for(let i=0;i<numberBinding.marks.length;++i) {
      const mark=numberBinding.marks[i], w=Math.max(24,Math.ceil(ctx.measureText(String(mark.number)).width)+10), h=24;
      let best=null,score=Infinity;
      for(let y=2;y+h<=view.height-2;y+=4)for(let x=2;x+w<=view.width-2;x+=4) {
        const box={x,y,w,h}, end=leaderEnd(anchors[i],box);
        if(anchors.some(a=>a.x>=x-3&&a.x<=x+w+3&&a.y>=y-3&&a.y<=y+h+3))continue;
        if(labels.some(label=>intersects({x:box.x-6,y:box.y-6,w:box.w+12,h:box.h+12},label.box)))continue;
        const inside=intersects(box,{x:rect.left-view.left,y:rect.top-view.top,w:rect.width,h:rect.height});
        const value=Math.hypot(x+w/2-anchors[i].x,y+h/2-anchors[i].y)+(inside?40:0);
        if(value<score){score=value;best=box;}
      }
      if(!best)throw new Error("Preview is too crowded to place every number.");
      labels.push({number:mark.number,anchor:anchors[i],box:best});
    }
    for(let i=0;i<labels.length;++i) {
      const label=labels[i],nodes=[label.anchor,leaderEnd(label.anchor,label.box)];
      labels.forEach((other,j)=>{if(i===j)return;const b=other.box;
        for(const p of [{x:b.x-3,y:b.y-3},{x:b.x+b.w+3,y:b.y-3},
          {x:b.x+b.w+3,y:b.y+b.h+3},{x:b.x-3,y:b.y+b.h+3}])
          if(p.x>=0&&p.x<=view.width&&p.y>=0&&p.y<=view.height)nodes.push(p);
      });
      const distance=nodes.map(()=>Infinity),parent=nodes.map(()=>-1),done=new Set();distance[0]=0;
      for(let step=0;step<nodes.length;++step) {
        let next=-1;nodes.forEach((_,n)=>{if(!done.has(n)&&(next<0||distance[n]<distance[next]))next=n;});
        if(next<0||!Number.isFinite(distance[next]))break;done.add(next);if(next===1)break;
        nodes.forEach((node,n)=>{if(done.has(n))return;
          if(labels.some((other,j)=>i!==j&&segmentHits(nodes[next],node,
            {x:other.box.x-1,y:other.box.y-1,w:other.box.w+2,h:other.box.h+2})))return;
          const value=distance[next]+Math.hypot(node.x-nodes[next].x,node.y-nodes[next].y);
          if(value<distance[n]){distance[n]=value;parent[n]=next;}
        });
      }
      if(!Number.isFinite(distance[1]))throw new Error("Preview is too crowded to route every number.");
      label.leader=[];for(let n=1;n>=0;n=parent[n]){label.leader.push(nodes[n]);if(n===0)break;}
      label.leader.reverse();
    }
    for(const label of labels) {
      const end=leaderEnd(label.anchor,label.box);
      ctx.beginPath();ctx.moveTo(label.anchor.x,label.anchor.y);
      label.leader.slice(1).forEach(p=>ctx.lineTo(p.x,p.y));
      ctx.strokeStyle="white";ctx.lineWidth=4;ctx.stroke();
      ctx.strokeStyle="#ad0524";ctx.lineWidth=2;ctx.stroke();
      ctx.beginPath();ctx.arc(label.anchor.x,label.anchor.y,2,0,Math.PI*2);ctx.fillStyle="#ad0524";ctx.fill();
    }
    for(const {number,box} of labels) {
      ctx.fillStyle="#ad0524";ctx.fillRect(box.x,box.y,box.w,box.h);
      ctx.strokeStyle="white";ctx.lineWidth=1.5;ctx.strokeRect(box.x,box.y,box.w,box.h);
      ctx.fillStyle="white";ctx.textAlign="center";ctx.textBaseline="middle";
      ctx.fillText(String(number),box.x+box.w/2,box.y+box.h/2);
    }
    numbers.hidden=false;
    // Geometry is diagnostic state; the verified image and download never use it.
    numbers.dataset.layout=JSON.stringify(labels);
  };
  const refreshNumbers = () => {try{redrawNumbers();}catch(error){
    numbers.hidden=true;status.textContent=`${error.message} Enlarge the view to read all numbers. Verified PNG remains available.`;
  }};
  addEventListener("resize",refreshNumbers);
  addEventListener("pagehide",()=>{disposed=true;resetBundle();});

  addEventListener("unload", () => {
    disposed=true;resetBundle();
    capability = "";
    if (objectUrl) URL.revokeObjectURL(objectUrl);
    if (downloadUrl) URL.revokeObjectURL(downloadUrl);
    if (cropDownloadUrl) URL.revokeObjectURL(cropDownloadUrl);
  });
  if (!path || invalidFragment || !capability) {
    if (path && invalidFragment) forgetCapability();
    fail("Invalid local reference link.");
    return;
  }
  if (!globalThis.crypto || !crypto.subtle) {
    fail("This browser cannot verify the reference PNG (WebCrypto unavailable).");
    return;
  }
  rememberCapability();

  (async () => {
    const requestGeneration=generation;
    const isCurrent=()=>!disposed && generation===requestGeneration && location.pathname===`/v1/references/${path[1]}/view`;
    const abandonStale = () => {if(isCurrent())return false;resetBundle();return true;};
    const id = path[1];
    const metadataUrl = new URL(`/v1/references/${id}`, location.origin);
    let metadataResult;
    const pollingStarted = Date.now();
    for (let attempt = 0; attempt < 20; ++attempt) {
      metadataResult = await fetchLocal(metadataUrl, 256 * 1024);
      if (metadataResult.response.status !== 202) break;
      const pending = JSON.parse(new TextDecoder().decode(metadataResult.bytes));
      if (pending.reference_id !== id ||
          (pending.state !== "pending" && pending.state !== "indexing"))
        throw new Error("invalid pending state");
      status.textContent = pending.state === "indexing" ?
        "Indexing local reference…" : "Waiting for reference metadata…";
      const retry = Number.isInteger(pending.retry_ms) ?
        Math.max(20, Math.min(250, pending.retry_ms)) : 25;
      const delay = Math.min(250, retry * (2 ** Math.min(attempt, 3)));
      if (Date.now() - pollingStarted + delay > 30000)
        throw new Error("reference remained pending for 30 seconds");
      await new Promise(resolve => setTimeout(resolve, delay));
    }
    if (metadataResult.response.status === 410) {
      forgetCapability();
      throw new Error(goneMessage(metadataResult,
        "reference is no longer available on this Mac"));
    }
    clearCredentialOnFailure(metadataResult.response);
    if (!metadataResult.response.ok)
      throw new Error(failureMessage(metadataResult,
        `local service returned ${metadataResult.response.status}`));
    if (!metadataResult.response.headers.get("content-type")?.startsWith("application/json"))
      throw new Error("invalid metadata type");
    const metadata = JSON.parse(new TextDecoder().decode(metadataResult.bytes));
    if(metadata.delivery_schema===4) {
      if(!isCurrent())return;
      await renderCollection(metadata,metadataUrl,id);return;
    }
    if (metadata.reference_id !== id || metadata.historical !== true ||
        (metadata.schema !== 1 && metadata.schema !== 2) ||
        !metadata.capture || !metadata.capture.crop)
      throw new Error("metadata identity mismatch");
    renderHistoricalMetadata(metadata, id);
    reportTiming("metadataReadyMs");
    const cropMetadata = metadata.capture.crop;
    const legacyDigest = typeof cropMetadata.sha256 === "string" ?
      cropMetadata.sha256 : "";
    const pixelIntegrity = cropMetadata.pixel_integrity;
    if (legacyDigest && !/^[0-9a-f]{64}$/.test(legacyDigest))
      throw new Error("invalid legacy crop digest");
    if (!legacyDigest && (!pixelIntegrity ||
        pixelIntegrity.contract !== "rgba8-sha256-v1" ||
        !/^[0-9a-f]{64}$/.test(pixelIntegrity.sha256)))
      throw new Error("invalid crop integrity contract");
    const primaryIsSource = metadata.schema === 2;
    const initialBinding=sourceBinding(metadata);
    let assetMetadata = primaryIsSource ? validateUnderlay(metadata.capture.display_underlay,metadata,id) : cropMetadata;
    if (!assetMetadata || (primaryIsSource ?
        !["pending", "ready"].includes(assetMetadata.state) ||
          (assetMetadata.state === "ready" &&
            !/^[0-9a-f]{64}$/.test(assetMetadata.sha256)) :
        !/^[0-9a-f]{64}$/.test(assetMetadata.sha256)))
      throw new Error("invalid primary image digest");
    const assetUrl = primaryIsSource ? underlayUrl(assetMetadata.url,id) : sameAssetUrl(assetMetadata.url,id,"crop.png");
    if (!assetUrl) throw new Error("invalid primary image URL");
    let assetResult;
    const assetStarted = Date.now();
    for (let attempt = 0; attempt < 40; ++attempt) {
      assetResult = await fetchLocal(assetUrl, 192 * 1024 * 1024);
      if (assetResult.response.status !== 202) break;
      const pending = JSON.parse(new TextDecoder().decode(assetResult.bytes));
      if (pending.reference_id !== id ||
          (pending.state !== "deriving" && pending.state !== "queued"))
        throw new Error("invalid crop derivation state");
      status.textContent = pending.state === "queued" ?
        "Reference image is queued…" : "Preparing reference image…";
      const retry = Number.isInteger(pending.retry_ms) ?
        Math.max(20, Math.min(250, pending.retry_ms)) : 25;
      const delay = Math.min(250, retry * (2 ** Math.min(attempt, 3)));
      if (Date.now() - assetStarted + delay > 30000)
        throw new Error("reference image remained pending for 30 seconds");
      await new Promise(resolve => setTimeout(resolve, delay));
    }
    if (assetResult.response.status === 410) {
      forgetCapability();
      throw new Error(goneMessage(assetResult,
        "reference is no longer available on this Mac"));
    }
    clearCredentialOnFailure(assetResult.response);
    if (!assetResult.response.ok)
      throw new Error(`local image service returned ${assetResult.response.status}`);
    if (assetResult.response.headers.get("content-type") !== "image/png")
      throw new Error("invalid image type");
    if (primaryIsSource) {
      // Derivation may have completed after the first metadata response, whose
      // annotated digest is null while pending. Bind the returned PNG to ready
      // metadata rather than accepting an absent or stale integrity contract.
      const readyResult = await fetchLocal(metadataUrl, 256 * 1024);
      if (readyResult.response.status === 410) {
        forgetCapability();
        throw new Error(goneMessage(readyResult,
          "reference is no longer available on this Mac"));
      }
      clearCredentialOnFailure(readyResult.response);
      if (!readyResult.response.ok)
        throw new Error(failureMessage(readyResult,
          `local service returned ${readyResult.response.status}`));
      if (!readyResult.response.headers.get("content-type")?.startsWith("application/json"))
        throw new Error("invalid metadata type");
      const ready = JSON.parse(new TextDecoder().decode(readyResult.bytes));
      const underlay=validateUnderlay(ready.capture?.display_underlay,ready,id);
      if(sourceBinding(ready)!==initialBinding || ready.schema!==2 ||
         underlay.state!=="ready" || underlay.url!==assetUrl.href ||
         (assetMetadata.state==="ready" && underlay.sha256!==assetMetadata.sha256))
        throw new Error("display underlay refresh mismatch");
      assetMetadata=underlay;
    }
    const png = assetResult.bytes;
    const signature = [137, 80, 78, 71, 13, 10, 26, 10];
    const verifyCropPixels = async (bytes, expected) => {
      const bitmap = await createImageBitmap(new Blob([bytes],
        {type: "image/png"}));
      if(abandonStale()){bitmap.close();return;}
      if (bitmap.width !== expected.width || bitmap.height !== expected.height) {
        bitmap.close();
        throw new Error("decoded crop dimensions mismatch");
      }
      const target = typeof OffscreenCanvas === "function" ?
        new OffscreenCanvas(bitmap.width, bitmap.height) :
        Object.assign(document.createElement("canvas"), {
          width: bitmap.width, height: bitmap.height});
      const targetContext = target.getContext("2d", {willReadFrequently: true});
      if (!targetContext) {
        bitmap.close();
        throw new Error("crop canvas unavailable");
      }
      targetContext.drawImage(bitmap, 0, 0);
      const rgba = targetContext.getImageData(0, 0,
        bitmap.width, bitmap.height).data;
      bitmap.close();
      const pixelDigest = hex(new Uint8Array(
        await crypto.subtle.digest("SHA-256", rgba)));
      if (!expected.pixel_integrity ||
          expected.pixel_integrity.contract !== "rgba8-sha256-v1" ||
          pixelDigest !== expected.pixel_integrity.sha256)
        throw new Error("crop pixel integrity mismatch");
    };
    if (png.length < signature.length ||
        !signature.every((value, index) => png[index] === value))
      throw new Error("invalid PNG signature");
    const digest = hex(new Uint8Array(await crypto.subtle.digest("SHA-256", png)));
    const declaredDigest = assetResult.response.headers.get("x-seethis-png-sha256") || "";
    if (!/^[0-9a-f]{64}$/.test(declaredDigest) || digest !== declaredDigest ||
        assetResult.response.headers.get("etag") !== `"sha256-${declaredDigest}"` ||
        declaredDigest !== assetMetadata.sha256)
      throw new Error("image hash mismatch");
    if(abandonStale())return;
    const verifiedPng = new Blob([png], {type: "image/png"});
    objectUrl = URL.createObjectURL(verifiedPng);
    const bitmap = await createImageBitmap(verifiedPng);
    if(abandonStale()){bitmap.close();return;}
    if (bitmap.width !== assetMetadata.width || bitmap.height !== assetMetadata.height) {
      bitmap.close();throw new Error("decoded image dimensions mismatch");
    }
    image.width = bitmap.width;
    image.height = bitmap.height;
    const context = image.getContext("2d", {alpha: false});
    if (!context) {bitmap.close();throw new Error("canvas unavailable");}
    context.drawImage(bitmap, 0, 0);
    bitmap.close();
    if(primaryIsSource) {
      const marksResult=await fetchLocal(new URL(`/r/${id}${capability}/metadata`,location.origin),256*1024);
      clearCredentialOnFailure(marksResult.response);
      if(marksResult.response.status===410)throw new Error(goneMessage(marksResult,"reference is no longer available on this Mac"));
      if(!marksResult.response.ok||!marksResult.response.headers.get("content-type")?.startsWith("application/json"))
        throw new Error("number metadata unavailable");
      const compact=JSON.parse(new TextDecoder().decode(marksResult.bytes));
      const regions=metadata.selection?.regions;
      if(compact.reference_id!==id||compact.delivery_schema!==2||compact.state!=="ready"||
         !Array.isArray(regions)||regions.length<1||!Array.isArray(compact.marks)||
         compact.marks.length!==regions.length||compact.screenshot?.sha256!==metadata.capture.source.sha256||
         compact.screenshot.width!==image.width||compact.screenshot.height!==image.height)
        throw new Error("number metadata identity mismatch");
      const expectedIDs=await Promise.all(regions.map(async(_,slot)=>hex(new Uint8Array(
        await crypto.subtle.digest("SHA-256",new TextEncoder().encode(`${id}:${slot}`)))).slice(0,32)));
      const seen=new Set();
      const marks=compact.marks.map(mark=>{
        const slot=expectedIDs.indexOf(mark.mark_id);
        if(slot<0||seen.has(mark.mark_id)||!Number.isSafeInteger(mark.number)||mark.number!==slot+1||
           !Number.isFinite(mark.pos?.x)||!Number.isFinite(mark.pos?.y)||mark.pos.x<0||mark.pos.y<0||
           mark.pos.x>image.width||mark.pos.y>image.height)
          throw new Error("number anchor or immutable slot invalid");
        seen.add(mark.mark_id);
        return Object.freeze({mark_id:mark.mark_id,number:mark.number,pos:Object.freeze({x:mark.pos.x,y:mark.pos.y})});
      });
      if(abandonStale())return;
      numberBinding=Object.freeze({referenceID:id,sourceSHA:metadata.capture.source.sha256,
        digest:assetMetadata.sha256,geometryVersion:expectedGeometryVersion,marks:Object.freeze(marks)});
      stage.style.position="relative";stage.style.minHeight="180px";
      stage.style.display="flex";stage.style.alignItems="center";stage.style.justifyContent="center";
      image.style.maxWidth="100%";image.style.maxHeight="70vh";
      image.style.width="auto";image.style.height="auto";
      numbers.style.position="absolute";numbers.style.left="0";numbers.style.top="0";
      numbers.style.pointerEvents="none";
    }
    if(abandonStale())return;
    image.hidden = true;
    reportTiming(primaryIsSource ? "contextImageReadyMs" : "cropImageReadyMs");
    status.textContent = primaryIsSource ?
      "Verified full-context image ready; freehand annotations are shown." :
      "Verified local crop ready.";
    let exported=primaryIsSource?verifiedPng:await new Promise((resolve,reject)=>
      image.toBlob(blob=>blob?resolve(blob):reject(new Error("PNG download unavailable")),"image/png"));
    if(primaryIsSource) {
      const annotated=metadata.capture.annotated;
      const url=sameAssetUrl(annotated?.url,id,"annotated.png");
      if(!url||!["pending","ready"].includes(annotated.state)||
         (annotated.state==="ready"?!/^[0-9a-f]{64}$/.test(annotated.sha256):annotated.sha256!==null))
        throw new Error("invalid numbered Save descriptor");
      let result;
      const started=Date.now();
      for(let attempt=0;attempt<40;++attempt) {
        result=await fetchLocal(url,192*1024*1024);
        if(result.response.status!==202)break;
        const pending=JSON.parse(new TextDecoder().decode(result.bytes));
        if(pending.reference_id!==id||!["queued","deriving"].includes(pending.state))
          throw new Error("invalid numbered Save pending state");
        if(Date.now()-started>30000)throw new Error("numbered Save remained pending");
        await new Promise(resolve=>setTimeout(resolve,25));
      }
      clearCredentialOnFailure(result.response);
      if(result.response.status===410){forgetCapability();throw new Error(goneMessage(result,"reference unavailable"));}
      if(result.response.status!==200||result.response.headers.get("content-type")!=="image/png")
        throw new Error("numbered Save unavailable");
      const readyResult=await fetchLocal(metadataUrl,256*1024);
      clearCredentialOnFailure(readyResult.response);
      if(readyResult.response.status!==200||!readyResult.response.headers.get("content-type")?.startsWith("application/json"))
        throw new Error("numbered Save metadata unavailable");
      const ready=JSON.parse(new TextDecoder().decode(readyResult.bytes));
      const finalUnderlay=validateUnderlay(ready.capture?.display_underlay,ready,id);
      if(finalUnderlay.state!=="ready" || finalUnderlay.sha256!==assetMetadata.sha256)
        throw new Error("display underlay changed during Save verification");
      const bound=ready.capture?.annotated;
      const digest=hex(new Uint8Array(await crypto.subtle.digest("SHA-256",result.bytes)));
      if(sourceBinding(ready)!==initialBinding || !bound||bound.state!=="ready"||
         bound.width!==image.width||bound.height!==image.height||
         sameAssetUrl(bound.url,id,"annotated.png")?.href!==url.href||
         !/^[0-9a-f]{64}$/.test(bound.sha256)||bound.sha256!==digest||
         (annotated.state==="ready"&&annotated.sha256!==digest)||
         result.response.headers.get("x-seethis-png-sha256")!==digest||
         result.response.headers.get("etag")!==`"sha256-${digest}"`)
        throw new Error("numbered Save integrity mismatch");
      exported=new Blob([result.bytes],{type:"image/png"});
      const decoded=await createImageBitmap(exported);
      if(abandonStale()){decoded.close();return;}
      const dimensionsMatch=decoded.width===image.width&&decoded.height===image.height;
      decoded.close();if(!dimensionsMatch)throw new Error("numbered Save dimensions mismatch");
    }
    if(abandonStale())return;
    downloadUrl = URL.createObjectURL(exported);
    save.href = downloadUrl;
    save.download = primaryIsSource ? "seethis-reference-context.png" :
      "seethis-reference.png";
    save.textContent = primaryIsSource ? "Save annotated full-context PNG" :
      "Save crop PNG";
    image.hidden=false;save.hidden = false;
    reportTiming("downloadReadyMs");
    if(primaryIsSource)refreshNumbers();
    if (!primaryIsSource) {
      capability = "";
    } else {
      cropSave.hidden = false;
      cropSave.addEventListener("click", async (event) => {
        if (cropDownloadUrl) return;
        event.preventDefault();
        cropSave.hidden = true;
        try {
          status.textContent = "Preparing selected crop…";
          const cropUrl = sameAssetUrl(cropMetadata.url, id, "crop.png");
          if (!cropUrl) throw new Error("invalid crop URL");
          let cropResult;
          const cropStarted = performance.now();
          for (let attempt = 0; attempt < 40; ++attempt) {
            cropResult = await fetchLocal(cropUrl, 192 * 1024 * 1024);
            if (cropResult.response.status !== 202) break;
            const pending = JSON.parse(new TextDecoder().decode(cropResult.bytes));
            if (pending.reference_id !== id ||
                (pending.state !== "deriving" && pending.state !== "queued"))
              throw new Error("invalid crop derivation state");
            status.textContent = pending.state === "queued" ?
              "Selected crop is queued…" : "Preparing selected crop…";
            const retry = Number.isInteger(pending.retry_ms) ?
              Math.max(20, Math.min(250, pending.retry_ms)) : 25;
            const delay = Math.min(250, retry * (2 ** Math.min(attempt, 3)));
            if (performance.now() - cropStarted + delay > 30000)
              throw new Error("selected crop remained pending for 30 seconds");
            await new Promise(resolve => setTimeout(resolve, delay));
          }
          if (cropResult.response.status === 410) {
            forgetCapability();
            throw new Error(goneMessage(cropResult,
              "reference is no longer available on this Mac"));
          }
          clearCredentialOnFailure(cropResult.response);
          if (!cropResult.response.ok)
            throw new Error(`local image service returned ${cropResult.response.status}`);
          if (cropResult.response.headers.get("content-type") !== "image/png")
            throw new Error("invalid crop type");
          const cropPng = cropResult.bytes;
          if (cropPng.length < signature.length ||
              !signature.every((value, index) => cropPng[index] === value))
            throw new Error("invalid crop PNG signature");
          const cropDigest = hex(new Uint8Array(
            await crypto.subtle.digest("SHA-256", cropPng)));
          const declaredCropDigest = cropResult.response.headers.get(
            "x-seethis-png-sha256") || "";
          if (!/^[0-9a-f]{64}$/.test(declaredCropDigest) ||
              cropDigest !== declaredCropDigest ||
              cropResult.response.headers.get("etag") !==
                `"sha256-${declaredCropDigest}"`)
            throw new Error("crop hash mismatch");
          if (legacyDigest && cropDigest !== legacyDigest)
            throw new Error("crop hash mismatch");
          if (!legacyDigest) await verifyCropPixels(cropPng, cropMetadata);
          if(abandonStale())return;
          cropDownloadUrl = URL.createObjectURL(new Blob([cropPng],
            {type: "image/png"}));
          cropSave.href = cropDownloadUrl;
          cropSave.hidden = false;
          cropSave.textContent = "Save selected crop PNG";
          reportTiming("cropReadyMs");
          status.textContent = "Verified selected crop ready.";
          capability = "";
        } catch (error) {
          cropSave.hidden = false;
          fail(`Could not resolve selected crop: ${error.message}`);
        }
      });
    }
  })().catch(error => fail(`Could not resolve local reference: ${error.message}`));
})();
)JS";

struct Request {
  std::string method;
  std::string target;
  std::map<std::string, std::string> headers;
  std::string body;
};

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string Trim(std::string value) {
  while (!value.empty() &&
         (value.front() == ' ' || value.front() == '\t'))
    value.erase(value.begin());
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t'))
    value.pop_back();
  return value;
}

bool SafeId(std::string_view id) {
  return id.size() == 32 &&
         std::all_of(id.begin(), id.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool LowerHex(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

std::string AgentPath(std::string_view id, std::string_view capability) {
  return "/r/" + std::string(id) + std::string(capability);
}

bool ConstantTimeEqual(std::string_view first, std::string_view second) {
  std::size_t difference = first.size() ^ second.size();
  const std::size_t length = std::max(first.size(), second.size());
  for (std::size_t index = 0; index < length; ++index) {
    const unsigned char a = index < first.size() ? first[index] : 0;
    const unsigned char b = index < second.size() ? second[index] : 0;
    difference |= a ^ b;
  }
  return difference == 0;
}

std::string RandomCapability() {
  std::array<std::uint8_t, 32> bytes{};
  if (getentropy(bytes.data(), bytes.size()) != 0)
    throw std::runtime_error("secure randomness unavailable");
  return core::Sha256(core::Bytes(bytes.begin(), bytes.end()));
}

std::string Escape(std::string_view value) {
  std::ostringstream result;
  for (unsigned char character : value) {
    switch (character) {
      case '\\': result << "\\\\"; break;
      case '"': result << "\\\""; break;
      case '\b': result << "\\b"; break;
      case '\f': result << "\\f"; break;
      case '\n': result << "\\n"; break;
      case '\r': result << "\\r"; break;
      case '\t': result << "\\t"; break;
      default:
        if (character < 0x20) {
          result << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(character) << std::dec;
        } else {
          result << character;
        }
    }
  }
  return result.str();
}

std::string RectJson(const core::Rect& rectangle) {
  std::ostringstream out;
  out << "{\"x\":" << rectangle.x << ",\"y\":" << rectangle.y
      << ",\"width\":" << rectangle.width << ",\"height\":"
      << rectangle.height << '}';
  return out.str();
}

std::string TransformJson(const core::Transform& transform) {
  std::ostringstream out;
  out << "{\"a\":" << transform.a << ",\"d\":" << transform.d
      << ",\"tx\":" << transform.tx << ",\"ty\":" << transform.ty
      << '}';
  return out.str();
}

std::string GestureMarkId(const core::Reference& reference,
                          std::size_t region_index) {
  return core::StableMarkId(reference.id,region_index);
}

// Project only frozen, bounded metadata. Detailed retrieval keeps its original
// captured snapshot; display availability does not affect capture eligibility.
std::string SourceContextJson(const core::Reference& reference) {
  const auto source=core::ProjectSourceContext(reference);
  std::ostringstream out;
  out << "{\"version\":1";
  const auto text=[&](const char* name,const std::optional<std::string>& value,
                      core::SourceFieldAvailability availability) {
    out << ",\"" << name << "\":";
    if(value)out << '\"' << Escape(*value) << '\"';
    else out << "null";
    out << ",\"" << name << "_availability\":\""
        << core::SourceFieldAvailabilityName(availability) << '\"';
  };
  const auto number=[&](const char* name,const auto& value) {
    out << ",\"" << name << "\":";
    if(value)out << *value;
    else out << "null";
  };
  text("app_name",source.app_name,source.app_name_availability);
  text("bundle_id",source.bundle_id,source.bundle_id_availability);
  number("window_id",source.window_id);
  text("window_title",source.window_title,source.window_title_availability);
  number("context_observed_utc_us",source.context_observed_utc_us);
  number("image_completed_utc_us",source.image_completed_utc_us);
  text("page_url",source.page_url,source.page_url_availability);
  number("page_url_observed_utc_us",source.page_url_observed_utc_us);
  out << '}';
  return out.str();
}

// Visibility is a global preference snapshot, not current app/window/page eligibility.
// Historical per-mark bits remain dormant in their unchanged notes sidecars.
std::string CompactReferenceJson(const core::Reference& reference,
                                 std::string_view agent_base,
                                 core::ReferenceStore& store,
                                 const core::MarkNotesSnapshot& notes,
                                 const core::GlobalVisibilitySnapshot& visibility,
                                 std::int64_t expires_utc_us) {
  std::ostringstream out;
  out << "{\"delivery_schema\":2,\"reference_id\":\"" << reference.id
      << "\",\"state\":\"ready\",\"lifecycle\":{\"state\":\"ready\","
      << "\"expires_utc_us\":" << expires_utc_us << "},\"screenshot\":{"
      << "\"url\":\"" << agent_base << "/source.png\",\"sha256\":\""
      << reference.source.sha256 << "\",\"width\":" << reference.source.width
      << ",\"height\":" << reference.source.height
      << "},\"source_context\":" << SourceContextJson(reference)
      << ",\"global_visibility\":\"" << visibility.name() << "\""
      << ",\"notes_revision\":" << notes.revision << ",\"marks\":[";
  for(std::size_t index=0;index<notes.marks.size();++index) {
    if(index)out<<',';
    const auto& mark=notes.marks[index];
    const auto& bounds=mark.delivery_bounds;
    const auto asset=store.RequestReadyMark(reference.id,mark.slot);
    const auto& anchor=mark.anchor;
    const auto display=std::find_if(reference.context.displays.begin(),
        reference.context.displays.end(),[&](const core::FrozenDisplay& item) {
          return item.id==anchor.display_id;
        });
    const double px=display==reference.context.displays.end()?bounds.x:
        reference.global_to_source.a*(display->logical.x+anchor.position.x)+
        reference.global_to_source.tx;
    const double py=display==reference.context.displays.end()?bounds.y:
        reference.global_to_source.d*(display->logical.y+anchor.position.y)+
        reference.global_to_source.ty;
    out << "{\"mark_id\":\"" << mark.id << "\",\"number\":" << mark.number
        << ",\"note\":\"" << Escape(mark.note) << "\",\"visible\":"
        << (visibility.permits_presentation()?"true":"false") << ",\"revision\":" << mark.revision
        << ",\"anchor\":{\"display_id\":" << anchor.display_id
        << ",\"x\":" << anchor.position.x << ",\"y\":"
        << anchor.position.y << "},\"pos\":{\"x\":"
        << std::setprecision(std::numeric_limits<double>::max_digits10) << px
        << ",\"y\":" << py << std::setprecision(6) << "},\"image\":{\"url\":\""
        << agent_base << "/marks/" << mark.id << ".png\",\"width\":"
        << bounds.width << ",\"height\":" << bounds.height
        << ",\"bounds\":" << RectJson(bounds)
        << ",\"state\":\""
        << (asset.state==core::ReadyAssetState::kReady?"ready":
            asset.state==core::ReadyAssetState::kPending?"pending":"failed")
        << "\",\"sha256\":";
    if(asset.state==core::ReadyAssetState::kReady&&
       !asset.byte_sha256.empty())out << '"' << asset.byte_sha256 << '"';
    else out << "null";
    out << ",\"code\":";
    if(asset.state==core::ReadyAssetState::kFailed)
      out << '"' << Escape(asset.code) << '"';
    else out << "null";
    out << "}}";
  }
  out << "]}\n";
  return out.str();
}

// A multi-shot root is always delivered as a collection, independent of any UI preview.
std::string CollectionJson(const core::Reference& root,std::string_view base,
    const core::ReferenceCollection& collection,
    std::int64_t expires) {
  std::ostringstream out;
  out << "{\"delivery_schema\":4,\"reference_id\":\"" << root.id
      << "\",\"revision\":" << collection.revision << ",\"state\":\"ready\",\"lifecycle\":{\"state\":\"ready\",\"expires_utc_us\":"
      << expires << "},\"notes_revision\":" << collection.notes_revision << ",\"shots\":[";
  bool first=true;
  for(const auto& shot:collection.shots) {
    if(!first)out << ',';first=false;const auto& ref=*shot.metadata;
    out << "{\"shot_id\":\"" << shot.id << "\",\"image\":{\"representation\":\"full-annotated-v1\",\"url\":\""
        << base << "/shots/" << shot.id << "/annotated.png\",\"source_sha256\":\""
        << ref.source.sha256 << "\",\"width\":" << ref.source.width
        << ",\"height\":" << ref.source.height << "},\"source_context\":" << SourceContextJson(ref) << '}';
  }
  out << "],\"marks\":[";first=true;
  for(const auto& shot:collection.shots) {
    const auto count=core::EffectiveRegions(*shot.metadata).size();
    for(std::size_t local=0;local<count;++local) {
      if(!first)out << ',';first=false;
      const auto slot=shot.first_mark_slot+local;
      out << "{\"mark_id\":\"" << core::StableMarkId(root.id,slot) << "\",\"number\":" << slot+1
          << ",\"note\":\"" << Escape(collection.notes[slot]) << "\",\"shot_id\":\"" << shot.id << "\"}";
    }
  }
  out << "]}\n";return out.str();
}

// Schema 3 is the default agent handoff. Only the canonical full numbered PNG
// is requested here; legacy crops and geometry remain explicit /metadata reads.
std::string SingleImageReferenceJson(const core::Reference& reference,
                                     std::string_view agent_base,
                                     const core::MarkNotesSnapshot& notes,
                                     const core::ReadyAssetResult& image,
                                     std::int64_t expires_utc_us) {
  const bool ready=image.state==core::ReadyAssetState::kReady && image.bytes &&
      !image.bytes->empty() && !image.byte_sha256.empty();
  const bool pending=image.state==core::ReadyAssetState::kPending;
  const bool unavailable=!ready&&!pending&&
      (image.state==core::ReadyAssetState::kReady || image.code.empty() ||
       image.code=="asset_unavailable");
  const char* state=ready?"ready":pending?"pending":unavailable?"unavailable":"failed";
  std::ostringstream out;
  out << "{\"delivery_schema\":3,\"reference_schema\":" << reference.schema
      << ",\"reference_id\":\"" << reference.id << "\",\"state\":\"" << state
      << "\",\"lifecycle\":{\"state\":\"ready\",\"expires_utc_us\":"
      << expires_utc_us << "},\"image\":{\"representation\":\"full-annotated-v1\","
      << "\"reference_id\":\"" << reference.id << "\",\"url\":\"" << agent_base
      << "/annotated.png\",\"state\":\"" << state << "\",\"sha256\":";
  if(ready)out << '\"' << image.byte_sha256 << '\"';
  else out << "null";
  out << ",\"source_sha256\":\"" << reference.source.sha256
      << "\",\"width\":" << reference.source.width
      << ",\"height\":" << reference.source.height << ",\"code\":";
  if(!ready)
    out << '\"' << Escape(unavailable?"asset_unavailable":image.code) << '\"';
  else out << "null";
  out << "},\"source_context\":" << SourceContextJson(reference)
      << ",\"notes_revision\":" << notes.revision << ",\"marks\":[";
  bool first=true;
  for(const auto& mark:notes.marks) {
    if(!first)out << ',';
    first=false;
    out << "{\"mark_id\":\"" << mark.id << "\",\"number\":" << mark.number
        << ",\"note\":\"" << Escape(mark.note) << "\"}";
  }
  out << "],\"details_url\":\"" << agent_base << "/details\"";
  if(pending)out << ",\"retry_ms\":25";
  out << "}\n";
  return out.str();
}

std::string ReferenceJson(const core::Reference& reference,
                          std::string_view base,
                          std::string_view agent_base,
                          const core::ReadyAssetResult& annotated,
                          const core::ReadyAssetResult& crop,
                          std::int64_t expires_utc_us,
                          const core::ReadyAssetResult* underlay=nullptr) {
  std::ostringstream out;
  out << "{\"schema\":" << reference.schema << ",\"reference_id\":\""
      << reference.id << "\",\"id\":\"" << reference.id
      << "\",\"state\":\"ready\",\"lifecycle\":{\"state\":\"ready\","
      << "\"expires_utc_us\":" << expires_utc_us << "},\"source_url\":\""
      << agent_base << "/source.png\",\"source_sha256\":\""
      << reference.source.sha256 << "\",\"crop_url\":\"" << agent_base
      << "/crop.png\",\"crop_sha256\":";
  const std::string& crop_digest = crop.state == core::ReadyAssetState::kReady &&
                                           !crop.byte_sha256.empty()
                                       ? crop.byte_sha256 : reference.crop.sha256;
  if (crop_digest.empty()) out << "null";
  else out << '"' << crop_digest << '"';
  out << ",\"crop_state\":\""
      << (crop.state == core::ReadyAssetState::kReady ? "ready" :
          crop.state == core::ReadyAssetState::kPending ? "pending" : "unavailable")
      << "\",\"annotated_url\":\"" << agent_base
      << "/annotated.png\",\"annotated_sha256\":";
  if (annotated.state == core::ReadyAssetState::kReady &&
      !annotated.byte_sha256.empty())
    out << '"' << annotated.byte_sha256 << '"';
  else out << "null";
  out << ",\"mark_id\":\"" << reference.mark_id
      << "\",\"historical\":true,\"timestamps\":{\"context_utc_us\":"
      << reference.context.observed.utc_us << ",\"capture_requested_utc_us\":"
      << reference.requested.utc_us << ",\"image_completed_utc_us\":"
      << reference.image_completed.utc_us << ",\"circle_completed_utc_us\":"
      << reference.circle_completed.utc_us << "},\"context\":{\"pid\":"
      << reference.context.pid << ",\"window_pid\":"
      << reference.context.window_pid << ",\"window_id\":"
      << reference.context.window_id << ",\"app_name\":\""
      << Escape(reference.context.app_name) << "\",\"bundle_id\":\""
      << Escape(reference.context.bundle_id) << "\",\"executable\":\""
      << Escape(reference.context.executable) << "\",\"window_title\":\""
      << Escape(reference.context.window_title) << "\",\"selection_method\":\""
      << Escape(reference.context.selection_method)
      << "\",\"window_layer\":" << reference.context.window_layer
      << ",\"window\":" << RectJson(reference.context.window)
      << ",\"quartz_to_appkit_top\":"
      << reference.context.quartz_to_appkit_top
      << ",\"exclusion_method\":\""
      << Escape(reference.context.exclusion_method)
      << "\",\"excluded_window_ids\":[";
  for (std::size_t index = 0;
       index < reference.context.excluded_window_ids.size(); ++index) {
    if (index) out << ',';
    out << reference.context.excluded_window_ids[index];
  }
  out << "],\"displays\":[";
  for (std::size_t index = 0; index < reference.context.displays.size(); ++index) {
    if (index) out << ',';
    const auto& display = reference.context.displays[index];
    out << "{\"uuid\":\"" << Escape(display.uuid) << "\",\"id\":"
        << display.id << ",\"scale\":" << display.scale
        << ",\"logical\":" << RectJson(display.logical)
        << ",\"pixels\":" << RectJson(display.pixels)
        << ",\"logical_to_pixels\":"
        << TransformJson(display.logical_to_pixels) << '}';
  }
  out << "]},\"selection\":{\"region\":" << RectJson(reference.region)
      << ",\"crop_pixels\":" << RectJson(reference.crop_pixels)
      << ",\"crop_margin_points\":" << reference.crop_margin
      << ",\"global_to_source\":"
      << TransformJson(reference.global_to_source) << ",\"regions\":[";
  const auto regions=core::EffectiveRegions(reference);
  for(std::size_t region_index=0;region_index<regions.size();++region_index) {
    if(region_index)out<<',';
    const auto& region=regions[region_index];
    out<<"{\"region\":"<<RectJson(region.region)
       <<",\"crop_pixels\":"<<RectJson(region.crop_pixels)<<",\"path\":[";
    for(std::size_t index=0;index<region.path.size();++index) {
      if(index)out<<',';
      const auto& point=region.path[index];
      out<<"{\"display_id\":"<<point.display_id
         <<",\"unit\":\"logical_points\",\"x\":"<<point.position.x
         <<",\"y\":"<<point.position.y<<",\"backing_scale\":"
         <<point.backing_scale<<'}';
    }
    out<<"]";
    if (!region.subpaths.empty()) {
      out << ",\"subpaths\":[";
      for (std::size_t path_index = 0; path_index < region.subpaths.size(); ++path_index) {
        if (path_index) out << ',';
        out << '[';
        const auto& path = region.subpaths[path_index];
        for (std::size_t index = 0; index < path.size(); ++index) {
          if (index) out << ',';
          const auto& point = path[index];
          out << "{\"display_id\":" << point.display_id
              << ",\"unit\":\"logical_points\",\"x\":" << point.position.x
              << ",\"y\":" << point.position.y << ",\"backing_scale\":"
              << point.backing_scale << '}';
        }
        out << ']';
      }
      out << ']';
    }
    out << "}";
  }
  out << "],\"path\":[";
  for (std::size_t index = 0; index < reference.path.size(); ++index) {
    if (index) out << ',';
    const auto& point = reference.path[index];
    out << "{\"display_id\":" << point.display_id
        << ",\"unit\":\"logical_points\",\"x\":" << point.position.x
        << ",\"y\":" << point.position.y << ",\"backing_scale\":"
        << point.backing_scale << '}';
  }
  out << "]},\"capture\":{\"clock\":\"" << Escape(reference.clock)
      << "\",\"api\":\"" << Escape(reference.capture_api)
      << "\",\"provenance\":\"" << Escape(reference.provenance)
      << "\",\"source\":{\"width\":" << reference.source.width
      << ",\"height\":" << reference.source.height << ",\"sha256\":\""
      << reference.source.sha256 << "\",\"encoding\":\""
      << Escape(reference.source.encoding) << "\",\"url\":\"" << base
      << "/source.png\"},\"crop\":{\"width\":" << reference.crop.width
      << ",\"height\":" << reference.crop.height << ",\"sha256\":";
  if(reference.crop.sha256.empty())out<<"null";
  else out<<'\"'<<reference.crop.sha256<<'\"';
  out << ",\"encoding\":\"" << Escape(reference.crop.encoding)
      << "\",\"pixel_integrity\":";
  if(reference.crop.pixel_hash_contract.empty())out<<"null";
  else out<<"{\"contract\":\""<<Escape(reference.crop.pixel_hash_contract)
          <<"\",\"sha256\":\""<<reference.crop.pixel_sha256<<"\"}";
  out << ",\"url\":\"" << base
      << "/crop.png\"},\"annotated\":{\"width\":"
      << reference.source.width << ",\"height\":"
      << reference.source.height << ",\"url\":\"" << base
      << "/annotated.png\",\"state\":\""
      << (annotated.state==core::ReadyAssetState::kReady?"ready":
          annotated.state==core::ReadyAssetState::kPending?"pending":"failed")
      << "\",\"sha256\":";
  if(annotated.state==core::ReadyAssetState::kReady&&
     !annotated.byte_sha256.empty())
    out<<'\"'<<annotated.byte_sha256<<'\"';
  else out<<"null";
  out << "}";
  if(reference.schema==2 && underlay) {
    out << ",\"display_underlay\":{\"representation\":\"strokes-only-v1\",\"geometry_version\":\""
        << core::ReferenceStore::kDisplayUnderlayGeometryVersion
        << "\",\"reference_id\":\"" << reference.id << "\",\"source_sha256\":\""
        << reference.source.sha256 << "\",\"width\":" << reference.source.width
        << ",\"height\":" << reference.source.height << ",\"url\":\"" << base
        << "/annotated.png?layers=strokes\",\"state\":\""
        << (underlay->state==core::ReadyAssetState::kReady?"ready":
            underlay->state==core::ReadyAssetState::kPending?"pending":"failed")
        << "\",\"sha256\":";
    if(underlay->state==core::ReadyAssetState::kReady&&!underlay->byte_sha256.empty())
      out << '"' << underlay->byte_sha256 << '"';
    else out << "null";
    out << "}";
  }
  out << ",\"record_url\":";
  if(reference.schema==1)out<<'\"'<<base<<"/record\"";
  else out<<"null";
  out<<"}}\n";
  return out.str();
}

std::string ReadinessJson(
    const platform::PermissionReadinessState& readiness) {
  const auto input = readiness.input_monitor.load();
  const auto screen = readiness.screen_recording.load();
  const auto deletion = readiness.delete_shortcut.load();
  std::ostringstream out;
  out << "{\"schema\":2,\"input_monitoring\":{\"state\":\""
      << platform::InputMonitorStateName(input) << "\",\"ready\":"
      << (platform::InputMonitorReady(input) ? "true" : "false")
      << ",\"guidance\":\"" << Escape(platform::InputMonitorGuidance(input))
      << "\"},\"screen_recording\":{\"state\":\""
      << platform::ScreenRecordingStateName(screen) << "\",\"ready\":"
      << (screen == platform::ScreenRecordingState::kReady ? "true" : "false")
      << ",\"guidance\":\"" << platform::ScreenRecordingGuidance(screen)
      << "\"},\"delete_shortcut\":{\"state\":\""
      << platform::DeleteShortcutStateName(deletion) << "\",\"ready\":"
      << (platform::DeleteShortcutReady(deletion) ? "true" : "false")
      << ",\"guidance\":\""
      << Escape(platform::DeleteShortcutGuidance(deletion))
      << "\"},\"app\":{\"name\":\"" << Escape(readiness.app_name)
      << "\",\"bundle_identifier\":\""
      << Escape(readiness.bundle_identifier) << "\",\"path\":\""
      << Escape(readiness.bundle_path)
      << "\"},\"update_policy\":\"Prefer a stable signing identity. For an ad-hoc rebuild, reauthorize this exact app when its code requirement changes; a stable path or bundle identifier alone does not preserve a cdhash-bound grant.\"}\n";
  return out.str();
}

void SendAll(int descriptor, std::string_view bytes) {
  while (!bytes.empty()) {
    const auto count = send(descriptor, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    if (count <= 0) return;
    bytes.remove_prefix(static_cast<std::size_t>(count));
  }
}

void Respond(int descriptor, int status, std::string_view reason,
             std::string_view type, std::string_view body,
             std::string_view extra_headers = {}) {
  std::ostringstream header;
  header << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "X-Content-Type-Options: nosniff\r\n"
         << "Referrer-Policy: no-referrer\r\n"
         << "Connection: close\r\n" << extra_headers << "\r\n";
  SendAll(descriptor, header.str());
  SendAll(descriptor, body);
}

void Error(int descriptor, int status, std::string_view reason,
           std::string_view code) {
  const std::string body = "{\"error\":\"" + std::string(code) + "\"}\n";
  Respond(descriptor, status, reason, "application/json; charset=utf-8", body,
          status == 401 ? "WWW-Authenticate: Bearer\r\n" : "");
}

void ExpiredReference(int descriptor, std::string_view id) {
  const std::string body = "{\"reference_id\":\"" + std::string(id) +
      "\",\"state\":\"expired\",\"error\":\"expired_reference\"}\n";
  Respond(descriptor, 410, "Gone", "application/json; charset=utf-8", body);
}

std::optional<Request> ReadRequest(int descriptor, std::string* failure) {
  timeval timeout{2, 0};
  setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  std::string bytes;
  std::array<char, 4096> buffer{};
  std::size_t boundary = std::string::npos;
  while ((boundary = bytes.find("\r\n\r\n")) == std::string::npos) {
    if (bytes.size() >= kMaximumHeaderBytes) {
      *failure = "headers_too_large";
      return {};
    }
    const auto count = recv(descriptor, buffer.data(), buffer.size(), 0);
    if (count <= 0) {
      *failure = "incomplete_request";
      return {};
    }
    bytes.append(buffer.data(), static_cast<std::size_t>(count));
  }
  if (boundary > kMaximumHeaderBytes) {
    *failure = "headers_too_large";
    return {};
  }
  for (std::size_t index = 0; index < boundary + 4; ++index) {
    if ((bytes[index] == '\n' && (index == 0 || bytes[index - 1] != '\r')) ||
        (bytes[index] == '\r' &&
         (index + 1 >= bytes.size() || bytes[index + 1] != '\n'))) {
      *failure = "malformed_line_ending";
      return {};
    }
  }
  Request request;
  std::istringstream headers(bytes.substr(0, boundary));
  std::string line;
  if (!std::getline(headers, line)) {
    *failure = "malformed_request_line";
    return {};
  }
  if (!line.empty() && line.back() == '\r') line.pop_back();
  std::istringstream first(line);
  std::string version, trailing;
  if (!(first >> request.method >> request.target >> version) || first >> trailing ||
      version != "HTTP/1.1" || request.method.empty() || request.target.empty()) {
    *failure = "malformed_request_line";
    return {};
  }
  while (std::getline(headers, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon == std::string::npos || colon == 0 ||
        std::isspace(static_cast<unsigned char>(line.front()))) {
      *failure = "malformed_header";
      return {};
    }
    const std::string name = Lower(line.substr(0, colon));
    if (request.headers.contains(name)) {
      *failure = "duplicate_header";
      return {};
    }
    request.headers.emplace(name, Trim(line.substr(colon + 1)));
  }
  if (request.headers.contains("transfer-encoding")) {
    *failure = "transfer_encoding_not_supported";
    return {};
  }
  std::size_t content_length = 0;
  if (auto it = request.headers.find("content-length");
      it != request.headers.end()) {
    if (it->second.empty() ||
        !std::all_of(it->second.begin(), it->second.end(),
                     [](unsigned char byte) { return std::isdigit(byte) != 0; })) {
      *failure = "invalid_content_length";
      return {};
    }
    try {
      content_length = std::stoull(it->second);
    } catch (...) {
      *failure = "invalid_content_length";
      return {};
    }
  }
  if (content_length > kMaximumBodyBytes) {
    *failure = "body_too_large";
    return {};
  }
  request.body = bytes.substr(boundary + 4);
  if (request.body.size() > content_length) {
    *failure = "unexpected_body";
    return {};
  }
  while (request.body.size() < content_length) {
    const auto count = recv(descriptor, buffer.data(),
                            std::min(buffer.size(), content_length - request.body.size()), 0);
    if (count <= 0) {
      *failure = "incomplete_body";
      return {};
    }
    request.body.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return request;
}

bool LoopbackPeer(int descriptor) {
  sockaddr_storage peer{};
  socklen_t size = sizeof(peer);
  if (getpeername(descriptor, reinterpret_cast<sockaddr*>(&peer), &size) != 0)
    return false;
  if (peer.ss_family != AF_INET) return false;
  const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&peer);
  return (ntohl(ipv4->sin_addr.s_addr) >> 24) == 127;
}

std::string Bearer(const Request& request) {
  auto found = request.headers.find("authorization");
  if (found == request.headers.end() || found->second.size() <= 7 ||
      found->second.substr(0, 7) != "Bearer ")
    return {};
  return found->second.substr(7);
}

#ifndef SEETHIS_COMPILED_SETTINGS_ASSETS
std::optional<std::string> ReadAsset(const std::filesystem::path& path) {
  try {
    if (!std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) > kMaximumAssetBytes)
      return {};
    std::ifstream input(path, std::ios::binary);
    std::string contents(static_cast<std::size_t>(std::filesystem::file_size(path)), '\0');
    if (!input.read(contents.data(), static_cast<std::streamsize>(contents.size())))
      return {};
    return contents;
  } catch (...) {
    return {};
  }
}
#endif

std::optional<std::string> SettingsAsset(const std::filesystem::path& root,
                                       bool javascript) {
#ifdef SEETHIS_COMPILED_SETTINGS_ASSETS
  (void)root;
  using namespace settings_assets;
  return javascript
      ? std::string(reinterpret_cast<const char*>(kSettingsJSBytes), kSettingsJSLength)
      : std::string(reinterpret_cast<const char*>(kIndexHTMLBytes), kIndexHTMLLength);
#else
  return ReadAsset(root / (javascript ? "settings.js" : "index.html"));
#endif
}

}  // namespace

ReferenceServer::ReferenceServer(core::ReferenceStore& references,
                                 core::SettingsStore& settings,
                                 std::filesystem::path settings_web_root,
                                 std::shared_ptr<platform::PermissionReadinessState> readiness)
    : references_(references),
      settings_(settings),
      settings_web_root_(std::move(settings_web_root)),
      readiness_(std::move(readiness)),
      settings_capability_(RandomCapability()) {}

ReferenceServer::~ReferenceServer() { Stop(); }

bool ReferenceServer::Start(std::string* error) {
  if (listener_ >= 0) return true;
  const auto retention = settings_.Get();
  references_.SetRetentionPolicy(retention.reference_ttl_seconds,
                                 retention.maximum_visible_references);
  listener_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listener_ < 0) {
    if (error) *error = "socket creation failed";
    return false;
  }
  int reuse = 1;
  setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(listener_, 16) != 0) {
    if (error) *error = "loopback bind/listen failed";
    close(listener_);
    listener_ = -1;
    return false;
  }
  socklen_t size = sizeof(address);
  if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
    if (error) *error = "bound port lookup failed";
    close(listener_);
    listener_ = -1;
    return false;
  }
  port_ = ntohs(address.sin_port);
  stopping_ = false;
  for (int index = 0; index < 4; ++index) {
    client_workers_.emplace_back([this] {
      for (;;) {
        int client = -1;
        {
          std::unique_lock lock(clients_mutex_);
          clients_ready_.wait(lock, [&] { return stopping_ || !clients_.empty(); });
          if (clients_.empty()) {
            if (stopping_) return;
            continue;
          }
          client = clients_.front();
          clients_.pop_front();
        }
        Handle(client);
        close(client);
      }
    });
  }
  thread_ = std::thread(&ReferenceServer::Serve, this);
  return true;
}

void ReferenceServer::Stop() {
  stopping_ = true;
  if (listener_ >= 0) shutdown(listener_, SHUT_RDWR);
  if (thread_.joinable()) thread_.join();
  clients_ready_.notify_all();
  for (auto& worker : client_workers_)
    if (worker.joinable()) worker.join();
  client_workers_.clear();
  {
    std::lock_guard lock(clients_mutex_);
    for (int client : clients_) close(client);
    clients_.clear();
  }
  if (listener_ >= 0) close(listener_);
  listener_ = -1;
  port_ = 0;
}

std::string ReferenceServer::base_url() const {
  return "http://127.0.0.1:" + std::to_string(port_);
}

std::string ReferenceServer::settings_url() const {
  return base_url() + "/settings/#token=" + settings_capability_;
}

std::string ReferenceServer::ClipboardText(
    const core::StoredReference& reference) const {
  return ClipboardText(reference.value.id);
}

std::string ReferenceServer::ClipboardText(std::string_view id) const {
  const std::string owned_id(id);
  const std::string token = settings_.ReferenceCapability(owned_id);
  std::string text = base_url() + AgentPath(owned_id, token);
  if (text.size() >= kMaximumClipboardTextBytes)
    throw std::runtime_error("compact reference URL exceeds bound");
  return text;
}

std::string ReferenceServer::ViewerUrl(std::string_view id) const {
  const std::string owned_id(id);
  return base_url() + "/v1/references/" + owned_id + "/view#cap=" +
         settings_.ReferenceCapability(owned_id);
}

void ReferenceServer::Serve() {
  while (!stopping_) {
    pollfd descriptor{listener_, POLLIN, 0};
    const int ready = poll(&descriptor, 1, 200);
    if (ready <= 0) continue;
    sockaddr_storage peer{};
    socklen_t size = sizeof(peer);
    const int client = accept(listener_, reinterpret_cast<sockaddr*>(&peer), &size);
    if (client < 0) continue;
    if (!LoopbackPeer(client)) {
      close(client);
      continue;
    }
    {
      std::lock_guard lock(clients_mutex_);
      if (clients_.size() >= 16) {
        Error(client, 503, "Service Unavailable", "server_busy");
        close(client);
      } else {
        clients_.push_back(client);
        clients_ready_.notify_one();
      }
    }
  }
}

void ReferenceServer::Handle(int client) {
  std::string failure;
  auto request = ReadRequest(client, &failure);
  if (!request) {
    Error(client, failure == "headers_too_large" ? 431 :
                  failure == "body_too_large" ? 413 : 400,
          failure == "headers_too_large" ? "Request Header Fields Too Large" :
          failure == "body_too_large" ? "Payload Too Large" : "Bad Request",
          failure);
    return;
  }
  constexpr std::string_view underlay_prefix="/v1/references/";
  constexpr std::string_view underlay_suffix="/annotated.png?layers=strokes";
  const bool canonical_underlay_target=request->target.starts_with(underlay_prefix) &&
      request->target.size()==underlay_prefix.size()+32+underlay_suffix.size() &&
      SafeId(std::string_view(request->target).substr(underlay_prefix.size(),32)) &&
      request->target.ends_with(underlay_suffix);
  if (request->target.size() > 2048 || request->target.front() != '/' ||
      (request->target.find('?') != std::string::npos && !canonical_underlay_target) ||
      request->target.find('#') != std::string::npos ||
      request->target.find('%') != std::string::npos ||
      request->target.find("..") != std::string::npos ||
      request->target.find('\\') != std::string::npos) {
    Error(client, 400, "Bad Request", "invalid_target");
    return;
  }
  const auto strict_loopback_request = [&]() {
    const auto host = request->headers.find("host");
    if (host == request->headers.end() ||
        host->second != "127.0.0.1:" + std::to_string(port_)) {
      Error(client, 400, "Bad Request", "invalid_host");
      return false;
    }
    const auto origin = request->headers.find("origin");
    if (origin != request->headers.end() && origin->second != base_url()) {
      Error(client, 403, "Forbidden", "invalid_origin");
      return false;
    }
    return true;
  };
  if (request->target == "/v1/reference-view.js") {
    if (!strict_loopback_request()) return;
    if (request->method != "GET") {
      Error(client, 405, "Method Not Allowed", "method_not_allowed");
      return;
    }
    Respond(client, 200, "OK", "text/javascript; charset=utf-8",
            kReferenceViewerScript);
    return;
  }
  if (request->target == "/settings" || request->target == "/settings/") {
    if (request->method != "GET") {
      Error(client, 405, "Method Not Allowed", "method_not_allowed");
      return;
    }
    auto asset = SettingsAsset(settings_web_root_, false);
    if (!asset) {
      Error(client, 503, "Service Unavailable", "settings_asset_unavailable");
      return;
    }
    Respond(client, 200, "OK", "text/html; charset=utf-8", *asset,
            "Content-Security-Policy: default-src 'self'; script-src 'self'; "
            "connect-src 'self'; style-src 'self' 'unsafe-inline'; "
            "object-src 'none'; base-uri 'none'; frame-ancestors 'none'\r\n");
    return;
  }
  if (request->target == "/settings/settings.js") {
    if (request->method != "GET") {
      Error(client, 405, "Method Not Allowed", "method_not_allowed");
      return;
    }
    auto asset = SettingsAsset(settings_web_root_, true);
    if (!asset) {
      Error(client, 503, "Service Unavailable", "settings_asset_unavailable");
      return;
    }
    Respond(client, 200, "OK", "text/javascript; charset=utf-8", *asset);
    return;
  }
  if (request->target == "/v1/readiness" ||
      request->target == "/v1/readiness/retry") {
    const std::string bearer = Bearer(*request);
    if (bearer.empty()) {
      Error(client, 401, "Unauthorized", "missing_bearer");
      return;
    }
    if (!ConstantTimeEqual(bearer, settings_capability_)) {
      Error(client, 403, "Forbidden", "invalid_settings_capability");
      return;
    }
    if (request->target == "/v1/readiness") {
      if (request->method != "GET") {
        Error(client, 405, "Method Not Allowed", "method_not_allowed");
        return;
      }
      Respond(client, 200, "OK", "application/json; charset=utf-8",
              ReadinessJson(*readiness_));
      return;
    }
    if (request->method != "POST") {
      Error(client, 405, "Method Not Allowed", "method_not_allowed");
      return;
    }
    const auto origin = request->headers.find("origin");
    if (origin == request->headers.end() || origin->second != base_url()) {
      Error(client, 403, "Forbidden", "invalid_origin");
      return;
    }
    const auto type = request->headers.find("content-type");
    if (type == request->headers.end() ||
        (Lower(type->second) != "application/json" &&
         Lower(type->second) != "application/json; charset=utf-8")) {
      Error(client, 415, "Unsupported Media Type", "unsupported_content_type");
      return;
    }
    if (Trim(request->body) != "{}") {
      Error(client, 422, "Unprocessable Content", "invalid_retry_request");
      return;
    }
    readiness_->retry_generation.fetch_add(1);
    Respond(client, 202, "Accepted", "application/json; charset=utf-8",
            "{\"retry_requested\":true}\n");
    return;
  }
  if (request->target == "/v1/settings") {
    const std::string bearer = Bearer(*request);
    if (bearer.empty()) {
      Error(client, 401, "Unauthorized", "missing_bearer");
      return;
    }
    if (!ConstantTimeEqual(bearer, settings_capability_)) {
      Error(client, 403, "Forbidden", "invalid_settings_capability");
      return;
    }
    if (request->method == "GET") {
      Respond(client, 200, "OK", "application/json; charset=utf-8",
              core::SettingsJson(settings_.Get()));
      return;
    }
    if (request->method != "PUT") {
      Error(client, 405, "Method Not Allowed", "method_not_allowed");
      return;
    }
    const auto origin = request->headers.find("origin");
    if (origin == request->headers.end() || origin->second != base_url()) {
      Error(client, 403, "Forbidden", "invalid_origin");
      return;
    }
    const auto type = request->headers.find("content-type");
    if (type == request->headers.end() ||
        (Lower(type->second) != "application/json" &&
         Lower(type->second) != "application/json; charset=utf-8")) {
      Error(client, 415, "Unsupported Media Type", "unsupported_content_type");
      return;
    }
    std::string parse_error;
    auto settings = core::ParseSettingsJson(request->body, &parse_error);
    if (!settings) {
      Error(client, 422, "Unprocessable Content", "invalid_settings");
      return;
    }
    if (!settings_.Update(*settings, &parse_error)) {
      Error(client, 500, "Internal Server Error", "settings_persist_failed");
      return;
    }
    references_.SetRetentionPolicy(settings->reference_ttl_seconds,
                                   settings->maximum_visible_references);
    Respond(client, 200, "OK", "application/json; charset=utf-8",
            core::SettingsJson(settings_.Get()));
    return;
  }
  constexpr std::string_view prefix = "/v1/references/";
  constexpr std::string_view agent_prefix = "/r/";
  const bool agent_path = request->target.rfind(agent_prefix, 0) == 0;
  if (!agent_path && request->target.rfind(prefix, 0) != 0) {
    Error(client, 404, "Not Found", "unknown_endpoint");
    return;
  }
  const std::string remainder = request->target.substr(
      agent_path ? agent_prefix.size() : prefix.size());
  const auto slash = remainder.find('/');
  const std::string handle = remainder.substr(0, slash);
  const std::string id = agent_path ? handle.substr(0, 32) : handle;
  const std::string path_capability = agent_path && handle.size() == 96
                                          ? handle.substr(32) : std::string{};
  const bool single_image = agent_path && slash == std::string::npos;
  const std::string resource = slash == std::string::npos ? "metadata" :
                               remainder.substr(slash + 1);
  const bool display_underlay=!agent_path && resource=="annotated.png?layers=strokes";
  const bool mark_resource=agent_path&&resource.starts_with("marks/")&&
      resource.ends_with(".png")&&resource.size()==6+32+4&&
      LowerHex(std::string(resource.substr(6,32)));
  const bool shot_resource=resource.starts_with("shots/") &&
      resource.size()==6+32+14 && resource.substr(38)=="/annotated.png" &&
      LowerHex(resource.substr(6,32));
  const bool valid_resource = shot_resource || resource == "metadata" || resource == "record" ||
                              (agent_path && (resource == "detail" || resource == "details")) ||
                              mark_resource || display_underlay ||
                              resource == "source.png" ||
                              resource == "crop.png" ||
                              resource == "annotated.png" ||
                              (!agent_path && resource == "view");
  if (!SafeId(id) || (agent_path &&
                      (handle.size() != 96 || !LowerHex(path_capability))) ||
      !valid_resource) {
    Error(client, 400, "Bad Request", "invalid_reference_target");
    return;
  }
  if (!strict_loopback_request()) return;
  if (request->method != "GET") {
    Error(client, 405, "Method Not Allowed", "method_not_allowed");
    return;
  }
  if (resource == "view") {
    Respond(client, 200, "OK", "text/html; charset=utf-8",
            kReferenceViewerHtml,
            "Content-Security-Policy: default-src 'none'; script-src 'self'; "
            "connect-src 'self'; img-src blob:; style-src 'none'; "
            "object-src 'none'; base-uri 'none'; frame-ancestors 'none'; "
            "form-action 'none'\r\n");
    return;
  }
  const std::string presented_capability = agent_path ? path_capability :
                                           Bearer(*request);
  if (presented_capability.empty()) {
    Error(client, 401, "Unauthorized", "missing_bearer");
    return;
  }
  const std::string expected_capability = settings_.ReferenceCapability(id);
  if (!ConstantTimeEqual(presented_capability, expected_capability)) {
    Error(client, 403, "Forbidden", "invalid_reference_capability");
    return;
  }
  const auto settings = settings_.Get();
  references_.SetRetentionPolicy(settings.reference_ttl_seconds,
                                 settings.maximum_visible_references);
  auto job = references_.LookupMetadata(id);
  if (!job) {
    Error(client, 404, "Not Found", "unknown_reference");
    return;
  }
  if (job->state == core::ReferenceJobState::kPending ||
      job->state == core::ReferenceJobState::kIndexing) {
    std::ostringstream body;
    body << "{\"reference_id\":\"" << id
         << "\",\"state\":\""
         << (job->state == core::ReferenceJobState::kIndexing ? "indexing" : "pending")
         << "\",\"retry_ms\":25,\"stage\":\""
         << Escape(job->code) << "\"}\n";
    Respond(client, 202, "Accepted", "application/json; charset=utf-8",
            body.str());
    return;
  }
  if (job->state == core::ReferenceJobState::kExpired) {
    ExpiredReference(client, id);
    return;
  }
  if (job->state == core::ReferenceJobState::kDeleted) {
    std::ostringstream body;
    body << "{\"reference_id\":\"" << id
         << "\",\"state\":\"deleted\",\"error\":\""
         << Escape(job->code) << "\"}\n";
    Respond(client, 410, "Gone", "application/json; charset=utf-8",
            body.str());
    return;
  }
  if (job->state == core::ReferenceJobState::kFailed || !job->metadata) {
    std::ostringstream body;
    body << "{\"reference_id\":\"" << id
         << "\",\"state\":\"failed\",\"code\":\""
         << Escape(job->code) << "\"}\n";
    Respond(client, 422, "Unprocessable Content",
            "application/json; charset=utf-8", body.str());
    return;
  }
  const auto& found = *job->metadata;
  const auto age = core::Now().utc_us - found.image_completed.utc_us;
  const bool outside_visible_limit =
      job->newer_ready >= settings.maximum_visible_references;
  if (age < 0 || age > settings.reference_ttl_seconds * 1'000'000LL ||
      outside_visible_limit) {
    ExpiredReference(client, id);
    return;
  }
  const auto collection=references_.Collection(id);
  if(collection.status!=core::MarkNotesStatus::kReady&&collection.shots.empty()) {
    Error(client,422,"Unprocessable Content","collection_corrupt");return;
  }
  if(shot_resource) {
    const auto shot_id=resource.substr(6,32);
    const auto shot=std::find_if(collection.shots.begin(),collection.shots.end(),
        [&](const auto& value){return value.id==shot_id;});
    if(shot==collection.shots.end()||!shot->metadata) {
      Error(client,404,"Not Found","unknown_shot");return;
    }
    const auto asset=references_.RequestShotAnnotated(id,shot_id);
    if(asset.state==core::ReadyAssetState::kPending) {
      Respond(client,202,"Accepted","application/json; charset=utf-8",
        "{\"reference_id\":\""+id+"\",\"shot_id\":\""+shot_id+
        "\",\"state\":\"pending\",\"retry_ms\":25}");return;
    }
    if(asset.state!=core::ReadyAssetState::kReady||!asset.bytes) {
      Error(client,404,"Not Found",asset.code.empty()?"unknown_shot":asset.code);return;
    }
    Respond(client,200,"OK","image/png",
      std::string_view(reinterpret_cast<const char*>(asset.bytes->data()),asset.bytes->size()),
      "ETag: \"sha256-"+asset.byte_sha256+"\"\r\nX-SeeThis-PNG-SHA256: "+asset.byte_sha256+
      "\r\nX-SeeThis-Reference-ID: "+id+"\r\nX-SeeThis-Shot-ID: "+shot_id+
      "\r\nX-SeeThis-Source-SHA256: "+shot->metadata->source.sha256+"\r\n");return;
  }
  if(collection.multi()) {
    if(resource!="metadata"&&resource!="details"&&resource!="detail") {
      Error(client,409,"Conflict","explicit_shot_required");return;
    }
    Respond(client,200,"OK","application/json; charset=utf-8",
      CollectionJson(found,base_url()+AgentPath(id,expected_capability),collection,
        job->accepted_utc_us+settings.reference_ttl_seconds*1'000'000LL));return;
  }
  if (resource == "record") {
    if(found.schema!=1) {
      Error(client, 422, "Unprocessable Content", "legacy_export_unavailable");
      return;
    }
    const auto payload = references_.Lookup(id);
    if (!payload || !payload->ready || payload->ready->record.empty()) {
      Error(client, 422, "Unprocessable Content", "asset_unavailable");
      return;
    }
    Respond(client, 200, "OK", "application/vnd.seethis.reference-v1",
            std::string_view(reinterpret_cast<const char*>(payload->ready->record.data()),
                             payload->ready->record.size()));
  } else if (resource == "source.png") {
    const auto asset = references_.ReadReadyAsset(id, false);
    if (!asset) {
      Error(client, 422, "Unprocessable Content", "asset_unavailable");
      return;
    }
    const auto digest=core::Sha256(*asset);
    Respond(client, 200, "OK", "image/png",
            std::string_view(reinterpret_cast<const char*>(asset->data()),asset->size()),
            "ETag: \"sha256-"+digest+"\"\r\nX-SeeThis-PNG-SHA256: "+digest+"\r\n");
  } else if (resource == "crop.png") {
    const auto asset = references_.RequestReadyCrop(id);
    if (asset.state == core::ReadyAssetState::kPending) {
      std::ostringstream body;
      body << "{\"reference_id\":\"" << id << "\",\"state\":\""
           << Escape(asset.code) << "\",\"retry_ms\":25}\n";
      Respond(client, 202, "Accepted", "application/json; charset=utf-8",
              body.str());
      return;
    }
    if (asset.state != core::ReadyAssetState::kReady || !asset.bytes) {
      Error(client, 422, "Unprocessable Content",
            asset.code.empty()?"asset_unavailable":asset.code);
      return;
    }
    const auto digest=asset.byte_sha256.empty()?core::Sha256(*asset.bytes):
        asset.byte_sha256;
    Respond(client, 200, "OK", "image/png",
            std::string_view(reinterpret_cast<const char*>(asset.bytes->data()),
                             asset.bytes->size()),
            "ETag: \"sha256-"+digest+"\"\r\nX-SeeThis-PNG-SHA256: "+digest+"\r\n");
  } else if (mark_resource) {
    const auto region_count=found.regions.empty()?1:found.regions.size();
    const std::string mark_id=resource.substr(6,32);
    std::size_t mark_index=region_count;
    for(std::size_t index=0;index<region_count;++index)
      if(ConstantTimeEqual(mark_id,GestureMarkId(found,index))) {
        mark_index=index;break;
      }
    if(mark_index==region_count) {
      Error(client,404,"Not Found","unknown_mark");return;
    }
    const auto asset=references_.RequestReadyMark(id,mark_index);
    if(asset.state==core::ReadyAssetState::kPending) {
      std::ostringstream body;
      body << "{\"reference_id\":\"" << id << "\",\"mark_id\":\""
           << mark_id << "\",\"state\":\"" << Escape(asset.code)
           << "\",\"retry_ms\":25}\n";
      Respond(client,202,"Accepted","application/json; charset=utf-8",body.str());
      return;
    }
    if(asset.state!=core::ReadyAssetState::kReady||!asset.bytes) {
      Error(client,422,"Unprocessable Content",
            asset.code.empty()?"asset_unavailable":asset.code);
      return;
    }
    const auto digest=asset.byte_sha256.empty()?core::Sha256(*asset.bytes):
        asset.byte_sha256;
    Respond(client,200,"OK","image/png",
            std::string_view(reinterpret_cast<const char*>(asset.bytes->data()),
                             asset.bytes->size()),
            "ETag: \"sha256-"+digest+"\"\r\nX-SeeThis-PNG-SHA256: "+digest+"\r\n");
  } else if (resource == "annotated.png" || display_underlay) {
    if(display_underlay && found.schema!=2) {
      Error(client,422,"Unprocessable Content","display_underlay_unsupported");return;
    }
    const auto asset=display_underlay?references_.RequestReadyDisplayUnderlay(id):
        references_.RequestReadyAnnotated(id);
    if(asset.state==core::ReadyAssetState::kPending) {
      std::ostringstream body;
      body << "{\"reference_id\":\"" << id << "\",\"state\":\""
           << Escape(asset.code) << "\",\"retry_ms\":25}\n";
      Respond(client, 202, "Accepted", "application/json; charset=utf-8",
              body.str());
      return;
    }
    if(asset.state!=core::ReadyAssetState::kReady||!asset.bytes) {
      Error(client, 422, "Unprocessable Content",
            asset.code.empty()?"asset_unavailable":asset.code);
      return;
    }
    Respond(client, 200, "OK", "image/png",
            std::string_view(reinterpret_cast<const char*>(asset.bytes->data()),
                             asset.bytes->size()),
            "ETag: \"sha256-"+asset.byte_sha256+"\"\r\n"
            "X-SeeThis-PNG-SHA256: "+asset.byte_sha256+"\r\n");
  } else if (agent_path && resource == "details") {
    const std::string agent_base=base_url()+AgentPath(id,expected_capability);
    std::ostringstream body;
    body << "{\"details_schema\":1,\"reference_schema\":" << found.schema
         << ",\"reference_id\":\"" << id
         << "\",\"legacy_metadata\":{\"delivery_schema\":2,\"url\":\""
         << agent_base << "/metadata\"},\"historical_detail\":{\"url\":\""
         << agent_base << "/detail\"},\"bearer_metadata\":{\"url\":\""
         << base_url() << "/v1/references/" << id
         << "\",\"auth\":\"reference_bearer\"},\"viewer\":{\"url\":\""
         << ViewerUrl(id) << "\"}}\n";
    Respond(client,200,"OK","application/json; charset=utf-8",body.str());
  } else if (agent_path && resource == "metadata") {
    const std::string agent_base=base_url()+AgentPath(id,expected_capability);
    const auto notes=references_.MarkSnapshots(id);
    if(notes.status!=core::MarkNotesStatus::kReady) {
      if(notes.status==core::MarkNotesStatus::kUnsupported)
        Error(client,409,"Conflict","notes_unsupported");
      else if(notes.status==core::MarkNotesStatus::kCorrupt)
        Error(client,422,"Unprocessable Content","notes_corrupt");
      else Error(client,500,"Internal Server Error","notes_unavailable");
      return;
    }
    if(single_image) {
      const auto image=references_.RequestReadyAnnotated(id);
      const bool ready=image.state==core::ReadyAssetState::kReady && image.bytes &&
          !image.bytes->empty() && !image.byte_sha256.empty();
      const bool pending=image.state==core::ReadyAssetState::kPending;
      Respond(client,ready?200:pending?202:422,
              ready?"OK":pending?"Accepted":"Unprocessable Content",
              "application/json; charset=utf-8",
              SingleImageReferenceJson(found,agent_base,notes,image,
                  found.image_completed.utc_us+
                  settings.reference_ttl_seconds*1'000'000LL));
      return;
    }
    Respond(client,200,"OK","application/json; charset=utf-8",
            CompactReferenceJson(found,agent_base,references_,notes,settings_.Visibility(),
                found.image_completed.utc_us+
                settings.reference_ttl_seconds*1'000'000LL));
  } else {
    core::ReadyAssetResult crop;
    if (agent_path) {
      crop = references_.RequestReadyCrop(id);
    } else if (!found.crop.sha256.empty()) {
      crop.state = core::ReadyAssetState::kReady;
      crop.byte_sha256 = found.crop.sha256;
    }
    const std::string agent_base =
        base_url() + AgentPath(id, expected_capability);
    const auto annotated=references_.RequestReadyAnnotated(id);
    core::ReadyAssetResult underlay;
    const bool describe_underlay=!agent_path && found.schema==2;
    if(describe_underlay)underlay=references_.RequestReadyDisplayUnderlay(id);
    Respond(client, 200, "OK", "application/json; charset=utf-8",
            ReferenceJson(found,
                          agent_path ? agent_base :
                                       base_url() + std::string(prefix) + id,
                          agent_base,
                          annotated, crop,
                          found.image_completed.utc_us +
                              settings.reference_ttl_seconds * 1'000'000LL,
                          describe_underlay?&underlay:nullptr));
  }
}

}  // namespace seethis::service
