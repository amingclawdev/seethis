#include "core/reference.h"
#include "platform/platform.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iostream>
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
  return observation;
}
void Observe(MarkController& marks,const Context& context) {
  marks.SetWindowObservation(
      WindowFor(context,marks.window_observation().generation+1));
}
std::uint64_t Begin(MarkController& marks,std::string id=idA) {
  const auto context=ContextFixture();Observe(marks,context);
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
            PixelAt(*annotated_pixels,100,70)==PixelAt(*original_pixels,100,70)&&
            PixelAt(*annotated_pixels,166,40)==PixelAt(*original_pixels,166,40),
        "2x negative-origin mapping preserves separate freehand subpaths without a synthetic chord");
  MarkController restarted_marks(restarted, [](const auto&) { return true; });
  Observe(restarted_marks,context);
  Check(!restarted_marks.HitIdentity(point(83.3333333333, 60), 0.25,
                                     context.displays),
        "restart hit testing rejects the synthetic exit-to-top chord");
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
            success_observation.generation==11 && success_observation.identity,
        "production Chrome adapter publishes one bounded success");

  int timeout_count=0;PageObservation timeout_observation;
  seethis::platform::AcquireChromePageAsync(
      [] { std::this_thread::sleep_for(std::chrono::milliseconds(450));
           return seethis::platform::ChromePageSample{}; },12,
      [&](PageObservation observation) {
        std::lock_guard lock(mutex);++timeout_count;
        timeout_observation=std::move(observation);
      });
  pump(0.7);
  Check(timeout_count==1 && timeout_observation.availability==PageAvailability::kTimedOut &&
            timeout_observation.generation==12,
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
}
#endif

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
  mismatch=WindowFor(context,marks.window_observation().generation+1);
  mismatch.bounds.x+=1;marks.SetWindowObservation(mismatch);
  Check(marks.marks().empty(),"native exact bounds mismatch hides the mark");

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
}
int main() {
  try {Transactions();PersistenceAndClipboard();AsyncJobs();Deletion();Retention();LegacyReadyMigration();GeometryAndCanonical();NativeAsymmetricCrop();MultiRegionReference();AnnotatedDeletionRace();RepresentativeCompressedCodec();PipelineLatencyEvidence();ExplicitSubpathRoundTrip();
#ifdef __APPLE__
    ChromeProductionAdapter();
#endif
    WindowVisibility();ChromePageVisibility();std::cout<<checks<<" reference checks passed\n";return 0;}
  catch(const std::exception& e){std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
