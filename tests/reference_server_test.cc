#include "service/reference_server.h"

#include "platform/platform.h"

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <limits>
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using namespace seethis::core;

int checks = 0;
void Check(bool condition, std::string_view message) {
  ++checks;
  if (!condition) throw std::runtime_error(std::string(message));
}

struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    std::string pattern = "/tmp/seethis-server-test-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* result = mkdtemp(buffer.data());
    if (!result) throw std::runtime_error("mkdtemp failed");
    path = result;
  }
  ~TempDirectory() { std::filesystem::remove_all(path); }
};

Bytes FileBytes(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Context ContextFixture(Stamp observed, int width = 100, int height = 100) {
  Context context;
  context.pid = 42;
  context.window_pid = 42;
  context.self_pid = 99;
  context.window_id = 7;
  context.app_name = "Fixture";
  context.bundle_id = "local.fixture";
  context.executable = "/fixture";
  context.window_title = "Immutable fixture";
  context.selection_method =
      "frontmost-pid/topmost-normal-containing-initial-point";
  context.window_layer = 0;
  context.window = {0, 0, static_cast<double>(width),
                    static_cast<double>(height)};
  context.quartz_to_appkit_top = height;
  context.observed = observed;
  context.displays.push_back(
      {"display-a", 1, {0, 0, static_cast<double>(width),
                         static_cast<double>(height)},
       {0, 0, static_cast<double>(width), static_cast<double>(height)}, 1,
       {1, -1, 0, static_cast<double>(height)}});
  context.displays.push_back(
      {"display-b", 2, {static_cast<double>(width),
                         -static_cast<double>(height) / 2,
                         static_cast<double>(width) / 2,
                         static_cast<double>(height) / 2},
       {0, 0, static_cast<double>(width),
        static_cast<double>(height)}, 2,
       {2, -2, -2 * static_cast<double>(width), 0}});
  context.excluded_window_ids = {8};
  context.exclusion_method =
      "exact-window-allowlist;self-pid-rejected;child-windows-off";
  return context;
}

WindowObservation WindowFor(const Context& context,
                            std::uint64_t generation) {
  WindowObservation observation;
  observation.availability = WindowAvailability::kReady;
  observation.generation = generation;
  observation.observed = Now();
  observation.pid = context.window_pid;
  observation.bundle_id = context.bundle_id;
  observation.window_id = context.window_id;
  observation.bounds = context.window;
  observation.window_title = context.window_title;
  observation.title = {1, TitleAvailability::kReady, true};
  return observation;
}

void Observe(MarkController& marks, const Context& context) {
  marks.SetWindowObservation(
      WindowFor(context, marks.window_observation().generation + 1));
}

StoredReference MakeReference(ReferenceStore& store, const std::string& id,
                              std::int64_t age_seconds,
                              bool realistically_large = false,
                              bool multi_region = false,
                              std::size_t extra_samples = 0, bool precise_start = false, std::size_t extra_mark_count = 0,
                              const Context* context_override = nullptr,
                              std::optional<CapturedPageMetadata> captured_page = std::nullopt) {
  const auto now = Now();
  Stamp observed{now.utc_us - age_seconds * 1'000'000LL,
                 now.monotonic_us - age_seconds * 1'000'000LL};
  if(context_override)observed=context_override->observed;
  MarkController marks(store, [](const auto&) { return true; });
  const auto generation = marks.Begin(id);
  const int width = extra_mark_count?1920:realistically_large ? 2048 : 100;
  const int height = extra_mark_count?1080:realistically_large ? 2400 : 100;
  const auto context=context_override?*context_override:ContextFixture(observed,width,height);
  std::optional<PageIdentity> frozenPage;
  if(context.bundle_id=="com.google.Chrome"&&(!context.scope.policy_present||context.scope.kind==CaptureScopeKind::kPage)) {
    PageIdentity page;page.bundle_id=context.bundle_id;page.browser_pid=context.window_pid;
    page.window_id=context.window_id;page.window_bounds=context.window;
    page.process_start_identity_us=700;page.opaque_tab_id="fixture-tab";
    page.navigation_digest=seethis::platform::ChromeNavigationDigest(
        captured_page&&captured_page->url?*captured_page->url:"https://example.test/legacy");
    frozenPage=page;
    if(context.scope.policy_present)marks.SetPageObservation({PageAvailability::kReady,1,Now(),page,captured_page});
  }
  Check(marks.BindContext(generation,context),"fixture context binds");
  if(frozenPage&&!context.scope.policy_present)
    Check(marks.BindPageIdentity(generation,*frozenPage,captured_page),"display metadata does not change fixture capture eligibility");
  CaptureResult capture;
  capture.requested = {observed.utc_us + 10, observed.monotonic_us + 10};
  capture.completed = {observed.utc_us + 20, observed.monotonic_us + 20};
  capture.pixels = {static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height),
                    Bytes(static_cast<std::size_t>(width) * height * 4)};
  std::uint32_t state = 0x9e3779b9U;
  // Dense competing-reference fixtures must have distinct pixels and PNG hashes;
  // keep established legacy fixtures unchanged for byte-preservation checks.
  if(extra_mark_count)for(unsigned char character:id)state=state*33U+character;
  for (std::size_t index = 0; index < capture.pixels.rgba.size(); ++index) {
    state = state * 1664525U + 1013904223U;
    capture.pixels.rgba[index] =
        index % 4 == 3 ? 255 : static_cast<std::uint8_t>(state >> 24);
  }
  marks.Captured(generation, std::move(capture));
  if(multi_region) {
    std::vector<InteractionSnapshot::LogicalRegion> regions;
    regions.push_back({{{1, CoordinateUnit::kLogicalPoints, {precise_start?20.1234567890123:20, 20}, 1},
                        {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                       {{1, CoordinateUnit::kLogicalPoints, {30, 30}, 1},
                        {1, CoordinateUnit::kLogicalPoints, {50, 50}, 1}}});
    regions.push_back({{{1, CoordinateUnit::kLogicalPoints, {precise_start?60.2345678901234:60, 60}, 1},
                        {1, CoordinateUnit::kLogicalPoints, {80, 80}, 1}}});
    for(std::size_t i=0;i<extra_mark_count;++i) {
      const double x=200+i*80,y=200+i*20;
      regions.push_back({{{1,CoordinateUnit::kLogicalPoints,{x,y},1},
                          {1,CoordinateUnit::kLogicalPoints,{x+40,y+30},1}}});
    }
    marks.Release(generation, std::move(regions),
                  {observed.utc_us + 30, observed.monotonic_us + 30});
  }
  else {
    std::vector<DisplayPoint> path;
    for(std::size_t index=0;index<extra_samples+2;++index) {
      const double position=20.0+20.0*index/(extra_samples+1);
      path.push_back({1,CoordinateUnit::kLogicalPoints,
                      {position,position},1});
    }
    marks.Release(generation,std::move(path),
        {observed.utc_us+30,observed.monotonic_us+30});
  }
  store.WaitForIdleForTesting();
  if (marks.phase() != ReferencePhase::kCommitted) {
    const auto job = store.LookupMetadata(id);
    std::cerr << "Reference fixture failure: id=" << id << " age_seconds=" << age_seconds
              << " now_monotonic_us=" << now.monotonic_us
              << " observed_monotonic_us=" << observed.monotonic_us
              << " phase=" << static_cast<int>(marks.phase()) << " status=" << marks.status()
              << " job_state=" << (job ? static_cast<int>(job->state) : -1)
              << " code=" << (job ? job->code : "missing") << '\n';
  }
  Check(marks.phase() == ReferencePhase::kCommitted, "fixture commits");
  return *store.Lookup(id)->ready;
}

struct Response {
  int status = 0;
  std::string bytes;
  std::string body;
};

Response Request(std::uint16_t port, const std::string& request) {
  const int client = socket(AF_INET, SOCK_STREAM, 0);
  if (client < 0) throw std::runtime_error("socket failed");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(client);
    throw std::runtime_error("connect failed");
  }
  std::size_t sent = 0;
  while (sent < request.size()) {
    const auto count = send(client, request.data() + sent, request.size() - sent, 0);
    if (count <= 0) throw std::runtime_error("send failed");
    sent += static_cast<std::size_t>(count);
  }
  shutdown(client, SHUT_WR);
  Response response;
  char buffer[4096];
  for (;;) {
    const auto count = recv(client, buffer, sizeof(buffer), 0);
    if (count <= 0) break;
    response.bytes.append(buffer, static_cast<std::size_t>(count));
  }
  close(client);
  const auto first_space = response.bytes.find(' ');
  if (first_space != std::string::npos)
    response.status = std::stoi(response.bytes.substr(first_space + 1, 3));
  const auto boundary = response.bytes.find("\r\n\r\n");
  if (boundary != std::string::npos) response.body = response.bytes.substr(boundary + 4);
  return response;
}

std::string Header(const Response& response,std::string_view name) {
  const std::string prefix=std::string(name)+": ";
  const auto begin=response.bytes.find(prefix);
  if(begin==std::string::npos||begin>response.bytes.find("\r\n\r\n"))return {};
  const auto value=begin+prefix.size();
  const auto end=response.bytes.find("\r\n",value);
  return end==std::string::npos?std::string{}:response.bytes.substr(value,end-value);
}

std::pair<std::uint32_t,std::uint32_t> PngSize(const Bytes& bytes) {
  Check(bytes.size()>=24 && std::equal(bytes.begin(),bytes.begin()+8,
      Bytes{137,80,78,71,13,10,26,10}.begin()),"expected PNG signature and dimensions");
  auto read=[&](std::size_t i){return (std::uint32_t(bytes[i])<<24)|
      (std::uint32_t(bytes[i+1])<<16)|(std::uint32_t(bytes[i+2])<<8)|bytes[i+3];};
  return {read(16),read(20)};
}
bool PngResponseMatches(const Response& result,const std::string& expected,
    std::pair<std::uint32_t,std::uint32_t> dimensions) {
  const Bytes bytes(result.body.begin(),result.body.end());
  return result.status==200 && Header(result,"Content-Type")=="image/png" &&
      Sha256(bytes)==expected && Header(result,"X-SeeThis-PNG-SHA256")==expected &&
      Header(result,"ETag")=="\"sha256-"+expected+"\"" && PngSize(bytes)==dimensions;
}
void CheckPngResponse(const Response& result,const std::string& expected,
    std::pair<std::uint32_t,std::uint32_t> dimensions) {
  Check(PngResponseMatches(result,expected,dimensions),
      "ready representation must match its exact core PNG identity, byte hash, headers and dimensions");
}
ReadyAssetResult AwaitAsset(ReferenceStore& store,const std::string& id,unsigned kind) {
  for(unsigned n=0;n<8;++n){
    auto asset=kind==0?store.RequestReadyAnnotated(id):kind==1?store.RequestReadyCrop(id):
        kind==14?store.RequestReadyDisplayUnderlay(id):store.RequestReadyMark(id,kind-2);
    if(asset.state==ReadyAssetState::kReady)return asset;
    Check(asset.state==ReadyAssetState::kPending,"oracle fixture must be pending or ready");
    store.WaitForIdleForTesting();
  }
  Check(false,"core oracle fixture did not make sequential bounded progress");return {};
}

void RunEmbeddedViewerCanvasHarness(std::string_view viewer_script) {
  TempDirectory temp;
  const std::string live_id(32, '4');
  const auto live_root = temp.path / "references";
  {
    ReferenceStore creator(live_root);
    MakeReference(creator, live_id, 0, false, true, 0, false, 10);
  }
  ReferenceStore live_references(live_root);
  ReferenceStoreTestHooks hooks;
  hooks.before_derive_annotated = [](std::string_view) {
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
  };
  live_references.SetTestHooks(hooks);
  SettingsStore live_settings(temp.path / "settings.json");
  const auto live_capability = live_settings.ReferenceCapability(live_id);
  const auto web = temp.path / "web";
  std::filesystem::create_directories(web);
  auto readiness = std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer live_server(
      live_references, live_settings, web, readiness);
  std::string error;
  Check(live_server.Start(&error), "embedded viewer live server starts");
  const auto port = live_server.port();
  const auto viewer_path = temp.path / "reference-view.js";
  const auto harness_path = temp.path / "canvas-harness.js";
  const auto config_path = temp.path / "live.json";
  std::ofstream(viewer_path) << viewer_script;
  std::ofstream(config_path) << "{\"port\":" << port << ",\"id\":\""
      << live_id << "\",\"capability\":\"" << live_capability << "\"}";
  {
    std::ofstream harness(harness_path);
    harness << R"JS((async () => {
const fs = require("fs"), vm = require("vm"), zlib = require("zlib");
const nodeCrypto = require("crypto");
globalThis.crypto = nodeCrypto.webcrypto;
const viewer = fs.readFileSync(process.argv[2], "utf8");
const live = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
const nativeFetch = globalThis.fetch;
const id = "0123456789abcdef0123456789abcdef";
const capability = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
const hash = bytes => nodeCrypto.createHash("sha256").update(bytes).digest("hex");
function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(name, bytes) {
  const type = Buffer.from(name), size = Buffer.alloc(4), crc = Buffer.alloc(4);
  size.writeUInt32BE(bytes.length); crc.writeUInt32BE(crc32(Buffer.concat([type, bytes])));
  return Buffer.concat([size, type, bytes, crc]);
}
function encode(width, height, rgba) {
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width); ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  const rows = [];
  for (let y = 0; y < height; ++y) rows.push(Buffer.from([0]), rgba.subarray(y * width * 4, (y + 1) * width * 4));
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), chunk("IHDR", ihdr),
    chunk("IDAT", zlib.deflateSync(Buffer.concat(rows))), chunk("IEND", Buffer.alloc(0))]);
}
function decode(bytes) {
  bytes = Buffer.from(bytes);
  if (!bytes.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) throw new Error("bad fixture signature");
  let width, height, compressed = [];
  for (let pos = 8; pos < bytes.length;) {
    const length = bytes.readUInt32BE(pos), type = bytes.toString("ascii", pos + 4, pos + 8);
    const data = bytes.subarray(pos + 8, pos + 8 + length);
    if (crc32(bytes.subarray(pos + 4, pos + 8 + length)) !== bytes.readUInt32BE(pos + 8 + length)) throw new Error("bad PNG CRC");
    if (type === "IHDR") {
      width = data.readUInt32BE(); height = data.readUInt32BE(4);
      if (data[8] !== 8 || data[9] !== 6 || data[12] !== 0) throw new Error("expected noninterlaced RGBA8 PNG");
    }
    if (type === "IDAT") compressed.push(data);
    pos += length + 12;
  }
  const rows = zlib.inflateSync(Buffer.concat(compressed)), stride = width * 4;
  if (rows.length !== (stride + 1) * height) throw new Error("bad PNG rows");
  const rgba = Buffer.alloc(stride * height);
  const paeth = (a,b,c) => {const p = a+b-c, pa=Math.abs(p-a), pb=Math.abs(p-b), pc=Math.abs(p-c); return pa<=pb && pa<=pc ? a : pb<=pc ? b : c;};
  for (let y=0; y<height; ++y) {
    const filter = rows[y * (stride + 1)];
    for (let x=0; x<stride; ++x) {
      const i=y*stride+x, a=x>=4 ? rgba[i-4] : 0, b=y ? rgba[i-stride] : 0, c=y && x>=4 ? rgba[i-stride-4] : 0;
      const prediction = [0,a,b,Math.floor((a+b)/2),paeth(a,b,c)][filter];
      if (prediction === undefined) throw new Error("bad PNG filter");
      rgba[i]=(rows[y*(stride+1)+x+1]+prediction)&255;
    }
  }
  return {width, height, rgba, bytes, close() {}};
}
// Every positive display asset below comes from the actual shared core through
// the live authenticated service. No raw PNG, local rasterizer or fake underlay.
const origin=`http://127.0.0.1:${live.port}`;
const headers={Authorization:`Bearer ${live.capability}`};
async function actual(suffix) {
  for(let n=0;n<100;++n) {
    const response=await nativeFetch(`${origin}/v1/references/${live.id}${suffix}`,{headers});
    const bytes=new Uint8Array(await response.arrayBuffer());
    if(response.status===202){await new Promise(r=>setTimeout(r,25));continue;}
    if(response.status!==200)throw new Error(`actual fixture ${suffix} returned ${response.status}`);
    return {bytes,headers:Object.fromEntries(response.headers.entries())};
  }
  throw new Error("actual fixture did not make bounded progress: "+suffix);
}
const first=JSON.parse(new TextDecoder().decode((await actual("")).bytes));
const underlay=await actual("/annotated.png?layers=strokes");
const numbered=await actual("/annotated.png");
const rawSource=await actual("/source.png");
const full=JSON.parse(new TextDecoder().decode((await actual("")).bytes));
const compactResponse=await nativeFetch(`${origin}/r/${live.id}${live.capability}/metadata`,{headers});
const compact=await compactResponse.json();
const png=numbered.bytes, sourcePng=rawSource.bytes, pngDigest=hash(png);
const display=decode(underlay.bytes), marked=decode(numbered.bytes);
if(Buffer.from(underlay.bytes).equals(Buffer.from(numbered.bytes)) ||
   display.rgba.equals(decode(rawSource.bytes).rgba) || display.rgba.equals(marked.rgba))
  throw new Error("genuine core underlay must contain strokes and differ from raw and numbered bytes");
if(first.capture.display_underlay.state!=="pending" || first.capture.display_underlay.sha256!==null)
  throw new Error("live fixture did not exercise pending/null SHA metadata");
const responseFor=(status,body,headers={})=>{
  const bytes=body instanceof Uint8Array?body:new TextEncoder().encode(body);
  return {status,ok:status>=200&&status<300,headers:{get:name=>Object.entries(headers).find(([k])=>k.toLowerCase()===String(name).toLowerCase())?.[1]||null},body:{getReader(){let done=false;return {read:async()=>done?{done:true}:(done=true,{done:false,value:bytes}),cancel:async()=>{done=true;}};}}};
};
const copy=value=>JSON.parse(JSON.stringify(value));
async function run(options={}) {
  let resolveTerminal,statusText="",metadataCalls=0,underlayCalls=0,saveCalls=0,toBlobCalls=0,removals=0,decodeCalls=0,drawn;
  const terminal=new Promise(resolve=>resolveTerminal=resolve);
  const blobs=new Map(),requests=[],listeners={};
  const status={dataset:{}};
  Object.defineProperty(status,"textContent",{get:()=>statusText,set:v=>{statusText=String(v);if(statusText.startsWith("Could not resolve"))resolveTerminal({kind:"failure"});}});
  const save={hidden:true,addEventListener(){},removeAttribute(){this._href="";}};
  Object.defineProperty(save,"href",{get:()=>save._href,set:v=>{save._href=v;resolveTerminal({kind:"success"});}});
  let viewWidth=options.crowded?10:options.narrow?390:1200;
  const rect=()=>({left:0,top:0,width:viewWidth,height:viewWidth*display.height/display.width});
  const overlayOps=[];
  const overlay={scale(){},measureText:text=>({width:text.length*8}),beginPath(){},moveTo(...v){overlayOps.push(["anchor",...v]);},lineTo(){},stroke(){},arc(){},fill(){},fillRect(){},strokeRect(){},fillText(...v){overlayOps.push(["text",...v]);}};
  const numbers={hidden:true,width:0,height:0,style:{},dataset:{},getContext:()=>overlay};
  const context={drawImage:bitmap=>drawn=bitmap,getImageData:()=>({data:drawn.rgba})};
  const elements={status,identity:{textContent:""},numbers,"image-stage":{style:{},getBoundingClientRect:()=>({left:0,top:0,width:viewWidth,height:options.crowded?10:Math.max(180,rect().height)})},image:{hidden:true,style:{},width:0,height:0,getBoundingClientRect:rect,getContext:()=>context,toBlob:cb=>{++toBlobCalls;cb(new Blob([png],{type:"image/png"}));}},save,"crop-save":{hidden:true,addEventListener(){},removeAttribute(){}}};
  globalThis.document={getElementById:id=>elements[id],createElement:()=>({getContext:()=>context})};
  globalThis.location={origin,pathname:`/v1/references/${live.id}/view`,hash:`#cap=${live.capability}`};
  globalThis.history={replaceState(){}};globalThis.sessionStorage={getItem:()=>null,setItem(){},removeItem(){++removals;}};
  globalThis.addEventListener=(n,fn)=>listeners[n]=fn;
  globalThis.URL.createObjectURL=blob=>{const url=`blob:verified-${blobs.size}`;blobs.set(url,blob);return url;};
  globalThis.URL.revokeObjectURL=url=>blobs.delete(url);
  globalThis.createImageBitmap=async blob=>{
    const decoded=decode(new Uint8Array(await blob.arrayBuffer()));++decodeCalls;
    if(options.delayedPagehide||options.delayedSelection||(options.delayedSavePagehide&&decodeCalls===2)){
      if(options.delayedSelection)location.pathname="/v1/references/"+"f".repeat(32)+"/view";
      else listeners.pagehide();
      resolveTerminal({kind:"disposed"});
    }
    if(options.wrongDimensions)decoded.width++;
    return decoded;
  };
  globalThis.fetch=async(url,requestOptions)=>{
    const parsed=new URL(url);requests.push({url:parsed.href,authorization:requestOptions.headers.Authorization});
    if(parsed.pathname===`/r/${live.id}${live.capability}/metadata`){
      const value=copy(compact);
      if(options.delayedCompactPagehide){listeners.pagehide();resolveTerminal({kind:"disposed"});}
      if(options.badPos)value.marks[0].pos.x=-1;
      if(options.duplicate)value.marks[1].mark_id=value.marks[0].mark_id;
      if(options.missing)value.marks.pop();
      if(options.wrongNumber)value.marks[1].number=12;
      if(options.unmatched)value.marks[1].mark_id="f".repeat(32);
      if(options.wrongCompactSource)value.screenshot.sha256="0".repeat(64);
      if(options.wrongCompactDimensions)value.screenshot.width++;
      if(options.wrongCompactID)value.reference_id="f".repeat(32);
      return responseFor(200,JSON.stringify(value),{"content-type":"application/json"});
    }
    if(parsed.pathname===`/v1/references/${live.id}`){
      ++metadataCalls;const value=copy(full);
      const descriptor=value.capture.display_underlay;
      if(options.pending&&metadataCalls===1){descriptor.state="pending";descriptor.sha256=null;}
      if(options.initialVersion&&metadataCalls===1)descriptor.geometry_version="unknown";
      if(options.initialRepresentation&&metadataCalls===1)descriptor.representation="numbered";
      if(options.refreshVersion&&metadataCalls>1)descriptor.geometry_version="unknown";
      if(options.refreshRepresentation&&metadataCalls>1)descriptor.representation="numbered";
      if(options.readyMismatch&&metadataCalls>1)descriptor.sha256="0".repeat(64);
      if(options.missingReadyDigest&&metadataCalls>1)descriptor.sha256=null;
      if(options.wrongSourceUrl)value.capture.source.url=origin+"/other/source.png";
      if(options.wrongReadySource&&metadataCalls>1)value.capture.source.sha256="0".repeat(64);
      if(options.wrongReadyIdentity&&metadataCalls>1)value.reference_id="f".repeat(32);
      if(options.wrongReadyDimensions&&metadataCalls>1)value.capture.source.width++;
      if(options.saveMismatch&&metadataCalls>2)value.capture.annotated.sha256="0".repeat(64);
      if(options.failedMetadata&&metadataCalls>1)return responseFor(422,"{}",{"content-type":"application/json"});
      return responseFor(200,JSON.stringify(value),{"content-type":"application/json"});
    }
    if(parsed.pathname.endsWith("/annotated.png")){
      const isUnderlay=parsed.search==="?layers=strokes";
      if(isUnderlay)++underlayCalls;else ++saveCalls;
      if(options.pending&&isUnderlay&&underlayCalls===1)return responseFor(202,JSON.stringify({reference_id:live.id,state:"deriving",retry_ms:20}),{"content-type":"application/json"});
      if(options.assetStatus&&isUnderlay)return responseFor(options.assetStatus,JSON.stringify({reference_id:live.id,state:"deleted"}),{"content-type":"application/json"});
      const asset=isUnderlay?underlay:numbered;
      const result=responseFor(200,options.corruptBytes&&isUnderlay?rawSource.bytes:asset.bytes,{...asset.headers,...(isUnderlay?options.headers:{})});
      if(options.delayedStreamPagehide&&isUnderlay){
        const read=result.body.getReader;result.body.getReader=()=>{const reader=read();return {...reader,read:async()=>{
          const chunk=await reader.read();if(chunk.done){listeners.pagehide();resolveTerminal({kind:"disposed"});}return chunk;
        }};};
      }
      return result;
    }
    throw new Error("unexpected request "+parsed.href);
  };
  vm.runInThisContext(viewer,{filename:"production-reference-view.js"});
  let timer;const result=await Promise.race([terminal,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error("viewer did not complete")),5000);})]);clearTimeout(timer);
  await new Promise(r=>setTimeout(r,result.kind==="disposed"?50:0));
  const initialLayout=numbers.dataset.layout?JSON.parse(numbers.dataset.layout):[];
  const layouts=[initialLayout];
  if(options.resize)for(const width of [1900,1920,1899,1901,390,1200]){viewWidth=width;listeners.resize();layouts.push(numbers.dataset.layout?JSON.parse(numbers.dataset.layout):[]);}
  const saved=save.href&&blobs.has(save.href)?Buffer.from(await blobs.get(save.href).arrayBuffer()):null;
  if(options.pagehide)listeners.pagehide();
  return {...result,status:statusText,drawn,saved,canvasWidth:elements.image.width,canvasHeight:elements.image.height,blobCount:blobs.size,imageHidden:elements.image.hidden,numbers,initialLayout,layouts,toBlobCalls,requests,metadataCalls,underlayCalls,saveCalls,removals,saveText:save.textContent};
}
const check=(v,message)=>{if(!v)throw new Error(message);};
function delivered(result){
  check(result.kind==="success"&&!result.imageHidden&&result.toBlobCalls===0,"display failed: "+result.status);
  check(result.saved.equals(Buffer.from(numbered.bytes)),"Save must retain exact independently verified numbered bytes");
  check(result.drawn.bytes.equals(Buffer.from(underlay.bytes)),"display must use actual shared stroke-only core PNG");
  check(result.saveText==="Save annotated full-context PNG","Save label changed");
}
for(const options of [{},{narrow:true},{resize:true},{pending:true}]){
  const result=await run(options);delivered(result);
  check(result.layouts.every(layout=>layout.length===compact.marks.length),"all scales and bidirectional resize must keep exactly one complete numbering layer");
  check(result.initialLayout.map(x=>x.number).join(",")===compact.marks.map(x=>x.number).join(","),"immutable slots changed");
  check(result.requests.every(r=>r.authorization===`Bearer ${live.capability}`&&!new URL(r.url).hash),"scoped capability lost");
  check(result.requests.some(r=>r.url.endsWith("?layers=strokes"))&&result.saveCalls===1,"display and Save must fetch separate representations");
}
const reset=await run({pagehide:true});check(reset.imageHidden&&reset.numbers.hidden&&reset.numbers.width===0&&reset.canvasWidth===0&&reset.canvasHeight===0&&reset.blobCount===0,"pagehide retained raster/model/URL");
for(const options of [{delayedPagehide:true},{delayedStreamPagehide:true},{delayedSelection:true},
                      {delayedSavePagehide:true},{delayedCompactPagehide:true}]){
  const delayed=await run(options);check(delayed.kind==="disposed"&&delayed.imageHidden&&!delayed.saved&&
    delayed.canvasWidth===0&&delayed.canvasHeight===0&&delayed.blobCount===0,
    "delayed completion repopulated stale canvas or URL: "+JSON.stringify({options,width:delayed.canvasWidth,height:delayed.canvasHeight,urls:delayed.blobCount}));
}
const crowded=await run({crowded:true});delivered(crowded);check(crowded.numbers.hidden&&crowded.status.includes("Enlarge the view"),"finite capacity must be explicit with valid Save");
const failures=[{initialVersion:true},{initialRepresentation:true},{refreshVersion:true},{refreshRepresentation:true},
  {readyMismatch:true},{missingReadyDigest:true},{wrongReadySource:true},{wrongReadyIdentity:true},{wrongReadyDimensions:true},
  {saveMismatch:true},{headers:{etag:'"wrong"'}},{headers:{"x-seethis-png-sha256":""}},{corruptBytes:true},
  {assetStatus:422},{assetStatus:401},{assetStatus:403},{assetStatus:410},{failedMetadata:true},
  {wrongSourceUrl:true},{badPos:true},{duplicate:true},{missing:true},{wrongNumber:true},{unmatched:true},{wrongCompactSource:true},{wrongCompactDimensions:true},{wrongCompactID:true},{wrongDimensions:true}];
for(const options of failures){const result=await run(options);check(result.kind==="failure"&&result.imageHidden&&!result.saved&&result.numbers.hidden,"invalid bundle accepted: "+JSON.stringify(options));}
console.log(JSON.stringify({core_underlay_sha256:hash(underlay.bytes),numbered_save_sha256:hash(numbered.bytes),negative_integrity_cases:failures.length,all_scale_number_layer:true,actual_core_fixture:true}));
})().catch(error=>{console.error(error);process.exitCode=1;});
)JS";
  }
  const std::string command = "/usr/bin/env node '" + harness_path.string() +
      "' '" + viewer_path.string() + "' '" + config_path.string() + "'";
  const int result = std::system(command.c_str());
  Check(result != -1 && WIFEXITED(result) && WEXITSTATUS(result) == 0,
        "embedded viewer verifies PNG pixels, exact download bytes, and live derived auth");
}

std::string Get(std::string_view target, std::string_view token = {}) {
  std::string request = "GET " + std::string(target) + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
  if (!token.empty()) request += "Authorization: Bearer " + std::string(token) + "\r\n";
  return request + "\r\n";
}

std::string ReferenceGet(std::uint16_t port, std::string_view target,
                         std::string_view token = {},
                         std::optional<std::string_view> origin = std::nullopt,
                         std::optional<std::string_view> host = std::nullopt) {
  std::string request = "GET " + std::string(target) +
      " HTTP/1.1\r\nHost: " +
      std::string(host.value_or(std::string_view{}).empty()
                      ? "127.0.0.1:" + std::to_string(port)
                      : std::string(*host)) +
      "\r\n";
  if (origin) request += "Origin: " + std::string(*origin) + "\r\n";
  if (!token.empty())
    request += "Authorization: Bearer " + std::string(token) + "\r\n";
  return request + "\r\n";
}

std::string CompactSourceObject(const std::string& body);

// A real persisted three-gesture capture, read through the HTTP server. The
// steady ready path is measured separately from setup/cold retries and optional
// discovery. Node decodes JSON before selecting the one supplied image URL.
void TestMultiShotDelivery() {
  TempDirectory temp;ReferenceStore store(temp.path/"references");
  SettingsStore settings(temp.path/"settings.json");
  const std::string id(32,'a'),shot(32,'b'),other(32,'c');
  const auto initial=MakeReference(store,id,0,false,true);
  MakeReference(store,other,0,false,true);
  auto metadata=initial.value;metadata.source={};metadata.crop={};
  metadata.context.window_title="Second captured context";
  CaptureResult capture;capture.requested=initial.value.requested;capture.completed=initial.value.image_completed;
  capture.pixels={100,100,Bytes(100*100*4,255)};
  AppendResult appended;
  store.AppendAsync(id,StableMarkId(id,0),1,shot,metadata,capture,[&](auto value){appended=value;});
  store.WaitForIdleForTesting();Check(appended.status==AppendStatus::kCommitted,"multi HTTP fixture appended");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(store,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"owned multi HTTP server starts");
  const auto root="/r/"+id+settings.ReferenceCapability(id);
  const auto foreign="/r/"+other+settings.ReferenceCapability(other);
  const auto get=[&](const std::string& path){return Request(server.port(),ReferenceGet(server.port(),path));};
  const auto response=get(root);
  Check(response.status==200&&response.body.find("\"delivery_schema\":4")!=std::string::npos&&
      response.body.find("\"revision\":2")!=std::string::npos&&response.body.find("Second captured context")!=std::string::npos,
      "default delivers complete versioned collection");
  Check(response.body.find("\"shot_id\":\""+id+"\"")!=std::string::npos&&
      response.body.find("\"shot_id\":\""+shot+"\"")!=std::string::npos&&
      response.body.find(StableMarkId(id,3))!=std::string::npos,"schema4 shot and global mark relationships");
  for(const auto* raw:{"\"anchor\"","\"path\"","\"crop\"","/source.png","/marks/"})
    Check(response.body.find(raw)==std::string::npos,"default excludes raw coordinates and redundant assets");
  Check(get(root+"/metadata").body==response.body,"metadata represents whole collection");
  Check(get(root+"/source.png").status==409&&get(root+"/annotated.png").status==409&&get(root+"/record").status==409,"ambiguous singular resources require shot");
  auto image=get(root+"/shots/"+shot+"/annotated.png");store.WaitForIdleForTesting();
  image=get(root+"/shots/"+shot+"/annotated.png");
  Check(image.status==200&&Header(image,"X-SeeThis-PNG-SHA256")==Sha256(Bytes(image.body.begin(),image.body.end())),"root capability serves exact member shot PNG");
  Check(get(foreign+"/shots/"+shot+"/annotated.png").status==404,"other root cannot retrieve member shot");
  server.Stop();
}

// Execute the served collection viewer against owned synthetic HTTP fixtures.
// The DOM/canvas adapter observes production handlers, decoded rasters and Save
// bytes; it is not a browser-engine layout test.
void TestMultiShotViewer() {
  TempDirectory temp;
  ReferenceStore store(temp.path/"references");
  SettingsStore settings(temp.path/"settings.json");
  const std::string id(32,'a'), foreign(32,'f'), two_id(32,'e');
  auto firstContext=ContextFixture(Now());firstContext.window_title="Captured A";
  const auto initial=MakeReference(store,id,0,false,false,0,false,0,&firstContext);
  const auto two=MakeReference(store,two_id,0,false,true);
  MakeReference(store,foreign,0);
  for(int index=1;index<7;++index) {
    auto metadata=index==1?two.value:initial.value;
    metadata.source={};metadata.crop={};metadata.operation_started=Now();
    metadata.context.window_title=(index==3||index==4)?"Captured B":"Captured A";
    CaptureResult capture;capture.requested=Now();capture.completed=Now();
    capture.pixels={100,100,Bytes(100*100*4)};
    for(std::size_t pixel=0;pixel<capture.pixels.rgba.size();++pixel)
      capture.pixels.rgba[pixel]=pixel%4==3?255:static_cast<std::uint8_t>(pixel*13+index*29);
    AppendResult appended;
    store.AppendReferenceAsync(id,index,std::string(32,static_cast<char>('0'+index)),
        metadata,std::move(capture),[&](auto value){appended=value;});
    store.WaitForIdleForTesting();
    if(appended.status!=AppendStatus::kCommitted)std::cerr<<"viewer append index="<<index<<" status="<<static_cast<int>(appended.status)<<" revision="<<appended.revision<<"\n";
    Check(appended.status==AppendStatus::kCommitted,"seven-shot viewer fixture appends");
  }
  const std::vector<std::string> notes={"", "世界 🌍\nsecond line", "<b>plain note</b>",
      "Captured B five", "Captured B six", "quote \" and slash \\", "seven", "eight"};
  for(std::size_t slot=0;slot<notes.size();++slot) {
    const auto revision=store.Collection(id).notes_revision;
    Check(store.UpdateMarkNote(id,StableMarkId(id,slot),notes[slot],revision).status==
        MarkNoteUpdateStatus::kUpdated,"collection fixture note commits");
  }
  const auto snapshot=store.Collection(id);
  Check(snapshot.revision==7&&snapshot.shots.size()==7&&snapshot.notes.size()==8,
      "viewer fixture is exactly revision7 seven shots eight marks");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(store,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"collection viewer synthetic server starts");
  const auto html=Request(server.port(),ReferenceGet(server.port(),"/v1/references/"+id+"/view"));
  const auto script=Request(server.port(),ReferenceGet(server.port(),"/v1/reference-view.js"));
  Check(html.status==200&&script.status==200,"collection harness receives actual served HTML and JS");
  const auto html_path=temp.path/"view.html", script_path=temp.path/"view.js";
  const auto config_path=temp.path/"viewer.json", harness_path=temp.path/"collection-viewer.js";
  std::ofstream(html_path)<<html.body;std::ofstream(script_path)<<script.body;
  const auto writeConfig=[&](int expectedShots) {
    std::ofstream(config_path)<<"{\"origin\":\""<<server.base_url()<<"\",\"id\":\""<<id
        <<"\",\"capability\":\""<<settings.ReferenceCapability(id)<<"\",\"foreign\":\""<<foreign
        <<"\",\"foreignCapability\":\""<<settings.ReferenceCapability(foreign)
        <<"\",\"expectedShots\":"<<expectedShots<<'}';
  };
  writeConfig(7);
  {
    std::ofstream harness(harness_path);
    harness<<R"JS((async()=>{
const fs=require("fs"),vm=require("vm"),zlib=require("zlib"),nodeCrypto=require("crypto");
const fixture=JSON.parse(fs.readFileSync(process.argv[2],"utf8"));
const html=fs.readFileSync(process.argv[3],"utf8"), viewer=fs.readFileSync(process.argv[4],"utf8");
const check=(value,message)=>{if(!value)throw new Error(message);};
const copy=value=>JSON.parse(JSON.stringify(value));
const hash=bytes=>nodeCrypto.createHash("sha256").update(bytes).digest("hex");
)JS";
    harness<<R"JS(function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(name, bytes) {
  const type = Buffer.from(name), size = Buffer.alloc(4), crc = Buffer.alloc(4);
  size.writeUInt32BE(bytes.length); crc.writeUInt32BE(crc32(Buffer.concat([type, bytes])));
  return Buffer.concat([size, type, bytes, crc]);
}
function encode(width, height, rgba) {
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width); ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  const rows = [];
  for (let y = 0; y < height; ++y) rows.push(Buffer.from([0]), rgba.subarray(y * width * 4, (y + 1) * width * 4));
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), chunk("IHDR", ihdr),
    chunk("IDAT", zlib.deflateSync(Buffer.concat(rows))), chunk("IEND", Buffer.alloc(0))]);
}
function decode(bytes) {
  bytes = Buffer.from(bytes);
  if (!bytes.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) throw new Error("bad fixture signature");
  let width, height, compressed = [];
  for (let pos = 8; pos < bytes.length;) {
    const length = bytes.readUInt32BE(pos), type = bytes.toString("ascii", pos + 4, pos + 8);
    const data = bytes.subarray(pos + 8, pos + 8 + length);
    if (crc32(bytes.subarray(pos + 4, pos + 8 + length)) !== bytes.readUInt32BE(pos + 8 + length)) throw new Error("bad PNG CRC");
    if (type === "IHDR") {
      width = data.readUInt32BE(); height = data.readUInt32BE(4);
      if (data[8] !== 8 || data[9] !== 6 || data[12] !== 0) throw new Error("expected noninterlaced RGBA8 PNG");
    }
    if (type === "IDAT") compressed.push(data);
    pos += length + 12;
  }
  const rows = zlib.inflateSync(Buffer.concat(compressed)), stride = width * 4;
  if (rows.length !== (stride + 1) * height) throw new Error("bad PNG rows");
  const rgba = Buffer.alloc(stride * height);
  const paeth = (a,b,c) => {const p = a+b-c, pa=Math.abs(p-a), pb=Math.abs(p-b), pc=Math.abs(p-c); return pa<=pb && pa<=pc ? a : pb<=pc ? b : c;};
  for (let y=0; y<height; ++y) {
    const filter = rows[y * (stride + 1)];
    for (let x=0; x<stride; ++x) {
      const i=y*stride+x, a=x>=4 ? rgba[i-4] : 0, b=y ? rgba[i-stride] : 0, c=y && x>=4 ? rgba[i-stride-4] : 0;
      const prediction = [0,a,b,Math.floor((a+b)/2),paeth(a,b,c)][filter];
      if (prediction === undefined) throw new Error("bad PNG filter");
      rgba[i]=(rows[y*(stride+1)+x+1]+prediction)&255;
    }
  }
  return {width, height, rgba, bytes, close() {}};
}
)JS";
    harness<<R"JS(
const base=`${fixture.origin}/v1/references/${fixture.id}`;
const root=`${fixture.origin}/r/${fixture.id}${fixture.capability}`;
let ownedPendingResponses=0;
async function actual(url,token=fixture.capability) {
  for(let attempt=0;attempt<80;++attempt) {
    const response=await fetch(url,{headers:{Authorization:`Bearer ${token}`}});
    const bytes=new Uint8Array(await response.arrayBuffer());
    if(response.status===202){
      if(url.includes("/shots/")) {
        const pending=JSON.parse(Buffer.from(bytes).toString());
        const shotID=new URL(url).pathname.split("/").at(-2);
        const expectedRoot=url.startsWith(base)||url.startsWith(root)?fixture.id:fixture.foreign;
        check(pending.reference_id===expectedRoot&&pending.shot_id===shotID&&pending.state==="pending","actual pending shot response identity");
        ++ownedPendingResponses;
      }
      await new Promise(r=>setTimeout(r,5));continue;
    }
    return {status:response.status,bytes,headers:Object.fromEntries(response.headers.entries())};
  }
  throw new Error("synthetic asset did not complete");
}
const initialResponse=await actual(base), collection=JSON.parse(Buffer.from(initialResponse.bytes).toString());
check(collection.delivery_schema===4&&collection.revision===fixture.expectedShots&&collection.shots.length===fixture.expectedShots&&collection.marks.length===fixture.expectedShots+1,"actual collection fixture identity");
const latest=collection.shots.length-1;
const groups=collection.shots.map(shot=>collection.marks.filter(mark=>mark.shot_id===shot.shot_id).map(mark=>mark.number));
const expectedGroups=[[1],[2,3],[4],[5],[6],[7],[8]];
if(fixture.expectedShots===8)expectedGroups.push([9]);
check(JSON.stringify(groups)===JSON.stringify(expectedGroups),"normative capture-order global mark groups");
check(collection.shots.slice(0,7).map(shot=>shot.source_context.window_title).join(",")==="Captured A,Captured A,Captured A,Captured B,Captured B,Captured A,Captured A","capture context order differs from fixture");
const assets=new Map();
for(const shot of collection.shots)assets.set(shot.shot_id,await actual(shot.image.url));
const foreignAsset=await actual(`${fixture.origin}/v1/references/${fixture.foreign}/shots/${fixture.foreign}/annotated.png`,fixture.foreignCapability);
const foreignOwnership=await actual(`${fixture.origin}/r/${fixture.foreign}${fixture.foreignCapability}/shots/${collection.shots[1].shot_id}/annotated.png`,fixture.foreignCapability);
check(foreignOwnership.status===404,"actual server rejects foreign root membership");
const badCapability=await actual(`${base}/shots/${collection.shots[1].shot_id}/annotated.png`,fixture.foreignCapability);
check(badCapability.status===403,"actual server rejects another root capability");
const responseFor=(status,bytes,headers={})=>new Response(bytes,{status,headers});
const jsonResponse=(status,value)=>responseFor(status,JSON.stringify(value),{"content-type":"application/json"});
const wait=async predicate=>{for(let i=0;i<2500;++i){if(predicate())return;await new Promise(r=>setTimeout(r,2));}throw new Error("viewer did not settle");};
class Element {
  constructor(tag="div"){this.tagName=tag.toUpperCase();this.hidden=true;this.children=[];this.dataset={};this.style={};this.listeners={};this.textContent="";this.width=this.height=0;this.value="";}
  append(...children){this.children.push(...children);}
  appendChild(child){this.append(child);return child;}
  replaceChildren(...children){this.children=[...children];this.textContent="";}
  addEventListener(name,fn){this.listeners[name]=fn;}
  removeAttribute(name){delete this[name];}
  getBoundingClientRect(){return {left:0,top:0,width:500,height:500};}
}
async function run(options={}) {
  const elements={},listeners={},requests=[],blobs=new Map();
  for(const match of html.matchAll(/<([a-z]+)\b[^>]*\bid="([^"]+)"[^>]*>/g))elements[match[2]]=new Element(match[1]);
  const image=elements.image, save=elements.save, status=elements.status;
  let drawn=null,metadataCalls=0,assetCalls=0,removals=0,hold=null,decodes=0;
  const ctx={drawImage:bitmap=>drawn=bitmap,getImageData:()=>({data:drawn.rgba})};
  image.getContext=()=>ctx;image.toBlob=cb=>cb(new Blob([drawn.bytes],{type:"image/png"}));
  elements.numbers.getContext=()=>({});
  const location={origin:fixture.origin,pathname:`/v1/references/${fixture.id}/view`,hash:`#cap=${fixture.capability}`};
  const urlClass=class extends URL {};
  urlClass.createObjectURL=blob=>{const url=`blob:collection-${blobs.size}-${Math.random()}`;blobs.set(url,blob);return url;};
  urlClass.revokeObjectURL=url=>blobs.delete(url);
  const defer=async(value,kind)=>{
    if(options.defer===kind&&!hold){return await new Promise(resolve=>{hold={release:()=>resolve(value)};});}
    return value;
  };
  const sandbox={console,URL:urlClass,Blob,TextEncoder,TextDecoder,Uint8Array,AbortController,
    crypto:nodeCrypto.webcrypto,performance,Date,Number,Map,Set,JSON,Math,Promise,location,
    setTimeout:(fn,ms)=>setTimeout(fn,options.fast?Math.min(ms,1):ms),clearTimeout,
    history:{replaceState(){}},sessionStorage:{getItem:()=>null,setItem(){},removeItem(){++removals;}},
    document:{getElementById:id=>elements[id],createElement:tag=>new Element(tag)},
    addEventListener:(name,fn)=>listeners[name]=fn,
    createImageBitmap:async blob=>{
      let bitmap=decode(new Uint8Array(await blob.arrayBuffer()));++decodes;
      if(options.wrongDimensions)++bitmap.width;
      if(decodes===1)bitmap=await defer(bitmap,"decode");
      return bitmap;
    },
    fetch:async(url,requestOptions)=>{
      const parsed=new URL(url);requests.push({url:parsed.href,options:requestOptions});
      check(requestOptions.headers.Authorization===`Bearer ${fixture.capability}`&&requestOptions.credentials==="omit"&&requestOptions.redirect==="error","viewer request scope/security changed");
      if(parsed.href===base){
        ++metadataCalls;
        if(options.metadataStatus)return jsonResponse(options.metadataStatus,{reference_id:fixture.id,state:options.metadataState||"failed",code:"fixture_failure",retry_ms:20});
        let value=copy(collection);
        if(options.mutate)value=options.mutate(value,metadataCalls);
        let response=jsonResponse(200,value);
        if(metadataCalls===2)response=await defer(response,"refresh");
        return response;
      }
      const shot=collection.shots.find(item=>item.image.url===parsed.href);
      check(shot,"viewer fetched an undeclared or redundant resource: "+parsed.href);
      ++assetCalls;
      if(options.pendingForever||(options.pendingOnce&&assetCalls===1))return jsonResponse(202,{reference_id:options.pendingWrongRoot?fixture.foreign:fixture.id,shot_id:options.pendingWrongShot?fixture.foreign:shot.shot_id,state:"pending",retry_ms:20});
      if(options.assetStatus)return jsonResponse(options.assetStatus,{reference_id:fixture.id,state:options.assetState||"failed"});
      let asset=options.foreign?foreignAsset:assets.get(shot.shot_id);
      if(options.live)asset=await actual(parsed.href);
      let headers={...asset.headers,...options.headers};
      if(options.omitHeader)delete headers[options.omitHeader];
      let bytes=asset.bytes;
      if(options.badSignature){bytes=new Uint8Array(bytes);bytes[0]=0;headers["x-seethis-png-sha256"]=hash(bytes);headers.etag=`"sha256-${hash(bytes)}"`;}
      if(options.corruptBytes){bytes=new Uint8Array(bytes);bytes[bytes.length-1]^=1;}
      let response=responseFor(asset.status,bytes,headers);
      if(assetCalls===1)response=await defer(response,"asset");
      return response;
    }};
  vm.runInNewContext(viewer,sandbox,{filename:"actual-served-reference-view.js"});
  const done=()=>status.textContent.startsWith("Could not resolve")||(!image.hidden&&!save.hidden);
  const ready=async()=>{await wait(done);return !image.hidden&&!save.hidden;};
  const select=async index=>{
    const select=elements["shot-select"];check(select&&select.listeners.change,"actual HTML/JS shot control missing");
    select.value=collection.shots[index].shot_id;select.listeners.change({target:select});
    await ready();
  };
  const result={elements,listeners,requests,blobs,ready,select,get drawn(){return drawn;},get hold(){return hold;},get removals(){return removals;},status};
  if(!options.defer)await ready();
  return result;
}
async function delivered(result,index){
  check(!result.elements.image.hidden&&!result.elements.save.hidden,"schema4 viewer rejected actual collection: "+result.status.textContent);
  const shot=collection.shots[index], expected=assets.get(shot.shot_id);
  check(Buffer.from(result.drawn.bytes).equals(Buffer.from(expected.bytes)),"canvas showed wrong selected shot");
  check(Buffer.from(await result.blobs.get(result.elements.save.href).arrayBuffer()).equals(Buffer.from(expected.bytes)),"Save differs from verified selected shot bytes");
  const rows=result.elements["mark-notes"].children, marks=collection.marks.filter(mark=>mark.shot_id===shot.shot_id);
  check(rows.length===marks.length&&rows.every((row,i)=>row.dataset.markId===marks[i].mark_id&&row.dataset.number===String(marks[i].number)&&row.textContent===`#${marks[i].number} ${marks[i].note}`),"global stable mark/number/plain note rows mismatch");
  check(result.elements["shot-select"].children.map(item=>item.value).join(",")===collection.shots.map(item=>item.shot_id).join(","),"history capture order changed");
  check(result.elements.numbers.hidden,"collection must not add second numbering layer");
}
const positive=await run({live:true});await delivered(positive,latest);
check(positive.requests.filter(item=>item.url!==base).length===1,"open fetched more than selected shot");
for(const index of [...collection.shots.keys(),...Array.from(collection.shots.keys()).reverse()]){await positive.select(index);await delivered(positive,index);}
await delivered(await run(),latest);
if(fixture.expectedShots===8){
  check(groups.at(-1).join(",")==="9","appended latest shot/global number missing on reopen");
  console.log(JSON.stringify({append_reopen_latest:true,shots:8,marks:9,revision:8,all_shots_accessible:true}));return;
}
for(const options of [{pendingOnce:true},{pendingOnce:true,pendingWrongRoot:true},{pendingOnce:true,pendingWrongShot:true}]){
  const result=await run(options);
  if(options.pendingWrongRoot||options.pendingWrongShot)check(result.elements.image.hidden,"wrong pending identity accepted");
  else await delivered(result,latest);
}
const failures=[
  {mutate:value=>({...value,reference_id:fixture.foreign})},
  {mutate:value=>{value.shots.at(-1).image.url=value.shots.at(-1).image.url.replace(fixture.id,fixture.foreign);return value;}},
  {mutate:value=>{value.shots.at(-1).image.url+="?unexpected=1";return value;}},
  {mutate:value=>{value.shots.at(-1).image.representation="source";return value;}},
  {mutate:value=>{value.shots.at(-1).image.source_sha256="bad";return value;}},
  {mutate:value=>{value.shots.at(-1).image.source_sha256="0".repeat(64);return value;}},
  {mutate:value=>{value.shots.at(-1).image.url=value.shots.at(-1).image.url.replace(fixture.capability,"0".repeat(64));return value;}},
  {mutate:value=>{value.shots.at(-1).image.url=value.shots.at(-1).image.url.replace(fixture.origin,"https://foreign.invalid");return value;}},
  {mutate:value=>{value.shots.at(-1).image.width++;return value;}},
  {mutate:value=>{value.shots[1].shot_id=value.shots[0].shot_id;return value;}},
  {mutate:value=>{value.marks[1].mark_id=value.marks[0].mark_id;return value;}},
  {mutate:value=>{value.marks[1].number=99;return value;}},
  {mutate:value=>{value.marks[0].shot_id=fixture.foreign;return value;}},
  {mutate:value=>{value.marks.pop();return value;}},
  {mutate:value=>{value.marks[0].note={html:"bad"};return value;}},
  {mutate:(value,n)=>{if(n>1)value.revision++;return value;}},
  {mutate:(value,n)=>{if(n>1)value.notes_revision++;return value;}},
  {mutate:(value,n)=>{if(n>1)value.marks[0].note="newer";return value;}},
  {mutate:value=>{value.lifecycle.expires_utc_us=1;return value;}},
  {headers:{"content-type":"image/jpeg"}}, {headers:{etag:'"wrong"'}},
  {headers:{"x-seethis-png-sha256":"0".repeat(64)}},{corruptBytes:true},{badSignature:true},{wrongDimensions:true},
  {foreign:true},
  ...["x-seethis-reference-id","x-seethis-shot-id","x-seethis-source-sha256"].flatMap(name=>[{omitHeader:name},{headers:{[name]:"f".repeat(name.endsWith("sha256")?64:32)}}]),
  {pendingForever:true,fast:true},
  ...[401,403,410,422].map(assetStatus=>({assetStatus,assetState:assetStatus===410?"deleted":"failed"})),
  ...[[401,"failed"],[403,"failed"],[410,"deleted"],[410,"expired"],[422,"failed"],[202,"pending"]].map(([metadataStatus,metadataState])=>({metadataStatus,metadataState,fast:true}))
];
for(const options of failures){const result=await run(options);check(result.elements.image.hidden&&result.elements.save.hidden&&result.elements["mark-notes"].children.length===0,"invalid collection bundle accepted: "+JSON.stringify(options));if([401,403,410].includes(options.assetStatus)||[401,403,410].includes(options.metadataStatus))check(result.removals>0,"terminal/auth failure retained stored credential");}
for(const defer of ["asset","decode","refresh"]){
  const stale=await run({defer});await wait(()=>stale.hold);
  await stale.select(0);await delivered(stale,0);
  stale.hold.release();await new Promise(r=>setTimeout(r,20));await delivered(stale,0);
  const disposed=await run({defer});await wait(()=>disposed.hold);disposed.listeners.pagehide();disposed.hold.release();
  await new Promise(r=>setTimeout(r,20));check(disposed.elements.image.hidden&&disposed.elements.save.hidden&&disposed.elements.image.width===0&&disposed.elements["mark-notes"].children.length===0&&disposed.blobs.size===0,"late completion repopulated disposed collection");
}
positive.listeners.pagehide();check(positive.blobs.size===0&&positive.elements.image.width===0,"collection teardown leaked verified raster");
console.log(JSON.stringify({actual_served_html_js:true,shots:7,marks:8,revision:7,groups,owned_pending_responses:ownedPendingResponses,all_shots_forward_backward:true,negative_cases:failures.length,stale_phases:3,dom_canvas_adapter:true,browser_engine_layout:false}));
})().catch(error=>{console.error(error);process.exitCode=1;});
)JS";
  }
  const auto command="/usr/bin/env node '"+harness_path.string()+"' '"+config_path.string()+"' '"+
      html_path.string()+"' '"+script_path.string()+"'";
  const int result=std::system(command.c_str());
  Check(result!=-1&&WIFEXITED(result)&&WEXITSTATUS(result)==0,
      "actual served collection viewer keeps all shot images, global note rows and verified Save bound");
  auto metadata=initial.value;metadata.source={};metadata.crop={};metadata.operation_started=Now();
  metadata.context.window_title="Captured A";
  CaptureResult capture;capture.requested=Now();capture.completed=Now();
  capture.pixels={100,100,Bytes(100*100*4,255)};
  AppendResult appended;
  store.AppendReferenceAsync(id,7,std::string(32,'7'),metadata,std::move(capture),
      [&](auto value){appended=value;});
  store.WaitForIdleForTesting();
  Check(appended.status==AppendStatus::kCommitted,"owned eighth shot appends after first viewer session");
  const auto revision=store.Collection(id).notes_revision;
  Check(store.UpdateMarkNote(id,StableMarkId(id,8),"new ninth mark",revision).status==
      MarkNoteUpdateStatus::kUpdated,"ninth global mark note commits");
  writeConfig(8);
  const int reopened=std::system(command.c_str());
  Check(reopened!=-1&&WIFEXITED(reopened)&&WEXITSTATUS(reopened)==0,
      "reopening takes fresh revision8 snapshot and selects newly appended eighth shot/global mark9");
  server.Stop();
}

void TestSingleImageDelivery() {
  TempDirectory temp;
  const auto directory=temp.path/"references";
  ReferenceStore store(directory);
  SettingsStore settings(temp.path/"settings.json");
  const std::string id(32,'9');
  auto context=ContextFixture(Now(),640,400);
  context.app_name="Capture fixture 世界";
  context.window_title="Three gestures";
  MarkController marks(store,[](const auto&){return true;});
  const auto generation=marks.Begin(id);
  Check(marks.BindContext(generation,context),"single-image fixture binds captured context");
  CaptureResult capture;
  capture.requested={context.observed.utc_us+10,context.observed.monotonic_us+10};
  capture.completed={context.observed.utc_us+20,context.observed.monotonic_us+20};
  capture.pixels={640,400,Bytes(640*400*4)};
  for(unsigned y=0;y<400;++y)for(unsigned x=0;x<640;++x) {
    const auto at=(y*640+x)*4;
    capture.pixels.rgba[at]=20+x%101;capture.pixels.rgba[at+1]=30+y%103;
    capture.pixels.rgba[at+2]=90;capture.pixels.rgba[at+3]=255;
  }
  marks.Captured(generation,std::move(capture));
  const auto point=[](double x,double y) {
    return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};
  };
  std::vector<std::vector<DisplayPoint>> paths={
      {point(70,80),point(170,90)}, {}, {point(450,300),point(550,320)}};
  for(unsigned i=0;i<=32;++i) {
    const double angle=i*2*std::acos(-1.0)/32;
    paths[1].push_back(point(300+50*std::cos(angle),200+50*std::sin(angle)));
  }
  marks.Release(generation,paths,
      {context.observed.utc_us+30,context.observed.monotonic_us+30});
  store.WaitForIdleForTesting();
  const auto stored=store.Lookup(id);
  Check(stored&&stored->ready&&stored->ready->value.schema==2,
        "three real mixed line/circle gestures persist schema2");
  const auto& reference=stored->ready->value;
  Check(store.UpdateMarkNote(id,StableMarkId(id,1),"世界 🌍\nsecond line\n\"quoted\"\\",0)
            .status==MarkNoteUpdateStatus::kUpdated&&
        store.UpdateMarkNote(id,StableMarkId(id,2),"line\tthree",1)
            .status==MarkNoteUpdateStatus::kUpdated,
        "Unicode/multiline notes use authoritative revision API; first remains empty");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(store,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"single-image HTTP fixture starts");
  const auto root="/r/"+id+settings.ReferenceCapability(id);
  auto get=[&](std::string_view target) {
    return Request(server.port(),ReferenceGet(server.port(),target));
  };
  // No renderer has been requested yet: cold metadata itself queues canonical PNG.
  const auto cold=get(root);
  Check(cold.status==202&&cold.body.find("\"delivery_schema\":3")!=std::string::npos&&
        cold.body.find("\"state\":\"pending\"")!=std::string::npos&&
        cold.body.find("\"lifecycle\":{\"state\":\"ready\"")!=std::string::npos&&
        cold.body.find("\"sha256\":null")!=std::string::npos&&
        cold.body.find("\"retry_ms\":25")!=std::string::npos,
        "cold default queues canonical image and remains202/null digest with ready lifecycle");
  store.WaitForIdleForTesting();
  const auto canonical=store.RequestReadyAnnotated(id);
  Check(canonical.state==ReadyAssetState::kReady&&canonical.bytes&&
        canonical.byte_sha256==Sha256(*canonical.bytes),
        "canonical ready PNG and actual byte digest exist before two-GET measurement");
  const auto source_path=temp.path/"source.png";
  {std::ofstream out(source_path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(reference.source.bytes.data()),reference.source.bytes.size());}
  const auto config_path=temp.path/"config.json";
  {
    const auto snapshot=store.MarkSnapshots(id);
    std::ofstream out(config_path);
    out<<"{\"origin\":\""<<server.base_url()<<"\",\"root\":\""<<root
       <<"\",\"id\":\""<<id<<"\",\"sha256\":\""<<canonical.byte_sha256
       <<"\",\"source_sha256\":\""<<reference.source.sha256
       <<"\",\"source\":\""<<source_path.string()<<"\",\"marks\":[";
    for(std::size_t i=0;i<snapshot.marks.size();++i) {
      if(i)out<<',';
      out<<"{\"mark_id\":\""<<snapshot.marks[i].id<<"\",\"number\":"<<snapshot.marks[i].number<<'}';
    }
    out<<"],\"context_observed\":"<<context.observed.utc_us
       <<",\"image_completed\":"<<reference.image_completed.utc_us<<'}';
  }
  const auto harness_path=temp.path/"single-image.js";
  {
    std::ofstream harness(harness_path);
    harness << R"JS((async () => {
const fs=require("fs"), net=require("net"), zlib=require("zlib"), crypto=require("crypto");
const fixture=JSON.parse(fs.readFileSync(process.argv[2],"utf8"));
const assert=(ok,message)=>{if(!ok)throw new Error(message);};
const hash=bytes=>crypto.createHash("sha256").update(bytes).digest("hex");
let requestCount=0;
async function get(url,authorization) {
  ++requestCount;
  const target=new URL(url);
  return await new Promise((resolve,reject)=>{
    const chunks=[];
    const socket=net.createConnection({host:target.hostname,port:target.port},()=>{
      socket.end(`GET ${target.pathname} HTTP/1.1\r\nHost: ${target.host}\r\n${authorization?`Authorization: Bearer ${authorization}\r\n`:""}\r\n`);
    });
    socket.setTimeout(5000,()=>socket.destroy(new Error("HTTP fixture timeout")));
    socket.on("data",chunk=>chunks.push(chunk));socket.on("error",reject);
    socket.on("end",()=>{
      const raw=Buffer.concat(chunks), boundary=raw.indexOf("\r\n\r\n");
      const lines=raw.subarray(0,boundary).toString().split("\r\n"), headers={};
      for(const line of lines.slice(1)) {const at=line.indexOf(":");headers[line.slice(0,at).toLowerCase()]=line.slice(at+1).trim();}
      resolve({status:Number(lines[0].split(" ")[1]),headers,body:raw.subarray(boundary+4),transfer:raw.length});
    });
  });
}
)JS";
    harness << R"PNG(function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function decode(bytes) {
  bytes = Buffer.from(bytes);
  if (!bytes.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) throw new Error("bad fixture signature");
  let width, height, compressed = [];
  for (let pos = 8; pos < bytes.length;) {
    const length = bytes.readUInt32BE(pos), type = bytes.toString("ascii", pos + 4, pos + 8);
    const data = bytes.subarray(pos + 8, pos + 8 + length);
    if (crc32(bytes.subarray(pos + 4, pos + 8 + length)) !== bytes.readUInt32BE(pos + 8 + length)) throw new Error("bad PNG CRC");
    if (type === "IHDR") {
      width = data.readUInt32BE(); height = data.readUInt32BE(4);
      if (data[8] !== 8 || data[9] !== 6 || data[12] !== 0) throw new Error("expected noninterlaced RGBA8 PNG");
    }
    if (type === "IDAT") compressed.push(data);
    pos += length + 12;
  }
  const rows = zlib.inflateSync(Buffer.concat(compressed)), stride = width * 4;
  if (rows.length !== (stride + 1) * height) throw new Error("bad PNG rows");
  const rgba = Buffer.alloc(stride * height);
  const paeth = (a,b,c) => {const p = a+b-c, pa=Math.abs(p-a), pb=Math.abs(p-b), pc=Math.abs(p-c); return pa<=pb && pa<=pc ? a : pb<=pc ? b : c;};
  for (let y=0; y<height; ++y) {
    const filter = rows[y * (stride + 1)];
    for (let x=0; x<stride; ++x) {
      const i=y*stride+x, a=x>=4 ? rgba[i-4] : 0, b=y ? rgba[i-stride] : 0, c=y && x>=4 ? rgba[i-stride-4] : 0;
      const prediction = [0,a,b,Math.floor((a+b)/2),paeth(a,b,c)][filter];
      if (prediction === undefined) throw new Error("bad PNG filter");
      rgba[i]=(rows[y*(stride+1)+x+1]+prediction)&255;
    }
  }
  return {width, height, rgba, bytes, close() {}};
}
)PNG";
    harness << R"JS(
const ready=await get(fixture.origin+fixture.root);
assert(ready.status===200,"steady ready JSON request must succeed");
const json=JSON.parse(ready.body.toString("utf8"));
assert(json.delivery_schema===3&&json.reference_schema===2&&json.reference_id===fixture.id,"schema and reference identity");
assert(json.state==="ready"&&json.lifecycle.state==="ready"&&json.lifecycle.expires_utc_us>fixture.image_completed,"ready lifecycle/expiry");
assert(json.notes_revision===2&&json.marks.length===3,"ordered three-mark notes revision");
const expectedNotes=["","世界 🌍\nsecond line\n\"quoted\"\\","line\tthree"];
for(let i=0;i<3;++i) {
  assert(JSON.stringify(Object.keys(json.marks[i]).sort())===JSON.stringify(["mark_id","note","number"]),"only compact mark fields");
  assert(json.marks[i].mark_id===fixture.marks[i].mark_id&&json.marks[i].number===fixture.marks[i].number&&json.marks[i].note===expectedNotes[i],"authoritative ID/number/exact decoded note");
}
assert(json.source_context.version===1&&json.source_context.app_name==="Capture fixture 世界"&&json.source_context.window_title==="Three gestures"&&
  json.source_context.context_observed_utc_us===fixture.context_observed&&json.source_context.image_completed_utc_us===fixture.image_completed,"captured source/version/timestamps");
for(const forbidden of ["screenshot","capture","pos","anchor","bounds","path","subpaths","samples","global_visibility"])
  assert(!ready.body.toString().includes(`"${forbidden}":`),`default omitted ${forbidden}`);
const image=json.image;
assert(image.representation==="full-annotated-v1"&&image.state==="ready"&&image.reference_id===json.reference_id&&
  image.sha256===fixture.sha256&&image.source_sha256===fixture.source_sha256&&image.width===640&&image.height===400,"full image descriptor binding");
const assetUrls=[];
function walk(value){if(value&&typeof value==="object")for(const [key,v] of Object.entries(value)){if(key==="url")assetUrls.push(v);walk(v);}}
walk(json);assert(assetUrls.length===1&&assetUrls[0]===image.url,"exactly one direct image URL");
let imageGets=0;
const png=await get(image.url);++imageGets;
assert(png.status===200&&png.headers["content-type"]==="image/png"&&hash(png.body)===image.sha256&&
  png.headers["x-seethis-png-sha256"]===image.sha256&&png.headers.etag===`"sha256-${image.sha256}"`,"actual canonical PNG bytes/hash/headers");
for(const response of [ready,png])assert(response.headers["cache-control"]==="no-store"&&response.headers["referrer-policy"]==="no-referrer","private response headers");
const pixels=decode(png.body), source=decode(fs.readFileSync(fixture.source));
assert(pixels.width===image.width&&pixels.height===image.height&&hash(source.bytes)===image.source_sha256,"decode full dimensions/source binding");
const at=(p,x,y)=>p.rgba.subarray((y*p.width+x)*4,(y*p.width+x+1)*4);
assert(at(pixels,5,5).equals(at(source,5,5))&&at(pixels,620,380).equals(at(source,620,380)),"full captured background survives");
const ink=(x,y)=>{for(let yy=y-2;yy<=y+2;++yy)for(let xx=x-2;xx<=x+2;++xx)if(at(pixels,xx,yy).equals(Buffer.from([255,45,85,255])))return true;return false;};
assert(ink(120,315)&&ink(300,150)&&ink(250,200)&&ink(300,250)&&ink(500,90),"both lines and complete circle remain on captured background");
// Identify connected badge backgrounds and read every glyph cell, so a source
// image, missing mark, wrong number or underlay cannot pass on digest alone.
const red=(x,y)=>at(pixels,x,y).equals(Buffer.from([226,35,63,255]));
const seen=new Uint8Array(pixels.width*pixels.height), boxes=[];
for(let y=0;y<pixels.height;++y)for(let x=0;x<pixels.width;++x){
  const start=y*pixels.width+x;if(seen[start]||!red(x,y))continue;
  const queue=[start];seen[start]=1;let x0=x,x1=x,y0=y,y1=y;
  for(let i=0;i<queue.length;++i){const px=queue[i]%pixels.width,py=Math.floor(queue[i]/pixels.width);
    x0=Math.min(x0,px);x1=Math.max(x1,px);y0=Math.min(y0,py);y1=Math.max(y1,py);
    for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1]]){const nx=px+dx,ny=py+dy,k=ny*pixels.width+nx;
      if(nx>=0&&ny>=0&&nx<pixels.width&&ny<pixels.height&&!seen[k]&&red(nx,ny)){seen[k]=1;queue.push(k);}}
  }
  if(x1-x0>=9&&y1-y0>=9)boxes.push({x:x0,y:y0,width:x1-x0+1,height:y1-y0+1});
}
const digits={1:[2,6,2,2,7],2:[7,1,7,4,7],3:[7,1,7,1,7]};
const reads=(box,number)=>{
  const unit=2,x=Math.floor(box.x+(box.width-3*unit)/2),y=Math.floor(box.y+(box.height-5*unit)/2);
  for(let row=0;row<5;++row)for(let col=0;col<3;++col)for(let yy=0;yy<unit;++yy)for(let xx=0;xx<unit;++xx){
    const expected=digits[number][row]&(1<<(2-col))?[255,255,255,255]:[226,35,63,255];
    if(!at(pixels,x+col*unit+xx,y+row*unit+yy).equals(Buffer.from(expected)))return false;
  }return true;
};
assert(boxes.length===3,"one complete numbered badge per included mark");
for(const mark of json.marks)assert(boxes.filter(box=>reads(box,mark.number)).length===1,"PNG number agrees with authoritative JSON mark");
assert(requestCount===2&&imageGets===1,"actual ready delivery uses exactly two GETs");
const steady={json_gets:requestCount-imageGets,image_gets:imageGets,total_gets:requestCount,json_bytes:ready.body.length,png_bytes:png.body.length,
  payload_bytes:ready.body.length+png.body.length,http_transfer_bytes:ready.transfer+png.transfer,width:pixels.width,height:pixels.height};
// Optional reads begin only after the measured 2GET path is complete.
const manifestResponse=await get(json.details_url), manifest=JSON.parse(manifestResponse.body);
assert(manifestResponse.status===200&&manifest.details_schema===1&&manifest.reference_schema===2&&manifest.reference_id===fixture.id,"bounded supplied details discovery");
const legacyResponse=await get(manifest.legacy_metadata.url), legacy=JSON.parse(legacyResponse.body);
assert(legacyResponse.status===200&&legacy.delivery_schema===2&&legacy.screenshot.url&&legacy.marks.every(m=>m.pos&&m.anchor&&m.image.url),"explicit legacy compact source/crops/geometry");
assert(JSON.stringify(legacy.source_context)===JSON.stringify(json.source_context)&&legacy.notes_revision===json.notes_revision,"unchanged captured source and notes on explicit schema2");
assert(legacy.marks.every((m,i)=>m.mark_id===json.marks[i].mark_id&&m.number===json.marks[i].number&&m.note===json.marks[i].note),"legacy authoritative marks match schema3");
assert(manifest.bearer_metadata.auth==="reference_bearer"&&(await get(manifest.bearer_metadata.url)).status===401,"discovery never grants Bearer authority");
const bearer=await get(manifest.bearer_metadata.url,fixture.root.slice(-64));
assert(bearer.status===200&&JSON.parse(bearer.body).capture.source,"existing scoped Bearer rich metadata");
const detail=await get(manifest.historical_detail.url);
assert(detail.status===200&&JSON.parse(detail.body).selection.regions.length===3,"historical detail paths remain available");
assert(manifest.viewer.url.includes("/view#cap="),"existing viewer discovery retains capability fragment");
console.log("single_image_delivery_metrics="+JSON.stringify({...steady,same_fixture_schema2_json_bytes:legacyResponse.body.length,
  same_fixture_schema2_image_states:legacy.marks.map(mark=>mark.image.state),
  schema3_minus_schema2_bytes:steady.json_bytes-legacyResponse.body.length,historical_schema2_example_json_bytes:2367,
  historical_example_comparable_fixture:false,token_savings:"NOT_MEASURED",noncontiguous:"NOT_APPLICABLE/NOT_RUN"}));
})().catch(error=>{console.error(error);process.exit(1);});
)JS";
  }
  const auto command="/usr/bin/env node '"+harness_path.string()+"' '"+config_path.string()+"'";
  const int result=std::system(command.c_str());
  Check(result!=-1&&WIFEXITED(result)&&WEXITSTATUS(result)==0,
        "decoded schema3 HTTP JSON and sole supplied PNG verify all three marks/numbers/notes/background and discovery");
  store.WaitForIdleForTesting();
  const auto before=get(root);
  Check(store.UpdateMarkNote(id,StableMarkId(id,0),"edited 世界\nnext",2).status==MarkNoteUpdateStatus::kUpdated&&
        store.UpdateMarkNote(id,StableMarkId(id,0),"stale",2).status==MarkNoteUpdateStatus::kRevisionConflict,
        "note updates preserve revision conflict semantics");
  const auto after=get(root);
  Check(before.status==200&&after.status==200&&after.body.find("\"notes_revision\":3")!=std::string::npos&&
        after.body.find("edited 世界\\nnext")!=std::string::npos&&
        after.body.find(canonical.byte_sha256)!=std::string::npos&&
        CompactSourceObject(before.body)==CompactSourceObject(after.body),
        "note revision changes exact default text while canonical image/source stay bound");
  for(const auto suffix:{"","/details","/metadata","/detail"}) {
    Check(get("/r/"+id+std::string(64,'0')+suffix).status==403&&
          Request(server.port(),ReferenceGet(server.port(),root+suffix,{},"https://example.test")).status==403&&
          Request(server.port(),ReferenceGet(server.port(),root+suffix,{},std::nullopt,"localhost:"+std::to_string(server.port()))).status==400,
          "default and optional discovery retain exact capability/Origin/Host scope");
  }
  Check(get(root+"?schema=2").status==400&&get(root+"/default").status==400&&
        get("/v1/references/"+id+"/details").status==400,
        "no new query forms or broad default/details aliases");
  // Actual missing/corrupt canonical sources must fail instead of serving raw PNG.
  for(bool remove_source:{false,true}) {
    const std::string failed_id(32,remove_source?'b':'a');
    MakeReference(store,failed_id,0,false,true);
    const auto path=directory/(failed_id+".source.png");
    if(remove_source)std::filesystem::remove(path);
    else {std::ofstream(path,std::ios::binary|std::ios::trunc)<<"damaged PNG";}
    const auto failed_root="/r/"+failed_id+settings.ReferenceCapability(failed_id);
    Check(get(failed_root).status==202,"unavailable canonical image first queues truthful validation");
    store.WaitForIdleForTesting();
    auto failed=get(failed_root);
    for(unsigned retry=0;failed.status==202&&retry<12;++retry) {
      store.WaitForIdleForTesting();failed=get(failed_root);
    }
    const auto failed_asset=get(failed_root+"/annotated.png");
    std::cout<<"canonical_failure_probe,missing="<<remove_source<<",default_status="<<failed.status
             <<",asset_status="<<failed_asset.status<<",failed_state="<<(failed.body.find("\"state\":\"failed\"")!=std::string::npos)
             <<",null_digest="<<(failed.body.find("\"sha256\":null")!=std::string::npos)
             <<",asset_corrupt="<<(failed.body.find("asset_corrupt")!=std::string::npos)<<'\n';
    Check(failed.status==422&&failed.body.find("\"state\":\"failed\"")!=std::string::npos&&
          failed.body.find("\"sha256\":null")!=std::string::npos&&
          failed.body.find("asset_corrupt")!=std::string::npos&&
          failed.body.find("\"lifecycle\":{\"state\":\"ready\"")!=std::string::npos&&
          failed_asset.status==422,
          "actual corrupt/missing canonical asset is failed422 with null digest and no unmarked fallback");
  }
  // Source availability is independent of an already valid canonical PNG.
  const std::string cached_id(32,'c');
  MakeReference(store,cached_id,0,false,true);
  const auto cached=AwaitAsset(store,cached_id,0);
  const auto cached_root="/r/"+cached_id+settings.ReferenceCapability(cached_id);
  std::filesystem::remove(directory/(cached_id+".source.png"));
  const auto cached_default=get(cached_root);
  const auto cached_png=get(cached_root+"/annotated.png");
  Check(cached_default.status==200&&cached_default.body.find("\"state\":\"ready\"")!=std::string::npos&&
        cached_default.body.find(cached.byte_sha256)!=std::string::npos&&
        PngResponseMatches(cached_png,cached.byte_sha256,{100,100})&&
        get(cached_root+"/source.png").status==422,
        "valid cached canonical PNG remains200/digest-bound when optional raw source is unavailable");
  Check(store.Delete(id)==DeleteResult::kDeleted,"schema3 reference deletion succeeds");
  for(const auto suffix:{"","/details","/metadata","/detail","/annotated.png"})
    Check(get(root+suffix).status==410,"deletion invalidates default and every discovery route");
  server.Stop();
}

struct ParsedHandoff {
  std::string target;
  std::string capability;
};

ParsedHandoff ParseCompactHandoff(std::string_view text,
                                  std::string_view base_url) {
  if (text.empty() || text.find_first_of(" \t\r\n") != std::string_view::npos)
    throw std::runtime_error("compact handoff is not one URL");
  const std::string_view url = text;
  const std::string prefix = std::string(base_url) + "/v1/references/";
  if (!url.starts_with(prefix)) throw std::runtime_error("invalid local URL");
  const auto fragment = url.find("#cap=");
  if (fragment == std::string_view::npos)
    throw std::runtime_error("missing capability fragment");
  ParsedHandoff result{std::string(url.substr(base_url.size(),
                                              fragment - base_url.size())),
                       std::string(url.substr(fragment + 5))};
  const std::string_view remainder(result.target.data() +
      std::string_view("/v1/references/").size(),
      result.target.size() - std::string_view("/v1/references/").size());
  const auto lower_hex = [](unsigned char byte) {
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
  };
  if (remainder.size() != 37 || !remainder.ends_with("/view") ||
      !std::all_of(remainder.begin(), remainder.begin() + 32, lower_hex) ||
      result.capability.size() != 64 ||
      !std::all_of(result.capability.begin(), result.capability.end(), lower_hex))
    throw std::runtime_error("invalid compact handoff identity");
  return result;
}

std::string PutSettings(std::string_view token, std::string_view body,
                        std::string_view origin,
                        std::string_view type = "application/json") {
  return "PUT /v1/settings HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " +
         std::string(token) + "\r\nOrigin: " + std::string(origin) +
         "\r\nContent-Type: " + std::string(type) + "\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

std::string PostRetry(std::string_view token, std::string_view body,
                      std::string_view origin,
                      std::string_view type = "application/json") {
  return "POST /v1/readiness/retry HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " +
         std::string(token) + "\r\nOrigin: " + std::string(origin) +
         "\r\nContent-Type: " + std::string(type) + "\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

void TestUrlOnlyControllerCompletion() {
  TempDirectory temp;
  ReferenceStore store(temp.path / "references");
  std::mutex dispatch_mutex;
  std::vector<std::function<void()>> dispatched;
  std::vector<std::string> published;
  std::int64_t pasteboard_change_count = 10;
  int completion_observations = 0;
  MarkController marks(
      store,
      [&](std::string_view id) -> std::optional<std::int64_t> {
        published.push_back("http://127.0.0.1:1/v1/references/" +
                            std::string(id) + "/view#cap=" +
                            std::string(64, 'c'));
        return ++pasteboard_change_count;
      },
      [&](std::shared_ptr<const StoredReference>, std::int64_t) {
        ++completion_observations;
        return true;
      },
      [&](std::function<void()> task) {
        std::lock_guard lock(dispatch_mutex);
        dispatched.push_back(std::move(task));
      });
  const auto run_dispatched = [&] {
    std::vector<std::function<void()>> ready;
    {
      std::lock_guard lock(dispatch_mutex);
      ready.swap(dispatched);
    }
    for (auto& task : ready) task();
  };
  const auto ready_id = std::string(32, '6');
  const auto observed = Now();
  const auto ready_context = ContextFixture(observed);
  Observe(marks, ready_context);
  marks.SetViewportContinuity(1,true);
  auto generation = marks.Begin(ready_id);
  Check(marks.BindContext(generation, ready_context),
        "URL-only ready fixture binds context");
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                 {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                {observed.utc_us + 30, observed.monotonic_us + 30});
  Check(published.size() == 1 &&
            marks.copy_result() == CopyResult::kCopied,
        "capture release immediately publishes one pending URL");
  const auto stable_url = published.back();
  auto unavailable_title =
      WindowFor(ready_context, marks.window_observation().generation + 1);
  unavailable_title.window_title.clear();
  unavailable_title.title = {1, TitleAvailability::kUnavailable, true};
  marks.SetWindowObservation(std::move(unavailable_title));
  Check(!marks.RecopyJob(ready_id) && published.size() == 1 &&
            published.back() == stable_url && pasteboard_change_count == 11 &&
            marks.copy_result() == CopyResult::kFailed,
        "fresh unavailable current title prevents pending URL publication and copy");
  Observe(marks, ready_context);
  Check(marks.RecopyJob(ready_id) && published.back() == stable_url &&
            published.size() == 2 && pasteboard_change_count == 12 &&
            marks.copy_result() == CopyResult::kCopied,
        "deliberate pending click republishes the stable URL");
  pasteboard_change_count = 5'000;
  CaptureResult capture;
  capture.requested = {observed.utc_us + 10, observed.monotonic_us + 10};
  capture.completed = {observed.utc_us + 20, observed.monotonic_us + 20};
  capture.pixels = {100, 100, Bytes(100 * 100 * 4)};
  for (std::size_t index = 3; index < capture.pixels.rgba.size(); index += 4)
    capture.pixels.rgba[index] = 255;
  marks.Captured(generation, std::move(capture));
  store.WaitForIdleForTesting();
  run_dispatched();
  Check(completion_observations == 1 && pasteboard_change_count == 5'000 &&
            store.LookupMetadata(ready_id)->state == ReferenceJobState::kReady &&
            marks.copy_result() == CopyResult::kCopied &&
            marks.status() == "Reference ready; click its mark to copy the URL",
        "unowned ready completion preserves the original result without claiming URL presence");
  std::cout<<"uc2_unrelated_ready,count="<<pasteboard_change_count
           <<",copy_result="<<static_cast<int>(marks.copy_result())
           <<",status="<<marks.status()<<'\n';
  Observe(marks, ready_context);
  Check(marks.RecopyJob(ready_id) && published.back() == stable_url &&
            pasteboard_change_count == 5'001,
        "deliberate ready click alone republishes one stable URL");
  struct Attempt {
    bool success = false;
    long long elapsed_us = 0;
  };
  std::vector<Attempt> copy_attempts;
  Observe(marks, ready_context);
  for (int attempt = 0; attempt < 220; ++attempt) {
    const auto start = std::chrono::steady_clock::now();
    const bool success = marks.RecopyJob(ready_id);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (attempt >= 20) copy_attempts.push_back({success, elapsed});
  }
  std::vector<long long> sorted_copy;
  bool every_copy_succeeded = true;
  for (std::size_t index = 0; index < copy_attempts.size(); ++index) {
    const auto& attempt = copy_attempts[index];
    std::cout << "deterministic_url_copy_attempt," << index
              << ",status=" << (attempt.success ? "published" : "failed")
              << ",elapsed_us=" << attempt.elapsed_us << '\n';
    every_copy_succeeded = every_copy_succeeded && attempt.success;
    sorted_copy.push_back(attempt.elapsed_us);
  }
  std::sort(sorted_copy.begin(), sorted_copy.end());
  std::cout << "deterministic_url_copy_p95_us=" << sorted_copy[189] << '\n';
  Check(copy_attempts.size() == 200 && every_copy_succeeded,
        "20 warmups and 200 ordered deterministic URL-copy attempts succeed");

  std::vector<Attempt> hover_attempts;
  const auto topology = ContextFixture(observed).displays;
  const DisplayPoint hover_point{
      1, CoordinateUnit::kLogicalPoints, {20, 20}, 1};
  Observe(marks, ready_context);
  for (int attempt = 0; attempt < 220; ++attempt) {
    const auto start = std::chrono::steady_clock::now();
    const auto hit = marks.HitIdentity(hover_point, 2, topology);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    const bool success = hit && *hit == ready_id;
    if (attempt >= 20) hover_attempts.push_back({success, elapsed});
  }
  std::vector<long long> sorted_hover;
  bool every_hover_succeeded = true;
  for (std::size_t index = 0; index < hover_attempts.size(); ++index) {
    const auto& attempt = hover_attempts[index];
    std::cout << "deterministic_hover_attempt," << index
              << ",status=" << (attempt.success ? "hit" : "miss")
              << ",elapsed_us=" << attempt.elapsed_us << '\n';
    every_hover_succeeded = every_hover_succeeded && attempt.success;
    sorted_hover.push_back(attempt.elapsed_us);
  }
  std::sort(sorted_hover.begin(), sorted_hover.end());
  std::cout << "deterministic_hover_p95_us=" << sorted_hover[189] << '\n';
  Check(hover_attempts.size() == 200 && every_hover_succeeded,
        "20 warmups and 200 ordered deterministic hover attempts stay separate");

  const auto failed_id = std::string(32, '7');
  generation = marks.Begin(failed_id);
  Check(marks.BindContext(generation, ContextFixture(Now())),
        "URL-only failed fixture binds context");
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1}},
                Now());
  const auto failed_url = published.back();
  pasteboard_change_count = 7'000;
  CaptureResult failed;
  failed.error = "capture_failed";
  marks.Captured(generation, std::move(failed));
  store.WaitForIdleForTesting();
  run_dispatched();
  Check(completion_observations == 1 && pasteboard_change_count == 7'000 &&
            store.LookupMetadata(failed_id)->state == ReferenceJobState::kFailed &&
            marks.copy_result() == CopyResult::kFailed &&
            marks.status() == "Reference failed: capture_failed",
        "failed completion leaves unrelated clipboard content unchanged and reports failure");
  std::cout<<"uc2_failed,count="<<pasteboard_change_count
           <<",copy_result="<<static_cast<int>(marks.copy_result())
           <<",status="<<marks.status()<<'\n';
  Check(marks.RecopyJob(failed_id) && published.back() == failed_url &&
            pasteboard_change_count == 7'001,
        "deliberate failed-mark click republishes the stable status URL");

  const auto unchanged_id = std::string(32, '8');
  const auto unchanged_observed = Now();
  generation = marks.Begin(unchanged_id);
  Check(marks.BindContext(generation, ContextFixture(unchanged_observed)),
        "unchanged-count ready fixture binds context");
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                 {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                {unchanged_observed.utc_us + 30,
                 unchanged_observed.monotonic_us + 30});
  const auto unchanged_publication_count = pasteboard_change_count;
  CaptureResult unchanged_capture;
  unchanged_capture.requested = {unchanged_observed.utc_us + 10,
                                 unchanged_observed.monotonic_us + 10};
  unchanged_capture.completed = {unchanged_observed.utc_us + 20,
                                 unchanged_observed.monotonic_us + 20};
  unchanged_capture.pixels = {100, 100, Bytes(100 * 100 * 4)};
  for(std::size_t index=3;index<unchanged_capture.pixels.rgba.size();index+=4)
    unchanged_capture.pixels.rgba[index]=255;
  marks.Captured(generation,std::move(unchanged_capture));
  store.WaitForIdleForTesting();
  run_dispatched();
  Check(completion_observations==2&&
            pasteboard_change_count==unchanged_publication_count&&
            marks.copy_result()==CopyResult::kCopied&&
            marks.status()=="Reference ready; click its mark to copy the URL",
        "unchanged-count completion is lifecycle-ready without claiming current ownership");
  std::cout<<"uc2_unchanged_ready,count="<<pasteboard_change_count
           <<",copy_result="<<static_cast<int>(marks.copy_result())
           <<",status="<<marks.status()<<'\n';
}

std::string CompactSourceObject(const std::string& body);

void TestChromeTransactionIdentityFreeze() {
  TempDirectory temp;
  ReferenceStore store(temp.path / "chrome-transaction-freeze");
  int copied = 0;
  MarkController marks(store, [&](const auto&) { ++copied; return true; });
  const auto id = std::string(32, '5');
  const auto observed = Now();
  auto context = ContextFixture(observed);
  context.bundle_id = "com.google.Chrome";
  context.window_title = "redacted capture title";
  Observe(marks, context);

  marks.SetViewportContinuity(1,true);
  const auto generation = marks.Begin(id);
  Check(marks.BindContext(generation, context),
        "Chrome transaction binds its original window");
  PageIdentity page_a;
  page_a.bundle_id = context.bundle_id;
  page_a.browser_pid = context.window_pid;
  page_a.window_id = context.window_id;
  page_a.window_bounds = context.window;
  page_a.process_start_identity_us = 700;
  page_a.opaque_tab_id = "page-a";
  page_a.navigation_digest =
      seethis::platform::ChromeNavigationDigest("https://example.test/a");
  Check(marks.BindPageIdentity(generation,page_a,CapturedPageMetadata{
      std::string("https://example.test/a"),SourceFieldAvailability::kCaptured,
      observed.utc_us+5}),
        "Chrome transaction freezes page A and captured metadata while drawing");
  marks.SetPageObservation(
      {PageAvailability::kReady, 1, Now(), page_a});

  auto page_b = page_a;
  page_b.opaque_tab_id = "page-b";
  page_b.navigation_digest =
      seethis::platform::ChromeNavigationDigest("https://example.test/b");
  marks.SetPageObservation(
      {PageAvailability::kReady, 2, Now(), page_b});

  CaptureResult capture;
  capture.requested = {observed.utc_us + 10, observed.monotonic_us + 10};
  capture.completed = {observed.utc_us + 20, observed.monotonic_us + 20};
  capture.pixels = {100, 100, Bytes(100 * 100 * 4)};
  for (std::size_t index = 3; index < capture.pixels.rgba.size(); index += 4)
    capture.pixels.rgba[index] = 255;
  marks.Captured(generation, std::move(capture));
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                 {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                {observed.utc_us + 30, observed.monotonic_us + 30});
  store.WaitForIdleForTesting();

  const auto stored = store.Lookup(id);
  Check(stored && stored->ready && stored->ready->value.page_identity &&
            SamePageIdentity(*stored->ready->value.page_identity, page_a) &&
            !SamePageIdentity(*stored->ready->value.page_identity, page_b),
        "release persists the page identity frozen during drawing");
  Check(marks.marks().empty() &&
            !marks.HitIdentity({1, CoordinateUnit::kLogicalPoints,
                                {20, 20}, 1},
                               2, context.displays) &&
            !marks.RecopyJob(id),
        "same-window page B hides page A after persistence");

  SettingsStore settings(temp.path/"settings.json");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(store,settings,temp.path/"web",readiness);
  std::string server_error;Check(server.Start(&server_error),"navigation projection endpoint starts");
  const auto root="/r/"+id+settings.ReferenceCapability(id);
  const auto captured=Request(server.port(),ReferenceGet(server.port(),root+"/metadata"));
  const auto frozen=CompactSourceObject(captured.body);
  AwaitAsset(store,id,0);
  const auto single=Request(server.port(),ReferenceGet(server.port(),root));
  Check(single.status==200&&CompactSourceObject(single.body)==frozen,
        "schema3 retains captured page A after actual same-window navigation to B");
  Check(captured.status==200&&frozen.find("\"page_url\":\"https://example.test/a\"")!=std::string::npos&&
        frozen.find("https://example.test/b")==std::string::npos,
      "explicit authenticated legacy endpoint retains page A metadata after same-window navigation to B");

  marks.SetPageObservation(
      {PageAvailability::kReady, 3, Now(), page_a});
  Check(marks.marks().size() == 1 &&
            marks.HitIdentity({1, CoordinateUnit::kLogicalPoints,
                               {20, 20}, 1},
                              2, context.displays) == id &&
            marks.RecopyJob(id) && copied == 2,
        "returning to page A restores its stored mark and copy target");
  Check(CompactSourceObject(Request(server.port(),ReferenceGet(server.port(),root+"/metadata")).body)==frozen,
      "returning to captured page leaves the same reference projection frozen");
  server.Stop();
}

// Extract the object without treating escaped quotes/braces as JSON structure.
std::string CompactSourceObject(const std::string& body) {
  const auto field=body.find("\"source_context\":{");
  Check(field!=std::string::npos,"authenticated compact response has source_context");
  const auto start=body.find('{',field);
  bool quoted=false,escaped=false;unsigned depth=1;
  for(std::size_t i=start+1;i<body.size();++i) {
    const char c=body[i];
    if(escaped){escaped=false;continue;}
    if(quoted&&c=='\\'){escaped=true;continue;}
    if(c=='"'){quoted=!quoted;continue;}
    if(!quoted&&c=='{')++depth;
    if(!quoted&&c=='}'&&--depth==0)return body.substr(start,i-start+1);
  }
  throw std::runtime_error("source_context is not a complete JSON object");
}

void TestExplicitScopeDelivery(bool kernel=false) {
  TempDirectory temp;SettingsStore settings(temp.path/"settings.json");ReferenceStore store(temp.path/"references");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(store,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"scope endpoint starts with owned ephemeral server");
  unsigned counter=0;
  for(const auto kind:kernel?std::vector<CaptureScopeKind>{CaptureScopeKind::kWindow,CaptureScopeKind::kApplication}:std::vector<CaptureScopeKind>{CaptureScopeKind::kPage,CaptureScopeKind::kWindow,CaptureScopeKind::kApplication}) {
    auto context=ContextFixture(Now());context.native_window={kernel?2u:1u,kernel?1000000:700,"delivery-scope",1};
    context.scope={kernel?2u:1u,kind,1,kernel?1000000:700,true,kind==CaptureScopeKind::kWindow};
    if(kernel)context.scope.process_source=ProcessStartSource::kKernelBSDStart;context.title={1,TitleAvailability::kReady};
    if(kind==CaptureScopeKind::kPage)context.bundle_id="com.google.Chrome";
    if(kind==CaptureScopeKind::kApplication) {
      context.window_id=0;context.window_pid=0;context.window={};context.window_title.clear();context.native_window={};
      context.selection_method="frontmost-application/selected-display";
    }
    const std::string id(32,static_cast<char>('a'+counter++));
    const auto stored=MakeReference(store,id,0,false,true,0,false,0,&context);
    AwaitAsset(store,id,0);
    const auto root="/r/"+id+settings.ReferenceCapability(id);
    const auto response=Request(server.port(),ReferenceGet(server.port(),root));
    Check(response.status==200&&response.body.find("\"scope\":{\"version\":"+std::to_string(context.scope.version)+",\"kind\":\""+
          std::string(CaptureScopeKindName(kind))+"\"")!=std::string::npos&&
        response.body.find(CaptureScopeName(context))!=std::string::npos&&
        response.body.find(ProcessStartSourceName(CaptureProcessSource(context)))!=std::string::npos,
        "actual default JSON carries immutable shot scope and user-facing label");
    const auto details=Request(server.port(),ReferenceGet(server.port(),root+"/metadata"));
    Check(details.status==200&&CompactSourceObject(response.body)==CompactSourceObject(details.body),
        "default and explicit metadata source projections agree on frozen scope");
    const auto projection=ProjectSourceContext(stored.value);
    Check(projection.scope==context.scope&&projection.scope_label==CaptureScopeName(context),"source projection comes from frozen shot policy");
    if(kind==CaptureScopeKind::kApplication)Check(!projection.window_id&&projection.window_title_availability==SourceFieldAvailability::kNotApplicable,
        "application scope never invents a native window in metadata");
  }
  server.Stop();std::cout<<checks<<" explicit scope HTTP/source projection checks passed\n";
}

void TestCompactSourceContext() {
  TempDirectory temp;
  const auto directory=temp.path/"references";
  SettingsStore settings(temp.path/"settings.json");
  std::string frozen_id,frozen_object;
  Bytes frozen_png;
  std::size_t next_id=1,maximum_increment=0;
  {
    ReferenceStore store(directory);store.SetRetentionPolicy(300,500);
    auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
    seethis::service::ReferenceServer server(store,settings,temp.path/"web",readiness);
    std::string error;Check(server.Start(&error),"source-context endpoint starts");
    auto fetch=[&](const Context& context,std::optional<CapturedPageMetadata> page={}) {
      std::ostringstream identity;identity<<std::hex<<std::setw(32)<<std::setfill('0')<<next_id++;
      const auto id=identity.str();
      const auto stored=MakeReference(store,id,0,false,true,0,false,0,&context,page);
      const auto target="/r/"+id+settings.ReferenceCapability(id);
      AwaitAsset(store,id,0);
      const auto single=Request(server.port(),ReferenceGet(server.port(),target));
      const auto response=Request(server.port(),ReferenceGet(server.port(),target+"/metadata"));
      Check(response.status==200,"explicit legacy compact capability endpoint succeeds");
      const auto source=CompactSourceObject(response.body);
      Check(single.status==200&&CompactSourceObject(single.body)==source,
          "schema3 preserves every capture-time source availability, timestamp and bounded text field");
      maximum_increment=std::max(maximum_increment,source.size()+18);
      Check(response.body.find("\"reference_id\":\""+id+"\"")!=std::string::npos &&
            response.body.find(stored.value.source.sha256)!=std::string::npos &&
            response.body.find("\"number\":1")!=std::string::npos &&
            response.body.find("\"number\":2")!=std::string::npos &&
            response.body.find("\"number\":1")<response.body.find("\"number\":2") &&
            response.body.find("\"pos\":{")!=std::string::npos &&
            response.body.find("\"anchor\":{")!=std::string::npos &&
            response.body.find("\"notes_revision\":0")!=std::string::npos,
          "source metadata stays bound to screenshot and ordered mark/crop/pos/note delivery");
      for(const char* forbidden:{"\"path\"","\"subpaths\"","\"rgba\"","\"bytes\"",
                                "\"displays\"","\"executable\"","\"window_pid\""})
        Check(response.body.find(forbidden)==std::string::npos,
            "default projection omits detailed inventories, trajectories and image bytes");
      Check(source.find("\"context_observed_utc_us\":"+std::to_string(context.observed.utc_us))!=std::string::npos &&
            source.find("\"image_completed_utc_us\":"+std::to_string(stored.value.image_completed.utc_us))!=std::string::npos &&
            stored.value.image_completed.utc_us!=context.observed.utc_us,
          "context observation and completed screenshot retain separate exact stamps");
      if(frozen_id.empty()&&page&&page->url&&page->url->size()<100) {
        frozen_id=id;frozen_object=source;frozen_png=stored.value.source.bytes;
        const auto unauthorized=Request(server.port(),ReferenceGet(server.port(),"/v1/references/"+id));
        const auto wrong=Request(server.port(),ReferenceGet(server.port(),"/r/"+id+std::string(64,'0')));
        Check(unauthorized.status==401&&wrong.status==403&&
              unauthorized.body.find("source_context")==std::string::npos&&
              wrong.body.find("source_context")==std::string::npos,
            "new metadata keeps the existing authentication boundary");
      }
      return source;
    };
    auto context=ContextFixture(Now());context.bundle_id="com.google.Chrome";
    context.app_name="Google Chrome";context.window_title="current tab title must not leak";
    CapturedPageMetadata page{std::string("chrome://History/?q=\"a\\b\""),
        SourceFieldAvailability::kCaptured,context.observed.utc_us+5};
    const auto original=fetch(context,page);
    Check(original.find("\"page_url\":\"chrome://History/?q=\\\"a\\\\b\\\"\"")!=std::string::npos &&
          original.find("\"page_url_availability\":\"captured\"")!=std::string::npos &&
          original.find("\"window_title\":null,\"window_title_availability\":\"not_applicable\"")!=std::string::npos &&
          original.find("\"page_url_observed_utc_us\":"+std::to_string(*page.observed_utc_us))!=std::string::npos,
        "captured non-HTTP URL preserves case, quotes, backslash and its own observation stamp");
    const auto legacy=fetch(context);
    Check(legacy.find("\"page_url\":null,\"page_url_availability\":\"legacy_unavailable\"")!=std::string::npos &&
          legacy.find("\"page_url_observed_utc_us\":null")!=std::string::npos,
        "legacy Chrome absence is explicit without current-tab fallback");
    // Every field's exact byte boundary is exercised through the actual endpoint.
    for(std::size_t field=0;field<4;++field) {
      const std::size_t limit=field==0?256:field==1?512:field==2?1024:16384;
      const std::string name=field==0?"app_name":field==1?"bundle_id":field==2?"window_title":"page_url";
      for(std::size_t extra:{0,1}) {
        auto sample=ContextFixture(Now());
        std::string value(limit+extra,'x');
        std::optional<CapturedPageMetadata> metadata;
        if(field==0)sample.app_name=value;
        if(field==1)sample.bundle_id=value;
        if(field==2)sample.window_title=value;
        if(field==3) {
          sample.bundle_id="com.google.Chrome";
          metadata=CapturedPageMetadata{value,SourceFieldAvailability::kCaptured,sample.observed.utc_us+5};
        }
        const auto result=fetch(sample,metadata);
        Check(result.find("\""+name+"\":"+(extra?"null":"\""+value+"\""))!=std::string::npos &&
              result.find("\""+name+"_availability\":\""+(extra?"invalid_or_over_limit":"captured")+"\"")!=std::string::npos,
            "each projection field preserves exact maximum and omits one byte over without rejecting capture");
      }
      for(const auto& invalid:std::vector<std::string>{std::string("a\0b",3),"a\nb","a\tb",
          std::string("a\x01" "b",3),std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3)}) {
        auto sample=ContextFixture(Now());std::optional<CapturedPageMetadata> metadata;
        if(field==0)sample.app_name=invalid;
        if(field==1)sample.bundle_id=invalid;
        if(field==2)sample.window_title=invalid;
        if(field==3){sample.bundle_id="com.google.Chrome";metadata=CapturedPageMetadata{invalid,SourceFieldAvailability::kCaptured,sample.observed.utc_us+5};}
        const auto result=fetch(sample,metadata);
        Check(result.find("\""+name+"\":null,\""+name+"_availability\":\"invalid_or_over_limit\"")!=std::string::npos,
            "invalid UTF-8, embedded NUL and C0 controls omit only their projection field");
      }
      auto sample=ContextFixture(Now());std::string unicode(limit-2,'x');unicode+="é";
      if(field==0)sample.app_name=unicode;
      if(field==1)sample.bundle_id=unicode;
      if(field==2)sample.window_title=unicode;
      std::optional<CapturedPageMetadata> metadata;
      if(field==3){sample.bundle_id="com.google.Chrome";metadata=CapturedPageMetadata{unicode,SourceFieldAvailability::kCaptured,sample.observed.utc_us+5};}
      Check(fetch(sample,metadata).find("\""+name+"\":\""+unicode+"\"")!=std::string::npos,
          "multibyte code point at exact byte boundary remains verbatim");
    }
    auto non_chrome=ContextFixture(Now());non_chrome.app_name="Codex";
    non_chrome.window_title="Design review";non_chrome.bundle_id="com.example.Codex";
    const auto codex=fetch(non_chrome);
    Check(codex.find("\"window_title\":\"Design review\"")!=std::string::npos &&
          codex.find("\"page_url\":null,\"page_url_availability\":\"not_applicable\"")!=std::string::npos,
        "non-Chrome title and URL applicability remain truthful");
    non_chrome.app_name="Discord";non_chrome.window_title="Team channel";fetch(non_chrome);
    non_chrome.window_title.clear();
    Check(fetch(non_chrome).find("\"window_title\":null,\"window_title_availability\":\"legacy_unavailable\"")!=std::string::npos,
        "missing historical title is null with explicit availability");
    // Quote/backslash expansion is measured on real responses, not request limits.
    auto worst=ContextFixture(Now());worst.bundle_id="com.google.Chrome";
    worst.app_name=std::string(256,'"');
    auto escaped=fetch(worst,CapturedPageMetadata{std::string(16384,'\\'),
        SourceFieldAvailability::kCaptured,worst.observed.utc_us+5});
    Check(escaped.size()>2*(256+16384) && escaped.size()<2*(256+512+1024+16384)+1024,
        "worst-case escaped source_context is bounded independently of marks and notes");
    Check(Request(server.port(),ReferenceGet(server.port(),
              "/r/"+frozen_id+settings.ReferenceCapability(frozen_id)+"/metadata")).body.find(frozen_object)!=std::string::npos,
        "later different-window/page captures cannot rewrite historical compact metadata");
    std::cout<<"source_context_max_incremental_bytes="<<maximum_increment<<'\n';
    server.Stop();store.WaitForIdleForTesting();
  }
  {
    ReferenceStore restarted(directory);
    auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
    seethis::service::ReferenceServer server(restarted,settings,temp.path/"web",readiness);
    std::string error;Check(server.Start(&error),"source-context endpoint restarts");
    const auto root="/r/"+frozen_id+settings.ReferenceCapability(frozen_id);
    const auto response=Request(server.port(),ReferenceGet(server.port(),root+"/metadata"));
    Check(response.status==200&&CompactSourceObject(response.body)==frozen_object,
        "same authenticated reference keeps exact historical projection after store restart");
    const auto png=Request(server.port(),ReferenceGet(server.port(),root+"/source.png"));
    Check(png.status==200&&Bytes(png.body.begin(),png.body.end())==frozen_png,
        "metadata projection leaves the immutable source screenshot bytes unchanged");
    server.Stop();
  }
}

void TestAnnotatedInterleaving() {
  TempDirectory temp;
  const auto reference_root=temp.path/"interleaving";
  const auto web=temp.path/"web";
  std::filesystem::create_directories(web);
  std::ofstream(web/"index.html")<<"<html>synthetic</html>";
  SettingsStore settings(temp.path/"settings.json");
  const std::string a(32,'a'),b(32,'b');
  const auto token_a=settings.ReferenceCapability(a);
  const auto token_b=settings.ReferenceCapability(b);
  auto run=[&](ReferenceStore& references,bool reopened) {
    auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
    seethis::service::ReferenceServer server(references,settings,web,readiness);
    std::string error;
    Check(server.Start(&error),"interleaving HTTP server starts");
    auto get=[&](const std::string& id,const std::string& suffix,const std::string& token) {
      return Request(server.port(),ReferenceGet(server.port(),"/v1/references/"+id+suffix,token));
    };
    const auto source=get(a,"/source.png",token_a);
    (void)get(a,"/crop.png",token_a);references.WaitForIdleForTesting();
    const auto crop=get(a,"/crop.png",token_a);
    Check(source.status==200&&crop.status==200,"interleaving preserves source/crop HTTP delivery");
    const auto source_hash=Sha256(Bytes(source.body.begin(),source.body.end()));
    const auto crop_hash=Sha256(Bytes(crop.body.begin(),crop.body.end()));
    Check(Header(source,"X-SeeThis-PNG-SHA256")==source_hash&&
          Header(crop,"X-SeeThis-PNG-SHA256")==crop_hash,
          "source and crop byte digests remain truthful");
    (void)get(a,"",token_a);references.WaitForIdleForTesting();
    const auto marked=get(a,"/annotated.png",token_a);
    const auto bytes=Bytes(marked.body.begin(),marked.body.end());
    const auto digest=Sha256(bytes);
    Check(marked.status==200&&Header(marked,"Content-Type")=="image/png"&&
          Header(marked,"ETag")=="\"sha256-"+digest+"\""&&
          Header(marked,"X-SeeThis-PNG-SHA256")==digest&&
          FileBytes(reference_root/(a+".annotated.png"))==bytes,
          "marked HTTP header and persisted PNG agree");
    for(int round=0;round<4;++round) {
      Check(get(b,"",token_b).status==200,"other-reference metadata remains available");
      references.WaitForIdleForTesting();
      const auto interleaved=get(a,"/annotated.png",token_a);
      Check(interleaved.status==200&&interleaved.body==marked.body&&
            Header(interleaved,"X-SeeThis-PNG-SHA256")==digest,
            "other-reference metadata must not turn a completed marked PNG back into202");
    }
    std::vector<Response> concurrent(4);
    std::vector<std::thread> threads;
    for(int index=0;index<4;++index)
      threads.emplace_back([&,index]{concurrent[index]=get(a,"/annotated.png",token_a);});
    for(auto& thread:threads)thread.join();
    for(const auto& response:concurrent)
      Check(response.status==200&&response.body==marked.body,"concurrent marked readers retain exact bytes");
    Check(get(a,"/annotated.png","").status==401&&
          get(a,"/annotated.png",token_b).status==403,
          "cached marked images still enforce reference authentication");
    Check(get(a,"/source.png",token_a).body==source.body&&
          get(a,"/crop.png",token_a).body==crop.body,
          "marked cache does not alter source or crop");
    if(reopened) {
      for(std::size_t index=0;index<ReferenceStore::kMaximumCachedReadyPayloads;++index) {
        const std::string id(32,static_cast<char>('0'+index));
        MakeReference(references,id,0,false,true);
        const auto token=settings.ReferenceCapability(id);
        Response cold;
        for(int poll=0;poll<12;++poll){cold=get(id,"/annotated.png",token);
          if(cold.status==200)break;
          Check(cold.status==202,"cold default asset remains explicitly queued/deriving");
          references.WaitForIdleForTesting();}
        if(cold.status!=200)std::cerr<<"cold_victim="<<id<<",status="<<cold.status<<",body="<<cold.body<<"\n";
        Check(cold.status==200,"bounded cache turnover completes each cold reference despite competing underlay work");
        const auto expected=FileBytes(reference_root/(id+".annotated.png"));
        const auto metadata=references.LookupMetadata(id);
        Check(metadata&&metadata->metadata&&metadata->metadata->id==id&&
              metadata->metadata->source.sha256==source_hash && !expected.empty() &&
              cold.body==std::string(expected.begin(),expected.end()),
            "each cold victim retains its exact same-source numbered persisted bytes");
        CheckPngResponse(cold,Sha256(expected),{metadata->metadata->source.width,
                                             metadata->metadata->source.height});
      }
      Check(get(a,"/annotated.png",token_a).status==202,"old marked payload is evicted at the bounded cache limit");
      references.WaitForIdleForTesting();
      Check(get(a,"/annotated.png",token_a).body==marked.body,"evicted PNG reloads exact persisted bytes");
      Check(references.Delete(a)==DeleteResult::kDeleted&&
            get(a,"/annotated.png",token_a).status==410&&
            !std::filesystem::exists(reference_root/(a+".annotated.png")),
            "deletion drops cached marked image and preserves410");
    }
    server.Stop();references.WaitForIdleForTesting();
  };
  {
    ReferenceStore references(reference_root);
    MakeReference(references,a,0,false,true);MakeReference(references,b,0,false,true);
    run(references,false);
  }
  ReferenceStore reopened(reference_root);
  run(reopened,true);
}

void TestDisplayUnderlayServiceProgress() {
  // Freeze the same 48-round/96-derivation bound as the core scheduler test.
  TempDirectory temp;const auto directory=temp.path/"references";
  const std::vector<std::string> ids={std::string(29,'0')+"830",std::string(29,'0')+"831"};
  {ReferenceStore creator(directory);for(const auto& id:ids)MakeReference(creator,id,0,false,true,0,false,10);}
  struct Expected {std::string digest,sourceSHA;std::pair<std::uint32_t,std::uint32_t> size;};
  Expected expected[2][15];
  // Separate actual core oracle; it cannot consume the measured store's slot
  // handoffs or alter its disk cache or request schedule.
  {
    ReferenceStore oracle(temp.path/"oracle");
    for(std::size_t r=0;r<2;++r){
      const auto frozen=MakeReference(oracle,ids[r],0,false,true,0,false,10);
      for(unsigned asset=0;asset<15;++asset){const auto ready=AwaitAsset(oracle,ids[r],asset);
        Check(ready.bytes&&!ready.bytes->empty(),"core-generated oracle owns bytes");
        expected[r][asset]={ready.byte_sha256,frozen.value.source.sha256,PngSize(*ready.bytes)};}
    }
  }
  Check(expected[0][0].sourceSHA!=expected[1][0].sourceSHA,
      "competing reference screenshots must have distinct immutable source SHA values");
  for(unsigned asset=0;asset<15;++asset)Check(expected[0][asset].digest!=expected[1][asset].digest,
      "each representation must discriminate a same-size cross-reference PNG swap");
  ReferenceStore references(directory);SettingsStore settings(temp.path/"settings.json");
  std::mutex mutex;std::condition_variable changed;bool entered=false;unsigned permits=0;
  std::atomic_size_t derivations=0;
  ReferenceStoreTestHooks hooks;
  hooks.before_derive_annotated=[&](std::string_view){
    std::unique_lock lock(mutex);++derivations;entered=true;changed.notify_all();
    changed.wait(lock,[&]{return permits>0;});--permits;
  };
  references.SetTestHooks(hooks);
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(references,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"mixed-progress actual service starts");
  const std::shared_ptr<void> releaseOnExit(nullptr,[&](void*) {
    {std::lock_guard lock(mutex);permits=std::numeric_limits<unsigned>::max()/2;}
    changed.notify_all();references.WaitForIdleForTesting();references.SetTestHooks({});
  });
  auto get=[&](std::size_t r,const std::string& suffix){return Request(server.port(),ReferenceGet(
      server.port(),"/v1/references/"+ids[r]+suffix,settings.ReferenceCapability(ids[r])));};
  auto compact=[&](std::size_t r){return Request(server.port(),ReferenceGet(server.port(),
      "/r/"+ids[r]+settings.ReferenceCapability(ids[r])+"/metadata"));};
  Check(get(0,"/annotated.png").status==202,"first full derivation is held");
  {std::unique_lock lock(mutex);Check(changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),"first full actually enters controlled derivation");}
  // Queue actual full+compact polling and all competitors while first full is held.
  for(std::size_t r=0;r<2;++r){get(r,"");compact(r);get(r,"/crop.png");get(r,"/annotated.png?layers=strokes");}
  {std::lock_guard lock(mutex);++permits;}changed.notify_all();
  references.WaitForIdleForTesting();
  bool seen[2][15]={};
  for(int round=0;round<48;++round){
    for(std::size_t r=0;r<2;++r){
      const auto full=get(r,""),small=compact(r);
      Check(full.status==200 && small.status==200 &&
          full.body.find(ids[r])!=std::string::npos && small.body.find(ids[r])!=std::string::npos &&
          full.body.find(expected[r][0].sourceSHA)!=std::string::npos &&
          small.body.find(expected[r][0].sourceSHA)!=std::string::npos,
          "real full/compact metadata retain exact reference and source identity");
      for(unsigned mark=0;mark<12;++mark)Check(small.body.find(StableMarkId(ids[r],mark))!=std::string::npos,
          "actual compact poll retains all twelve stable immutable slot IDs");
      std::vector<std::string> suffixes={"/annotated.png","/crop.png"};
      for(std::size_t mark=0;mark<12;++mark)suffixes.push_back("/marks/"+StableMarkId(ids[r],mark)+".png");
      suffixes.push_back("/annotated.png?layers=strokes");
      for(std::size_t asset=0;asset<suffixes.size();++asset){
        Response result;
        if(asset>=2&&asset<14)result=Request(server.port(),ReferenceGet(server.port(),
            "/r/"+ids[r]+settings.ReferenceCapability(ids[r])+suffixes[asset]));
        else result=get(r,suffixes[asset]);
        Check(result.status==200||result.status==202,"mixed representation must be ready or pending");
        if(result.status==200){
          CheckPngResponse(result,expected[r][asset].digest,expected[r][asset].size);
          Check(!PngResponseMatches(result,expected[1-r][asset].digest,expected[1-r][asset].size),
              "actual valid PNG from one reference must fail the other reference's exact integrity gate");
          seen[r][asset]=true;
        }
      }
    }
    {std::lock_guard lock(mutex);++permits;}changed.notify_all();
    references.WaitForIdleForTesting();
    Check(derivations<=96,"mixed service exceeds frozen 96 derivation budget");
    bool done=true;for(const auto& row:seen)for(bool ready:row)done&=ready;
    if(done)break;
  }
  std::cout<<"mixed_service_progress,first830_underlay="<<seen[0][14]<<",annotated_derivations="<<derivations<<'\n';
  Check(seen[0][14],"named starvation victim first830 underlay did not become ready within48 rounds/96 derivations");
  for(const auto& row:seen)for(bool ready:row)Check(ready,"every mixed service asset must be ready at least once, not simultaneously resident");
  references.SetTestHooks({});server.Stop();
}

void TestAbandonedMetadataUnderlayProgress() {
  TempDirectory temp;const std::string oldID(32,'a'),victimID(32,'b');
  const auto directory=temp.path/"references";
  {ReferenceStore creator(directory);MakeReference(creator,oldID,0,false,true);MakeReference(creator,victimID,0,false,true);}
  ReferenceStore oracle(temp.path/"oracle");
  const auto frozen=MakeReference(oracle,victimID,0,false,true);
  const auto full=AwaitAsset(oracle,victimID,0),mark=AwaitAsset(oracle,victimID,2);
  ReferenceStore references(directory);SettingsStore settings(temp.path/"settings.json");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(references,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"abandoned metadata service starts");
  auto get=[&](const std::string& id,const std::string& suffix){return Request(server.port(),
      ReferenceGet(server.port(),"/v1/references/"+id+suffix,settings.ReferenceCapability(id)));};
  Check(get(oldID,"").status==200,"ordinary client polls full metadata only");
  references.WaitForIdleForTesting();
  Check(get(victimID,"").body.find(frozen.value.source.sha256)!=std::string::npos,
      "later ordinary client is bound to its own frozen source");
  Response numbered,numberedMark;bool fullReady=false,markReady=false;
  for(unsigned poll=0;poll<12 && (!fullReady||!markReady);++poll){
    numbered=get(victimID,"/annotated.png");
    numberedMark=Request(server.port(),ReferenceGet(server.port(),"/r/"+victimID+
      settings.ReferenceCapability(victimID)+"/marks/"+StableMarkId(victimID,0)+".png"));
    if(numbered.status==200){CheckPngResponse(numbered,full.byte_sha256,PngSize(*full.bytes));fullReady=true;}
    else Check(numbered.status==202,"later numbered full remains truthful pending");
    if(numberedMark.status==200){CheckPngResponse(numberedMark,mark.byte_sha256,PngSize(*mark.bytes));markReady=true;}
    else Check(numberedMark.status==202,"later numbered mark remains truthful pending");
    references.WaitForIdleForTesting();
  }
  Check(fullReady&&markReady,"abandoned metadata-queued underlay cannot starve later exact numbered full or mark assets");
  // Deliberately never request ?layers=strokes in this ordinary-client test.
  server.Stop();
}

void TestDisplayUnderlayHttp() {
  TempDirectory temp;
  ReferenceStore references(temp.path/"references");
  const std::string id(32,'7'), legacyID(32,'8');
  const auto stored=MakeReference(references,id,0,false,true,0,true);
  const std::string expiredID(32,'9');
  MakeReference(references,expiredID,120,false,true);
  const auto legacy=MakeReference(references,legacyID,0);
  SettingsStore settings(temp.path/"settings.json");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(references,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"underlay service starts");
  const auto token=settings.ReferenceCapability(id),legacyToken=settings.ReferenceCapability(legacyID);
  const auto root="/v1/references/"+id;
  const auto agent="/r/"+id+token;
  const auto query=root+"/annotated.png?layers=strokes";
  auto get=[&](const std::string& path,std::string_view auth){return Request(server.port(),ReferenceGet(server.port(),path,auth));};
  auto post=ReferenceGet(server.port(),query,token);post.replace(0,3,"POST");
  Check(Request(server.port(),post).status==405,"underlay retains GET-only method policy");
  Check(get(query,{}).status==401 && get(query,"wrong").status==403,
      "underlay retains bearer authentication");
  Check(Request(server.port(),ReferenceGet(server.port(),query,token,"https://foreign.example")).status==403 &&
        Request(server.port(),ReferenceGet(server.port(),query,token,std::nullopt,"localhost")).status==400,
      "underlay retains exact Host and Origin guards");
  for(const auto& suffix:{"?layers","?layers=","?layers=numbered","?layers=Strokes",
      "?Layers=strokes","?layers=strokes&layers=strokes","?layers=strokes&extra=1",
      "?layers=strokes?","?layers=%73trokes","?layers=strokes#x","?%6cayers=strokes"})
    Check(get(root+"/annotated.png"+suffix,token).status==400,
        "noncanonical layer target rejects without numbered fallback");
  for(const auto& suffix:{"/source.png?layers=strokes","/crop.png?layers=strokes",
      "/view?layers=strokes","/metadata?layers=strokes","?layers=strokes"})
    Check(get(root+suffix,token).status==400,"other v1 query behavior stays rejected");
  for(const auto& suffix:{"?layers=strokes","/metadata?layers=strokes","/detail?layers=strokes",
      "/source.png?layers=strokes","/crop.png?layers=strokes","/annotated.png?layers=strokes"})
    Check(get(agent+suffix,{}).status==400,"all r query behavior remains unchanged");
  auto first=get(root,token);
  Check(first.status==200 && first.body.find("\"display_underlay\":{")!=std::string::npos &&
        first.body.find("\"representation\":\"strokes-only-v1\"")!=std::string::npos &&
        first.body.find(std::string(ReferenceStore::kDisplayUnderlayGeometryVersion))!=std::string::npos,
      "full v1 schema2 metadata includes supported descriptor");
  references.WaitForIdleForTesting();
  Response asset;
  for(int n=0;n<12;++n){asset=get(query,token);if(asset.status==200)break;Check(asset.status==202,"underlay pending stays retryable");references.WaitForIdleForTesting();}
  Check(asset.status==200,"underlay reaches ready within bounded derivations");
  const auto digest=Sha256(Bytes(asset.body.begin(),asset.body.end()));
  Check(Header(asset,"X-SeeThis-PNG-SHA256")==digest && Header(asset,"ETag")=="\"sha256-"+digest+"\"",
      "underlay response hash and ETag match its actual bytes");
  const auto full=get(root,token),alias=get(root+"/metadata",token);
  Check(full.body==alias.body && full.body.find(query)!=std::string::npos &&
        full.body.find(digest)!=std::string::npos,"full alias has same ready underlay descriptor");
  for(const auto& suffix:{"","/metadata","/detail","/record"})
    Check(get(agent+suffix,{}).body.find("display_underlay")==std::string::npos,
        "r compact and full detail shapes remain descriptor-free");
  const auto compact=get(agent+"/metadata",{});
  const auto& ref=stored.value;
  const auto mark=references.MarkSnapshots(id).marks.front();
  const auto display=std::find_if(ref.context.displays.begin(),ref.context.displays.end(),[&](const auto& d){return d.id==mark.anchor.display_id;});
  const double x=ref.global_to_source.a*(display->logical.x+mark.anchor.position.x)+ref.global_to_source.tx;
  const double y=ref.global_to_source.d*(display->logical.y+mark.anchor.position.y)+ref.global_to_source.ty;
  std::ostringstream position;position<<std::setprecision(std::numeric_limits<double>::max_digits10)<<"\"pos\":{\"x\":"<<x<<",\"y\":"<<y<<"}";
  Check(compact.body.find(position.str())!=std::string::npos && compact.body.find("\"capture\"")==std::string::npos &&
        compact.body.find("\"anchor\":{\"display_id\":1,\"x\":60.2346")!=std::string::npos,
      "only existing compact source pos round-trips doubles without changing field shape");
  Check(get(query,legacyToken).status==403,"underlay rejects other reference capability");
  Check(legacy.value.schema==1,"legacy regression fixture is actually schema1");
  {
    const auto legacyRoot="/v1/references/"+legacyID;
    Check(get(legacyRoot,legacyToken).body.find("display_underlay")==std::string::npos &&
          get(legacyRoot+"/annotated.png?layers=strokes",legacyToken).status==422,
        "schema1 descriptor stays absent and opt-in is unsupported after authentication");
  }
  Check(references.Delete(id)==DeleteResult::kDeleted && get(query,token).status==410,
      "deletion invalidates underlay with existing lifecycle response");
  const std::string pendingID(32,'6');auto pending=stored.value;
  pending.id=pending.mark_id=pendingID;
  Check(references.AcceptPending(pending,10,20).state==ReferenceJobState::kPending,
      "schema2 pending variant fixture admitted");
  const auto pendingQuery="/v1/references/"+pendingID+"/annotated.png?layers=strokes";
  const auto pendingToken=settings.ReferenceCapability(pendingID);
  Check(get(pendingQuery,{}).status==401&&get(pendingQuery,pendingToken).status==202,
      "variant authentication precedes truthful pending lifecycle");
  references.Fail(pendingID,"capture_failed");references.WaitForIdleForTesting();
  Check(get(pendingQuery,pendingToken).status==422,"variant retains failed lifecycle");
  auto expiryPolicy=settings.Get();expiryPolicy.reference_ttl_seconds=60;
  Check(settings.Update(expiryPolicy,&error),"expiry fixture updates the canonical validated settings authority");
  const auto expiredQuery="/v1/references/"+expiredID+"/annotated.png?layers=strokes";
  const auto expiredUnauth=get(expiredQuery,{});
  const auto expired=get(expiredQuery,settings.ReferenceCapability(expiredID));
  Check(expiredUnauth.status==401 && expired.status==410 &&
        expired.body.find("\"reference_id\":\""+expiredID+"\"")!=std::string::npos &&
        expired.body.find("\"state\":\"expired\"")!=std::string::npos &&
        Header(expired,"Content-Type")=="application/json; charset=utf-8",
      "variant expiry remains authenticated and cannot serve stale bytes");
  server.Stop();
}

void TestDisplayUnderlayViewer() {
  TempDirectory temp;ReferenceStore references(temp.path/"references");
  SettingsStore settings(temp.path/"settings.json");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(references,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"viewer extractor server starts");
  const auto script=Request(server.port(),ReferenceGet(server.port(),"/v1/reference-view.js"));
  Check(script.status==200,"production viewer script fetched");
  RunEmbeddedViewerCanvasHarness(script.body);server.Stop();
}

void TestIdleSettingsMigration() {
  TempDirectory temp;
  const std::string secret(64, '4');
  const std::string reference_id(32, '8');
  const std::string material = secret + ":reference-read:" + reference_id;
  const auto expected_capability = Sha256(Bytes(material.begin(), material.end()));
  const auto fixture = [&](int schema, std::int64_t hold,
                           const std::string& extra = "") {
    return "{\"schema\":" + std::to_string(schema) +
        ",\"shortcut_key_code\":12,\"shortcut_modifiers\":9," +
        (schema == 1 ? std::string() :
         "\"delete_shortcut_key_code\":7,\"delete_shortcut_modifiers\":6,") +
        "\"maximum_hold_ms\":" + std::to_string(hold) +
        ",\"crop_margin_points\":8.123456789012345," +
        "\"mark_hit_radius_points\":10.987654321098765," +
        "\"reference_ttl_seconds\":123456,\"maximum_visible_references\":123," +
        "\"reference_secret\":\"" + secret + "\"" + extra + "}\n";
  };
  for (const int schema : {1, 2, 3}) {
    for (const std::int64_t hold : {30'000, 45'000, 120'000}) {
      const auto path = temp.path /
          ("settings-" + std::to_string(schema) + "-" + std::to_string(hold) + ".json");
      const std::string original = fixture(schema, hold);
      { std::ofstream(path) << original; }
      SettingsStore migrated(path);
      const auto result = migrated.Get();
      const auto expected_hold = schema < 3 && hold == 30'000 ? 300'000 : hold;
      Check(migrated.usable() && result.maximum_hold_ms == expected_hold &&
                result.shortcut_key_code == 12 && result.shortcut_modifiers == 9 &&
                result.delete_shortcut_key_code == (schema == 1 ? 2 : 7) &&
                result.delete_shortcut_modifiers == (schema == 1 ? 4 : 6) &&
                result.crop_margin_points == 8.123456789012345 &&
                result.mark_hit_radius_points == 10.987654321098765 &&
                result.reference_ttl_seconds == 123456 &&
                result.maximum_visible_references == 123 &&
                migrated.ReferenceCapability(reference_id) == expected_capability,
            "persisted migration changes only the legacy timeout default and missing delete binding");
      const auto bytes = FileBytes(path);
      const std::string stored(bytes.begin(), bytes.end());
      Check(stored.find("\"schema\":4") != std::string::npos &&
                stored.find("\"maximum_hold_ms\":" + std::to_string(expected_hold)) != std::string::npos,
            "candidate upgrade actually persists schema 4 and effective timeout");
      SettingsStore reloaded(path);
      Check(reloaded.usable() && reloaded.Get().maximum_hold_ms == expected_hold &&
                reloaded.Get().crop_margin_points == result.crop_margin_points &&
                reloaded.Get().mark_hit_radius_points == result.mark_hit_radius_points &&
                reloaded.ReferenceCapability(reference_id) == expected_capability &&
                FileBytes(path) == bytes,
            "migration is idempotent and preserves fractional settings and reference capabilities");
    }
  }
  for (const int schema : {2, 3, 5}) {
    const auto path = temp.path / ("unsupported-" + std::to_string(schema) + ".json");
    const auto original = fixture(schema, 30'000, schema == 5 ? "" : ",\"future_field\":7");
    { std::ofstream(path) << original; }
    const auto bytes = FileBytes(path);
    SettingsStore unsupported(path);
    std::string error;
    Check(unsupported.load_state() == SettingsLoadState::kUnsupported &&
              !unsupported.Update(Settings{}, &error) && FileBytes(path) == bytes,
          "unknown-key and future-schema files remain byte-preserved and reject updates");
  }
  Settings defaults;
  std::string error;
  const auto parsed_defaults = ParseSettingsJson(SettingsJson(defaults), &error);
  Check(defaults.maximum_hold_ms == 300'000 && ValidateSettings(defaults, &error) &&
            parsed_defaults && parsed_defaults->maximum_hold_ms == 300'000,
        "fresh five-minute default validates and round-trips through public API schema 2");
  for (const std::int64_t hold : {999, 300'001}) {
    defaults.maximum_hold_ms = hold;
    Check(!ValidateSettings(defaults, &error), "idle setting rejects out-of-range limits");
  }
  defaults.maximum_hold_ms = 1'000;
  Check(ValidateSettings(defaults, &error), "existing minimum custom idle interval remains valid");
}

void TestGlobalVisibility() {
  TempDirectory temp;
  const auto root=temp.path/"references",path=temp.path/"settings.json";
  const std::string a(32,'a'),b(32,'b');
  ReferenceStore references(root);
  MakeReference(references,a,0,false,true);MakeReference(references,b,0,false,true);
  for(const auto& id:{a,b}) {
    auto notes=references.MarkSnapshots(id);
    Check(notes.marks.size()==2,"global fixture has two independent slots");
    Check(references.UpdateMarkNote(id,notes.marks[0].id,"matching note 世界\nline two",0).status==MarkNoteUpdateStatus::kUpdated,
          "global fixture saves exact note owner");
    Check(references.UpdateMarkVisibility(id,notes.marks[1].id,false,1).status==MarkNoteUpdateStatus::kUpdated,
          "global fixture has mixed dormant bits");
  }
  const auto aNotes=FileBytes(root/(a+".notes")),bNotes=FileBytes(root/(b+".notes"));
  // Upgrade a real schema3 document while historical per-mark bits are mixed.
  std::string legacy=SettingsJson(Settings{});
  legacy.replace(legacy.find("\"schema\":2"),10,"\"schema\":3");
  legacy.insert(legacy.rfind('}'),",\"reference_secret\":\""+std::string(64,'c')+"\"");
  std::ofstream(path)<<legacy;
  std::atomic_int fault=0;
  SettingsStore settings(path,[&](SettingsPersistPoint point) {
    return !(fault==1&&point==SettingsPersistPoint::kBeforeRename) &&
           !(fault==2&&point==SettingsPersistPoint::kDirectorySync);
  });
  const auto capability=settings.ReferenceCapability(a);
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(references,settings,temp.path/"web",readiness);
  std::string error;Check(server.Start(&error),"global visibility service starts");
  const auto get=[&](const std::string& id) {
    return Request(server.port(),ReferenceGet(server.port(),"/r/"+id+settings.ReferenceCapability(id)+"/metadata"));
  };
  for(const auto& id:{a,b})for(std::size_t slot=0;slot<2;++slot) {
    (void)references.RequestReadyMark(id,slot);references.WaitForIdleForTesting();
  }
  const auto normalized=[](std::string body) {
    const auto start=body.find("\"global_visibility\":\"")+21;
    const auto end=body.find('"',start);body.replace(start,end-start,"STATE");
    std::size_t offset=0;
    while((offset=body.find("\"visible\":",offset))!=std::string::npos) {
      offset+=10;const auto length=body.compare(offset,4,"true")==0?4:5;
      body.replace(offset,length,"BOOL");offset+=4;
    }
    return body;
  };
  const std::map<std::string,std::string> immutable={{a,normalized(get(a).body)},{b,normalized(get(b).body)}};
  const auto assertProjection=[&](const Response& response,const std::string& state) {
    Check(response.status==200 && response.body.find("\"global_visibility\":\""+state+"\"")!=std::string::npos,
          "compact metadata reports exact confirmed/error global state");
    const bool isA=response.body.find("\"reference_id\":\""+a+"\"")!=std::string::npos;
    Check(state=="unavailable" || normalized(response.body)==immutable.at(isA?a:b),
          "global projection preserves every non-visibility note/number/position/hash/context/asset byte");
    const std::string wanted="\"visible\":"+std::string(state=="visible"?"true":"false");
    const std::string forbidden="\"visible\":"+std::string(state=="visible"?"false":"true");
    auto first=response.body.find(wanted),second=response.body.find(wanted,first+1);
    Check(first!=std::string::npos&&second!=std::string::npos&&response.body.find(forbidden)==std::string::npos,
          "every mark boolean derives from the same global snapshot");
    Check(response.body.find("\"notes_revision\":2")!=std::string::npos &&
          response.body.find("matching note 世界\\nline two")!=std::string::npos &&
          response.body.find("\"number\":1")!=std::string::npos &&
          response.body.find("\"number\":2")!=std::string::npos &&
          response.body.find("\"pos\":{")!=std::string::npos &&
          response.body.find("\"sha256\":\"")!=std::string::npos,
          "global projection retains notes, numbering, positions and screenshot hash");
  };
  for(const auto& id:{a,b})assertProjection(get(id),"visible");
  Check(settings.SetGlobalVisibility(false,&error),"one global Invisible save succeeds");
  for(const auto& id:{b,a})assertProjection(get(id),"invisible");
  SettingsStore restarted(path);
  Check(restarted.Visibility().state==GlobalVisibilityState::kInvisible &&
        restarted.ReferenceCapability(a)==capability,"Invisible and original capability survive restart");
  auto changed=settings.Get();changed.crop_margin_points=17.25;changed.maximum_hold_ms=45000;
  Check(Request(server.port(),PutSettings(server.settings_capability_for_testing(),SettingsJson(changed),server.base_url())).status==200,
        "old schema2 public PUT commits unrelated settings");
  Check(settings.Visibility().state==GlobalVisibilityState::kInvisible &&
        settings.Get().crop_margin_points==17.25&&settings.ReferenceCapability(a)==capability,
        "old PUT retains internal visibility, fractional settings and capability");
  const auto before=FileBytes(path);fault=1;
  Check(!settings.SetGlobalVisibility(true,&error) && FileBytes(path)==before &&
        settings.Visibility().state==GlobalVisibilityState::kInvisible,
        "production pre-rename failure keeps prior confirmed memory and bytes");
  assertProjection(get(a),"invisible");
  fault=2;
  Check(!settings.SetGlobalVisibility(true,&error)&&FileBytes(path)!=before&&
        settings.Visibility().state==GlobalVisibilityState::kUncertain,
        "production post-rename sync failure is explicitly uncertain");
  const auto renamed=FileBytes(path);
  for(int i=0;i<4;++i) {assertProjection(get(i%2?a:b),"uncertain");
    Check(FileBytes(path)==renamed&&settings.Visibility().state==GlobalVisibilityState::kUncertain,
          "equal-byte readback never promotes uncertain write");}
  SettingsStore uncertainRestart(path,[](SettingsPersistPoint p){return p!=SettingsPersistPoint::kDirectorySync;});
  Check(uncertainRestart.Visibility().state==GlobalVisibilityState::kUncertain,
        "restart with failed actual sync cannot establish Saved");
  SettingsStore confirmedRestart(path);
  Check(confirmedRestart.Visibility().state==GlobalVisibilityState::kVisible&&FileBytes(path)==renamed,
        "restart confirms actual file and directory durability without byte rewrite");
  fault=0;Check(settings.SetGlobalVisibility(true,&error),"successful durable retry confirms Visible");
  for(const auto& id:{a,b})assertProjection(get(id),"visible");
  std::atomic_bool start=false;
  std::atomic_bool writesOkay=true;
  std::thread toggles([&]{while(!start.load())std::this_thread::yield();
    for(int i=0;i<120;++i)if(!settings.SetGlobalVisibility(i%2==0))writesOkay=false;});
  std::thread updates([&]{while(!start.load())std::this_thread::yield();
    for(int i=0;i<80;++i) {auto publicSettings=settings.Get();publicSettings.crop_margin_points=20+i%4;
      if(!settings.Update(publicSettings))writesOkay=false;}});
  start=true;
  for(int i=0;i<100;++i) {
    const auto response=get(i%2?a:b);
    const bool visible=response.body.find("\"global_visibility\":\"visible\"")!=std::string::npos;
    assertProjection(response,visible?"visible":"invisible");
  }
  toggles.join();updates.join();
  Check(writesOkay&&settings.Visibility().state==GlobalVisibilityState::kInvisible &&
        settings.ReferenceCapability(a)==capability,
        "toggle and old public updates serialize under one store lock without reset or secret rotation");
  SettingsStore finalRestart(path);
  Check(finalRestart.Visibility().state==GlobalVisibilityState::kInvisible&&
        finalRestart.Get().crop_margin_points==23 && finalRestart.Get().maximum_hold_ms==45000,
        "last serialized global and public settings persist together across restart");
  Check(FileBytes(root/(a+".notes"))==aNotes&&FileBytes(root/(b+".notes"))==bNotes,
        "global migration and toggles leave dormant sidecar bytes and revisions untouched");
  server.Stop();
  auto bytes=FileBytes(path);std::string malformed(bytes.begin(),bytes.end());
  malformed.insert(malformed.rfind('}'),",\"future_field\":1");std::ofstream(path)<<malformed;
  SettingsStore unavailable(path);
  Check(unavailable.Visibility().state==GlobalVisibilityState::kUnavailable &&
        unavailable.ReferenceCapability(a)==capability&&!unavailable.ToggleGlobalVisibility(&error),
        "unsupported settings retain capability and suppress all presentation without rewrite");
  seethis::service::ReferenceServer unavailableServer(references,unavailable,temp.path/"web",readiness);
  Check(unavailableServer.Start(&error),"unsupported settings read service remains available");
  const auto response=Request(unavailableServer.port(),ReferenceGet(unavailableServer.port(),
      "/r/"+a+capability+"/metadata"));
  assertProjection(response,"unavailable");
  Check(FileBytes(path)==Bytes(malformed.begin(),malformed.end()),"unsupported settings stay byte preserved after reads");
  unavailableServer.Stop();
}

void TestSettingsAssets() {
  TempDirectory temp;
  const auto web=temp.path/"web";
  std::filesystem::create_directories(web);
  {std::ofstream(web/"index.html",std::ios::binary)<<"fixture html";}
  {std::ofstream(web/"settings.js",std::ios::binary)<<"fixture js";}
  ReferenceStore references(temp.path/"references");
  SettingsStore settings(temp.path/"settings.json");
  auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  seethis::service::ReferenceServer server(references,settings,web,readiness);
  Check(server.Start(),"focused settings server starts");
#ifdef SEETHIS_COMPILED_SETTINGS_ASSETS
  const auto html_bytes=FileBytes(std::filesystem::path(SEETHIS_TEST_SETTINGS_SOURCE_DIR)/"index.html");
  const auto js_bytes=FileBytes(std::filesystem::path(SEETHIS_TEST_SETTINGS_SOURCE_DIR)/"settings.js");
  const std::string html(html_bytes.begin(),html_bytes.end());
  const std::string javascript(js_bytes.begin(),js_bytes.end());
#else
  const std::string html="fixture html", javascript="fixture js";
#endif
  auto response=Request(server.port(),Get("/settings/"));
  Check(response.status==200&&response.body==html,"actual HTML route preserves exact bytes");
  Check(Header(response,"Content-Type")=="text/html; charset=utf-8"&&
      Header(response,"Content-Security-Policy").find("frame-ancestors 'none'")!=std::string::npos,
      "HTML content type and CSP preserved");
  Check(Request(server.port(),Get("/settings")).body==html,"existing HTML alias preserved");
  response=Request(server.port(),Get("/settings/settings.js"));
  Check(response.status==200&&response.body==javascript&&
      Header(response,"Content-Type")=="text/javascript; charset=utf-8",
      "actual JS route preserves exact bytes and type");
  Check(response.bytes.find(server.settings_capability_for_testing())==std::string::npos,
      "compiled or fixture assets do not embed settings capability");
  Check(Request(server.port(),Get("/settings/index.html")).status==404,
      "unknown settings filename does not become a file route");
  Check(Request(server.port(),Get("/settings/../index.html")).status==400,
      "traversal remains rejected");
  Check(Request(server.port(),"POST /settings/ HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").status==405,
      "settings route method guard preserved");
  std::filesystem::remove(web/"index.html");
#ifdef SEETHIS_COMPILED_SETTINGS_ASSETS
  Check(Request(server.port(),Get("/settings/")).body==html,
      "Store route ignores missing bundle disk asset without fallback");
  Check(mkfifo((web/"index.html").c_str(),0600)==0,"fixture FIFO created");
  Check(Request(server.port(),Get("/settings/")).body==html,
      "Store route ignores nonregular disk asset without reading it");
#else
  Check(Request(server.port(),Get("/settings/")).status==503,"default missing asset rejected");
  std::filesystem::create_directory(web/"index.html");
  Check(Request(server.port(),Get("/settings/")).status==503,"default directory rejected");
  std::filesystem::remove(web/"index.html");
  Check(mkfifo((web/"index.html").c_str(),0600)==0,"fixture FIFO created");
  Check(Request(server.port(),Get("/settings/")).status==503,"default FIFO rejected without blocking");
  std::filesystem::remove(web/"index.html");
  std::filesystem::create_symlink("/dev/null",web/"index.html");
  Check(Request(server.port(),Get("/settings/")).status==503,"default device rejected without blocking");
  std::filesystem::remove(web/"index.html");
  const std::string boundary(256*1024,'x');
  {std::ofstream(web/"index.html",std::ios::binary).write(boundary.data(),boundary.size());}
  Check(Request(server.port(),Get("/settings/")).body==boundary,"default maximum size boundary accepted");
  {std::ofstream(web/"index.html",std::ios::binary|std::ios::app)<<"x";}
  Check(Request(server.port(),Get("/settings/")).status==503,"default oversized asset rejected");
#endif
  server.Stop();
}

}  // namespace

int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string_view(argv[1])=="--finder-instance-only") {TestExplicitScopeDelivery(true);return 0;}
    if(argc==2&&std::string_view(argv[1])=="--context-fallback-only") {TestExplicitScopeDelivery();return 0;}
    if(argc==2&&std::string_view(argv[1])=="--p4-component-only") {
      TestMultiShotDelivery();TestMultiShotViewer();TestSingleImageDelivery();
      std::cout<<checks<<" P4 protocol checks passed\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--global-visibility-only") {
      TestIdleSettingsMigration();TestGlobalVisibility();
      std::cout<<checks<<" global visibility checks passed\n";return 0;
    }
    TestMultiShotDelivery();TestMultiShotViewer();
    TestSingleImageDelivery();
    if(argc==2&&std::string_view(argv[1])=="--single-image-only") {
      std::cout<<checks<<" single-image delivery checks passed\n";return 0;
    }
    TestCompactSourceContext();
    if(argc==2&&std::string_view(argv[1])=="--compact-source-context-only") {
      TestChromeTransactionIdentityFreeze();
      std::cout<<checks<<" compact source-context checks passed\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--shared-underlay-only") {
      TestDisplayUnderlayHttp();TestDisplayUnderlayServiceProgress();TestAbandonedMetadataUnderlayProgress();TestDisplayUnderlayViewer();
      std::cout<<checks<<" shared underlay service checks passed\n";return 0;
    }
    TestSettingsAssets();
    if(argc==2&&std::string_view(argv[1])=="--settings-assets-only") {
      std::cout<<checks<<" focused settings HTTP checks passed\n";return 0;
    }
    TestUrlOnlyControllerCompletion();
    TestChromeTransactionIdentityFreeze();
    TestAnnotatedInterleaving();
    TestDisplayUnderlayHttp();
    TestDisplayUnderlayServiceProgress();
    TestAbandonedMetadataUnderlayProgress();
    TestIdleSettingsMigration();
    TestGlobalVisibility();
    TempDirectory temp;
    const auto legacy_path = temp.path / "legacy-settings.json";
    const std::string legacy_secret(64, '1');
    {
      std::ofstream legacy(legacy_path);
      legacy << "{\"schema\":1,\"shortcut_key_code\":12,"
                "\"shortcut_modifiers\":9,\"maximum_hold_ms\":30000,"
                "\"crop_margin_points\":8,\"mark_hit_radius_points\":10,"
                "\"reference_ttl_seconds\":604800,"
                "\"maximum_visible_references\":500,"
                "\"reference_secret\":\"" << legacy_secret << "\"}\n";
    }
    SettingsStore migrated_settings(legacy_path);
    const auto migrated_bytes = FileBytes(legacy_path);
    const std::string migrated_text(migrated_bytes.begin(),migrated_bytes.end());
    const std::string migration_id(32,'8');
    const std::string expected_material =
        legacy_secret + ":reference-read:" + migration_id;
    Check(migrated_settings.Get().shortcut_key_code == 12 &&
              migrated_settings.Get().shortcut_modifiers == 9 &&
              migrated_settings.Get().delete_shortcut_key_code == 2 &&
              migrated_settings.Get().delete_shortcut_modifiers ==
                  kShortcutOption &&
              migrated_settings.ReferenceCapability(migration_id) ==
                  Sha256(Bytes(expected_material.begin(),expected_material.end())) &&
              migrated_settings.Get().maximum_hold_ms == 300'000 &&
              migrated_text.find("\"schema\":4") != std::string::npos &&
              migrated_text.find("\"delete_shortcut_key_code\":2") !=
                  std::string::npos,
          "legacy settings migrate idle default and preserve capture and secret while adding Option+D");
    const auto settings_path = temp.path / "settings.json";
    SettingsStore settings(settings_path);
    Check(std::filesystem::exists(settings_path), "default settings persist");
    struct stat info{};
    Check(stat(settings_path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
          "settings file is owner-only");
    const auto first_capability = settings.ReferenceCapability(std::string(32, 'a'));
    SettingsStore reloaded(settings_path);
    Check(reloaded.ReferenceCapability(std::string(32, 'a')) == first_capability,
          "reference capability remains stable across restart");
    Check(SettingsJson(settings.Get()).find("secret") == std::string::npos,
          "public settings omit capability root");
    Check(settings.Get().shortcut_key_code == 0 &&
              settings.Get().shortcut_modifiers == kShortcutOption &&
              settings.Get().delete_shortcut_key_code == 2 &&
              settings.Get().delete_shortcut_modifiers == kShortcutOption &&
              settings.Get().maximum_hold_ms == 300'000 &&
              reloaded.Get().maximum_hold_ms == 300'000,
          "new settings persist five-minute inactivity and keep distinct Option+A and Option+D");
    std::string parse_error;
    Check(!ParseSettingsJson("{}", &parse_error), "missing settings rejected");
    Check(!ParseSettingsJson("{\"schema\":1 garbage}", &parse_error),
          "malformed JSON syntax is rejected");
    const std::string public_legacy =
        "{\"schema\":1,\"shortcut_key_code\":0,\"shortcut_modifiers\":4,"
        "\"maximum_hold_ms\":30000,\"crop_margin_points\":8,"
        "\"mark_hit_radius_points\":10,\"reference_ttl_seconds\":604800,"
        "\"maximum_visible_references\":500}";
    Check(!ParseSettingsJson(public_legacy,&parse_error),
          "public schema-1 update cannot erase the separate delete binding");
    auto invalid = settings.Get();
    invalid.shortcut_modifiers = 0;
    Check(!settings.Update(invalid, &parse_error), "invalid native settings rejected");
    auto conflict = settings.Get();
    conflict.delete_shortcut_key_code = conflict.shortcut_key_code;
    conflict.delete_shortcut_modifiers = conflict.shortcut_modifiers;
    Check(!settings.Update(conflict,&parse_error)&&
              settings.Get().delete_shortcut_key_code == 2,
          "capture/delete binding conflicts are rejected without mutation");

    const auto reference_root = temp.path / "references";
    const std::string current_id(32, 'a');
    const std::string old_id(32, 'b');
    const std::string deleted_id(32, '9');
    const std::string multi_id(32, '8');
    const std::string queued_multi_id(32, '7');
    StoredReference current, old, deleted, multi, queued_multi;
    {
      ReferenceStore creator(reference_root);
      current = MakeReference(creator, current_id, 0, true);
      old = MakeReference(creator, old_id, 120);
      deleted = MakeReference(creator, deleted_id, 0);
      multi = MakeReference(creator, multi_id, 0, false, true);
      queued_multi=MakeReference(creator,queued_multi_id,0,false,true);
    }
    Check(std::filesystem::exists(reference_root/(multi_id+".source.png"))&&
              std::filesystem::exists(reference_root/(multi_id+".ready"))&&
              !std::filesystem::exists(reference_root/(multi_id+".stref"))&&
              !std::filesystem::exists(reference_root/(multi_id+".crop.png")),
          "schema-2 service fixture stores one primary image and small metadata");
    ReferenceStore references(reference_root);
    const auto cold = references.LookupMetadata(current_id);
    const auto cold_record_size =
        std::filesystem::file_size(reference_root / (current_id + ".stref"));
    Check(cold && cold->state == ReferenceJobState::kReady && cold->metadata &&
              !cold->ready && cold->metadata->source.bytes.empty() &&
              cold->metadata->crop.bytes.empty() &&
              cold_record_size >= 59'000'000,
          "cold restart exposes lightweight metadata for a representative 59 MB record");
    std::cout << "cold_record_bytes=" << cold_record_size << '\n';
    const auto web = temp.path / "web";
    std::filesystem::create_directories(web);
    const auto settings_assets = std::filesystem::path(__FILE__)
        .parent_path().parent_path() / "web" / "settings";
    std::filesystem::copy_file(settings_assets / "index.html", web / "index.html");
    std::filesystem::copy_file(settings_assets / "settings.js", web / "settings.js");
    auto readiness =
        std::make_shared<seethis::platform::PermissionReadinessState>();
    readiness->input_monitor.store(
        seethis::platform::InputMonitorState::kDenied);
    readiness->screen_recording.store(
        seethis::platform::ScreenRecordingState::kReady);
    readiness->delete_shortcut.store(
        seethis::platform::DeleteShortcutState::kInputUnavailable);
    readiness->app_name = "SeeThis Test";
    readiness->bundle_identifier = "local.seethis.overlay";
    readiness->bundle_path = "/Applications/SeeThis Test.app";
    seethis::service::ReferenceServer server(references, settings, web,
                                              readiness);
    std::string start_error;
    Check(server.Start(&start_error) && server.port() != 0, "server binds an ephemeral loopback port");
    const auto settings_token = server.settings_capability_for_testing();
    Check(settings_token.size() == 64, "settings capability has 256-bit material");
    std::vector<std::string> historical_publications;
    std::int64_t historical_change_count = 20;
    int historical_completion_publications = 0;
    MarkController historical_copy(
        references,
        [&](std::string_view id) -> std::optional<std::int64_t> {
          historical_publications.push_back(server.ClipboardText(id));
          return ++historical_change_count;
        },
        [&](std::shared_ptr<const StoredReference>, std::int64_t) {
          ++historical_completion_publications;
          return true;
        },
        [](std::function<void()> task) { task(); });
    historical_copy.SetViewportContinuity(1,true);
    Observe(historical_copy, current.value.context);
    struct HistoricalRecopyAttempt {
      bool copied = false;
      bool metadata_only = false;
      bool stable_url = false;
      long long elapsed_us = 0;
    };
    const auto expected_historical_url = server.ClipboardText(current_id);
    std::vector<HistoricalRecopyAttempt> historical_recopy_attempts;
    for(int attempt=0;attempt<220;++attempt) {
      const auto start=std::chrono::steady_clock::now();
      const bool copied=historical_copy.RecopyJob(current_id);
      const auto metadata=references.LookupMetadata(current_id);
      const bool metadata_only=metadata&&metadata->state==ReferenceJobState::kReady&&
                               !metadata->ready;
      const bool stable_url=!historical_publications.empty()&&
                            historical_publications.back()==expected_historical_url;
      const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now()-start).count();
      if(attempt>=20)historical_recopy_attempts.push_back(
          {copied,metadata_only,stable_url,elapsed});
    }
    std::vector<long long> sorted_historical_recopy;
    bool every_historical_recopy_succeeded=true;
    for(std::size_t index=0;index<historical_recopy_attempts.size();++index) {
      const auto& attempt=historical_recopy_attempts[index];
      const bool success=attempt.copied&&attempt.metadata_only&&attempt.stable_url;
      std::cout<<"large_historical_recopy_attempt,"<<index<<",status="
               <<(success?"published_metadata_only":"failed_or_materialized")
               <<",elapsed_us="<<attempt.elapsed_us<<'\n';
      every_historical_recopy_succeeded=
          every_historical_recopy_succeeded&&success;
      sorted_historical_recopy.push_back(attempt.elapsed_us);
    }
    std::sort(sorted_historical_recopy.begin(),sorted_historical_recopy.end());
    std::cout<<"large_historical_recopy_p95_us="
             <<sorted_historical_recopy[189]<<'\n';
    Check(cold_record_size>=27ULL*1024*1024&&
              historical_recopy_attempts.size()==200&&
              every_historical_recopy_succeeded&&
              historical_publications.size()==220&&
              historical_completion_publications==0,
          "20 warmups and 200 ordered 27 MiB historical recopies remain metadata-only");

    auto response = Request(server.port(), Get("/settings/"));
    Check(response.status == 200 && response.body.find("settings") != std::string::npos,
          "settings HTML is served without embedding capability");
    Check(response.bytes.find(settings_token) == std::string::npos,
          "settings capability is absent from HTTP response");
    Check(response.body.find("Maximum inactivity (milliseconds)") != std::string::npos &&
              response.body.find("min=\"1000\" max=\"300000\"") != std::string::npos,
          "served product settings form accepts the full supported inactivity range");
    Check(Request(server.port(), Get("/v1/settings")).status == 401,
          "missing settings bearer is explicit");
    Check(Request(server.port(), Get("/v1/settings", "wrong")).status == 403,
          "wrong settings bearer is forbidden");
    Check(Request(server.port(), Get("/v1/settings", settings_token)).status == 200,
          "scoped settings bearer reads settings");
    Check(Request(server.port(), Get("/v1/readiness")).status == 401,
          "readiness requires the settings bearer");
    Check(Request(server.port(), Get("/v1/readiness", "wrong")).status == 403,
          "readiness rejects the wrong settings bearer");
    response = Request(server.port(), Get("/v1/readiness", settings_token));
    Check(response.status == 200 &&
              response.body.find("\"state\":\"denied\",\"ready\":false") !=
                  std::string::npos &&
              response.body.find("\"state\":\"ready\",\"ready\":true") !=
                  std::string::npos,
          "readiness keeps input and screen authorization separate");
    Check(response.body.find(
              "\"delete_shortcut\":{\"state\":\"input_unavailable\",\"ready\":false") !=
              std::string::npos,
          "readiness reports delete observation independently");
    Check(response.body.find("local.seethis.overlay") != std::string::npos &&
              response.body.find("/Applications/SeeThis Test.app") !=
                  std::string::npos &&
              response.body.find("cdhash-bound grant") != std::string::npos,
          "readiness reports exact app identity, location and update policy");
    Check(response.body.find("Enable Input Monitoring for this exact app") !=
                  std::string::npos,
          "readiness exposes Input Monitoring recovery guidance");
    Check(response.body.find(settings_token) == std::string::npos,
          "readiness response does not disclose its capability");
    readiness->screen_recording.store(
        seethis::platform::ScreenRecordingState::kDenied);
    response = Request(server.port(), Get("/v1/readiness", settings_token));
    Check(response.status == 200 &&
              response.body.find("\"screen_recording\":{\"state\":\"denied\",\"ready\":false") !=
                  std::string::npos,
          "service reports denied Screen Recording independently");
    Check(response.body.find("Enable Screen Recording for this exact app") !=
                  std::string::npos,
          "readiness exposes Screen Recording recovery guidance independently");
    readiness->screen_recording.store(
        seethis::platform::ScreenRecordingState::kReady);
    const std::pair<seethis::platform::InputMonitorState, std::string_view>
        reported_states[] = {
            {seethis::platform::InputMonitorState::kTapCreationFailed,
             "tap_creation_failed"},
            {seethis::platform::InputMonitorState::kPartialTap, "partial_tap"},
            {seethis::platform::InputMonitorState::kDisabled, "disabled"},
            {seethis::platform::InputMonitorState::kRestartRequired,
             "restart_required"},
            {seethis::platform::InputMonitorState::kGranted, "granted"},
            {seethis::platform::InputMonitorState::kRecovered, "recovered"},
        };
    for (const auto& [state, name] : reported_states) {
      readiness->input_monitor.store(state);
      response = Request(server.port(), Get("/v1/readiness", settings_token));
      const std::string expected = "\"state\":\"" + std::string(name) +
          "\",\"ready\":" +
          (seethis::platform::InputMonitorReady(state) ? "true" : "false");
      Check(response.status == 200 &&
                response.body.find(expected) != std::string::npos,
            "service reports each deterministic input state");
    }
    const std::pair<seethis::platform::DeleteShortcutState,std::string_view>
        delete_states[] = {
          {seethis::platform::DeleteShortcutState::kUnknown,"unknown"},
          {seethis::platform::DeleteShortcutState::kInputUnavailable,
           "input_unavailable"},
          {seethis::platform::DeleteShortcutState::kConflict,"conflict"},
          {seethis::platform::DeleteShortcutState::kRegistrationFailed,
           "registration_failed"},
          {seethis::platform::DeleteShortcutState::kSecureInput,"secure_input"},
          {seethis::platform::DeleteShortcutState::kReady,"ready"},
        };
    for(const auto& [state,name]:delete_states) {
      readiness->delete_shortcut.store(state);
      response=Request(server.port(),Get("/v1/readiness",settings_token));
      const std::string expected="\"delete_shortcut\":{\"state\":\""+
          std::string(name)+"\",\"ready\":"+
          (seethis::platform::DeleteShortcutReady(state)?"true":"false");
      Check(response.status==200&&response.body.find(expected)!=std::string::npos,
            "service reports each deterministic delete-shortcut state");
    }
    const auto retry_before = readiness->retry_generation.load();
    Check(Request(server.port(), PostRetry("", "{}",
          server.base_url())).status == 401,
          "input retry requires the settings bearer");
    Check(Request(server.port(), PostRetry("wrong", "{}",
          server.base_url())).status == 403,
          "input retry rejects the wrong settings bearer");
    Check(Request(server.port(), Get("/v1/readiness/retry",
          settings_token)).status == 405,
          "input retry rejects unsupported methods");
    Check(Request(server.port(), PostRetry(settings_token, "{}",
          "http://evil.invalid")).status == 403,
          "input retry rejects a foreign Origin");
    Check(Request(server.port(), PostRetry(settings_token, "{}",
          server.base_url(), "text/plain")).status == 415,
          "input retry rejects unsupported content type");
    Check(Request(server.port(), PostRetry(settings_token, "{\"extra\":1}",
          server.base_url())).status == 422,
          "input retry rejects a nonempty request schema");
    Check(Request(server.port(), PostRetry(settings_token, "{}",
          server.base_url())).status == 202 &&
              readiness->retry_generation.load() == retry_before + 1,
          "authorized input retry publishes one bounded request");
    Check(settings.Get().shortcut_key_code == reloaded.Get().shortcut_key_code &&
              settings.Get().shortcut_modifiers ==
                  reloaded.Get().shortcut_modifiers,
          "input retry does not mutate saved shortcut settings");

    auto maximum = settings.Get();
    maximum.maximum_hold_ms = 300'000;
    Check(Request(server.port(), PutSettings(settings_token, SettingsJson(maximum),
          server.base_url())).status == 200 &&
              SettingsStore(settings_path).Get().maximum_hold_ms == 300'000,
          "five-minute public settings update saves and reloads successfully");
    const auto before_invalid_timeout = FileBytes(settings_path);
    maximum.maximum_hold_ms = 300'001;
    Check(Request(server.port(), PutSettings(settings_token, SettingsJson(maximum),
          server.base_url())).status == 422 &&
              FileBytes(settings_path) == before_invalid_timeout,
          "API rejects above-range inactivity without changing persisted settings");
    auto changed = settings.Get();
    changed.shortcut_key_code = 12;
    changed.shortcut_modifiers = kShortcutCommand | kShortcutShift;
    changed.reference_ttl_seconds = 60;
    changed.maximum_visible_references = 500;
    changed.maximum_hold_ms = 30'000;
    const std::string changed_json = SettingsJson(changed);
    const std::string origin = server.base_url();
    Check(Request(server.port(), PutSettings(settings_token,
          changed_json, "http://evil.invalid")).status == 403,
          "settings mutation rejects foreign Origin");
    Check(Request(server.port(), PutSettings(settings_token,
          changed_json, origin, "text/plain")).status == 415,
          "settings mutation rejects unsupported content type");
    Check(Request(server.port(), PutSettings(settings_token,
          "{}", origin)).status == 422,
          "settings mutation rejects malformed schema");
    response = Request(server.port(), PutSettings(settings_token,
                           changed_json, origin));
    Check(response.status == 200 && settings.Get().shortcut_key_code == 12 &&
              settings.Get().maximum_hold_ms == 30'000 &&
              response.body.find("\"schema\":2") != std::string::npos,
          "public schema-2 settings update preserves deliberate 30-second custom interval");
    Check(response.body.find("input_monitoring") == std::string::npos,
          "settings persistence response does not fabricate readiness");
    SettingsStore updated(settings_path);
    const auto updated_bytes = FileBytes(settings_path);
    const std::string updated_text(updated_bytes.begin(), updated_bytes.end());
    Check(updated.Get().shortcut_key_code == 12 &&
          updated.Get().maximum_hold_ms == 30'000 &&
          updated_text.find("\"schema\":4") != std::string::npos &&
          updated.ReferenceCapability(current_id) ==
              settings.ReferenceCapability(current_id),
          "API update saves schema 4 and reloads custom timeout without remigration or scope rotation");

    const std::string current_token = settings.ReferenceCapability(current_id);
    const std::string current_root = "/v1/references/" + current_id;
    const std::string multi_token = settings.ReferenceCapability(multi_id);
    const std::string multi_root = "/v1/references/" + multi_id;
    const std::string queued_multi_token =
        settings.ReferenceCapability(queued_multi_id);
    const std::string queued_multi_root =
        "/v1/references/" + queued_multi_id;
    const std::string unknown_id(32, 'c');
    const std::string unknown_view = "/v1/references/" + unknown_id + "/view";
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/view"));
    const auto unknown_response = Request(server.port(), ReferenceGet(
        server.port(), unknown_view));
    Check(response.status == 200 && response.body == unknown_response.body &&
              response.body.find(current_id) == std::string::npos &&
              response.body.find(current_token) == std::string::npos,
          "generic viewer shell reveals neither existence, identity nor capability");
    Check(response.body.find("<h2>Current local operation</h2>") !=
                  std::string::npos &&
              response.body.find("<h2>Historical capture metadata</h2>") !=
                  std::string::npos &&
              response.body.find("Readiness and Clipboard acknowledgements are current local operation facts") !=
                  std::string::npos &&
              response.body.find("<pre id=\"identity\"") != std::string::npos,
          "viewer separates current local operation from historical metadata text");
    Check(response.bytes.find(
              "Content-Security-Policy: default-src 'none'; script-src 'self'; "
              "connect-src 'self'; img-src blob:; style-src 'none'; "
              "object-src 'none'; base-uri 'none'; frame-ancestors 'none'; "
              "form-action 'none'\r\n") != std::string::npos &&
              response.bytes.find("Cache-Control: no-store\r\n") !=
                  std::string::npos &&
              response.bytes.find("Referrer-Policy: no-referrer\r\n") !=
                  std::string::npos,
          "viewer shell has the exact restrictive response policy");
    response = Request(server.port(), ReferenceGet(
        server.port(), "/v1/reference-view.js"));
    RunEmbeddedViewerCanvasHarness(response.body);
    Check(response.status == 200 &&
              response.body.find("history.replaceState") != std::string::npos &&
              response.body.find("Authorization: `Bearer") != std::string::npos &&
              response.body.find("crypto.subtle.digest") != std::string::npos &&
              response.body.find("URL.createObjectURL") != std::string::npos &&
              response.body.find("sessionStorage") != std::string::npos &&
              response.body.find("rememberCapability") != std::string::npos &&
              response.body.find("forgetCapability") != std::string::npos &&
              response.body.find("const initialHash = location.hash") !=
                  std::string::npos &&
              response.body.find(
                  "const invalidFragment = initialHash && !fragment") !=
                  std::string::npos &&
              response.body.find(
                  "if (path && invalidFragment) forgetCapability()") !=
                  std::string::npos &&
              response.body.find("clearCredentialOnFailure") != std::string::npos &&
              response.body.find("readBounded(response, maximum)") !=
                  std::string::npos &&
              response.body.find("local fetch exceeded 2 seconds") !=
                  std::string::npos &&
              response.body.find("performance.now()") != std::string::npos &&
              response.body.find("Indexing local reference") !=
                  std::string::npos &&
              response.body.find("Waiting for reference metadata") !=
                  std::string::npos &&
              response.body.find("contextImageReadyMs") != std::string::npos &&
              response.body.find("cropReadyMs") != std::string::npos &&
              response.body.find("toBlob") != std::string::npos &&
              response.body.find("verifyCropPixels") != std::string::npos &&
              response.body.find("if (!legacyDigest) await verifyCropPixels") !=
                  std::string::npos &&
              response.body.find("cropDigest !== legacyDigest") !=
                  std::string::npos &&
              response.body.find("crop pixel integrity mismatch") !=
                  std::string::npos &&
              response.body.find("crop-save") != std::string::npos &&
              response.body.find("annotated.png") != std::string::npos &&
              response.body.find("const verifiedPng = new Blob") != std::string::npos &&
              response.body.find("renderHistoricalMetadata") != std::string::npos &&
              response.body.find("timestampText") != std::string::npos &&
              response.body.find("Historical capture facts (captured metadata only)") !=
                  std::string::npos &&
              response.body.find("Application name:") != std::string::npos &&
              response.body.find("Window title:") != std::string::npos &&
              response.body.find("Bundle identifier:") != std::string::npos &&
              response.body.find("Capture requested (UTC microseconds):") !=
                  std::string::npos &&
              response.body.find("Circle completed (UTC microseconds):") !=
                  std::string::npos &&
              response.body.find("Pixel dimensions:") != std::string::npos &&
              response.body.find("Logical bounds (shared global AppKit logical points, Y-up)") !=
                  std::string::npos &&
              response.body.find("Region ${regionIndex + 1} (independent):") !=
                  std::string::npos &&
              response.body.find("display-local logical point") !=
                  std::string::npos &&
              response.body.find("shared global AppKit logical points are Y-up") !=
                  std::string::npos &&
              response.body.find("historical metadata unavailable on this Mac") !=
                  std::string::npos &&
              response.body.find("reference has expired on this Mac") !=
                  std::string::npos &&
              response.body.find("pending.retry_ms") != std::string::npos &&
              response.body.find("Math.max(20") != std::string::npos &&
              response.body.find("Math.min(250") != std::string::npos &&
              response.body.find("pending.state !== \"deriving\"") !=
                  std::string::npos &&
              response.body.find("attempt < 40") != std::string::npos &&
              response.body.find("reference was deleted from this Mac") !=
                  std::string::npos &&
              response.body.find("attempt < 20") != std::string::npos &&
              response.body.find(current_token) == std::string::npos,
          "fixed viewer script scrubs, authenticates, verifies and displays locally");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view", {}, std::nullopt,
              "127.0.0.1")).status == 400,
          "viewer rejects a Host without the exact bound port");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view", {},
              "http://evil.invalid")).status == 403,
          "viewer rejects an explicit foreign Origin");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view", {},
              server.base_url())).status == 200,
          "viewer permits the exact loopback Origin");
    Check(Request(server.port(), "POST " + current_root +
              "/view HTTP/1.1\r\nHost: 127.0.0.1:" +
              std::to_string(server.port()) + "\r\n\r\n").status == 405,
          "viewer rejects unsupported methods");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view?cap=x")).status == 400,
          "viewer rejects query parameters");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view#cap=x")).status == 400,
          "server rejects a fragment-like HTTP request target");

    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root)).status == 401,
          "reference read requires bearer");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, "wrong")).status == 403,
          "reference read rejects wrong capability");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, current_token, std::nullopt,
              "localhost:" + std::to_string(server.port()))).status == 400,
          "reference data rejects a noncanonical loopback Host");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, current_token,
              "http://evil.invalid")).status == 403,
          "reference data rejects an explicit foreign Origin");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, settings.ReferenceCapability(old_id)))
              .status == 403,
          "reference read rejects a capability scoped to another ID");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + unknown_id,
              settings.ReferenceCapability(unknown_id))).status == 404,
          "authorized unknown reference is distinct");
    const std::string pending_id(32, 'd');
    Reference pending;
    pending.id = pending_id;
    pending.mark_id = pending_id;
    pending.context = ContextFixture(Now());
    pending.path = {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                    {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}};
    pending.circle_completed = Now();
    Check(references.AcceptPending(pending, 10, 20).state ==
              ReferenceJobState::kPending,
          "service fixture accepts a pending identity");
    const auto pending_url = server.ClipboardText(pending_id);
    const std::string pending_path = "/r/" + pending_id +
        settings.ReferenceCapability(pending_id);
    Check(pending_url.find_first_of(" \t\r\n") == std::string::npos &&
              pending_url == server.base_url() + pending_path,
          "pending publication is one direct JSON capability URL");
    response = Request(server.port(), ReferenceGet(server.port(), pending_path));
    Check(response.status == 202 &&
              response.body.find("\"state\":\"pending\"") != std::string::npos &&
              response.body.find("\"retry_ms\":25") != std::string::npos,
          "pending JSON capability remains directly retryable");
    const auto pending_start = std::chrono::steady_clock::now();
    response = Request(server.port(), ReferenceGet(
        server.port(), "/v1/references/" + pending_id,
        settings.ReferenceCapability(pending_id)));
    const auto pending_elapsed = std::chrono::steady_clock::now() - pending_start;
    Check(response.status == 202 &&
              response.body.find("\"state\":\"pending\"") != std::string::npos &&
              response.body.find("\"retry_ms\":25") != std::string::npos,
          "same scoped ID reports machine-readable pending state");
    Check(pending_elapsed < std::chrono::milliseconds(500),
          "pending status stays on the lightweight index path");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + pending_id + "/crop.png",
              settings.ReferenceCapability(pending_id))).status == 202,
          "pending asset keeps the same retryable identity");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + pending_id + "/annotated.png",
              settings.ReferenceCapability(pending_id))).status == 202,
          "pending annotated asset keeps the same retryable identity");
    Check(Request(server.port(), ReferenceGet(
              server.port(), pending_path + "/crop.png")).status == 202 &&
              Request(server.port(), ReferenceGet(
              server.port(), pending_path + "/annotated.png")).status == 202,
          "pending assets share the JSON path capability");
    references.Fail(pending_id, "capture_failed");
    references.WaitForIdleForTesting();
    response = Request(server.port(), ReferenceGet(
        server.port(), "/v1/references/" + pending_id,
        settings.ReferenceCapability(pending_id)));
    Check(response.status == 422 &&
              response.body.find("\"state\":\"failed\"") != std::string::npos &&
              response.body.find("capture_failed") != std::string::npos,
          "terminal failure remains explicit at the same scoped ID");
    Check(server.ClipboardText(pending_id) == pending_url,
          "failed state retains the exact pending URL identity");
    Check(Request(server.port(), ReferenceGet(server.port(), pending_path)).status == 422,
          "failed JSON capability reports the same terminal lifecycle");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/not-an-id", "x")).status == 400,
          "invalid reference identity is rejected");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/../settings", "x")).status == 400,
          "traversal target is rejected");

    const auto cold_metadata_start = std::chrono::steady_clock::now();
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root, current_token));
    const auto cold_metadata_elapsed = std::chrono::steady_clock::now() -
        cold_metadata_start;
    Check(response.status == 200 &&
              response.body.starts_with("{\"schema\":1,") &&
              response.body.find("Immutable fixture") != std::string::npos,
          "legacy metadata returns its persisted schema and immutable context");
    Check(cold_metadata_elapsed < std::chrono::milliseconds(500),
          "cold metadata stays inside the viewer request bound");
    std::cout << "cold_metadata_initial_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     cold_metadata_elapsed).count() << '\n';
    Check(response.body.find(current.value.source.sha256) != std::string::npos &&
          response.body.find("crop_pixels") != std::string::npos,
          "metadata returns source and crop identity");
    const auto multi_response = Request(server.port(), ReferenceGet(
        server.port(), multi_root, multi_token));
    Check(multi_response.status == 200 &&
              multi_response.body.starts_with("{\"schema\":2,") &&
              multi_response.body.find("\"record_url\":null") !=
                  std::string::npos &&
              multi_response.body.find("\"sha256\":null,\"encoding\":"
                  "\"png-zlib-v1\",\"pixel_integrity\":{\"contract\":"
                  "\"rgba8-sha256-v1\"") != std::string::npos &&
              multi_response.body.find(multi.value.crop.pixel_sha256) !=
                  std::string::npos &&
              multi_response.body.find("display-b") != std::string::npos &&
              multi_response.body.find("\"regions\":[{\"region\":") !=
                  std::string::npos &&
              multi_response.body.find("},{\"region\":") !=
                  std::string::npos,
          "multi-region metadata returns schema 2 with independent paths and bounds");
    const std::string compact_multi_root="/r/"+multi_id+multi_token;
    const auto persisted_note=references.UpdateMarkNote(
        multi_id,StableMarkId(multi_id,0),"Inspect first\nmark",0);
    Check(persisted_note.status==MarkNoteUpdateStatus::kUpdated&&
              persisted_note.revision==1,
          "native note save precedes read-only compact projection");
    const auto compact_multi=Request(server.port(),ReferenceGet(
        server.port(),compact_multi_root+"/metadata"));
    const auto first_mark=compact_multi.body.find("\"mark_id\":\"");
    const auto second_mark=first_mark==std::string::npos?std::string::npos:
        compact_multi.body.find("\"mark_id\":\"",first_mark+1);
    Check(compact_multi.status==200&&first_mark!=std::string::npos&&
          second_mark!=std::string::npos&&
          compact_multi.body.find("\"notes_revision\":1")!=std::string::npos&&
          compact_multi.body.find("\"note\":\"Inspect first\\nmark\"")!=
              std::string::npos&&
          compact_multi.body.find("\"note\":\"\"")!=std::string::npos&&
          compact_multi.body.find("\"number\":1")!=std::string::npos&&
          compact_multi.body.find("\"number\":2")!=std::string::npos&&
          compact_multi.body.find("\"path\"")==std::string::npos&&
          compact_multi.body.find("\"subpaths\"")==std::string::npos&&
          compact_multi.body.find("\"pos\":{\"x\":")!=std::string::npos&&
          compact_multi.body.find(multi.value.source.sha256)!=std::string::npos,
          "two outer gestures have stable ordered compact assets without samples");
    const auto first_mark_id=compact_multi.body.substr(first_mark+11,32);
    const auto second_mark_id=compact_multi.body.substr(second_mark+11,32);
    Check(first_mark_id!=second_mark_id&&
          Request(server.port(),ReferenceGet(server.port(),
              "/r/"+multi_id+current_token+"/marks/"+first_mark_id+
              ".png")).status==403&&
          Request(server.port(),ReferenceGet(server.port(),
              compact_multi_root+"/marks/"+std::string(32,'0')+".png"))
              .status==404,
          "mark assets retain exact reference capability and identity");
    const auto numbered=references.MarkSnapshots(multi_id);
    Check(numbered.status==MarkNotesStatus::kReady&&numbered.marks.size()==2&&
              numbered.marks[0].note=="Inspect first\nmark"&&
              numbered.marks[1].note.empty(),
          "HTTP notes project the shared ordered store snapshot");
    Response second_image;
    for(std::size_t slot=0;slot<2;++slot) {
      const auto& mark_id=slot==0?first_mark_id:second_mark_id;
      auto image=Request(server.port(),ReferenceGet(server.port(),
          compact_multi_root+"/marks/"+mark_id+".png"));
      for(int attempt=0;image.status==202&&attempt<4;++attempt) {
        references.WaitForIdleForTesting();
        image=Request(server.port(),ReferenceGet(server.port(),
            compact_multi_root+"/marks/"+mark_id+".png"));
      }
      Check(image.status==200&&Header(image,"Content-Type")=="image/png"&&
            Header(image,"X-SeeThis-PNG-SHA256")==
                Sha256(Bytes(image.body.begin(),image.body.end())),
            "each ordered mark has its own ready annotated crop and byte hash");
      if(slot==1)second_image=image;
      const auto be32=[&](std::size_t offset) {
        return (std::uint32_t(std::uint8_t(image.body[offset]))<<24)|
            (std::uint32_t(std::uint8_t(image.body[offset+1]))<<16)|
            (std::uint32_t(std::uint8_t(image.body[offset+2]))<<8)|
            std::uint32_t(std::uint8_t(image.body[offset+3]));
      };
      const auto bounds=numbered.marks[slot].delivery_bounds;
      const auto section=compact_multi.body.find("\"mark_id\":\""+mark_id+"\"");
      Check(image.body.size()>24&&section!=std::string::npos&&
                be32(16)==bounds.width&&be32(20)==bounds.height&&
                compact_multi.body.find("\"width\":"+
                    std::to_string(std::uint32_t(bounds.width))+
                    ",\"height\":"+
                    std::to_string(std::uint32_t(bounds.height))+
                    ",\"bounds\":",section)!=std::string::npos,
            "compact JSON dimensions match delivered numbered PNG bounds");
    }
    const auto settled_compact_multi=Request(server.port(),ReferenceGet(
        server.port(),compact_multi_root+"/metadata"));
    Check(settled_compact_multi.status==200&&
          settled_compact_multi.body.find("\"state\":\"pending\"")==
              std::string::npos&&
          settled_compact_multi.body.find("\"sha256\":null")==
              std::string::npos,
          "metadata advances every crop to ready without fabricating pending hashes");
    const auto hidden_update=references.UpdateMarkVisibility(
        multi_id,second_mark_id,false,1);
    const auto hidden_compact=Request(server.port(),ReferenceGet(
        server.port(),compact_multi_root+"/metadata"));
    const auto hidden_first=hidden_compact.body.find(
        "\"mark_id\":\""+first_mark_id+"\"");
    const auto hidden_second=hidden_compact.body.find(
        "\"mark_id\":\""+second_mark_id+"\"");
    const auto hidden_image=Request(server.port(),ReferenceGet(server.port(),
        compact_multi_root+"/marks/"+second_mark_id+".png"));
    Check(hidden_update.status==MarkNoteUpdateStatus::kUpdated&&
          hidden_update.revision==2&&hidden_compact.status==200&&
          hidden_first!=std::string::npos&&hidden_second!=std::string::npos&&
          hidden_first<hidden_second&&
          hidden_compact.body.find("\"visible\":true",hidden_first)<hidden_second&&
          hidden_compact.body.find("\"visible\":true",hidden_second)!=
              std::string::npos&&
          hidden_compact.body.find("\"global_visibility\":\"visible\"")!=std::string::npos&&
          hidden_compact.body.find("\"notes_revision\":2")!=
              std::string::npos&&
          hidden_compact.body.find("\"note\":\"Inspect first\\nmark\"")!=
              std::string::npos&&
          hidden_compact.body.find("\"number\":2",hidden_second)!=
              std::string::npos&&
          hidden_compact.body.find("\"path\"")==std::string::npos&&
          hidden_image.status==200&&hidden_image.body==second_image.body&&
          Header(hidden_image,"X-SeeThis-PNG-SHA256")==
              Header(second_image,"X-SeeThis-PNG-SHA256"),
          "dormant hidden bit does not override global Visible or alter numbered image");
    Check(references.UpdateMarkVisibility(multi_id,second_mark_id,true,2)
              .status==MarkNoteUpdateStatus::kUpdated&&
          Request(server.port(),ReferenceGet(server.port(),compact_multi_root+"/metadata"))
              .body.find("\"visible\":false")==std::string::npos,
          "Show restores delivery visibility without changing mark membership");
    const auto notes_path=reference_root/(multi_id+".notes");
    const auto valid_notes=FileBytes(notes_path);
    {std::ofstream out(notes_path,std::ios::binary|std::ios::trunc);out<<"bad";}
    const auto corrupt_notes=Request(server.port(),ReferenceGet(
        server.port(),compact_multi_root+"/metadata"));
    Check(corrupt_notes.status==422&&
              corrupt_notes.body.find("notes_corrupt")!=std::string::npos&&
              FileBytes(notes_path)==Bytes({'b','a','d'})&&
              Request(server.port(),ReferenceGet(server.port(),compact_multi_root)).status==422,
          "compact read reports corrupt note metadata without overwriting it");
    auto future_notes=valid_notes;
    future_notes[std::string("seethis-mark-notes\n").size()]=3;
    {std::ofstream out(notes_path,std::ios::binary|std::ios::trunc);
      out.write(reinterpret_cast<const char*>(future_notes.data()),
                future_notes.size());}
    const auto future_read=Request(server.port(),ReferenceGet(
        server.port(),compact_multi_root+"/metadata"));
    Check(future_read.status==409&&
              future_read.body.find("notes_unsupported")!=std::string::npos&&
              FileBytes(notes_path)==future_notes&&
              Request(server.port(),ReferenceGet(server.port(),compact_multi_root)).status==409,
          "compact read reports unsupported note version without overwriting it");
    {std::ofstream out(notes_path,std::ios::binary|std::ios::trunc);
      out.write(reinterpret_cast<const char*>(valid_notes.data()),
                valid_notes.size());}
    std::cout<<"compact_metadata_bytes="<<compact_multi.body.size()
             <<",detailed_metadata_bytes="<<multi_response.body.size()<<'\n';
    const std::string sparse_id(32,'1'),dense_id(32,'2');
    const auto sparse=MakeReference(references,sparse_id,0);
    const auto dense=MakeReference(references,dense_id,0,false,false,175);
    const auto sparse_root="/r/"+sparse_id+settings.ReferenceCapability(sparse_id);
    const auto dense_root="/r/"+dense_id+settings.ReferenceCapability(dense_id);
    const auto sparse_compact=Request(server.port(),ReferenceGet(
        server.port(),sparse_root+"/metadata"));
    const auto dense_compact=Request(server.port(),ReferenceGet(
        server.port(),dense_root+"/metadata"));
    const auto sparse_detail=Request(server.port(),ReferenceGet(
        server.port(),sparse_root+"/detail"));
    const auto dense_detail=Request(server.port(),ReferenceGet(
        server.port(),dense_root+"/detail"));
    Check(sparse_compact.status==200&&dense_compact.status==200&&
          sparse_detail.status==200&&dense_detail.status==200&&
          sparse_compact.body.size()==dense_compact.body.size()&&
          dense_detail.body.size()>sparse_detail.body.size()+5000&&
          dense_compact.body.find("\"path\"")==std::string::npos,
          "175 extra samples leave default metadata bytes fixed while detail grows");
    std::cout<<"sample_growth_metadata_bytes,compact_2="
             <<sparse_compact.body.size()<<",compact_177="
             <<dense_compact.body.size()<<",detail_2="
             <<sparse_detail.body.size()<<",detail_177="
             <<dense_detail.body.size()<<'\n';
    Check(references.Delete(sparse_id)==DeleteResult::kDeleted&&
          references.Delete(dense_id)==DeleteResult::kDeleted,
          "size fixtures clean their exact reference identities");
    Check(multi_response.body.find("\"annotated\":{\"width\":100,\"height\":100,")!=
              std::string::npos&&
              multi_response.body.find(multi_root+"/annotated.png")!=
                  std::string::npos&&
              multi_response.body.find("\"state\":\"pending\",\"sha256\":null")!=
                  std::string::npos,
          "metadata starts bounded annotated derivation with URL and truthful pending digest");
    references.WaitForIdleForTesting();
    const auto annotated_start=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/annotated.png",multi_token));
    Check(annotated_start.status==200||annotated_start.status==202,
          "metadata-queued annotated identity may already be ready or remain truthfully pending");
    references.WaitForIdleForTesting();
    const auto annotated_response=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/annotated.png",multi_token));
    const auto annotated_bytes=Bytes(annotated_response.body.begin(),
                                     annotated_response.body.end());
    const auto annotated_hash=Sha256(annotated_bytes);
    const auto annotated_metadata=Request(server.port(),ReferenceGet(
        server.port(),multi_root,multi_token));
    const auto annotated_repeat=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/annotated.png",multi_token));
    Check(annotated_response.status==200&&annotated_repeat.status==200&&
              Header(annotated_response,"Content-Type")=="image/png"&&
              Header(annotated_response,"ETag")=="\"sha256-"+annotated_hash+"\""&&
              Header(annotated_response,"X-SeeThis-PNG-SHA256")==annotated_hash&&
              annotated_repeat.body==annotated_response.body&&
              annotated_metadata.body.find("\"state\":\"ready\",\"sha256\":\""+
                                           annotated_hash+"\"")!=std::string::npos&&
              FileBytes(reference_root/(multi_id+".annotated.png"))==
                  annotated_bytes,
          "annotated endpoint serves stable authenticated PNG bytes and metadata digest");
    if(annotated_start.status==200) {
      Check(annotated_start.body==annotated_response.body &&
            Header(annotated_start,"Content-Type")==Header(annotated_response,"Content-Type") &&
            Header(annotated_start,"X-SeeThis-PNG-SHA256")==Header(annotated_response,"X-SeeThis-PNG-SHA256") &&
            Header(annotated_start,"ETag")==Header(annotated_response,"ETag"),
          "early ready response is the exact later same-reference numbered PNG, never raw or underlay");
      CheckPngResponse(annotated_start,annotated_hash,{multi.value.source.width,multi.value.source.height});
    }
    Check(Request(server.port(),ReferenceGet(
              server.port(),multi_root+"/annotated.png",current_token)).status==403,
          "annotated image rejects a capability scoped to another reference");
    const auto primary_source=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/source.png",multi_token));
    const auto primary_bytes=Bytes(primary_source.body.begin(),primary_source.body.end());
    Check(primary_source.status==200&&primary_bytes==multi.value.source.bytes&&
              Header(primary_source,"Content-Type")=="image/png"&&
              primary_bytes.size()>=8&&primary_bytes[0]==137&&
              primary_bytes[1]==80&&primary_bytes[2]==78&&primary_bytes[3]==71&&
              Header(primary_source,"X-SeeThis-PNG-SHA256")==Sha256(primary_bytes)&&
              Header(primary_source,"ETag")=="\"sha256-"+Sha256(primary_bytes)+"\"",
          "schema-2 primary viewer asset is the persisted full-context PNG");
    const auto regions_start = multi_response.body.find("\"regions\":[");
    const auto first_region = multi_response.body.find(
        "\"crop_pixels\":{\"x\":", regions_start);
    const auto second_region = multi_response.body.find(
        "\"crop_pixels\":{\"x\":", first_region + 1);
    Check(regions_start != std::string::npos &&
              first_region != std::string::npos && second_region != std::string::npos &&
              first_region != second_region &&
              multi_response.body.find("\"subpaths\":[[{\"display_id\":1") !=
                  std::string::npos,
          "schema-2 metadata exposes executable per-region source geometry and paths");
    Check(Request(server.port(), ReferenceGet(
              server.port(), multi_root + "/record", multi_token)).status == 422,
          "schema-2 URL-only references do not materialize a legacy record export");
    std::mutex derive_mutex;
    std::condition_variable derive_ready;
    bool derive_entered=false,allow_derive=false,derive_identity_matches=true;
    unsigned derive_starts=0;
    ReferenceStoreTestHooks derive_hooks;
    derive_hooks.before_derive_crop=[&](std::string_view id) {
      std::unique_lock lock(derive_mutex);
      derive_identity_matches=derive_identity_matches&&
          (id==multi_id||id==queued_multi_id);
      ++derive_starts;derive_entered=true;derive_ready.notify_all();
      derive_ready.wait(lock,[&]{return allow_derive;});
    };
    references.SetTestHooks(std::move(derive_hooks));
    Check(derive_starts==0,
          "metadata and full-context source readiness do not derive an unused crop");
    const auto first_crop_start=std::chrono::steady_clock::now();
    const auto first_crop=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/crop.png",multi_token));
    const auto first_crop_us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-first_crop_start).count();
    {
      std::unique_lock lock(derive_mutex);
      Check(derive_ready.wait_for(lock,std::chrono::seconds(2),
                                  [&]{return derive_entered;}),
            "cold crop enters the bounded background executor");
    }
    const auto held_start=std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    const auto held_us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-held_start).count();
    const auto metadata_while_deriving_start=std::chrono::steady_clock::now();
    const auto metadata_while_deriving=Request(server.port(),ReferenceGet(
        server.port(),multi_root,multi_token));
    const auto metadata_while_deriving_us=
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now()-metadata_while_deriving_start).count();
    const auto repeated_crop=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/crop.png",multi_token));
    const auto queued_crop=Request(server.port(),ReferenceGet(
        server.port(),queued_multi_root+"/crop.png",queued_multi_token));
    {
      std::lock_guard lock(derive_mutex);
      Check(first_crop.status==202&&first_crop.body.find("\"state\":\"deriving\"")!=
                std::string::npos&&first_crop_us<500'000&&
            metadata_while_deriving.status==200&&
                metadata_while_deriving_us<500'000&&
            repeated_crop.status==202&&derive_starts==1&&
                queued_crop.status==202&&
                queued_crop.body.find("\"state\":\"queued\"")!=
                    std::string::npos&&
                held_us>=2'000'000&&
                derive_identity_matches,
            "cold crop stays truthful beyond the old two-second request bound");
      allow_derive=true;
    }
    derive_ready.notify_all();references.WaitForIdleForTesting();
    const auto completed_crop=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/crop.png",multi_token));
    const auto completed_bytes=Bytes(completed_crop.body.begin(),completed_crop.body.end());
    const auto completed_hash=Sha256(completed_bytes);
    Check(completed_crop.status==200&&!completed_bytes.empty()&&
              Header(completed_crop,"X-SeeThis-PNG-SHA256")==completed_hash&&
              Header(completed_crop,"ETag")=="\"sha256-"+completed_hash+"\""&&
              completed_hash!=multi.value.crop.pixel_sha256,
          "crop polling returns bytes matching their own SHA/ETag, distinct from pixel integrity");
    const auto queued_derivation=Request(server.port(),ReferenceGet(
        server.port(),queued_multi_root+"/crop.png",queued_multi_token));
    references.WaitForIdleForTesting();
    const auto queued_completed=Request(server.port(),ReferenceGet(
        server.port(),queued_multi_root+"/crop.png",queued_multi_token));
    unsigned completed_derivations=0;
    bool completed_identities_match=false;
    {std::lock_guard lock(derive_mutex);
      completed_derivations=derive_starts;
      completed_identities_match=derive_identity_matches;}
    const auto queued_bytes=Bytes(queued_completed.body.begin(),queued_completed.body.end());
    const auto queued_hash=Sha256(queued_bytes);
    Check(queued_derivation.status==202&&queued_completed.status==200&&
              !queued_bytes.empty()&&
              Header(queued_completed,"X-SeeThis-PNG-SHA256")==queued_hash&&
              Header(queued_completed,"ETag")=="\"sha256-"+queued_hash+"\""&&
              queued_hash!=queued_multi.value.crop.pixel_sha256&&completed_derivations==2&&
                  completed_identities_match,
          "queued identity advances through one bounded derivation slot to exact PNG");
    std::cout<<"cold_schema2_crop_protocol,statuses="<<first_crop.status<<','
             <<repeated_crop.status<<','<<queued_crop.status<<','
             <<completed_crop.status<<','<<queued_derivation.status<<','
             <<queued_completed.status
             <<",first_response_us="<<first_crop_us
             <<",metadata_while_deriving_us="<<metadata_while_deriving_us
             <<",blocked_derivation_us="<<held_us
             <<",derive_starts="<<completed_derivations<<'\n';
    references.SetTestHooks({});
    Check(references.Delete(multi_id)==DeleteResult::kDeleted&&
              Request(server.port(),ReferenceGet(
                server.port(),multi_root+"/crop.png",multi_token)).status==410&&
              Request(server.port(),ReferenceGet(
                server.port(),multi_root+"/annotated.png",multi_token)).status==410&&
              Request(server.port(),ReferenceGet(
                server.port(),compact_multi_root+"/marks/"+first_mark_id+
                    ".png")).status==410&&
              !std::filesystem::exists(reference_root/(multi_id+".annotated.png")),
          "schema-2 deletion invalidates every derived route and annotated cache");
    std::vector<std::pair<int,long long>> metadata_attempts;
    for (int attempt = 0; attempt < 220; ++attempt) {
      const auto start = std::chrono::steady_clock::now();
      const auto measured = Request(server.port(), ReferenceGet(
          server.port(), current_root, current_token));
      const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      if (attempt >= 20) metadata_attempts.push_back({measured.status,elapsed});
    }
    std::vector<long long> sorted_metadata;
    bool every_metadata_succeeded=true;
    for(std::size_t index=0;index<metadata_attempts.size();++index) {
      const auto [status,elapsed]=metadata_attempts[index];
      std::cout<<"cold_metadata_attempt,"<<index<<",status="<<status
               <<",elapsed_us="<<elapsed<<'\n';
      every_metadata_succeeded=every_metadata_succeeded&&status==200;
      sorted_metadata.push_back(elapsed);
    }
    std::sort(sorted_metadata.begin(), sorted_metadata.end());
    std::cout << "cold_metadata_p95_us=" << sorted_metadata[189] << '\n';
    Check(metadata_attempts.size()==200&&every_metadata_succeeded&&
              sorted_metadata[189] < 500'000,
          "200-attempt cold metadata p95 remains within viewer bounds");
    Check(!references.LookupMetadata(current_id)->ready,
          "metadata cohort never warms the full stored payload");
    const auto crop_start = std::chrono::steady_clock::now();
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/crop.png", current_token));
    const auto crop_elapsed = std::chrono::steady_clock::now() - crop_start;
    const auto cold_crop_bytes=Bytes(response.body.begin(),response.body.end());
    Check(response.status == 200 &&
              cold_crop_bytes==current.value.crop.bytes&&
              Header(response,"X-SeeThis-PNG-SHA256")==Sha256(cold_crop_bytes)&&
              Header(response,"ETag")=="\"sha256-"+Sha256(cold_crop_bytes)+"\"",
          "following cold crop returns exact immutable PNG bytes without record prewarm");
    Check(crop_elapsed < std::chrono::seconds(2),
          "following cold crop reaches verified bytes within viewer bounds");
    std::cout << "cold_crop_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     crop_elapsed).count() << '\n';
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/record", current_token));
    Check(response.status == 200 && Bytes(response.body.begin(), response.body.end()) == current.record,
          "record endpoint returns exact immutable envelope bytes");
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/source.png", current_token));
    const auto source_bytes=Bytes(response.body.begin(),response.body.end());
    Check(response.status == 200 && source_bytes==current.value.source.bytes&&
              Header(response,"X-SeeThis-PNG-SHA256")==Sha256(source_bytes)&&
              Header(response,"ETag")=="\"sha256-"+Sha256(source_bytes)+"\"",
          "source endpoint returns exact immutable PNG bytes");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + old_id,
              settings.ReferenceCapability(old_id))).status == 410,
          "expired reference is distinct from unknown reference");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/r/" + old_id +
              settings.ReferenceCapability(old_id))).status == 410,
          "expired path capability reports the same gone lifecycle");

    const std::string deleted_root="/v1/references/"+deleted_id;
    const std::string deleted_token=settings.ReferenceCapability(deleted_id);
    const std::string deleted_agent_root="/r/"+deleted_id+deleted_token;
    Check(references.Delete(deleted_id)==DeleteResult::kDeleted,
          "service deletion fixture invalidates the ready reference cache");
    for(const auto& suffix:{"","/record","/source.png","/crop.png"}) {
      response=Request(server.port(),ReferenceGet(
          server.port(),deleted_root+suffix,deleted_token));
      Check(response.status==410&&
                response.body.find("\"state\":\"deleted\"")!=std::string::npos&&
                response.body.find("deleted_reference")!=std::string::npos,
            "old authenticated route reports truthful deleted state");
    }
    for(const auto& suffix:{"","/record","/source.png","/crop.png",
                            "/annotated.png"}) {
      response=Request(server.port(),ReferenceGet(
          server.port(),deleted_agent_root+suffix));
      Check(response.status==410&&
                response.body.find("\"state\":\"deleted\"")!=std::string::npos,
            "deletion invalidates JSON capability and every agent asset");
    }
    Check(Request(server.port(),ReferenceGet(
              server.port(),deleted_root,"wrong")).status==403,
          "deletion does not weaken scoped read authentication");
    Check(Request(server.port(),ReferenceGet(
              server.port(),deleted_root+"/view")).status==200,
          "old compact URL retains the generic shell that resolves authenticated gone state");
    Check(Request(server.port(),"DELETE "+deleted_root+
              " HTTP/1.1\r\nHost: 127.0.0.1:"+
              std::to_string(server.port())+"\r\nAuthorization: Bearer "+
              deleted_token+"\r\n\r\n").status==405,
          "existing read capability gains no delete authority");

    std::string post = "POST /v1/references/" + current_id +
                       " HTTP/1.1\r\nHost: 127.0.0.1:" +
                       std::to_string(server.port()) + "\r\n\r\n";
    Check(Request(server.port(), post).status == 405,
          "reference mutation method is rejected");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n").status == 400,
          "duplicate headers are rejected");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n").status == 400,
          "transfer encoding is rejected");
    Check(Request(server.port(), "PUT /v1/settings HTTP/1.1\r\nHost: a\r\nContent-Length: 16385\r\n\r\n").status == 413,
          "oversized request body is rejected before reading it");
    std::string high_bit_length =
        "GET /v1/settings HTTP/1.1\r\nHost: a\r\nContent-Length: ";
    high_bit_length.push_back(static_cast<char>(0x80));
    high_bit_length += "\r\n\r\n";
    Check(Request(server.port(), high_bit_length).status == 400,
          "high-bit Content-Length byte is rejected safely");
    Check(Request(server.port(), Get("/v1/settings", settings_token)).status == 200,
          "server remains available after high-bit Content-Length rejection");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\r\nHost: a\r\n\r\nx").status == 400,
          "undeclared request body is rejected");
    Check(Request(server.port(), "BROKEN\r\n\r\n").status == 400,
          "malformed request line is rejected");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\nHost: a\r\n\r\n").status == 400,
          "bare line feed is rejected");
    Check(Request(server.port(), Get("/unknown")).status == 404,
          "unknown endpoint is explicit");

    const auto clipboard = server.ClipboardText(current);
    const std::string expected_clipboard = server.base_url() +
        "/r/" + current_id + current_token;
    Check(clipboard == expected_clipboard &&
              clipboard.find_first_of(" \t\r\n") == std::string::npos &&
              clipboard.size() <
                  seethis::service::ReferenceServer::kMaximumClipboardTextBytes,
          "clipboard publication is exactly one authenticated bounded URL");
    const std::string maximum_port_clipboard =
        "http://127.0.0.1:65535/r/" + current_id + current_token;
    Check(maximum_port_clipboard.size() <
                  seethis::service::ReferenceServer::kMaximumClipboardTextBytes,
          "maximum-port reference URL remains below two KiB");
    Check(cold_record_size >= 27ULL * 1024 * 1024,
          "URL-only copy fixture has a former record of at least 27 MiB");
    Check(clipboard.size() * 1000 < cold_record_size &&
              clipboard.find("SeeThis reference") == std::string::npos &&
              clipboard.find("hex-stref1") == std::string::npos &&
              (current.value.source.sha256.empty() ||
               clipboard.find(current.value.source.sha256) == std::string::npos),
          "former record, title, and source cannot inflate pasted URL text");
    Check(current.clipboard_png == current.value.crop.bytes,
          "legacy durable crop bytes remain available outside URL-only publication");
    Check(multi.clipboard_png.empty() && multi.clipboard_text.size() < 256,
          "schema-2 URL path stores no legacy PNG or embedded record payload");
    Check(clipboard == server.ClipboardText(current_id),
          "historical re-copy preserves stable authenticated URL identity");

    const auto parsed = ParseCompactHandoff(server.ViewerUrl(current_id),
                                           server.base_url());
    Check(parsed.target == current_root + "/view" &&
              parsed.capability == current_token &&
              parsed.target.find('#') == std::string::npos,
          "viewer compatibility URL retains fragment capability");
    const std::string parsed_root = parsed.target.substr(
        0, parsed.target.size() - std::string_view("/view").size());
    response = Request(server.port(), ReferenceGet(
        server.port(), parsed_root, parsed.capability));
    Check(response.status == 200 &&
              response.body.find("\"reference_id\":\"" + current_id + "\"") !=
                  std::string::npos &&
              response.body.find(current.value.crop.sha256) != std::string::npos,
          "local tool uses parsed bearer for authenticated metadata");
    response = Request(server.port(), ReferenceGet(
        server.port(), parsed_root + "/crop.png", parsed.capability));
    const auto parsed_crop=Bytes(response.body.begin(),response.body.end());
    Check(response.status == 200 && parsed_crop==current.value.crop.bytes&&
              Header(response,"X-SeeThis-PNG-SHA256")==Sha256(parsed_crop)&&
              Header(response,"ETag")=="\"sha256-"+Sha256(parsed_crop)+"\"",
          "legacy Bearer path still retrieves the exact authenticated PNG");

    const std::string agent_root = "/r/" + current_id + current_token;
    response = Request(server.port(), ReferenceGet(server.port(), agent_root+"/metadata"));
    const auto mark_start=response.body.find("\"mark_id\":\"");
    const std::string agent_mark_id=mark_start==std::string::npos?"":
        response.body.substr(mark_start+11,32);
    Check(response.status == 200 &&
              Header(response,"Cache-Control")=="no-store" &&
              Header(response,"Referrer-Policy")=="no-referrer" &&
              response.body.find("\"delivery_schema\":2")!=std::string::npos &&
              response.body.find("\"reference_id\":\""+current_id+"\"")!=std::string::npos &&
              response.body.find("\"state\":\"ready\"")!=std::string::npos &&
              response.body.find("\"screenshot\":{")!=std::string::npos &&
              response.body.find(current.value.source.sha256)!=std::string::npos &&
              response.body.find("\"marks\":[{")!=std::string::npos &&
              agent_mark_id.size()==32 &&
              response.body.find("\"visible\":true")!=std::string::npos &&
              response.body.find("\"pos\":{")!=std::string::npos &&
              response.body.find("\"path\"")==std::string::npos &&
              response.body.find("\"subpaths\"")==std::string::npos &&
              response.body.find("\"rgba\"")==std::string::npos,
          "explicit legacy capability read returns compact ordered mark metadata");
    AwaitAsset(references,current_id,0);
    const auto legacy_default=Request(server.port(),ReferenceGet(server.port(),agent_root));
    Check(legacy_default.status==200&&legacy_default.body.find("\"delivery_schema\":3,\"reference_schema\":1")!=std::string::npos&&
          legacy_default.body.find("full-annotated-v1")!=std::string::npos&&
          legacy_default.body.find(current.value.source.sha256)!=std::string::npos,
          "persisted schema1 uses schema3 default without migrating historical data");
    auto mark_asset=Request(server.port(),ReferenceGet(server.port(),
        agent_root+"/marks/"+agent_mark_id+".png"));
    if(mark_asset.status==202) {
      references.WaitForIdleForTesting();
      mark_asset=Request(server.port(),ReferenceGet(server.port(),
          agent_root+"/marks/"+agent_mark_id+".png"));
    }
    Check(mark_asset.status==200&&
          Header(mark_asset,"X-SeeThis-PNG-SHA256")==
              Sha256(Bytes(mark_asset.body.begin(),mark_asset.body.end())),
          "compact mark URL returns the scoped annotated crop");
    const auto detailed=Request(server.port(),ReferenceGet(
        server.port(),agent_root+"/detail"));
    Check(detailed.status==200&&
          detailed.body.find("\"context\":{")!=std::string::npos&&
          detailed.body.find("\"regions\":[")!=std::string::npos,
          "explicit capability-scoped detail retains historical trajectories");
    for(const auto& suffix:{"/source.png","/crop.png","/annotated.png"}) {
      auto asset = Request(server.port(), ReferenceGet(
          server.port(), agent_root + suffix));
      if (asset.status == 202) {
        references.WaitForIdleForTesting();
        asset = Request(server.port(), ReferenceGet(
            server.port(), agent_root + suffix));
      }
      Check(asset.status == 200 && Header(asset,"X-SeeThis-PNG-SHA256")==
                Sha256(Bytes(asset.body.begin(),asset.body.end())),
            "same path capability fetches each exact PNG asset");
    }
    const std::string wrong_agent_root = "/r/" + current_id +
        settings.ReferenceCapability(old_id);
    Check(Request(server.port(), ReferenceGet(
              server.port(), wrong_agent_root)).status == 403 &&
              Request(server.port(), ReferenceGet(
              server.port(), "/r/"+current_id)).status == 400 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root.substr(0, agent_root.size()-1))).status == 400 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root+"/view")).status == 400 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root, {}, server.base_url()+"/other")).status == 403 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root, {}, std::nullopt,
              "localhost:"+std::to_string(server.port()))).status == 400,
          "path capability stays scoped and Host/Origin constrained");

    { std::ofstream damaged(reference_root / (current_id + ".source.png"),
                            std::ios::binary | std::ios::app);
      damaged << "hash-mismatch"; }
    Response damaged_asset;
    std::thread slow_asset([&] {
      damaged_asset = Request(server.port(), ReferenceGet(
          server.port(), current_root + "/source.png", current_token));
    });
    const auto concurrent_start = std::chrono::steady_clock::now();
    const auto concurrent_metadata = Request(server.port(), ReferenceGet(
        server.port(), current_root, current_token));
    const auto concurrent_elapsed = std::chrono::steady_clock::now() -
        concurrent_start;
    slow_asset.join();
    Check(damaged_asset.status == 422 &&
              damaged_asset.body.find("asset_unavailable") != std::string::npos,
          "hash-mismatched cold asset fails honestly");
    Check(concurrent_metadata.status == 200 &&
              concurrent_elapsed < std::chrono::milliseconds(500),
          "metadata stays responsive while another worker validates a large corrupt asset");
    Check(std::filesystem::remove(reference_root / (current_id + ".source.png")) &&
              Request(server.port(), ReferenceGet(
                  server.port(), current_root + "/source.png", current_token)).status == 422,
          "missing cold asset fails honestly without weakening the route");

    changed.maximum_visible_references = 1;
    changed.reference_ttl_seconds = 3600;
    Check(settings.Update(changed, &parse_error), "capacity settings update succeeds");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + old_id,
              settings.ReferenceCapability(old_id))).status == 410,
          "references outside visible storage limit expire explicitly");
    auto terminal = [&](char digit) {
      Reference reference;
      reference.id = std::string(32, digit);
      reference.context = ContextFixture(Now());
      reference.path = {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1}};
      reference.circle_completed = Now();
      auto accepted = references.AcceptPending(reference, 1, 2);
      Check(accepted.state == ReferenceJobState::kPending,
            "terminal retention service fixture accepts");
      references.Fail(reference.id, "cancelled");
      references.WaitForIdleForTesting();
      return reference.id;
    };
    const auto first_failed = terminal('e');
    const auto second_failed = terminal('f');
    const auto expired_failed = Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + first_failed,
              settings.ReferenceCapability(first_failed)));
    Check(expired_failed.status == 410 &&
              expired_failed.body.find("\"state\":\"expired\"") !=
                  std::string::npos,
          "failed terminal identity becomes an authenticated expired tombstone");
    const auto third_failed = terminal('0');
    (void)second_failed;
    (void)third_failed;
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + first_failed,
              settings.ReferenceCapability(first_failed))).status == 404,
          "oldest failed tombstone eventually becomes unknown");
    server.Stop();
    Check(server.port() == 0, "server stops with app-owned lifetime");
    std::cout << checks << " reference server/settings checks passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "FAILED after " << checks << " checks: " << exception.what() << '\n';
    return 1;
  }
}
