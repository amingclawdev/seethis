#!/usr/bin/env python3
"""Build an offline, visibly synthetic preview from the exact shipped runs UI.

No server, real references, capabilities, run store or transcript access.
"""
import argparse
import json
from pathlib import Path


def fixture():
    start = 1792922400000
    ids = [format(i, "032x") for i in range(101, 107)]
    first, second = ids[:2]
    notes = [
        {"revision": 1, "at_ms": start, "shot_id": first, "number": 1,
         "before": "", "after": "Make the event date and location easy to find."},
        {"revision": 2, "at_ms": start + 10000, "shot_id": first, "number": 2,
         "before": "", "after": "Change the button to Reserve my seat and open the signup page."},
        {"revision": 3, "at_ms": start + 30000, "shot_id": second, "number": 3,
         "before": "", "after": "Explain that the email address is only for event updates."},
        {"revision": 4, "at_ms": start + 40000, "shot_id": second, "number": 4,
         "before": "", "after": "Show Registration confirmed after submitting."},
        {"revision": 5, "at_ms": start + 300000, "shot_id": first, "number": 2,
         "before": "Change the button to Reserve my seat and open the signup page.",
         "after": "The event is full. Change the button to Join the waitlist; keep the signup page."},
        {"revision": 6, "at_ms": start + 320000, "shot_id": second, "number": 4,
         "before": "Show Registration confirmed after submitting.",
         "after": "Show You are on the waitlist. Match the language on both pages."},
        {"revision": 7, "at_ms": start + 350000, "shot_id": first, "number": 1,
         "before": "Make the event date and location easy to find.", "after": ""},
    ]
    runs = [
        {"run_id": "b" * 32, "session_id": "mock-session-b", "status": "in-progress", "status_source": "hook",
         "summary": "", "start": {"at": "2026-10-25T10:06:00Z", "locator": {}}, "reported_end": None, "inferred_end": None},
        {"run_id": "a" * 32, "session_id": "mock-session-a", "status": "done", "status_source": "agent",
         "notes_revision": 4, "notes_revision_source": "agent",
         "summary": "Updated the homepage and signup form; checked navigation and confirmation.",
         "start": {"at": "2026-10-25T10:01:00Z", "locator": {}},
         "reported_end": {"at": "2026-10-25T10:04:00Z", "status": "done"}, "inferred_end": None},
    ]
    dialogue = {
        "a" * 32: [
            ("user", 60, "Please update both pages using these four marks. Keep the existing layout and verify the full signup flow."),
            ("assistant", 75, "I have read the two screenshots. Marks 1–2 describe the homepage; marks 3–4 describe the signup form. I will update the labels, add the email explanation, and keep the form result on the page."),
            ("assistant", 120, "The homepage now includes the date and location below its title. The main button opens the signup page. The form explains why an email address is needed."),
            ("user", 160, "Please also check that the user can return to the homepage without losing their place."),
            ("assistant", 210, "Verified the forward and back links, empty-email validation, and the Registration confirmed message. All four requested changes are present. This is the first completed run; later changes will be recorded separately."),
        ],
        "b" * 32: [
            ("user", 360, "Continue with this same reference. The event is full now. I updated notes 2 and 4: please change the two-page flow to a waitlist."),
            ("assistant", 375, "I read the previous run and fetched the latest reference. Note 2 now asks for Join the waitlist. Note 4 asks for You are on the waitlist. I will preserve the date, location and email explanation."),
            ("assistant", 400, "The homepage button has been updated. I am checking the signup heading and result message so they do not promise a confirmed seat."),
            ("user", 420, "Keep the old run and its notes in the history. The new wording should only apply from this change onward."),
            ("assistant", 440, "The previous run remains unchanged. I am validating both navigation paths and the submission result before reporting an ending."),
        ],
    }
    return {"ids": ids, "start": start, "notes": notes, "runs": runs,
            "messages": {key: [{"kind": "message", "role": role, "at_ms": start + sec * 1000,
                                 "order": i, "text": text} for i, (role, sec, text) in enumerate(rows)]
                         for key, rows in dialogue.items()}}


BOOTSTRAP = r"""
(()=>{
'use strict';
const fixture=__FIXTURE__,ids=fixture.ids,copy=x=>JSON.parse(JSON.stringify(x));
let available=true,scenario='in-progress';
history.replaceState=()=>{};
location.hash='cap=mock-only-no-access&ref='+ids[0];
const names=['Launch Day · Homepage + signup','Sponsor page · Not started','Agenda · Blocked','Mobile layout · Completed','Footer links · Interrupted','Copy review · Unconfirmed'];
const states=['in-progress','none','blocked','done','interrupted','stopped-without-end'];
function currentRuns(id){
 const state=id===ids[0]?scenario:states[ids.indexOf(id)];if(state==='none')return [];
 const rs=copy(fixture.runs),r=rs[0];
 if(['handoff','takeover-confirmed'].includes(state)){
  const old=rs[1];old.reported_end=null;old.summary='Previous segment paused for handoff.';old.status='in-progress';old.status_source='hook';
  old.start.locator={path:'/mock/session-a.jsonl',byte:100};r.start.locator={path:'/mock/session-b.jsonl',byte:200};
  if(state==='takeover-confirmed'){r.pending_handoffs=[{from_run_id:old.run_id,to_run_id:r.run_id}];return rs;}
  old.chain_id=r.chain_id=old.run_id;old.current_segment=false;r.current_segment=true;
  old.status='transferred';old.status_source='handoff';old.handoff_end={at:'2026-10-25T10:06:00Z',to_run_id:r.run_id,locator:{path:'/mock/session-a.jsonl',byte:900}};
  r.handoff_from={run_id:old.run_id};return rs;
 }
 r.status=state;r.status_source=['done','blocked'].includes(state)?'agent':'hook';
 r.summary=state==='blocked'?'Waiting for the final event date before updating the agenda.':state==='done'?'Both pages updated and navigation checked.':'';
 r.reported_end=null;r.inferred_end=null;r.notes_revision=null;r.notes_revision_source=null;
 if(['done','blocked'].includes(state)){r.reported_end={at:'2026-10-25T10:09:00Z',status:state};r.notes_revision=7;r.notes_revision_source='agent';}
 else if(state!=='in-progress')r.inferred_end={at:'2026-10-25T10:09:00Z',status:state};
 return rs;
}
function mockImage(second){
 const marks=second?'<circle cx="195" cy="352" r="22" fill="#cb2141"/><text x="195" y="360" class="n">3</text><path d="M220 352H635" class="mark"/><circle cx="195" cy="708" r="22" fill="#cb2141"/><text x="195" y="716" class="n">4</text><path d="M220 708H950" class="mark"/>':'<circle cx="125" cy="395" r="22" fill="#cb2141"/><text x="125" y="403" class="n">1</text><path d="M150 395H970" class="mark"/><circle cx="125" cy="551" r="22" fill="#cb2141"/><text x="125" y="559" class="n">2</text><path d="M150 551H532" class="mark"/>';
 const content=second?'<text x="160" y="225" font-size="46" font-weight="750">Save your spot.</text><text x="160" y="273" fill="#657773">Join the people building what comes next.</text><text x="240" y="345" font-size="18">Email address</text><rect x="240" y="370" width="720" height="62" rx="8" fill="white" stroke="#c8d5ce"/><text x="260" y="410" fill="#9ba9a2">you@example.com</text><text x="240" y="466" font-size="18" fill="#60776b">Your email is used only for event updates.</text><rect x="240" y="520" width="720" height="65" rx="8" fill="#256b52"/><text x="600" y="561" text-anchor="middle" fill="white">Reserve my seat</text><rect x="240" y="630" width="720" height="116" rx="9" fill="#e0efe4"/><text x="274" y="680" font-size="24" font-weight="650">Registration confirmed</text><text x="274" y="716" font-size="18">Check your inbox for your invitation.</text>':'<text x="160" y="220" font-size="18" letter-spacing="4" fill="#256b52">IDEAS. PEOPLE. A DAY TO BUILD.</text><text x="160" y="302" font-size="66" font-weight="780">Make something</text><text x="160" y="378" font-size="66" font-weight="780">worth sharing.</text><text x="160" y="448" fill="#657773">October 30 · 10 AM – 6 PM · Toronto Design Hub</text><rect x="160" y="495" width="340" height="68" rx="10" fill="#256b52"/><text x="330" y="537" text-anchor="middle" fill="white">Reserve my seat →</text><text x="160" y="640" font-size="25" font-weight="650">A small room. Big possibilities.</text><text x="160" y="689" fill="#657773">Meet makers, share an idea, and build your first version.</text><text x="160" y="728" fill="#657773">Design, code, conversation — all experience levels welcome.</text>';
 return '<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="900" viewBox="0 0 1200 900"><style>text{font-family:Arial,sans-serif;font-size:24px;fill:#20332f}.n{fill:white;text-anchor:middle;font-size:22px;font-weight:bold}.mark{stroke:#cb2141;stroke-width:5;fill:none}</style><rect width="1200" height="900" fill="#f5f7f0"/><rect width="1200" height="55" fill="#e4e9e3"/><text x="34" y="35" font-size="18">MOCK SCREENSHOT · launch-day.example / '+(second?'signup':'home')+'</text><text x="65" y="115" font-weight="750">LAUNCH DAY</text><text x="890" y="115" font-size="18">About     Schedule     Join</text><path d="M60 140H1140" stroke="#d3dcd5"/>'+content+marks+'<text x="65" y="856" font-size="16" fill="#8b9990">Synthetic sample for interaction review · not a real reference</text></svg>';
}
window.fetch=async(input)=>{
 const prefix='/runs/api/';if(typeof input!=='string'||!input.startsWith(prefix))throw Error('Mock preview blocks all external requests');
 const parts=input.slice(prefix.length).split('/'),id=parts[0],action=parts[1];let value;
 if(id==='references')value={references:ids.map((id,i)=>({id,title:names[i],captured_at_ms:fixture.start-i*3600000}))};
 else if(!ids.includes(id))return new Response(JSON.stringify({error:'mock_reference_missing'}),{status:404});
 else if(action==='details')value={reference_id:id,title:names[ids.indexOf(id)],captured_at_ms:fixture.start,shots:[{id:ids[0]},{id:ids[1]}],marks:[{number:1,shot_id:ids[0],note:''},{number:2,shot_id:ids[0],note:fixture.notes[4].after},{number:3,shot_id:ids[1],note:fixture.notes[2].after},{number:4,shot_id:ids[1],note:fixture.notes[5].after}],note_history:copy(fixture.notes),delivery_log:{available:true,limit:256,full:false,records:[{notes_revision:4,at_ms:fixture.start+65000},{notes_revision:7,at_ms:fixture.start+365000}]},history_scope:'Mock history · 7 changes, including a cleared note. Nothing is written to SeeThis.'};
 else if(action==='runs')value=available?{available:true,runs:currentRuns(id)}:{available:false,reason:'mock_plugin_disconnected'};
 else if(action==='log'){
  const messages=copy(fixture.messages[parts[2]]||[]);
  if(['handoff','takeover-confirmed'].includes(scenario)){
   if(parts[2]==='a'.repeat(32)&&messages.length)messages[messages.length-1].text='I have paused before checking the final navigation and submission result. These checks remain for the incoming agent.';
   if(parts[2]==='b'.repeat(32)&&messages.length>1)messages[1].text=scenario==='handoff'?'I confirmed takeover and committed the handoff. The earlier log segment is preserved; I am continuing the remaining checks.':'I confirmed takeover. The change has not been committed; the old segment remains current.';
  }
  value=available?{available:true,messages,interval:{live_snapshot:parts[2]==='b'.repeat(32)&&['in-progress','handoff','takeover-confirmed'].includes(scenario)}}:{available:false,reason:'mock_plugin_disconnected'};
 }
 else if(action==='shot')return new Response(new Blob([mockImage(parts[2]===ids[1])],{type:'image/svg+xml'}));
 else throw Error('No mock endpoint for this request');
 return new Response(JSON.stringify(value),{headers:{'Content-Type':'application/json'}});
};
document.getElementById('demo-connection').onchange=e=>{available=e.target.value==='connected';document.getElementById('refresh').click();};
document.getElementById('demo-task').onchange=e=>{scenario=e.target.value;document.getElementById('refresh').click();};
})();
"""


def build(source):
    html = source.split('kRunsHtml=R"HTML(', 1)[1].split(')HTML";', 1)[0]
    js = source.split('kRunsScript=R"JS(', 1)[1].split(')JS";', 1)[0]
    banner = """<aside style="padding:14px 28px;background:#fff2cf;border-bottom:1px solid #dbc68d;display:flex;gap:14px;align-items:center;flex-wrap:wrap" aria-label="Mock preview controls">
<strong>MOCK PREVIEW · Synthetic data</strong><span>Does not read or modify real references or logs</span>
<label>Plugin <select id="demo-connection"><option value="connected">Connected</option><option value="disconnected">Disconnected</option></select></label>
<label>Launch Day task <select id="demo-task"><option value="in-progress">In progress</option><option value="takeover-confirmed">Takeover confirmed</option><option value="handoff">Handoff chain</option><option value="none">Not started</option><option value="blocked">Blocked</option><option value="done">Completed</option><option value="interrupted">Interrupted</option><option value="stopped-without-end">Unconfirmed</option></select></label>
<span>Open Launch Day details → Enlarge an image → Switch between runs → Expand Previous text</span></aside>"""
    html = html.replace("<body>", "<body>" + banner, 1)
    html = html.replace("<title>", '<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; script-src \'unsafe-inline\'; style-src \'unsafe-inline\'; img-src blob: data:; connect-src \'none\'; base-uri \'none\'; form-action \'none\'"><title>MOCK · ', 1)
    boot = BOOTSTRAP.replace("__FIXTURE__", json.dumps(fixture(), ensure_ascii=False).replace("<", "\\u003c"))
    return html.replace('<script src="/runs/app.js"></script>', "<script>" + boot + "</script><script>" + js + "</script>")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(build((root / "src/service/runs_view.h").read_text()))
    print(args.output.resolve())
