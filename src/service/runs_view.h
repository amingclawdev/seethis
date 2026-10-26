#pragma once
#include <string_view>

namespace seethis::service {
inline constexpr std::string_view kRunsHtml=R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SeeThis · Reference runs</title><style>
:root{color-scheme:light;--ink:#20332f;--muted:#64756f;--line:#dce4de;--green:#256b52}*{box-sizing:border-box}
body{margin:0;background:#f5f6f1;color:var(--ink);font:15px/1.5 system-ui,sans-serif}button,select{font:inherit;border:1px solid var(--line);background:#fff;color:var(--ink);border-radius:8px;padding:8px 13px;cursor:pointer}button:hover{background:#eaf0e9}button:disabled{opacity:.45;cursor:default}[hidden]{display:none!important}
header{padding:22px 30px;background:#fff;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;align-items:center;gap:20px}.brand{font-size:12px;letter-spacing:.16em;color:var(--green);font-weight:750}h1{font-size:25px;letter-spacing:-.035em;line-height:1.2;margin:8px 0}h2{font-size:16px;margin:0}.muted{color:var(--muted)}.small{font-size:12px}.actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
main{max-width:1600px;margin:auto;padding:24px 30px}#status{margin:0 0 18px;white-space:pre-wrap;color:var(--muted)}#list{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:15px}.ref-card{padding:20px;background:white;border:1px solid var(--line);border-radius:14px}.ref-card.selected{border-color:var(--green);box-shadow:0 0 0 1px var(--green)}.ref-card h2{overflow-wrap:anywhere;margin-bottom:10px}.ref-card code{display:block;color:var(--muted);font-size:11px;margin:10px 0}
.detail-title{margin-bottom:18px}.detail-title h1{overflow-wrap:anywhere}.columns{display:grid;grid-template-columns:minmax(300px,46%) minmax(360px,54%);border:1px solid var(--line);border-radius:14px;overflow:hidden;background:#fff}.images{padding:22px;border-right:1px solid var(--line)}.image-stage{min-height:220px;display:flex;align-items:center;justify-content:center;background:#f2f4ef;border:1px solid var(--line);border-radius:9px;margin:15px 0}.image-stage img{max-width:100%;max-height:54vh;object-fit:contain}.carousel{justify-content:space-between}.marks{max-height:26vh;overflow:auto}.mark{padding:12px 0;border-bottom:1px solid var(--line);white-space:pre-wrap;overflow-wrap:anywhere}.number{font-weight:750;margin-right:10px;color:var(--green)}
.conversation{min-width:0;display:flex;flex-direction:column;height:76vh;min-height:500px}.run-controls{padding:20px;border-bottom:1px solid var(--line)}#run-select{width:100%;margin:10px 0}#run-meta{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;color:var(--muted);max-height:110px;overflow:auto}#timeline{padding:20px;overflow-y:auto;flex:1;overscroll-behavior:contain}.event{padding:15px 17px;margin-bottom:14px;border:1px solid var(--line);border-radius:11px;background:#fafbf8}.event.user{margin-left:28px;background:#eaf1ec}.event.assistant{margin-right:28px;background:#fff}.event.note{border-left:3px solid #bd8b35;background:#fff9ec}.event.note.during{border-left:5px solid #c65a16;background:#fff0d4}.note-warning{font-weight:700;color:#963e0b}.event.boundary{border:0;background:#f0f3ee;color:var(--muted);font-size:12px}.event-head{font-size:12px;font-weight:700;display:flex;justify-content:space-between;gap:10px}.event pre{white-space:pre-wrap;overflow-wrap:anywhere;font:14px/1.65 system-ui,sans-serif;margin:10px 0 0}.event details{font-size:12px;margin-top:10px}footer{font-size:12px;color:var(--muted);padding:18px 0}code{overflow-wrap:anywhere}
@media(max-width:800px){header{padding:18px}main{padding:18px}.columns{grid-template-columns:1fr}.images{border-right:0;border-bottom:1px solid var(--line)}.conversation{height:65vh}.image-stage img{max-height:42vh}}
.connection{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-top:10px}.badge{display:inline-flex;align-items:center;gap:6px;padding:5px 10px;border-radius:6px;font-size:12px;font-weight:700;background:#edf0ed;color:#52615c}.badge.good{background:#e2f3e8;color:#176044}.badge.active{background:#e5efff;color:#254e91}.badge.warning{background:#fff0d4;color:#865900}.badge.bad{background:#fde8e5;color:#9b332b}.badge::before{content:'●';font-size:9px}.detail-heading{display:flex;justify-content:space-between;align-items:start;gap:16px;flex-wrap:wrap}.task-summary{margin:12px 0}.task-summary p{margin:7px 0}.back-link{font-weight:650}.image-stage{position:relative}.image-stage img{cursor:zoom-in}#enlarge{margin-bottom:12px}button:focus-visible,select:focus-visible{outline:3px solid #3f85c5;outline-offset:3px}.run-heading{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}.ref-card .badge{margin-bottom:12px}.ref-card button{display:block;margin-top:12px}
dialog{width:96vw;max-width:none;height:94vh;max-height:none;padding:0;border:1px solid var(--line);border-radius:12px;background:#f5f6f1;color:var(--ink)}dialog::backdrop{background:rgba(12,22,18,.78)}.viewer-shell{display:flex;flex-direction:column;height:100%}.viewer-bar{background:#fff;display:flex;justify-content:space-between;align-items:center;gap:12px;padding:14px 18px;flex-wrap:wrap;border-bottom:1px solid var(--line)}#viewer-stage{overflow:auto;flex:1;min-height:0;padding:16px;display:block;text-align:center}#viewer-image{display:block;margin:auto;max-width:none}#viewer-image.fit{max-width:100%;max-height:100%;width:auto;height:auto}#viewer-caption{overflow-wrap:anywhere;max-width:60ch}.viewer-hint{margin:0;padding:8px 18px;font-size:12px;color:var(--muted)}@media(max-width:600px){header{align-items:start;flex-direction:column}.columns{grid-template-columns:minmax(0,1fr)}#list{grid-template-columns:minmax(0,1fr)}.images{padding:14px}.viewer-bar{padding:10px}.event.user{margin-left:12px}.event.assistant{margin-right:12px}}
</style></head><body>
<header><div><div class="brand">SEETHIS / CODEX</div><h1>Reference runs</h1><div class="connection" aria-live="polite"><span id="connection-state" class="badge">Plugin · Not checked</span><span id="connection-note" class="small muted">Open a reference to check the connection.</span></div></div><div class="actions"><button id="back" class="back-link" hidden>← Back to references</button><button id="refresh">Refresh</button><button id="clear" hidden>Clear log</button></div></header>
<main><p id="status" role="status">Loading local references…</p><section id="list" aria-label="References"></section>
<section id="detail" hidden><div class="detail-title"><div class="detail-heading"><h1 id="title"></h1><span id="task-state" class="badge" role="status">Task · Checking</span></div><code id="identity" class="small muted"></code><div class="task-summary"><p id="task-reason" class="muted"></p></div></div>
<div class="columns"><section class="images" aria-label="Reference images"><div class="actions carousel" id="carousel"><button id="previous">Previous</button><span id="shot-count"></span><button id="next">Next</button></div><div class="image-stage"><img id="image" alt="Marked reference with mark numbers" hidden><span id="image-status">Loading image…</span></div><button id="enlarge" disabled>Enlarge image ↗</button><h2>Notes on this image</h2><div id="marks" class="marks"></div><p id="history-scope" class="small muted"></p><p id="delivery-status" class="small muted" role="status"></p></section>
<section class="conversation" aria-label="Readable conversation and note changes"><div class="run-controls"><div class="run-heading"><h2>Conversation & note changes</h2><span id="run-count" class="small muted"></span></div><select id="run-select" aria-label="Run"></select><label class="small">Logs <select id="log-scope" aria-label="Log scope"><option value="chain">Handoff chain</option><option value="segment">This segment</option></select></label><p id="run-note-basis" class="badge">Unknown</p><details><summary class="small">Run details & source</summary><div id="run-meta"></div></details></div><div id="timeline" tabindex="0" aria-label="Timeline"></div></section></div></section>
<footer>Local display only · Logs stay in memory. Note events do not prove Codex read the change. Refresh reads current saved data; it does not replay work.</footer></main>
<dialog id="viewer" aria-label="Enlarged reference image"><div class="viewer-shell"><div class="viewer-bar"><strong id="viewer-caption"></strong><div class="actions"><button id="viewer-previous" aria-label="Previous enlarged image">← Previous</button><button id="viewer-next" aria-label="Next enlarged image">Next →</button><button id="zoom-out" aria-label="Zoom out">−</button><span id="zoom-label" aria-live="polite">Fit</span><button id="zoom-in" aria-label="Zoom in">+</button><button id="actual-size">100%</button><button id="fit">Fit</button><button id="viewer-close">Close ✕</button></div></div><div id="viewer-stage"><img id="viewer-image" class="fit" alt="Enlarged marked reference"></div><p class="viewer-hint">Esc closes · ← → switches images · Use 100% or + to read small text; scroll to pan.</p></div></dialog>
<script src="/runs/app.js"></script></body></html>)HTML";

inline constexpr std::string_view kRunsScript=R"JS((()=>{
'use strict';
function segmentRuns(run,runs){
  if(!run)return [];
  if(!run.chain_id)return [run];
  const found=new Map(runs.map(r=>[r.run_id,r])),seen=new Set(),segments=[];
  let current=found.get(run.chain_id);
  if(!current)throw Error('First log segment is unavailable; choose This segment.');
  while(current){
    if(seen.has(current.run_id)||current.chain_id!==run.chain_id)throw Error('Invalid handoff chain.');
    if(segments.length===8)throw Error('More than 8 linked segments; choose This segment.');
    seen.add(current.run_id);segments.push(current);
    const next=current.handoff_end?.to_run_id;if(!next)break;
    const following=found.get(next);
    if(!following||following.handoff_from?.run_id!==current.run_id)throw Error('Linked log segment is unavailable; choose This segment.');
    current=following;
  }
  if(!seen.has(run.run_id))throw Error('Selected segment is not linked to this chain.');
  return segments;
}
function timelineEvents(notes,messages,run,segments=null){
  const spans=segments||(run?[run]:[]);
  // Classify against the selected run even while showing the whole handoff chain.
  const start=run?Date.parse(run.start.at):NaN,endRecord=run&&(run.handoff_end||run.reported_end||run.inferred_end),end=endRecord?Date.parse(endRecord.at):NaN;
  const items=messages.map(m=>({...m,source:0}));
  for(const n of notes)items.push({...n,kind:'note',source:1,order:n.revision,outside:!Number.isFinite(start)||!Number.isFinite(n.at_ms)||(endRecord&&(!Number.isFinite(end)||end<start))?'Unknown':n.at_ms<start?'Before hand-in':Number.isFinite(end)&&n.at_ms>=end?'After run':'In progress'});
  for(const [index,part] of spans.entries()){
    const begin=Date.parse(part.start.at),ending=part.handoff_end||part.reported_end||part.inferred_end,finish=ending?Date.parse(ending.at):NaN;
    const label=spans.length>1?'Segment '+(index+1)+' · '+part.session_id:'Run';
    if(Number.isFinite(begin))items.push({kind:'boundary',text:label+' handed in',at_ms:begin,source:2,order:index*2});
    if(Number.isFinite(finish))items.push({kind:'boundary',text:label+' · Recorded end · '+part.status+' · '+part.status_source,at_ms:finish,source:2,order:index*2+1});
  }
  return items.sort((a,b)=>a.at_ms-b.at_ms||a.source-b.source||a.order-b.order);
}
function noteBasis(run,log){
  const revision=run?.notes_revision;
  if(!Number.isSafeInteger(revision)||revision<0||run?.notes_revision_source!=='agent')return 'Unknown';
  const begin=Date.parse(run.start?.at),end=Date.parse(run.reported_end?.at);
  const matched=log?.available===true&&Number.isFinite(begin)&&Number.isFinite(end)&&end>begin&&
    Array.isArray(log.records)&&log.records.some(d=>d.notes_revision===revision&&Number.isFinite(d.at_ms)&&d.at_ms>begin&&d.at_ms<end);
  return matched?'Based on note revision '+revision:'Agent reports note revision '+revision+' (unverified)';
}
function deliveryStatus(log){
  if(log?.available!==true)return 'Delivery record unavailable; unrecorded deliveries cannot be verified.';
  if(log.full===true)return 'Delivery record full';
  return 'Recorded '+(log.records?.length||0)+' / '+log.limit+' deliveries; only revisions and time intervals are checked; requesters are not recorded.';
}
function taskState(r,available=true){
  if(!available)return {label:'Unknown',tone:'warning',reason:'Task status could not be read. This does not mean the task has not started.'};
  if(!r)return {label:'Not started',tone:'',reason:'No run is registered. Hand this reference to Codex to begin.'};
  const source=r.status_source==='agent'?'Agent report':r.status_source==='handoff'?'Confirmed handoff':'Host observation';
  if(r.status==='transferred'&&r.status_source==='handoff')return {label:'Transferred',tone:'',reason:'This log segment was handed off. It is not a completed task; inspect the current segment.'};
  if(r.status==='in-progress'&&r.pending_handoffs?.length)return {label:'Takeover confirmed',tone:'warning',reason:'The incoming agent confirmed. The active log segment has not switched; the incoming agent still needs to complete the switch.'};
  if(r.status==='done'&&r.status_source==='agent')return {label:'Completed',tone:'good',reason:'Agent reported completion. Review its result below.'};
  if(r.status==='in-progress')return {label:'In progress',tone:'active',reason:'Registered; no ending recorded yet. This is not a heartbeat or proof of current activity.'};
  if(r.status==='blocked')return {label:'Exception · Blocked',tone:'warning',reason:source+': '+(r.summary||'Cannot continue; see the run details.')};
  if(r.status==='interrupted')return {label:'Exception · Interrupted',tone:'warning',reason:source+': the run was interrupted; no completion was reported.'};
  if(r.status==='stopped-without-end')return {label:'Exception · Unconfirmed',tone:'warning',reason:source+': the turn stopped without an end report. Completion is unknown.'};
  return {label:'Unknown',tone:'warning',reason:'No supported completion or progress report is available.'};
}
if(typeof module!=='undefined'&&module.exports){module.exports={timelineEvents,taskState,segmentRuns,noteBasis,deliveryStatus};return;}
const $=id=>document.getElementById(id),el=(tag,text,cls)=>{const x=document.createElement(tag);if(text!==undefined)x.textContent=text;if(cls)x.className=cls;return x;};
const initial=new URLSearchParams(location.hash.slice(1)),cap=initial.get('cap'),first=initial.get('ref');
history.replaceState(null,'','/runs/');
let listFocus=first,selected=null,detail=null,runs=[],run=null,messages=[],shot=0,epoch=0,imageEpoch=0,imageUrl=null;
let zoom=null,displayedSegments=[];
$('log-scope').value='chain';
const time=ms=>Number.isFinite(ms)?new Date(ms).toLocaleString():'Time unavailable';
async function api(path){
  const response=await fetch('/runs/api/'+path,{headers:{Authorization:'Bearer '+cap},cache:'no-store',credentials:'omit',redirect:'error',referrerPolicy:'no-referrer'});
  if(!response.ok)throw Error((await response.json()).error||('HTTP '+response.status));
  return response;
}
function closeViewer(){if($('viewer').open)$('viewer').close();}
function clearImage(){++imageEpoch;if(imageUrl)URL.revokeObjectURL(imageUrl);imageUrl=null;$('image').removeAttribute('src');$('viewer-image').removeAttribute('src');$('image').hidden=true;$('enlarge').disabled=true;}
function status(value){$('status').textContent=value;}
function connection(available,reason=''){
  $('connection-state').textContent='Plugin · '+(available===true?'Connected':available===false?'Unavailable':available===null?'Checking':'Unknown');
  $('connection-state').className='badge '+(available===true?'good':available===false?'bad':'');
  $('connection-note').textContent=available===true?'Last successful read '+new Date().toLocaleTimeString():available===false?(reason||'Reconnect the plugin, then Refresh.'):available===null?'Checking the local plugin…':'Connection could not be checked: '+reason;
}
function renderTask(available=true){const s=taskState(run,available);$('task-state').textContent='Task · '+s.label;$('task-state').className='badge '+s.tone;$('task-reason').textContent=s.reason;}
function setZoom(value){
  zoom=value===null?null:Math.max(.25,Math.min(4,value));const im=$('viewer-image');
  im.className=zoom===null?'fit':'';im.style.width=zoom===null?'':Math.round(($('image').naturalWidth||1200)*zoom)+'px';
  $('zoom-label').textContent=zoom===null?'Fit':Math.round(zoom*100)+'%';
}
function viewerPosition(){
  if(!detail)return;
  $('viewer-caption').textContent=detail.title+' · Shot '+(shot+1)+' of '+detail.shots.length;
  $('viewer-previous').disabled=shot===0;$('viewer-next').disabled=shot+1===detail.shots.length;
}
function openViewer(){if(!imageUrl)return;$('viewer-image').src=imageUrl;setZoom(null);viewerPosition();$('viewer').showModal();$('viewer-close').focus();}
function renderTimeline(){
  $('timeline').replaceChildren();
  const items=timelineEvents(detail?.note_history||[],messages,run,displayedSegments);
  if(!items.length)$('timeline').append(el('p','No saved messages or note changes for this selection.','muted'));
  for(const event of items){
    const card=el('article',undefined,'event '+(event.kind==='message'?event.role:event.kind)+(event.kind==='note'&&event.outside==='In progress'?' during':''));
    if(event.kind==='boundary'){card.append(el('span',event.text+' · '+time(event.at_ms)));}
    else{
      let label=event.kind==='note'?(event.after?'Note edited':'Note cleared')+' · Mark '+event.number:event.role==='user'?'You':'Codex';
      if(event.segment_label)label+=' · '+event.segment_label;
      const head=el('div',undefined,'event-head');head.append(el('span',label),el('span',time(event.at_ms)));card.append(head);
      card.append(el('pre',event.kind==='note'?(event.after||'(Empty note)'):event.text));
      if(event.kind==='note'){
        const shotIndex=detail.shots.findIndex(s=>s.id===event.shot_id);
        card.append(el('p','Shot '+(shotIndex+1)+' · revision '+event.revision+(event.outside?' · '+event.outside:''),'small muted'));
        if(event.outside==='In progress')card.append(el('p','Codex will not automatically see this change. Tell it in the conversation or hand in the reference again.','note-warning'));
        const previous=el('details');previous.append(el('summary','Previous text'),el('pre',event.before||'(Empty note)'));card.append(previous);
      }
    }
    $('timeline').append(card);
  }
}
async function showShot(){
  clearImage();const ticket=imageEpoch,owner=selected,s=detail.shots[shot];
  $('previous').disabled=shot===0;$('next').disabled=shot+1===detail.shots.length;
  $('carousel').hidden=detail.shots.length<2;$('shot-count').textContent='Shot '+(shot+1)+' of '+detail.shots.length;
  viewerPosition();setZoom(null);
  $('image-status').hidden=false;$('image-status').textContent='Loading image…';$('marks').replaceChildren();
  for(const m of detail.marks.filter(m=>m.shot_id===s.id)){const row=el('div',undefined,'mark');row.append(el('span',String(m.number),'number'),el('span',m.note||'No note'));$('marks').append(row);}
  try{
    for(let tries=0;tries<40;tries++){
      const response=await api(owner+'/shot/'+s.id);
      if(ticket!==imageEpoch||selected!==owner)return;
      if(response.status===202){await new Promise(resolve=>setTimeout(resolve,100));continue;}
      const blob=await response.blob();if(ticket!==imageEpoch||selected!==owner)return;
      imageUrl=URL.createObjectURL(blob);$('image').src=imageUrl;$('viewer-image').src=imageUrl;$('image').hidden=false;$('image-status').hidden=true;$('enlarge').disabled=false;return;
    }
    throw Error('Image is still preparing. Refresh to retry.');
  }catch(error){if(ticket===imageEpoch)$('image-status').textContent='Image unavailable: '+error.message;}
}
async function showRun(id){
  const ticket=++epoch,owner=selected;run=runs.find(r=>r.run_id===id)||null;messages=[];displayedSegments=run?[run]:[];renderTimeline();
  $('clear').disabled=!run;$('log-scope').disabled=!run?.chain_id;
  renderTask();$('run-note-basis').textContent=noteBasis(run,detail?.delivery_log);
  if(!run){$('run-meta').textContent='No registered run. Note history remains visible.';return;}
  const ending=run.handoff_end||run.reported_end||run.inferred_end,begin=run.handoff_start||run.start.locator;
  $('run-meta').textContent=run.status+' · '+run.status_source+'\n'+(run.summary||'No explicit summary.')+'\nSession '+run.session_id+'\n'+(begin?.path||'Log location unavailable')+'\nBytes '+(begin?.byte??'unknown')+' → '+(ending?.locator?.byte??(ending?'unknown':'open'))+(run.chain_id?'\n'+(run.current_segment?'Current':'Previous')+' segment · chain '+run.chain_id:'')+'\n'+noteBasis(run,detail.delivery_log);
  if(run.reported_end&&run.inferred_end||run.handoff_end&&run.inferred_end)$('run-meta').textContent+='\nEarlier inference retained: '+run.inferred_end.status;
  status('Reading registered log segments…');
  try{
    let live=false,characters=0;const failures=[];
    try{displayedSegments=$('log-scope').value==='chain'?segmentRuns(run,runs):[run];}
    catch(error){displayedSegments=[run];failures.push(error.message+'; showing only the selected registered segment');}
    renderTimeline();
    for(const [index,part] of displayedSegments.entries()){
      try{
        const data=await (await api(owner+'/log/'+part.run_id)).json();if(ticket!==epoch||owner!==selected)return;
        if(!data.available)throw Error(data.reason||'Log unavailable');
        const size=data.messages.reduce((sum,m)=>sum+(m.text?.length||0),0);
        if(characters+size>2*1024*1024){failures.push('Display text limit reached; choose This segment.');break;}
        characters+=size;live=live||data.interval.live_snapshot;
        messages.push(...data.messages.map(m=>({...m,segment_label:displayedSegments.length>1?'Segment '+(index+1)+' · '+part.session_id:undefined})));
      }catch(error){if(ticket!==epoch)return;failures.push('Segment '+(index+1)+': '+error.message);}
    }
    if(ticket!==epoch||owner!==selected)return;renderTimeline();
    status((failures.length?'Log incomplete: '+failures.join('; ')+'. ':'')+'Showing '+messages.length+' messages across '+displayedSegments.length+' segment(s) and '+detail.note_history.length+' note changes.'+(live?' Active segment: current snapshot, no ending inferred.':' Fixed cutoffs retained.')+' Equal timestamps use stable display order, not a causal claim.');
  }catch(error){if(ticket===epoch)status('Log unavailable: '+error.message+'. Note history remains visible.');}
}
async function openReference(id,preserveRun=null){
  const ticket=++epoch;selected=id;listFocus=id;messages=[];displayedSegments=[];run=null;detail=null;runs=[];closeViewer();clearImage();
  connection(null);$('task-state').textContent='Task · Checking';$('task-reason').textContent='Reading registered runs…';$('run-select').replaceChildren();$('run-select').disabled=true;$('clear').disabled=true;$('run-count').textContent='';$('run-meta').textContent='';$('run-note-basis').textContent='Unknown';
  $('list').hidden=true;$('detail').hidden=true;$('back').hidden=false;$('clear').hidden=false;status('Loading reference…');
  try{
    const data=await (await api(id+'/details')).json();if(ticket!==epoch)return;detail=data;
    $('title').textContent=data.title;$('identity').textContent=id+' · '+time(data.captured_at_ms);
    $('history-scope').textContent=data.history_scope;$('delivery-status').textContent=deliveryStatus(data.delivery_log);$('detail').hidden=false;shot=Math.min(shot,data.shots.length-1);showShot();renderTimeline();
    const progress=await (await api(id+'/runs')).json();if(ticket!==epoch)return;
    $('run-select').replaceChildren();
    if(!progress.available){connection(false,progress.reason||'Plugin unavailable');renderTask(false);$('run-meta').textContent='Plugin unavailable: '+(progress.reason||'unknown')+'. Connect the plugin using its --connect command.';status('Images and notes are ready. Plugin connection is unavailable.');return;}
    connection(true);
    runs=progress.runs;
    $('run-select').disabled=runs.length===0;$('run-count').textContent=runs.length+' run'+(runs.length===1?'':'s');
    for(const [index,r] of runs.entries()){const option=el('option','Run '+(runs.length-index)+' · '+taskState(r).label+' · '+noteBasis(r,detail.delivery_log)+' · '+time(Date.parse(r.start.at)));option.value=r.run_id;$('run-select').append(option);}
    if(!runs.length){const option=el('option','No runs yet');option.value='';$('run-select').append(option);}
    const choice=runs.find(r=>r.run_id===preserveRun)||runs[0];$('run-select').value=choice?.run_id||'';
    if(!choice)status('No run is registered for this reference yet. Paste its JSON link into a new Codex conversation.');
    await showRun(choice?.run_id);
  }catch(error){if(ticket===epoch){connection(undefined,error.message);renderTask(false);status('Reference unavailable: '+error.message);$('detail').hidden=true;}}
}
async function showList(){
  const ticket=++epoch;selected=null;detail=null;run=null;messages=[];displayedSegments=[];closeViewer();clearImage();
  $('detail').hidden=true;$('list').hidden=false;$('back').hidden=true;$('clear').hidden=true;$('list').replaceChildren();status('Loading local references…');
  try{
    const data=await (await api('references')).json();if(ticket!==epoch)return;
    const checks=[];
    for(const ref of data.references){const card=el('article',undefined,'ref-card'+(ref.id===listFocus?' selected':''));if(ref.id===listFocus)card.append(el('p','Selected reference','small muted'));card.append(el('h2',ref.title),el('p',time(ref.captured_at_ms),'small muted'),el('code',ref.id));const badge=el('span','Task · Open to check','badge');card.append(badge);const button=el('button','View details →');button.onclick=()=>{shot=0;openReference(ref.id);};card.append(button);$('list').append(card);if(checks.length<12)checks.push({ref,badge});}
    status(data.references.length+' available references. Select one to see images, notes and conversation runs.');
    if(!checks.length){$('connection-state').textContent='Plugin · Not checked';$('connection-state').className='badge';$('connection-note').textContent='Create a reference to check the connection.';}
    else{
      connection(null);let cursor=0,anyAvailable=false,lastReason='';
      await Promise.all(Array.from({length:Math.min(3,checks.length)},async()=>{
        while(cursor<checks.length&&ticket===epoch){const {ref,badge}=checks[cursor++];
          try{const p=await (await api(ref.id+'/runs')).json();if(ticket!==epoch)return;const s=taskState(p.runs?.[0]||null,p.available===true);badge.textContent='Task · '+s.label;badge.className='badge '+s.tone;anyAvailable=anyAvailable||p.available===true;if(!p.available)lastReason=p.reason||'Plugin unavailable';}
          catch(error){if(ticket!==epoch)return;badge.textContent='Task · Unknown';badge.className='badge warning';lastReason=error.message;}
        }
      }));
      if(ticket===epoch)connection(anyAvailable,lastReason);
    }
  }catch(error){if(ticket===epoch){connection(undefined,error.message);status('References unavailable: '+error.message+'. Open Reference runs from SeeThis Inspector again.');}}
}
$('previous').onclick=()=>{if(shot>0){shot--;showShot();}};$('next').onclick=()=>{if(shot+1<detail.shots.length){shot++;showShot();}};
$('run-select').onchange=()=>showRun($('run-select').value);$('log-scope').onchange=()=>showRun(run?.run_id);$('back').onclick=showList;
$('image').onclick=openViewer;$('enlarge').onclick=openViewer;$('viewer-close').onclick=closeViewer;
$('viewer-previous').onclick=()=>{if(shot>0){shot--;showShot();}};$('viewer-next').onclick=()=>{if(detail&&shot+1<detail.shots.length){shot++;showShot();}};
$('fit').onclick=()=>setZoom(null);$('actual-size').onclick=()=>setZoom(1);$('zoom-in').onclick=()=>setZoom((zoom??1)+.25);$('zoom-out').onclick=()=>setZoom((zoom??1)-.25);
$('viewer').addEventListener('keydown',event=>{if(event.key==='ArrowLeft'){$('viewer-previous').onclick();event.preventDefault();}else if(event.key==='ArrowRight'){$('viewer-next').onclick();event.preventDefault();}});
$('viewer').addEventListener('close',()=>{$('enlarge').focus();});
$('refresh').onclick=()=>selected?openReference(selected,run?.run_id):showList();
$('clear').onclick=()=>{if($('clear').disabled)return;++epoch;messages=[];renderTimeline();status('Log cleared from this page. Original log and note history are unchanged.');};
if(!cap){status('Open Reference runs from SeeThis Inspector to authorize this local page.');$('refresh').disabled=true;}
else showList();
})();)JS";
}
