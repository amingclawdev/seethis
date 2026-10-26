'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(process.argv[2]||'src/service/runs_view.h','utf8');
const script=source.split('kRunsScript=R"JS(')[1].split(')JS";')[0];
const box={module:{exports:{}}};vm.runInNewContext(script,box);
assert(!/[\u4e00-\u9fff]/u.test(source),'Product-authored Reference runs UI must be English');
const merge=box.module.exports.timelineEvents;
const state=box.module.exports.taskState;
const segments=box.module.exports.segmentRuns;
const oldSegment={run_id:'old',chain_id:'old',session_id:'session-old',start:{at:'2026-10-25T00:00:00Z'},status:'transferred',status_source:'handoff',handoff_end:{at:'2026-10-25T00:00:01Z',to_run_id:'new'}};
const newSegment={run_id:'new',chain_id:'old',session_id:'session-new',start:{at:'2026-10-25T00:00:01Z'},status:'in-progress',handoff_from:{run_id:'old'},current_segment:true};
assert.equal(state(oldSegment).label,'Transferred');
assert.equal(state({status:'in-progress',pending_handoffs:[{}]}).label,'Takeover confirmed');
assert.deepEqual(Array.from(segments(newSegment,[newSegment,oldSegment]),r=>r.run_id),['old','new']);
assert.throws(()=>segments(newSegment,[newSegment]),/unavailable/);
assert.throws(()=>segments(newSegment,[newSegment,{...oldSegment,handoff_end:{to_run_id:'missing'}}]),/unavailable/);
assert.throws(()=>segments(newSegment,[{...newSegment,handoff_end:{to_run_id:'old'}},{...oldSegment,handoff_from:{run_id:'new'}}]),/Invalid/);
assert.equal(merge([],[],newSegment,[oldSegment,newSegment]).filter(x=>x.kind==='boundary').length,3);
assert.equal(state(null).label,'Not started');
assert.equal(state(null,false).label,'Unknown');
assert.equal(state({status:'done',status_source:'agent'}).label,'Completed');
assert.equal(state({status:'done',status_source:'hook'}).label,'Unknown');
assert.equal(state({status:'in-progress'}).label,'In progress');
assert(state({status:'in-progress'}).reason.includes('not a heartbeat'));
assert.equal(state({status:'blocked',status_source:'agent'}).label,'Exception · Blocked');
assert.equal(state({status:'interrupted',status_source:'hook'}).label,'Exception · Interrupted');
assert.equal(state({status:'stopped-without-end',status_source:'hook'}).label,'Exception · Unconfirmed');
assert.equal(state({status:'unsupported'}).label,'Unknown');
const run={start:{at:'2026-10-25T00:00:00Z'},reported_end:{at:'2026-10-25T00:00:02Z'},status:'done',status_source:'agent'};
const start=Date.parse(run.start.at);
const notes=[{revision:1,at_ms:start-1000,shot_id:'a',after:'first'},{revision:2,at_ms:start+1000,shot_id:'b',after:'second · 修改记录 café 🌱'},{revision:3,at_ms:start+3000,shot_id:'a',after:''}];
const messages=[{kind:'message',role:'assistant',at_ms:start+1000,order:4,text:'reply · 用户日志 café 🌱'},{kind:'message',role:'user',at_ms:start,order:2,text:'question'}];
const original=JSON.stringify({notes,messages,run});
const result=merge(notes,messages,run);
assert.equal(result.length,7);
assert.equal(result[0].outside,'Before hand-in');
assert.equal(result.at(-1).outside,'After run');
assert.equal(result.filter(x=>x.kind==='note').length,3);
assert.equal(result.filter(x=>x.kind==='boundary').length,2);
assert.equal(result.filter(x=>x.at_ms===start+1000)[0].role,'assistant');
assert.equal(JSON.stringify({notes,messages,run}),original);
assert.equal(merge(notes,[],null).length,3);
assert.equal(merge([],[],null).length,0);
assert.equal(merge([],messages,{start:run.start,status:'in-progress',status_source:'hook'}).filter(x=>x.kind==='boundary').length,1);
assert(!script.includes('innerHTML'));assert(!script.includes('localStorage'));assert(!script.includes('sessionStorage'));
console.log('Timeline: chronological merge, boundaries, all-shot notes, equal-time ordering, no mutation, clear/empty/live checks passed.');

const basis=box.module.exports.noteBasis,deliveryStatus=box.module.exports.deliveryStatus;
const reported={...run,notes_revision:4,notes_revision_source:'agent'};
const delivered={available:true,full:false,limit:256,records:[{notes_revision:4,at_ms:start+1}]};
assert.equal(basis(reported,delivered),'Based on note revision 4');
assert.equal(basis(run,delivered),'Unknown');
assert.equal(basis({...reported,notes_revision:0},{...delivered,records:[{notes_revision:0,at_ms:start+1}]}),'Based on note revision 0');
for(const records of [[],[{notes_revision:3,at_ms:start+1}],[{notes_revision:4,at_ms:start}],[{notes_revision:4,at_ms:start+2000}],[{notes_revision:4,at_ms:start-1}],[{notes_revision:4,at_ms:start+2001}]])
 assert.equal(basis(reported,{...delivered,records}),'Agent reports note revision 4 (unverified)');
for(const r of [{...reported,notes_revision_source:'hook'},{...reported,notes_revision:true},{...reported,notes_revision:'4'}])assert.equal(basis(r,delivered),'Unknown');
assert.equal(basis({...reported,reported_end:null},delivered),'Agent reports note revision 4 (unverified)');
assert.equal(basis(reported,{...delivered,available:false}),'Agent reports note revision 4 (unverified)');
assert.equal(basis(reported,{...delivered,full:true}),'Based on note revision 4');
assert.equal(deliveryStatus({...delivered,full:true}),'Delivery record full');
assert.equal(deliveryStatus({...delivered,available:false}),'Delivery record unavailable; unrecorded deliveries cannot be verified.');
assert.equal(deliveryStatus(delivered),'Recorded 1 / 256 deliveries; only revisions and time intervals are checked; requesters are not recorded.');
assert.equal(merge([{revision:1,at_ms:NaN}],[],run)[0].outside,'Unknown');
assert.equal(merge(notes,[],run).find(n=>n.revision===2).outside,'In progress');
assert.equal(merge(notes,[],newSegment,[oldSegment,newSegment]).find(n=>n.revision===1).outside,'Before hand-in');
assert.equal(merge([{revision:1,at_ms:start+500}],[],newSegment,[oldSegment,newSegment])[1].outside,'Before hand-in');
console.log('Note basis: exact revision plus strict run interval, reported-only/unknown, capacity and selected-run phase checks passed.');

// Execute the shipped script against a small DOM/network adapter. This verifies
// UI state/ownership, not browser rendering or macOS physical interaction.
class Element {
 constructor(tag){this.tag=tag;this.children=[];this.hidden=false;this.textContent='';this.value='';this.style={};this.events={};this.open=false;this.naturalWidth=1200;}
 append(...xs){this.children.push(...xs);}
 replaceChildren(...xs){this.children=[...xs];}
 removeAttribute(name){delete this[name];}
 addEventListener(name,fn){this.events[name]=fn;}
 showModal(){this.open=true;}
 close(){this.open=false;this.events.close?.();}
 focus(){this.focused=true;}
}
const elements=new Map();const get=id=>{if(!elements.has(id))elements.set(id,new Element(id));return elements.get(id);};
const ref='a'.repeat(32),shotB='b'.repeat(32),runId='c'.repeat(32);
const model={reference_id:ref,title:'用户标题 · café 🌱',captured_at_ms:start,shots:[{id:ref},{id:shotB}],marks:[{number:1,shot_id:ref,note:'first image note · 用户笔记 café 🌱'},{number:2,shot_id:shotB,note:'second image note'}],note_history:notes.map((n,i)=>({...n,shot_id:i===1?shotB:ref,number:i===1?2:1,before:'previous · 之前 café 🌱'})),history_scope:'Earlier edits unknown',delivery_log:{...delivered,full:true}};
const storedRun={...reported,run_id:runId,session_id:'synthetic-session',start:{...run.start,locator:{path:'/synthetic/location'}},summary:'运行摘要 · café 🌱'};
const requested=[];let pluginAvailable=true,holdLog=null,replaced=null,noRuns=false,listFailure=false,holdProgress=null,handoff=false,missingOldLog=false,missingOldMetadata=false;
const ui={console,URLSearchParams,URL,Date,Promise,setTimeout,document:{getElementById:get,createElement:tag=>new Element(tag)},location:{hash:'#cap=synthetic-local-view&ref='+ref},history:{replaceState:(_,__,url)=>{replaced=url;}},fetch:async(url,options)=>{
 assert(url.startsWith('/runs/api/'));assert.equal(options.headers.Authorization,'Bearer synthetic-local-view');assert.equal(options.cache,'no-store');requested.push(url);
 let data;
 if(url.endsWith('/references')){if(listFailure)throw Error('reference_service_unavailable');data={references:[{id:ref,title:'用户标题 · café 🌱',captured_at_ms:start}]};}
 else if(url.endsWith('/details'))data=model;
 else if(url.endsWith('/runs')){if(holdProgress)await holdProgress;data=pluginAvailable?{available:true,runs:noRuns?[]:handoff?[{...newSegment,start:{...newSegment.start,locator:{}}},{...oldSegment,start:{...oldSegment.start,locator:{}}}].filter(r=>!missingOldMetadata||r.run_id!=='old'):[storedRun]}:{available:false,reason:'plugin_not_connected'};}
 else if(url.includes('/shot/'))return {ok:true,status:200,blob:async()=>new Blob(['synthetic'])};
 else if(url.includes('/log/')){if(holdLog)await holdLog;data=handoff?(missingOldLog&&url.endsWith('/old')?{available:false,reason:'interval_truncated'}:{available:true,messages:[{kind:'message',role:'assistant',at_ms:start+1000,order:0,text:url.endsWith('/old')?'old segment message':'new segment message'}],interval:{live_snapshot:url.endsWith('/new')}}):{available:true,messages,interval:{live_snapshot:false}};}
 else throw Error('unexpected path');
 return {ok:true,status:200,json:async()=>structuredClone(data)};
}};
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const allText=x=>[x.textContent,...x.children.map(allText)].join('\n');
(async()=>{
 vm.runInNewContext(script,ui);for(let i=0;i<12;i++)await tick();
 assert.equal(replaced,'/runs/');assert.equal(get('list').children.length,1);assert(get('list').children[0].className.includes('selected'));
 get('list').children[0].children.at(-1).onclick();for(let i=0;i<12;i++)await tick();
 assert.equal(get('detail').hidden,false);
 assert.equal(get('connection-state').textContent,'Plugin · Connected');
 assert.equal(get('task-state').textContent,'Task · Completed');
 assert.equal(get('delivery-status').textContent,'Delivery record full');assert.equal(get('run-note-basis').textContent,'Based on note revision 4');assert(allText(get('run-meta')).includes('Based on note revision 4'));assert(allText(get('timeline')).includes('Codex will not automatically see this change.'));
 assert(allText(get('marks')).includes('first image note'));assert(!allText(get('marks')).includes('second image note'));
 assert([...elements.values()].some(x=>x.textContent.includes('用户标题 · café 🌱')));
 assert(allText(get('run-meta')).includes('运行摘要 · café 🌱'));
 assert(allText(get('marks')).includes('用户笔记 café 🌱'));
 assert(allText(get('timeline')).includes('用户日志 café 🌱'));
 assert(allText(get('timeline')).includes('修改记录 café 🌱'));
 assert(allText(get('timeline')).includes('之前 café 🌱'));
 assert(allText(get('timeline')).includes('You'));assert(allText(get('timeline')).includes('Codex'));
 assert.equal(get('timeline').children.filter(x=>x.className.startsWith('event note')).length,3);
 get('enlarge').onclick();assert.equal(get('viewer').open,true);assert.equal(get('viewer-image').src,get('image').src);
 get('actual-size').onclick();assert.equal(get('viewer-image').style.width,'1200px');assert.equal(get('zoom-label').textContent,'100%');
 get('zoom-in').onclick();assert.equal(get('zoom-label').textContent,'125%');
 get('fit').onclick();assert.equal(get('viewer-image').className,'fit');
 get('viewer-close').onclick();assert.equal(get('viewer').open,false);assert.equal(get('enlarge').focused,true);
 const before=allText(get('timeline'));get('next').onclick();for(let i=0;i<4;i++)await tick();
 assert(allText(get('marks')).includes('second image note'));assert(!allText(get('marks')).includes('first image note'));assert.equal(allText(get('timeline')),before);
 get('image').onclick();assert(get('viewer-caption').textContent.includes('Shot 2 of 2'));get('viewer-previous').onclick();for(let i=0;i<4;i++)await tick();
 assert(get('viewer-caption').textContent.includes('Shot 1 of 2'));assert.equal(get('viewer').open,true);assert.equal(get('viewer-image').src,get('image').src);
 get('viewer-close').onclick();
 get('clear').onclick();assert(!allText(get('timeline')).includes('question'));assert.equal(get('timeline').children.filter(x=>x.className.startsWith('event note')).length,3);
 await get('back').onclick();assert.equal(get('list').children.length,1);assert(get('list').children[0].className.includes('selected'));assert.equal(get('detail').hidden,true);
 pluginAvailable=false;get('list').children[0].children.at(-1).onclick();for(let i=0;i<12;i++)await tick();
 assert.equal(get('detail').hidden,false);assert(allText(get('status')).includes('Plugin connection is unavailable'));
 assert.equal(get('connection-state').textContent,'Plugin · Unavailable');assert.equal(get('task-state').textContent,'Task · Unknown');
 assert.equal(get('run-select').disabled,true);assert.equal(get('run-select').children.length,0);
 pluginAvailable=true;noRuns=true;await get('refresh').onclick();for(let i=0;i<8;i++)await tick();
 assert.equal(get('connection-state').textContent,'Plugin · Connected');assert.equal(get('task-state').textContent,'Task · Not started');assert.equal(get('run-select').disabled,true);
 noRuns=false;
 pluginAvailable=true;let release;holdLog=new Promise(resolve=>{release=resolve;});get('refresh').onclick();for(let i=0;i<8;i++)await tick();
 await get('back').onclick();release();for(let i=0;i<8;i++)await tick();
 assert.equal(get('detail').hidden,true);assert(allText(get('status')).includes('available references'));
 holdLog=null;listFailure=true;await get('refresh').onclick();assert.equal(get('connection-state').textContent,'Plugin · Unknown');
 assert(allText(get('connection-note')).includes('reference_service_unavailable'));
 listFailure=false;await get('refresh').onclick();assert.equal(get('connection-state').textContent,'Plugin · Connected');
 let releaseProgress;holdProgress=new Promise(resolve=>{releaseProgress=resolve;});get('list').children[0].children.at(-1).onclick();for(let i=0;i<8;i++)await tick();
 assert.equal(get('clear').disabled,true);get('clear').onclick();releaseProgress();for(let i=0;i<12;i++)await tick();
 assert.equal(get('connection-state').textContent,'Plugin · Connected');assert.equal(get('task-state').textContent,'Task · Completed');assert.equal(get('clear').disabled,false);
 handoff=true;await get('refresh').onclick();for(let i=0;i<8;i++)await tick();
 assert(allText(get('timeline')).includes('old segment message'));assert(allText(get('timeline')).includes('new segment message'));
 assert(allText(get('timeline')).includes('Segment 1 · session-old'));assert(allText(get('run-meta')).includes('Current segment'));
 get('log-scope').value='segment';await get('log-scope').onchange();
 assert(!allText(get('timeline')).includes('old segment message'));assert(allText(get('timeline')).includes('new segment message'));
 get('run-select').value='old';await get('run-select').onchange();assert.equal(get('task-state').textContent,'Task · Transferred');
 get('log-scope').value='chain';missingOldLog=true;await get('log-scope').onchange();
 assert(allText(get('status')).includes('Log incomplete: Segment 1: interval_truncated'));
 assert(!allText(get('timeline')).includes('old segment message'));assert(allText(get('timeline')).includes('new segment message'));
 assert.equal(get('timeline').children.filter(x=>x.className.startsWith('event note')).length,3);
 missingOldMetadata=true;await get('refresh').onclick();for(let i=0;i<8;i++)await tick();
 assert(allText(get('status')).includes('Log incomplete:'));
 assert(allText(get('status')).includes('showing only the selected registered segment'));
 assert(allText(get('timeline')).includes('new segment message'));
 assert.equal(get('timeline').children.filter(x=>x.className.startsWith('event note')).length,3);
 get('clear').onclick();assert(!allText(get('timeline')).includes('new segment message'));
 console.log('Handoff UI: linked chain / individual segments, immutable boundary labels, confirmed vs transferred status, missing segment and clear checks passed.');
 console.log('Shipped UI adapter: list/back, image enlargement and zoom, carousel ownership, all-shot timeline, roles, clear, separate connection/task truth table, empty vs unavailable, stale async response checks passed. Browser layout NOT_RUN.');
})().catch(error=>{console.error(error);process.exitCode=1;});
