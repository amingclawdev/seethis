#include "core/reference.h"
#include "platform/platform.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <sys/resource.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>
namespace seethis::platform {
core::Pixels NormalizeSyntheticCaptureForTest(const core::Pixels& source);
std::pair<std::string,core::TitleEvidence> ObserveBoundTitleForTest(
    CFDictionaryRef first,CFArrayRef second,std::int64_t pid,std::uint64_t identifier);
std::optional<std::vector<std::uint64_t>> ParseNativeWindowInventoryForTest(
    CFArrayRef inventory,std::int64_t pid);
}
#endif

using namespace seethis::core;
namespace {
int checks=0;
void Check(bool condition,const char* message) {++checks;if(!condition)throw std::runtime_error(message);}
struct TempDirectory {
  std::filesystem::path path;
  TempDirectory(){char name[]="/tmp/seethis-reference-test-XXXXXX";auto p=mkdtemp(name);if(!p)throw std::runtime_error("mkdtemp");path=p;}
  ~TempDirectory(){std::filesystem::remove_all(path);}
};
Bytes FileBytes(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::uint32_t TestCrc(const Bytes& bytes,std::size_t begin,std::size_t end) {
  std::uint32_t value=0xffffffff;
  for(std::size_t index=begin;index<end;++index) {
    value^=bytes[index];
    for(int bit=0;bit<8;++bit)
      value=(value>>1)^(0xedb88320U & (0U-(value&1)));
  }
  return ~value;
}
void PutTestBe32(Bytes& bytes,std::size_t offset,std::uint32_t value) {
  for(int shift=3;shift>=0;--shift)
    bytes[offset+static_cast<std::size_t>(3-shift)]=
        static_cast<std::uint8_t>(value>>(shift*8));
}
void PutTestBe64(Bytes& bytes,std::size_t offset,std::uint64_t value) {
  for(int shift=7;shift>=0;--shift)
    bytes[offset+static_cast<std::size_t>(7-shift)]=
        static_cast<std::uint8_t>(value>>(shift*8));
}
void AppendTestBe32(Bytes& bytes,std::uint32_t value) {
  const auto offset=bytes.size();bytes.resize(offset+4);PutTestBe32(bytes,offset,value);
}
Bytes CorruptAdlerPng(const Bytes& source) {
  auto be32=[](const Bytes& bytes,std::size_t offset) {
    return (std::uint32_t(bytes[offset])<<24)|
        (std::uint32_t(bytes[offset+1])<<16)|
        (std::uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
  };
  Bytes result=source;
  for(std::size_t offset=8;offset+12<=result.size();) {
    const auto size=be32(result,offset);
    if(size>result.size()-offset-12)return {};
    if(std::string(result.begin()+offset+4,result.begin()+offset+8)=="IDAT") {
      if(size<4)return {};
      const auto adler=offset+8+size-4;
      result[adler+3]^=1;
      PutTestBe32(result,offset+8+size,
                  TestCrc(result,offset+4,offset+8+size));
      return result;
    }
    offset+=size+12;
  }
  return {};
}
Bytes InsertEmptyStoredBlock(const Bytes& source) {
  auto be32=[](const Bytes& bytes,std::size_t offset) {
    return (std::uint32_t(bytes[offset])<<24)|
        (std::uint32_t(bytes[offset+1])<<16)|
        (std::uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
  };
  for(std::size_t offset=8;offset+12<=source.size();) {
    const auto size=be32(source,offset);
    if(size>source.size()-offset-12)return {};
    if(std::string(source.begin()+offset+4,source.begin()+offset+8)=="IDAT") {
      if(size<6)return {};
      const auto stream=offset+8,stream_end=stream+size;
      Bytes alternative={source[stream],source[stream+1],0,0,0,0xff,0xff};
      alternative.insert(alternative.end(),source.begin()+stream+2,
                         source.begin()+stream_end);
      Bytes result(source.begin(),source.begin()+offset);
      AppendTestBe32(result,static_cast<std::uint32_t>(alternative.size()));
      const auto idat=result.size();
      result.insert(result.end(),{'I','D','A','T'});
      result.insert(result.end(),alternative.begin(),alternative.end());
      AppendTestBe32(result,TestCrc(result,idat,result.size()));
      result.insert(result.end(),source.begin()+stream_end+4,source.end());
      return result;
    }
    offset+=size+12;
  }
  return {};
}
struct TestIdat {std::size_t chunk=0,data=0,size=0,end=0;};
std::optional<TestIdat> FindSingleIdat(const Bytes& png) {
  auto be32=[](const Bytes& bytes,std::size_t offset) {
    return (std::uint32_t(bytes[offset])<<24)|
        (std::uint32_t(bytes[offset+1])<<16)|
        (std::uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
  };
  std::optional<TestIdat> found;
  for(std::size_t offset=8;offset+12<=png.size();) {
    const auto size=be32(png,offset);if(size>png.size()-offset-12)return {};
    if(std::string(png.begin()+offset+4,png.begin()+offset+8)=="IDAT") {
      if(found)return {};
      found=TestIdat{offset,offset+8,size,offset+8+size};
    }
    offset+=size+12;
  }
  return found;
}
std::optional<Pixels> DecodeTestPng(const Bytes& png) {
  if(png.size()<33)return {};
  auto be32=[&](std::size_t offset) {
    return (std::uint32_t(png[offset])<<24)|(std::uint32_t(png[offset+1])<<16)|
        (std::uint32_t(png[offset+2])<<8)|png[offset+3];
  };
  const auto width=be32(16),height=be32(20);
  if(!width||!height||width>4096||height>4096||
     png[24]!=8||png[25]!=6)return {};
  Bytes compressed;
  for(std::size_t offset=8;offset+12<=png.size();) {
    const auto size=be32(offset);
    if(size>png.size()-offset-12)return {};
    if(std::string(png.begin()+offset+4,png.begin()+offset+8)=="IDAT")
      compressed.insert(compressed.end(),png.begin()+offset+8,
                        png.begin()+offset+8+size);
    offset+=size+12;
  }
  const auto stride=std::size_t(width)*4;
  Bytes filtered((stride+1)*height);
  uLongf length=filtered.size();
  if(compressed.empty()||uncompress(filtered.data(),&length,compressed.data(),
                                   compressed.size())!=Z_OK||
     length!=filtered.size())return {};
  Pixels result{width,height,Bytes(stride*height)};
  for(std::size_t y=0;y<height;++y) {
    const auto filter=filtered[y*(stride+1)];
    if(filter>4)return {};
    for(std::size_t x=0;x<stride;++x) {
      const auto left=x>=4?result.rgba[y*stride+x-4]:0;
      const auto above=y?result.rgba[(y-1)*stride+x]:0;
      const auto upper_left=y&&x>=4?result.rgba[(y-1)*stride+x-4]:0;
      int predictor=0;
      if(filter==1)predictor=left;
      if(filter==2)predictor=above;
      if(filter==3)predictor=(int(left)+above)/2;
      if(filter==4) {
        const int p=int(left)+above-upper_left;
        const int a=std::abs(p-int(left)),b=std::abs(p-above),
                  c=std::abs(p-int(upper_left));
        predictor=a<=b&&a<=c?left:b<=c?above:upper_left;
      }
      result.rgba[y*stride+x]=std::uint8_t(
          filtered[y*(stride+1)+1+x]+predictor);
    }
  }
  return result;
}
std::array<std::uint8_t,4> PixelAt(const Pixels& pixels,std::size_t x,
                                   std::size_t y) {
  const auto offset=(y*pixels.width+x)*4;
  return {pixels.rgba[offset],pixels.rgba[offset+1],
          pixels.rgba[offset+2],pixels.rgba[offset+3]};
}
Bytes AppendTrailingZlibStream(const Bytes& first,const Bytes& second) {
  const auto a=FindSingleIdat(first),b=FindSingleIdat(second);
  if(!a||!b)return {};
  Bytes payload(first.begin()+a->data,first.begin()+a->end);
  payload.insert(payload.end(),second.begin()+b->data,second.begin()+b->end);
  Bytes result(first.begin(),first.begin()+a->chunk);
  AppendTestBe32(result,static_cast<std::uint32_t>(payload.size()));
  const auto crc=result.size();result.insert(result.end(),{'I','D','A','T'});
  result.insert(result.end(),payload.begin(),payload.end());
  AppendTestBe32(result,TestCrc(result,crc,result.size()));
  result.insert(result.end(),first.begin()+a->end+4,first.end());
  return result;
}
Bytes InvalidFilterPng(const Bytes& source) {
  auto result=source;const auto idat=FindSingleIdat(result);
  if(!idat||idat->size<11)return {};
  const auto raw_begin=idat->data+7;
  const auto raw_size=std::size_t(result[idat->data+3])|
      (std::size_t(result[idat->data+4])<<8);
  if(raw_begin+raw_size+4!=idat->end)return {};
  result[raw_begin]=1;
  std::uint32_t a=1,b=0;
  for(std::size_t index=raw_begin;index<raw_begin+raw_size;++index) {
    a=(a+result[index])%65521;b=(b+a)%65521;
  }
  PutTestBe32(result,raw_begin+raw_size,(b<<16)|a);
  PutTestBe32(result,idat->end,TestCrc(result,idat->chunk+4,idat->end));
  return result;
}
FrozenDisplay Display(std::string uuid,DisplayId id,seethis::core::Rect logical,double scale) {
  return {uuid,id,logical,{0,0,logical.width*scale,logical.height*scale},scale,
    {scale,-scale,-logical.x*scale,(logical.y+logical.height)*scale}};
}
Context ContextFixture() {
  Context c;c.pid=42;c.window_pid=42;c.self_pid=12;c.window_id=500;
  c.app_name="Original app";c.bundle_id="test.original";c.executable="/test/original";c.window_title="Original window";
  c.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
  c.window={0,0,100,100};c.quartz_to_appkit_top=100;c.observed={1'000'000,1000};
  c.displays={Display("display-primary",1,{0,0,100,100},1)};
  c.excluded_window_ids={700,701};c.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";return c;
}
std::vector<DisplayPoint> Path(double shift=0) {
  return {{1,CoordinateUnit::kLogicalPoints,{20+shift,20},1},
          {1,CoordinateUnit::kLogicalPoints,{40+shift,20},1},
          {1,CoordinateUnit::kLogicalPoints,{40+shift,40},1},
          {1,CoordinateUnit::kLogicalPoints,{20+shift,40},1},
          {1,CoordinateUnit::kLogicalPoints,{20+shift,20},1}};
}
CaptureResult Capture() {
  CaptureResult r;r.requested={1'000'001,1001};r.completed={1'000'003,1003};
  r.pixels.width=100;r.pixels.height=100;r.pixels.rgba.resize(100*100*4);
  for(unsigned y=0;y<100;++y)for(unsigned x=0;x<100;++x) {
    auto p=(y*100+x)*4;r.pixels.rgba[p]=x;r.pixels.rgba[p+1]=y;
    r.pixels.rgba[p+2]=x^y;r.pixels.rgba[p+3]=255;
  }
  return r;
}
Reference PendingReference(std::string id) {
  Reference reference;reference.id=std::move(id);
  reference.context=ContextFixture();reference.path=Path();
  reference.circle_completed={1'000'002,1002};return reference;
}
std::string Identifier(unsigned value) {
  char buffer[33];std::snprintf(buffer,sizeof(buffer),"%032x",value);return buffer;
}
const std::string idA(32,'a'),idB(32,'b'),idC(32,'c'),idD(32,'d'),
    idE(32,'e'),idF(32,'f');
WindowObservation WindowFor(const Context& context,std::uint64_t generation,
                            Stamp observed=Now()) {
  WindowObservation observation;
  observation.availability=WindowAvailability::kReady;
  observation.generation=generation;observation.observed=observed;
  observation.pid=context.window_pid;observation.bundle_id=context.bundle_id;
  observation.window_id=context.window_id;observation.bounds=context.window;
  observation.native_window=context.native_window;
  observation.window_title=context.window_title;
  observation.title={1,TitleAvailability::kReady};
  return observation;
}
void Observe(MarkController& marks,const Context& context) {
  marks.SetWindowObservation(
      WindowFor(context,marks.window_observation().generation+1));
}
std::uint64_t Begin(MarkController& marks,std::string id=idA) {
  const auto context=ContextFixture();marks.SetViewportContinuity(1,true);Observe(marks,context);
  auto generation=marks.Begin(id);
  Check(marks.BindContext(generation,context),"context accepted");return generation;
}
void Commit(ReferenceStore& store,MarkController& marks,std::string id=idA,double shift=0) {
  auto generation=Begin(marks,id);marks.Captured(generation,Capture());
  Check(marks.AllowsDrawing(),"valid pixels enable drawing");
  marks.Release(generation,Path(shift),{1'000'002,1002});
  store.WaitForIdleForTesting();
  Check(marks.phase()==ReferencePhase::kCommitted,"committed only after release and pixels");
}
void Transactions() {
  TempDirectory temp;ReferenceStore store(temp.path);std::vector<StoredReference> copied;
  MarkController marks(store,[&](const auto& r){copied.push_back(r);return true;});
  auto context=ContextFixture();auto generation=marks.Begin(idA);
  Observe(marks,context);
  Check(!marks.AllowsDrawing(),"context pending stays mouse transparent");
  Check(marks.BindContext(generation,context),"bind original context");
  context.pid=999;context.window_title="changed current foreground";context.window.x=99;
  Check(marks.AllowsDrawing(),"drawing starts after immutable context snapshot");
  marks.Release(generation,Path(),{1'000'002,1002});
  Check(marks.phase()==ReferencePhase::kReleasedPendingCapture && copied.empty(),"release before async callback defers commit");
  marks.Captured(generation,Capture());
  store.WaitForIdleForTesting();
  Check(marks.phase()==ReferencePhase::kCommitted&&store.LookupMetadata(idA)->state==ReferenceJobState::kReady&&
      copied.size()==1&&marks.marks().empty(),"release-before-pixels persists/copies atomically while unready projection stays hidden");
  marks.SetViewportContinuity(1,true);Observe(marks,ContextFixture());
  Check(!marks.Pending()&&!marks.AllowsDrawing(),"Healthy plus fresh observation restores saved display without new capture");
  Check(marks.marks().size()==1 && copied.size()==1,"release commits same generation on pixels");
  Check(marks.marks()[0].value.source.bytes.empty()&&
        marks.marks()[0].value.crop.bytes.empty(),
        "UI mark history retains metadata without full image payloads");
  Check(marks.marks()[0].value.context.pid==42 && marks.marks()[0].value.context.window.x==0 &&
        marks.marks()[0].value.context.window_title=="Original window","context immutable after capture begins");
  const auto bytes=store.Lookup(idA)->ready->record;
  marks.Captured(generation,Capture());Check(marks.marks().size()==1 && copied.size()==1,"duplicate callback suppressed");
  auto accepted=Begin(marks,idD);marks.Release(accepted,Path(),{1'000'002,1002});marks.Abort("Escape after release");
  marks.Captured(accepted,Capture());store.WaitForIdleForTesting();
  Check(marks.marks().size()==2 && copied.size()==2,"accepted job survives later drawing cancellation");
  const char* routes[]={"user","modifier loss","lost key-up","watchdog","application switch","resign active","display changed","input monitor interrupted"};
  for(auto reason:routes) {
    auto g=Begin(marks,idB);marks.Abort(reason);marks.Captured(g,Capture());marks.Release(g,Path(),{1'000'002,1002});
    Check(!marks.AllowsDrawing() && !marks.Pending() && marks.marks().size()==2,"cancel route cannot persist");
  }
  auto denied=Begin(marks,idE);auto failed=Capture();failed.error="permission denied";marks.Captured(denied,std::move(failed));
  marks.Release(denied,Path(),{1'000'002,1002});store.WaitForIdleForTesting();Check(marks.phase()==ReferencePhase::kAborted,"denial aborts without false success");
  auto missing=Begin(marks,idF);auto empty=Capture();empty.pixels.rgba.clear();marks.Captured(missing,std::move(empty));
  Check(marks.AllowsDrawing() && marks.marks().size()==2,"missing pixels becomes terminal only after release");
  auto old=Begin(marks,idB);auto current=Begin(marks,idC);marks.Captured(old,Capture());
  Check(marks.phase()==ReferencePhase::kCapturePending,"superseded generation ignored");
  marks.Release(current,Path(20),{1'000'002,1002});marks.Captured(current,Capture());store.WaitForIdleForTesting();
  Check(marks.marks().size()==3 && marks.marks().back().value.id==idC,"superseding generation isolated");
  auto g=Begin(marks,idB);auto later=Now();later.monotonic_us+=31'000'000;marks.Expire(later);marks.Captured(g,Capture());
  Check(marks.phase()==ReferencePhase::kAborted,"capture timeout ignores late result");
  Check(store.Lookup(idA)->ready->record==bytes,"historical immutable after later transactions");
}
// Explicit simulated monotonic time; no live-duration or native-provider claim.
void SeparatedCaptureDeadlines() {
  TempDirectory temp;ReferenceStore store(temp.path);
  const auto origin=Now();std::atomic<std::int64_t> elapsed{0};
  const auto clock=[&] {const auto delta=elapsed.load();return Stamp{origin.utc_us+delta,origin.monotonic_us+delta};};
  ReferenceStoreTestHooks hooks;hooks.now=clock;store.SetTestHooks(hooks);
  MarkController marks(store,[](const auto&){return true;},clock);
  Commit(store,marks,idA);
  auto generation=Begin(marks,idB);elapsed+=30'000'001;marks.Expire(clock());
  Check(!marks.Pending()&&marks.phase()==ReferencePhase::kAborted,"F3 genuine unreturned screenshot keeps30s service bound");
  marks.Captured(generation,Capture());Check(!store.LookupMetadata(idB),"F3 late unaccepted screenshot cannot create a reference");

  generation=Begin(marks,idC);elapsed+=29'000'000;marks.Release(generation,Path(),clock());
  elapsed+=1'000'001;marks.Expire(clock());store.WaitForIdleForTesting();
  const auto failed=store.LookupMetadata(idC);
  Check(failed&&failed->state==ReferenceJobState::kFailed&&failed->code=="capture_timeout",
      "F3 release cannot renew unfinished screenshot's original deadline");
  marks.Captured(generation,Capture());store.WaitForIdleForTesting();
  Check(store.LookupMetadata(idC)->state==ReferenceJobState::kFailed,"F3 late accepted screenshot cannot revive failed ownership");

  auto chrome=ContextFixture();chrome.bundle_id="com.google.Chrome";
  marks.SetViewportContinuity(1,true);Observe(marks,chrome);
  generation=marks.Begin(idD);Check(marks.BindContext(generation,chrome),"F3 Chrome context binds");
  marks.Captured(generation,Capture());elapsed+=40'000'000;marks.Expire(clock());
  Check(marks.AllowsDrawing(),"F3 arrived screenshot has no overall30s drawing cap while page pending");
  marks.Release(generation,Path(),clock());elapsed+=30'000'000;marks.Expire(clock());
  Check(marks.Pending(),"F3 released page wait lives at exact release budget boundary");
  elapsed+=1;marks.Expire(clock());
  Check(!marks.Pending()&&marks.phase()==ReferencePhase::kAborted,"F3 released page no-return expires its own budget");
  seethis::platform::ChromePageSample sample;sample.availability=PageAvailability::kReady;
  sample.browser_pid=chrome.pid;sample.window_id=chrome.window_id;sample.window_bounds=chrome.window;
  sample.process_start_identity_us=700;sample.opaque_tab_id="owned-tab";sample.url="https://example.test/f3";
  const auto identity=seethis::platform::MakeChromePageIdentity(sample);
  Check(identity&&!marks.BindPageIdentity(generation,*identity),"F3 late page cannot revive released timeout");
  const auto next=Begin(marks,idE);marks.Captured(next,Capture());marks.Release(next,Path(),clock());store.WaitForIdleForTesting();
  Check(marks.phase()==ReferencePhase::kCommitted&&!marks.BindPageIdentity(generation,*identity),
      "F3 next valid generation recovers without accepting old page");

  unsigned completions=0;marks.SetAppendCompletion([&](auto,std::uint64_t){++completions;});
  const auto append=marks.BeginAppendReference(idA,1,idF);
  Check(append&&marks.BindContext(append,ContextFixture()),"F3 unfinished Append starts");
  elapsed+=29'000'000;marks.Release(append,Path(),clock());elapsed+=1'000'001;marks.Expire(clock());
  marks.Captured(append,Capture());store.WaitForIdleForTesting();
  Check(completions==1&&store.Collection(idA).shots.size()==1,"F3 unfinished accepted Append cancels once at screenshot service bound");

  auto metadata=PendingReference(idF);metadata.operation_started=origin;const auto original=metadata.operation_started;
  elapsed+=60'000'000;AppendResult result;
  store.AppendReferenceAsync(idA,1,idF,metadata,Capture(),[&](auto r){result=r;});store.WaitForIdleForTesting();
  Check(result.status==AppendStatus::kCommitted&&store.Collection(idA).shots.size()==2&&
        metadata.operation_started.monotonic_us==original.monotonic_us,
      "F3 Append admission budget is independent of immutable original gesture start");
  hooks.stage=[&](std::string_view stage,std::string_view) {if(stage=="append_before_publish")elapsed+=30'000'001;};
  store.SetTestHooks(hooks);unsigned terminal=0;
  store.AppendReferenceAsync(idA,2,idB,metadata,Capture(),[&](auto r){result=r;++terminal;});store.WaitForIdleForTesting();
  Check(terminal==1&&result.status==AppendStatus::kCancelled&&store.Collection(idA).shots.size()==2,
      "F3 physical Append finishing after deadline cannot publish membership");
  hooks.stage={};store.SetTestHooks(hooks);
  store.AppendReferenceAsync(idA,2,idB,metadata,Capture(),[&](auto r){result=r;});store.WaitForIdleForTesting();
  Check(result.status==AppendStatus::kCommitted&&store.Collection(idA).shots.size()==3,
      "F3 fresh admission recovers after timed-out physical work drains");
}

void FailedActionCustodyLifecycle() {
  TempDirectory temp;ReferenceStore store(temp.path);store.SetRetentionPolicy(3600,1000);
  // Stage real attempt creation on one simulated clock: Appends age31s while
  // ordinary pending captures age11s. No existing transaction is retimed.
  const auto fixed=Now();std::atomic<std::int64_t> elapsed{0};ReferenceStoreTestHooks clock;
  clock.now=[&]{const auto delta=elapsed.load();return Stamp{fixed.utc_us+delta,fixed.monotonic_us+delta};};store.SetTestHooks(clock);
  int copies=0,appendEvents=0;
  MarkController marks(store,[&](std::string_view)->std::optional<std::int64_t>{return ++copies;},
      [](auto,std::int64_t){return true;},[](auto task){task();},clock.now);
  Commit(store,marks);
  const auto append=marks.BeginAppendReference(idA,1,idC);
  Check(append&&marks.BindContext(append,ContextFixture()),"custody fixture starts actual unfinished append");
  marks.Release(append,Path(10),{1'000'002,1002});
  marks.SetAppendCompletion([&](auto,std::uint64_t){++appendEvents;});
  for(unsigned index=0;index<5;++index) {
    const auto g=Begin(marks,Identifier(9000+index));marks.Release(g,Path(),{1'000'002,1002});
    CaptureResult failure;failure.error="capture_failed";marks.Captured(g,failure);store.WaitForIdleForTesting();
  }
  const auto another=marks.BeginAppendReference(idA,1,idD);
  Check(another!=0,"over four retained failures do not consume active append capacity");
  Check(marks.BindContext(another,ContextFixture()),"second actual append binds");
  marks.Release(another,Path(20),{1'000'002,1002});
  elapsed=20'000'000;
  const auto pendingGeneration=Begin(marks,idB);marks.Release(pendingGeneration,Path(),{1'000'002,1002});
  std::uint64_t lastGeneration=0;
  for(unsigned index=0;index<=500;++index) {
    lastGeneration=Begin(marks,Identifier(10'000+index));
    marks.Release(lastGeneration,Path(),{1'000'002,1002});
    CaptureResult failed;failed.error="capture_failed";marks.Captured(lastGeneration,failed);
    store.WaitForIdleForTesting();
  }
  Observe(marks,ContextFixture());const auto before=copies;
  Check(store.LookupMetadata(Identifier(10'000))->state==ReferenceJobState::kFailed&&
        !marks.RecopyJob(Identifier(10'000))&&marks.RecopyJob(Identifier(10'500))&&copies==before+1,
        "501 real failures evict oldest custody at insertion while newest status URL remains owned");
  Check(marks.RecopyJob(idB)&&store.LookupMetadata(idB)->state==ReferenceJobState::kPending,
        "terminal custody cap never evicts an unfinished ordinary capture");
  const auto fourth=Begin(marks,idE);marks.Release(fourth,Path(30),{1'000'002,1002});
  Check(marks.BeginAppendReference(idA,1,idF)==0,
        "four actual unfinished entries including metadata-less appends still enforce busy admission");
  marks.Abort("no active drawing");
  elapsed=31'000'000;marks.Expire(clock.now());
  Check(appendEvents==2&&store.Collection(idA).shots.size()==1,
        "expiry cancels only two real appends and never completes failed custody");
  marks.Captured(append,Capture());marks.Captured(another,Capture());
  marks.Captured(lastGeneration,Capture());store.WaitForIdleForTesting();
  Observe(marks,ContextFixture());const auto after=copies;
  Check(marks.RecopyJob(Identifier(10'500))&&copies==after+1&&
        store.LookupMetadata(Identifier(10'500))->state==ReferenceJobState::kFailed&&appendEvents==2&&
        store.Collection(idA).shots.size()==1,
        "abort expiry and duplicate late capture do not reactivate terminal failure or canceled appends");
  Check(marks.DeleteJob(idB)==DeleteResult::kDeleted,"unfinished capture remains deletable after custody eviction");
  marks.Captured(pendingGeneration,Capture());store.WaitForIdleForTesting();
  Check(store.LookupMetadata(idB)->state==ReferenceJobState::kDeleted,
        "late capture after custody-bound test deletion preserves tombstone");
}

void StatusURLActionBoundaries() {
  {
    TempDirectory temp;ReferenceStore store(temp.path);int copies=0;
    MarkController marks(store,[&](std::string_view)->std::optional<std::int64_t>{return ++copies;},
        [](auto,std::int64_t){return true;},[](auto task){task();});
    const auto generation=Begin(marks);
    marks.Release(generation,Path(),{1'000'002,1002});
    CaptureResult failed;failed.error="capture_failed";marks.Captured(generation,failed);
    store.WaitForIdleForTesting();const auto metadata=store.LookupMetadata(idA);
    Check(metadata&&metadata->state==ReferenceJobState::kFailed&&copies==1&&marks.marks().empty()&&
          marks.SavedProjection(*metadata->metadata)==SavedProjectionState::kTerminal,
          "failed completion keeps one release publication and no ready projection");
    Check(marks.RecopyJob(idA)&&copies==2&&!marks.CopyHistoryJob(idA),
          "explicit same-source failed status URL recopy does not turn failure into ready history");
    auto wrong=WindowFor(ContextFixture(),marks.window_observation().generation+1);
    wrong.window_title="different source";marks.SetWindowObservation(wrong);
    Check(!marks.RecopyJob(idA)&&marks.DeleteJob(idA)==DeleteResult::kNotFound&&copies==2,
          "failed status actions reject another current title without mutation");
    Observe(marks,ContextFixture());
    Check(marks.RecopyJob(idA)&&copies==3,"fresh matching title restores owned failed status URL");
    marks.SetViewportContinuity(1,false);
    Check(!marks.RecopyJob(idA)&&copies==3,"failed status copy requires healthy current input");
    marks.SetViewportContinuity(1,true);Observe(marks,ContextFixture());
    marks.SetPresentationPermitted(false);
    Check(!marks.RecopyJob(idA)&&copies==3,"failed status copy obeys global presentation suppression");
    marks.SetPresentationPermitted(true);
    Check(!marks.RecopyJob(idA)&&copies==3&&store.LookupMetadata(idA)->state==ReferenceJobState::kFailed,
          "retired presentation does not manufacture new failed action custody");
    Check(marks.DeleteHistoryJob(idA)==DeleteResult::kDeleted&&!marks.RecopyJob(idA)&&copies==3,
          "deleted failed status cannot be recopied or resurrected");
  }
  {
    TempDirectory temp;ReferenceStore store(temp.path);int copies=0;
    MarkController marks(store,[&](std::string_view)->std::optional<std::int64_t>{return ++copies;},
        [](auto,std::int64_t){return true;},[](auto task){task();});
    marks.SetViewportContinuity(1,true);Observe(marks,ContextFixture());
    Check(store.AcceptPending(PendingReference(idA),0,0).state==ReferenceJobState::kPending,
          "unowned pending fixture contains otherwise valid source metadata");
    Check(!marks.RecopyJob(idA)&&marks.DeleteJob(idA)==DeleteResult::kNotFound&&copies==0,
          "matching source alone cannot manufacture a live accepted pending action");
    store.Fail(idA,"capture_failed");store.WaitForIdleForTesting();
    Check(!marks.RecopyJob(idA)&&marks.DeleteJob(idA)==DeleteResult::kNotFound&&copies==0,
          "valid failed metadata without owned capture custody cannot acquire live actions");
    auto invalid=PendingReference(idB);invalid.path.clear();
    Check(store.AcceptPending(invalid,0,0).state==ReferenceJobState::kFailed,
          "malformed pending metadata becomes an invalid failure");
    Check(!marks.RecopyJob(idB)&&copies==0,"invalid failed metadata never acquires a status publication");
  }
  {
    TempDirectory temp;ReferenceStore store(temp.path);int copies=0;
    MarkController marks(store,[&](const auto&){++copies;return true;});Commit(store,marks);
    const auto accepted=store.LookupMetadata(idA)->accepted_utc_us;
    ReferenceStoreTestHooks clock;clock.now=[=]{return Stamp{accepted+1'000'001,Now().monotonic_us};};
    store.SetTestHooks(clock);store.SetRetentionPolicy(1,100);
    Check(store.LookupMetadata(idA)->state==ReferenceJobState::kExpired,
          "ready status fixture reaches actual retention expiry");
    Observe(marks,ContextFixture());
    Check(!marks.RecopyJob(idA)&&marks.DeleteJob(idA)==DeleteResult::kNotFound&&copies==1&&marks.marks().empty(),
          "expired reference rejects live copy/delete and remains nonprojectable");
    store.WaitForIdleForTesting();
  }
}

void PersistenceAndClipboard() {
  TempDirectory temp;ReferenceStore store(temp.path);bool clipboardWorks=false;Bytes initialRecord,initialText,initialPNG;
  MarkController marks(store,[&](const auto& r){initialRecord=r.record;initialText=r.clipboard_text;initialPNG=r.clipboard_png;return clipboardWorks;});
  Commit(store,marks);Check(marks.copy_result()==CopyResult::kPersistedClipboardFailed,"persisted clipboard failure distinct");
  Check(store.Load().size()==1,"partial success remains durable");
  clipboardWorks=true;Check(marks.Recopy(0) && marks.copy_result()==CopyResult::kCopied,"identical stored envelope can retry");
  const auto original=*store.Lookup(idA)->ready;
  Check(store.Save(original)==SaveResult::kIdempotent,"same id same bytes idempotent");
  auto changed=original.value;changed.context.window_title="different immutable bytes";
  Check(store.Save(Canonical(changed))==SaveResult::kConflict,"same id different valid bytes conflicts");
  Commit(store,marks,idB,50);Check(marks.marks().size()==2,"multiple persistent marks");
  const auto topology=ContextFixture().displays;
  auto hitA=marks.Hit(Path()[0],2,topology);auto hitB=marks.Hit(Path(50)[0],2,topology);
  Check(hitA && *hitA==0 && hitB && *hitB==1,"exact mark hit selection");
  Check(!marks.Hit({1,CoordinateUnit::kLogicalPoints,{5,5},1},2,topology),"outside mark passes through");
  ReferenceStore restartedStore(temp.path);Bytes record,text,png;
  MarkController restarted(restartedStore,[&](const auto& r){record=r.record;text=r.clipboard_text;png=r.clipboard_png;return true;});
  restartedStore.WaitForIdleForTesting();
  restarted.SetViewportContinuity(1,true);
  Observe(restarted,ContextFixture());
  Check(restarted.marks().size()==2 && restarted.RecopyJob(idA),"restart indexes and recopies reference");
  Check(record==original.record && text==original.clipboard_text && png==original.clipboard_png,"restart re-copy byte identical in every representation");
  Check(restartedStore.Lookup(idA)->ready->value.context.window_title=="Original window","conflict cannot overwrite history");
  auto topologyChanged=topology;topologyChanged[0].logical.x=100;
  Check(!restarted.HitJob(Path()[0],3,topologyChanged),"historical mark hidden on geometry change");
  topologyChanged=topology;topologyChanged[0].uuid="other-display";
  Check(!restarted.HitJob(Path()[0],3,topologyChanged),"historical mark hidden on UUID change");
  topologyChanged=topology;topologyChanged[0].id=9;
  Check(!restarted.HitJob(Path()[0],3,topologyChanged),"transient remap conservatively hidden");
  Commit(store,marks,idC);auto overlap=marks.Hit(Path()[0],2,topology);Check(overlap && *overlap==2,"newest overlapping mark wins");
  {std::ofstream corrupt(temp.path/(idA+".stref"),std::ios::binary|std::ios::app);corrupt<<"corruption";}
  {std::ofstream staging(temp.path/".stage-interrupted");staging<<"incomplete unpublished bytes";}
  std::vector<std::string> errors;Check(store.Load(&errors).size()==2 && errors.size()==1,"corruption rejected and unfinished staging ignored");
  Check(store.Save(original)==SaveResult::kConflict,"corrupt published identity never silently overwritten");
}

void AsyncJobs() {
  TempDirectory temp;
  {
    ReferenceStore store(temp.path / "ordered");
    std::mutex mutex;
    std::condition_variable condition;
    bool release_first=false;
    std::vector<std::string> copied_text,completed;
    std::vector<std::function<void()>> dispatched;
    std::int64_t pasteboard=0;
    ReferenceStoreTestHooks hooks;
    hooks.stage=[&](std::string_view stage,std::string_view id) {
      if(stage=="finalize"&&id==std::string(32,'7')) {
        std::unique_lock lock(mutex);
        condition.wait(lock,[&]{return release_first;});
      }
    };
    store.SetTestHooks(std::move(hooks));
    MarkController marks(store,
      [&](std::string_view id)->std::optional<std::int64_t> {
        copied_text.emplace_back(id);return ++pasteboard;
      },
      [&](std::shared_ptr<const StoredReference> ready,std::int64_t expected) {
        completed.push_back(ready->value.id);return expected==pasteboard;
      },
      [&](std::function<void()> task){
        std::lock_guard lock(mutex);dispatched.push_back(std::move(task));
      });
    auto first=Begin(marks,std::string(32,'7'));
    marks.Captured(first,Capture());
    marks.Release(first,Path(),{1'000'002,1002},12);
    auto second=Begin(marks,std::string(32,'8'));
    marks.Captured(second,Capture());
    marks.Release(second,Path(20),{1'000'002,1002},13);
    for(int attempt=0;attempt<200;++attempt) {
      auto job=store.Lookup(std::string(32,'8'));
      if(job&&job->state==ReferenceJobState::kReady)break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(store.Lookup(std::string(32,'8'))->state==ReferenceJobState::kReady,
          "second job completes while first worker is delayed");
    {
      std::lock_guard lock(mutex);release_first=true;
    }
    condition.notify_all();store.WaitForIdleForTesting();
    std::vector<std::function<void()>> tasks;
    {std::lock_guard lock(mutex);tasks.swap(dispatched);}
    for(auto& task:tasks)task();
    Check(completed.size()==1&&completed[0]==std::string(32,'8'),
          "older completion cannot overwrite newer clipboard generation");
    Check(copied_text.size()==2&&marks.marks().size()==2,
          "rapid circles keep both accepted jobs");
    Check(store.Lookup(std::string(32,'7'))->key_up_monotonic_ms==12,
          "accepted job freezes release timing");
  }
  {
    ReferenceStore store(temp.path / "bounded");
    for(char digit='1';digit<='4';++digit) {
      Reference reference;reference.id=std::string(32,digit);
      reference.context=ContextFixture();reference.path=Path();
      reference.circle_completed={1'000'002,1002};
      Check(store.AcceptPending(reference,1,2).state==ReferenceJobState::kPending,
            "bounded executor accepts capacity cohort");
    }
    Reference excess;excess.id=std::string(32,'5');excess.context=ContextFixture();
    excess.path=Path();excess.circle_completed={1'000'002,1002};
    auto busy=store.AcceptPending(excess,1,2);
    Check(busy.state==ReferenceJobState::kFailed&&busy.code=="busy",
          "fifth unfinished job receives explicit backpressure");
  }
  {
    ReferenceStore restarted(temp.path/"bounded");
    auto shutdown=restarted.Lookup(std::string(32,'1'));
    Check(shutdown&&shutdown->state==ReferenceJobState::kFailed&&
          shutdown->code=="shutdown","graceful shutdown leaves no permanent pending job");
  }
  {
    const auto path=temp.path/"recovery";
    std::filesystem::create_directories(path);
    {std::ofstream(path/(std::string(32,'6')+".pending"))<<"pending\n";}
    ReferenceStore recovered(path);
    auto interrupted=recovered.Lookup(std::string(32,'6'));
    Check(interrupted&&interrupted->state==ReferenceJobState::kFailed&&
          interrupted->code=="interrupted","durable pending becomes interrupted after restart");
    Check(std::filesystem::exists(path/(std::string(32,'6')+".failed"))&&
          !std::filesystem::exists(path/(std::string(32,'6')+".pending")),
          "restart publishes terminal sidecar and removes pending intent");
  }
  {
    ReferenceStore store(temp.path/"failure");
    ReferenceStoreTestHooks hooks;hooks.fail_save=true;store.SetTestHooks(hooks);
    MarkController marks(store,[](const auto&){return true;});
    auto generation=Begin(marks,std::string(32,'9'));
    marks.Captured(generation,Capture());
    marks.Release(generation,Path(),{1'000'002,1002});
    store.WaitForIdleForTesting();
    auto failed=store.Lookup(std::string(32,'9'));
    Check(failed&&failed->state==ReferenceJobState::kFailed&&
          failed->code=="store_failed","save failure is terminal at the same ID");
    for(char digit='a';digit<='d';++digit)
      Check(store.AcceptPending(PendingReference(std::string(32,digit)),1,2).state==
                ReferenceJobState::kPending,
            "failed finalizer owner releases its accounting exactly once");
    Check(store.AcceptPending(PendingReference(std::string(32,'e')),1,2).code=="busy",
          "failure recovery preserves the four-job admission bound");
  }
  {
    ReferenceStore store(temp.path/"ownership");
    std::mutex mutex;std::condition_variable condition;bool release=false;
    ReferenceStoreTestHooks hooks;
    hooks.stage=[&](std::string_view stage,std::string_view) {
      if(stage=="finalize") {std::unique_lock lock(mutex);
        condition.wait(lock,[&]{return release;});}
    };
    store.SetTestHooks(std::move(hooks));
    std::atomic<std::int64_t> pasteboard{40};
    MarkController marks(store,
      [&](std::string_view)->std::optional<std::int64_t>{return pasteboard.load();},
      [&](std::shared_ptr<const StoredReference>,std::int64_t expected) {
        return expected==pasteboard.load();
      },[](std::function<void()> task){task();});
    auto generation=Begin(marks,std::string(32,'4'));
    marks.Captured(generation,Capture());
    marks.Release(generation,Path(),{1'000'002,1002});
    pasteboard.store(41);
    {std::lock_guard lock(mutex);release=true;}
    condition.notify_all();store.WaitForIdleForTesting();
    Check(marks.copy_result()==CopyResult::kPersistedClipboardFailed,
          "user pasteboard ownership loss blocks ready-format replacement");
    Check(store.Lookup(std::string(32,'4'))->state==ReferenceJobState::kReady,
          "clipboard ownership loss does not discard committed reference");
  }
  {
    std::atomic<std::int64_t> fake_utc{Now().utc_us};
    std::atomic<std::int64_t> fake_monotonic{Now().monotonic_us};
    std::weak_ptr<int> clock_hook_lifetime;
    {
      auto clock_probe=std::make_shared<int>(0);clock_hook_lifetime=clock_probe;
      ReferenceStore store(temp.path/"timeout");
      ReferenceStoreTestHooks hooks;
      hooks.now=[&,clock_probe]{(void)clock_probe;
        return Stamp{fake_utc.load(),fake_monotonic.load()};};
      store.SetTestHooks(std::move(hooks));clock_probe.reset();
      auto reference=PendingReference(std::string(32,'0'));
      const auto accepted=store.AcceptPending(reference,1,2);
      Check(accepted.state==ReferenceJobState::kPending&&
            accepted.deadline_monotonic_us-accepted.accepted_monotonic_us==30'000'000&&
            accepted.deadline_utc_us-accepted.accepted_utc_us==30'000'000,
            "timeout fixture starts pending");
      fake_utc.fetch_add(86'400'000'000);
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
      Check(store.Lookup(reference.id)->state==ReferenceJobState::kPending,
            "forward UTC-only jump cannot expire a monotonic deadline");
      fake_utc.fetch_sub(172'800'000'000);
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
      Check(store.Lookup(reference.id)->state==ReferenceJobState::kPending,
            "backward UTC-only jump cannot defer or alter a monotonic deadline");
      fake_monotonic.fetch_add(31'000'000);
      for(int attempt=0;attempt<100;++attempt) {
        if(store.Lookup(reference.id)->state==ReferenceJobState::kFailed)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      auto timed_out=store.Lookup(reference.id);
      Check(timed_out&&timed_out->state==ReferenceJobState::kFailed&&
            timed_out->code=="timeout","monotonic expiry terminalizes the same stable ID");
    }
    Check(clock_hook_lifetime.expired(),
          "store shutdown joins timer and worker tail before captured clocks leave scope");
  }
  {
    std::atomic<std::int64_t> fake_utc{Now().utc_us};
    std::atomic<std::int64_t> fake_monotonic{Now().monotonic_us};
    ReferenceStore store(temp.path/"precommit-clock");
    ReferenceStoreTestHooks hooks;
    hooks.now=[&]{return Stamp{fake_utc.load(),fake_monotonic.load()};};
    store.SetTestHooks(std::move(hooks));
    const std::string forward(32,'f'),backward(32,'b');
    Check(store.AcceptPending(PendingReference(forward),1,2).state==ReferenceJobState::kPending,
          "precommit forward-jump fixture starts pending");
    fake_utc.fetch_add(86'400'000'000);
    store.FinalizeAsync(forward,Capture());store.WaitForIdleForTesting();
    Check(store.Lookup(forward)->state==ReferenceJobState::kReady,
          "forward UTC-only jump cannot reject finalizer precommit");
    Check(store.AcceptPending(PendingReference(backward),1,2).state==ReferenceJobState::kPending,
          "precommit backward-jump fixture starts pending");
    fake_utc.fetch_sub(172'800'000'000);fake_monotonic.fetch_add(31'000'000);
    store.FinalizeAsync(backward,Capture());store.WaitForIdleForTesting();
    auto expired=store.Lookup(backward);
    Check(expired&&expired->state==ReferenceJobState::kFailed&&expired->code=="timeout",
          "backward UTC-only jump cannot defer monotonic finalizer expiry");
  }
  {
    std::mutex mutex;std::condition_variable condition;
    bool release_owners=false;std::size_t blocked=0;
    std::atomic<std::int64_t> fake_utc{Now().utc_us};
    std::atomic<std::int64_t> fake_monotonic{Now().monotonic_us};
    const std::string first(32,'a'),second(32,'b');
    ReferenceStoreTestHooks hooks;
    hooks.now=[&]{return Stamp{fake_utc.load(),fake_monotonic.load()};};
    hooks.stage=[&](std::string_view stage,std::string_view id) {
      if(stage!="capture"||(id!=first&&id!=second))return;
      std::unique_lock lock(mutex);++blocked;condition.notify_all();
      condition.wait(lock,[&]{return release_owners;});
    };
    ReferenceStore store(temp.path/"retained-owner-accounting");
    store.SetTestHooks(std::move(hooks));
    Check(store.AcceptPending(PendingReference(first),1,2).state==ReferenceJobState::kPending&&
          store.AcceptPending(PendingReference(second),1,2).state==ReferenceJobState::kPending,
          "two jobs reserve bounded capacity");
    store.FinalizeAsync(first,Capture());store.FinalizeAsync(second,Capture());
    {
      std::unique_lock lock(mutex);
      Check(condition.wait_for(lock,std::chrono::seconds(2),[&]{return blocked==2;}),
            "both workers retain captured buffers");
    }
    fake_utc.fetch_add(86'400'000'000);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(store.Lookup(first)->state==ReferenceJobState::kPending&&
          store.Lookup(second)->state==ReferenceJobState::kPending,
          "UTC jump leaves blocked owners pending");
    fake_monotonic.fetch_add(31'000'000);
    for(int attempt=0;attempt<100;++attempt) {
      if(store.Lookup(first)->state==ReferenceJobState::kFailed&&
         store.Lookup(second)->state==ReferenceJobState::kFailed)break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(store.Lookup(first)->code=="timeout"&&store.Lookup(second)->code=="timeout",
          "blocked owners become externally terminal without blocking timer");
    const std::string replacement1(32,'c'),replacement2(32,'d'),excess(32,'e');
    Check(store.AcceptPending(PendingReference(replacement1),1,2).state==ReferenceJobState::kPending&&
          store.AcceptPending(PendingReference(replacement2),1,2).state==ReferenceJobState::kPending,
          "only capacity not retained by timed-out owners remains available");
    auto rejected=store.AcceptPending(PendingReference(excess),1,2);
    Check(rejected.state==ReferenceJobState::kFailed&&rejected.code=="busy",
          "retained owner bytes reject the exact excess replacement");
    store.Fail(replacement1,"cancelled");store.Fail(replacement1,"cancelled-again");
    store.Fail(replacement2,"cancelled");
    store.FinalizeAsync(replacement1,Capture());
    {
      std::lock_guard lock(mutex);release_owners=true;
    }
    condition.notify_all();store.WaitForIdleForTesting();
    Check(store.Lookup(first)->state==ReferenceJobState::kFailed&&
          store.Lookup(second)->state==ReferenceJobState::kFailed&&
          !std::filesystem::exists(temp.path/"retained-owner-accounting"/(first+".stref"))&&
          !std::filesystem::exists(temp.path/"retained-owner-accounting"/(second+".stref")),
          "late worker callbacks cannot resurrect timed-out jobs");
    std::vector<std::string> recovered;
    for(char digit='1';digit<='4';++digit) {
      recovered.emplace_back(32,digit);
      Check(store.AcceptPending(PendingReference(recovered.back()),1,2).state==
                ReferenceJobState::kPending,
            "last owner release restores one reservation exactly once");
    }
    auto still_bounded=store.AcceptPending(PendingReference(std::string(32,'5')),1,2);
    Check(still_bounded.state==ReferenceJobState::kFailed&&still_bounded.code=="busy",
          "release cannot underflow accounting or expand the four-job bound");
    store.FinalizeAsync(recovered.front(),Capture());
    for(std::size_t index=1;index<recovered.size();++index)
      store.Fail(recovered[index],"cancelled");
    store.WaitForIdleForTesting();
    Check(store.Lookup(recovered.front())->state==ReferenceJobState::kReady,
          "normal owner completion releases after finishing");
    for(char digit='6';digit<='9';++digit)
      Check(store.AcceptPending(PendingReference(std::string(32,digit)),1,2).state==
                ReferenceJobState::kPending,
            "normal finish and cancellation leave no accounting leak");
    Check(store.AcceptPending(PendingReference(std::string(32,'f')),1,2).code=="busy",
          "late callbacks and repeated cancellation do not double-release capacity");
  }
}
void Deletion() {
  TempDirectory temp;
  const auto ready_root=temp.path/"ready";
  {
    ReferenceStore store(ready_root);
    MarkController marks(store,[](const auto&){return true;});
    Commit(store,marks,idA);
    Commit(store,marks,idB);
    const auto overlap=marks.HitIdentity(Path().front(),2,
                                         ContextFixture().displays);
    Check(overlap&&*overlap==idB,
          "hover identity uses the newest overlapping stroke");
    Check(marks.DeleteJob(idB)==DeleteResult::kDeleted,
          "ready deletion removes the selected identity");
    Check(!marks.HitIdentity(Path().front(),2,ContextFixture().displays)||
              *marks.HitIdentity(Path().front(),2,ContextFixture().displays)==idA,
          "deleted topmost mark disappears and reveals the unrelated mark");
    for(const auto& suffix:{".pending",".failed",".ready",".stref",
                            ".source.png",".crop.png"})
      Check(!std::filesystem::exists(ready_root/(idB+suffix)),
            "ready deletion removes every owned local payload");
    Check(std::filesystem::exists(ready_root/(idB+".deleted"))&&
          store.Lookup(idB)->state==ReferenceJobState::kDeleted&&
          store.Lookup(idB)->code=="deleted_reference",
          "ready deletion leaves a truthful nonrecyclable tombstone");
    Check(store.Lookup(idA)->state==ReferenceJobState::kReady&&
          marks.RecopyJob(idA),"ready deletion preserves unrelated history");
    Check(marks.DeleteJob(idB)==DeleteResult::kAlreadyDeleted,
          "repeated deletion is idempotent");
    Check(store.Save(*store.Lookup(idA)->ready)==SaveResult::kIdempotent,
          "unrelated immutable record remains usable");
  }
  {std::ofstream(ready_root/(idB+".stref"))<<"late crash residue";}
  {std::ofstream(ready_root/(idB+".source.png"))<<"late crash residue";}
  {
    ReferenceStore restarted(ready_root);
    Check(restarted.Lookup(idB)->state==ReferenceJobState::kDeleted&&
          restarted.Lookup(idA)->state==ReferenceJobState::kReady,
          "restart restores deleted and unrelated ready states separately");
    Check(!std::filesystem::exists(ready_root/(idB+".stref"))&&
              !std::filesystem::exists(ready_root/(idB+".source.png")),
          "restart tombstone removes assets left by an interrupted delete race");
    Check(restarted.AcceptPending(PendingReference(idB),1,2).state==
              ReferenceJobState::kDeleted,
          "deleted identity cannot be recycled after restart");
    Check(restarted.Save(*restarted.Lookup(idA)->ready)==SaveResult::kIdempotent,
          "restart retains the unrelated reference bytes");
  }
  {
    const auto failure_root=temp.path/"cleanup-failure";
    ReferenceStore store(failure_root);
    MarkController marks(store,[](const auto&){return true;});
    Commit(store,marks,idC);
    ReferenceStoreTestHooks hooks;
    hooks.remove_owned_file=[&](const std::filesystem::path& path) {
      if(path.filename()==idC+".source.png")return false;
      errno=0;return unlink(path.c_str())==0||errno==ENOENT;
    };
    store.SetTestHooks(std::move(hooks));
    Check(marks.DeleteJob(idC)==DeleteResult::kCleanupFailed&&
          store.Lookup(idC)->state==ReferenceJobState::kDeleted&&
          store.Lookup(idC)->code=="delete_cleanup_failed"&&
          std::filesystem::exists(failure_root/(idC+".source.png")),
          "partial cleanup is durable and actionable");
    store.SetTestHooks({});
    Check(marks.DeleteJob(idC)==DeleteResult::kAlreadyDeleted&&
          !std::filesystem::exists(failure_root/(idC+".source.png"))&&
          store.Lookup(idC)->code=="deleted_reference",
          "repeated deletion retries and completes partial cleanup exactly once");
  }
  {
    const auto race_root=temp.path/"late-worker";
    ReferenceStore store(race_root);
    std::mutex mutex;std::condition_variable condition;
    bool stored=false,release=false;
    ReferenceStoreTestHooks hooks;
    hooks.stage=[&](std::string_view stage,std::string_view id) {
      if(stage!="stored"||id!=idD)return;
      std::unique_lock lock(mutex);stored=true;condition.notify_all();
      condition.wait(lock,[&]{return release;});
    };
    store.SetTestHooks(std::move(hooks));
    int pending_copies=0,ready_copies=0,completion_dispatches=0;
    MarkController marks(store,
      [&](std::string_view)->std::optional<std::int64_t> {
        ++pending_copies;return pending_copies;
      },
      [&](std::shared_ptr<const StoredReference>,std::int64_t) {
        ++ready_copies;return true;
      },[&](std::function<void()> task){++completion_dispatches;task();});
    const auto generation=Begin(marks,idD);
    marks.Captured(generation,Capture());
    marks.Release(generation,Path(),{1'000'002,1002});
    {
      std::unique_lock lock(mutex);
      Check(condition.wait_for(lock,std::chrono::seconds(2),[&]{return stored;}),
            "late-worker race reaches the post-store boundary");
    }
    Check(marks.DeleteJob(idD)==DeleteResult::kDeleted&&
          store.Lookup(idD)->state==ReferenceJobState::kDeleted,
          "pending deletion tombstones a worker after immutable save");
    {
      std::lock_guard lock(mutex);release=true;
    }
    condition.notify_all();store.WaitForIdleForTesting();
    Check(store.Lookup(idD)->state==ReferenceJobState::kDeleted&&
          pending_copies==1&&ready_copies==0&&completion_dispatches==1&&
          marks.marks().empty(),
          "deletion completes once and late work cannot recreate mark or clipboard");
    for(const auto& suffix:{".pending",".failed",".ready",".stref",
                            ".source.png",".crop.png"})
      Check(!std::filesystem::exists(race_root/(idD+suffix)),
            "late completion cannot recreate any owned payload");
    Check(marks.DeleteJob(idD)==DeleteResult::kAlreadyDeleted,
          "concurrent completion leaves repeated deletion harmless");
  }
  {
    const auto pending_root=temp.path/"pending";
    ReferenceStore store(pending_root);
    MarkController marks(store,[](const auto&){return true;});
    const auto generation=Begin(marks,idE);
    marks.Release(generation,Path(),{1'000'002,1002});
    Check(marks.DeleteJob(idE)==DeleteResult::kDeleted,
          "pending-before-capture deletion cancels the stable identity");
    marks.Captured(generation,Capture());store.WaitForIdleForTesting();
    Check(store.Lookup(idE)->state==ReferenceJobState::kDeleted&&
          marks.marks().empty()&&
          !std::filesystem::exists(pending_root/(idE+".stref")),
          "late capture after pending deletion cannot resurrect history");
  }
}
void Retention() {
  TempDirectory temp;
  std::atomic<std::int64_t> fake_utc{Now().utc_us};
  std::atomic<std::int64_t> fake_monotonic{Now().monotonic_us};
  ReferenceStoreTestHooks clock;
  clock.now=[&]{return Stamp{fake_utc.load(),fake_monotonic.load()};};
  const auto failed_root=temp.path/"failed";
  {
    ReferenceStore store(failed_root);store.SetTestHooks(clock);
    store.SetRetentionPolicy(1,3);
    for(unsigned value=1;value<=8;++value) {
      auto job=store.AcceptPending(PendingReference(Identifier(value)),1,2);
      Check(job.state==ReferenceJobState::kPending,"retention failure cohort accepts");
      store.Fail(job.id,value%2?"cancelled":"timeout");
    }
    store.WaitForIdleForTesting();
    const auto jobs=store.Jobs();
    const auto failed=std::count_if(jobs.begin(),jobs.end(),[](const auto& job) {
      return job.state==ReferenceJobState::kFailed;
    });
    const auto expired=std::count_if(jobs.begin(),jobs.end(),[](const auto& job) {
      return job.state==ReferenceJobState::kExpired;
    });
    Check(jobs.size()<=6&&failed<=3&&expired<=3,
          "visible failures and expired tombstones have independent finite bounds");
    Check(!store.Lookup(Identifier(1))&&
          store.Lookup(Identifier(5))->state==ReferenceJobState::kExpired,
          "old terminal IDs progress from bounded 410 tombstone to unknown");
    std::size_t failed_sidecars=0;
    for(const auto& item:std::filesystem::directory_iterator(failed_root))
      if(item.path().extension()==".failed")++failed_sidecars;
    Check(failed_sidecars<=3,"durable failure sidecars obey visible retention bound");
  }
  {
    ReferenceStore restarted(failed_root);restarted.SetTestHooks(clock);
    restarted.SetRetentionPolicy(1,3);restarted.WaitForIdleForTesting();
    const auto jobs=restarted.Jobs();
    Check(jobs.size()<=6,"restart restores only bounded terminal metadata");
    Check(std::all_of(jobs.begin(),jobs.end(),[](const auto& job) {
      return job.accepted_utc_us>0&&job.terminal_utc_us>0;
    }),"restarted failures and tombstones retain usable timestamps");
    fake_utc.fetch_add(1'000'001);
    Check(!restarted.Lookup(Identifier(5)),
          "expired tombstone becomes unknown after its finite grace TTL");
    restarted.WaitForIdleForTesting();
  }
  const auto ready_root=temp.path/"ready";std::string retained_ready_id;
  {
    ReferenceStore store(ready_root);store.SetTestHooks(clock);
    store.SetRetentionPolicy(3600,12);
    for(unsigned value=32;value<46;++value) {
      MarkController marks(store,[](const auto&){return true;});
      Commit(store,marks,Identifier(value),double(value-32));
    }
    store.WaitForIdleForTesting();const auto jobs=store.Jobs();
    const auto visible=std::count_if(jobs.begin(),jobs.end(),[](const auto& job) {
      return job.state==ReferenceJobState::kReady;
    });
    const auto payloads=std::count_if(jobs.begin(),jobs.end(),[](const auto& job) {
      return static_cast<bool>(job.ready);
    });
    Check(visible==12&&payloads<=static_cast<decltype(payloads)>(
              ReferenceStore::kMaximumCachedReadyPayloads),
          "ready metadata is bounded separately from full payload cache");
    Check(store.Lookup(Identifier(32))->state==ReferenceJobState::kExpired&&
          !std::filesystem::exists(ready_root/(Identifier(32)+".stref")),
          "ready overflow becomes 410 and releases its durable full asset");
    auto uncached=std::find_if(jobs.begin(),jobs.end(),[](const auto& job) {
      return job.state==ReferenceJobState::kReady&&!job.ready;
    });
    Check(uncached!=jobs.end(),"ready cohort exceeds the full-payload cache");
    auto loaded=store.Lookup(uncached->id);
    Check(loaded&&loaded->state==ReferenceJobState::kReady&&loaded->ready&&
          loaded->ready->value.id==uncached->id,
          "uncached retained ID loads directly from immutable per-ID storage");
    retained_ready_id=uncached->id;
  }
  {
    ReferenceStore restarted(ready_root);restarted.SetTestHooks(clock);
    restarted.SetRetentionPolicy(3600,12);
    auto indexed=restarted.Jobs();
    Check(std::count_if(indexed.begin(),indexed.end(),[](const auto& job) {
      return static_cast<bool>(job.ready);
    })==0,"restart indexes ready metadata without eager record payload reads");
    Check(std::all_of(indexed.begin(),indexed.end(),[](const auto& job) {
      return job.state!=ReferenceJobState::kReady||
             (job.context.pid>0&&!job.path.empty());
    }),"ready sidecars preserve lightweight hit-test metadata across restart");
    Check(static_cast<bool>(restarted.Lookup(retained_ready_id)->ready),
          "restart retrieves one retained ready identity by exact path");
    fake_utc.fetch_add(3'600'000'001);
    Check(!restarted.Lookup(Identifier(32)),
          "ready tombstone eventually becomes unknown after its grace TTL");
  }
  {
    ReferenceStore store(temp.path/"owned");store.SetTestHooks(clock);
    store.SetRetentionPolicy(1,1);
    std::mutex mutex;std::condition_variable condition;bool blocked=false,release=false;
    auto hooks=clock;hooks.stage=[&](std::string_view stage,std::string_view) {
      if(stage!="capture")return;std::unique_lock lock(mutex);blocked=true;
      condition.notify_all();condition.wait(lock,[&]{return release;});
    };store.SetTestHooks(std::move(hooks));
    const auto owned=Identifier(64);
    Check(store.AcceptPending(PendingReference(owned),1,2).state==ReferenceJobState::kPending,
          "retention owner fixture accepts");
    store.FinalizeAsync(owned,Capture());
    {std::unique_lock lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(2),
      [&]{return blocked;}),"retention fixture worker owns capture");}
    store.Fail(owned,"cancelled");fake_utc.fetch_add(2'000'000);
    Check(store.Lookup(owned)->state==ReferenceJobState::kFailed,
          "terminal worker-owned job is protected from retention eviction");
    {std::lock_guard lock(mutex);release=true;}condition.notify_all();
    store.WaitForIdleForTesting();
    Check(store.Lookup(owned)->state==ReferenceJobState::kExpired,
          "owner release permits terminal transition to bounded tombstone");
    store.FinalizeAsync(owned,Capture());fake_utc.fetch_add(1'000'001);
    Check(!store.Lookup(owned),"late finalize cannot resurrect an evicted identity");
    for(unsigned value=70;value<74;++value)
      Check(store.AcceptPending(PendingReference(Identifier(value)),1,2).state==
                ReferenceJobState::kPending,
            "owner retention releases capacity exactly once");
    Check(store.AcceptPending(PendingReference(Identifier(74)),1,2).code=="busy",
          "retention cannot underflow the four-job admission bound");
  }
}
void LegacyReadyMigration() {
  TempDirectory temp;const auto root=temp.path/"legacy";
  std::vector<std::string> ids;std::vector<Bytes> records;
  {
    ReferenceStore store(root);
    for(unsigned value=100;value<107;++value) {
      ids.push_back(Identifier(value));
      MarkController marks(store,[](const auto&){return true;});
      Commit(store,marks,ids.back(),double(value-100));
      records.push_back(store.Lookup(ids.back())->ready->record);
    }
  }
  std::vector<Bytes> immutable;
  for(const auto& id:ids) {
    immutable.push_back(FileBytes(root/(id+".stref")));
    Check(std::filesystem::remove(root/(id+".ready")),
          "legacy fixture removes only the new metadata sidecar");
  }
  Bytes copied;
  {
    ReferenceStore migrated(root);
    MarkController marks(migrated,[&](const auto& ready) {
      copied=ready.record;return true;
    });
    const auto pending=migrated.Jobs();
    Check(std::none_of(pending.begin(),pending.end(),[](const auto& job) {
      return job.state==ReferenceJobState::kReady&&
             (job.context.pid<=0||job.path.empty());
    }),"legacy indexing never publishes ready marks with empty geometry");
    migrated.WaitForIdleForTesting();const auto ready=migrated.Jobs();
    marks.SetViewportContinuity(1,true);
    Observe(marks,ContextFixture());
    Check(ready.size()==ids.size()&&std::all_of(ready.begin(),ready.end(),
      [](const auto& job) {return job.state==ReferenceJobState::kReady&&
        job.context.pid>0&&!job.path.empty()&&!job.ready;}),
      "bounded migration derives metadata without retaining full payloads");
    Check(marks.marks().size()==ids.size()&&
          marks.Hit(Path().front(),2,ContextFixture().displays).has_value(),
          "first completed migration makes legacy marks visible and hit-testable");
    Check(marks.RecopyJob(ids.front())&&copied==records.front(),
          "legacy re-copy resolves the exact immutable record");
    for(std::size_t index=0;index<ids.size();++index) {
      Check(std::filesystem::exists(root/(ids[index]+".ready")),
            "migration durably publishes a ready sidecar");
      Check(FileBytes(root/(ids[index]+".stref"))==immutable[index],
            "migration preserves predecessor record bytes");
    }
  }
  {
    ReferenceStore restarted(root);
    MarkController marks(restarted,[](const auto&){return true;});
    marks.SetViewportContinuity(1,true);
    Observe(marks,ContextFixture());
    const auto indexed=restarted.Jobs();
    Check(std::all_of(indexed.begin(),indexed.end(),[](const auto& job) {
      return job.state==ReferenceJobState::kReady&&job.context.pid>0&&
             !job.path.empty()&&!job.ready;
    }),"second restart uses durable metadata without remigration or eager payloads");
    Check(marks.Hit(Path().front(),2,ContextFixture().displays).has_value()&&
          restarted.Lookup(ids.back())->ready,
          "second restart supports mark hit-testing and exact direct lookup");
  }
  const auto corrupt_root=temp.path/"corrupt-legacy";
  {
    ReferenceStore store(corrupt_root);
    for(unsigned value=200;value<202;++value) {
      MarkController marks(store,[](const auto&){return true;});
      Commit(store,marks,Identifier(value));
    }
  }
  const auto corrupt_id=Identifier(200),valid_id=Identifier(201);
  Check(std::filesystem::remove(corrupt_root/(corrupt_id+".ready"))&&
        std::filesystem::remove(corrupt_root/(valid_id+".ready")),
        "mixed legacy fixture removes metadata sidecars");
  {std::ofstream corrupt(corrupt_root/(corrupt_id+".stref"),
                         std::ios::binary|std::ios::app);corrupt<<"corrupt";}
  {
    ReferenceStore migrated(corrupt_root);migrated.WaitForIdleForTesting();
    Check(migrated.Lookup(corrupt_id)->state==ReferenceJobState::kFailed&&
          migrated.Lookup(corrupt_id)->code=="corrupt"&&
          std::filesystem::exists(corrupt_root/(corrupt_id+".failed")),
          "corrupt predecessor terminalizes durably and honestly");
    Check(migrated.Lookup(valid_id)->state==ReferenceJobState::kReady&&
          std::filesystem::exists(corrupt_root/(valid_id+".ready")),
          "corrupt predecessor does not affect unrelated migration");
  }
  const auto shutdown_root=temp.path/"shutdown-legacy";
  {
    ReferenceStore store(shutdown_root);
    for(unsigned value=300;value<307;++value) {
      MarkController marks(store,[](const auto&){return true;});
      Commit(store,marks,Identifier(value));
    }
  }
  for(unsigned value=300;value<307;++value)
    Check(std::filesystem::remove(shutdown_root/(Identifier(value)+".ready")),
          "shutdown legacy fixture removes metadata sidecar");
  {ReferenceStore interrupted(shutdown_root);}
  std::size_t first_wave=0;
  for(unsigned value=300;value<307;++value)
    if(std::filesystem::exists(shutdown_root/(Identifier(value)+".ready")))
      ++first_wave;
  Check(first_wave<=2,"shutdown drains only the bounded scheduled migration wave");
  {
    ReferenceStore resumed(shutdown_root);resumed.WaitForIdleForTesting();
    const auto jobs=resumed.Jobs();
    Check(std::all_of(jobs.begin(),jobs.end(),
      [](const auto& job) {return job.state==ReferenceJobState::kReady&&
        job.context.pid>0&&!job.path.empty()&&!job.ready;}),
      "restart resumes interrupted migration through the bounded executor");
  }
  std::atomic<std::int64_t> fake_utc{Now().utc_us};
  ReferenceStoreTestHooks clock;clock.now=[&] {return Stamp{fake_utc.load(),1};};
  {
    ReferenceStore retained(root);retained.SetTestHooks(clock);
    const auto before_expiry=retained.Lookup(ids.front());
    Check(before_expiry&&before_expiry->accepted_utc_us>0,
          "migrated legacy record has a retained acceptance clock");
    fake_utc.store(before_expiry->accepted_utc_us+1'000'001);
    retained.SetRetentionPolicy(1,ids.size());
    const auto expired=retained.Lookup(ids.front());
    Check(expired&&expired->state==ReferenceJobState::kExpired,
          "migrated legacy records enter the normal expired state");
    retained.WaitForIdleForTesting();
    Check(!std::filesystem::exists(root/(ids.front()+".stref"))&&
          !std::filesystem::exists(root/(ids.front()+".ready")),
          "legacy migration cooperates with durable asset pruning");
    fake_utc.fetch_add(2'000'000);
    Check(!retained.Lookup(ids.front()),
          "migrated legacy tombstone eventually becomes unknown");
  }
}
void NativeAsymmetricCrop() {
#ifdef __APPLE__
  TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});
  auto capture=Capture();
  // Premultiplied red at the future top-left crop corner additionally checks alpha.
  const std::size_t corner=(52*100+12)*4;
  capture.pixels.rgba[corner]=64;capture.pixels.rgba[corner+1]=0;
  capture.pixels.rgba[corner+2]=0;capture.pixels.rgba[corner+3]=128;
  capture.pixels=seethis::platform::NormalizeSyntheticCaptureForTest(capture.pixels);
  Check(ValidPixels(capture.pixels),"actual native conversion accepts asymmetric fixture");
  auto generation=Begin(marks);marks.Captured(generation,std::move(capture));
  marks.Release(generation,Path(),{1'000'002,1002});
  store.WaitForIdleForTesting();
  Check(marks.phase()==ReferencePhase::kCommitted,"native normalized fixture commits");
  const auto stored=store.Lookup(idA);const auto& ref=stored->ready->value;
  Check(ref.crop_pixels.x==12 && ref.crop_pixels.y==52 && ref.crop_pixels.width==36 && ref.crop_pixels.height==36,
    "AppKit circle maps to exact top-left source crop coordinates");
  Pixels expected{36,36,{}};
  for(unsigned y=52;y<88;++y)for(unsigned x=12;x<48;++x)
    expected.rgba.insert(expected.rgba.end(),{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y),static_cast<std::uint8_t>(x^y),255});
  expected.rgba[0]=128;expected.rgba[1]=0;expected.rgba[2]=0;expected.rgba[3]=128;
  Check(ref.crop.bytes==EncodePng(expected),"exact asymmetric crop pixels: native Y inversion must fail this regression");
  Check(ref.global_to_source.d==-1 && ref.region.x==12 && ref.region.y==12,
    "native normalized crop retains frozen global-to-source Y transform");
#endif
}
void GeometryAndCanonical() {
  auto c=ContextFixture();c.displays.push_back(Display("negative-retina",2,{-100,0,100,100},2));c.window={-60,10,120,80};
  std::vector<DisplayPoint> path={{2,CoordinateUnit::kLogicalPoints,{70,20},2},{1,CoordinateUnit::kLogicalPoints,{30,70},1}};
  auto crop=CropBounds(c,path,240,160,0);
  Check(crop && crop->x==60 && crop->y==40 && crop->width==120 && crop->height==100,"mixed scale negative-origin cross-display crop in frozen window pixel frame");
  crop=CropBounds(c,path,240,160,8);
  Check(crop && crop->x==44 && crop->y==24 && crop->width==152 && crop->height==132,"margin transformed from logical points");
  path[0].backing_scale=1;Check(!CropBounds(c,path,240,160,0),"stale sample scale rejected");path[0].backing_scale=2;
  path[0].position.x=-1;Check(!CropBounds(c,path,240,160,0),"out of display sample rejected");
  path={{2,CoordinateUnit::kLogicalPoints,{10,50},2}};Check(!CropBounds(c,path,240,160,0),"nonintersecting crop rejected");
  path={{2,CoordinateUnit::kLogicalPoints,{45,15},2},{1,CoordinateUnit::kLogicalPoints,{50,85},1}};
  crop=CropBounds(c,path,240,160,8);Check(crop && crop->x==0 && crop->y==0 && crop->width==236 && crop->height==160,"spanning crop clamped to source bounds");
  auto reordered=c.displays;std::reverse(reordered.begin(),reordered.end());Check(DisplayTopologyMatches(c.displays,reordered),"display enumeration ordering irrelevant");
  TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});Commit(store,marks);
  const auto r=store.Lookup(idA)->ready->value;const auto wire=Serialize(r);auto decoded=Deserialize(wire);
  Check(decoded && Serialize(*decoded)==wire && Validate(*decoded),"stable canonical binary round trip");
  Check(wire[0]==0 && wire[7]==1,"canonical integers big endian fixed width");
  auto bad=wire;bad.push_back(0);Check(!Deserialize(bad),"trailing serialization bytes rejected");
  Check(Sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA-256 empty standard vector");
  Check(Sha256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 abc standard vector");
  Check(Canonical(r).clipboard_text==Canonical(*decoded).clipboard_text,"canonical envelope stable");
  auto invalid=r;invalid.context.window_pid=999;Check(!Validate(invalid),"owner mismatch rejected");
  invalid=r;invalid.context.displays[0].scale=std::numeric_limits<double>::infinity();Check(!Validate(invalid),"nonfinite transform rejected");
  invalid=r;invalid.context.displays[0].pixels.width=99;Check(!Validate(invalid),"inconsistent frozen pixel bounds rejected");
  invalid=r;invalid.image_completed.monotonic_us=900;Check(!Validate(invalid),"invalid phase chronology rejected");
  invalid=r;invalid.crop_pixels.x=1000;Check(!Validate(invalid),"out of source crop rejected");
  invalid=r;invalid.source.bytes[20]^=1;invalid.source.sha256=Sha256(invalid.source.bytes);Check(!Validate(invalid),"rehashing malformed PNG cannot create valid record");
  invalid=r;invalid.source.bytes=CorruptAdlerPng(r.source.bytes);invalid.source.sha256=Sha256(invalid.source.bytes);Check(!invalid.source.bytes.empty()&&!Validate(invalid),"rehashing a bad PNG Adler checksum cannot create a valid record");
  invalid=r;invalid.source.bytes=InsertEmptyStoredBlock(r.source.bytes);invalid.source.sha256=Sha256(invalid.source.bytes);Check(!invalid.source.bytes.empty()&&!Validate(invalid),"an empty nonfinal stored block is not canonical");
  const auto malformed_wire=Serialize(invalid);
  const auto malformed_import=Deserialize(malformed_wire);
  Check(malformed_import&&!Validate(*malformed_import)&&
            store.Save(Canonical(*malformed_import))==SaveResult::kInvalid,
        "unversioned deserialize/import cannot upgrade a noncanonical legacy PNG");
  invalid=r;invalid.crop.bytes=EncodePng(Pixels{r.crop.width,r.crop.height,Bytes(std::size_t(r.crop.width)*r.crop.height*4,127)});
  invalid.crop.sha256=Sha256(invalid.crop.bytes);Check(!Validate(invalid),"well formed but unrelated crop pixels rejected");
  auto noncanonical=Canonical(r);noncanonical.clipboard_text.push_back(' ');Check(store.Save(noncanonical)==SaveResult::kInvalid,"noncanonical clipboard representation rejected");
#ifdef __APPLE__
  auto image=r.source.bytes;CFDataRef data=CFDataCreate(kCFAllocatorDefault,image.data(),image.size());
  CGImageSourceRef source=CGImageSourceCreateWithData(data,nullptr);Check(source!=nullptr,"native ImageIO recognizes canonical PNG");
  CGImageRef native=CGImageSourceCreateImageAtIndex(source,0,nullptr);
  Check(native && CGImageGetWidth(native)==100 && CGImageGetHeight(native)==100,"native ImageIO decodes canonical PNG dimensions");
  if(native)CGImageRelease(native);CFRelease(source);CFRelease(data);
#endif
}
void MultiRegionReference() {
  TempDirectory temp;
  {ReferenceStore store(temp.path);
  MarkController marks(store,[](const auto&){return true;});
  const std::string id(32,'9');
  const auto generation=Begin(marks,id);
  marks.Captured(generation,Capture());
  std::vector<std::vector<DisplayPoint>> regions={
    {{1,CoordinateUnit::kLogicalPoints,{10,10},1},
     {1,CoordinateUnit::kLogicalPoints,{20,20},1}},
    {{1,CoordinateUnit::kLogicalPoints,{70,70},1},
     {1,CoordinateUnit::kLogicalPoints,{80,80},1}}};
  marks.Release(generation,regions,{1'000'002,1002});
  store.WaitForIdleForTesting();
  const auto ready=store.Lookup(id);
  Check(ready&&ready->state==ReferenceJobState::kReady&&ready->ready,
        "two-region session commits one ready identity");
  const auto& value=ready->ready->value;
  Check(value.schema==2&&value.path.empty()&&value.regions.size()==2&&
            value.source.width==100&&value.source.height==100&&
            value.source.encoding=="png-zlib-v1"&&
            value.crop.encoding=="png-zlib-v1"&&
            value.crop.bytes.empty()&&value.crop.sha256.empty()&&
            value.crop.pixel_hash_contract=="rgba8-sha256-v1"&&
            value.crop.pixel_sha256.size()==64,
        "schema-2 record stores explicit regions over one selected-display still");
  Check(value.regions[0].crop_pixels.x==2&&
            value.regions[1].crop_pixels.x==62&&value.crop_pixels.x==2&&
            value.crop_pixels.width==86,
        "each image-space region and their aggregate crop are deterministic");
  const auto original_pixels=Capture().pixels;
  const auto legacy_source=EncodePng(original_pixels);
  const auto rejects_source=[&](Bytes bytes,bool rehash) {
    auto invalid=value;invalid.source.bytes=std::move(bytes);
    invalid.source.encoding="png-zlib-v1";
    if(rehash)invalid.source.sha256=Sha256(invalid.source.bytes);
    return !Validate(invalid);
  };
  auto truncated=value.source.bytes;truncated.pop_back();
  auto invalid_crc=value.source.bytes;invalid_crc[20]^=1;
  auto oversized_pixels=original_pixels;oversized_pixels.height=101;
  oversized_pixels.rgba.resize(100*101*4,127);
  auto oversized=EncodePng(oversized_pixels);
  PutTestBe32(oversized,20,100);
  PutTestBe32(oversized,29,TestCrc(oversized,12,29));
  Check(rejects_source(truncated,true)&&rejects_source(invalid_crc,true)&&
            rejects_source(CorruptAdlerPng(legacy_source),true)&&
            rejects_source(AppendTrailingZlibStream(legacy_source,legacy_source),true)&&
            rejects_source(InvalidFilterPng(legacy_source),true)&&
            rejects_source(oversized,true)&&rejects_source(invalid_crc,false),
        "streaming decoder rejects truncation, CRC, Adler, trailing streams, filters, oversized inflate and SHA mismatch");
  const auto wire=Serialize(value);const auto decoded=Deserialize(wire);
  Check(decoded&&decoded->regions.size()==2&&Serialize(*decoded)==wire,
        "multi-region canonical bytes round-trip without flattening");
  const auto canonical=Canonical(value);
  Check(canonical.record.empty()&&canonical.clipboard_png.empty()&&
            canonical.clipboard_text.size()<256,
        "new region records avoid legacy record and PNG clipboard payloads");
  Check(std::filesystem::exists(temp.path/(id+".source.png"))&&
            std::filesystem::exists(temp.path/(id+".ready"))&&
            !std::filesystem::exists(temp.path/(id+".stref"))&&
            !std::filesystem::exists(temp.path/(id+".crop.png")),
        "schema-2 readiness persists one primary image plus small metadata");
  Check(marks.HitIdentity({1,CoordinateUnit::kLogicalPoints,{15,15},1},2,
                          ContextFixture().displays)==id&&
            !marks.HitIdentity({1,CoordinateUnit::kLogicalPoints,{50,50},1},2,
                               ContextFixture().displays),
        "collection hit testing finds regions but never their empty gap");
  }
  ReferenceStore restarted(temp.path);
  const auto cold=restarted.LookupMetadata(std::string(32,'9'));
  Check(cold&&cold->state==ReferenceJobState::kReady&&cold->regions.size()==2&&
            cold->metadata&&cold->metadata->regions.size()==2,
        "schema-2 ready sidecar restores both regions without loading image bytes");
  const auto cold_full=restarted.Lookup(std::string(32,'9'));
  const auto cold_crop=restarted.ReadReadyAsset(std::string(32,'9'),true);
  Check(cold_full&&cold_full->ready&&cold_full->ready->record.empty()&&cold_crop&&
            cold_full->ready->value.crop.bytes==*cold_crop&&
            cold_full->ready->value.crop.sha256.empty()&&
            cold_full->ready->value.crop.pixel_hash_contract=="rgba8-sha256-v1"&&
            cold_full->ready->value.crop.pixel_sha256==
                cold->metadata->crop.pixel_sha256&&
            Sha256(*cold_crop)!=cold->metadata->crop.pixel_sha256&&
            !std::filesystem::exists(temp.path/(std::string(32,'9')+".crop.png")),
        "cold schema-2 lookup derives codec-independent pixels and fresh PNG bytes");
  const auto annotated_pending=restarted.RequestReadyAnnotated(std::string(32,'9'));
  Check(annotated_pending.state==ReadyAssetState::kPending,
        "cold annotated full-context PNG derives asynchronously");
  restarted.WaitForIdleForTesting();
  const auto annotated=restarted.RequestReadyAnnotated(std::string(32,'9'));
  const auto annotated_pixels=annotated.bytes?DecodeTestPng(*annotated.bytes):std::nullopt;
  const auto source_pixels=cold_full&&cold_full->ready?
      DecodeTestPng(cold_full->ready->value.source.bytes):std::nullopt;
  Check(annotated.state==ReadyAssetState::kReady&&annotated.bytes&&
            annotated_pixels&&source_pixels&&
            annotated_pixels->width==100&&annotated_pixels->height==100&&
            annotated.byte_sha256==Sha256(*annotated.bytes)&&
            PixelAt(*annotated_pixels,15,85)!=PixelAt(*source_pixels,15,85)&&
            PixelAt(*annotated_pixels,75,25)!=PixelAt(*source_pixels,75,25)&&
            PixelAt(*annotated_pixels,50,50)==PixelAt(*source_pixels,50,50)&&
            PixelAt(*annotated_pixels,15,15)==PixelAt(*source_pixels,15,15),
        "all freehand regions render at source size while gap and untouched context retain source pixels");
  for(std::size_t index=0;index<2;++index) {
    const auto snapshots=restarted.MarkSnapshots(std::string(32,'9'));
    Check(snapshots.status==MarkNotesStatus::kReady&&snapshots.marks.size()==2,
          "numbered mark snapshot remains available after restart");
    const auto pending=restarted.RequestReadyMark(std::string(32,'9'),index);
    Check(pending.state==ReadyAssetState::kPending,
          "each outer gesture starts its own bounded crop derivation");
    restarted.WaitForIdleForTesting();
    const auto mark=restarted.RequestReadyMark(std::string(32,'9'),index);
    const auto image=mark.bytes?DecodeTestPng(*mark.bytes):std::nullopt;
    const auto& pos=snapshots.marks[index].delivery_bounds;
    const auto& original=cold->metadata->regions[index].crop_pixels;
    const std::size_t source_x=index==0?15:75;
    const std::size_t source_y=index==0?85:25;
    std::size_t badge_pixels=0;
    bool badge_agrees=true;
    if(image)for(unsigned y=0;y<image->height;++y)for(unsigned x=0;x<image->width;++x)
      if(PixelAt(*image,x,y)==std::array<std::uint8_t,4>{226,35,63,255}) {
        ++badge_pixels;
        badge_agrees=badge_agrees&&PixelAt(*annotated_pixels,x+std::size_t(pos.x),
            y+std::size_t(pos.y))==PixelAt(*image,x,y);
      }
    Check(mark.state==ReadyAssetState::kReady&&mark.bytes&&image&&
          image->width==pos.width&&image->height==pos.height&&
          pos.x<=original.x&&pos.y<=original.y&&
          pos.x+pos.width>=original.x+original.width&&
          pos.y+pos.height>=original.y+original.height&&
          (pos.width>original.width||pos.height>original.height)&&
          mark.byte_sha256==Sha256(*mark.bytes)&&
          PixelAt(*image,source_x-std::size_t(pos.x),
                        source_y-std::size_t(pos.y))!=
              PixelAt(*source_pixels,source_x,source_y)&&
          badge_pixels>100&&badge_agrees,
          "numbered gesture crop agrees with its expanded delivery bounds");
  }
  Check(restarted.RequestReadyMark(std::string(32,'9'),2).code=="invalid_mark",
        "out-of-range gesture cannot alias another crop");
  const auto annotated_path=temp.path/(std::string(32,'9')+".annotated.png");
  const auto annotated_digest_path=temp.path/(std::string(32,'9')+".annotated.sha256");
  Check(std::filesystem::exists(annotated_path)&&FileBytes(annotated_path)==
            *annotated.bytes&&std::filesystem::exists(annotated_digest_path)&&
            restarted.RequestReadyAnnotated(std::string(32,'9')).bytes==
                annotated.bytes,
        "annotated PNG and bound digest have one persisted deterministic identity");
  {ReferenceStore cold_annotated(temp.path);
    Check(cold_annotated.RequestReadyAnnotated(std::string(32,'9')).state==
              ReadyAssetState::kPending,"restart loads annotated sidecar asynchronously");
    cold_annotated.WaitForIdleForTesting();
    const auto recovered=cold_annotated.RequestReadyAnnotated(std::string(32,'9'));
    Check(recovered.state==ReadyAssetState::kReady&&recovered.bytes&&
              *recovered.bytes==*annotated.bytes&&
              recovered.byte_sha256==annotated.byte_sha256,
          "cold restart returns exact cached annotated PNG and digest");}
  {const auto& source=cold_full->ready->value.source.bytes;
    std::ofstream out(annotated_path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(source.data()),source.size());}
  {ReferenceStore tampered(temp.path);
    Check(tampered.RequestReadyAnnotated(std::string(32,'9')).state==
              ReadyAssetState::kPending,
          "valid same-size replacement PNG is not trusted on cold load");
    tampered.WaitForIdleForTesting();
    const auto recovered=tampered.RequestReadyAnnotated(std::string(32,'9'));
    Check(recovered.state==ReadyAssetState::kReady&&recovered.bytes&&
              *recovered.bytes==*annotated.bytes&&
              FileBytes(annotated_path)==*annotated.bytes,
          "bound digest mismatch regenerates the original annotated bytes");}
  std::filesystem::remove(annotated_digest_path);
  {ReferenceStore missing_digest(temp.path);
    Check(missing_digest.RequestReadyAnnotated(std::string(32,'9')).state==
              ReadyAssetState::kPending,
          "missing annotated digest does not bless cached PNG bytes");
    missing_digest.WaitForIdleForTesting();
    const auto recovered=missing_digest.RequestReadyAnnotated(std::string(32,'9'));
    Check(recovered.state==ReadyAssetState::kReady&&recovered.bytes&&
              *recovered.bytes==*annotated.bytes&&
              std::filesystem::exists(annotated_digest_path),
          "missing digest regenerates and republishes the bound cache");}
  const auto captured_pixels=Capture().pixels;
  const auto& crop_bounds=cold->metadata->crop_pixels;
  Pixels alternate_pixels{static_cast<std::uint32_t>(crop_bounds.width),
                          static_cast<std::uint32_t>(crop_bounds.height),{}};
  for(std::size_t row=0;row<alternate_pixels.height;++row) {
    const auto begin=captured_pixels.rgba.begin()+
        ((static_cast<std::size_t>(crop_bounds.y)+row)*captured_pixels.width+
         static_cast<std::size_t>(crop_bounds.x))*4;
    alternate_pixels.rgba.insert(alternate_pixels.rgba.end(),begin,
                                  begin+alternate_pixels.width*4);
  }
  auto alternate_encoding=cold_full->ready->value;
  alternate_encoding.crop.bytes=EncodePng(alternate_pixels);
  std::string alternate_error;
  const bool alternate_valid=Validate(alternate_encoding,&alternate_error);
  if(!alternate_valid)std::cerr<<"alternate_encoding_error="<<alternate_error<<'\n';
  Check(alternate_encoding.crop.bytes!=*cold_crop&&alternate_valid,
        "persisted RGBA identity survives a different valid zlib byte encoding");
  TempDirectory legacy_fixture;
  auto legacy_metadata=*cold->metadata;
  legacy_metadata.source.bytes=EncodePng(captured_pixels);
  legacy_metadata.source.sha256=Sha256(legacy_metadata.source.bytes);
  legacy_metadata.crop.bytes=alternate_encoding.crop.bytes;
  legacy_metadata.crop.sha256=Sha256(legacy_metadata.crop.bytes);
  legacy_metadata.crop.pixel_hash_contract.clear();
  legacy_metadata.crop.pixel_sha256.clear();
  auto new_metadata=*cold->metadata;
  new_metadata.source.bytes.clear();new_metadata.crop.bytes.clear();
  auto legacy_sidecar_metadata=legacy_metadata;
  legacy_sidecar_metadata.source.bytes.clear();
  legacy_sidecar_metadata.crop.bytes.clear();
  const auto new_metadata_wire=Serialize(new_metadata);
  const auto legacy_metadata_wire=Serialize(legacy_sidecar_metadata);
  const auto v4_sidecar=FileBytes(temp.path/(std::string(32,'9')+".ready"));
  const auto metadata_position=std::search(v4_sidecar.begin(),v4_sidecar.end(),
      new_metadata_wire.begin(),new_metadata_wire.end());
  Check(metadata_position!=v4_sidecar.end(),"v4 fixture contains canonical metadata payload");
  Bytes v3_sidecar(v4_sidecar.begin(),metadata_position);
  v3_sidecar.insert(v3_sidecar.end(),legacy_metadata_wire.begin(),
                    legacy_metadata_wire.end());
  PutTestBe32(v3_sidecar,4,3);
  PutTestBe64(v3_sidecar,8,4'000'000'000'000'000ULL);
  PutTestBe64(v3_sidecar,16,4'000'000'000'000'001ULL);
  {std::ofstream output(legacy_fixture.path/(std::string(32,'9')+".ready"),
                        std::ios::binary);
   output.write(reinterpret_cast<const char*>(v3_sidecar.data()),v3_sidecar.size());}
  {std::ofstream output(legacy_fixture.path/(std::string(32,'9')+".source.png"),
                        std::ios::binary);
   output.write(reinterpret_cast<const char*>(legacy_metadata.source.bytes.data()),
                legacy_metadata.source.bytes.size());}
  std::cout<<"legacy_v3_fixture,sidecar_sha="<<Sha256(v3_sidecar)
           <<",source_sha="<<legacy_metadata.source.sha256
           <<",crop_sha="<<legacy_metadata.crop.sha256<<'\n';
  Check(Sha256(v3_sidecar)==
            "2e0ed22700111b11b66087986519569122111edca04c8f587cde03d89c88d598"&&
        legacy_metadata.source.sha256==
            "23696abf589456df21917fb6fc75588f9cd690898b39ce37186970902c6f738b"&&
        legacy_metadata.crop.sha256==
            "7597adcc850db43f630bf5554fe5a553b7b229bee08372c58057f6b01ffb7ba3",
        "legacy v3 sidecar/source/crop fixture hashes stay frozen");
  ReferenceStore legacy_restarted(legacy_fixture.path);
  const auto legacy_index=legacy_restarted.LookupMetadata(std::string(32,'9'));
  const auto legacy_pending=legacy_restarted.RequestReadyCrop(std::string(32,'9'));
  legacy_restarted.WaitForIdleForTesting();
  const auto legacy_ready=legacy_restarted.RequestReadyCrop(std::string(32,'9'));
  Check(legacy_index&&legacy_index->state==ReferenceJobState::kReady&&
            legacy_index->metadata->source.encoding=="png-zlib-stored-v1"&&
            legacy_index->metadata->crop.encoding=="png-zlib-stored-v1"&&
            legacy_pending.state==ReadyAssetState::kPending&&
            legacy_ready.state==ReadyAssetState::kReady&&legacy_ready.bytes&&
            *legacy_ready.bytes==legacy_metadata.crop.bytes&&
            legacy_ready.byte_sha256==legacy_metadata.crop.sha256,
        "frozen v3 sidecar and assets restart with exact legacy crop reproduction");
  const auto source_path=temp.path/(std::string(32,'9')+".source.png");
  auto corrupt_source=cold_full->ready->value.source.bytes;corrupt_source[20]^=1;
  {std::ofstream out(source_path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(corrupt_source.data()),
              static_cast<std::streamsize>(corrupt_source.size()));}
  Check(!restarted.ReadReadyAsset(std::string(32,'9'),false),
        "cold schema-2 source rejects hash corruption");
  {ReferenceStore corrupt_restarted(temp.path);
    Check(!corrupt_restarted.ReadReadyAsset(std::string(32,'9'),true),
          "uncached schema-2 crop rejects source hash corruption");}
  {const auto& source=cold_full->ready->value.source.bytes;
    std::ofstream out(source_path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(source.data()),
              static_cast<std::streamsize>(source.size()));}
  Check(restarted.Delete(std::string(32,'9'))==DeleteResult::kDeleted&&
            !std::filesystem::exists(source_path)&&
            !std::filesystem::exists(temp.path/(std::string(32,'9')+".ready"))&&
            !std::filesystem::exists(annotated_path)&&
            !std::filesystem::exists(annotated_digest_path),
        "schema-2 deletion removes primary and annotated images and bound digest");
}

void AnnotatedDeletionRace() {
  TempDirectory temp;
  ReferenceStore store(temp.path);
  MarkController marks(store,[](const auto&){return true;});
  const auto id=std::string(32,'6');
  Commit(store,marks,id);
  std::mutex mutex;
  std::condition_variable condition;
  bool entered=false,release=false,identity_matches=true;
  ReferenceStoreTestHooks hooks;
  hooks.before_derive_annotated=[&](std::string_view derived_id) {
    std::unique_lock lock(mutex);
    identity_matches=identity_matches&&derived_id==id;
    entered=true;condition.notify_all();
    condition.wait(lock,[&]{return release;});
  };
  store.SetTestHooks(std::move(hooks));
  Check(store.RequestReadyAnnotated(id).state==ReadyAssetState::kPending,
        "annotated worker starts with a pending response");
  {
    std::unique_lock lock(mutex);
    Check(condition.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),
          "annotated worker reaches the held derivation boundary");
  }
  Check(store.Delete(id)==DeleteResult::kDeleted,
        "deletion tombstones an in-flight annotated reference");
  {
    std::lock_guard lock(mutex);release=true;
  }
  condition.notify_all();store.WaitForIdleForTesting();
  Check(identity_matches&&store.LookupMetadata(id)->state==ReferenceJobState::kDeleted&&
            !std::filesystem::exists(temp.path/(id+".annotated.png"))&&
            !std::filesystem::exists(temp.path/(id+".annotated.sha256"))&&
            store.RequestReadyAnnotated(id).state==ReadyAssetState::kFailed,
        "late annotated worker cannot resurrect tombstone or owned PNG");
}

void RepresentativeCompressedCodec() {
  using Clock=std::chrono::steady_clock;
  constexpr std::uint32_t width=3456,height=2234;
  const std::string id(32,'7');TempDirectory temp;
  std::map<std::string,Clock::time_point> stages;std::mutex stage_mutex;
  {
    ReferenceStore store(temp.path);ReferenceStoreTestHooks hooks;
    hooks.stage=[&](std::string_view stage,std::string_view) {
      std::lock_guard lock(stage_mutex);stages[std::string(stage)]=Clock::now();
    };
    store.SetTestHooks(std::move(hooks));
    MarkController marks(store,[](const auto&){return true;});
    const auto generation=marks.Begin(id);
    auto context=ContextFixture();context.window={0,0,1728,1117};
    context.quartz_to_appkit_top=1117;
    context.displays={Display("representative-retina",1,{0,0,1728,1117},2)};
    Check(marks.BindContext(generation,std::move(context)),
          "representative scale-2 context binds");
    CaptureResult capture;capture.requested={1'000'001,1001};
    capture.completed={1'000'003,1003};capture.pixels.width=width;
    capture.pixels.height=height;capture.pixels.rgba.resize(
        std::size_t(width)*height*4);
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
      const auto offset=(std::size_t(y)*width+x)*4;
      capture.pixels.rgba[offset]=static_cast<std::uint8_t>((x/32)%16*16);
      capture.pixels.rgba[offset+1]=static_cast<std::uint8_t>((y/32)%16*16);
      capture.pixels.rgba[offset+2]=static_cast<std::uint8_t>((x/64+y/64)%16*16);
      capture.pixels.rgba[offset+3]=255;
    }
    marks.Captured(generation,std::move(capture));
    marks.Release(generation,{
      {{1,CoordinateUnit::kLogicalPoints,{100,100},2},
       {1,CoordinateUnit::kLogicalPoints,{500,400},2}},
      {{1,CoordinateUnit::kLogicalPoints,{900,600},2},
       {1,CoordinateUnit::kLogicalPoints,{1500,1000},2}}},
      {1'000'002,1002});
    store.WaitForIdleForTesting();
    const auto ready=store.LookupMetadata(id);
    const auto source_bytes=std::filesystem::file_size(temp.path/(id+".source.png"));
    const auto raw_bytes=std::uint64_t(width)*height*4;
    const auto crop_pending=store.RequestReadyCrop(id);
    store.WaitForIdleForTesting();const auto crop_ready=store.RequestReadyCrop(id);
    rusage usage{};getrusage(RUSAGE_SELF,&usage);
    std::map<std::string,Clock::time_point> measured;
    {std::lock_guard lock(stage_mutex);measured=stages;}
    const auto micros=[&](std::string_view begin,std::string_view end) {
      return std::chrono::duration_cast<std::chrono::microseconds>(
          measured[std::string(end)]-measured[std::string(begin)]).count();
    };
    Check(ready&&ready->state==ReferenceJobState::kReady&&ready->metadata&&
              ready->metadata->source.width==width&&
              ready->metadata->source.height==height&&
              ready->metadata->source.encoding=="png-zlib-v1"&&
              ready->metadata->crop.bytes.empty()&&
              ready->metadata->crop.sha256.empty()&&
              ready->metadata->crop.pixel_sha256.size()==64&&
              ready->metadata->regions.size()==2&&source_bytes<raw_bytes/4&&
              crop_pending.state==ReadyAssetState::kPending&&
              crop_ready.state==ReadyAssetState::kReady&&crop_ready.bytes&&
              crop_ready.byte_sha256==Sha256(*crop_ready.bytes),
          "representative 3456x2234 scale-2 content compresses and derives on demand");
    std::cout<<"codec_representative,status=ready,width="<<width
      <<",height="<<height<<",scale=2,regions=2,raw_bytes="<<raw_bytes
      <<",source_png_bytes="<<source_bytes
      <<",compression_ppm="<<(source_bytes*1'000'000/raw_bytes)
      <<",encode_us="<<micros("encode_begin","encode_end")
      <<",validate_us="<<micros("validate_begin","validate_end")
      <<",persist_readback_us="<<micros("source_readback_begin","source_readback_end")
      <<",peak_rss_bytes="<<usage.ru_maxrss<<'\n';
  }
  ReferenceStore restarted(temp.path);
  const auto cold=restarted.LookupMetadata(id);
  const auto source=restarted.ReadReadyAsset(id,false);
  Check(cold&&cold->state==ReferenceJobState::kReady&&cold->metadata&&source&&
            Sha256(*source)==cold->metadata->source.sha256&&
            cold->metadata->crop.pixel_hash_contract=="rgba8-sha256-v1",
        "representative compressed source and versioned metadata survive restart");
}

void PipelineLatencyEvidence() {
  using Clock=std::chrono::steady_clock;
  struct Sample {
    long long release_url=0,queue=0,encode=0,validate=0,serialize=0;
    long long canonical=0,pack=0,write=0,readback=0,source_readback=0;
    long long metadata_serialize=0,assets=0,release_ready=0;
    std::uintmax_t record_bytes=0,source_bytes=0,crop_bytes=0,metadata_bytes=0;
  };
  TempDirectory temp;ReferenceStore store(temp.path);
  std::mutex mutex;std::map<std::string,Clock::time_point> stages;
  Clock::time_point url_at{},ready_at{};
  ReferenceStoreTestHooks hooks;
  hooks.stage=[&](std::string_view stage,std::string_view) {
    std::lock_guard lock(mutex);stages[std::string(stage)]=Clock::now();
  };
  store.SetTestHooks(std::move(hooks));
  MarkController marks(store,
    [&](std::string_view)->std::optional<std::int64_t> {
      url_at=Clock::now();return 1;
    },
    [&](std::shared_ptr<const StoredReference>,std::int64_t) {
      ready_at=Clock::now();return true;
    },[](std::function<void()> task){task();});
  std::vector<Sample> measured;
  const auto elapsed=[](Clock::time_point begin,Clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count();
  };
  for(unsigned attempt=0;attempt<25;++attempt) {
    {std::lock_guard lock(mutex);stages.clear();}url_at={};ready_at={};
    const auto id=Identifier(1000+attempt);
    const auto generation=Begin(marks,id);marks.Captured(generation,Capture());
    const auto release=Clock::now();
    marks.Release(generation,{
      {{1,CoordinateUnit::kLogicalPoints,{10,10},1},
       {1,CoordinateUnit::kLogicalPoints,{20,20},1}},
      {{1,CoordinateUnit::kLogicalPoints,{70,70},1},
       {1,CoordinateUnit::kLogicalPoints,{80,80},1}}},
      {1'000'002,1002});
    store.WaitForIdleForTesting();
    std::map<std::string,Clock::time_point> captured;
    {std::lock_guard lock(mutex);captured=stages;}
    const auto required={"capture","encode_begin","encode_end",
      "validate_begin","validate_end","serialize_begin","serialize_end",
      "metadata_serialize_begin","metadata_serialize_end",
      "source_readback_begin","source_readback_end","assets_begin","assets_end"};
    Check(std::all_of(required.begin(),required.end(),[&](const char* stage) {
            return captured.contains(stage);})&&url_at!=Clock::time_point{}&&
          ready_at!=Clock::time_point{},
          "pipeline evidence observes every deterministic stage");
    Sample sample;
    sample.release_url=elapsed(release,url_at);
    sample.queue=elapsed(release,captured["capture"]);
    sample.encode=elapsed(captured["encode_begin"],captured["encode_end"]);
    sample.validate=elapsed(captured["validate_begin"],captured["validate_end"]);
    sample.serialize=elapsed(captured["serialize_begin"],captured["serialize_end"]);
    sample.metadata_serialize=elapsed(captured["metadata_serialize_begin"],
                                      captured["metadata_serialize_end"]);
    sample.source_readback=elapsed(captured["source_readback_begin"],
                                   captured["source_readback_end"]);
    sample.assets=elapsed(captured["assets_begin"],captured["assets_end"]);
    sample.release_ready=elapsed(release,ready_at);
    sample.record_bytes=std::filesystem::exists(temp.path/(id+".stref"))?
        std::filesystem::file_size(temp.path/(id+".stref")):0;
    sample.source_bytes=std::filesystem::file_size(temp.path/(id+".source.png"));
    sample.crop_bytes=std::filesystem::exists(temp.path/(id+".crop.png"))?
        std::filesystem::file_size(temp.path/(id+".crop.png")):0;
    sample.metadata_bytes=std::filesystem::file_size(temp.path/(id+".ready"));
    Check(sample.record_bytes==0&&sample.crop_bytes==0&&
              !captured.contains("canonical_check_begin")&&
              !captured.contains("pack_begin")&&
              !captured.contains("record_write_begin")&&
              !captured.contains("record_readback_begin"),
          "schema-2 measured path omits legacy packing and duplicate assets");
    if(attempt>=5)measured.push_back(sample);
  }
  const auto percentile=[&](auto member,std::size_t rank) {
    std::vector<long long> sorted;sorted.reserve(measured.size());
    for(const auto& sample:measured)sorted.push_back(sample.*member);
    std::sort(sorted.begin(),sorted.end());return sorted[rank];
  };
  for(std::size_t index=0;index<measured.size();++index) {
    const auto& s=measured[index];
    std::cout<<"pipeline_attempt,"<<index<<",status=ready"
      <<",release_url_us="<<s.release_url<<",queue_us="<<s.queue
      <<",encode_us="<<s.encode<<",validate_us="<<s.validate
      <<",serialize_us="<<s.serialize<<",metadata_serialize_us="<<s.metadata_serialize
      <<",canonical_recheck_us="<<s.canonical
      <<",pack_us="<<s.pack<<",record_write_us="<<s.write
      <<",record_readback_us="<<s.readback<<",source_readback_us="<<s.source_readback
      <<",assets_us="<<s.assets
      <<",release_ready_us="<<s.release_ready
      <<",record_bytes="<<s.record_bytes<<",source_bytes="<<s.source_bytes
      <<",crop_bytes="<<s.crop_bytes<<",metadata_bytes="<<s.metadata_bytes<<'\n';
  }
  const std::size_t p50=(measured.size()-1)/2,p95=measured.size()*95/100-1;
  std::cout<<"pipeline_samples="<<measured.size()<<",warmups=5"
    <<",release_url_p50_us="<<percentile(&Sample::release_url,p50)
    <<",release_url_p95_us="<<percentile(&Sample::release_url,p95)
    <<",release_ready_p50_us="<<percentile(&Sample::release_ready,p50)
    <<",release_ready_p95_us="<<percentile(&Sample::release_ready,p95)
    <<",encode_p95_us="<<percentile(&Sample::encode,p95)
    <<",validate_p95_us="<<percentile(&Sample::validate,p95)
    <<",record_write_p95_us="<<percentile(&Sample::write,p95)
    <<",record_readback_p95_us="<<percentile(&Sample::readback,p95)
    <<",source_readback_p95_us="<<percentile(&Sample::source_readback,p95)<<'\n';
}

void ExplicitSubpathRoundTrip() {
  TempDirectory temp;
  ReferenceStore store(temp.path / "explicit-subpaths");
  MarkController marks(store, [](const auto&) { return true; });
  const auto id = std::string(32, '7');
  const DisplayGeometry geometry{7, {-200, -50}, {100, 80}, 2};
  const auto point = [](double x, double y) {
    return DisplayPoint{7, CoordinateUnit::kLogicalPoints, {x, y}, 2};
  };
  InteractionController controller;
  Check(controller.ShortcutKeyDown(1, point(50, 40)) == StartResult::kStarted,
        "QA boundary sequence starts");
  controller.SetSelectedDisplayGeometry(geometry);
  Check(controller.PointerDown(point(50, 40)), "QA boundary sequence begins");
  Check(controller.PointerMoved(point(120, 40)), "QA boundary sequence exits");
  Check(!controller.PointerMoved(point(120, 100)),
        "QA first wholly outside sample is ignored");
  Check(!controller.PointerMoved(point(50, 100)),
        "QA second wholly outside sample is ignored");
  Check(controller.PointerMoved(point(50, 70)), "QA sequence re-enters");
  Check(controller.PointerMoved(point(50, 60)), "QA sequence continues");
  Check(controller.PointerUp(point(50, 50)) && controller.ShortcutKeyUp(20),
        "QA boundary sequence completes");
  const auto produced = controller.Snapshot();
  Check(produced.subpaths.size() == 1 && produced.subpaths[0].size() == 2 &&
            produced.subpaths[0][0].size() == 2 &&
            produced.subpaths[0][1].size() == 4 &&
            !controller.HitTestFinishedMark(point(83.3333333333, 60), 0.25),
        "QA producer has no synthetic exit-to-top chord");

  auto context = ContextFixture();
  context.window = {-200, -50, 100, 80};
  context.displays = {Display("qa-origin-negative-retina", 7,
                              {-200, -50, 100, 80}, 2)};
  Observe(marks,context);
  const auto generation = marks.Begin(id);
  Check(marks.BindContext(generation, context), "QA context binds");
  auto capture = Capture();
  capture.pixels.width = 200;
  capture.pixels.height = 160;
  capture.pixels.rgba.assign(200 * 160 * 4, 127);
  for (std::size_t offset = 3; offset < capture.pixels.rgba.size(); offset += 4)
    capture.pixels.rgba[offset] = 255;
  marks.Captured(generation, std::move(capture));
  marks.Release(generation, produced.subpaths, {1'000'003, 1'003});
  store.WaitForIdleForTesting();
  const auto ready = store.Lookup(id);
  Check(ready && ready->ready && ready->ready->value.subpath_version == 1 &&
            ready->ready->value.regions.size() == 1 &&
            ready->ready->value.regions[0].subpaths.size() == 2 &&
            ready->ready->value.regions[0].subpaths[0].size() == 2 &&
            ready->ready->value.regions[0].subpaths[1].size() == 4 &&
            !marks.HitIdentity(point(83.3333333333, 60), 0.25,
                               context.displays),
        "explicit subpaths persist under one logical region");
  const auto record = Serialize(ready->ready->value);
  const auto decoded = Deserialize(record);
  Check(decoded && decoded->subpath_version == 1 &&
            decoded->regions[0].subpaths.size() == 2 &&
            decoded->regions[0].subpaths[0].size() == 2 &&
            decoded->regions[0].subpaths[1].size() == 4 && Validate(*decoded),
        "explicit subpath record round-trips and validates");
  ReferenceStore restarted(temp.path / "explicit-subpaths");
  const auto metadata = restarted.LookupMetadata(id);
  Check(metadata && metadata->metadata && metadata->metadata->subpath_version == 1 &&
            metadata->metadata->regions[0].subpaths.size() == 2 &&
            metadata->metadata->regions[0].subpaths[0].size() == 2 &&
            metadata->metadata->regions[0].subpaths[1].size() == 4,
        "restart restores explicit subpath metadata without flattening");
  Check(restarted.RequestReadyAnnotated(id).state==ReadyAssetState::kPending,
        "negative-origin Retina subpaths start background annotated derivation");
  restarted.WaitForIdleForTesting();
  const auto annotated=restarted.RequestReadyAnnotated(id);
  const auto annotated_pixels=annotated.bytes?DecodeTestPng(*annotated.bytes):std::nullopt;
  const auto original_source=restarted.ReadReadyAsset(id,false);
  const auto original_pixels=original_source?DecodeTestPng(*original_source):std::nullopt;
  Check(annotated.state==ReadyAssetState::kReady&&annotated_pixels&&
            original_pixels&&annotated_pixels->width==200&&
            annotated_pixels->height==160&&
            PixelAt(*annotated_pixels,100,80)!=PixelAt(*original_pixels,100,80)&&
            PixelAt(*annotated_pixels,100,40)!=PixelAt(*original_pixels,100,40)&&
            // The original (100,70) probe lies on the new legitimate number
            // leader. (150,40) lies on the forbidden exit-to-reentry chord,
            // outside the callout; it must retain the original capture pixel.
            PixelAt(*annotated_pixels,150,40)==PixelAt(*original_pixels,150,40)&&
            PixelAt(*annotated_pixels,166,40)==PixelAt(*original_pixels,166,40),
        "2x negative-origin mapping preserves separate freehand subpaths without a synthetic chord");
  (void)restarted.RequestReadyDisplayUnderlay(id);restarted.WaitForIdleForTesting();
  const auto underlay=restarted.RequestReadyDisplayUnderlay(id);
  const auto strokes=underlay.bytes?DecodeTestPng(*underlay.bytes):std::nullopt;
  Check(strokes&&PixelAt(*strokes,150,40)==PixelAt(*original_pixels,150,40)&&
        PixelAt(*strokes,166,40)==PixelAt(*original_pixels,166,40)&&
        PixelAt(*strokes,100,80)!=PixelAt(*original_pixels,100,80),
        "actual underlay uses negative-origin2x subpaths without synthetic chord or number leaders");
  MarkController restarted_marks(restarted, [](const auto&) { return true; });
  Observe(restarted_marks,context);
  Check(!restarted_marks.HitIdentity(point(83.3333333333, 60), 0.25,
                                     context.displays),
        "restart hit testing rejects the synthetic exit-to-top chord");
}

void CapturedSourceContext() {
  using A=SourceFieldAvailability;
  const auto context=ContextFixture();
  Reference text; text.context=context;
  for(auto [field,limit] : std::vector<std::pair<std::string Context::*,std::size_t>>{
      {&Context::app_name,256},{&Context::bundle_id,512},{&Context::window_title,1024}}) {
    auto project=[&] { auto p=ProjectSourceContext(text);
      return field==&Context::app_name?p.app_name:
          field==&Context::bundle_id?p.bundle_id:p.window_title; };
    text.context=context; text.context.*field=std::string(limit,'x');
    Check(project()==text.context.*field,"text byte boundary is preserved exactly");
    (text.context.*field)+='x';Check(!project(),"one byte over projects null");
    for(const auto& bad:std::vector<std::string>{std::string("a\0b",3),"a\nb",
        std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),
        std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),"a\x7f"}) {
      text.context.*field=bad; Check(!project(),"malformed UTF8/control projects null");
    }
    text.context.*field=std::string(limit-2,'x')+"é";
    Check(project()==text.context.*field,"multibyte byte boundary remains exact");
    (text.context.*field)+='x';Check(!project(),"multibyte one-byte-over is null");
    text.context.*field="quoted \\\" value";
    Check(project()==text.context.*field,"quotes and backslashes remain unmodified");
  }
  text.context=context; text.context.app_name=std::string(257,'x');
  const auto original=text.context.app_name;
  Check(ProjectSourceContext(text).app_name_availability==A::kInvalidOrOverLimit&&
        text.context.app_name==original,"display bounds never truncate the stored snapshot");
  text.context.app_name.clear();text.context.bundle_id="com.google.Chrome";
  text.context.window_title.clear();
  const auto legacy=ProjectSourceContext(text);
  Check(!legacy.app_name&&!legacy.window_title&&!legacy.page_url&&
        legacy.app_name_availability==A::kLegacyUnavailable&&
        legacy.window_title_availability==A::kNotApplicable&&
        legacy.page_url_availability==A::kLegacyUnavailable,
        "legacy Chrome absence is explicit without current context substitution");

  TempDirectory temp; const auto root=temp.path/"captured";
  ReferenceStore store(root);MarkController marks(store,[](const auto&){return true;});
  auto chrome=context;chrome.bundle_id="com.google.Chrome";
  seethis::platform::ChromePageSample sample;
  sample.availability=PageAvailability::kReady;sample.browser_pid=chrome.pid;
  sample.window_id=chrome.window_id;sample.window_bounds=chrome.window;
  sample.process_start_identity_us=700;sample.opaque_tab_id="captured-tab";
  sample.url="chrome://Settings/Case?quote=\"&slash=\\";
  auto page=*seethis::platform::MakeChromePageIdentity(sample);
  auto metadata=seethis::platform::MakeChromeCapturedPageMetadata(sample,{1'000'002,1002});
  auto g=marks.Begin(idA);Observe(marks,chrome);Check(marks.BindContext(g,chrome),"CJC context binds");
  Check(!marks.BindPageIdentity(g+1,page,metadata),"wrong capture generation cannot freeze URL");
  auto wrong_window=page;++wrong_window.window_id;
  Check(!marks.BindPageIdentity(g,wrong_window,metadata),"wrong native window cannot freeze URL");
  Check(marks.BindPageIdentity(g,page,metadata),"matching URL and identity bind together");
  marks.Release(g,std::vector<std::vector<DisplayPoint>>{Path()},{1'000'002,1002});
  auto pending=store.LookupMetadata(idA);
  auto projection=ProjectSourceContext(*pending);
  Check(pending&&pending->captured_page&&projection.page_url==sample.url&&
        projection.context_observed_utc_us==1'000'000&&
        projection.page_url_observed_utc_us==1'000'002&&!projection.image_completed_utc_us,
        "pending context/page stamps are separate and accepted time is not completion");
  marks.Captured(g,Capture());store.WaitForIdleForTesting();
  const auto ready=store.Lookup(idA);projection=ProjectSourceContext(*ready);
  Check(ready&&ready->ready&&projection.page_url==sample.url&&
        projection.image_completed_utc_us==1'000'003&&
        DecodeTestPng(ready->ready->value.source.bytes)&&
        DecodeTestPng(ready->ready->value.source.bytes)->rgba==Capture().pixels.rgba,
        "ready URL and real screenshot completion retain the original source pixels");
  auto wrong_context=ready->ready->value;++wrong_context.page_identity->window_id;
  Check(!ProjectSourceContext(wrong_context).page_url,
        "projection rejects a URL whose matching digest belongs to another native window");
  auto record=Serialize(ready->ready->value);auto decoded=Deserialize(record);
  Check(decoded&&decoded->captured_page&&ProjectSourceContext(*decoded).page_url==sample.url,
        "new optional record tail roundtrips exact nonHTTP URL case/content");
  auto corrupt=record;corrupt.push_back(0);
  Check(!Deserialize(corrupt),"trailing record tail bytes are rejected");
  // Tail is version,present,status,url-present,string-length,string,stamp-present,stamp.
  corrupt=record;PutTestBe64(corrupt,record.size()-sample.url.size()-40,99);
  Check(!Deserialize(corrupt),"unknown metadata availability is rejected by exact tail parser");
  const auto malformed_root=temp.path/"malformed-v7";
  std::filesystem::create_directory(malformed_root);
  auto malformed_sidecar=FileBytes(root/(idA+".ready"));
  malformed_sidecar.push_back(0);
  {std::ofstream output(malformed_root/(idA+".ready"),std::ios::binary);
    output.write(reinterpret_cast<const char*>(malformed_sidecar.data()),malformed_sidecar.size());}
  std::filesystem::copy_file(root/(idA+".source.png"),malformed_root/(idA+".source.png"));
  {ReferenceStore malformed_store(malformed_root);const auto bad=malformed_store.LookupMetadata(idA);
    Check(bad&&bad->state==ReferenceJobState::kFailed&&!ProjectSourceContext(*bad).page_url&&
          !ProjectSourceContext(*bad).image_completed_utc_us,
          "malformed v7 sidecar never publishes partial URL or completed image time");}
  sample.url="https://example.test/navigated";
  const auto navigated=seethis::platform::MakeChromePageIdentity(sample);
  marks.SetPageObservation({PageAvailability::kReady,99,Now(),navigated,
      seethis::platform::MakeChromeCapturedPageMetadata(sample,Now())});
  Check(ProjectSourceContext(*store.LookupMetadata(idA)).page_url==metadata.url,
        "navigation cannot rewrite historical URL or time");
  ReferenceStore restarted(root);const auto cold=restarted.LookupMetadata(idA);
  Check(cold&&cold->captured_page&&cold->metadata&&
        ProjectSourceContext(*cold).page_url==metadata.url&&
        ProjectSourceContext(*cold).image_completed_utc_us==1'000'003,
        "sidecar v7 survives restart without loading source bytes");
  const auto v7=FileBytes(root/(idA+".ready"));
  Check(v7.size()>8&&v7[7]==7,"captured-page ready sidecar uses version7");

  auto bad_metadata=metadata;bad_metadata.url="https://example.test/wrong-digest";
  g=marks.Begin(idB);Observe(marks,chrome);Check(marks.BindContext(g,chrome),"wrong digest context binds");
  Check(marks.BindPageIdentity(g,page,bad_metadata),"bad display digest never adds a capture gate");
  marks.Release(g,std::vector<std::vector<DisplayPoint>>{Path()},{1'000'002,1002});
  marks.Captured(g,Capture());store.WaitForIdleForTesting();
  const auto mismatch=ProjectSourceContext(*store.LookupMetadata(idB));
  Check(store.LookupMetadata(idB)->state==ReferenceJobState::kReady&&!mismatch.page_url&&
        !mismatch.page_url_observed_utc_us&&mismatch.page_url_availability==A::kInvalidOrOverLimit,
        "wrong digest omits only display URL metadata");
  for(const auto& url:std::vector<std::string>{std::string(16384,'x'),
      std::string(16385,'x'),std::string(16382,'x')+"é",std::string("x\0y",3),
      "https://example.test/\ncontrol",std::string("\xff",1)}) {
    sample.url=url;auto value=seethis::platform::MakeChromeCapturedPageMetadata(sample,{5,6});
    const bool expected=url.size()==16384;
    Check((value.url==url)==expected&&
          value.availability==(expected?A::kCaptured:A::kInvalidOrOverLimit),
          "URL bounds reject only invalid display value while preserving valid16KiB");
    auto identity=seethis::platform::MakeChromePageIdentity(sample);
    Check(identity.has_value(),"invalid display text does not change existing identity eligibility");
  }
  sample.url=std::string(16385,'x');
  page=*seethis::platform::MakeChromePageIdentity(sample);
  const auto over_limit=seethis::platform::MakeChromeCapturedPageMetadata(sample,Now());
  g=marks.Begin(idD);Observe(marks,chrome);Check(marks.BindContext(g,chrome),"overlimit capture context binds");
  Check(marks.BindPageIdentity(g,page,over_limit),"overlimit URL preserves existing capture identity gate");
  marks.Release(g,std::vector<std::vector<DisplayPoint>>{Path()},{1'000'002,1002});
  marks.Captured(g,Capture());store.WaitForIdleForTesting();
  {ReferenceStore invalid_restart(root);const auto job=invalid_restart.LookupMetadata(idD);
    Check(job&&job->state==ReferenceJobState::kReady&&!ProjectSourceContext(*job).page_url&&
          ProjectSourceContext(*job).page_url_availability==A::kInvalidOrOverLimit,
          "invalid display status survives ready-sidecar v7 restart without its URL");}
  // Restore the matching captured identity for pending restart fixtures.
  sample.url=*metadata.url;page=*seethis::platform::MakeChromePageIdentity(sample);
  // Interrupted pending metadata survives the transition to a durable failure.
  const auto interrupted_root=temp.path/"interrupted";
  {ReferenceStore pending_store(temp.path/"pending-live");auto value=PendingReference(idC);
    value.mark_id=idC;value.schema=2;value.path.clear();value.regions={{Path(),{},{},{}}};
    value.context=chrome;value.page_identity=page;value.captured_page=metadata;
    pending_store.AcceptPending(value,1,2);pending_store.WaitForIdleForTesting();
    std::filesystem::create_directory(interrupted_root);
    std::filesystem::copy_file(temp.path/"pending-live"/(idC+".pending"),
                               interrupted_root/(idC+".pending"));}
  {ReferenceStore interrupted(interrupted_root);auto job=interrupted.LookupMetadata(idC);
    Check(job&&job->state==ReferenceJobState::kFailed&&job->code=="interrupted"&&
          ProjectSourceContext(*job).page_url==metadata.url&&
          !ProjectSourceContext(*job).image_completed_utc_us,
          "interrupted pending restart preserves URL with no invented image time");}
  {ReferenceStore failed_again(interrupted_root);auto job=failed_again.LookupMetadata(idC);
    Check(job&&ProjectSourceContext(*job).page_url==metadata.url&&
          !ProjectSourceContext(*job).image_completed_utc_us,
          "failed metadata tail survives a second restart honestly");}
}

void SourceContextLegacySidecars() {
  using A=SourceFieldAvailability;
  TempDirectory temp;ReferenceStore writer(temp.path/"source");
  MarkController marks(writer,[](const auto&){return true;});Commit(writer,marks);
  const auto original=writer.Lookup(idA);auto metadata=*original->metadata;
  const auto wire=Serialize(metadata);
  const auto v5=FileBytes(temp.path/"source"/(idA+".ready"));
  const auto position=std::search(v5.begin(),v5.end(),wire.begin(),wire.end());
  Check(position!=v5.end(),"legacy fixture finds exact existing metadata encoding");
  const auto begin=static_cast<std::size_t>(position-v5.begin()),end=begin+wire.size();
  for(unsigned version=2;version<=5;++version) {
    auto bytes=v5;
    if(version==4)bytes.erase(bytes.begin()+end,bytes.begin()+end+8);
    if(version<=3)bytes.resize(end);
    if(version==2)bytes.erase(bytes.begin()+begin-8,bytes.begin()+begin);
    PutTestBe64(bytes,0,version);
    const auto root=temp.path/std::to_string(version);std::filesystem::create_directory(root);
    for(const auto& suffix:{".stref",".source.png",".crop.png"})
      std::filesystem::copy_file(temp.path/"source"/(idA+suffix),root/(idA+suffix));
    {std::ofstream output(root/(idA+".ready"),std::ios::binary);
      output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    ReferenceStore old(root);auto job=old.LookupMetadata(idA);
    Check(job&&job->state==ReferenceJobState::kReady&&job->metadata&&
          !job->captured_page&&!job->metadata->captured_page&&
          ProjectSourceContext(*job).page_url_availability==A::kNotApplicable&&
          ProjectSourceContext(*job).image_completed_utc_us==1'000'003,
          "v2-v5 sidecars read original metadata without fabricated URL history");
    Check(FileBytes(root/(idA+".ready"))==bytes,
          "legacy metadata reads do not destructively rewrite sidecars");
  }
}

#ifdef __APPLE__
void ChromeProductionAdapter() {
  auto pump=[](double seconds) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);
  };
  seethis::platform::ChromePageSample sample;
  sample.availability=PageAvailability::kReady;sample.browser_pid=42;
  sample.window_id=500;sample.window_bounds=ContextFixture().window;
  sample.process_start_identity_us=700;sample.opaque_tab_id="adapter-tab";
  sample.url="https://example.test/adapter";
  std::mutex mutex;int success_count=0;PageObservation success_observation;
  seethis::platform::AcquireChromePageAsync(
      [sample] { return sample; },11,
      [&](PageObservation observation) {
        std::lock_guard lock(mutex);++success_count;
        success_observation=std::move(observation);
      });
  pump(0.4);
  Check(success_count==1 && success_observation.availability==PageAvailability::kReady &&
            success_observation.generation==11 && success_observation.identity&&
            success_observation.captured_page&&success_observation.captured_page->url==sample.url&&
            success_observation.captured_page->observed_utc_us==success_observation.observed.utc_us,
        "production Chrome adapter publishes one bounded success");

  TempDirectory transaction_temp;ReferenceStore transaction_store(transaction_temp.path);
  MarkController transaction(transaction_store,[](const auto&){return true;});
  auto captured_context=ContextFixture();captured_context.bundle_id="com.google.Chrome";
  const auto capture_generation=transaction.Begin(idA);Observe(transaction,captured_context);
  Check(transaction.BindContext(capture_generation,captured_context),"provider transaction context binds");
  transaction.Release(capture_generation,std::vector<std::vector<DisplayPoint>>{Path()},{1'000'002,1002});
  transaction.Captured(capture_generation,Capture());
  seethis::platform::AcquireChromePageAsync([sample]{return sample;},capture_generation,
      [&](PageObservation observation) {
        if(observation.identity)transaction.BindPageIdentity(capture_generation,
            *observation.identity,observation.captured_page);
      });
  pump(0.4);transaction_store.WaitForIdleForTesting();
  auto provider_job=transaction_store.LookupMetadata(idA);
  Check(provider_job&&provider_job->state==ReferenceJobState::kReady&&
        ProjectSourceContext(*provider_job).page_url==sample.url,
        "actual production provider callback propagates URL into persisted transaction");

  int timeout_count=0;PageObservation timeout_observation;
  seethis::platform::AcquireChromePageAsync(
      [sample] { std::this_thread::sleep_for(std::chrono::milliseconds(450));
           return sample; },12,
      [&](PageObservation observation) {
        std::lock_guard lock(mutex);++timeout_count;
        timeout_observation=std::move(observation);
      });
  pump(0.7);
  Check(timeout_count==1 && timeout_observation.availability==PageAvailability::kTimedOut &&
            timeout_observation.generation==12&&!timeout_observation.captured_page,
        "production Chrome adapter timeout wins exactly once over a late result");

  TempDirectory temp;ReferenceStore store(temp.path/"adapter-generation");
  MarkController marks(store,[](const auto&) { return true; });
  auto slow=sample;slow.opaque_tab_id="slow";
  auto fast=sample;fast.opaque_tab_id="fast";
  seethis::platform::AcquireChromePageAsync(
      [slow] { std::this_thread::sleep_for(std::chrono::milliseconds(120));
               return slow; },20,
      [&](PageObservation observation) { marks.SetPageObservation(std::move(observation)); });
  seethis::platform::AcquireChromePageAsync(
      [fast] { return fast; },21,
      [&](PageObservation observation) { marks.SetPageObservation(std::move(observation)); });
  pump(0.5);
  Check(marks.page_observation().generation==21 && marks.page_observation().identity &&
            marks.page_observation().identity->opaque_tab_id=="fast",
        "production Chrome adapter generation handoff keeps the newest page");

  struct BlockedProvider {
    std::mutex mutex;
    std::condition_variable ready;
    bool released=false;
    std::atomic<int> entered=0,active=0,maximum=0;
    seethis::platform::ChromePageSample Acquire(seethis::platform::ChromePageSample sample) {
      const auto count=++active;++entered;
      auto previous=maximum.load();while(count>previous&&!maximum.compare_exchange_weak(previous,count)){}
      std::unique_lock lock(mutex);ready.wait(lock,[&]{return released;});--active;return sample;
    }
    void Release(){ {std::lock_guard lock(mutex);released=true;}ready.notify_all(); }
    ~BlockedProvider(){Release();}
  };
  auto blocked=std::make_shared<BlockedProvider>();
  int completed=0,rejected=0;
  for(int i=0;i<2;++i)seethis::platform::AcquireChromePageAsync(
      [blocked,sample]{return blocked->Acquire(sample);},30+i,[&](PageObservation page){
        ++completed;Check(page.availability==PageAvailability::kTimedOut&&!page.identity,
            "sustained provider timeout publishes no late ready identity");});
  pump(0.4);
  Check(blocked->entered==2&&blocked->active==2&&completed==2&&
      seethis::platform::ChromePhysicalWorkCountForTesting()==2,
      "two logical timeouts retain both physically blocked provider admissions");
  for(int i=0;i<1000;++i) {
    bool inline_completion=false;
    seethis::platform::AcquireChromePageAsync([blocked,sample]{return blocked->Acquire(sample);},40+i,
        [&](PageObservation page){inline_completion=true;++rejected;
          Check(page.availability==PageAvailability::kUnavailable&&!page.identity,
              "full provider admission rejects unavailable without identity");});
    Check(inline_completion&&blocked->entered==2&&blocked->maximum==2&&
        seethis::platform::ChromePhysicalWorkCountForTesting()==2,
        "repeated timed-out provider polling has no physical or pending queue growth");
  }
  blocked->Release();pump(0.1);
  Check(completed==2&&rejected==1000&&blocked->active==0&&
      seethis::platform::ChromePhysicalWorkCountForTesting()==0,
      "actual native return drains retained admissions without late callbacks");
  int recovered=0;
  seethis::platform::AcquireChromePageAsync([sample]{return sample;},2000,[&](PageObservation page){
    ++recovered;Check(page.availability==PageAvailability::kReady&&page.identity,
        "fresh provider recovers after physical completion");});
  pump(0.1);
  Check(recovered==1&&seethis::platform::ChromePhysicalWorkCountForTesting()==0,
      "provider recovery cancels timer and drains its physical record");
}
#endif

void TitleScopedVisibility() {
  using A=TitleAvailability;
#ifdef __APPLE__
  {
    const std::int64_t pid=42,window=500;
    CFNumberRef owner=CFNumberCreate(nullptr,kCFNumberSInt64Type,&pid);
    CFNumberRef number=CFNumberCreate(nullptr,kCFNumberSInt64Type,&window);
    CFDictionaryRef bounds=CGRectCreateDictionaryRepresentation(CGRectMake(0,0,100,100));
    const void* keys[]={kCGWindowOwnerPID,kCGWindowNumber,kCGWindowBounds,kCGWindowName};
    const void* values[]={owner,number,bounds,CFSTR("Synthetic title 世界")};
    CFDictionaryRef first=CFDictionaryCreate(nullptr,keys,values,4,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void* entries[]={first,first};
    CFArrayRef second=CFArrayCreate(nullptr,entries,1,&kCFTypeArrayCallBacks);
    auto result=seethis::platform::ObserveBoundTitleForTest(first,second,pid,window);
    Check(result.first=="Synthetic title 世界"&&result.second.version==1&&result.second.availability==A::kReady,
          "production bound-title parser returns exact UTF8 only for twice matching native window");
    CFArrayRef duplicate=CFArrayCreate(nullptr,entries,2,&kCFTypeArrayCallBacks);
    Check(seethis::platform::ObserveBoundTitleForTest(first,duplicate,pid,window).second.availability==A::kAmbiguous,
          "production title provider rejects duplicate native ID ambiguity");
    Check(seethis::platform::ObserveBoundTitleForTest(first,nullptr,pid,window).second.availability==A::kUnavailable,
          "production unavailable repeated inventory cannot confirm captured title");
    Check(seethis::platform::ObserveBoundTitleForTest(first,second,pid+1,window).second.availability==A::kInconsistent,
          "title from a different source native owner cannot bind capture");
    CFMutableDictionaryRef changed=CFDictionaryCreateMutableCopy(nullptr,0,first);
    CFDictionarySetValue(changed,kCGWindowName,CFSTR("Synthetic B"));
    const void* changed_entries[]={changed};
    CFArrayRef changed_inventory=CFArrayCreate(nullptr,changed_entries,1,&kCFTypeArrayCallBacks);
    Check(seethis::platform::ObserveBoundTitleForTest(first,changed_inventory,pid,window).second.availability==A::kInconsistent,
          "title changes between same-native reads fail closed without choosing either title");
    CFDictionarySetValue(changed,kCGWindowName,CFSTR(""));
    Check(seethis::platform::ObserveBoundTitleForTest(changed,changed_inventory,pid,window).second.availability==A::kInvalidOrOverLimit,
          "empty provider title is explicitly unavailable rather than empty-equals-empty");
    CFDictionaryRemoveValue(changed,kCGWindowName);
    Check(seethis::platform::ObserveBoundTitleForTest(changed,changed_inventory,pid,window).second.availability==A::kUnavailable,
          "missing provider title gets versioned unavailable status");
    CFRelease(changed_inventory);CFRelease(changed);CFRelease(duplicate);CFRelease(second);
    CFRelease(first);CFRelease(bounds);CFRelease(owner);CFRelease(number);
  }
#endif
  TempDirectory temp;
  ReferenceStore legacy_store(temp.path/"immutable");
  MarkController legacy(legacy_store,[](const auto&){return true;});Commit(legacy_store,legacy);
  auto original=legacy_store.Lookup(idA)->ready->value;
  const auto legacy_wire=Serialize(original);
  auto context=original.context;context.native_window={1,700,"title-session",3};
  auto current=WindowFor(context,1);
  const auto visible=[&](const Context& c,const WindowObservation& w) {
    return ReferenceVisible(c,std::nullopt,w,{});
  };
  Check(visible(context,current),"old no-tag immutable capture title participates without recapture");
  current.window_title="Synthetic session B";
  Check(!visible(context,current),"same native window different title hides old capture");
  current.window_title=context.window_title;
  Check(visible(context,current),"same native A title restores old capture");
  Check(Serialize(original)==legacy_wire,"legacy interpretation never rewrites immutable title bytes");
  auto weak=context;weak.native_window={};
  Check(visible(weak,current)&&std::string(TitleScopeCapabilityName(weak)).find("weaker native tuple")!=std::string::npos,
        "real legacy title remains supported with honestly weaker native-v0 lifetime");
  auto absent=context;absent.selection_method.clear();
  Check(!visible(absent,current)&&std::string(TitleScopeCapabilityName(absent)).find("recapture")!=std::string::npos,
        "missing historical provenance is not filled from a fresh matching title");
  absent=context;absent.window_title.clear();current.window_title.clear();
  Check(!visible(absent,current),"two missing historical/live titles never match");

  context.title={1,A::kReady};current=WindowFor(context,2);
  Check(visible(context,current),"explicit v1 ready exact title matches");
  // Same native tuple and same title cannot identify an unseen session change.
  const auto indistinguishable=current;
  Check(visible(context,indistinguishable),"identical-title sessions are irreducibly indistinguishable");
  for(auto status:{A::kUnavailable,A::kDenied,A::kStale,A::kInconsistent,
      A::kKnownTruncated,A::kAmbiguous,A::kInvalidOrOverLimit,static_cast<A>(99)}) {
    auto unavailable=current;unavailable.title.availability=status;
    Check(!visible(context,unavailable),"unavailable live status cannot match even equal title bytes");
    auto capture=context;capture.title.availability=status;
    Check(!visible(capture,current),"unavailable captured v1 never falls back to old tuple matching");
  }
  for(auto text:{std::string(),std::string("Original window "),std::string("original window"),
      std::string("Original window suffix"),std::string("\xc0\xaf"),std::string(1025,'x')}) {
    auto changed=current;changed.window_title=text;
    Check(!visible(context,changed),"empty invalid prefix case suffix or overlimit title never matches");
  }
  auto exact=context;exact.window_title=std::string(1024,'x');
  Check(visible(exact,WindowFor(exact,3)),"1024-byte boundary accepted exactly without truncation");
  exact.window_title+= "x";
  Check(!visible(exact,WindowFor(exact,4)),"1025-byte same strings remain unavailable");
  exact.window_title="Caf\xc3\xa9";auto decomposed=WindowFor(exact,5);decomposed.window_title="Cafe\xcc\x81";
  Check(!visible(exact,decomposed),"no Unicode normalization merges distinct title bytes");
  exact.window_title=std::string("A\0B",3);
  Check(!visible(exact,WindowFor(exact,6)),"embedded NUL title is unavailable");
  auto stale=current;stale.observed.monotonic_us-=2'000'001;
  Check(!visible(context,stale),"ready exact title cannot defeat stale native observation");
  auto unknown=current;unknown.title.version=99;
  Check(!visible(context,unknown),"unknown observation title policy fails closed");
  auto modern=original;modern.context=context;
  auto legacy_native=modern;legacy_native.context.title={};
  const auto prior=Serialize(legacy_native),wire=Serialize(modern);
  Check(wire.size()==prior.size()+24&&std::equal(prior.begin(),prior.end(),wire.begin()),
        "title tail5 leaves every positional Context/native-tail byte unchanged");
  auto decoded=Deserialize(wire);
  Check(decoded&&Serialize(*decoded)==wire&&visible(decoded->context,current),
        "title tail5 roundtrips exact capture bytes and policy");
  auto truncated=wire;truncated.pop_back();
  Check(!Deserialize(truncated),"structurally truncated title tail rejected, never guessed as legacy");
  for(auto policy:{std::uint64_t(0),std::uint64_t(99)}) {
    auto unsupported=wire;PutTestBe64(unsupported,unsupported.size()-16,policy);
    const auto saved=Deserialize(unsupported);
    Check(saved&&Serialize(*saved)==unsupported&&!visible(saved->context,current),
          "unsupported or malformed zero title version retains history and never downgrades");
  }
  auto unknown_status=wire;PutTestBe64(unknown_status,unknown_status.size()-8,99);
  const auto invalid_status=Deserialize(unknown_status);
  Check(invalid_status&&Serialize(*invalid_status)==unknown_status&&!visible(invalid_status->context,current),
        "unknown immutable title status is retained but cannot reveal overlay");

  const auto immutable_root=temp.path/"legacy-cold";
  {
    ReferenceStore writer(immutable_root);
    Check(writer.Save(Canonical(legacy_native))==SaveResult::kSaved,"historical no-title-tag native1 immutable record saves");
  }
  const auto immutable_before=FileBytes(immutable_root/(idA+".stref"));
  {
    ReferenceStore cold(immutable_root);cold.WaitForIdleForTesting();
    MarkController controller(cold,[](const auto&){return true;});controller.SetViewportContinuity(1,true);Observe(controller,context);
    Check(controller.marks().size()==1,"cold migrated immutable historical title reveals A without recapture");
    auto b=WindowFor(context,2);b.window_title="Cold synthetic B";controller.SetWindowObservation(b);
    Check(controller.marks().empty(),"cold immutable legacy capture hides on same-window B title");
    Observe(controller,context);
    Check(controller.marks().size()==1&&FileBytes(immutable_root/(idA+".stref"))==immutable_before,
          "cold immutable legacy A return restores eligibility and keeps exact original record bytes");
  }
  auto chrome=context;chrome.bundle_id="com.google.Chrome";chrome.window_title.clear();
  chrome.title={1,A::kDenied};
  PageIdentity page;page.browser_pid=chrome.window_pid;page.window_id=chrome.window_id;
  page.window_bounds=chrome.window;page.process_start_identity_us=700;
  page.opaque_tab_id="synthetic-tab-A";page.navigation_digest=std::string(64,'a');
  PageObservation page_now;page_now.availability=PageAvailability::kReady;
  page_now.generation=1;page_now.observed=Now();page_now.identity=page;
  auto chrome_now=WindowFor(chrome,1);chrome_now.title.availability=A::kAmbiguous;
  Check(ReferenceVisible(chrome,page,chrome_now,page_now),
        "stronger Chrome page authority works independently of nonapplicable title evidence");
  page_now.identity->opaque_tab_id="synthetic-tab-B";
  Check(!ReferenceVisible(chrome,page,chrome_now,page_now),
        "matching native/title signals cannot override a different Chrome tab identity");
  const auto root=temp.path/"ready";
  {
    ReferenceStore writer(root);MarkController controller(writer,[](const auto&){return true;});
    for(int kind=0;kind<4;++kind) {
      auto captured=context;
      if(kind==0)captured.title={}; // Actual native-v1 historical ready-sidecar v8.
      if(kind==2)captured.title={1,A::kUnavailable};
      if(kind==3)captured.native_window={}; // New v1 policy without native extension must survive.
      const auto id=Identifier(880+kind);Observe(controller,captured);
      const auto g=controller.Begin(id);Check(controller.BindContext(g,captured),"title fixture binds original capture");
      controller.Captured(g,Capture());controller.Release(g,std::vector<std::vector<DisplayPoint>>{Path()},{1'000'002,1002});writer.WaitForIdleForTesting();
      Check(writer.LookupMetadata(id)->state==ReferenceJobState::kReady,"title fixture persists history");
      Check(writer.UpdateMarkNote(id,StableMarkId(id,0),"Synthetic saved note",0).status==MarkNoteUpdateStatus::kUpdated,
            "saved note bound to immutable historical mark before restart");
    }
  }
  for(int kind=0;kind<4;++kind) {
    const auto id=Identifier(880+kind);const auto before=FileBytes(root/(id+".ready"));
    Check(before[7]==(kind==0?8:9),"legacy8 and additive title9 sidecar versions remain explicit");
    ReferenceStore cold(root);auto job=cold.LookupMetadata(id);
    Check(job&&job->metadata&&job->state==ReferenceJobState::kReady&&job->context.window_title==context.window_title&&
          FileBytes(root/(id+".ready"))==before&&cold.MarkSnapshots(id).marks[0].note=="Synthetic saved note",
          "cold ready restart retains exact capture/title history notes and original sidecar bytes");
    MarkController controller(cold,[](const auto&){return true;});controller.SetViewportContinuity(1,true);Observe(controller,context);
    const bool eligible=kind!=2;
    Check(ReferenceVisible(job->context,std::nullopt,controller.window_observation(),{})==eligible,
          "legacy and new required contexts restore while unavailable v1 stays hidden");
    auto b=WindowFor(context,2);b.window_title="Synthetic B";controller.SetWindowObservation(b);
    Check(controller.marks().empty()&&controller.inspector_jobs().size()==4,
          "cold same-native B hides all A overlays while preserving all ready history");
    Observe(controller,context);
    Check(ReferenceVisible(job->context,std::nullopt,controller.window_observation(),{})==eligible,
          "cold A return re-evaluates original frozen title without recapture");
  }
  // Mutate only synthetic sidecars. Both native-v1 legacy8 and new9 contain two
  // independent positional Contexts; disagreements must preserve readable notes.
  const auto find_text=[](const Bytes& bytes,const std::string& text,std::size_t start=0) {
    const auto pos=std::search(bytes.begin()+start,bytes.end(),text.begin(),text.end());
    if(pos==bytes.end())throw std::runtime_error("fixture text not found");
    return static_cast<std::size_t>(pos-bytes.begin());
  };
  for(int kind=0;kind<2;++kind)for(int field=0;field<13;++field) {
    const auto id=Identifier(880+kind);const auto before=FileBytes(root/(id+".ready"));auto mixed=before;
    const auto title_at=find_text(mixed,context.window_title);
    const auto selection_at=find_text(mixed,context.selection_method);
    const auto bounds_at=selection_at+context.selection_method.size()+8;
    const auto stamp_at=bounds_at+32+8;
    if(field==0)mixed[title_at]^=1;
    if(field==1)PutTestBe64(mixed,64,43); // job.pid
    if(field==2)PutTestBe64(mixed,72,43); // job.window_pid
    if(field==3)PutTestBe64(mixed,88,501); // job.window_id
    if(field==4)mixed[bounds_at]^=1;
    if(field==5)PutTestBe64(mixed,stamp_at,1'000'001);
    if(field==6)mixed[selection_at]^=1;
    if(field==7)mixed[find_text(mixed,context.exclusion_method)]^=1;
    if(field==8)mixed[find_text(mixed,"historical;context-before-overlay;")]^=1;
    if(field==9)PutTestBe64(mixed,stamp_at+8,1001);
    const auto native_size=32+context.native_window.observation_session.size();
    const auto native_start=mixed.size()-(kind==1?32:0)-2*native_size;
    if(field==10)PutTestBe64(mixed,native_start+8,701);
    if(field==11)mixed[find_text(mixed,context.window_title,title_at+context.window_title.size())]^=1;
    if(field==12)PutTestBe64(mixed,40,1'000'009); // duplicate circle timestamp
    const auto target=temp.path/("mixed-"+std::to_string(kind)+"-"+std::to_string(field));
    std::filesystem::copy(root,target,std::filesystem::copy_options::recursive);
    {std::ofstream out(target/(id+".ready"),std::ios::binary);out.write(reinterpret_cast<const char*>(mixed.data()),mixed.size());}
    ReferenceStore cold(target);const auto job=cold.LookupMetadata(id);
    const auto hydrated=cold.Lookup(id);
    Check(hydrated&&cold.LookupMetadata(id)->state==ReferenceJobState::kReady,
          "history asset demand cannot replace unavailable capture evidence or destroy historical notes");
    Check(job&&job->state==ReferenceJobState::kReady&&job->metadata&&
          !visible(cold.LookupMetadata(id)->context,WindowFor(context,10))&&
          !visible(job->metadata->context,WindowFor(context,10))&&
          cold.MarkSnapshots(id).status==MarkNotesStatus::kReady&&
          cold.MarkSnapshots(id).marks[0].note=="Synthetic saved note"&&
          FileBytes(target/(id+".ready"))==mixed,
          "mixed legacy/new duplicate title native bounds timestamps provenance hide without dropping history/notes");
  }
  // Supported sidecar framing with unknown policy/status remains inspectable.
  for(int field=0;field<3;++field) {
    const auto id=Identifier(881);auto bytes=FileBytes(root/(id+".ready"));
    for(std::size_t start:{bytes.size()-32,bytes.size()-16}) {
      PutTestBe64(bytes,start,field==0?0:field==1?99:1);
      PutTestBe64(bytes,start+8,field==2?99:0);
    }
    const auto target=temp.path/("unsupported-"+std::to_string(field));
    std::filesystem::copy(root,target,std::filesystem::copy_options::recursive);
    {std::ofstream out(target/(id+".ready"),std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    ReferenceStore cold(target);const auto job=cold.LookupMetadata(id);
    Check(job&&job->state==ReferenceJobState::kReady&&job->metadata&&
          !visible(job->context,WindowFor(context,11))&&cold.MarkSnapshots(id).marks[0].note=="Synthetic saved note",
          "unknown/zero title policy and unknown status coldload retain historical notes and stay hidden");
  }
}

void NativeWindowLifetimes() {
#ifdef __APPLE__
  {
    const std::int64_t pid=42,window=500;
    CFNumberRef owner=CFNumberCreate(nullptr,kCFNumberSInt64Type,&pid);
    CFNumberRef number=CFNumberCreate(nullptr,kCFNumberSInt64Type,&window);
    const void* keys[]={kCGWindowOwnerPID,kCGWindowNumber};
    const void* values[]={owner,number};
    CFDictionaryRef valid=CFDictionaryCreate(nullptr,keys,values,2,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void* entries[]={valid};
    CFArrayRef inventory=CFArrayCreate(nullptr,entries,1,&kCFTypeArrayCallBacks);
    Check(seethis::platform::ParseNativeWindowInventoryForTest(inventory,pid)==
          std::optional(std::vector<std::uint64_t>{500}),
          "production complete-inventory parser returns only exact owner IDs");
    Check(!seethis::platform::ParseNativeWindowInventoryForTest(nullptr,pid),
          "null inventory remains unknown and supplies no absence proof");
    const void* duplicates[]={valid,valid};
    CFArrayRef duplicate=CFArrayCreate(nullptr,duplicates,2,&kCFTypeArrayCallBacks);
    Check(!seethis::platform::ParseNativeWindowInventoryForTest(duplicate,pid),
          "duplicate inventory identifiers are ambiguous rather than disappearance proof");
    CFDictionaryRef truncated=CFDictionaryCreate(nullptr,keys,values,1,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void* bad_entries[]={truncated,CFSTR("not a window dictionary")};
    for(int i=0;i<2;++i) {
      CFArrayRef bad=CFArrayCreate(nullptr,bad_entries+i,1,&kCFTypeArrayCallBacks);
      Check(!seethis::platform::ParseNativeWindowInventoryForTest(bad,pid),
            "truncated or unparseable inventory cannot establish disappearance");CFRelease(bad);
    }
    CFRelease(truncated);CFRelease(duplicate);CFRelease(inventory);
    CFRelease(valid);CFRelease(owner);CFRelease(number);
  }
#endif

  NativeWindowLifetimeTracker tracker("observer-A");
  auto context=ContextFixture();
  tracker.Observe(context.pid,700,{500,501},1);
  context.native_window=tracker.Identity(context.pid,700,500);
  auto current=WindowFor(context,1);
  Check(WindowObservationMatchesContext(context,current),"new exact native identity is eligible");
  auto changed=context;changed.window_title="same duplicated title";
  Check(WindowObservationMatchesContext(changed,current),"titles never key native identity");
  changed.window_title.clear();
  Check(WindowObservationMatchesContext(changed,current),"missing title never removes native identity");
  current.native_window.process_start_identity_us=701;
  Check(!WindowObservationMatchesContext(context,current),"PID and window reuse after restart fails closed");
  current=WindowFor(context,2);current.native_window.process_start_identity_us=0;
  Check(!WindowObservationMatchesContext(context,current),"missing fresh process start fails closed for new record");
  for(int field=0;field<5;++field) {
    auto invalid=context;
    if(field==0)invalid.native_window.process_start_identity_us=0;
    if(field==1)invalid.native_window.process_start_identity_us=-1;
    if(field==2)invalid.native_window.observation_session.clear();
    if(field==3)invalid.native_window.incarnation=0;
    if(field==4)invalid.native_window.version=99;
    Check(!WindowObservationMatchesContext(invalid,WindowFor(context,3)),
          "new invalid identity never silently selects legacy tuple branch");
  }
  tracker.Observe(context.pid,700,{500,501},2); // B foreground, A still exists offscreen.
  Check(tracker.Identity(context.pid,700,500).incarnation==context.native_window.incarnation,
        "complete inventory preserves A while another window is selected or A is offscreen");
  tracker.Observe(context.pid,700,{},3,false);
  tracker.Observe(context.pid,700,{501},1);
  Check(tracker.Identity(context.pid,700,500).incarnation==context.native_window.incarnation,
        "unavailable/truncated and older inventories cannot invent disappearance");
  tracker.Observe(context.pid,700,{501},4);
  tracker.Observe(context.pid,700,{500,501},5);
  current=WindowFor(context,4);current.native_window=tracker.Identity(context.pid,700,500);
  Check(!WindowObservationMatchesContext(context,current),
        "actually observed same-process disappearance/recreation cannot inherit old marks");
  NativeWindowLifetimeTracker restarted("observer-B");restarted.Observe(context.pid,700,{500},1);
  current.native_window=restarted.Identity(context.pid,700,500);
  Check(WindowObservationMatchesContext(context,current),
        "SeeThis restart retains process-start and tuple compatibility without invented historical creation proof");
  auto legacy=context;legacy.native_window={};
  current.native_window={};
  Check(WindowObservationMatchesContext(legacy,current),"genuine legacy fresh tuple branch remains eligible");
  auto unbundled_legacy=legacy;unbundled_legacy.bundle_id.clear();
  auto unbundled_current=current;unbundled_current.bundle_id.clear();
  Check(WindowObservationMatchesContext(unbundled_legacy,unbundled_current),
        "genuine legacy executable-only app keeps its unchanged weaker empty-bundle tuple branch");
  auto unbundled_new=context;unbundled_new.bundle_id.clear();
  unbundled_current.native_window=context.native_window;
  Check(!WindowObservationMatchesContext(unbundled_new,unbundled_current),
        "new missing bundle identity fails closed without changing legacy compatibility");

  TempDirectory temp;ReferenceStore store(temp.path/"native");
  MarkController marks(store,[](const auto&){return true;});Commit(store,marks);
  auto original=store.Lookup(idA)->ready->value;
  const auto legacy_wire=Serialize(original);
  Check(Serialize(*Deserialize(legacy_wire))==legacy_wire,
        "legacy record roundtrip leaves old native metadata bytes unchanged");
  auto modern=original;modern.context=context;
  const auto wire=Serialize(modern);const auto decoded=Deserialize(wire);
  Check(decoded&&Serialize(*decoded)==wire&&decoded->context.native_window.process_start_identity_us==700,
        "native record-tail v4 roundtrips persisted real process identity");
  Check(Bytes(wire.begin(),wire.begin()+legacy_wire.size())==legacy_wire,
        "native identity is appended without changing positional legacy Context fields");
  auto missing=modern;missing.context.native_window.process_start_identity_us=0;
  const auto missing_roundtrip=Deserialize(Serialize(missing));
  Check(missing_roundtrip&&missing_roundtrip->context.native_window.version==1&&
        !WindowObservationMatchesContext(missing_roundtrip->context,WindowFor(context,5)),
        "new missing identity persists its version and stays hidden without deletion");
  const auto root=temp.path/"native-ready";
  {
    ReferenceStore writer(root);MarkController controller(writer,[](const auto&){return true;});
    Observe(controller,context);const auto generation=controller.Begin(idB);
    Check(controller.BindContext(generation,context),"native capture context binds");
    controller.Captured(generation,Capture());controller.Release(generation,Path(),{1'000'002,1002});
    writer.WaitForIdleForTesting();
    Check(writer.Lookup(idB)->state==ReferenceJobState::kReady,"new identity reaches ready store");
  }
  const auto sidecar=FileBytes(root/(idB+".ready"));
  Check(sidecar.size()>8&&sidecar[7]==8,"new native ready sidecar uses additive version8");
  {
    ReferenceStore writer(root);MarkController controller(writer,[](const auto&){return true;});
    auto absent=context;absent.native_window.process_start_identity_us=0;
    const auto g=controller.Begin(idC);Check(controller.BindContext(g,absent),"missing native identity is retained honestly");
    controller.Captured(g,Capture());controller.Release(g,Path(),{1'000'002,1002});writer.WaitForIdleForTesting();
    Check(writer.Lookup(idC)->state==ReferenceJobState::kReady,"missing native identity does not delete captured history");
  }
  ReferenceStore reader(root);auto cold=reader.LookupMetadata(idB);
  Check(cold&&cold->metadata&&cold->context.native_window.process_start_identity_us==700&&
        cold->metadata->context.native_window.incarnation==context.native_window.incarnation&&
        cold->metadata->source.bytes.empty()&&FileBytes(root/(idB+".ready"))==sidecar,
        "cold restart restores exact native identity without image load or sidecar rewrite");
  const auto absent=reader.LookupMetadata(idC);
  Check(absent&&absent->state==ReferenceJobState::kReady&&absent->context.native_window.version==1&&
        absent->context.native_window.process_start_identity_us==0&&
        !WindowObservationMatchesContext(absent->context,WindowFor(context,6)),
        "cold new missing identity stays new and hidden rather than downgrading to legacy");

  const auto chrome_root=temp.path/"native-cjc";
  auto chrome=context;chrome.bundle_id="com.google.Chrome";chrome.window_title.clear();
  seethis::platform::ChromePageSample sample;
  sample.availability=PageAvailability::kReady;sample.browser_pid=chrome.pid;
  sample.window_id=chrome.window_id;sample.window_bounds=chrome.window;
  sample.process_start_identity_us=700;sample.opaque_tab_id="native-tab";
  sample.url="https://example.test/captured?Case=Kept";
  const auto page=*seethis::platform::MakeChromePageIdentity(sample);
  const auto metadata=seethis::platform::MakeChromeCapturedPageMetadata(sample,{1'000'002,1002});
  {
    ReferenceStore writer(chrome_root);MarkController controller(writer,[](const auto&){return true;});
    const auto g=controller.Begin(idD);Observe(controller,chrome);
    Check(controller.BindContext(g,chrome)&&controller.BindPageIdentity(g,page,metadata),
          "new Chrome capture binds native process and independent page identity");
    controller.Captured(g,Capture());controller.Release(g,Path(),{1'000'002,1002});writer.WaitForIdleForTesting();
    const auto value=writer.Lookup(idD)->ready->value;
    const auto decoded=Deserialize(Serialize(value));
    Check(decoded&&decoded->captured_page&&decoded->page_identity&&
          decoded->captured_page->url==sample.url&&SamePageIdentity(*decoded->page_identity,page)&&
          decoded->context.native_window.process_start_identity_us==700,
          "tail4 preserves tail3 captured URL and exact page digest");
    auto inconsistent=chrome;inconsistent.native_window.process_start_identity_us++;
    Check(!PageIdentityMatchesContext(page,inconsistent),"new native/page process-start disagreement fails closed");
  }
  const auto cjc_sidecar=FileBytes(chrome_root/(idD+".ready"));
  ReferenceStore chrome_restart(chrome_root);const auto combined=chrome_restart.LookupMetadata(idD);
  Check(combined&&combined->state==ReferenceJobState::kReady&&combined->metadata&&
        combined->page_identity&&SamePageIdentity(*combined->page_identity,page)&&
        ProjectSourceContext(*combined).page_url==sample.url&&
        combined->context.native_window.process_start_identity_us==700&&
        FileBytes(chrome_root/(idD+".ready"))==cjc_sidecar,
        "sidecar8 restart keeps native identity plus CJC URL/digest without rewriting history");
}

void WindowVisibility() {
  TempDirectory temp;
  ReferenceStore store(temp.path / "window-visibility");
  int copied=0;
  MarkController marks(store,[&](const auto&) {++copied;return true;});
  Commit(store,marks);
  const auto context=ContextFixture();
  const auto visible=[&] {
    return marks.marks().size()==1 &&
        marks.HitIdentity(Path().front(),2,context.displays)==idA;
  };
  Check(visible()&&marks.RecopyJob(idA)&&copied==2,
        "exact fresh native window match gates paint, hit and copy together");

  auto mismatch=WindowFor(context,marks.window_observation().generation+1);
  mismatch.pid+=1;marks.SetWindowObservation(mismatch);
  Check(marks.marks().empty()&&!marks.HitIdentity(Path().front(),2,context.displays)&&
            !marks.RecopyJob(idA)&&marks.DeleteJob(idA)==DeleteResult::kNotFound&&
            marks.inspector_jobs().size()==1,
        "native process mismatch hides interactive paths but preserves inspector history");
  mismatch=WindowFor(context,marks.window_observation().generation+1);
  mismatch.bundle_id="test.other";marks.SetWindowObservation(mismatch);
  Check(marks.marks().empty(),"native bundle mismatch hides the mark");
  mismatch=WindowFor(context,marks.window_observation().generation+1);
  mismatch.window_id+=1;marks.SetWindowObservation(mismatch);
  Check(marks.marks().empty(),"native window identifier mismatch hides the mark");

  auto unavailable=WindowFor(context,marks.window_observation().generation+1);
  unavailable.availability=WindowAvailability::kUnavailable;
  marks.SetWindowObservation(unavailable);
  Check(marks.marks().empty(),"unavailable foreground window hides the mark");
  auto stale=Now();stale.monotonic_us-=2'000'001;
  marks.SetWindowObservation(WindowFor(
      context,marks.window_observation().generation+1,stale));
  Check(marks.marks().empty(),"stale foreground window hides the mark");
  auto future=Now();future.monotonic_us+=1'000'000;
  marks.SetWindowObservation(WindowFor(
      context,marks.window_observation().generation+1,future));
  Check(marks.marks().empty(),"future foreground window fails closed");

  auto restored=WindowFor(context,marks.window_observation().generation+1);
  marks.SetWindowObservation(restored);
  auto duplicate=restored;duplicate.pid+=1;duplicate.observed=Now();
  marks.SetWindowObservation(duplicate);
  auto older=duplicate;--older.generation;marks.SetWindowObservation(older);
  Check(visible(),"duplicate and out-of-order window observations cannot replace newer state");
  mismatch=WindowFor(context,marks.window_observation().generation+1);
  mismatch.bounds.x+=1;marks.SetWindowObservation(mismatch);
  Check(marks.marks().empty()&&!marks.HitIdentity(Path().front(),2,context.displays),"current window movement hides ink and hits");Observe(marks,context);
  Check(visible()&&marks.HitIdentity(Path().front(),2,context.displays).has_value(),
      "fresh exact original bounds restore original ink and hit identity");

  auto legacy=store.Lookup(idA)->ready->value;
  legacy.id=idB;legacy.mark_id=idB;
  legacy.context.bundle_id="com.google.Chrome";
  legacy.page_identity.reset();
  {
    ReferenceStore legacy_writer(temp.path / "legacy-chrome-no-page");
    Check(legacy_writer.Save(Canonical(legacy))==SaveResult::kSaved,
          "legacy Chrome record without page identity remains readable");
  }
  ReferenceStore legacy_store(temp.path / "legacy-chrome-no-page");
  legacy_store.WaitForIdleForTesting();
  MarkController legacy_marks(legacy_store,[](const auto&) {return true;});
  Observe(legacy_marks,legacy.context);
  Check(legacy_marks.marks().empty()&&
            !legacy_marks.HitIdentity(Path().front(),2,legacy.context.displays)&&
            !legacy_marks.RecopyJob(idB),
        "legacy Chrome record without page identity is projection-hidden but inspectable");
  Check(legacy_marks.DeleteJob(idB)==DeleteResult::kNotFound,
        "projection-hidden legacy Chrome record cannot be deleted through the overlay");
  Check(legacy_marks.inspector_jobs().size()==1&&
            legacy_marks.NeedsChromePageObservation()&&
            legacy_store.Lookup(idB)->state==ReferenceJobState::kReady,
        "legacy Chrome record remains readable and unmodified in inspector history");
}

void ChromePageVisibility() {
  const auto process_start=seethis::platform::ChromeProcessStartIdentity(
      1'700'000'000.123456);
  const auto process_start_again=seethis::platform::ChromeProcessStartIdentity(
      1'700'000'000.123456);
  const auto restarted_process=seethis::platform::ChromeProcessStartIdentity(
      1'700'000'001.123456);
  Check(process_start && process_start_again && restarted_process &&
            *process_start==*process_start_again &&
            *process_start!=*restarted_process &&
            !seethis::platform::ChromeProcessStartIdentity(0) &&
            !seethis::platform::ChromeProcessStartIdentity(
                std::numeric_limits<double>::infinity()) &&
            !seethis::platform::ChromeProcessStartIdentity(
                std::numeric_limits<double>::quiet_NaN()),
        "Chrome process-start identity is stable, restart-distinct and fail-closed");
  TempDirectory temp;
  ReferenceStore store(temp.path / "chrome-page");
  ReferenceStore missing_store(temp.path / "chrome-missing");
  ReferenceStore quick_store(temp.path / "chrome-quick");
  auto missing = PendingReference(std::string(32, '9'));
  missing.context.bundle_id = "com.google.Chrome";
  const auto missing_job = missing_store.AcceptPending(missing, 1, 2);
  Check(missing_job.state == ReferenceJobState::kFailed,
        "Chrome capture without a page identity fails closed instead of downgrading");
  MarkController quick(quick_store, [](const auto&) { return true; });
  const auto quick_generation = quick.Begin(std::string(32, 'a'));
  auto quick_context = ContextFixture(); quick_context.bundle_id = "com.google.Chrome";
  Observe(quick,quick_context);
  Check(quick.BindContext(quick_generation, quick_context),
        "quick Chrome capture context binds");
  PageIdentity quick_page;
  quick_page.provider="chrome";quick_page.provider_version=2;
  quick_page.bundle_id=quick_context.bundle_id;
  quick_page.browser_pid=quick_context.window_pid;
  quick_page.window_id=quick_context.window_id;
  quick_page.window_bounds=quick_context.window;
  quick_page.process_start_identity_us=700;quick_page.opaque_tab_id="quick-tab";
  quick_page.navigation_digest=seethis::platform::ChromeNavigationDigest(
      "https://example.test/quick");
  auto wrong_quick_page=quick_page;wrong_quick_page.window_id+=1;
  Check(!quick.BindPageIdentity(quick_generation,wrong_quick_page),
        "page identity from a different window cannot bind to a capture");
  quick.Release(quick_generation, Path(), Now());
  Check(quick.phase()==ReferencePhase::kReleasedPendingCapture&&
            quick.AwaitingPageIdentity(quick_generation)&&
            quick.NeedsChromePageObservation()&&
            !quick_store.Lookup(std::string(32,'a')),
        "released Chrome capture waits for its matching page identity");
  Check(quick.BindPageIdentity(quick_generation,quick_page)&&
            !quick.AwaitingPageIdentity(quick_generation),
        "matching late page identity binds only to the released transaction");
  quick.Captured(quick_generation,Capture());quick_store.WaitForIdleForTesting();
  Check(quick_store.Lookup(std::string(32,'a'))->state==ReferenceJobState::kReady&&
            quick.NeedsChromePageObservation(),
        "late identity preserves schema-2 Chrome capture and stored observation need");
  int copied = 0;
  MarkController marks(store, [&](const auto&) { ++copied; return true; });
  const auto id = std::string(32, '8');
  marks.SetViewportContinuity(1,true);
  auto generation = marks.Begin(id);
  auto context = ContextFixture();
  context.pid = 42; context.window_pid = 42; context.window_id = 500;
  context.bundle_id = "com.google.Chrome";
  const std::string hostile_title="https://hostile.example/title\n# designated => secret";
  context.window_title = hostile_title;
  Observe(marks,context);
  Check(marks.BindContext(generation, context), "Chrome page context binds");
  Check(marks.NeedsChromePageObservation(),
        "active Chrome capture requests page observation");
  seethis::platform::ChromeWindowCandidate window{
      context.pid, context.window_id, context.window, 1, true};
  Check(seethis::platform::CorrelateChromeWindow(context, {window}).has_value(),
        "Chrome window correlation uses PID, captured ID and bounds");
  Check(!seethis::platform::CorrelateChromeWindow(context, {window, window}),
        "ambiguous Chrome window correlation fails closed");
  seethis::platform::ChromePageSample sample;
  sample.availability = PageAvailability::kReady;
  sample.browser_pid = context.pid; sample.window_id = context.window_id;
  sample.window_bounds = context.window; sample.process_start_identity_us = 700;
  sample.opaque_tab_id = "tab-a"; sample.url = "https://example.test/a?x=1#top";
  const auto page = seethis::platform::MakeChromePageIdentity(sample);
  Check(page && marks.BindPageIdentity(generation, *page),
        "Chrome page identity is captured without retaining the URL");
  auto capture = Capture();
  marks.Captured(generation, std::move(capture));
  marks.Release(generation, std::vector<std::vector<DisplayPoint>>{Path()}, Now());
  store.WaitForIdleForTesting();
  const auto ready = store.Lookup(id);
  Check(ready && ready->ready && ready->ready->value.page_identity &&
            ready->ready->value.page_identity->navigation_digest ==
                seethis::platform::ChromeNavigationDigest(sample.url),
        "page identity persists as a digest");
  auto invalid_context_identity=ready->ready->value;
  ++invalid_context_identity.page_identity->window_id;
  Check(!Validate(invalid_context_identity),
        "persisted page identity must match its immutable capture context");
  Check(ready->ready->value.context.window_title.empty(),
        "new Chrome capture redacts the hostile window title before persistence");
  ReferenceStore restarted_store(temp.path / "chrome-page");
  const auto restarted = restarted_store.Lookup(id);
  Check(restarted && restarted->metadata && restarted->metadata->page_identity &&
            restarted->metadata->page_identity->provider_version==2 &&
            restarted->metadata->context.window_title.empty(),
        "Chrome page sidecar v6 restarts with redacted context and identity");
  Check(!restarted->captured_page&&!ProjectSourceContext(*restarted).page_url&&
        ProjectSourceContext(*restarted).page_url_availability==SourceFieldAvailability::kLegacyUnavailable,
        "v6 Chrome sidecar reports honest legacy URL unavailability");
  auto malformed=*page; malformed.process_start_identity_us=0;
  Check(!ValidPageIdentity(malformed),
        "malformed Chrome process-start identity fails validation");
  auto legacy_identity=*page; legacy_identity.provider_version=1;
  Check(ValidPageIdentity(legacy_identity) &&
            !SamePageIdentity(legacy_identity,*page),
        "legacy projected Chrome identity remains readable but cannot match v2");
  const auto encoded = Serialize(ready->ready->value);
  const auto decoded = Deserialize(encoded);
  Check(decoded && decoded->page_identity &&
            decoded->page_identity->opaque_tab_id == "tab-a" &&
            encoded.end() == std::search(encoded.begin(), encoded.end(),
              sample.url.begin(), sample.url.end()) &&
            encoded.end() == std::search(encoded.begin(), encoded.end(),
              hostile_title.begin(), hostile_title.end()),
        "page identity round trip has no raw URL or hostile Chrome title");
  auto observation = PageObservation{PageAvailability::kReady, 1, Now(), page};
  marks.SetPageObservation(observation);
  Check(marks.marks().size() == 1 && marks.HitIdentity(Path().front(), 2,
            context.displays) && marks.RecopyJob(id) && copied == 2,
        "matching Chrome page gates paint, hit and copy together");
  // Drive the same identity refresh and pointer-policy inputs used when a
  // pointer approaches, enters, and leaves the painted circle. A native paint
  // failure still needs a correlated AppKit trace; this tests the core seam.
  const auto away=DisplayPoint{1,CoordinateUnit::kLogicalPoints,{5,5},1};
  const auto near=Path().front();
  for(const auto& pointer:{away,near,near,away}) {
    Observe(marks,context);
    observation.generation+=1;observation.observed=Now();
    marks.SetPageObservation(observation);
    const auto hit=marks.HitIdentity(pointer,2,context.displays);
    const bool approaching=pointer.position.x==near.position.x;
    const auto dirty=seethis::platform::StrokeDirtyBounds(Path(),1,6);
    Check(marks.marks().size()==1 &&
              hit.has_value()==approaching &&
              seethis::platform::OverlayAcceptsPointer(
                  false,hit.has_value(),{})==approaching &&
              dirty && dirty->width>0 && dirty->height>0 &&
              seethis::platform::MarkStrokeWidth(approaching)>0,
          "fresh exact-page hover refresh keeps paint, hit, pointer policy and redraw bounds coherent");
  }
  auto duplicate_page=*page;duplicate_page.opaque_tab_id="ignored-duplicate";
  observation.identity = duplicate_page;
  const auto accepted_generation=observation.generation;
  observation.observed = Now();
  marks.SetPageObservation(observation);
  Check(marks.marks().size() == 1 &&
            SamePageIdentity(*page,*marks.page_observation().identity),
        "duplicate page generation cannot replace the accepted identity");

  auto window_mismatch=WindowFor(
      context,marks.window_observation().generation+1);
  ++window_mismatch.window_id;marks.SetWindowObservation(window_mismatch);
  Check(marks.marks().empty()&&!marks.HitIdentity(Path().front(),2,context.displays)&&
            !marks.RecopyJob(id)&&marks.DeleteJob(id)==DeleteResult::kNotFound,
        "Chrome page match cannot bypass a different foreground window");
  Observe(marks,context);
  Check(marks.marks().size()==1,
        "restoring the exact Chrome window restores a matching page reference");

  auto page_window_mismatch=*page;++page_window_mismatch.window_id;
  observation.identity=page_window_mismatch;observation.generation=accepted_generation+1;
  observation.observed=Now();marks.SetPageObservation(observation);
  Check(marks.marks().empty(),"Chrome page window identifier mismatch hides the mark");
  auto page_bounds_mismatch=*page;page_bounds_mismatch.window_bounds.x+=1;
  observation.identity=page_bounds_mismatch;observation.generation=accepted_generation+2;
  observation.observed=Now();marks.SetPageObservation(observation);
  Check(marks.marks().empty(),"Chrome page exact bounds mismatch hides the mark");
  observation.identity=page;observation.generation=accepted_generation+3;observation.observed=Now();
  marks.SetPageObservation(observation);
  Check(marks.marks().size()==1,"exact Chrome window and page restoration repaints");

  auto mismatch = *page; mismatch.opaque_tab_id = "tab-b";
  observation.identity = mismatch; observation.generation = accepted_generation+4; observation.observed = Now();
  marks.SetPageObservation(observation);
  const auto mismatch_hit=marks.HitIdentity(Path().front(), 2, context.displays);
  const auto mismatch_copy=marks.RecopyJob(id);
  const auto mismatch_delete=marks.DeleteJob(id);
  Check(marks.marks().empty() && !mismatch_hit && !mismatch_copy &&
            mismatch_delete == DeleteResult::kNotFound &&
            !marks.inspector_jobs().empty(),
        "same-window different tab fails closed while inspector history remains");

  observation.identity = page; observation.generation = accepted_generation+5; observation.observed = Now();
  marks.SetPageObservation(observation);
  Check(marks.marks().size() == 1, "returning to the original tab restores visibility");
  auto navigated = *page;
  navigated.navigation_digest = seethis::platform::ChromeNavigationDigest(
      "https://example.test/a?x=2#top");
  observation.identity = navigated; observation.generation = accepted_generation+6; observation.observed = Now();
  marks.SetPageObservation(observation);
  Check(marks.marks().empty(), "navigation digest change hides the mark");
  observation.identity = page; observation.generation = accepted_generation+7; observation.observed = Now();
  marks.SetPageObservation(observation);
  Check(marks.marks().size() == 1, "back navigation with matching identity restores the mark");
  auto restarted_identity=*page;
  restarted_identity.process_start_identity_us+=1;
  observation.identity = restarted_identity; observation.generation = accepted_generation+8;
  observation.observed = Now(); marks.SetPageObservation(observation);
  Check(marks.marks().empty(), "browser process restart hides the prior page mark");

  observation.availability = PageAvailability::kUnavailable; observation.identity.reset();
  observation.generation = accepted_generation+9; observation.observed = Now(); marks.SetPageObservation(observation);
  Check(marks.marks().empty() && !marks.HitIdentity(Path().front(), 2, context.displays) &&
            !marks.RecopyJob(id) && marks.DeleteJob(id) == DeleteResult::kNotFound,
        "synchronous foreground invalidation hides every interactive path");
  observation.availability = PageAvailability::kDenied;
  observation.generation = accepted_generation+10; observation.observed = Now(); marks.SetPageObservation(observation);
  Check(marks.marks().empty() && !marks.HitIdentity(Path().front(), 2, context.displays) &&
            !marks.RecopyJob(id) && marks.DeleteJob(id) == DeleteResult::kNotFound,
        "denied page acquisition hides every interactive path");
  observation.availability = PageAvailability::kReady; observation.identity = page;
  observation.generation = accepted_generation+9; observation.observed = Now(); marks.SetPageObservation(observation);
  Check(marks.marks().empty(), "out-of-order completion cannot repaint an old page");
  auto stale=Now(); stale.monotonic_us-=2'000'001;
  observation.availability=PageAvailability::kReady; observation.identity=page;
  observation.generation=accepted_generation+11; observation.observed=stale; marks.SetPageObservation(observation);
  Check(marks.marks().empty(), "expired Chrome page observation hides the mark");
  auto future=Now(); future.monotonic_us+=1'000'000;
  observation.generation=accepted_generation+12; observation.observed=future; marks.SetPageObservation(observation);
  Check(marks.marks().empty(), "future Chrome page observation fails closed");
}
void NumberedNotesLifecycle() {
  TempDirectory temp;
  const std::string id(32,'8');
  std::string mark_id;
  Bytes immutable_ready,immutable_source,immutable_record;
  {
    ReferenceStore store(temp.path);
    MarkController marks(store,[](const auto&){return true;});
    Commit(store,marks,id);
    const auto first=store.MarkSnapshots(id);
    Check(first.status==MarkNotesStatus::kReady&&first.revision==0&&
              first.marks.size()==1&&first.marks[0].number==1&&
              first.marks[0].slot==0&&first.marks[0].note.empty()&&
              first.marks[0].anchor.position.x==20&&
              first.marks[0].id==StableMarkId(id,0),
          "fresh ordered mark has stable slot, first-point anchor and empty note");
    mark_id=first.marks[0].id;
    immutable_ready=FileBytes(temp.path/(id+".ready"));
    immutable_source=FileBytes(temp.path/(id+".source.png"));
    immutable_record=FileBytes(temp.path/(id+".stref"));
    const auto saved=store.UpdateMarkNote(id,mark_id,"Review this\nsoon",0);
    Check(saved.status==MarkNoteUpdateStatus::kUpdated&&saved.revision==1&&
              store.MarkSnapshots(id).marks[0].note=="Review this\nsoon"&&
              store.MarkSnapshots(id).revision==1,
          "note update atomically advances snapshot-wide revision");
    Check(store.UpdateMarkNote(id,mark_id,"stale",0).status==
              MarkNoteUpdateStatus::kRevisionConflict&&
              store.UpdateMarkNote(id,std::string(32,'0'),"wrong",1).status==
              MarkNoteUpdateStatus::kInvalid&&
              store.UpdateMarkNote(id,mark_id,std::string(4097,'a'),1).status==
              MarkNoteUpdateStatus::kInvalid,
          "stale, unknown and oversized note writes fail without changing saved text");
    Check(FileBytes(temp.path/(id+".ready"))==immutable_ready&&
              FileBytes(temp.path/(id+".source.png"))==immutable_source&&
              FileBytes(temp.path/(id+".stref"))==immutable_record,
          "notes leave immutable reference and source bytes untouched");
    // A v1 notes sidecar has no per-mark identity or visibility byte.
    auto legacy=FileBytes(temp.path/(id+".notes"));
    legacy.resize(legacy.size()-mark_id.size()-1);
    legacy[std::string("seethis-mark-notes\n").size()]=1;
    {std::ofstream out(temp.path/(id+".notes"),std::ios::binary|std::ios::trunc);
      out.write(reinterpret_cast<const char*>(legacy.data()),legacy.size());}
  }
  {
    ReferenceStore store(temp.path);
    const auto restored=store.MarkSnapshots(id);
    Check(restored.status==MarkNotesStatus::kReady&&restored.revision==1&&
              restored.marks[0].id==mark_id&&
              restored.marks[0].note=="Review this\nsoon"&&
              restored.marks[0].visible,
          "v1 sidecar note survives restart with visibility defaulting to shown");
    ReferenceStoreTestHooks hooks;hooks.fail_note_after_rename=true;
    store.SetTestHooks(hooks);
    const auto uncertain=store.UpdateMarkNote(id,mark_id,"Needs follow-up",1);
    Check(uncertain.status==MarkNoteUpdateStatus::kCommitUncertain&&
              uncertain.revision==2&&
              store.MarkSnapshots(id).marks[0].note=="Needs follow-up"&&
              store.MarkSnapshots(id).revision==2,
          "post-rename uncertainty preserves draft for read-back reconciliation");
    store.SetTestHooks({});
    const auto retried=store.UpdateMarkNote(id,mark_id,"Needs follow-up",2);
    Check(retried.status==MarkNoteUpdateStatus::kUpdated&&
              retried.revision==3&&store.MarkSnapshots(id).revision==3,
          "retrying the same note after uncertainty performs a durable write");
    const auto path=temp.path/(id+".notes");
    auto bytes=FileBytes(path);
    bytes[std::string("seethis-mark-notes\n").size()]=3;
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);
      out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    Check(store.MarkSnapshots(id).status==MarkNotesStatus::kUnsupported&&
              store.UpdateMarkNote(id,mark_id,"blocked",2).status==
                  MarkNoteUpdateStatus::kUnsupported&&
              store.UpdateMarkVisibility(id,mark_id,false,2).status==
                  MarkNoteUpdateStatus::kUnsupported&&FileBytes(path)==bytes,
          "unknown sidecar version is surfaced and retained byte-for-byte");
    bytes={1,2,3};
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);
      out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    Check(store.MarkSnapshots(id).status==MarkNotesStatus::kCorrupt&&
              store.UpdateMarkNote(id,mark_id,"blocked",2).status==
                  MarkNoteUpdateStatus::kCorrupt&&
              store.UpdateMarkVisibility(id,mark_id,false,2).status==
                  MarkNoteUpdateStatus::kCorrupt&&FileBytes(path)==bytes,
          "corrupt sidecar is surfaced and retained byte-for-byte");
    Check(store.Delete(id)==DeleteResult::kDeleted&&
              !std::filesystem::exists(path)&&
              store.MarkSnapshots(id).status==MarkNotesStatus::kDeleted&&
              store.UpdateMarkNote(id,mark_id,"blocked",2).status==
                  MarkNoteUpdateStatus::kDeleted&&
              store.UpdateMarkVisibility(id,mark_id,false,2).status==
                  MarkNoteUpdateStatus::kDeleted,
          "deletion removes note sidecar and blocks resurrection");
  }
  {
    const auto root=temp.path/"pruned";
    ReferenceStore store(root);
    MarkController marks(store,[](const auto&){return true;});
    const std::string expiring_id(32,'7');
    Commit(store,marks,expiring_id);
    const auto snapshot=store.MarkSnapshots(expiring_id);
    Check(snapshot.status==MarkNotesStatus::kReady&&
              store.UpdateMarkNote(expiring_id,snapshot.marks[0].id,"ephemeral",0)
                  .status==MarkNoteUpdateStatus::kUpdated,
          "retention fixture persists a note before expiry");
    const auto accepted=store.LookupMetadata(expiring_id)->accepted_utc_us;
    ReferenceStoreTestHooks future;
    future.now=[accepted] {auto now=Now();now.utc_us=accepted+2'000'000;return now;};
    store.SetTestHooks(future);
    store.SetRetentionPolicy(1,1);store.WaitForIdleForTesting();
    Check(store.MarkSnapshots(expiring_id).status==MarkNotesStatus::kExpired&&
              store.UpdateMarkNote(expiring_id,snapshot.marks[0].id,"late",1)
                  .status==MarkNoteUpdateStatus::kExpired&&
              store.UpdateMarkVisibility(expiring_id,snapshot.marks[0].id,false,1)
                  .status==MarkNoteUpdateStatus::kExpired&&
              !std::filesystem::exists(root/(expiring_id+".notes")),
          "expiry prunes note sidecar and rejects stale editor save");
  }
}
void MarkVisibilityLifecycle() {
  TempDirectory temp;
  const std::string id(32,'6');
  std::vector<std::string> mark_ids;
  std::string image_hash;
  Bytes immutable_ready,immutable_record;
  {
    ReferenceStore store(temp.path);
    MarkController marks(store,[](const auto&){return true;});
    const auto generation=Begin(marks,id);
    marks.Captured(generation,Capture());
    std::vector<std::vector<DisplayPoint>> regions={
      {{1,CoordinateUnit::kLogicalPoints,{10,10},1},
       {1,CoordinateUnit::kLogicalPoints,{20,20},1}},
      {{1,CoordinateUnit::kLogicalPoints,{45,45},1},
       {1,CoordinateUnit::kLogicalPoints,{50,55},1},
       {1,CoordinateUnit::kLogicalPoints,{55,45},1},
       {1,CoordinateUnit::kLogicalPoints,{45,45},1}},
      {{1,CoordinateUnit::kLogicalPoints,{70,20},1},
       {1,CoordinateUnit::kLogicalPoints,{80,30},1},
       {1,CoordinateUnit::kLogicalPoints,{75,35},1}}};
    marks.Release(generation,regions,{1'000'002,1002});
    store.WaitForIdleForTesting();
    const auto initial=store.MarkSnapshots(id);
    Check(initial.status==MarkNotesStatus::kReady&&initial.marks.size()==3&&
          initial.revision==0,"line, circle and freeform marks share one snapshot");
    for(std::size_t slot=0;slot<3;++slot) {
      mark_ids.push_back(initial.marks[slot].id);
      Check(initial.marks[slot].id==StableMarkId(id,slot)&&
            initial.marks[slot].number==slot+1&&initial.marks[slot].visible,
            "initial mark identity and drawing-order number are stable");
    }
    immutable_ready=FileBytes(temp.path/(id+".ready"));
    immutable_record=FileBytes(temp.path/(id+".stref"));
    (void)store.RequestReadyMark(id,1);store.WaitForIdleForTesting();
    const auto image=store.RequestReadyMark(id,1);
    Check(image.state==ReadyAssetState::kReady&&!image.byte_sha256.empty(),
          "numbered mark image is ready before visibility change");
    image_hash=image.byte_sha256;
    Check(store.UpdateMarkNote(id,mark_ids[0],"Keep this note",0).status==
              MarkNoteUpdateStatus::kUpdated&&
          store.UpdateMarkVisibility(id,mark_ids[1],false,1).status==
              MarkNoteUpdateStatus::kUpdated,
          "hide one selected mark after saving another mark's note");
    const auto hidden=store.MarkSnapshots(id);
    Check(hidden.revision==2&&hidden.marks[0].visible&&
          !hidden.marks[1].visible&&hidden.marks[2].visible&&
          hidden.marks[0].note=="Keep this note"&&
          store.UpdateMarkNote(id,mark_ids[0],"stale editor",1).status==
              MarkNoteUpdateStatus::kRevisionConflict&&
          store.UpdateMarkVisibility(id,mark_ids[2],false,1).status==
              MarkNoteUpdateStatus::kRevisionConflict,
          "visibility changes only one mark and share note conflict revision");
    Check(store.UpdateMarkNote(id,mark_ids[2],"Later note",2).status==
              MarkNoteUpdateStatus::kUpdated&&
          !store.MarkSnapshots(id).marks[1].visible,
          "later note update preserves hidden visibility");
    ReferenceStoreTestHooks hooks;hooks.fail_note_after_rename=true;
    store.SetTestHooks(hooks);
    const auto uncertain=store.UpdateMarkVisibility(id,mark_ids[0],false,3);
    Check(uncertain.status==MarkNoteUpdateStatus::kCommitUncertain&&
          uncertain.revision==4&&!store.MarkSnapshots(id).marks[0].visible,
          "renamed visibility bytes do not imply durable success");
    store.SetTestHooks({});
    Check(store.UpdateMarkVisibility(id,mark_ids[0],false,4).status==
              MarkNoteUpdateStatus::kUpdated&&
          store.UpdateMarkVisibility(id,mark_ids[1],true,5).status==
              MarkNoteUpdateStatus::kUpdated,
          "retry durably confirms hide and a Show affects only its mark");
    Check(FileBytes(temp.path/(id+".ready"))==immutable_ready&&
          FileBytes(temp.path/(id+".stref"))==immutable_record&&
          store.RequestReadyMark(id,1).byte_sha256==image_hash,
          "visibility and notes preserve capture, trajectory and delivered image");
  }
  {
    ReferenceStore store(temp.path);
    const auto restored=store.MarkSnapshots(id);
    Check(restored.status==MarkNotesStatus::kReady&&restored.revision==6&&
          restored.marks.size()==3&&
          !restored.marks[0].visible&&restored.marks[1].visible&&
          restored.marks[2].visible&&
          restored.marks[0].note=="Keep this note"&&
          restored.marks[2].note=="Later note",
          "mixed visibility and notes survive cold restart");
    for(std::size_t slot=0;slot<3;++slot)
      Check(restored.marks[slot].id==mark_ids[slot]&&
            restored.marks[slot].number==slot+1,
            "restart does not renumber or replace stable marks");
    Check(store.UpdateMarkVisibility(id,mark_ids[0],true,6).status==
              MarkNoteUpdateStatus::kUpdated,
          "hidden mark remains addressable for Show after restart");
  }
  {
    ReferenceStore store(temp.path);
    Check(store.MarkSnapshots(id).marks[0].visible&&
          store.MarkSnapshots(id).revision==7,
          "Show persists across another restart");
    const auto path=temp.path/(id+".notes");
    auto bytes=FileBytes(path);
    const auto where=std::string(bytes.begin(),bytes.end()).find(mark_ids[1]);
    Check(where!=std::string::npos,"v2 sidecar binds each entry to stable mark ID");
    bytes[where]='x';
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);
      out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    Check(store.MarkSnapshots(id).status==MarkNotesStatus::kCorrupt&&
          store.UpdateMarkVisibility(id,mark_ids[1],false,7).status==
              MarkNoteUpdateStatus::kCorrupt&&FileBytes(path)==bytes,
          "invalid stable-mark binding is preserved without a rewrite");
    ReferenceStoreTestHooks hooks;
    hooks.remove_owned_file=[path](const std::filesystem::path& candidate) {
      if(candidate==path)return false;
      std::error_code error;
      (void)std::filesystem::remove(candidate,error);
      return !error;
    };
    store.SetTestHooks(hooks);
    Check(store.Delete(id)==DeleteResult::kCleanupFailed&&
          std::filesystem::exists(path)&&
          store.MarkSnapshots(id).status==MarkNotesStatus::kDeleted&&
          store.UpdateMarkVisibility(id,mark_ids[1],false,7).status==
              MarkNoteUpdateStatus::kDeleted,
          "a durable delete tombstone blocks visibility writes even if cleanup leaves bytes");
  }
}

// Decode the production PNG and identify whole connected badge backgrounds, then
// read every glyph cell. Pixel existence alone cannot detect an occluded numeral.
std::vector<seethis::core::Rect> NumberBadgeComponents(const Pixels& pixels) {
  std::vector<seethis::core::Rect> result;
  std::vector<bool> seen(std::size_t(pixels.width)*pixels.height);
  for(unsigned y=0;y<pixels.height;++y)for(unsigned x=0;x<pixels.width;++x) {
    const auto start=std::size_t(y)*pixels.width+x;
    if(seen[start]||PixelAt(pixels,x,y)!=
        std::array<std::uint8_t,4>{226,35,63,255})continue;
    unsigned x0=x,x1=x,y0=y,y1=y;
    std::vector<std::size_t> queue{start};seen[start]=true;
    for(std::size_t cursor=0;cursor<queue.size();++cursor) {
      const auto at=queue[cursor];const unsigned px=at%pixels.width,py=at/pixels.width;
      x0=std::min(x0,px);x1=std::max(x1,px);
      y0=std::min(y0,py);y1=std::max(y1,py);
      for(const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
        const int nx=int(px)+dx,ny=int(py)+dy;
        if(nx<0||ny<0||nx>=int(pixels.width)||ny>=int(pixels.height))continue;
        const auto next=std::size_t(ny)*pixels.width+nx;
        if(!seen[next]&&PixelAt(pixels,nx,ny)==
            std::array<std::uint8_t,4>{226,35,63,255}) {
          seen[next]=true;queue.push_back(next);
        }
      }
    }
    if(x1-x0>=9&&y1-y0>=9)
      result.push_back({double(x0),double(y0),double(x1-x0+1),double(y1-y0+1)});
  }
  return result;
}
bool BadgeReads(const Pixels& pixels,seethis::core::Rect red,unsigned number,int unit) {
  static constexpr std::array<std::array<unsigned,5>,10> digits={{
    {{7,5,5,5,7}},{{2,6,2,2,7}},{{7,1,7,4,7}},{{7,1,7,1,7}},
    {{5,5,7,1,1}},{{7,4,7,1,7}},{{7,4,7,5,7}},{{7,1,1,1,1}},
    {{7,5,7,5,7}},{{7,5,7,1,7}}}};
  const auto text=std::to_string(number);
  const int width=(int(text.size())*4-1)*unit;
  const int x=int(red.x+(red.width-width)/2),y=int(red.y+(red.height-5*unit)/2);
  if(x<0||y<0||x+width>int(pixels.width)||y+5*unit>int(pixels.height))return false;
  for(std::size_t n=0;n<text.size();++n)for(int row=0;row<5;++row)
    for(int column=0;column<3;++column)for(int yy=0;yy<unit;++yy)
      for(int xx=0;xx<unit;++xx) {
        const bool white=digits[text[n]-'0'][row]&(1<<(2-column));
        if(PixelAt(pixels,x+int(n)*4*unit+column*unit+xx,y+row*unit+yy)!=
            (white?std::array<std::uint8_t,4>{255,255,255,255}:
                   std::array<std::uint8_t,4>{226,35,63,255}))return false;
      }
  return true;
}
void NumberedRetinaReadability() {
  for(int backing:{1,2}) {
    TempDirectory temp;const auto id=Identifier(700+backing);
    ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});
    auto context=ContextFixture();context.window={0,0,1920,1080};
    context.quartz_to_appkit_top=1080;
    context.displays={Display("retina",1,context.window,backing)};
    Observe(marks,context);const auto generation=marks.Begin(id);
    Check(marks.BindContext(generation,context),"Retina fixture binds frozen display geometry");
    auto capture=Capture();capture.pixels={unsigned(1920*backing),unsigned(1080*backing),{}};
    capture.pixels.rgba.resize(std::size_t(capture.pixels.width)*capture.pixels.height*4);
    for(std::size_t at=0;at<capture.pixels.rgba.size();at+=4) {
      capture.pixels.rgba[at]=36;capture.pixels.rgba[at+1]=42;
      capture.pixels.rgba[at+2]=52;capture.pixels.rgba[at+3]=255;
    }
    marks.Captured(generation,std::move(capture));
    // First four reproduce the old top-edge overlap; the right edge and a dense
    // interior group exercise collision placement independently of inward clamp.
    const std::vector<Point2D> anchors={{0,1080},{10,1080},{20,1080},{30,1080},
      {1919,1080},{1919,1070},{1919,1060},{1919,1050},
      {900,540},{905,540},{910,540},{915,540}};
    std::vector<std::vector<DisplayPoint>> paths;
    for(std::size_t slot=0;slot<anchors.size();++slot) {
      const auto a=anchors[slot];const double direction=a.x>1800?-1:1;
      std::vector<DisplayPoint> path={{1,CoordinateUnit::kLogicalPoints,a,double(backing)},
        {1,CoordinateUnit::kLogicalPoints,{a.x+direction*35,a.y-25},double(backing)}};
      if(slot%2) {
        path.push_back({1,CoordinateUnit::kLogicalPoints,{a.x+direction*50,a.y-10},double(backing)});
        path.push_back(path.front());
      }
      paths.push_back(std::move(path));
    }
    marks.Release(generation,paths,{1'000'002,1002});store.WaitForIdleForTesting();
    const auto metadata=store.LookupMetadata(id);
    Check(metadata&&metadata->metadata&&metadata->state==ReferenceJobState::kReady,
          "Retina twelve-mark line/circle fixture is ready");
    const auto source=FileBytes(temp.path/(id+".source.png"));
    const auto ready=FileBytes(temp.path/(id+".ready"));
    (void)store.RequestReadyAnnotated(id);store.WaitForIdleForTesting();
    const auto full=store.RequestReadyAnnotated(id);
    const auto pixels=full.bytes?DecodeTestPng(*full.bytes):std::nullopt;
    Check(pixels&&pixels->width==1920*backing&&pixels->height==1080*backing,
          "numbered full output preserves natural source dimensions");
    const std::map<std::string,std::string> frozen7fa6={
      {"natural-1x-full.png","870caeba37eeaddb2322589d1b04638049a4731f7c5f5b412afe5fe636ec3f69"},
      {"natural-1x-mark-1.png","42af1d94e0f14772bf7a2c72420b0bd00ebfcb8b12d785938db186ef6fdd0377"},
      {"natural-1x-mark-10.png","5b80e86abdcf1201a2c957fda094dcf93f1ef1c79ede5bae69768430714d3106"},
      {"natural-1x-mark-11.png","19deb0f0e8b54724e81fb049f0ab8b12ffe9413a6bed305701a5c4909913588d"},
      {"natural-1x-mark-12.png","20eb156b760b83575dc75839bd18fa97337babd2f10bc7394352aca4ab893817"},
      {"natural-1x-mark-2.png","702e536b1fdbec235053785d4b8dd5898bac8f7452f39dcb114bb75776be67ac"},
      {"natural-1x-mark-3.png","09328db33b12f755ed55649b82402bfd6e956aa4c1ce6affe6612a0709a9d558"},
      {"natural-1x-mark-4.png","8b868b6c4ef40c74b483fb445bda736f987da459b6ca3029545d56de26bc1a11"},
      {"natural-1x-mark-5.png","24b859318f378ee1c87f9f57e933de437f3bdd3797f27b921c0a9853bf90c7ec"},
      {"natural-1x-mark-6.png","538bef54749103d6e0473ae00bd5db625f8c2a7acb4964a455a97fba2d682695"},
      {"natural-1x-mark-7.png","605fef68c88e7c0471da420d8a2a0b9fa751b822f68dfc048f240d55a39e35f9"},
      {"natural-1x-mark-8.png","92a83e3bccdb0b0673839bf7f237c1feffdd4555ae05f1412823cf9a65a9ee3a"},
      {"natural-1x-mark-9.png","78c3008bdcc6e3fa780daa683850066863aecc120c4b15524a3bd06a226fe04e"},
      {"natural-1x-raw.png","f9693419e617cae1493cd3a56e5a03c9d749c7d7ba113efbf0b7e5a0df032f2e"},
      {"natural-2x-full.png","b401fbe9f5ebb677beeb32a7904d2636bc99a0dca98d8edf391fa90af7b37ccb"},
      {"natural-2x-mark-1.png","3c6684776af2a09fa43bf69a848882c2fc738d8d4579951af657795006ffe0fa"},
      {"natural-2x-mark-10.png","d8aaf908654902d75689b92bbe95ee7dc8e441fd6ae5468befd26a05e4bb5ac1"},
      {"natural-2x-mark-11.png","4a4a8aff3b194e20e9e5400e83033bb016311bb060d3526ea31f354c23560840"},
      {"natural-2x-mark-12.png","c833f1a3b4379b40accf9baf362d6fedc7986a6e59e81b38f0be446dc8de4fc1"},
      {"natural-2x-mark-2.png","3e8589881417e8e58bbbec0a7f7f98b162fc11c4337e833edafb3b309bb10217"},
      {"natural-2x-mark-3.png","3f07caa70f1708fa37010c9b0109e49eca6c8a852ad13d0b742a7bf94f612b6d"},
      {"natural-2x-mark-4.png","f05e4ee8e4d30ccd25781202eb5cf05ee4cd7bdd6f928dfe074a123101cdc964"},
      {"natural-2x-mark-5.png","fa3e51e3f835d70fedf6dafc4e48463324cda7b9452e613e6ac70c3a553550e4"},
      {"natural-2x-mark-6.png","eb36977acf6d45877d0d32a744122df25806e8f2b48feae3b84f70f2e48be55e"},
      {"natural-2x-mark-7.png","c9f7b48f5eec4d9c396505036de2f97cf6ff62545ee8d8e8464410eb16be02b8"},
      {"natural-2x-mark-8.png","86cf54da4b23c7a242268a02c522683a2ad0b13f5496b605a5342e6dc4de67b8"},
      {"natural-2x-mark-9.png","781641892fceb7f574c6675998a60ee8fdce47f4e6669df345b625d3c7bb9294"},
      {"natural-2x-raw.png","ba9ee0d75cebc46f27341fc31078f941d8ac54b3877cfbc58b17cd42497232e7"}
    };
    const auto save=[&](const std::string& name,const Bytes& bytes) {
      if(const auto oracle=frozen7fa6.find(name);oracle!=frozen7fa6.end())
        Check(Sha256(bytes)==oracle->second,"raw/full/all-mark bytes match independent frozen7fa6 oracle");
      if(const char* directory=std::getenv("SEETHIS_TEST_ARTIFACT_DIR")) {
        std::filesystem::create_directories(directory);
        std::ofstream out(std::filesystem::path(directory)/name,std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
      }
    };
    save("natural-"+std::to_string(backing)+"x-full.png",*full.bytes);
    save("natural-"+std::to_string(backing)+"x-raw.png",source);
    (void)store.RequestReadyDisplayUnderlay(id);store.WaitForIdleForTesting();
    const auto underlay=store.RequestReadyDisplayUnderlay(id);
    const auto strokes=underlay.bytes?DecodeTestPng(*underlay.bytes):std::nullopt;
    Check(strokes&&strokes->width==pixels->width&&strokes->height==pixels->height&&
          underlay.byte_sha256==Sha256(*underlay.bytes)&&*underlay.bytes!=source&&
          *underlay.bytes!=*full.bytes&&NumberBadgeComponents(*strokes).empty(),
          "core underlay contains real strokes and no numbered badge raster");
    // Every source pixel is the known background or a stroke blend, never the
    // white/dark leader or badge colors. This rejects a valid numbered PNG too.
    const auto stroke_only=[](const Pixels& image) {
      for(std::size_t at=0;at<image.rgba.size();at+=4)
        if(image.rgba[at]<36||image.rgba[at]>255||image.rgba[at+1]<42||
           image.rgba[at+1]>45||image.rgba[at+2]<52||image.rgba[at+2]>85||
           image.rgba[at+3]!=255)return false;
      return true;
    };
    Check(stroke_only(*strokes)&&!stroke_only(*pixels),
          "stroke-only pixel assertion rejects numbered substitution despite valid PNG/hash");
    save("natural-"+std::to_string(backing)+"x-underlay.png",*underlay.bytes);
    if(const char* directory=std::getenv("SEETHIS_TEST_ARTIFACT_DIR")) {
      std::ofstream model(std::filesystem::path(directory)/
          ("natural-"+std::to_string(backing)+"x-underlay-model.json"));
      model<<std::setprecision(std::numeric_limits<double>::max_digits10)
           <<"{\"representation\":\"strokes-only-v1\",\"geometry_version\":\""
           <<ReferenceStore::kDisplayUnderlayGeometryVersion<<"\",\"reference_id\":\""<<id
           <<"\",\"source_sha256\":\""<<metadata->metadata->source.sha256
           <<"\",\"sha256\":\""<<underlay.byte_sha256<<"\",\"numbered_sha256\":\""
           <<full.byte_sha256<<"\",\"width\":"<<strokes->width<<",\"height\":"<<strokes->height
           <<",\"source_rgba\":[36,42,52,255],\"backing_scale\":"<<backing<<",\"marks\":[";
      for(std::size_t slot=0;slot<paths.size();++slot) {
        if(slot)model<<",";
        model<<"{\"mark_id\":\""<<StableMarkId(id,slot)<<"\",\"number\":"<<slot+1
             <<",\"source_anchor\":["<<anchors[slot].x*backing<<","<<(1080-anchors[slot].y)*backing
             <<"],\"source_path\":[";
        for(std::size_t n=0;n<paths[slot].size();++n) {
          if(n)model<<",";
          model<<"["<<paths[slot][n].position.x*backing<<","<<(1080-paths[slot][n].position.y)*backing<<"]";
        }
        model<<"]}";
      }
      model<<"]}\n";
    }
    const auto components=NumberBadgeComponents(*pixels);
    Check(components.size()==anchors.size(),
          "full PNG has twelve separate complete badge backgrounds, including adjacent edge marks");
    std::vector<seethis::core::Rect> labels(anchors.size());
    for(unsigned number=1;number<=anchors.size();++number) {
      unsigned matches=0;
      for(const auto& box:components)if(BadgeReads(*pixels,box,number,2*backing)) {
        labels[number-1]=box;++matches;
      }
      Check(matches==1,"every stable multi-digit numeral is fully readable exactly once at natural scale");
      const auto b=labels[number-1];
      Check(b.height==20*backing&&b.width<=28*backing,
            "natural badges remain modest and use ten logical-pixel glyphs at both backing scales");
      const auto a=anchors[number-1];
      const auto x=std::min(unsigned(a.x*backing),pixels->width-1);
      const auto y=std::min(unsigned((1080-a.y)*backing),pixels->height-1);
      Check(PixelAt(*pixels,x,y)==std::array<std::uint8_t,4>{45,45,45,255},
            "high contrast leader terminates at the exact original drawing-start anchor");
    }
    auto snapshots=store.MarkSnapshots(id);
    if(const char* directory=std::getenv("SEETHIS_TEST_ARTIFACT_DIR")) {
      std::ofstream manifest(std::filesystem::path(directory)/
          ("natural-"+std::to_string(backing)+"x-geometry.json"));
      manifest<<"{\"width\":"<<pixels->width<<",\"height\":"<<pixels->height
              <<",\"backing_scale\":"<<backing<<",\"glyph_height\":"<<10*backing
              <<",\"marks\":[";
      for(std::size_t slot=0;slot<anchors.size();++slot) {
        const auto b=snapshots.marks[slot].delivery_bounds,l=labels[slot];
        const auto original=metadata->metadata->regions[slot].crop_pixels;
        if(slot)manifest<<",";
        manifest<<"{\"number\":"<<slot+1<<",\"source_anchor\":["<<anchors[slot].x*backing
                <<","<<(1080-anchors[slot].y)*backing<<"],\"badge_red_bounds\":["
                <<l.x<<","<<l.y<<","<<l.width<<","<<l.height<<"],\"delivery_bounds\":["
                <<b.x<<","<<b.y<<","<<b.width<<","<<b.height<<"],\"original_crop\":["
                <<original.x<<","<<original.y<<","<<original.width<<","<<original.height<<"]}";
      }
      manifest<<"]}\n";
    }
    for(std::size_t slot=0;slot<anchors.size();++slot) {
      (void)store.RequestReadyMark(id,slot);store.WaitForIdleForTesting();
      const auto crop=store.RequestReadyMark(id,slot);
      const auto cropped=crop.bytes?DecodeTestPng(*crop.bytes):std::nullopt;
      const auto bounds=snapshots.marks[slot].delivery_bounds;
      Check(cropped&&cropped->width==bounds.width&&cropped->height==bounds.height,
            "selected crop size exactly matches its expanded delivery bounds");
      const auto label=labels[slot];
      Check(label.x>=bounds.x&&label.y>=bounds.y&&
            label.x+label.width<=bounds.x+bounds.width&&
            label.y+label.height<=bounds.y+bounds.height,
            "expanded delivery bounds contain the same full-set badge placement");
      bool same=true;
      for(unsigned y=label.y;y<label.y+label.height;++y)
        for(unsigned x=label.x;x<label.x+label.width;++x)
          same=same&&PixelAt(*pixels,x,y)==PixelAt(*cropped,x-unsigned(bounds.x),y-unsigned(bounds.y));
      Check(same,"every badge pixel agrees between full image and selected crop without relayout");
      save("natural-"+std::to_string(backing)+"x-mark-"+std::to_string(slot+1)+".png",*crop.bytes);
    }
    (void)store.RequestReadyDisplayUnderlay(id);store.WaitForIdleForTesting();
    const auto reloaded_underlay=store.RequestReadyDisplayUnderlay(id);
    Check(reloaded_underlay.bytes&&*reloaded_underlay.bytes==*underlay.bytes,
          "underlay regenerates exactly after twelve individual marks exceed the shared LRU budget");
    Check(store.UpdateMarkNote(id,snapshots.marks[9].id,"stable ten",0).status==MarkNoteUpdateStatus::kUpdated&&
          store.UpdateMarkVisibility(id,snapshots.marks[0].id,false,1).status==MarkNoteUpdateStatus::kUpdated&&
          store.UpdateMarkVisibility(id,snapshots.marks[2].id,false,2).status==MarkNoteUpdateStatus::kUpdated,
          "fixture creates sparse visible numbers without changing immutable slots");
    snapshots=store.MarkSnapshots(id);
    (void)store.RequestReadyAnnotated(id);store.WaitForIdleForTesting();
    Check(!snapshots.marks[0].visible&&!snapshots.marks[2].visible&&
          snapshots.marks[9].number==10&&snapshots.marks[9].note=="stable ten"&&
          store.RequestReadyAnnotated(id).bytes&&*store.RequestReadyAnnotated(id).bytes==*full.bytes&&
          FileBytes(temp.path/(id+".source.png"))==source&&FileBytes(temp.path/(id+".ready"))==ready,
          "notes and sparse visibility preserve raw bytes, geometry and numbered export identity");
    // A fully valid PNG plus the old matching bound digest was trusted by v1.
    // A cold start must now reject that cache and publish the new numbered asset.
    {std::ofstream out(temp.path/(id+".annotated.png"),std::ios::binary|std::ios::trunc);
      out.write(reinterpret_cast<const char*>(source.data()),source.size());}
    {std::ofstream out(temp.path/(id+".annotated.sha256"),std::ios::trunc);
      out<<"seethis-annotated-png-v1\n"<<id<<"\n"<<metadata->metadata->source.sha256<<"\n"<<Sha256(source)<<"\n";}
    {ReferenceStore cold(temp.path);(void)cold.RequestReadyAnnotated(id);cold.WaitForIdleForTesting();
      const auto regenerated=cold.RequestReadyAnnotated(id);
      Check(regenerated.bytes&&*regenerated.bytes==*full.bytes&&
            FileBytes(temp.path/(id+".annotated.png"))==*full.bytes,
            "legacy bound v1 cache is regenerated using the current collision-aware renderer");
      Check(cold.MarkSnapshots(id).marks[9].note=="stable ten"&&
            FileBytes(temp.path/(id+".source.png"))==source&&FileBytes(temp.path/(id+".ready"))==ready,
            "legacy cache migration preserves notes, identities and immutable capture metadata");}
    {ReferenceStore cold(temp.path);(void)cold.RequestReadyAnnotated(id);cold.WaitForIdleForTesting();
      Check(cold.RequestReadyAnnotated(id).bytes&&*cold.RequestReadyAnnotated(id).bytes==*full.bytes,
            "second cold restart retains exact regenerated numbered bytes");}
  }
}

// The expected samples below are independent source-scale RGBA goldens, chosen
// from a horizontal capsule with exact quarter-pixel coordinates. No test copy
// of the production segment compositor is used.
void DisplayUnderlayPixels() {
  for(int backing:{1,2}) {
    TempDirectory temp;ReferenceStore store(temp.path);
    MarkController marks(store,[](const auto&){return true;});
    auto context=ContextFixture();context.window={-40.25,-50.75,80,60};
    context.displays={Display("negative",7,context.window,backing),
                      Display("positive",8,{39.75,-50.75,80,60},backing)};
    const auto point=[&](double x,double y) {
      const double gx=context.window.x+x/backing;
      const double gy=context.window.y+context.window.height-y/backing;
      const auto& d=context.displays[gx<39.75?0:1];
      return DisplayPoint{d.id,CoordinateUnit::kLogicalPoints,
                         {gx-d.logical.x,gy-d.logical.y},double(backing)};
    };
    Observe(marks,context);const auto id=Identifier(810+backing);
    const auto generation=marks.Begin(id);
    Check(marks.BindContext(generation,context),"fractional negative multidisplay context binds");
    auto capture=Capture();capture.pixels={unsigned(80*backing),unsigned(60*backing),{}};
    capture.pixels.rgba.resize(std::size_t(capture.pixels.width)*capture.pixels.height*4);
    for(std::size_t at=0;at<capture.pixels.rgba.size();at+=4) {
      capture.pixels.rgba[at]=10;capture.pixels.rgba[at+1]=20;
      capture.pixels.rgba[at+2]=30;capture.pixels.rgba[at+3]=64;
    }
    for(const auto [x,y]:{std::pair{31U,41U},std::pair{31U,38U}})
      capture.pixels.rgba[(std::size_t(y)*capture.pixels.width+x)*4+3]=0;
    marks.Captured(generation,std::move(capture));
    const std::vector<std::vector<DisplayPoint>> paths={
      {point(20.25,40.25),point(60.75,40.25)},
      // The closed gesture starts away from its bounding-box top-left.
      {point(16.25,10.25),point(16.25,16.25),point(10.25,16.25),
       point(10.25,10.25),point(16.25,10.25)},
      {point(44,40.25),point(48,40.25)}};
    marks.Release(generation,paths,{1'000'002,1002});store.WaitForIdleForTesting();
    const auto raw=store.ReadReadyAsset(id,false);
    if(!raw) {const auto job=store.LookupMetadata(id);
      std::cerr<<"fractional fixture phase="<<int(marks.phase())<<",job="
               <<(job?job->code:"absent")<<"\n";}
    Check(raw.has_value(),"fractional fixture source ready");
    Check(store.RequestReadyDisplayUnderlay(id).state==ReadyAssetState::kPending,
          "underlay starts pending without a numbered asset");
    store.WaitForIdleForTesting();const auto underlay=store.RequestReadyDisplayUnderlay(id);
    const auto pixels=underlay.bytes?DecodeTestPng(*underlay.bytes):std::nullopt;
    Check(pixels&&underlay.byte_sha256==Sha256(*underlay.bytes),"real underlay digest and decoder agree");
    Check(PixelAt(*pixels,30,40)==std::array<std::uint8_t,4>{255,45,85,255}&&
          PixelAt(*pixels,30,39)==std::array<std::uint8_t,4>{236,43,81,207}&&
          PixelAt(*pixels,30,41)==std::array<std::uint8_t,4>{150,34,61,112}&&
          PixelAt(*pixels,30,38)==std::array<std::uint8_t,4>{10,20,30,64}&&
          PixelAt(*pixels,19,40)==std::array<std::uint8_t,4>{232,43,80,200},
          "quarter-pixel width/color/straight-alpha/round-cap samples match fixed golden RGBA");
    Check(PixelAt(*pixels,31,41)==std::array<std::uint8_t,4>{255,45,85,64}&&
          PixelAt(*pixels,31,38)==std::array<std::uint8_t,4>{10,20,30,0}&&
          PixelAt(*pixels,46,39)==std::array<std::uint8_t,4>{251,45,84,243}&&
          PixelAt(*pixels,46,41)==std::array<std::uint8_t,4>{195,39,71,148},
          "fully transparent source and ordered overlapping strokes match fixed RGBA goldens");
    Check(PixelAt(*pixels,10,10)!=std::array<std::uint8_t,4>{10,20,30,64}&&
          PixelAt(*pixels,13,13)==std::array<std::uint8_t,4>{10,20,30,64}&&
          PixelAt(*pixels,21,35)==std::array<std::uint8_t,4>{10,20,30,64},
          "circle start retains stroke while interior and prospective number-leader area remain source");
    Check(!std::filesystem::exists(temp.path/(id+".annotated.png"))&&
          !std::filesystem::exists(temp.path/(id+".annotated.sha256")),
          "underlay executes no numbered persistence path");
    auto numbered=store.RequestReadyAnnotated(id);store.WaitForIdleForTesting();
    numbered=store.RequestReadyAnnotated(id);
    Check(numbered.bytes&&*numbered.bytes!=*underlay.bytes&&
          store.ReadReadyAsset(id,false)==raw,"underlay/full/raw domains never alias");
    const auto disk=FileBytes(temp.path/(id+".annotated.png"));
    {ReferenceStore cold(temp.path);
      Check(cold.RequestReadyDisplayUnderlay(id).state==ReadyAssetState::kPending,
            "restart cannot satisfy underlay from a valid numbered disk asset");
      cold.WaitForIdleForTesting();const auto regenerated=cold.RequestReadyDisplayUnderlay(id);
      Check(regenerated.bytes&&*regenerated.bytes==*underlay.bytes&&
            FileBytes(temp.path/(id+".annotated.png"))==disk,
            "memory-only restart regenerates exact underlay without rewriting numbered disk domain");}
    {ReferenceStore metadata_only(temp.path);
      (void)metadata_only.RequestReadyDisplayUnderlay(id);
      metadata_only.WaitForIdleForTesting();
      // Intentionally NEVER retrieve the ready underlay: metadata callers may
      // disappear or choose an ordinary export without fetching a preview.
      ReadyAssetResult export_asset;
      for(unsigned poll=0;poll<12;++poll) {
        export_asset=metadata_only.RequestReadyAnnotated(id);
        if(export_asset.state!=ReadyAssetState::kPending)break;
        metadata_only.WaitForIdleForTesting();
      }
      Check(export_asset.bytes&&*export_asset.bytes==*numbered.bytes,
            "ordinary numbered client progresses within12polls with metadata underlay never fetched");
    }
    if(const char* directory=std::getenv("SEETHIS_TEST_ARTIFACT_DIR")) {
      std::filesystem::create_directories(directory);
      const auto save=[&](const std::string& name,const Bytes& bytes) {
        std::ofstream out(std::filesystem::path(directory)/name,std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
      };
      save("fractional-"+std::to_string(backing)+"x-underlay.png",*underlay.bytes);
      save("fractional-"+std::to_string(backing)+"x-raw.png",*raw);
    }
  }
}
void CommitTwelveUnderlay(ReferenceStore& store,const std::string& id) {
    MarkController marks(store,[](const auto&){return true;});
    const auto generation=Begin(marks,id);
    auto capture=Capture();
    for(std::size_t at=0;at<capture.pixels.rgba.size();at+=4)
      capture.pixels.rgba[at]+=std::uint8_t(id.back())%16;
    marks.Captured(generation,std::move(capture));
    std::vector<std::vector<DisplayPoint>> paths;
    for(unsigned slot=0;slot<12;++slot) {
      const double x=10+(slot%6)*12,y=20+(slot/6)*40;
      paths.push_back({{1,CoordinateUnit::kLogicalPoints,{x,y},1},
                       {1,CoordinateUnit::kLogicalPoints,{x+8,y+8},1}});
    }
    marks.Release(generation,paths,{1'000'002,1002});store.WaitForIdleForTesting();
}
void DisplayUnderlayMixedProgress() {
  TempDirectory temp;ReferenceStore store(temp.path);
  for(unsigned ref=0;ref<2;++ref)CommitTwelveUnderlay(store,Identifier(830+ref));
  std::mutex mutex;std::condition_variable condition;
  bool entered=false;unsigned permits=0,derivations=0;
  ReferenceStoreTestHooks hooks;
  hooks.before_derive_annotated=[&](std::string_view) {
    std::unique_lock lock(mutex);++derivations;
    entered=true;condition.notify_all();
    condition.wait(lock,[&]{return permits>0;});--permits;
  };
  store.SetTestHooks(hooks);
  (void)store.RequestReadyAnnotated(Identifier(830));
  {std::unique_lock lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(2),
      [&]{return entered;}),"mixed scheduler holds the first full derivation");}
  std::array<std::array<bool,15>,2> ready{};
  const auto poll=[&] {
    // Full metadata asks full/crop; compact metadata asks immutable per-mark
    // assets. Preview is deliberately last, the old first-miss-wins victim.
    for(unsigned ref=0;ref<2;++ref) {
      const auto id=Identifier(830+ref);
      Check(store.LookupMetadata(id)->metadata&&store.MarkSnapshots(id).marks.size()==12,
            "mixed full/compact metadata retains all immutable slots");
      const auto full=store.RequestReadyAnnotated(id),crop=store.RequestReadyCrop(id);
      ready[ref][0]=ready[ref][0]||full.state==ReadyAssetState::kReady;
      ready[ref][1]=ready[ref][1]||crop.state==ReadyAssetState::kReady;
      for(unsigned slot=0;slot<12;++slot) {
        const auto asset=store.RequestReadyMark(id,slot);
        ready[ref][slot+2]=ready[ref][slot+2]||asset.state==ReadyAssetState::kReady;
      }
      const auto underlay=store.RequestReadyDisplayUnderlay(id);
      ready[ref][14]=ready[ref][14]||underlay.state==ReadyAssetState::kReady;
    }
  };
  poll();
  {std::lock_guard lock(mutex);++permits;}condition.notify_all();
  store.WaitForIdleForTesting();
  for(unsigned round=0;round<48;++round) {
    poll();{std::lock_guard lock(mutex);++permits;}condition.notify_all();
    store.WaitForIdleForTesting();
  }
  bool all=true;for(const auto& ref:ready)for(bool done:ref)all=all&&done;
  Check(ready[0][14]&&ready[1][14],
        "bounded mixed progress: victim underlay must become ready for both references");
  if(!all||derivations>96) {
    std::cerr<<"mixed progress derivations="<<derivations<<",missing=";
    for(unsigned ref=0;ref<2;++ref)for(unsigned asset=0;asset<15;++asset)
      if(!ready[ref][asset])std::cerr<<ref<<":"<<asset<<",";
    std::cerr<<"\n";
  }
  Check(all&&derivations<=96,
        "full/crop/twelve marks/underlay each ready within48 released rounds under unchanged cache budget");
  std::cout<<"underlay_mixed_progress,rounds=48,derivations="<<derivations<<",all30_ready="<<all<<"\n";
}

void DisplayUnderlayQueueSaturation() {
  TempDirectory temp;ReferenceStore store(temp.path);
  for(unsigned ref=0;ref<4;++ref)CommitTwelveUnderlay(store,Identifier(870+ref));
  std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;
  unsigned derivations=0;
  ReferenceStoreTestHooks hooks;
  hooks.before_derive_annotated=[&](std::string_view) {
    std::unique_lock lock(mutex);++derivations;
    if(derivations==1) {entered=true;condition.notify_all();
      condition.wait(lock,[&]{return released;});}
  };
  store.SetTestHooks(hooks);(void)store.RequestReadyAnnotated(Identifier(870));
  {std::unique_lock lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(2),
      [&]{return entered;}),"saturation holds the active full derivation");}
  for(unsigned slot=0;slot<12;++slot)(void)store.RequestReadyMark(Identifier(870),slot);
  (void)store.RequestReadyDisplayUnderlay(Identifier(870));
  (void)store.RequestReadyAnnotated(Identifier(871));
  for(unsigned slot=0;slot<12;++slot)(void)store.RequestReadyMark(Identifier(871),slot);
  (void)store.RequestReadyDisplayUnderlay(Identifier(871));
  (void)store.RequestReadyAnnotated(Identifier(872));
  for(unsigned slot=0;slot<4;++slot)(void)store.RequestReadyMark(Identifier(872),slot);
  // Exactly32 valid descriptors are now abandoned. Only a new fourth identity
  // polls after release; no original queued requester helps the store drain.
  {std::lock_guard lock(mutex);released=true;}condition.notify_all();store.WaitForIdleForTesting();
  ReadyAssetResult victim;unsigned ready_round=0;
  for(unsigned round=1;round<=48;++round) {
    victim=store.RequestReadyDisplayUnderlay(Identifier(873));
    if(victim.state==ReadyAssetState::kReady){ready_round=round;break;}
    store.WaitForIdleForTesting();
  }
  std::cout<<"underlay_saturation,abandoned=32,rounds=48,ready_round="<<ready_round
           <<",derivations="<<derivations<<'\n';
  Check(ready_round>0&&derivations<=96,
        "newfourth underlay must progress within48rounds96derivations behind32abandoned descriptors");
  const auto pixels=victim.bytes?DecodeTestPng(*victim.bytes):std::nullopt;
  Check(pixels&&PixelAt(*pixels,0,0)[0]==std::uint8_t(Identifier(873).back())%16,
        "saturated FIFO returns the victim's distinct immutable source pixels");
}
void CommitUnderlay(ReferenceStore& store,const std::string& id) {
  MarkController marks(store,[](const auto&){return true;});
  const auto generation=Begin(marks,id);marks.Captured(generation,Capture());
  marks.Release(generation,std::vector<std::vector<DisplayPoint>>{Path()},
                {1'000'002,1002});store.WaitForIdleForTesting();
}

void DisplayCropAbandonedProgress() {
  TempDirectory temp;ReferenceStore store(temp.path);
  CommitTwelveUnderlay(store,Identifier(910));CommitTwelveUnderlay(store,Identifier(911));
  (void)store.RequestReadyCrop(Identifier(910));store.WaitForIdleForTesting();
  // The first completed crop is deliberately never consumed. The second
  // identity still has the same fixed48-poll progress requirement.
  ReadyAssetResult victim;unsigned ready_round=0;
  for(unsigned round=1;round<=48;++round) {
    victim=store.RequestReadyCrop(Identifier(911));
    if(victim.state==ReadyAssetState::kReady){ready_round=round;break;}
    store.WaitForIdleForTesting();
  }
  std::cout<<"crop_abandoned_progress,rounds=48,ready_round="<<ready_round<<'\n';
  Check(ready_round>0&&victim.bytes&&victim.byte_sha256==Sha256(*victim.bytes),
        "abandoned first crop cannot delay second crop beyond48polls");
  const auto first=store.RequestReadyCrop(Identifier(910));
  Check(first.state==ReadyAssetState::kReady&&first.bytes&&*first.bytes!=*victim.bytes,
        "shared bounded cache hands off both distinct immutable crop identities");
  const auto first_bytes=*first.bytes;
  {ReferenceStore cold(temp.path);
    Check(cold.RequestReadyCrop(Identifier(910)).state==ReadyAssetState::kPending,
          "source-crop cache handoff stays memory-only after restart");
    cold.WaitForIdleForTesting();
    const auto regenerated=cold.RequestReadyCrop(Identifier(910));
    Check(regenerated.bytes&&*regenerated.bytes==first_bytes,
          "cold source crop regenerates unchanged bytes");}
  Check(store.Delete(Identifier(910))==DeleteResult::kDeleted&&
        store.RequestReadyCrop(Identifier(910)).state==ReadyAssetState::kFailed&&
        store.RequestReadyDisplayUnderlay(Identifier(910)).state==ReadyAssetState::kFailed,
        "delete invalidates typed cached crop and underlay domains");
}
void DisplayUnderlayLifecycle() {
  for(int action=0;action<3;++action) {
    TempDirectory temp;ReferenceStore store(temp.path);
    const auto id=Identifier(850+action);CommitUnderlay(store,id);
    std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;
    ReferenceStoreTestHooks hooks;
    hooks.before_derive_annotated=[&](std::string_view) {
      std::unique_lock lock(mutex);entered=true;condition.notify_all();
      condition.wait(lock,[&]{return released;});
    };
    store.SetTestHooks(hooks);(void)store.RequestReadyDisplayUnderlay(id);
    {std::unique_lock lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(2),
        [&]{return entered;}),"underlay reaches held source work");}
    if(action==0)Check(store.Delete(id)==DeleteResult::kDeleted,"underlay delete fence accepted");
    if(action==1) {
      std::ofstream corrupt(temp.path/(id+".source.png"),std::ios::binary|std::ios::trunc);
      corrupt<<"corrupt source";
    }
    if(action==2) {
      const auto accepted=store.LookupMetadata(id)->accepted_utc_us;
      hooks.now=[accepted] {auto now=Now();now.utc_us=accepted+2'000'000;return now;};
      store.SetTestHooks(hooks);store.SetRetentionPolicy(1,1);
    }
    {std::lock_guard lock(mutex);released=true;}condition.notify_all();store.WaitForIdleForTesting();
    const auto failed_underlay=store.RequestReadyDisplayUnderlay(id);
    (void)store.RequestReadyAnnotated(id);store.WaitForIdleForTesting();
    Check(failed_underlay.state==ReadyAssetState::kFailed&&
          store.RequestReadyAnnotated(id).state==ReadyAssetState::kFailed&&
          !std::filesystem::exists(temp.path/(id+".annotated.png")),
          "delete/failure/expiry rejects stale completion of both representation domains");
  }
  TempDirectory temp;auto store=std::make_unique<ReferenceStore>(temp.path);
  CommitUnderlay(*store,Identifier(860));
  std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;
  std::atomic<bool> destroyed=false;
  ReferenceStoreTestHooks hooks;
  hooks.before_derive_annotated=[&](std::string_view) {
    std::unique_lock lock(mutex);entered=true;condition.notify_all();
    condition.wait(lock,[&]{return released;});
  };
  store->SetTestHooks(hooks);(void)store->RequestReadyDisplayUnderlay(Identifier(860));
  {std::unique_lock lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(2),
      [&]{return entered;}),"shutdown owns a held underlay derivation");}
  std::thread shutdown([&]{store.reset();destroyed=true;});
  Check(!destroyed,"store shutdown cannot complete while its executor owns source work");
  {std::lock_guard lock(mutex);released=true;}condition.notify_all();shutdown.join();
  Check(destroyed,"store joins underlay executor before releasing its lifetime");
}

void MultiShotReworkBoundaries() {
  // Exercise current, released, encoding, and queued-completion boundaries. All
  // observations are injected; a context return must never reset the latch.
  for(int boundary=-1;boundary<4;++boundary)for(int fault=0;fault<8;++fault) {
    TempDirectory temp;ReferenceStore store(temp.path);
    {MarkController seed(store,[](const auto&){return true;});Commit(store,seed);}
    std::mutex dispatch_mutex;std::vector<std::function<void()>> dispatched;
    MarkController marks(store,MarkController::PendingClipboard{},MarkController::ReadyClipboard{},
      [&](std::function<void()> task) {
        if(boundary==3){std::lock_guard lock(dispatch_mutex);dispatched.push_back(std::move(task));}
        else task();
      });
    auto context=ContextFixture();const bool chrome=fault>=4&&fault<7;
    if(chrome)context.bundle_id="com.google.Chrome";
    Observe(marks,context);marks.SetViewportContinuity(99,true);
    PageIdentity page;page.browser_pid=context.window_pid;page.window_id=context.window_id;
    page.window_bounds=context.window;page.process_start_identity_us=700;
    page.opaque_tab_id="original-tab";page.navigation_digest=std::string(64,'a');
    const auto publishPage=[&](PageAvailability availability,PageIdentity value) {
      marks.SetPageObservation({availability,marks.page_observation().generation+1,Now(),
          availability==PageAvailability::kReady?std::optional<PageIdentity>(value):std::nullopt});
    };
    if(chrome)publishPage(PageAvailability::kReady,page);
    const auto discontinuity=[&] {
      if(fault<4||fault==7) {
        if(fault==7)marks.SetOwnInputFocus(true);
        auto observation=WindowFor(context,marks.window_observation().generation+1);
        if(fault==0||fault==7)++observation.window_id;
        if(fault==1)observation.window_title="known different title";
        if(fault==2)++observation.bounds.width;
        if(fault==3)observation.availability=WindowAvailability::kUnavailable;
        marks.SetWindowObservation(observation);Observe(marks,context);
        if(fault==7)marks.SetOwnInputFocus(false);
      } else {
        auto other=page;other.opaque_tab_id="different-tab";
        publishPage(fault==4?PageAvailability::kReady:fault==5?PageAvailability::kUnavailable:PageAvailability::kTimedOut,other);
        publishPage(PageAvailability::kReady,page);
      }
    };
    const auto generation=marks.BeginAppend(idA,StableMarkId(idA,0),1,idB);
    if(boundary==-1)discontinuity();
    Check(marks.BindContext(generation,context),"rework context binds");
    if(chrome)Check(marks.BindPageIdentity(generation,page),"rework page binds");
    if(boundary==0)discontinuity();
    marks.Release(generation,Path(),{1'000'002,1002});
    if(boundary==1)discontinuity();
    std::mutex mutex;std::condition_variable condition;bool entered=false,resume=false;
    if(boundary==2) {
      ReferenceStoreTestHooks hooks;hooks.stage=[&](std::string_view stage,std::string_view) {
        if(stage!="append_payload")return;
        std::unique_lock lock(mutex);entered=true;condition.notify_all();condition.wait(lock,[&]{return resume;});
      };store.SetTestHooks(hooks);
    }
    marks.Captured(generation,Capture());
    if(boundary==2) {
      {std::unique_lock lock(mutex);condition.wait(lock,[&]{return entered;});}
      discontinuity();{std::lock_guard lock(mutex);resume=true;}condition.notify_all();
    }
    store.WaitForIdleForTesting();
    if(boundary==3) {discontinuity();for(auto& task:dispatched)task();}
    const auto visible=marks.LiveShots(idA);
    const auto expected=chrome?1:2;
    if(visible.size()!=expected)std::cerr<<"pending coordinate discriminator boundary="<<boundary<<" fault="<<fault<<" visible="<<visible.size()<<" expected="<<expected<<'\n';
    Check(store.Collection(idA).shots.size()==2&&visible.size()==expected,
        "fresh exact source return restores old and pending saved geometry at every capture boundary");
  }
  for(bool chrome:{false,true}) {
    TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});Commit(store,marks);
    auto context=ContextFixture();if(chrome)context.bundle_id="com.google.Chrome";
    Observe(marks,context);marks.SetViewportContinuity(100,true);
    PageIdentity page;page.browser_pid=context.window_pid;page.window_id=context.window_id;
    page.window_bounds=context.window;page.process_start_identity_us=700;
    page.opaque_tab_id="own-ui-tab";page.navigation_digest=std::string(64,'a');
    if(chrome)marks.SetPageObservation({PageAvailability::kReady,1,Now(),page});
    const auto generation=marks.BeginAppend(idA,StableMarkId(idA,0),1,idB);marks.BindContext(generation,context);
    if(chrome)marks.BindPageIdentity(generation,page);
    marks.Release(generation,Path(),{1'000'002,1002});
    marks.SetOwnInputFocus(true);
    marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
    if(chrome)marks.SetPageObservation({PageAvailability::kUnavailable,2,Now()});
    Observe(marks,context);if(chrome)marks.SetPageObservation({PageAvailability::kReady,3,Now(),page});
    marks.SetOwnInputFocus(false);marks.Captured(generation,Capture());store.WaitForIdleForTesting();
    Check(marks.LiveShot(idA).has_value(),"positively owned UI suspension preserves valid pending continuity");
  }
  {
    TempDirectory temp;ReferenceStore store(temp.path);int copies=0;
    MarkController marks(store,[&](const auto& reference){Check(reference.value.id==idA,"live copy retains root owner");++copies;return true;});Commit(store,marks);
    auto next=ContextFixture();next.window_id+=7;next.window_title="New owning window";
    Observe(marks,next);marks.SetViewportContinuity(101,true);
    auto generation=marks.BeginAppend(idA,StableMarkId(idA,0),1,idB);marks.BindContext(generation,next);
    marks.Release(generation,Path(),{1'000'002,1002});marks.Captured(generation,Capture());store.WaitForIdleForTesting();
    const int before=copies;
    Check(marks.LiveShot(idA)&&marks.RecopyJob(idA)&&copies==before+1,"latest admitted shot permits deliberate root copy in its own window");
    auto unknown=next;++unknown.window_id;Observe(marks,unknown);
    Check(!marks.RecopyJob(idA)&&marks.DeleteJob(idA)==DeleteResult::kNotFound&&
        store.LookupMetadata(idA)->state==ReferenceJobState::kReady,"invalid current context denies live actions without deleting root");
    Observe(marks,next);marks.SetViewportContinuity(102,true);
    Check(marks.RecopyJob(idA)&&copies==before+2,"fresh matching saved source recovers deliberate live copy after ordinary epoch");
    Observe(marks,ContextFixture());
    Check(marks.RecopyJob(idA),"matching original saved shot uses the same live action projection as later shots");
    Observe(marks,next);
    generation=marks.BeginAppend(idA,StableMarkId(idA,0),2,idC);marks.BindContext(generation,next);
    marks.Release(generation,Path(),{1'000'002,1002});marks.Captured(generation,Capture());store.WaitForIdleForTesting();
    Check(marks.LiveShot(idA)&&marks.DeleteJob(idA)==DeleteResult::kDeleted&&
        store.LookupMetadata(idA)->state==ReferenceJobState::kDeleted,"latest admitted shot deletes the entire original root");
  }
}
void MultiShotControllerTransactions() {
  TempDirectory temp;ReferenceStore store(temp.path);int clipboard_calls=0;
  MarkController marks(store,[&](const auto&){++clipboard_calls;return true;});
  Commit(store,marks);const int before=clipboard_calls;
  const auto mark=StableMarkId(idA,0);
  Check(marks.BeginAppend(idA,StableMarkId(idB,0),1,idB)==0&&store.MetadataJobs().size()==1,"invalid selected owner refuses without new root");
  marks.SetViewportContinuity(10,true);
  auto generation=marks.BeginAppend(idA,mark,1,idB);
  Check(generation!=0&&marks.BindContext(generation,ContextFixture()),"append freezes explicit real mark owner");
  marks.Release(generation,Path(10),{1'000'002,1002});
  Check(store.LookupMetadata(idA)->state==ReferenceJobState::kReady&&store.Collection(idA).shots.size()==1,"pending append keeps root ready and old snapshot");
  marks.Captured(generation,Capture());store.WaitForIdleForTesting();
  Check(marks.phase()==ReferencePhase::kCommitted&&store.Collection(idA).shots.size()==2&&clipboard_calls==before,"append success never publishes clipboard");
  Check(marks.LiveShot(idA)&&marks.LiveShot(idA)->id==idB&&marks.marks().size()==2&&marks.marks()[1].value.first_mark_slot==1,"saved projection contains original and latest geometry equally");
  marks.SetViewportContinuity(11,true);
  Check(marks.LiveShots(idA).size()==2&&marks.marks().size()==2,"unclassified epoch is not positive coordinate loss");
  auto movedWindow=marks.window_observation();movedWindow.generation++;movedWindow.bounds.x++;marks.SetWindowObservation(movedWindow);
  Check(marks.marks().empty(),"positive window movement suppresses stroke number and hit together");
  Observe(marks,ContextFixture());Check(marks.LiveShots(idA).size()==2,"fresh exact original window restores both saved shots");
  generation=marks.BeginAppend(idA,mark,2,idC);marks.Abort("cancel");
  marks.Captured(generation,Capture());marks.Release(generation,Path(),{1'000'002,1002});store.WaitForIdleForTesting();
  Check(store.Collection(idA).shots.size()==2,"cancel retains old committed snapshot");
  generation=marks.BeginAppend(idA,mark,2,idC);marks.BindContext(generation,ContextFixture());
  marks.Release(generation,Path(),{1'000'002,1002});marks.Abort("cancel after release before pixels");
  marks.Captured(generation,Capture());store.WaitForIdleForTesting();
  Check(store.Collection(idA).shots.size()==2,"released append cancellation before pixels stays invisible");
  {
    std::mutex mutex;std::condition_variable condition;bool entered=false,resume=false;
    ReferenceStoreTestHooks hooks;hooks.stage=[&](std::string_view stage,std::string_view) {
      if(stage!="append_payload")return;
      std::unique_lock lock(mutex);entered=true;condition.notify_all();condition.wait(lock,[&]{return resume;});
    };store.SetTestHooks(hooks);
    generation=marks.BeginAppend(idA,mark,2,idC);marks.BindContext(generation,ContextFixture());
    marks.Release(generation,Path(),{1'000'002,1002});marks.Captured(generation,Capture());
    {std::unique_lock lock(mutex);condition.wait(lock,[&]{return entered;});}
    marks.Abort("cancel encoded append before publish");
    {std::lock_guard lock(mutex);resume=true;}condition.notify_all();store.WaitForIdleForTesting();
    store.SetTestHooks({});
    Check(store.Collection(idA).shots.size()==2&&marks.LiveShots(idA).size()==2,"cancellation after encoding wins publication while existing saved shots remain visible");
  }
  generation=marks.BeginAppend(idA,mark,2,idC);marks.BindContext(generation,ContextFixture());
  marks.Release(generation,Path(),{1'000'002,1002});auto deadline=Now();deadline.monotonic_us+=31'000'000;
  marks.Expire(deadline);marks.Captured(generation,Capture());store.WaitForIdleForTesting();
  Check(store.Collection(idA).shots.size()==2,"released missing-pixel append expires without partial ownership");
  generation=marks.BeginAppend(idA,mark,2,idD);marks.BindContext(generation,ContextFixture());
  marks.Release(generation,Path(),{1'000'002,1002});marks.SetViewportContinuity(12,true);
  marks.Captured(generation,Capture());store.WaitForIdleForTesting();
  Check(store.Collection(idA).shots.size()==3&&marks.LiveShot(idA)&&marks.LiveShot(idA)->id==idD,
      "async epoch and recovered geometry do not erase old or new stored geometry");
  generation=marks.BeginAppend(idA,mark,3,idE);marks.BindContext(generation,ContextFixture());
  marks.Release(generation,Path(),{1'000'002,1002});Check(store.Delete(idA)==DeleteResult::kDeleted,"delete during append");
  marks.Captured(generation,Capture());store.WaitForIdleForTesting();
  Check(store.Collection(idA).status==MarkNotesStatus::kDeleted&&clipboard_calls==before,"delete wins late append with no implicit publication");
}
void OwnUIQueuedAppendCompletion() {
  for(int fault=0;fault<6;++fault) {
    TempDirectory temp;ReferenceStore store(temp.path);
    std::atomic_bool hold=false;std::mutex mutex;std::vector<std::function<void()>> queued;
    MarkController marks(store,MarkController::PendingClipboard{},MarkController::ReadyClipboard{},
      [&](std::function<void()> task) {if(hold){std::lock_guard lock(mutex);queued.push_back(std::move(task));}else task();});
    auto context=ContextFixture();context.observed=Now();context.title={1,TitleAvailability::kReady};
    context.native_window={1,700,"queued-append",1};marks.SetViewportContinuity(900,true);Observe(marks,context);
    auto pixels=[&]{auto value=Capture();value.requested=Now();value.completed=Now();return value;};
    auto generation=marks.Begin(idA);marks.BindContext(generation,context);
    marks.Captured(generation,pixels());marks.Release(generation,Path(),Now());store.WaitForIdleForTesting();
    const auto original=store.LookupMetadata(idA)->metadata;
    marks.RecordPresentedSingle(*original,context.displays);
    if(fault==4)marks.SetOwnInputFocus(true);
    auto target=marks.BeginOwnUIPresentationSource();Check(target.has_value(),"queued append starts with actually painted original witness");
    // An attempted new capture while own UI is already current has no ordinary
    // authorization, even though a matching presentation channel exists.
    if(fault==4)marks.SetWindowObservation({WindowAvailability::kUnavailable,2,Now()});
    hold=true;generation=marks.BeginAppendReference(idA,1,idB);
    Check(marks.BindContext(generation,context),"queued append binds immutable original capture");
    marks.Captured(generation,pixels());marks.Release(generation,Path(30),Now());store.WaitForIdleForTesting();
    marks.SetOwnInputFocus(true);
    marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
    Check(marks.PublishOwnUIPresentationObservation(*target,WindowFor(context,100),{}),"queued completion uses fresh bound source without changing public foreground");
    if(fault==1)marks.SetViewportContinuity(901,true);
    if(fault==2)marks.RetireReferencePresentation(idA);
    if(fault==3) {
      auto changed=WindowFor(context,101);changed.window_id++;
      Check(!marks.PublishOwnUIPresentationObservation(*target,changed,{}),"queued completion mismatched source retires atomically");
    }
    if(fault==5)std::this_thread::sleep_for(std::chrono::milliseconds(2050));
    {std::lock_guard lock(mutex);Check(queued.size()==1,"exact atomic append completion queued");for(auto& task:queued)task();queued.clear();}
    Check(store.Collection(idA).shots.size()==2,"queued append commits same root atomically");
    if(fault==5) {
      Check(!marks.own_ui_presentation_source(),"expired queued completion invalidates private target generation before ordinary return");
      marks.SetOwnInputFocus(false);Observe(marks,context);
    }
    const auto live=marks.LiveShots(idA);
    if(fault==0)Check(live.size()==2&&live[0].id==idA&&live[1].id==idB&&
        Serialize(*live[0].metadata)==Serialize(*original)&&live[1].first_mark_slot==1,
        "already external-authorized append completes with OLD+NEW exact geometry, ownership and global slots during own UI");
    else if(fault==3)Check(live.empty(),"mismatched private query temporarily hides queued saved shots");
    else Check(live.size()==2,"committed saved projection is independent of old transaction admission witnesses");
    marks.SetOwnInputFocus(false);Observe(marks,context);
    Check(marks.LiveShots(idA).size()==2&&!marks.Pending()&&Serialize(*store.Collection(idA).shots.front().metadata)==Serialize(*original),
        "fresh ordinary return restores queued OLD and NEW without starting capture or rewriting original");
  }
}

void OwnUIPresentationContinuity() {
  // Owned native evidence only: no app, permission, window-server or UI calls.
  for(bool chrome:{false,true})for(int fault=0;fault<16;++fault) {
    if((fault==12||fault==14||fault==15)&&!chrome||fault==13&&chrome)continue;
    TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});
    auto context=ContextFixture();context.observed=Now();context.native_window={1,700,"owned-source",1};
    context.title={1,TitleAvailability::kReady};if(chrome)context.bundle_id="com.google.Chrome";
    PageIdentity identity;identity.browser_pid=context.pid;identity.window_id=context.window_id;
    identity.window_bounds=context.window;identity.process_start_identity_us=700;
    identity.opaque_tab_id="original-tab";identity.navigation_digest=std::string(64,'a');
    const auto page=[&](std::uint64_t generation) {
      return chrome?PageObservation{PageAvailability::kReady,generation,Now(),identity}:PageObservation{};
    };
    Observe(marks,context);if(chrome)marks.SetPageObservation(page(1));marks.SetViewportContinuity(600,true);
    const auto finish=[&](std::uint64_t generation) {
      Check(marks.BindContext(generation,context),"own source capture binds original");
      if(chrome)Check(marks.BindPageIdentity(generation,identity),"own source capture binds original page");
      auto pixels=Capture();pixels.requested=Now();pixels.completed=Now();
      marks.Captured(generation,std::move(pixels));marks.Release(generation,Path(),Now());store.WaitForIdleForTesting();
      Check(marks.phase()==ReferencePhase::kCommitted,"own source owned capture completes");
    };
    finish(marks.Begin(idA));const auto original=*store.LookupMetadata(idA)->metadata;
    // No unpresented/history member may seed an own-UI source, even in the same tuple.
    Check(marks.BeginOwnUIPresentationSource().has_value(),"fresh valid saved single seeds query identity without paint");
    marks.RecordPresentedSingle(original,context.displays);
    auto target=marks.BeginOwnUIPresentationSource();Check(target.has_value(),"actually painted fresh single seeds exact query");
    marks.EndOwnUIPresentationSource();
    finish(marks.BeginAppendReference(idA,1,idB));
    target=marks.BeginOwnUIPresentationSource();Check(target.has_value(),"admitted collection seeds exact query");
    const auto before=marks.LiveShots(idA);Check(before.size()==2,"own source starts from exact two admitted shots");
    marks.SetOwnInputFocus(true);
    marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
    if(chrome)marks.SetPageObservation({PageAvailability::kUnavailable,2,Now()});
    auto window=WindowFor(context,100);auto freshPage=page(100);
    Check(marks.PublishOwnUIPresentationObservation(*target,window,freshPage),"fresh private observation sustains admitted presentation");
    Check(marks.window_observation().availability==WindowAvailability::kUnavailable&&
          (!chrome||marks.page_observation().availability==PageAvailability::kUnavailable)&&
          !marks.BeginPresentationPrefix(),"private origin cannot enable ordinary foreground/capture prefix");
    MarkController history(store,[](const auto&){return true;});history.SetViewportContinuity(600,true);
    Check(!history.BeginOwnUIPresentationSource()&&!history.PublishOwnUIPresentationObservation(*target,window,freshPage)&&
          history.LiveShots(idA).empty(),"history/restart cannot borrow source or admitted membership");
    const auto current=marks.LiveShots(idA);
    Check(current.size()==2&&Serialize(*current[0].metadata)==Serialize(*before[0].metadata)&&
          Serialize(*current[1].metadata)==Serialize(*before[1].metadata),"private refresh preserves exact source metadata, coordinates, paths and global slots");
    if(fault==0) {
      // Sustain beyond the unchanged 2s age bound through genuinely new owned
      // provider observations, then hand back to genuine external foreground.
      if(!chrome)for(int index=0;index<3;++index) {
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        Check(marks.PublishOwnUIPresentationObservation(*target,WindowFor(context,101+index),{}),"sustained >2s uses fresh provider evidence");
      }
      marks.SetOwnInputFocus(false);Observe(marks,context);if(chrome)marks.SetPageObservation(page(101));
      marks.EndOwnUIPresentationSource();
      Check(marks.LiveShots(idA).size()==2,"fresh real foreground handoff retains original admissions");
      continue;
    }
    window=WindowFor(context,110);freshPage=page(110);
    if(fault==1)window.observed.monotonic_us-=3'000'000;
    if(fault==2)++window.window_id;
    if(fault==3)++window.native_window.incarnation;
    if(fault==4)window.availability=WindowAvailability::kUnavailable;
    if(fault==5)++window.bounds.width;
    if(fault==6) {if(chrome)freshPage.availability=PageAvailability::kAmbiguous;else window.title.availability=TitleAvailability::kAmbiguous;}
    if(fault==7) {if(chrome)freshPage.availability=PageAvailability::kTimedOut;else window.window_title="changed title";}
    if(fault==8)marks.SetViewportContinuity(601,true);
    if(fault==9)marks.SetPresentationPermitted(false);
    if(fault==10) {
      if(!chrome)std::this_thread::sleep_for(std::chrono::milliseconds(2050));
      else freshPage.identity->opaque_tab_id="navigation changed";
      if(!chrome) {
        Check(marks.LiveShots(idA).empty(),"QA expired private source is no longer visible before handoff");
        marks.SetOwnInputFocus(false);Observe(marks,context);
        Check(marks.LiveShots(idA).size()==2,"fresh ordinary handoff restores saved shots after expired private evidence");
      }
      window=WindowFor(context,110); // New evidence after a missing refresh cannot resurrect.
    }
    if(fault==12||fault==14) {
      if(fault==14) {
        marks.SetOwnInputFocus(false);Observe(marks,context);marks.SetPageObservation(page(101));
        marks.EndOwnUIPresentationSource();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1100));
      marks.SetOwnInputFocus(false);Observe(marks,context);
      Check(marks.LiveShots(idA).size()==2,"still-fresh window-first handoff preserves admissions while awaiting page");
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      Check(marks.LiveShots(idA).empty(),"old page expiry hides retained marks before fresh page replacement");
      marks.SetPageObservation(page(120));
      Check(marks.LiveShots(idA).size()==2,
          "fresh page with still-fresh original window restores saved shots after page expiry");
    }
    if(fault==15) {
      marks.SetPageObservation({PageAvailability::kReady,120,Now(),std::nullopt});
      Check(!marks.own_ui_presentation_source()&&marks.LiveShots(idA).empty(),
          "invalid ready page cannot be normalized into an own-UI unavailable exemption");
    }
    if(fault==13) {
      marks.SetOwnInputFocus(false);Observe(marks,context);marks.EndOwnUIPresentationSource();
      Check(marks.LiveShots(idA).size()==2,"within-bound ordinary handoff is healthy before expiry counterexample");
      std::this_thread::sleep_for(std::chrono::milliseconds(2050));Observe(marks,context);
      Check(marks.LiveShots(idA).size()==2,"fresh ordinary replacement restores saved marks after prior evidence expiry");
    }
    if(fault==11)marks.RetireReferencePresentation(idA);
#ifdef __APPLE__
    if(chrome&&(fault==6||fault==7)) {
      seethis::platform::ChromePageSample sample;sample.availability=fault==6?PageAvailability::kAmbiguous:PageAvailability::kReady;
      sample.browser_pid=context.pid;sample.window_id=context.window_id;sample.window_bounds=context.window;
      sample.process_start_identity_us=700;sample.opaque_tab_id="original-tab";sample.url="https://owned.example.test/original";
      int callbacks=0;bool accepted=true;
      seethis::platform::AcquireChromePageAsync([sample,fault] {
        if(fault==7)std::this_thread::sleep_for(std::chrono::milliseconds(450));return sample;
      },110,[&](PageObservation observation) {
        ++callbacks;accepted=marks.PublishOwnUIPresentationObservation(*target,WindowFor(context,110),std::move(observation));
      });
      CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.7,false);
      Check(callbacks==1&&!accepted&&marks.LiveShots(idA).empty(),"production async ambiguity/timeout retires private target once; late success cannot resurrect");
    }
#endif
    if(fault==11||fault==8)Check(marks.PublishOwnUIPresentationObservation(*target,window,freshPage),"raw epoch or transient capture-witness retirement does not prohibit valid fresh private query");
    else Check(!marks.PublishOwnUIPresentationObservation(*target,window,freshPage),"invalid/stale/retired private observation fails closed");
    Observe(marks,context);
    if(chrome) {
      if(fault==15) {marks.SetPageObservation(page(120));Check(marks.LiveShots(idA).empty(),"same invalid page generation cannot publish a recovery sample");}
      marks.SetPageObservation(page(std::max<std::uint64_t>(120,marks.page_observation().generation+1)));
    }
    marks.SetOwnInputFocus(false);marks.SetPresentationPermitted(true);
    const auto returned=marks.LiveShots(idA);
    if(returned.size()!=(fault==3?0:2))std::cerr<<"own-source recovery discriminator chrome="<<chrome<<" fault="<<fault<<" visible="<<returned.size()<<'\n';
    if(fault==3)Check(returned.empty(),"known native lifetime loss remains terminal after exact source return");
    else Check(returned.size()==2,"fresh external return restores temporary saved-source hide");
    Check(!marks.PublishOwnUIPresentationObservation(*target,WindowFor(context,121),page(121)),
          "late private callback cannot publish after actual own-focus exit");
  }
}

void UnifiedSavedProjectionTransitions() {
  for(bool chrome:{false,true}) {
    TempDirectory temp;ReferenceStore store(temp.path);
    auto context=ContextFixture();context.observed=Now();context.title={1,TitleAvailability::kReady};
    context.native_window={1,700,"unified-source",1};if(chrome)context.bundle_id="com.google.Chrome";
    PageIdentity page;page.browser_pid=context.pid;page.window_id=context.window_id;
    page.window_bounds=context.window;page.process_start_identity_us=700;page.opaque_tab_id="unified-tab";
    page.navigation_digest=std::string(64,'a');
    const auto observe=[&](MarkController& owner) {
      Observe(owner,context);
      if(chrome)owner.SetPageObservation({PageAvailability::kReady,owner.page_observation().generation+1,Now(),page});
    };
    const auto complete=[&](MarkController& owner,std::uint64_t generation,double shift) {
      Check(generation&&owner.BindContext(generation,context),"unified ordinary source capture binds independently");
      if(chrome)Check(owner.BindPageIdentity(generation,page),"unified ordinary Chrome capture binds real page independently");
      auto pixels=Capture();pixels.requested=Now();pixels.completed=Now();owner.Captured(generation,pixels);
      owner.Release(generation,std::vector<std::vector<DisplayPoint>>{Path(shift),Path(shift+5)},Now());
      store.WaitForIdleForTesting();Check(owner.phase()==ReferencePhase::kCommitted,"unified capture atomically commits two-stroke round");
    };
    {MarkController seed(store,[](const auto&){return true;});seed.SetViewportContinuity(1,true);observe(seed);complete(seed,seed.Begin(idA),0);}
    // No original paint, fresh-single, presented-single or admitted checkpoint.
    MarkController marks(store,[](const auto&){return true;});marks.SetViewportContinuity(1200,true);observe(marks);
    const auto original=Serialize(*store.Collection(idA).shots.front().metadata);
    Check(store.UpdateMarkNote(idA,StableMarkId(idA,0),"original exact note 世界",0).status==MarkNoteUpdateStatus::kUpdated,"unified original note owner persists");
    const auto visible=[&](std::size_t count,const char* assertion) {
      const auto collection=store.Collection(idA);const auto live=marks.LiveShots(idA);const auto notes=store.MarkSnapshots(idA);
      Check(live.size()==count&&marks.marks().size()==count&&!marks.Pending(),assertion);
      Check(Serialize(*collection.shots.front().metadata)==original&&notes.marks[0].note=="original exact note 世界",
          "unified original exact serialized geometry/subpaths/display-unit-scale-xy and note never change");
      for(std::size_t shot=0;shot<live.size();++shot) {
        Check(live[shot].id==collection.shots[shot].id&&live[shot].first_mark_slot==shot*2&&
            Serialize(*live[shot].metadata)==Serialize(*collection.shots[shot].metadata),"unified projected root/shot/source and exact immutable geometry match saved object");
        for(std::size_t local=0;local<2;++local) {
          const auto slot=shot*2+local;const auto& mark=notes.marks[slot];
          Check(mark.id==StableMarkId(idA,slot)&&mark.slot==slot&&mark.number==slot+1&&mark.shot_id==live[shot].id&&mark.local_slot==local,
              "unified stable IDs/global-local slots/number/shot and original note ownership remain exact");
        }
      }
    };
    for(std::size_t count=1;count<=3;++count) {
      if(count>1) {
        marks.SetPresentationPermitted(false);marks.RetireLiveShot();
        marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
        marks.SetPresentationPermitted(true);observe(marks);
        complete(marks,marks.BeginAppendReference(idA,count-1,count==2?idB:idC),count*10);
      }
      visible(count,"unified saved projection restores OLD plus NEW after hidden unpainted single append");
      auto stale=marks.window_observation();stale.generation++;stale.observed.monotonic_us-=2'000'001;
      marks.SetWindowObservation(stale);Check(marks.marks().empty(),"unified stale source hides all saved ink");observe(marks);
      visible(count,"unified fresh matching replacement restores saved projection after stale evidence");
      auto unavailable=marks.window_observation();unavailable.generation++;unavailable.availability=WindowAvailability::kUnavailable;
      marks.SetWindowObservation(unavailable);Check(marks.marks().empty(),"unified unavailable source temporarily hides");observe(marks);
      visible(count,"unified available exact source restores saved projection");
      auto away=marks.window_observation();away.generation++;away.window_id++;
      marks.SetWindowObservation(away);Check(marks.marks().empty(),"unified other window hides without coordinate guessing");observe(marks);
      visible(count,"unified exact original window return restores saved projection");
      if(chrome) {
        auto other=page;other.opaque_tab_id="away";
        marks.SetPageObservation({PageAvailability::kReady,marks.page_observation().generation+1,Now(),other});
        Check(marks.marks().empty(),"unified page away hides all saved shots");observe(marks);
        visible(count,"unified original Chrome page return restores saved shots");
        auto expired=marks.page_observation();expired.generation++;expired.observed.monotonic_us-=2'000'001;
        marks.SetPageObservation(expired);Check(marks.marks().empty(),"unified stale page hides");observe(marks);
        visible(count,"unified fresh matching page replaces expired page without old witness");
      } else {
        away=marks.window_observation();away.generation++;away.window_title="away title";
        marks.SetWindowObservation(away);Check(marks.marks().empty(),"unified title away hides");observe(marks);
        visible(count,"unified exact original title return restores saved projection");
      }
      marks.SetViewportContinuity(1300+count,false);Check(marks.marks().empty(),"unified monitor unhealthy hides saved projection");
      marks.SetViewportContinuity(1400+count,true);observe(marks);
      visible(count,"unified Healthy plus fresh source restores without automatic new capture");
      marks.SetViewportContinuity(1500+count,true);visible(count,"unified ordinary epoch alone is not coordinate invalidity");
      marks.SetPresentationPermitted(false);Check(marks.marks().empty(),"unified global Hide suppresses same projection");
      marks.SetPresentationPermitted(true);observe(marks);visible(count,"unified Show with fresh evidence restores same saved objects");
      marks.SetDisplayObservation({});Check(marks.marks().empty(),"unified unavailable topology is temporary");
      marks.SetDisplayObservation(context.displays);visible(count,"unified recovered original topology restores without coordinate ban");
      const auto target=marks.BeginOwnUIPresentationSource();Check(target.has_value(),"unified saved source seeds own UI query identity without paint");
      marks.SetOwnInputFocus(true);marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
      if(chrome)marks.SetPageObservation({PageAvailability::kUnavailable,marks.page_observation().generation+1,Now()});
      const auto privatePage=[&](std::uint64_t generation) {return chrome?PageObservation{PageAvailability::kReady,generation,Now(),page}:PageObservation{};};
      Check(marks.PublishOwnUIPresentationObservation(*target,WindowFor(context,10000+count*10),privatePage(10000+count*10)),"unified exact fresh own UI query presents saved members");
      visible(count,"unified private source keeps ink/numbers at original geometry");
      std::this_thread::sleep_for(std::chrono::milliseconds(2050));
      Check(marks.marks().empty(),"unified expired private evidence hides while Inspector stays active");
      marks.RetireLiveShot();const auto renewed=marks.BeginOwnUIPresentationSource();
      Check(renewed&&renewed->generation!=target->generation&&marks.marks().empty()&&
          marks.window_observation().availability==WindowAvailability::kUnavailable&&!marks.Pending()&&!marks.BeginPresentationPrefix(),
          "unified own-active reacquisition retains only bound query identity, never freshness or capture authority");
      Check(marks.PublishOwnUIPresentationObservation(*renewed,WindowFor(context,20000+count*10),privatePage(20000+count*10)),
          "unified fresh reobservation restores private projection while SAME own UI remains active");
      Check(!marks.PublishOwnUIPresentationObservation(*target,WindowFor(context,30000+count*10),privatePage(30000+count*10)),"unified late old query generation cannot supply authority");
      visible(count,"unified own-active fresh private recovery restores exact saved projection");
      marks.SetOwnInputFocus(false);observe(marks);marks.EndOwnUIPresentationSource();
      visible(count,"unified fresh ordinary return restores without requiring old private target");
    }
    for(int reason=1;reason<4;++reason) {
      MarkController blocked(store,[](const auto&){return true;});blocked.SetViewportContinuity(1,true);observe(blocked);
      if(reason==1){auto moved=blocked.window_observation();moved.generation++;moved.bounds.x++;blocked.SetWindowObservation(moved);}
      if(reason==2){auto displays=context.displays;displays[0].logical.x++;displays[0].logical_to_pixels.tx-=displays[0].scale;blocked.SetDisplayObservation(displays);}
      if(reason==3){auto replaced=blocked.window_observation();replaced.generation++;replaced.native_window.incarnation++;blocked.SetWindowObservation(replaced);}
      if(reason!=3)Check(blocked.SavedProjection(*store.Collection(idA).shots.front().metadata)==SavedProjectionState::kTemporarilyHidden,
          "unified current geometry or topology mismatch temporarily hides old saved source");
      blocked.SetDisplayObservation(context.displays);observe(blocked);const auto state=blocked.SavedProjection(*store.Collection(idA).shots.front().metadata);
      Check(state==(reason==3?SavedProjectionState::kTerminal:SavedProjectionState::kVisible),
          "unified fresh exact original geometry/topology restores old source while native replacement stays terminal");
      if(reason==1) {
        complete(blocked,blocked.BeginAppendReference(idA,3,idD),50);
        Check(blocked.LiveShots(idA).size()==4&&blocked.LiveShots(idA).back().id==idD,
            "unified recovered old geometry and deliberate new capture project together");
        observe(blocked);const auto pending=blocked.BeginAppendReference(idA,4,idE);
        Check(pending&&blocked.BindContext(pending,context),"unified pending coordinate fixture binds ordinary source");
        if(chrome)Check(blocked.BindPageIdentity(pending,page),"unified pending coordinate fixture binds page");
        blocked.Release(pending,std::vector<std::vector<DisplayPoint>>{Path(55),Path(60)},Now());
        auto pendingMove=blocked.window_observation();pendingMove.generation++;pendingMove.observed=Now();pendingMove.bounds.x++;blocked.SetWindowObservation(pendingMove);
        auto capture=Capture();capture.requested=Now();capture.completed=Now();blocked.Captured(pending,capture);store.WaitForIdleForTesting();
        observe(blocked);
        Check(store.Collection(idA).shots.size()==5&&blocked.LiveShots(idA).size()==5,
            "unified temporary window move during pending capture does not poison published coordinates");
        std::mutex queueMutex;std::vector<std::function<void()>> queued;
        MarkController delayed(store,MarkController::PendingClipboard{},MarkController::ReadyClipboard{},
            [&](std::function<void()> callback){std::lock_guard lock(queueMutex);queued.push_back(std::move(callback));});
        delayed.SetViewportContinuity(1,true);observe(delayed);const auto late=delayed.BeginAppendReference(idA,5,idF);
        Check(late&&delayed.BindContext(late,context),"unified queued-loss source binds");
        if(chrome)Check(delayed.BindPageIdentity(late,page),"unified queued-loss page binds");
        delayed.Release(late,std::vector<std::vector<DisplayPoint>>{Path(40),Path(45)},Now());
        auto queuedMove=delayed.window_observation();queuedMove.generation++;queuedMove.observed=Now();queuedMove.bounds.x++;delayed.SetWindowObservation(queuedMove);
        auto latePixels=Capture();latePixels.requested=Now();latePixels.completed=Now();
        delayed.Captured(late,latePixels);store.WaitForIdleForTesting();
        const auto queuedCollection=store.Collection(idA);
        if(queuedCollection.shots.size()!=6)std::cerr<<"queued coordinate persistence discriminator phase="<<static_cast<int>(delayed.phase())<<" status="<<delayed.status()<<" shots="<<queuedCollection.shots.size()<<'\n';
        Check(queuedCollection.shots.size()==6,"unified queued append atomically persists complete new shot before dispatch");
        Check(delayed.LiveShots(idA).empty(),"unified current geometry mismatch hides newly Ready shot BEFORE queued completion callback");
        observe(delayed);Check(delayed.LiveShots(idA).size()==6,"unified exact original return restores Ready old and new shots BEFORE queued completion");
        {std::lock_guard lock(queueMutex);Check(queued.size()==1,"unified exact delayed completion remains owned");for(auto& callback:queued)callback();queued.clear();}
        Check(delayed.LiveShots(idA).size()==6,"unified queued completion cannot reinstate a temporary geometry ban");
      }
    }
    const auto terminalReference=*store.Collection(idA).shots.front().metadata;
    Check(store.Delete(idA)==DeleteResult::kDeleted,"unified terminal delete owns saved root");observe(marks);
    Check(marks.marks().empty()&&marks.SavedProjection(terminalReference)==SavedProjectionState::kTerminal,
        "unified terminal deletion cannot restore on observation/Show/private callback");
  }
}

void PersistentPresentationCheckpoints() {
  for(bool paintedBeforeRetirement:{false,true}) {
    TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});
    marks.SetViewportContinuity(500,true);Commit(store,marks);
    const auto original=*store.LookupMetadata(idA)->metadata;
    if(paintedBeforeRetirement)marks.RecordPresentedSingle(original,original.context.displays);
    marks.SetViewportContinuity(501,true);Observe(marks,ContextFixture());
    Check(marks.marks().size()==1,"legacy global rendering survives independent retirement");
    marks.RecordPresentedSingle(original,original.context.displays);
    const auto g=marks.BeginAppendReference(idA,1,idB);
    Check(g&&marks.BindContext(g,ContextFixture()),"root-only append needs no selected mark");
    marks.Captured(g,Capture());marks.Release(g,Path(10),{1'000'002,1002});store.WaitForIdleForTesting();
    Check(marks.LiveShots(idA).size()==2&&marks.IsShotAdmitted(idA,idA)&&marks.IsShotAdmitted(idA,idB),
        "saved single-to-multi conversion needs no former paint or carry checkpoint");
  }
  {
    TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});
    marks.SetViewportContinuity(600,true);Commit(store,marks);
    const auto original=*store.LookupMetadata(idA)->metadata;
    marks.RecordPresentedSingle(original,original.context.displays);
    Check(marks.BeginPresentationPrefix()&&marks.marks().size()==1&&!marks.HitIdentity(Path()[0],2,original.context.displays),
        "prefix keeps paint while atomically suspending hit and admits no fresh lease");
    Check(marks.ConfirmPresentationPrefix(),"exact confirmed prefix preserves checkpoint");
    auto g=marks.BeginAppendReference(idA,1,idB);marks.BindContext(g,ContextFixture());
    marks.Release(g,Path(10),{1'000'002,1002});marks.Captured(g,Capture());store.WaitForIdleForTesting();
    Check(marks.LiveShots(idA).size()==2&&marks.IsShotAdmitted(idA,idA)&&marks.IsShotAdmitted(idA,idB),
        "first single conversion carries actual continuous displayed receipt and new shot");
    const auto retained=marks.LiveShots(idA).size();
    g=marks.BeginAppendReference(idA,2,idC);marks.BindContext(g,ContextFixture());
    Check(marks.LiveShots(idA).size()==retained,"capture begin does not retire old presentation");
    marks.Abort("cancel component");Check(marks.LiveShots(idA).size()==retained,"cancel preserves admitted committed shots");
    g=marks.BeginAppendReference(idA,2,idD);marks.BindContext(g,ContextFixture());
    CaptureResult bad;bad.error="injected capture failure";marks.Captured(g,bad);
    marks.Release(g,Path(30),{1'000'002,1002});store.WaitForIdleForTesting();
    Check(marks.LiveShots(idA).size()==retained&&store.Collection(idA).shots.size()==2,"failed attempt cannot erase old presentation or saved data");
    g=marks.BeginAppendReference(idA,2,idE);marks.BindContext(g,ContextFixture());marks.Release(g,Path(30),{1'000'002,1002});
    auto expired=Now();expired.monotonic_us+=31'000'000;marks.Expire(expired);
    Check(!marks.Pending()&&marks.LiveShots(idA).size()==retained,"timeout clears attempt while retaining continuous presentation");
    Check(marks.BeginPresentationPrefix(),"second prefix starts from current membership");marks.CancelPresentationPrefix();
    Check(marks.LiveShots(idA).size()==retained&&!marks.ConfirmPresentationPrefix(),"abandoned prefix ends suppression and preserves valid existing members");
    marks.SetViewportContinuity(601,true);
    Check(marks.LiveShots(idA).size()==retained,"ordinary epoch alone does not invalidate saved coordinates");
    auto movedWindow=marks.window_observation();movedWindow.generation++;movedWindow.bounds.x++;marks.SetWindowObservation(movedWindow);
    Check(marks.LiveShots(idA).empty(),"current different window bounds temporarily hide original and appended coordinates");
    Observe(marks,ContextFixture());marks.RecordPresentedSingle(original,original.context.displays);
    Check(marks.LiveShots(idA).size()==retained,"fresh exact original context restores saved members independent of paint");
  }
}
void MultiShotCollections() {
  TempDirectory temp;const auto dir=temp.path/"owned";
  Bytes first;std::int64_t accepted=0;std::uint64_t notes_revision=0;
  {
    ReferenceStore store(dir);MarkController marks(store,[](const auto&){return true;});
    Commit(store,marks);first=*store.ReadReadyAsset(idA,false);
    accepted=store.LookupMetadata(idA)->accepted_utc_us;
    const auto mark=StableMarkId(idA,0);
    Check(store.UpdateMarkNote(idA,mark,"original 世界\nexact",0).status==MarkNoteUpdateStatus::kUpdated,"initial note");
    auto append=[&](std::uint64_t expected,const std::string& operation) {
      auto metadata=PendingReference(idA);metadata.context.window_title="Second actual window";
      auto pixels=Capture();pixels.pixels.width=200;pixels.pixels.rgba.resize(200*100*4,255);
      AppendResult result;
      store.AppendAsync(idA,mark,expected,operation,metadata,pixels,[&](auto value){result=value;});
      store.WaitForIdleForTesting();return result;
    };
    ReferenceStoreTestHooks hooks;
    hooks.stage=[&](std::string_view stage,std::string_view){if(stage=="append_payload")
      Check(store.UpdateMarkNote(idA,mark,"concurrent exact note",1).status==MarkNoteUpdateStatus::kUpdated,"concurrent edit before append lock");};
    store.SetTestHooks(hooks);
    Check(append(1,idB).status==AppendStatus::kCommitted,"append commits");store.SetTestHooks({});
    auto collection=store.Collection(idA);auto snapshot=store.MarkSnapshots(idA);
    Check(collection.multi()&&collection.revision==2&&collection.shots.size()==2,"two-shot membership");
    Check(snapshot.marks.size()==2&&snapshot.marks[0].id==mark&&snapshot.marks[1].id==StableMarkId(idA,1)&&snapshot.marks[1].number==2&&snapshot.marks[1].shot_id==idB,"stable global identity and ownership");
    Check(snapshot.marks[0].note=="concurrent exact note"&&snapshot.revision==3,"append merges latest note and advances notes revision");
    const auto& newest=collection.shots.back();
    const auto& firstLocal=snapshot.marks[newest.first_mark_slot];
    Check(firstLocal.local_slot==0&&firstLocal.slot==newest.first_mark_slot&&
        firstLocal.shot_id==newest.id&&firstLocal.id==StableMarkId(idA,newest.first_mark_slot)&&
        firstLocal.number==newest.first_mark_slot+1,
        "authoritative latest local zero maps through global offset for stroke selection");
    notes_revision=snapshot.revision;
    Check(*store.ReadReadyAsset(idA,false)==first&&store.LookupMetadata(idA)->accepted_utc_us==accepted,"first bytes and root retention unchanged");
    Check(collection.shots[1].metadata->context.window_title=="Second actual window"&&collection.shots[1].metadata->global_to_source.a==2&&collection.shots[1].metadata->source.width==200,"per-shot context size transform");
    Check(append(1,idC).status==AppendStatus::kStale&&store.Collection(idA).revision==2,"stale revision cannot publish");
    Check(append(1,idB).status==AppendStatus::kIdempotent&&store.Collection(idA).shots.size()==2,"operation retry readback no duplicate");
    hooks={};hooks.fail_append_before_publish=true;store.SetTestHooks(hooks);
    Check(append(2,idC).status==AppendStatus::kIOError&&store.Collection(idA).revision==2,"crash before publication leaves old complete root");
    hooks={};hooks.fail_append_after_rename=true;store.SetTestHooks(hooks);
    Check(append(2,idD).status==AppendStatus::kCommitUncertain&&store.Collection(idA).revision==3,"postrename uncertain publishes complete new snapshot");
    store.SetTestHooks({});Check(append(2,idD).status==AppendStatus::kIdempotent,"uncertain operation reconciles by identity");
    auto get=[&](const std::string& shot) {auto asset=store.RequestShotAnnotated(idA,shot);store.WaitForIdleForTesting();return store.RequestShotAnnotated(idA,shot);};
    const auto a=get(idA),b=get(idB);
    Check(a.state==ReadyAssetState::kReady&&b.state==ReadyAssetState::kReady&&a.byte_sha256!=b.byte_sha256,"shot cache binds image global number and digest");
    Check(store.RequestShotAnnotated(idA,idF).code=="unknown_shot","cross-reference unknown shot denied");
    Check(store.MetadataJobs().size()==1,"pending and committed shots never become history children");
  }
  {
    ReferenceStore store(dir);const auto collection=store.Collection(idA);const auto marks=store.MarkSnapshots(idA);
    Check(collection.shots.size()==3&&collection.revision==3&&marks.revision==notes_revision+1&&marks.marks[0].note=="concurrent exact note","restart retains full collection notes and ownership");
    Check(*store.ReadReadyAsset(idA,false)==first,"restart preserves original bytes");
    Check(store.Delete(idA)==DeleteResult::kDeleted&&store.Collection(idA).status==MarkNotesStatus::kDeleted,"delete wins over collection");
    Check(!std::filesystem::exists(dir/(idA+".collection"))&&!std::filesystem::exists(dir/(idA+".shot-"+idB+".png")),"delete cleans committed and orphan shot files");
  }
  {
    ReferenceStore store(temp.path/"empty");MarkController marks(store,[](const auto&){return true;});Commit(store,marks);
    auto empty=PendingReference(idA);empty.path.clear();empty.regions.clear();empty.capture_display_id=1;
    AppendResult result;
    store.AppendAsync(idA,StableMarkId(idA,0),1,idB,empty,Capture(),[&](auto value){result=value;});
    store.WaitForIdleForTesting();
    Check(result.status==AppendStatus::kCommitted&&store.Collection(idA).shots.size()==2&&
      store.MarkSnapshots(idA).marks.size()==1,"empty appended shot persists without allocating a global mark");
    auto image=store.RequestShotAnnotated(idA,idB);store.WaitForIdleForTesting();image=store.RequestShotAnnotated(idA,idB);
    Check(image.state==ReadyAssetState::kReady&&image.bytes&&!image.bytes->empty(),"empty shot still has a preview image");
    auto metadata=PendingReference(idA);ReferenceStoreTestHooks hooks;hooks.fail_save=true;store.SetTestHooks(hooks);
    store.AppendAsync(idA,StableMarkId(idA,0),2,idC,metadata,Capture(),[&](auto value){result=value;});store.WaitForIdleForTesting();
    Check(result.status==AppendStatus::kIOError&&store.Collection(idA).revision==2,"save failure preserves collection");
    store.SetTestHooks({});AppendResult competingA,competingB;
    store.AppendAsync(idA,StableMarkId(idA,0),2,idE,metadata,Capture(),[&](auto value){competingA=value;});
    store.AppendAsync(idA,StableMarkId(idA,0),2,idF,metadata,Capture(),[&](auto value){competingB=value;});
    store.WaitForIdleForTesting();
    Check((competingA.status==AppendStatus::kCommitted&&competingB.status==AppendStatus::kStale)||
      (competingB.status==AppendStatus::kCommitted&&competingA.status==AppendStatus::kStale),"concurrent expected revision permits exactly one complete append");
    const auto accepted=store.LookupMetadata(idA)->accepted_utc_us;hooks={};
    hooks.now=[=]{auto value=Now();value.utc_us=accepted+8LL*24*60*60*1'000'000;return value;};store.SetTestHooks(hooks);
    store.AppendAsync(idA,StableMarkId(idA,0),2,idD,metadata,Capture(),[&](auto value){result=value;});store.WaitForIdleForTesting();
    Check(result.status==AppendStatus::kUnavailable&&store.LookupMetadata(idA)->state==ReferenceJobState::kExpired,"expiry wins append and append never renews retention");
  }
  {
    ReferenceStore store(temp.path/"corrupt");MarkController marks(store,[](const auto&){return true;});Commit(store,marks);
    std::ofstream manifest(temp.path/"corrupt"/(idA+".collection"));manifest<<"bad";manifest.close();
    Check(store.Collection(idA).status==MarkNotesStatus::kCorrupt&&store.MarkSnapshots(idA).status==MarkNotesStatus::kCorrupt,"corrupt manifest never falls back to first image");
  }
}


void RejectedPrivateObservationCoordinates() {
  TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});
  auto context=ContextFixture();context.observed=Now();context.title={1,TitleAvailability::kReady};
  context.native_window={1,700,"owned-private-order",1};
  marks.SetViewportContinuity(1,true);Observe(marks,context);
  const auto generation=marks.Begin(idA);Check(marks.BindContext(generation,context),"rejected-private ordinary context binds");
  auto pixels=Capture();pixels.requested=Now();pixels.completed=Now();marks.Captured(generation,pixels);
  marks.Release(generation,Path(),Now());store.WaitForIdleForTesting();
  const auto original=store.Collection(idA).shots.front().metadata;
  auto other=context;other.window_id+=2;other.window_title="Other saved source";other.observed=Now();
  Observe(marks,other);const auto otherGeneration=marks.Begin(idB);
  Check(marks.BindContext(otherGeneration,other),"rejected-private other saved source binds");
  pixels=Capture();pixels.requested=Now();pixels.completed=Now();marks.Captured(otherGeneration,pixels);
  marks.Release(otherGeneration,Path(),Now());store.WaitForIdleForTesting();Observe(marks,context);
  auto target=marks.BeginOwnUIPresentationSource();Check(target.has_value(),"rejected-private saved source query exists");
  marks.SetOwnInputFocus(true);marks.SetWindowObservation({WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()});
  Check(marks.PublishOwnUIPresentationObservation(*target,WindowFor(context,100),{}),"rejected-private valid observation accepted");
  for(int fault=0;fault<3;++fault) {
    auto rejected=WindowFor(context,fault==0?99:101);rejected.bounds.width+=1;
    if(fault==1)rejected.observed.monotonic_us=0;
    auto staleTarget=*target;if(fault==2)++staleTarget.epoch;
    Check(!marks.PublishOwnUIPresentationObservation(staleTarget,rejected,{}),"rejected-private stale generation/time/epoch rejected");
    Check(marks.SavedProjection(*original)==SavedProjectionState::kVisible&&marks.own_ui_presentation_source()->generation==target->generation,
        "rejected private envelope preserves current valid observation and saved coordinates");
  }
  marks.SetOwnInputFocus(false);Observe(marks,context);marks.EndOwnUIPresentationSource();
  Check(marks.SavedProjection(*original)==SavedProjectionState::kVisible&&marks.LiveShots(idA).size()==1,
      "QA REJECTED private observation cannot permanently invalidate saved coordinates before freshness rejection");
  target=marks.BeginOwnUIPresentationSource();marks.SetOwnInputFocus(true);
  auto unrelated=WindowFor(other,150);unrelated.bounds.width+=1;
  Check(!marks.PublishOwnUIPresentationObservation(*target,unrelated,{}),"private callback from other source cannot satisfy requested identity");
  marks.SetOwnInputFocus(false);Observe(marks,other);marks.EndOwnUIPresentationSource();
  Check(marks.LiveShots(idB).size()==1,"wrong-source private callback cannot permanently invalidate another saved source");
  Observe(marks,context);target=marks.BeginOwnUIPresentationSource();marks.SetOwnInputFocus(true);
  auto changed=WindowFor(context,200);changed.bounds.width+=1;
  Check(!marks.PublishOwnUIPresentationObservation(*target,changed,{}),"genuinely new private geometry mismatch rejects publication");
  marks.SetOwnInputFocus(false);Observe(marks,context);marks.EndOwnUIPresentationSource();
  Check(marks.SavedProjection(*original)==SavedProjectionState::kVisible&&marks.LiveShots(idA).size()==1,
      "fresh exact original return restores saved geometry after genuine private move/resize hide");
}

void ExplicitContextScopes() {
  auto legacy=PendingReference(idA);
  const auto oldBytes=Serialize(legacy);
  const auto old=Deserialize(oldBytes);
  Check(old&&old->context.scope.version==0&&
      old->context.scope.kind==CaptureScopeKind::kLegacy&&Serialize(*old)==oldBytes,
      "absent scope remains byte-identical legacy rather than application fallback");
  auto application=legacy;
  application.context.scope={1,CaptureScopeKind::kApplication,
      application.context.displays.front().id,700,true};
  const auto restored=Deserialize(Serialize(application));
  Check(restored&&restored->context.scope.version==1&&
      restored->context.scope.kind==CaptureScopeKind::kApplication&&
      restored->context.scope.display_id==application.context.scope.display_id&&
      restored->context.scope.process_start_identity_us==700,
      "explicit application scope survives actual production serialization");
  application.context.window_id=0;application.context.window_pid=0;application.context.window={};
  application.context.window_title.clear();application.context.selection_method="frontmost-application/selected-display";
  auto current=WindowFor(legacy.context,1);
  current.application_available=true;current.application_start_identity_us=700;
  Check(ReferenceVisible(application.context,{},current,{}),
      "application scope has no forged window ID and is visible in its current application instance");
  current.window_id+=10;current.bounds.x+=20;
  Check(ReferenceVisible(application.context,{},current,{}),
      "application scope remains fixed-screen across windows of the same application instance");
  ++current.application_start_identity_us;
  Check(!ReferenceVisible(application.context,{},current,{}),
      "application process replacement cannot revive application scope");
  auto chrome=legacy.context;chrome.bundle_id="com.google.Chrome";chrome.window_title.clear();
  auto observed=WindowFor(chrome,2);
  Check(!ReferenceVisible(chrome,{},observed,{}),
      "legacy Chrome missing page identity never silently becomes window scope");
  chrome.native_window={1,700,"scope-test",1};
  observed=WindowFor(chrome,2);
  chrome.scope={1,CaptureScopeKind::kWindow,chrome.displays.front().id,700,true};
  Check(ReferenceVisible(chrome,{},observed,{}),
      "new explicit Chrome window scope permits basic capture without page authorization");
  auto titled=legacy.context;titled.native_window={1,700,"scope-test",1};titled.title={1,TitleAvailability::kReady};
  titled.scope={1,CaptureScopeKind::kWindow,1,700,true,true};auto fresh=WindowFor(titled,3);
  Check(ReferenceVisible(titled,{},fresh,{}),"new window with reliable captured title is visible");
  fresh.window_title="another source";
  Check(!ReferenceVisible(titled,{},fresh,{}),"reliable normal-window title remains an exact constraint");
  titled.window_title.clear();titled.title={1,TitleAvailability::kUnavailable};titled.scope.title_required=false;
  fresh=WindowFor(titled,4);
  Check(ReferenceVisible(titled,{},fresh,{}),"new explicit window with genuinely absent title remains usable without invented title evidence");
  titled.scope={};Check(!ReferenceVisible(titled,{},fresh,{}),"missing title does not broaden a legacy record");
  std::cout<<checks<<" explicit context scope persistence and visibility checks passed\n";
}

void TypedProcessIdentityVisibility() {
  auto reference=PendingReference(idA);auto& launch=reference.context;
  launch.native_window={1,1000000,"typed-test",1};launch.title={1,TitleAvailability::kReady};
  launch.scope={1,CaptureScopeKind::kWindow,1,1000000,true,true};
  const auto old=Serialize(reference);Check(Deserialize(old)&&Serialize(*Deserialize(old))==old,"existing tail6/v1 canonical bytes stay unchanged");
  auto kernel=launch;kernel.native_window.version=2;kernel.scope.version=2;kernel.scope.process_source=ProcessStartSource::kKernelBSDStart;
  auto current=WindowFor(launch,1);current.application_available=true;current.application_start_identity_us=1000000;
  current.kernel_application_start_identity_us=1000000;current.kernel_native_window=kernel.native_window;
  Check(ReferenceVisible(launch,{},current,{})&&ReferenceVisible(kernel,{},current,{}),"two typed sources can coexist even when their numbers happen to coincide");
  current.native_window={1,0,{},0};current.application_start_identity_us=0;
  Check(!ReferenceVisible(launch,{},current,{})&&ReferenceVisible(kernel,{},current,{}),"old launchDate absence cannot borrow equal kernel value");
  current.native_window=launch.native_window;current.native_window.process_start_identity_us=2000000;current.application_start_identity_us=2000000;
  Check(ReferenceVisible(kernel,{},current,{}),"later differing launchDate cannot rebind a kernel-shot identity");
  current.kernel_native_window={2,0,{},0};current.kernel_application_start_identity_us=0;
  Check(!ReferenceVisible(kernel,{},current,{}),"kernel source absence hides only its records without borrowing launchDate");
  current.kernel_native_window=kernel.native_window;current.kernel_application_start_identity_us=1000000;
  Check(ReferenceVisible(kernel,{},current,{}),"fresh same kernel identity restores the original mark");
  current.kernel_native_window.process_start_identity_us++;
  Check(!ReferenceVisible(kernel,{},current,{}),"same PID replacement kernel start never matches old shot");
  auto typed=reference;typed.context=kernel;const auto bytes=Serialize(typed);const auto decoded=Deserialize(bytes);
  Check(decoded&&decoded->context.scope==kernel.scope&&Serialize(*decoded)==bytes,"kernel v2 source is explicit in canonical tail and survives roundtrip");
  auto shortBytes=bytes;shortBytes.pop_back();Check(!Deserialize(shortBytes),"missing source field cannot downgrade to old launchDate");
  typed.context.scope.process_source=static_cast<ProcessStartSource>(99);
  const auto unknown=Deserialize(Serialize(typed));Check(!unknown||!ValidCaptureScope(unknown->context),"unknown v2 source fails closed rather than becoming legacy");
  auto chrome=kernel;chrome.bundle_id="com.google.Chrome";chrome.scope.kind=CaptureScopeKind::kPage;
  Check(!ValidCaptureScope(chrome),"kernel source cannot masquerade as Chrome launchDate-based page identity");
  std::cout<<checks<<" typed process source isolation and canonical checks passed\n";
}

void ExplicitScopeStoreLifecycle(bool kernel=false) {
  TempDirectory temp;const auto directory=temp.path/"scoped";
  const std::int64_t start=kernel?1000000:700;
  for(const auto kind:kernel?std::vector<CaptureScopeKind>{CaptureScopeKind::kWindow,CaptureScopeKind::kApplication}:std::vector<CaptureScopeKind>{CaptureScopeKind::kPage,CaptureScopeKind::kWindow,CaptureScopeKind::kApplication}) {
    const auto offset=static_cast<unsigned>(kind)*10;const auto root=Identifier(6100+offset);
    auto context=ContextFixture();context.native_window={kernel?2u:1u,start,"scope-test",1};
    context.scope={kernel?2u:1u,kind,1,start,true,kind==CaptureScopeKind::kWindow};
    if(kernel)context.scope.process_source=ProcessStartSource::kKernelBSDStart;
    if(kind==CaptureScopeKind::kWindow)context.title={1,TitleAvailability::kReady};
    if(kind==CaptureScopeKind::kPage)context.bundle_id="com.google.Chrome";
    if(kind==CaptureScopeKind::kApplication) {
      context.window_pid=0;context.window_id=0;context.window={};context.window_title.clear();
      context.selection_method="frontmost-application/selected-display";context.native_window={};
    }
    const auto observe=[&](MarkController& marks) {
      auto window=WindowFor(context,marks.window_observation().generation+1);
      window.pid=context.pid;window.application_available=true;window.application_start_identity_us=kernel?0:start;
      if(kernel){window.kernel_native_window=context.native_window;window.native_window={1,0,{},0};window.kernel_application_start_identity_us=start;}
      marks.SetWindowObservation(window);marks.SetDisplayObservation(context.displays);
      if(kind==CaptureScopeKind::kPage) {
        seethis::platform::ChromePageSample sample;sample.availability=PageAvailability::kReady;
        sample.browser_pid=context.pid;sample.window_id=context.window_id;sample.window_bounds=context.window;
        sample.process_start_identity_us=700;sample.opaque_tab_id="scope-owned-tab";sample.url="https://example.test/scope";
        marks.SetPageObservation({PageAvailability::kReady,marks.page_observation().generation+1,Now(),
            seethis::platform::MakeChromePageIdentity(sample)});
      }
    };
    {
      ReferenceStore store(directory);MarkController marks(store,[](const auto&){return true;});
      marks.SetViewportContinuity(1,true);observe(marks);
      for(unsigned round=0;round<2;++round) {
        const auto id=Identifier(6100+offset+round);const auto generation=marks.Begin(id);
        Check(marks.BindContext(generation,context),"explicit scope production controller admission");
        marks.Captured(generation,Capture());
        marks.Release(generation,std::vector<std::vector<DisplayPoint>>{Path(),Path(10)},Stamp{1'000'002,1002});
        store.WaitForIdleForTesting();
        Check(store.LookupMetadata(id)->state==ReferenceJobState::kReady,
            "page/window/app consecutive multi-stroke capture reaches Ready without page wait for basic scopes");
        Check(store.Collection(id).shots.front().metadata->context.scope==context.scope,
            "scope remains frozen in finalized production metadata");
      }
      Check(store.UpdateMarkNote(root,StableMarkId(root,0),"scope note",0).status==MarkNoteUpdateStatus::kUpdated,
          "scoped mark note persists through production note writer");
      auto appended=context;appended.scope={kernel?2u:1u,CaptureScopeKind::kWindow,1,start,true};
      if(kernel)appended.scope.process_source=ProcessStartSource::kKernelBSDStart;
      appended.window_pid=appended.pid;appended.window_id=500;appended.window={0,0,100,100};
      appended.window_title="Original window";appended.native_window={kernel?2u:1u,start,"scope-test",1};
      appended.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
      auto window=WindowFor(appended,marks.window_observation().generation+1);
      window.application_available=true;window.application_start_identity_us=kernel?0:start;
      if(kernel){window.kernel_native_window=appended.native_window;window.native_window={1,0,{},0};window.kernel_application_start_identity_us=start;}
      marks.SetWindowObservation(window);
      const auto generation=marks.BeginAppendReference(root,1,Identifier(6102+offset));
      Check(generation&&marks.BindContext(generation,appended),"Append binds its independent frozen scope");
      marks.Captured(generation,Capture());marks.Release(generation,Path(),Stamp{1'000'002,1002});store.WaitForIdleForTesting();
      const auto collection=store.Collection(root);const auto notes=store.MarkSnapshots(root);
      Check(collection.shots.size()==2&&collection.shots.front().metadata->context.scope.kind==kind&&
          collection.shots.back().metadata->context.scope.kind==CaptureScopeKind::kWindow&&
          notes.marks.size()==3&&notes.marks[0].note=="scope note"&&notes.marks[2].number==3,
          "mixed-scope Append preserves each shot, original note and global numbering");
      if(kind==CaptureScopeKind::kApplication) {
        observe(marks);Check(!marks.marks().empty(),"application index selects current instance/display with no window ID");
        auto other=window;other.pid+=1;other.generation=marks.window_observation().generation+1;marks.SetWindowObservation(other);
        Check(marks.marks().empty(),"application switch hides ink and its shared source eligibility");
        observe(marks);Check(!marks.marks().empty(),"fresh A after B restores application marks");
        marks.SetPresentationPermitted(false);Check(marks.marks().empty(),"global hide gates application projection");
        marks.SetPresentationPermitted(true);Check(!marks.marks().empty(),"global show restores application projection");
        marks.SetDisplayObservation({});Check(marks.marks().empty(),"missing display never falls back application projection");
        marks.SetDisplayObservation(context.displays);
        const auto target=marks.BeginOwnUIPresentationSource();
        Check(target&&target->context.scope.kind==CaptureScopeKind::kApplication,"own Inspector source preserves explicit app identity without a native window");
        marks.SetOwnInputFocus(true);
        auto owned=WindowFor(context,marks.window_observation().generation+10);
        owned.pid=context.pid;owned.application_available=true;owned.application_start_identity_us=kernel?0:start;
        if(kernel)owned.kernel_application_start_identity_us=start;
        owned.availability=WindowAvailability::kUnavailable;
        Check(marks.PublishOwnUIPresentationObservation(*target,owned,{}),"own UI fresh application observation uses original process instance/display scope");
        Check(!marks.marks().empty(),"app ink and shared note eligibility survive legitimate own Inspector ownership");
        marks.SetOwnInputFocus(false);marks.EndOwnUIPresentationSource();observe(marks);
      }
    }
    {
      ReferenceStore reopened(directory);reopened.WaitForIdleForTesting();
      const auto collection=reopened.Collection(root);const auto notes=reopened.MarkSnapshots(root);
      Check(collection.status==MarkNotesStatus::kReady&&collection.shots.size()==2&&
          collection.shots.front().metadata->context.scope==context.scope&&notes.marks[0].note=="scope note",
          "Ready v10 cold restart preserves per-shot scope and note");
      Check(reopened.Delete(root)==DeleteResult::kDeleted&&reopened.Collection(root).status==MarkNotesStatus::kDeleted,
          "whole-reference deletion removes mixed-scope collection");
    }
  }
  std::cout<<checks<<" explicit scope store/controller lifecycle checks passed\n";
}

void ColdCorruptPresentationWithdrawal() {
  for(const auto schema:{1u,2u}) {
    TempDirectory temp;
    {
      ReferenceStore store(temp.path);
      auto reference=PendingReference(idA);reference.schema=schema;
      if(schema==2) {
        reference.capture_display_id=1;reference.path.clear();reference.regions={{Path(),{},{},{}}};
        reference.capture_api="ScreenCaptureKit.SCScreenshotManager.selected-display";
        reference.provenance="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id";
      }
      Check(store.AcceptPending(reference,0,0).state==ReferenceJobState::kPending,"cold corruption fixture accepted");
      store.FinalizeAsync(idA,Capture());store.WaitForIdleForTesting();
      Check(store.LookupMetadata(idA)->state==ReferenceJobState::kReady,"cold corruption fixture persisted ready");
    }
    ReferenceStore store(temp.path);store.WaitForIdleForTesting();
    Check(!store.Jobs().front().ready,"cold reopened fixture has no hydrated original payload");
    MarkController marks(store,[](const auto&){return true;});marks.SetViewportContinuity(1,true);Observe(marks,ContextFixture());
    Check(marks.marks().size()==1&&marks.LiveMarkSnapshots(idA).marks.size()==1&&
        marks.HitIdentity(Path().front(),2,ContextFixture().displays)==idA,"cold valid metadata initially supplies ink number and hit");
    {std::ofstream corrupt(temp.path/(idA+".stref"));corrupt<<"corrupt original fixture";}
    {std::ofstream corrupt(temp.path/(idA+".source.png"));corrupt<<"corrupt original fixture";}
    const auto discovered=store.Lookup(idA);
    Check(discovered&&discovered->state==ReferenceJobState::kFailed&&discovered->code=="corrupt",
        "cold hydration explicitly discovers original corruption");
    const auto absent=[&] {
      return !store.ReadPresentation(idA)&&marks.marks().empty()&&marks.jobs().empty()&&
          marks.presentation_jobs().empty()&&marks.LiveMarkSnapshots(idA).marks.empty()&&
          !marks.HitIdentity(Path().front(),2,ContextFixture().displays)&&!marks.HitJob(Path().front(),2,ContextFixture().displays);
    };
    Check(absent(),"discovered corrupt original withdraws ink number and every ready/failed fallback hit immediately");
    auto other=ContextFixture();other.window_id+=1;other.window_title="B";Observe(marks,other);Observe(marks,ContextFixture());
    Check(absent(),"A to B to A cannot resurrect discovered corrupt ink numbers or hits");
    const auto history=marks.inspector_jobs();
    Check(history.size()==1&&history.front().state==ReferenceJobState::kFailed&&history.front().code=="corrupt",
        "corrupt durable reference status remains available to Inspector history");
    const auto pending=store.AcceptPending(PendingReference(idB),0,0);
    Check(pending.state==ReferenceJobState::kPending&&store.ReadPresentation(idB)->notes.status==MarkNotesStatus::kPending,
        "corruption withdrawal preserves legitimate pending note publication");
    store.Fail(idB,"capture_failed");store.WaitForIdleForTesting();
    Check(store.ReadPresentation(idB)&&store.ReadPresentation(idB)->job.state==ReferenceJobState::kFailed&&
        marks.jobs().size()==1,"legitimate capture failure keeps existing presentation fallback while corrupt reference stays withdrawn");
  }
  std::cout<<"Cold original corruption publication withdrawal checks passed for schema1 and2\n";
}

void ActivePresentationPublication() {
  TempDirectory temp;ReferenceStore store(temp.path);MarkController marks(store,[](const auto&){return true;});
  const auto pending=store.AcceptPending(PendingReference(Identifier(999)),0,0);
  const auto pendingPresentation=store.ReadPresentation(Identifier(999));
  Check(pending.state==ReferenceJobState::kPending&&pendingPresentation&&pendingPresentation->notes.status==MarkNotesStatus::kPending&&
      pendingPresentation->notes.marks.size()==1,"pending note publication preserves stable owner metadata before image completion");
  store.Fail(Identifier(999),"fixture-complete");store.WaitForIdleForTesting();
  Commit(store,marks);
  const auto original=store.ReadPresentation(idA);
  Check(original&&original->notes.status==MarkNotesStatus::kReady&&original->notes.marks.size()==1,
      "presentation publishes validated ready membership and precomputed number metadata");
  const auto revision=store.ReadPresentation()->revision;
  (void)store.Collection(idA);(void)store.MarkSnapshots(idA);
  Check(store.ReadPresentation()->revision==revision,"unchanged durable read does not rebuild current source publication");
  Check(store.UpdateMarkNote(idA,StableMarkId(idA,0),"cached note",0).status==MarkNoteUpdateStatus::kUpdated,
      "presentation note fixture saves through real durable API");
  Check(store.ReadPresentation(idA)->notes.marks.front().note=="cached note"&&original->notes.marks.front().note.empty(),
      "note publication changes atomically while older snapshot stays immutable");
  Check(store.UpdateMarkVisibility(idA,StableMarkId(idA,0),false,1).status==MarkNoteUpdateStatus::kUpdated&&
      !store.ReadPresentation(idA)->notes.marks.front().visible,"visibility edit immediately updates published metadata");
  ReferenceStoreTestHooks uncertain;uncertain.fail_note_after_rename=true;store.SetTestHooks(uncertain);
  const auto uncertainResult=store.UpdateMarkNote(idA,StableMarkId(idA,0),"cached note",2);
  Check(uncertainResult.status==MarkNoteUpdateStatus::kCommitUncertain&&store.ReadPresentation(idA)->notes.revision==3&&
      store.ReadPresentation(idA)->notes.marks.front().note=="cached note","uncertain note rename republishes validated readback atomically");
  store.SetTestHooks({});
  auto other=ContextFixture();other.window_id+=10;other.window_title="other current source";
  for(unsigned index=0;index<15;++index) {
    char identity[33];std::snprintf(identity,sizeof(identity),"%032x",index+100);
    auto metadata=PendingReference(identity);metadata.context=other;
    AppendResult result;store.AppendReferenceAsync(idA,index+1,identity,metadata,Capture(),[&](auto value){result=value;});
    store.WaitForIdleForTesting();Check(result.status==AppendStatus::kCommitted,"mixed-source synthetic append commits");
  }
  Observe(marks,ContextFixture());
  Check(marks.marks().size()==1&&marks.LiveShots(idA).size()==1&&marks.LiveMarkSnapshots(idA).marks.size()==1,
      "current source A excludes every unrelated shot within a shared appended root");
  auto unavailable=WindowObservation{WindowAvailability::kUnavailable,marks.window_observation().generation+1,Now()};
  marks.SetWindowObservation(unavailable);Check(marks.marks().empty(),"unavailable current observation hides cached ink");
  auto rejected=WindowFor(other,unavailable.generation);marks.SetWindowObservation(rejected);
  Check(marks.marks().empty(),"same-generation fresh payload cannot replace an admitted unavailable observation");
  Observe(marks,other);Check(marks.marks().size()==15&&marks.LiveMarkSnapshots(idA).marks.size()==15,
      "higher-generation source B restores all and only its fifteen appended shots");
  Observe(marks,ContextFixture());Check(marks.marks().size()==1&&marks.LiveMarkSnapshots(idA).marks.front().note=="cached note",
      "A to B to A restores immutable coordinates and edited note");
  std::mutex mutex;std::condition_variable condition;bool entered=false,release=false;
  ReferenceStoreTestHooks hooks;hooks.stage=[&](std::string_view stage,std::string_view) {
    if(stage!="append_before_publish")return;
    std::unique_lock lock(mutex);entered=true;condition.notify_all();condition.wait(lock,[&]{return release;});
  };store.SetTestHooks(hooks);
  auto metadata=PendingReference(idF);AppendResult result;
  store.AppendReferenceAsync(idA,16,idF,metadata,Capture(),[&](auto value){result=value;});
  {std::unique_lock lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"controlled append owns persistence lock");}
  const auto began=std::chrono::steady_clock::now();
  // A watchdog releases the worker even if this regression reintroduces IO.
  std::thread watchdog([&]{std::unique_lock lock(mutex);condition.wait_for(lock,std::chrono::milliseconds(200),[&]{return release;});release=true;condition.notify_all();});
  for(int sample=0;sample<20;++sample) {
    marks.Expire(Now());Check(marks.marks().size()==1&&marks.HitIdentity(Path().front(),2,ContextFixture().displays)==idA,
        "pointer and deadline processing consume previous valid publication during held append persistence");
  }
  const auto elapsed=std::chrono::steady_clock::now()-began;
  {std::lock_guard lock(mutex);release=true;}condition.notify_all();watchdog.join();store.WaitForIdleForTesting();store.SetTestHooks({});
  Check(elapsed<std::chrono::milliseconds(100),"current-source event processing never waits for collection persistence lock");
  Check(result.status==AppendStatus::kCommitted&&marks.marks().size()==2,"completed append publishes new current shot atomically");
  {std::ofstream corrupt(temp.path/(idA+".collection"));corrupt<<"malicious invalid manifest";}
  Check(store.Collection(idA).status==MarkNotesStatus::kCorrupt&&store.ReadPresentation(idA)->notes.status==MarkNotesStatus::kCorrupt&&marks.marks().empty(),
      "explicit validation failure withdraws all cached ink and number metadata");
  Check(store.Delete(idA)==DeleteResult::kDeleted&&!store.ReadPresentation(idA)&&marks.marks().empty(),
      "delete withdraws publication and prevents cached mark resurrection");
  Observe(marks,other);Observe(marks,ContextFixture());Check(marks.marks().empty(),"deleted publication stays absent across source return");
  Commit(store,marks,idB);const auto accepted=store.LookupMetadata(idB)->accepted_utc_us;
  ReferenceStoreTestHooks expired;expired.now=[=]{return Stamp{accepted+8LL*24*60*60*1'000'000,Now().monotonic_us};};store.SetTestHooks(expired);
  Check(!store.ReadPresentation(idB)&&marks.marks().empty(),"cached metadata obeys retention deadline before a pruning sweep");
  store.SetTestHooks({});
  std::cout<<"Active presentation publication/current-shot/held-IO regression checks passed\n";
}

}
int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string_view(argv[1])=="--finder-instance-only") {TypedProcessIdentityVisibility();ExplicitScopeStoreLifecycle(true);CapturedSourceContext();SourceContextLegacySidecars();TitleScopedVisibility();NativeWindowLifetimes();return 0;}
    if(argc==2&&std::string_view(argv[1])=="--context-scope-component-only") {
      ExplicitContextScopes();ExplicitScopeStoreLifecycle();return 0;
    }
    ColdCorruptPresentationWithdrawal();
    if(argc==2&&std::string_view(argv[1])=="--cold-corrupt-presentation-component-only")return 0;
    ActivePresentationPublication();
    if(argc==2&&std::string_view(argv[1])=="--active-presentation-component-only")return 0;
    StatusURLActionBoundaries();FailedActionCustodyLifecycle();
    if(argc==2&&std::string_view(argv[1])=="--f3-deadline-component-only") {Transactions();SeparatedCaptureDeadlines();std::cout<<checks<<" F3 deadline checks passed\n";return 0;}
    if(argc==2&&std::string_view(argv[1])=="--status-action-component-only") {
      std::cout<<checks<<" status action checks passed\n";return 0;
    }
#ifdef __APPLE__
    if(argc==2&&std::string_view(argv[1])=="--chrome-provider-component-only") {
      ChromeProductionAdapter();std::cout<<checks<<" Chrome production adapter checks passed\n";return 0;
    }
#endif
    if(argc==2&&std::string_view(argv[1])=="--own-source-component-only"){OwnUIPresentationContinuity();return 0;}
    RejectedPrivateObservationCoordinates();
    if(argc==2&&std::string_view(argv[1])=="--rejected-private-component-only")return 0;
    UnifiedSavedProjectionTransitions();
    if(argc==2&&std::string_view(argv[1])=="--unified-projection-component-only")return 0;
    OwnUIQueuedAppendCompletion();OwnUIPresentationContinuity();
    if(argc==2&&std::string_view(argv[1])=="--p4-component-only") {
      TitleScopedVisibility();NativeWindowLifetimes();
      PersistentPresentationCheckpoints();MultiShotCollections();MultiShotControllerTransactions();MultiShotReworkBoundaries();Transactions();NumberedNotesLifecycle();MarkVisibilityLifecycle();
      std::cout<<checks<<" P4 owned component checks passed\n";return 0;
    }
    TitleScopedVisibility();NativeWindowLifetimes();CapturedSourceContext();SourceContextLegacySidecars();DisplayUnderlayPixels();DisplayUnderlayMixedProgress();DisplayUnderlayQueueSaturation();DisplayCropAbandonedProgress();DisplayUnderlayLifecycle();NumberedRetinaReadability();Transactions();PersistenceAndClipboard();AsyncJobs();Deletion();Retention();LegacyReadyMigration();GeometryAndCanonical();NativeAsymmetricCrop();MultiRegionReference();AnnotatedDeletionRace();RepresentativeCompressedCodec();PipelineLatencyEvidence();ExplicitSubpathRoundTrip();NumberedNotesLifecycle();MarkVisibilityLifecycle();
#ifdef __APPLE__
    ChromeProductionAdapter();
#endif
    WindowVisibility();ChromePageVisibility();std::cout<<checks<<" reference checks passed\n";return 0;}
  catch(const std::exception& e){std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
