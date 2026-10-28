// Compile the real action methods and their adapters; do not start the app.
#import "../src/platform/mac/input_mac.mm"
#import "../src/platform/mac/overlay_mac.mm"
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <sys/proc_info.h>

using namespace seethis::platform;

namespace seethis::platform {
bool ChromeFrontDescriptorsMatchForTesting(const void* first,const void* last,double top);
using ChromeFrontSend = std::function<NSAppleEventDescriptor*(NSAppleEventDescriptor*,NSAppleEventSendOptions,NSTimeInterval,NSError**)>;
bool ChromeFrontPropertiesReadForTesting(const ChromeFrontSend&,const void* expected,int budget_ms,bool twice);
}

static void Require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
template<class Predicate> static void PumpUntil(Predicate predicate,int line=__builtin_LINE()) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!predicate() && std::chrono::steady_clock::now()<deadline)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,false);
  if(!predicate())throw std::runtime_error("main run-loop test deadline exceeded at line "+std::to_string(line));
}

struct NativeFake {
  std::mutex mutex;
  std::condition_variable ready;
  ChromeRunningTarget target{123,1000};
  ChromeRunningTarget foreground{123,1000};
  ChromeProviderState provider=ChromeProviderState::kReady;
  ChromePermissionResult preflight{ChromeConsent::kNotRequested,-1744};
  ChromePermissionResult result{ChromeConsent::kGranted,0};
  bool hold_native=true;
  bool hold_discovery=false;
  bool hold_preflight=false;
  bool hold_provider=false;
  bool hold_captured_preflight=false;
  int capture_preflight_at=0;
  std::atomic<bool> captured_preflight_entered=false;
  bool publish_result=true;
  std::atomic<int> asks=0,worker_discoveries=0,probes=0,preflights=0;
  std::atomic<int> native_active=0,native_max=0;
  struct Flight {
    NativeFake& fake;
    explicit Flight(NativeFake& value):fake(value) {
      const int active=++fake.native_active;
      int maximum=fake.native_max.load();
      while(active>maximum && !fake.native_max.compare_exchange_weak(maximum,active)){}
    }
    ~Flight(){--fake.native_active;}
  };
  std::atomic<bool> wrong_thread=false;
  ChromeRunningTarget Discover() {
    std::unique_lock lock(mutex);
    if(!NSThread.isMainThread) {
      ++worker_discoveries;
      ready.wait(lock,[&]{return !hold_discovery;});
    }
    return target;
  }
  ChromePermissionResult Permission(std::int64_t pid,bool ask) {
    if(NSThread.isMainThread)wrong_thread=true;
    Flight flight(*this);
    std::unique_lock lock(mutex);
    if(!ask) {
      const int call=++preflights;
      if(call==capture_preflight_at) {
        // Model a native call that captured old permission before blocking;
        // changing preflight later must not change this returned value.
        const auto captured=preflight;captured_preflight_entered=true;
        ready.wait(lock,[&]{return !hold_captured_preflight;});return captured;
      }
      ready.wait(lock,[&]{return !hold_preflight;});return preflight;
    }
    ++asks;
    Require(pid==123,"native permission used unexpected PID");
    ready.wait(lock,[&]{return !hold_native;});
    if(publish_result)preflight=result;
    return result;
  }
  ChromeProviderState Provider() {
    if(NSThread.isMainThread)wrong_thread=true;
    Flight flight(*this);
    std::unique_lock lock(mutex);++probes;
    ready.wait(lock,[&]{return !hold_provider;});return provider;
  }
  void ReleasePreflight() {
    {std::lock_guard lock(mutex);hold_preflight=false;}ready.notify_all();
  }
  void ReleaseCapturedPreflight() {
    {std::lock_guard lock(mutex);hold_captured_preflight=false;}ready.notify_all();
  }
  void ReleaseProvider() {
    {std::lock_guard lock(mutex);hold_provider=false;}ready.notify_all();
  }
  void Release() {
    {std::lock_guard lock(mutex);hold_native=false;hold_discovery=false;
      hold_preflight=false;hold_provider=false;hold_captured_preflight=false;}
    ready.notify_all();
  }
  void Target(ChromeRunningTarget next) {std::lock_guard lock(mutex);target=next;}
  ChromeRunningTarget Foreground() {std::lock_guard lock(mutex);return foreground;}
  void Foreground(ChromeRunningTarget next) {std::lock_guard lock(mutex);foreground=next;}
};

// Only external foreground discovery differs; connect/retry/status/shortcuts
// use the actual STSystemInspectorChromeAdapter implementation.
@interface STConsentInspectorAdapter : STSystemInspectorChromeAdapter
@property(nonatomic) std::int64_t fakeForeground;
@end
@implementation STConsentInspectorAdapter
- (std::int64_t)foregroundChromePID {return _fakeForeground;}
@end

static std::shared_ptr<NativeFake> current_fake;
static std::shared_ptr<NativeFake> Configure() {
  if(current_fake)current_fake->Release();
  StopChromeConnection();
  PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
  auto fake=std::make_shared<NativeFake>();
  current_fake=fake;
  SetChromeConnectionTestAdapters({
      {[fake]{return fake->Discover();},
       [fake](std::int64_t pid,bool ask){return fake->Permission(pid,ask);}},
      [fake](ChromeRunningTarget) {return fake->Provider();},[fake]{return fake->Foreground();}});
  StartChromeConnection();RefreshChromeConnection();
  PumpUntil([&]{return ChromeConnectionStatus().permission.consent==fake->preflight.consent &&
      ChromePhysicalWorkCountForTesting()==0;});
  fake->worker_discoveries=0;
  return fake;
}
static STInspectorChromeView* View() {
  STConsentInspectorAdapter* adapter=[STConsentInspectorAdapter new];
  adapter.fakeForeground=123;
  auto* view=[[STInspectorChromeView alloc] initWithAdapter:adapter];
  view.chromeContextActive=YES;
  [view refreshChrome];return view;
}
static void RequireHidden(STInspectorChromeView* view) {
  [view refreshChrome];
  Require(view.stateLabel.hidden && view.guidanceLabel.hidden &&
      !view.shortcutLabel.hidden,"connection presentation or effective hints changed");
  for(NSButton* button in view.buttons)Require(button.hidden,"healthy/other context recovery visible");
}

static void TestActionsAndMainProgress(bool inspector_first) {
  auto fake=Configure();
  STApplicationDelegate* menu=[STApplicationDelegate new];
  auto* view=View();
  const auto start=std::chrono::steady_clock::now();
  if(inspector_first)[view connectChrome:nil];else [menu connectChrome:nil];
  Require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250),
      "production Connect action blocked main");
  PumpUntil([&]{return fake->asks==1;});
  [menu connectChrome:nil];[view connectChrome:nil];
  [menu retryChrome:nil];[view retryChrome:nil];RefreshChromeConnection();
  Require(fake->asks==1 && ChromeConnectionStatus().consent_pending,
      "entry paths failed to coalesce unresolved native request");
  [view refreshChrome];
  Require(!view.stateLabel.hidden && !view.buttons[0].enabled &&
      [view.stateLabel.stringValue containsString:@"request in progress"],
      "pending recovery description must be truthful");
  auto sentinel=std::make_shared<std::atomic_bool>(false);
  auto timer=std::make_shared<std::atomic_bool>(false);
  dispatch_async(dispatch_get_main_queue(),^{sentinel->store(true);});
  NSTimer* heartbeat=[NSTimer timerWithTimeInterval:0.01 repeats:NO
      block:^(NSTimer*){timer->store(true);}];
  [NSRunLoop.mainRunLoop addTimer:heartbeat forMode:NSRunLoopCommonModes];
  PumpUntil([&]{return sentinel->load() && timer->load();});
  Require(fake->asks==1 && ChromeConnectionStatus().consent_pending,
      "native fake unexpectedly completed before main progress assertion");
  // The OS may expose a grant through prompt-free preflight before the original
  // native call returns. Backend single-flight remains; healthy UI stays hidden.
  {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kGranted,0};}
  RefreshChromeConnection();
  PumpUntil([]{return ChromeConnectionStatus().permission.consent==ChromeConsent::kGranted;});
  RequireHidden(view);
  Require(ChromeConnectionStatus().consent_pending,"preflight grant canceled physical slot");
  [menu connectChrome:nil];Require(fake->asks==1 && fake->probes==0,
      "pending grant started another request or provider");
  fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending &&
      ChromeConnectionStatus().provider==ChromeProviderState::kReady;});
  Require(!fake->wrong_thread,"native/preflight/provider executed on wrong thread");
  RequireHidden(view);
}

static void TestQueuedStaleTarget(bool restart=false,bool quit=false) {
  auto fake=Configure();
  {std::lock_guard lock(fake->mutex);fake->hold_discovery=true;}
  STApplicationDelegate* menu=[STApplicationDelegate new];[menu connectChrome:nil];
  PumpUntil([&]{return fake->worker_discoveries>=1;});
  Require(fake->worker_discoveries<=2 && ChromePhysicalWorkCountForTesting()<=2,
      "queued discovery exceeded shared physical admission bound");
  if(restart) {StopChromeConnection();StartChromeConnection();RefreshChromeConnection();}
  else fake->Target(quit?ChromeRunningTarget{}:ChromeRunningTarget{123,2000});
  fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending && ChromePhysicalWorkCountForTesting()==0;});
  Require(fake->asks==0 && fake->probes==0,"queued stale target requested permission");
}

static void TestOutstandingInvalidation(bool restart,bool quit,bool away_back) {
  auto fake=Configure();
  STApplicationDelegate* menu=[STApplicationDelegate new];[menu connectChrome:nil];
  PumpUntil([&]{return fake->asks==1;});
  if(restart) {StopChromeConnection();StartChromeConnection();RefreshChromeConnection();}
  else {
    fake->Target(quit?ChromeRunningTarget{}:ChromeRunningTarget{123,2000});
    RefreshChromeConnection();
    if(away_back){fake->Target({123,1000});RefreshChromeConnection();}
  }
  [menu connectChrome:nil];
  Require(fake->asks==1 && ChromeConnectionStatus().consent_pending,
      "invalidation incorrectly released unresolved native slot");
  fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  Require(fake->probes==0 && ChromeConnectionStatus().provider==ChromeProviderState::kUnknown,
      "stale consent started a provider or resurrected publication");
}

static void TestOutcomes() {
  for(const auto result:{ChromePermissionFromOSStatus(-1743),
                        ChromePermissionFromOSStatus(-50),
                        ChromePermissionFromOSStatus(-600)}) {
    auto fake=Configure();
    {std::lock_guard lock(fake->mutex);fake->result=result;}
    auto* view=View();[view connectChrome:nil];
    PumpUntil([&]{return fake->asks==1;});fake->Release();
    PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
    Require(ChromeConnectionStatus().permission.consent==result.consent &&
        ChromeConnectionStatus().permission.os_status==result.os_status && fake->probes==0,
        "native outcomes collapsed or improperly probed");
  }
  auto fake=Configure();auto* view=View();[view connectChrome:nil];
  PumpUntil([&]{return fake->asks==1;});
  {std::lock_guard lock(fake->mutex);
    fake->preflight={ChromeConsent::kDenied,-1743};fake->publish_result=false;}
  fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  Require(ChromeConnectionStatus().permission.consent==ChromeConsent::kDenied && fake->probes==0,
      "late native grant overwrote current revocation");
  fake=Configure();
  {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kGranted,0};}
  STApplicationDelegate* menu=[STApplicationDelegate new];[menu connectChrome:nil];
  PumpUntil([&]{return ChromeConnectionStatus().provider==ChromeProviderState::kReady;});
  Require(fake->asks==0,"already granted Connect requested consent");
  fake=Configure();fake->Target({});[menu connectChrome:nil];
  Require(fake->asks==0 && !ChromeConnectionStatus().consent_pending,
      "absent target requested consent");
  fake->Target({123,0});[menu connectChrome:nil];
  Require(fake->asks==0,"invalid launch identity requested consent");
}

// Real input state and callback, with OS physical snapshots/registration/pointer
// injected. Events are local objects, never posted; no input adapter Start.
struct InputOverlay final : OverlayAdapter {
  seethis::core::InteractionController* controller;
  int drawing=0,finished=0,refreshes=0;
  std::function<void()> onObservation,onPointMapping;
  std::function<void(ViewportInput,const std::optional<seethis::core::DisplayPoint>&)> onViewportInput;
  explicit InputOverlay(seethis::core::InteractionController* value):controller(value) {}
  void Refresh() override {
    ++refreshes;
    const auto state=controller->Snapshot().state;
    if(state==seethis::core::InteractionState::kDrawing)++drawing;
    if(state==seethis::core::InteractionState::kFinished)++finished;
  }
  void ObserveViewportInput(ViewportInput event,const std::optional<seethis::core::DisplayPoint>& point = {}) override {
    if(onViewportInput)onViewportInput(event,point);
    if(onObservation)onObservation();
  }
  void RebuildDisplays() override {}
  void UpdatePointerPolicy(const std::optional<seethis::core::DisplayPoint>&) override {}
  std::optional<seethis::core::DisplayPoint> DisplayPointAtGlobalLogical(
      seethis::core::Point2D point) const override {
    if(onPointMapping)onPointMapping();
    return seethis::core::DisplayPoint{1,seethis::core::CoordinateUnit::kLogicalPoints,point,1};
  }
  void ToggleInspector() override {}
  bool InspectorVisible() const override {return false;}
};
struct InputFixture {
  std::filesystem::path directory=std::filesystem::current_path()/
      ("hotkey-consent-fixture-"+std::to_string(getpid()));
  seethis::core::SettingsStore settings{directory/"settings.json"};
  seethis::core::InteractionController controller;
  seethis::core::DeleteGestureController deletion;
  InputOverlay overlay{&controller};
  std::shared_ptr<PermissionReadinessState> readiness=std::make_shared<PermissionReadinessState>();
  std::array<bool,128> keys{};
  CGEventFlags flags=kCGEventFlagMaskAlternate;
  pid_t foreground=123;
  seethis::core::DeleteEventProvenance lastCarbonPress;
  std::unique_ptr<MacInputAdapter> input;
  const seethis::core::DisplayPoint pointer{1,seethis::core::CoordinateUnit::kLogicalPoints,{10,10},1};
  explicit InputFixture(std::filesystem::path owned={})
      :directory(owned.empty()?std::filesystem::current_path()/
          ("hotkey-consent-fixture-"+std::to_string(getpid())):std::move(owned)) {
    readiness->input_monitor=InputMonitorState::kGranted;
    input=std::make_unique<MacInputAdapter>(&controller,&overlay,&settings,readiness,&deletion);
    Bind();
  }
  ~InputFixture() {input.reset();std::filesystem::remove_all(directory);}
  void Bind() {
    input->ConfigureCaptureForTesting({[this](CGKeyCode key){return key<keys.size() && keys[key];},
        [this]{return flags;},[this]{return foreground;}},pointer);
  }
  OSStatus Carbon(UInt32 kind=kEventHotKeyPressed,std::optional<UInt32> generation={},
      std::optional<EventTime> time={}) {
    EventRef event=nullptr;
    Require(CreateEvent(nullptr,kEventClassKeyboard,kind,
        time.value_or(GetCurrentEventTime()+0.001),kEventAttributeNone,&event)==noErr,
        "could not create local Carbon event");
    if(kind==kEventHotKeyPressed)lastCarbonPress=CarbonEventProvenance(GetEventTime(event));
    EventHotKeyID identity{kCaptureHotKeySignature,
        generation.value_or(static_cast<UInt32>(input->CaptureGenerationForTesting()))};
    Require(SetEventParameter(event,kEventParamDirectObject,typeEventHotKeyID,
        sizeof(identity),&identity)==noErr,"could not identify Carbon event");
    const auto result=input->HotKeyForTesting(event);ReleaseEvent(event);return result;
  }
  void Raw(CGEventType type,CGKeyCode key,std::optional<std::uint64_t> timestamp={}) {
    CGEventRef event=CGEventCreateKeyboardEvent(nullptr,key,type==kCGEventKeyDown);
    Require(event!=nullptr,"could not create local keyboard event");
    CGEventSetFlags(event,flags);
    if(timestamp)CGEventSetTimestamp(event,*timestamp);
    input->RawEventForTesting(type,event);CFRelease(event);
  }
  void Up(bool raw=false) {
    keys[settings.Get().shortcut_key_code]=false;
    if(raw)Raw(kCGEventKeyUp,settings.Get().shortcut_key_code);
    else Carbon(kEventHotKeyReleased);
  }
  void Idle(int line=__builtin_LINE()) const {
    if(controller.Snapshot().state!=seethis::core::InteractionState::kIdle ||
       !controller.Snapshot().regions.empty() || overlay.finished!=0)
      throw std::runtime_error("interrupted consent hold produced drawing/reference completion at line "+
          std::to_string(line)+" state="+std::to_string(static_cast<int>(controller.Snapshot().state))+
          " finished="+std::to_string(overlay.finished));
  }
};
static void MainProgress() {
  auto sentinel=std::make_shared<std::atomic_bool>(false);
  auto timer=std::make_shared<std::atomic_bool>(false);
  dispatch_async(dispatch_get_main_queue(),^{sentinel->store(true);});
  NSTimer* heartbeat=[NSTimer timerWithTimeInterval:0.01 repeats:NO
      block:^(NSTimer*){timer->store(true);}];
  [NSRunLoop.mainRunLoop addTimer:heartbeat forMode:NSRunLoopCommonModes];
  PumpUntil([&]{return sentinel->load() && timer->load();});
}
static void GrantAndDrain(const std::shared_ptr<NativeFake>& fake) {
  {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kGranted,0};}
  RefreshChromeConnection();
  PumpUntil([]{return ChromeConnectionStatus().permission.consent==ChromeConsent::kGranted &&
      ChromePhysicalWorkCountForTesting()==0;});
}
static void TestCapturedConsentPublicationOrdering() {
  for(bool unchanged_unavailable:{false,true}) {
    auto fake=Configure();
    if(unchanged_unavailable) {
      {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kUnavailable,-1712};}
      RefreshChromeConnection();
      PumpUntil([]{return ChromeConnectionStatus().permission.consent==ChromeConsent::kUnavailable &&
          ChromePhysicalWorkCountForTesting()==0;});
    }
    {std::lock_guard lock(fake->mutex);
      fake->hold_native=false;fake->hold_captured_preflight=true;
      fake->capture_preflight_at=fake->preflights.load()+2;}
    ConnectChromeExplicit();PumpUntil([&]{return fake->captured_preflight_entered.load();});
    const auto begun_authorization=ChromeConnectionStatus().authorization_generation;
    const auto newer=unchanged_unavailable?ChromeConsent::kUnavailable:ChromeConsent::kDenied;
    {std::lock_guard lock(fake->mutex);fake->preflight={newer,unchanged_unavailable?-1712:-1743};}
    RefreshChromeConnection();
    PumpUntil([&]{return ChromeConnectionStatus().permission.consent==newer &&
        ChromePhysicalWorkCountForTesting()==1;});
    const auto newer_authorization=ChromeConnectionStatus().authorization_generation;
    if(unchanged_unavailable)Require(newer_authorization==begun_authorization,
        "same unavailable refresh changed authorization instead of testing independent publication ordering");
    MainProgress();
    Require(fake->asks==1 && ChromeConnectionStatus().consent_pending,
        "newer passive observation released the physically blocked consent");
    fake->ReleaseCapturedPreflight();
    PumpUntil([]{return !ChromeConnectionStatus().consent_pending && ChromePhysicalWorkCountForTesting()==0;});
    const auto final=ChromeConnectionStatus();
    Require(final.permission.consent==newer && final.authorization_generation==newer_authorization &&
        fake->probes==0 && final.provider!=ChromeProviderState::kReady,
        "captured older consent grant overwrote newer published permission");
    // A later fresh explicit request is still allowed to authorize and recover.
    {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kNotRequested,-1744};}
    RefreshChromeConnection();
    PumpUntil([]{return ChromeConnectionStatus().permission.consent==ChromeConsent::kNotRequested &&
        ChromePhysicalWorkCountForTesting()==0;});
    ConnectChromeExplicit();
    PumpUntil([]{return ChromeConnectionStatus().permission.consent==ChromeConsent::kGranted &&
        ChromeConnectionStatus().provider==ChromeProviderState::kReady && ChromePhysicalWorkCountForTesting()==0;});
    Require(fake->asks==2 && fake->probes==1,"fresh explicit authorization failed after superseded old result");
  }
  {
    auto fake=Configure();
    {std::lock_guard lock(fake->mutex);fake->hold_captured_preflight=true;
      fake->capture_preflight_at=fake->preflights.load()+1;}
    ConnectChromeExplicit();PumpUntil([&]{return fake->captured_preflight_entered.load();});
    {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kDenied,-1743};}
    RefreshChromeConnection();
    PumpUntil([]{return ChromeConnectionStatus().permission.consent==ChromeConsent::kDenied &&
        ChromePhysicalWorkCountForTesting()==1;});
    fake->ReleaseCapturedPreflight();
    PumpUntil([]{return !ChromeConnectionStatus().consent_pending && ChromePhysicalWorkCountForTesting()==0;});
    Require(fake->asks==0 && fake->probes==0 &&
        ChromeConnectionStatus().permission.consent==ChromeConsent::kDenied,
        "superseded blocked initial preflight entered a delayed ask=true");
  }
  {
    auto fake=Configure();ConnectChromeExplicit();PumpUntil([&]{return fake->asks==1;});
    // A normal passive poll can supersede a pending native prompt's result.
    // It cannot permanently wedge recovery after the OS genuinely grants.
    const auto calls=fake->preflights.load();RefreshChromeConnection();
    PumpUntil([&]{return fake->preflights>calls && ChromePhysicalWorkCountForTesting()==1;});
    fake->Release();
    PumpUntil([]{return !ChromeConnectionStatus().consent_pending &&
        ChromeConnectionStatus().provider==ChromeProviderState::kReady && ChromePhysicalWorkCountForTesting()==0;});
    Require(fake->asks==1 && ChromeConnectionStatus().permission.consent==ChromeConsent::kGranted,
        "passive polling prevented prompt-free recovery or re-entered ask=true");
  }
  std::cout<<"Captured-old-grant ordering and unchanged-unavailable publication checks PASS\n";
}
static void TestBlockedPreflightAndFreshness() {
  auto fake=Configure();GrantAndDrain(fake);
  const auto authorization=ChromeConnectionStatus().authorization_generation;
  RefreshChromeConnection();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
  Require(ChromeConnectionStatus().authorization_generation==authorization &&
      ChromeConnectionStatus().permission.consent==ChromeConsent::kGranted,
      "successful same-permission refresh churned authorization");
  const auto before=fake->preflights.load();
  {std::lock_guard lock(fake->mutex);fake->hold_preflight=true;}
  const auto start=std::chrono::steady_clock::now();RefreshChromeConnection();
  PumpUntil([&]{return fake->preflights>before;});
  auto* view=View();STApplicationDelegate* menu=[STApplicationDelegate new];
  for(int i=0;i<100;++i) {
    Require(ChromeAutomationPreflight(123),"fresh grant changed during normal refresh");
    [view refreshChrome];
  }
  InputFixture input;input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "pending same-permission refresh interrupted first physical gesture");input.Up();
  [menu retryChrome:nil];MainProgress();
  Require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250) &&
      ChromeConnectionStatus().authorization_generation==authorization &&
      ChromeConnectionStatus().permission.consent==ChromeConsent::kGranted,
      "ordinary refresh jittered grant/generation or blocked main progress");
  PumpUntil([]{return ChromeConnectionStatus().permission.consent==ChromeConsent::kUnavailable;});
  Require(ChromePhysicalWorkCountForTesting()==1 && fake->native_active==1 &&
      ChromeConnectionStatus().authorization_generation>authorization,
      "preflight timeout released physical quota or retained authorization");
  const auto calls=fake->preflights.load();
  for(int i=0;i<1000;++i){RefreshChromeConnection();RetryChromeExplicit();}
  Require(fake->preflights==calls && ChromePhysicalWorkCountForTesting()==1,
      "timed-out repeated preflight spawned physical replacements");
  fake->ReleasePreflight();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
  Require(ChromeConnectionStatus().permission.consent==ChromeConsent::kUnavailable,
      "late preflight revived an expired grant");
  GrantAndDrain(fake);
  const auto recovered=ChromeConnectionStatus().authorization_generation;
  std::this_thread::sleep_for(std::chrono::milliseconds(1050));
  {std::lock_guard lock(fake->mutex);fake->hold_preflight=true;}
  const auto expired=RefreshChromeConnection();
  Require(expired.permission.consent==ChromeConsent::kUnavailable &&
      expired.authorization_generation>recovered,"one-second permission cache expiry was not failclosed");
  fake->ReleasePreflight();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
  Require(!fake->wrong_thread,"ask=false executed on main");
}
static void TestSharedPhysicalSaturation() {
  auto fake=Configure();GrantAndDrain(fake);
  {std::lock_guard lock(fake->mutex);fake->hold_preflight=true;fake->hold_provider=true;}
  const auto before=fake->preflights.load();RefreshChromeConnection();
  PumpUntil([&]{return fake->preflights>before;});
  std::atomic<int> entered=0;
  int accepted_callbacks=0,rejected_callbacks=0;
  AcquireChromePageAsync([fake,&entered] {
    ++entered;(void)fake->Provider();return ChromePageSample{};
  },900,[&](auto page) {
    ++accepted_callbacks;Require(page.availability!=seethis::core::PageAvailability::kReady,
        "blocked synthetic provider unexpectedly ready");
  });
  PumpUntil([&]{return entered==1 && fake->native_active==2;});
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1250);
  int rejected=0;
  while(std::chrono::steady_clock::now()<deadline) {
    RefreshChromeConnection();RetryChromeExplicit();ConnectChromeExplicit();
    bool inline_callback=false;
    AcquireChromePageAsync([&] {++entered;return ChromePageSample{};},901,[&](auto page) {
      ++rejected_callbacks;inline_callback=true;
      Require(NSThread.isMainThread && page.availability==seethis::core::PageAvailability::kUnavailable,
          "saturation rejection was not inline main unavailable");
    });
    ++rejected;
    Require(inline_callback && ChromePhysicalWorkCountForTesting()==2 &&
        fake->native_active==2 && fake->native_max<=2 && entered==1 && fake->asks==0,
        "cross permission/provider physical or waiting work exceeded bound");
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,false);
  }
  Require(accepted_callbacks==1 && rejected_callbacks==rejected &&
      ChromePhysicalWorkCountForTesting()==2,"logical timeout changed physical lease count");
  fake->ReleaseProvider();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==1;});
  int recovered=0;
  AcquireChromePageAsync([]{return ChromePageSample{};},902,[&](auto){++recovered;});
  PumpUntil([&]{return recovered==1;});
  Require(fake->native_active==1,"recovery canceled the remaining blocked native call");
  fake->ReleasePreflight();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
  Require(accepted_callbacks==1,"late provider delivered after timeout");
  GrantAndDrain(fake);
}
static void TestConsentBlockedPreflightInvalidation(bool restart) {
  auto fake=Configure();{std::lock_guard lock(fake->mutex);fake->hold_preflight=true;}
  const auto before=fake->preflights.load();ConnectChromeExplicit();
  PumpUntil([&]{return fake->preflights>before;});
  MainProgress();
  if(restart){StopChromeConnection();StartChromeConnection();}
  else {fake->Target({123,2000});RefreshChromeConnection();}
  fake->ReleasePreflight();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
  Require(fake->asks==0 && fake->probes==0 && !ChromeConnectionStatus().consent_pending,
      "blocked ask=false consent continuation prompted stale target/lifecycle");
}
static void TestLatePageIdentity(bool restart) {
  auto fake=Configure();GrantAndDrain(fake);
  {std::lock_guard lock(fake->mutex);fake->hold_provider=true;}
  ChromePageSample sample;sample.availability=seethis::core::PageAvailability::kReady;
  sample.browser_pid=123;sample.window_id=500;sample.window_bounds={0,0,100,100};
  sample.process_start_identity_us=1000;sample.opaque_tab_id="owned-tab";
  sample.url="https://owned.example.test/page";
  int callbacks=0;
  AcquireChromePageAsync([fake,sample]{(void)fake->Provider();return sample;},975,
      [&](auto page){++callbacks;Require(page.availability!=seethis::core::PageAvailability::kReady &&
          !page.identity && !page.captured_page,"late page revived stale PID/lifecycle authorization");});
  PumpUntil([&]{return fake->probes==1;});
  if(restart){StopChromeConnection();StartChromeConnection();}
  else {fake->Target({123,2000});RefreshChromeConnection();}
  Require(ChromePhysicalWorkCountForTesting()>=1,"identity invalidation freed physical provider");
  fake->ReleaseProvider();PumpUntil([&]{return callbacks==1 && ChromePhysicalWorkCountForTesting()==0;});
}
static void TestMainConsumerAndOffMainAdmission() {
  auto fake=Configure();std::atomic<int> exited=0,callbacks=0;
  for(int i=0;i<2;++i)AcquireChromePageAsync([&]{++exited;return ChromePageSample{};},950+i,
      [&](auto){++callbacks;});
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  // Intentionally do not service main; worker exits must not free notification/timer records.
  while(exited<2 && std::chrono::steady_clock::now()<deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  Require(exited==2 && callbacks==0 && ChromePhysicalWorkCountForTesting()==2,
      "slow main consumer released pending completion admission");
  for(int i=0;i<1000;++i) {
    bool inline_callback=false;
    AcquireChromePageAsync([&]{++exited;return ChromePageSample{};},960+i,
        [&](auto){inline_callback=true;});
    Require(inline_callback && exited==2 && ChromePhysicalWorkCountForTesting()==2,
        "busy pending-main rejection enqueued work");
  }
  std::thread invalid([&]{AcquireChromePageAsync([&]{++exited;return ChromePageSample{};},999,
      [&](auto){++callbacks;});});invalid.join();
  Require(exited==2 && callbacks==0,"off-main invalid entry changed callback thread or native count");
  PumpUntil([&]{return callbacks==2 && ChromePhysicalWorkCountForTesting()==0;});
  // Reentrant submission and a native exception still drain both timer/record leases.
  int recovered=0;
  AcquireChromePageAsync([]()->ChromePageSample{throw std::runtime_error("owned native fault");},1000,
      [&](auto page){Require(page.availability==seethis::core::PageAvailability::kUnavailable,
          "provider exception was not failclosed");++recovered;
        AcquireChromePageAsync([]{return ChromePageSample{};},1001,[&](auto){++recovered;});});
  PumpUntil([&]{return recovered==2 && ChromePhysicalWorkCountForTesting()==0;});
}

static void TestPhysicalConsentHold(bool grant,bool custom) {
  auto fake=Configure();InputFixture input;
  if(custom) {
    auto settings=input.settings.Get();settings.shortcut_key_code=8;
    settings.shortcut_modifiers=seethis::core::kShortcutControl|seethis::core::kShortcutShift;
    Require(input.settings.Update(settings),"custom fixture binding rejected");input.Bind();
    input.keys[0]=true;input.Carbon();input.keys[0]=false;
    Require(fake->asks==0,"default key authorized customized binding");
  }
  const auto settings=input.settings.Get();
  const auto key=settings.shortcut_key_code;
  input.flags=NativeModifiers(settings.shortcut_modifiers);
  input.keys[key]=true;
  input.flags|=kCGEventFlagMaskCommand;input.Carbon();
  input.flags=NativeModifiers(settings.shortcut_modifiers);
  input.Carbon(kEventHotKeyPressed,static_cast<UInt32>(input.input->CaptureGenerationForTesting()-1));
  input.Carbon(kEventHotKeyPressed,{},GetCurrentEventTime()-1);
  input.readiness->input_monitor=InputMonitorState::kDisabled;input.Carbon();
  input.readiness->input_monitor=InputMonitorState::kGranted;
  Require(fake->asks==0 && !ChromeConnectionStatus().consent_pending,
      "wrong modifiers/generation/provenance/readiness authorized capture");
  {std::lock_guard lock(fake->mutex);fake->result=grant?
      ChromePermissionResult{ChromeConsent::kGranted,0}:ChromePermissionResult{ChromeConsent::kDenied,-1743};}
  const auto start=std::chrono::steady_clock::now();input.Carbon();
  Require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250),
      "production capture keydown blocked main");
  Require(fake->asks==0&&input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "optional Automation consumed basic capture first press");
  ConnectChromeExplicit();PumpUntil([&]{return fake->asks==1;});input.Idle();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kIdle,
      "explicit Connect did not interrupt the owned drawing");
  input.Carbon();MainProgress();
  Require(fake->asks==1 && ChromeConnectionStatus().consent_pending,
      "blocked capture request did not preserve single-flight/main progress");
  fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  if(grant)PumpUntil([&]{return ChromeConnectionStatus().provider==ChromeProviderState::kReady;});
  input.Carbon();input.Idle();
  Require(fake->asks==1,"fast outcome repeated held physical authorization");
  input.flags=0;input.Raw(kCGEventFlagsChanged,58);input.input->WatchdogForTesting();
  input.flags=NativeModifiers(settings.shortcut_modifiers);input.Carbon();input.Idle();
  Require(fake->asks==1,"modifier release/repress rearmed held capture key");
  input.Up(true);input.input->WatchdogForTesting();input.Idle();
  input.keys[key]=true;input.Carbon();
  if(grant) {
    Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
        "fresh gesture after grant failed to start normal capture");
    input.Up();Require(input.overlay.finished==0,"empty fresh gesture persisted phantom reference");
  } else {
    Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing&&fake->asks==1,
        "Denied optional Automation blocked a released fresh basic capture");input.Up();
    Require(ChromeConnectionStatus().permission.consent==ChromeConsent::kDenied,
        "denied outcome was not preserved truthfully");
  }
  Require(!fake->wrong_thread,"capture request/preflight used incorrect queue");
}
static void TestPhysicalFocusAndBinding() {
  auto fake=Configure();InputFixture input;input.keys[0]=true;input.Carbon();
  ConnectChromeExplicit();PumpUntil([&]{return fake->asks==1;});fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  input.foreground=999;fake->Foreground({});input.input->FocusChangedForTesting();
  auto settings=input.settings.Get();settings.shortcut_key_code=8;
  Require(input.settings.Update(settings),"binding change failed");input.Bind();
  input.keys[8]=true;input.Carbon();input.Idle();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kIdle && fake->asks==1,"focus/config switch discarded original hold");
  input.keys[0]=false;input.input->WatchdogForTesting();input.Idle();
  // Reconciliation makes a new callback eligible; it never starts drawing itself.
  input.keys[8]=false;input.input->WatchdogForTesting();input.keys[8]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "released original key did not restore configured fresh gesture");input.Up();
}
static void TestPhysicalTargetAndPassivePaths() {
  auto fake=Configure();InputFixture input;
  RefreshChromeConnection();RetryChromeExplicit();auto* view=View();[view refreshChrome];
  Require(fake->asks==0,"passive refresh/retry/Inspector prompted");
  // Cached discovery was absent until immediately before the first keydown.
  fake->Target({});RefreshChromeConnection();fake->Target({123,2000});fake->Foreground({123,2000});
  input.keys[0]=true;input.Carbon();ConnectChromeForCaptureExplicit({123,2000});PumpUntil([&]{return fake->asks==1;});input.Idle();
  Require(ChromeConnectionStatus().target==ChromeRunningTarget{123,2000},
      "first foreground intent used stale cached Chrome readiness");
  fake->Target({123,3000});RefreshChromeConnection();fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  Require(fake->probes==0,"relaunch published obsolete capture authorization");input.Up();
  fake=Configure();fake->Foreground({456,1000});input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,"unrelated optional Automation target cannot block basic capture");
  Require(fake->asks==0 && !ChromeConnectionStatus().consent_pending,
      "foreground mismatch authorized background Chrome");input.Up();
  fake->Foreground({123,0});input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,"invalid optional Automation identity cannot consume basic first hold");
  Require(fake->asks==0,"invalid foreground launch identity prompted");input.Up();
  fake->Foreground({});input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing && fake->asks==0,
      "non-Chrome capture prompted or lost normal drawing");input.Up();
  fake=Configure();{std::lock_guard lock(fake->mutex);
    fake->preflight={ChromeConsent::kGranted,0};fake->provider=ChromeProviderState::kTimedOut;}
  PublishChromeObservation(123,true,seethis::core::PageAvailability::kUnavailable);
  RetryChromeExplicit();PumpUntil([&]{return ChromeConnectionStatus().provider==ChromeProviderState::kTimedOut;});
  input.keys[0]=true;input.Carbon();
  Require(fake->asks==0 && input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "granted page/provider failure incorrectly requested consent");input.Up();
}
static void TestPhysicalQueuedTargetAbandonment(bool quit) {
  auto fake=Configure();InputFixture input;
  {std::lock_guard lock(fake->mutex);fake->hold_discovery=true;}
  // Both passive refresh and explicit consent may own an admitted discovery.
  // Hold the passive call before capture so this legal two-unit schedule is
  // exercised deterministically instead of waiting for a transient count of 1.
  RefreshChromeConnection();
  PumpUntil([&]{return fake->worker_discoveries>=1;});
  input.keys[0]=true;input.Carbon();ConnectChromeExplicit();
  PumpUntil([&]{return fake->worker_discoveries>=2;});
  Require(fake->worker_discoveries<=2 && ChromePhysicalWorkCountForTesting()==2,
      "two blocked hotkey discoveries must retain exactly two shared physical admissions");
  input.Idle();MainProgress();
  fake->Target(quit?ChromeRunningTarget{}:ChromeRunningTarget{123,2000});
  fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending &&
      ChromePhysicalWorkCountForTesting()==0;});
  Require(fake->asks==0 && fake->probes==0,"queued hotkey quit/relaunch entered native permission");
  input.Carbon();input.Idle();input.Up();input.Idle();
}
static void TestPhysicalNewHoldWhilePending() {
  auto fake=Configure();InputFixture input;input.keys[0]=true;input.Carbon();
  ConnectChromeExplicit();PumpUntil([&]{return fake->asks==1;});input.Up();input.Idle();
  input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing&&input.overlay.finished==0&&fake->asks==1 && ChromeConnectionStatus().consent_pending,
      "new physical gesture duplicated unresolved native permission");
  const auto admittedDrawing=input.overlay.drawing;
  fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  input.Carbon();
  Require(fake->asks==1&&input.overlay.drawing==admittedDrawing&&input.overlay.finished==0&&
      input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "completion must not replay or complete an independently admitted basic hold");
  input.Up();input.input->WatchdogForTesting();input.Idle();
  Require(!input.input->CaptureHeldForTesting(),"actual release retires the independent basic hold before its next gesture");
  input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "actual release did not permit a later fresh gesture");input.Up();
}
static void TestPhysicalDrawingInterruption() {
  auto fake=Configure();InputFixture input;
  (void)input.controller.ShortcutKeyDown(MonotonicMilliseconds(),input.pointer);
  (void)input.controller.PointerDown(input.pointer);
  auto point=input.pointer;point.position.x=50;
  (void)input.controller.PointerMoved(point);(void)input.controller.PointerUp(point);
  Require(!input.controller.Snapshot().regions.empty(),"drawing fixture did not contain region");
  input.keys[0]=true;input.Carbon();ConnectChromeExplicit();PumpUntil([&]{return fake->asks==1;});input.Idle();
  Require(input.overlay.drawing==0,"interruption published partial drawing");
  input.Up();input.Idle();fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});input.Idle();
  // A normal granted hold canceled on focus change remains one physical intent
  // even if readiness is revoked before a repeat returns to Chrome.
  input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "granted fixture did not draw");
  input.foreground=999;input.input->FocusChangedForTesting();input.Idle();
  {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kDenied,-1743};}
  RefreshChromeConnection();
  PumpUntil([]{RefreshChromeConnection();
    return ChromeConnectionStatus().permission.consent==ChromeConsent::kDenied;});
  input.foreground=123;input.Carbon();input.input->WatchdogForTesting();input.Idle();
  Require(fake->asks==1,"canceled normal held gesture became repeat authorization");
  input.keys[0]=false;input.input->WatchdogForTesting();input.Idle();
  input.keys[0]=true;input.Carbon();
  Require(fake->asks==1&&input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "fresh Denied Automation gesture must retain basic capture");input.Up();
}

// Actual MacInputAdapter callbacks, local unposted event objects and injected
// snapshots. No Start, live tap, global hotkey, app panel or clipboard operation.
static void TestCaptureRawReleaseIngress() {
  using namespace seethis::core;
  auto fake=Configure();fake->Foreground({}); // capture target is not Chrome
  const auto make=[](bool invalidSettings=false) {
    char pattern[]="/tmp/seethis-r2-input-XXXXXX";const char* path=mkdtemp(pattern);
    Require(path,"R2 temporary directory unavailable");
    if(invalidSettings){std::ofstream file(std::filesystem::path(path)/"settings.json");file<<"invalid-owned-fixture";}
    return std::make_unique<InputFixture>(std::filesystem::path(path));
  };
  const auto now=[] {return CurrentCarbonEventProvenance().earliest_ns;};
  const auto rawUp=[&](InputFixture& f,CGKeyCode key) {
    usleep(2000);const auto stamp=now();f.Raw(kCGEventKeyUp,key,stamp);return stamp;
  };
  const auto press=[](InputFixture& f) {
    usleep(2000);const auto binding=f.settings.Get();f.keys[binding.shortcut_key_code]=true;
    f.flags=NativeModifiers(binding.shortcut_modifiers);f.Carbon();
  };
  const auto twoStrokes=[](InputFixture& f) {
    const auto before=f.overlay.drawing;
    for(int index=0;index<2;++index) {
      const auto point=seethis::core::DisplayPoint{1,CoordinateUnit::kLogicalPoints,{10.0,20.0+index*20},1};
      auto end=point;end.position.x+=20;
      Require(f.controller.PointerDown(point)&&f.controller.PointerMoved(end)&&f.controller.PointerUp(end),
          "R2 actual-adapter-started controller rejected stroke");
      Require(f.controller.Snapshot().state==InteractionState::kDrawing,"R2 mouse up incorrectly finished hold");
    }
    Require(f.controller.Snapshot().regions.size()==2&&f.overlay.drawing==before,
        "R2 two strokes must not create a second input admission");
  };
  {
    auto owned=make();auto& f=*owned;const auto key=f.settings.Get().shortcut_key_code;
    // Same adapter/controller/hold/registration across both rounds. Append is
    // not an input concern; R1's unchanged selector separately covers OFF/ON.
    for(int round=0;round<2;++round) {
      const auto before=f.overlay.drawing;press(f);
      Require(f.controller.Snapshot().state==InteractionState::kDrawing&&f.overlay.drawing==before+1,
          round==0?"R2 first Carbon press did not enter controller/overlay exactly once":
          "R2 second-round Carbon press did not enter controller/overlay exactly once");
      twoStrokes(f);
      if(round==0)f.Carbon(kEventHotKeyReleased);
      else {f.flags=0;f.Raw(kCGEventFlagsChanged,58);}
      Require(f.controller.Snapshot().state==InteractionState::kFinished&&f.input->CaptureHeldForTesting(),
          "R2 release completion must not clear original held main key");
      f.flags=NativeModifiers(f.settings.Get().shortcut_modifiers);
      const auto finished=f.overlay.finished;const auto drawing=f.overlay.drawing;
      const auto stamp=rawUp(f,key); // Combined remains DOWN: the source-bound counterexample.
      Require(!f.input->CaptureHeldForTesting()&&f.keys[key]&&f.overlay.drawing==drawing&&f.overlay.finished==finished,
          "R2 matching raw up must rearm without completion/replay/autostart");
      const auto floor=f.input->CaptureBoundaryForTesting();
      Require(floor.valid()&&floor.latest_ns>=stamp,"R2 event release did not advance monotonic floor");
    }
    press(f);const auto drawing=f.overlay.drawing;const auto heldPress=f.lastCarbonPress;
    f.Carbon();Require(f.overlay.drawing==drawing,"R2 still-held press repeated capture");
    const auto floor=f.input->CaptureBoundaryForTesting();
    usleep(2000);
    for(const std::uint64_t stamp:std::array<std::uint64_t,4>{0,1,heldPress.latest_ns,now()+10'000'000'000ULL}) {
      f.Raw(kCGEventKeyUp,key,stamp);
      Require(f.input->CaptureHeldForTesting()&&f.input->CaptureBoundaryForTesting().latest_ns==floor.latest_ns,
          "R2 invalid/prepress/future release cleared hold or changed floor");
    }
    f.Raw(kCGEventKeyUp,static_cast<CGKeyCode>(key+1),now());
    Require(f.input->CaptureHeldForTesting(),"R2 wrong key cleared hold");
    // Rebinding advances the floor but preserves the immutable original hold.
    f.Bind();const auto reboundFloor=f.input->CaptureBoundaryForTesting();
    const auto oldRelease=rawUp(f,key);Require(!f.input->CaptureHeldForTesting(),"R2 same-key rebind lost original release");
    press(f);const auto next=f.overlay.drawing;
    f.Raw(kCGEventKeyUp,key,oldRelease);f.Carbon();
    Require(f.input->CaptureHeldForTesting()&&f.overlay.drawing==next&&
        f.input->CaptureBoundaryForTesting().latest_ns>=reboundFloor.latest_ns,
        "R2 delayed old up rearmed a later hold or lowered registration floor");
    // Physical-false fallback still retires the associated record coherently.
    f.keys[key]=false;f.Carbon(kEventHotKeyReleased);
    Require(!f.input->CaptureHeldForTesting(),"R2 physical-false fallback stopped working");
  }
  {
    auto owned=make();auto& f=*owned;press(f);twoStrokes(f);
    const auto original=f.settings.Get().shortcut_key_code;
    const auto acceptedPress=CurrentCarbonEventProvenance(); // latest bound after accepted press
    f.Carbon(kEventHotKeyReleased);const auto before=f.overlay.drawing;
    f.Carbon();Require(f.overlay.drawing==before,"R2 Carbon release alone rearmed a held key");
    const auto oldFloor=f.input->CaptureBoundaryForTesting();
    f.Raw(kCGEventKeyUp,original,acceptedPress.earliest_ns-10'000'000);
    Require(f.input->CaptureHeldForTesting()&&f.input->CaptureBoundaryForTesting().latest_ns==oldFloor.latest_ns,
        "R2 stale prepress receipt changed current hold/floor");
    // Original key survives a different-key binding and focus cancellation.
    auto binding=f.settings.Get();binding.shortcut_key_code=8;
    Require(f.settings.Update(binding),"R2 owned different-key binding rejected");f.Bind();
    f.foreground=456;f.input->FocusChangedForTesting();
    Require(f.input->CaptureHeldForTesting(),"R2 focus/binding change discarded original hold");
    const auto advanced=f.input->CaptureBoundaryForTesting();
    f.overlay.onObservation=[&]{Require(!f.input->CaptureHeldForTesting(),"R2 release consumer ran after synchronous presentation work");};
    const auto originalUp=rawUp(f,original);f.overlay.onObservation={};
    Require(!f.input->CaptureHeldForTesting()&&f.input->CaptureBoundaryForTesting().latest_ns>=advanced.latest_ns,
        "R2 original key could not release across different-key rebind");
    press(f);Require(f.controller.Snapshot().state==InteractionState::kDrawing,"R2 fresh new binding did not start");
    f.Raw(kCGEventKeyUp,original,originalUp);f.Carbon();
    Require(f.input->CaptureHeldForTesting(),"R2 old original key release cleared newly bound hold");
    // Valid release after an already rejected press does not replay that press.
    f.Carbon(kEventHotKeyReleased);const auto drawing=f.overlay.drawing;
    rawUp(f,binding.shortcut_key_code);
    Require(f.overlay.drawing==drawing,"R2 late release automatically replayed a rejected press");
  }
  {
    auto owned=make();auto& f=*owned;press(f);twoStrokes(f);
    const auto key=f.settings.Get().shortcut_key_code;
    f.foreground=456;f.input->FocusChangedForTesting();
    Require(f.input->CaptureHeldForTesting()&&f.controller.Snapshot().state==InteractionState::kIdle,
        "R2 focus interruption must not discard physical hold");
    f.input->RawEventForTesting(kCGEventTapDisabledByUserInput,nullptr);
    Require(f.input->CaptureHeldForTesting()&&f.controller.Snapshot().state==InteractionState::kIdle,
        "R2 monitor interruption must cancel without clearing held key");
    rawUp(f,key);const auto drawing=f.overlay.drawing;
    f.Carbon();Require(f.overlay.drawing==drawing,"R2 raw release bypassed unhealthy monitor");
    f.readiness->input_monitor=InputMonitorState::kGranted;
    f.input->CaptureRegisteredForTesting(false);f.Carbon();
    Require(f.overlay.drawing==drawing,"R2 raw release bypassed registration readiness");
    f.input->CaptureRegisteredForTesting(true);
    f.Carbon(kEventHotKeyPressed,static_cast<UInt32>(f.input->CaptureGenerationForTesting()-1));
    f.Carbon(kEventHotKeyPressed,{},GetCurrentEventTime()-1);
    f.flags|=kCGEventFlagMaskCommand;f.Carbon();
    Require(f.overlay.drawing==drawing&&!f.input->CaptureHeldForTesting(),
        "R2 stale generation/provenance/extra modifier bypassed admission");
    press(f);Require(f.overlay.drawing==drawing+1,"R2 healthy fresh press did not recover");
  }
  {
    auto owned=make(true);auto& f=*owned;Require(!f.settings.usable(),"R2 malformed owned config fixture not invalid");
    press(f);Require(!f.input->CaptureHeldForTesting()&&f.overlay.drawing==0,"R2 unavailable settings admitted capture");
  }
  // Existing injected consent backend only; no real Automation request or UI.
  fake=Configure();
  {
    auto owned=make();auto& f=*owned;press(f);
    Require(fake->asks==0&&f.controller.Snapshot().state==InteractionState::kDrawing,
        "R2 optional consent consumed physical first hold");
    ConnectChromeExplicit();PumpUntil([&]{return fake->asks==1;});
    Require(f.input->CaptureHeldForTesting()&&f.controller.Snapshot().state==InteractionState::kIdle,"R2 explicit consent must interrupt without discarding hold");
    f.flags=0;f.Raw(kCGEventFlagsChanged,58);f.flags=kCGEventFlagMaskAlternate;f.Carbon(kEventHotKeyReleased);f.Carbon();
    Require(fake->asks==1&&f.controller.Snapshot().state==InteractionState::kIdle&&f.input->CaptureHeldForTesting(),"R2 modifier/Carbon release bypassed consent hold");
    rawUp(f,f.settings.Get().shortcut_key_code);
    Require(f.controller.Snapshot().state==InteractionState::kIdle,"R2 consent release auto-started drawing");
    fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  }
  StopChromeConnection();
  std::cout<<"R2 actual-adapter raw-release/rebind/provenance/readiness/two-round multi-stroke checks PASS\n";
}

// Local CG objects only, fed into the actual unstarted input callback. The
// existing physical-input seam supplies display conversion, never NSScreen.
static void TestSourceFocusEventIngress() {
  InputFixture f;double top=100;
  f.input->ConfigureCaptureForTesting({[&](CGKeyCode key){return f.keys[key];},
      [&]{return f.flags;},[&]{return f.foreground;},[&]{return top;}},f.pointer);
  for(const auto type:{kCGEventLeftMouseDown,kCGEventRightMouseDown,kCGEventOtherMouseDown,
      kCGEventLeftMouseDragged,kCGEventRightMouseDragged,kCGEventOtherMouseDragged,kCGEventScrollWheel}) {
    CGEventRef event=type==kCGEventScrollWheel?
        CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitPixel,1,1):
        CGEventCreateMouseEvent(nullptr,type,CGPointMake(25,30),kCGMouseButtonLeft);
    Require(event,"owned mouse event created");CGEventSetLocation(event,CGPointMake(25,30));CGEventSetTimestamp(event,CurrentCarbonEventProvenance().earliest_ns);
    unsigned mappings=0,delivered=0;
    f.overlay.onPointMapping=[&]{++mappings;CGEventSetLocation(event,CGPointMake(80,90));};
    f.overlay.onViewportInput=[&](ViewportInput kind,const auto& point) {
      if(kind==ViewportInput::kInterrupted)return;
      ++delivered;
      const auto expected=type==kCGEventLeftMouseDown?ViewportInput::kPrimaryDown:
          type==kCGEventScrollWheel?ViewportInput::kScroll:ViewportInput::kPointer;
      Require(kind==expected,"F3 actual adapter maps only fresh primary down, never right/other/drag/scroll");
      Require(point&&point->position.x==25&&point->position.y==70&&point->display_id==1&&point->backing_scale==1,
          "F3 actual adapter forwards latched original CG down point despite later event/pointer movement");
    };
    f.input->RawEventForTesting(type,event);
    Require(delivered==1&&mappings==1,"F3 event point is converted once before presentation callbacks");
    CFRelease(event);
  }
  f.overlay.onPointMapping={};
  auto stale=CGEventCreateMouseEvent(nullptr,kCGEventLeftMouseDown,CGPointMake(25,30),kCGMouseButtonLeft);
  CGEventSetTimestamp(stale,1);unsigned staleCalls=0;
  f.overlay.onViewportInput=[&](ViewportInput kind,const auto&) {
    if(kind==ViewportInput::kInterrupted)return;
    ++staleCalls;Require(kind==ViewportInput::kPointer,"F3 delayed/invalid timestamp fails closed as generic pointer");
  };
  f.input->RawEventForTesting(kCGEventLeftMouseDown,stale);CFRelease(stale);
  Require(staleCalls==1,"F3 stale native event still invalidates through ordinary pointer policy");
  std::cout<<"F3 actual unstarted adapter primary/right/other/drag/scroll original event-point and stale-event checks passed\n";
}


// No production app initialization, timers, panels, OS input or foreground changes.
// The production click/number-click/refresh methods run against owned fixtures.
@interface STLatencyClickCoordinator : STOverlayCoordinator
@property(nonatomic) STDeleteChord chord;
@property(nonatomic) unsigned inspectorRefreshes;
- (seethis::core::MarkController*)configureStore:(seethis::core::ReferenceStore*)store
    settings:(seethis::core::SettingsStore*)settings
    interaction:(seethis::core::InteractionController*)interaction
    deletion:(seethis::core::DeleteGestureController*)deletion
    displays:(const std::vector<seethis::core::FrozenDisplay>&)displays;
- (void)primeMotionState;
@end
@implementation STLatencyClickCoordinator
- (seethis::core::MarkController*)configureStore:(seethis::core::ReferenceStore*)store
    settings:(seethis::core::SettingsStore*)settings
    interaction:(seethis::core::InteractionController*)interaction
    deletion:(seethis::core::DeleteGestureController*)deletion
    displays:(const std::vector<seethis::core::FrozenDisplay>&)displays {
  _references=store;_settings=settings;self.controller=interaction;_deletion=deletion;_displays=displays;
  _marks=std::make_unique<seethis::core::MarkController>(*store,[](const auto&){return true;});
  _hoveredID=std::string(32,'f');self.panels=[NSMutableArray array];return _marks.get();
}
- (void)primeMotionState {_previousState=self.controller->Snapshot().state;}
- (STDeleteChord)physicalDeleteChord {return self.chord;}
- (void)prepareInspectorSelection:(const std::string&)identifier {(void)identifier;}
- (void)refreshInspector {++_inspectorRefreshes;}
- (BOOL)inspectorVisible {return YES;}
- (void)showInspectorIfAllowed:(seethis::core::InspectorSelectionTrigger)trigger {(void)trigger;}
- (BOOL)recopyInspectorSelection:(const std::string&)identifier {(void)identifier;return NO;}
- (void)updatePointerPolicy:(const std::optional<seethis::core::DisplayPoint>&)point {(void)point;}
@end
static void TestCurrentContextNativeClick() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-latency-click-XXXXXX";const char* directory=mkdtemp(pattern);
  Require(directory,"click temporary fixture unavailable");
  {
    ReferenceStore store(std::filesystem::path(directory)/"references");
    SettingsStore settings(std::filesystem::path(directory)/"settings.json");
    InteractionController interaction;DeleteGestureController deletion;
    Context context;context.pid=context.window_pid=42;context.self_pid=99;context.window_id=7;
    context.app_name="Synthetic";context.bundle_id="test.click";context.executable="/fixture";
    context.window_title="A";context.window={0,0,100,100};context.quartz_to_appkit_top=100;
    context.observed={1'000'000,1000};context.native_window={1,700,"click-owned",1};
    context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    context.displays={{"synthetic",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}}};
    STLatencyClickCoordinator* coordinator=[STLatencyClickCoordinator new];
    auto* marks=[coordinator configureStore:&store settings:&settings interaction:&interaction deletion:&deletion displays:context.displays];
    marks->SetViewportContinuity(1,true);
    const auto point=[](double x,double y){return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};};
    const auto observe=[&](const Context& c) {
      WindowObservation w;w.availability=WindowAvailability::kReady;w.generation=marks->window_observation().generation+1;
      w.observed=Now();w.pid=c.window_pid;w.bundle_id=c.bundle_id;w.window_id=c.window_id;w.bounds=c.window;
      w.window_title=c.window_title;w.title={1,TitleAvailability::kReady};w.native_window=c.native_window;marks->SetWindowObservation(w);
    };
    std::optional<PageIdentity> capturePage;
    const auto add=[&](char letter,double y) {
      observe(context);const std::string id(32,letter);auto g=marks->Begin(id);
      Require(marks->BindContext(g,context),"click fixture source binds");
      if(capturePage)Require(marks->BindPageIdentity(g,*capturePage),"click fixture exact page binds");
      CaptureResult capture;capture.requested={1'000'001,1001};capture.completed={1'000'002,1002};capture.pixels={100,100,Bytes(40000,255)};
      marks->Captured(g,std::move(capture));marks->Release(g,{point(20,y),point(70,y)},{1'000'003,1003});
      store.WaitForIdleForTesting();Require(store.ReadPresentation(id)!=nullptr,"click fixture ready publication missing");
    };
    add('a',20);add('b',20);add('c',65);observe(context);
    deletion.Configure(1,true,true);deletion.KeyDown(true,false);deletion.HotKeyPressed(1,true,true);
    Require(deletion.Snapshot().armed(),"test delete hold not armed");coordinator.chord={true,true};
    Require(![coordinator click:point(95,95)]&&![coordinator click:point(40,20+settings.Get().mark_hit_radius_points+0.01)],"blank and near miss do not delete");
    auto other=context;other.window_id=9;observe(other);
    Require(![coordinator click:point(40,20)],"wrong current window cannot delete cached source");observe(context);
    Require([coordinator click:point(40,20)]&&store.LookupMetadata(std::string(32,'b'))->state==ReferenceJobState::kDeleted&&
        store.LookupMetadata(std::string(32,'a'))->state==ReferenceJobState::kReady,
        "production direct click ignores stale hover and deletes only topmost exact overlap target");
    Require([coordinator click:point(40,20)]&&store.LookupMetadata(std::string(32,'a'))->state==ReferenceJobState::kDeleted&&
        ![coordinator click:point(40,20)],"next overlap target deletes and post-delete click cannot resurrect either reference");
    deletion.KeyUp();coordinator.chord={false,true};
    Require([coordinator click:point(40,65)]&&store.LookupMetadata(std::string(32,'c'))->state==ReferenceJobState::kReady,
        "physical release prevents direct stroke deletion even with a valid hit");
    deletion.KeyDown(true,false);deletion.HotKeyPressed(1,true,true);coordinator.chord={true,false};
    Require([coordinator click:point(40,65)]&&store.LookupMetadata(std::string(32,'c'))->state==ReferenceJobState::kReady,
        "wrong physical modifiers prevent deletion");
    deletion.KeyUp();deletion.KeyDown(true,false);deletion.HotKeyPressed(1,true,true);coordinator.chord={true,true};
    const auto badges=STPresentedNoteBadges(store,*marks,std::string(32,'c'),settings.Visibility(),context.displays);
    Require(badges.size()==1&&[coordinator handleNoteBadgeClick:badges.front().anchor]&&
        store.LookupMetadata(std::string(32,'c'))->state==ReferenceJobState::kDeleted,
        "production number-button click deletes its fresh exact current source");
    Require(![coordinator handleNoteBadgeClick:badges.front().anchor],"post-delete number click remains absent");
    context.bundle_id="com.google.Chrome";
    PageIdentity page;page.browser_pid=42;page.window_id=7;page.window_bounds=context.window;
    page.process_start_identity_us=700;page.opaque_tab_id="owned-tab";page.navigation_digest=std::string(64,'a');capturePage=page;
    marks->SetPageObservation({PageAvailability::kReady,1,Now(),page});add('d',45);
    auto wrongPage=page;wrongPage.navigation_digest=std::string(64,'b');
    marks->SetPageObservation({PageAvailability::kReady,2,Now(),wrongPage});
    Require(![coordinator click:point(40,45)]&&store.LookupMetadata(std::string(32,'d'))->state==ReferenceJobState::kReady,
        "production direct click rejects another page in the same native Chrome window");
    marks->SetPageObservation({PageAvailability::kReady,3,Now(),page});
    Require([coordinator click:point(40,45)]&&store.LookupMetadata(std::string(32,'d'))->state==ReferenceJobState::kDeleted,
        "production direct click restores exact page A and deletes only its mark");
    Require(interaction.ShortcutKeyDown(1000,point(20,20))==StartResult::kStarted,"motion refresh round starts");
    [coordinator primeMotionState];const auto refreshes=coordinator.inspectorRefreshes;
    for(int index=0;index<60;++index) {
      (void)interaction.PointerMoved(point(20+index,20),1001+index);[coordinator refresh];
    }
    Require(coordinator.inspectorRefreshes==refreshes,"each drawing motion avoids redundant full Inspector refresh");
    (void)interaction.ShortcutKeyUp(1100);
  }
  std::filesystem::remove_all(directory);
  std::cout<<"Production current-source stroke/number click stale-hover/overlap/miss/release/post-delete and Inspector-motion checks passed\n";
}

static void TestColdCorruptNativePresentation() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-cold-corrupt-native-XXXXXX";const char* directory=mkdtemp(pattern);
  Require(directory,"cold corrupt native fixture directory unavailable");
  const std::filesystem::path root(directory),references=root/"references";const std::string id(32,'a');
  const auto point=[](double x,double y){return DisplayPoint{1,CoordinateUnit::kLogicalPoints,{x,y},1};};
  Context context;context.pid=context.window_pid=42;context.self_pid=99;context.window_id=7;
  context.app_name="Synthetic";context.bundle_id="test.cold-corrupt";context.executable="/fixture";
  context.window_title="A";context.window={0,0,100,100};context.quartz_to_appkit_top=100;
  context.observed={1'000'000,1000};context.native_window={1,700,"cold-corrupt-owned",1};
  context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
  context.excluded_window_ids={8};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
  context.displays={{"synthetic",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}}};
  {
    ReferenceStore store(references);Reference reference;reference.id=id;reference.context=context;
    reference.path={point(20,20),point(70,20)};reference.circle_completed={1'000'003,1003};
    Require(store.AcceptPending(reference,0,0).state==ReferenceJobState::kPending,"cold native reference accepted");
    CaptureResult capture;capture.requested={1'000'001,1001};capture.completed={1'000'002,1002};capture.pixels={100,100,Bytes(40000,255)};
    store.FinalizeAsync(id,std::move(capture));store.WaitForIdleForTesting();
    Require(store.LookupMetadata(id)->state==ReferenceJobState::kReady,"cold native original persisted");
  }
  {
    ReferenceStore store(references);store.WaitForIdleForTesting();SettingsStore settings(root/"settings.json");
    Require(!store.Jobs().front().ready,"native cold reopen has no hydrated original");
    InteractionController interaction;DeleteGestureController deletion;
    STLatencyClickCoordinator* coordinator=[STLatencyClickCoordinator new];
    auto* marks=[coordinator configureStore:&store settings:&settings interaction:&interaction deletion:&deletion displays:context.displays];
    marks->SetViewportContinuity(1,true);
    const auto observe=[&](const Context& c) {
      WindowObservation w;w.availability=WindowAvailability::kReady;w.generation=marks->window_observation().generation+1;
      w.observed=Now();w.pid=c.window_pid;w.bundle_id=c.bundle_id;w.window_id=c.window_id;w.bounds=c.window;
      w.window_title=c.window_title;w.title={1,TitleAvailability::kReady};w.native_window=c.native_window;marks->SetWindowObservation(w);
    };observe(context);
    const auto original=store.ReadPresentation(id);
    Require(original&&marks->marks().size()==1&&STVisibleStrokeHit(store,*marks,settings.Visibility(),context.displays,point(40,20),1),
        "cold native metadata initially supplies production ink and stroke hit");
    const auto badges=STPresentedNoteBadges(store,*marks,id,settings.Visibility(),context.displays);
    Require(badges.size()==1&&[coordinator noteBadgeAtPoint:badges.front().anchor],"cold native fixture warms actual number cache");
    {std::ofstream corrupt(references/(id+".stref"));corrupt<<"corrupt original fixture";}
    {std::ofstream corrupt(references/(id+".source.png"));corrupt<<"corrupt original fixture";}
    const auto discovered=store.Lookup(id);
    Require(discovered&&discovered->state==ReferenceJobState::kFailed&&discovered->code=="corrupt","native cold Lookup discovers corrupt original");
    const auto absent=[&] {
      return marks->marks().empty()&&STVisiblePublishedRegions(store,*original->collection.shots.front().metadata,settings.Visibility()).empty()&&
          !STVisibleStrokeHit(store,*marks,settings.Visibility(),context.displays,point(40,20),1)&&
          STPresentedNoteBadges(store,*marks,id,settings.Visibility(),context.displays).empty()&&
          ![coordinator noteBadgeAtPoint:badges.front().anchor]&&![coordinator click:point(40,20)]&&
          ![coordinator handleNoteBadgeClick:badges.front().anchor];
    };
    Require(absent(),"corrupt original immediately clears production paint geometry stroke/number hits and warmed badge cache");
    auto other=context;other.window_id=9;other.window_title="B";observe(other);observe(context);
    Require(absent(),"native A to B to A cannot resurrect corrupt paint stroke or number targets");
    Require(store.LookupMetadata(id)->state==ReferenceJobState::kFailed&&marks->inspector_jobs().size()==1,
        "native corruption remains an explicit historical failed status");
  }
  std::filesystem::remove_all(root);
  std::cout<<"Production cold-corrupt paint/stroke/number/cache/ABA withdrawal checks passed\n";
}

@interface STFallbackCoordinator : STLatencyClickCoordinator
- (seethis::core::MarkController*)configureFallback:(seethis::core::ReferenceStore*)store
    settings:(seethis::core::SettingsStore*)settings interaction:(seethis::core::InteractionController*)interaction
    deletion:(seethis::core::DeleteGestureController*)deletion context:(const seethis::core::Context&)context;
- (BOOL)pagePending;
- (void)bindPendingContext:(const seethis::core::Context&)context generation:(std::uint64_t)generation;
@end
@implementation STFallbackCoordinator
- (seethis::core::MarkController*)configureFallback:(seethis::core::ReferenceStore*)store
    settings:(seethis::core::SettingsStore*)settings interaction:(seethis::core::InteractionController*)interaction
    deletion:(seethis::core::DeleteGestureController*)deletion context:(const seethis::core::Context&)context {
  auto* marks=[self configureStore:store settings:settings interaction:interaction deletion:deletion displays:context.displays];
  STNoteVisibilitySource source;source.marks=marks;source.lookup=[store](auto id){return store->LookupMetadata(id);};
  source.snapshots=[store](auto id){return store->MarkSnapshots(id);};source.collection=[store](auto id){return store->Collection(id);};
  source.displays=[context]{return context.displays;};source.foregroundPID=[context]{return context.pid;};
  source.screenReady=[]{return true;};source.visibility=[settings]{return settings->Visibility();};
  _noteVisibility=[[STNoteVisibilityCoordinator alloc] initWithEditor:nil source:std::move(source)];
  marks->SetViewportContinuity(1,true);marks->SetDisplayObservation(context.displays);return marks;
}
- (BOOL)pagePending {return _chromeLifecycle.pending();}
- (void)bindPendingContext:(const seethis::core::Context&)context generation:(std::uint64_t)generation {
  _captureContext=context;_generation=generation;
  _capturePageIdentity=context.scope.kind==seethis::core::CaptureScopeKind::kPage?_marks->page_observation().identity:std::nullopt;
}
- (BOOL)inspectorVisible {return NO;}
@end
static void TestFrozenScopeNativePipeline() {
  using namespace seethis::core;
  auto fake=Configure();GrantAndDrain(fake);
  std::atomic<int> pageReads=0;
  ChromePageSample sample;sample.availability=PageAvailability::kReady;sample.browser_pid=123;
  sample.window_id=500;sample.window_bounds={0,0,100,100};sample.process_start_identity_us=1000;
  sample.opaque_tab_id="owned-A";sample.url="https://example.test/A";
  WindowObservation content;content.availability=WindowAvailability::kReady;content.pid=123;
  content.bundle_id="com.google.Chrome";content.window_id=500;content.bounds=sample.window_bounds;
  content.native_window={1,1000,"fallback-owned",1};sample.content_window=content;
  {std::lock_guard lock(fake->mutex);fake->hold_provider=true;}
  SetChromeConnectionTestAdapters({{[fake]{return fake->Discover();},
      [fake](auto pid,bool ask){return fake->Permission(pid,ask);}},
      [fake](auto){return fake->Provider();},[fake]{return fake->Foreground();},
      [](WindowObservation& window){window.browser_content=true;return true;},
      [fake,&sample,&pageReads](const WindowObservation&){const auto captured=sample;++pageReads;(void)fake->Provider();return captured;}});
  char pattern[]="/tmp/seethis-fallback-native-XXXXXX";const char* directory=mkdtemp(pattern);Require(directory,"owned fallback directory");
  {
    ReferenceStore store(std::filesystem::path(directory)/"references");SettingsStore settings(std::filesystem::path(directory)/"settings.json");
    InteractionController interaction;DeleteGestureController deletion;
    Context context;context.pid=context.window_pid=123;context.self_pid=99;context.window_id=500;
    context.app_name="Chrome fixture";context.bundle_id="com.google.Chrome";context.window={0,0,100,100};
    context.quartz_to_appkit_top=100;context.observed=Now();context.native_window=content.native_window;
    context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.excluded_window_ids={700};context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    context.displays={{"owned-display",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}}};
    context.scope={1,CaptureScopeKind::kWindow,1,1000,true};
    STFallbackCoordinator* coordinator=[STFallbackCoordinator new];
    auto* marks=[coordinator configureFallback:&store settings:&settings interaction:&interaction deletion:&deletion context:context];
    Require(!marks->NeedsChromePageObservation(),"empty production publication has no historical Chrome demand");
    auto raw=content;raw.application_available=true;raw.application_start_identity_us=1000;
    raw.observed=Now();raw.generation=1;raw.window_id=999;raw.bounds={0,0,12,9};
    [coordinator publishForegroundObservation:raw];PumpUntil([&]{return pageReads==1;});
    const DisplayPoint point{1,CoordinateUnit::kLogicalPoints,{20,20},1};
    auto applicationContext=context;applicationContext.window_id=0;applicationContext.window_pid=0;applicationContext.window={};
    applicationContext.native_window={};applicationContext.selection_method="frontmost-application/selected-display";
    applicationContext.scope.kind=CaptureScopeKind::kApplication;
    auto basic=FreezeCaptureScope(applicationContext,point,marks->window_observation(),marks->page_observation(),true);
    Require(basic&&basic->scope.kind==CaptureScopeKind::kApplication,"missing selected window plus pending page chooses immediate application scope");
    auto normalForeground=content;normalForeground.observed=Now();normalForeground.generation=1;
    const auto windowBasic=FreezeCaptureScope(context,point,normalForeground,marks->page_observation(),false);
    Require(windowBasic&&windowBasic->scope.kind==CaptureScopeKind::kWindow,"matching fresh native point/foreground window retains basic scope without Automation");
    auto generation=marks->Begin(std::string(32,'a'));Require(marks->BindContext(generation,*basic),"first basic shot binds without waiting for provider");
    [coordinator bindPendingContext:*basic generation:generation];
    CaptureResult capture;capture.requested=Now();capture.completed=Now();capture.pixels={100,100,Bytes(40000,255)};
    marks->Captured(generation,capture);
    fake->ReleaseProvider();PumpUntil([&]{return ![coordinator pagePending]&&ChromePhysicalWorkCountForTesting()==0;});
    Require(marks->page_observation().availability==PageAvailability::kReady&&marks->window_observation().browser_content,
        "empty store real production refresh creates a matched fresh page/window pair");
    Require(!marks->BindPageIdentity(generation,*marks->page_observation().identity)&&marks->AllowsDrawing(),
        "late Ready cannot narrow or cancel already frozen application capture");
    marks->Release(generation,std::vector<std::vector<DisplayPoint>>{{point,{1,CoordinateUnit::kLogicalPoints,{60,20},1}}},Now());
    store.WaitForIdleForTesting();Require(store.LookupMetadata(std::string(32,'a'))->state==ReferenceJobState::kReady&&
        store.LookupMetadata(std::string(32,'a'))->context.scope.kind==CaptureScopeKind::kApplication,"basic shot persists original scope after late Ready");
    auto page=FreezeCaptureScope(context,point,marks->window_observation(),marks->page_observation(),true);
    Require(page&&page->scope.kind==CaptureScopeKind::kPage,"later gesture reaches page scope from real callback rather than prebound identity");
    generation=marks->Begin(std::string(32,'b'));Require(marks->BindContext(generation,*page),"next page shot freezes current observed identity");
    [coordinator bindPendingContext:*page generation:generation];marks->Captured(generation,capture);
    raw.generation=marks->window_observation().generation+1;raw.observed=Now();
    [coordinator publishForegroundObservation:raw];PumpUntil([&]{return ![coordinator pagePending]&&ChromePhysicalWorkCountForTesting()==0;});
    Require(marks->AllowsDrawing(),"fresh same-page positive association survives visible hover/Find auxiliary raw window");
    marks->Release(generation,std::vector<std::vector<DisplayPoint>>{{point,{1,CoordinateUnit::kLogicalPoints,{60,20},1}}},Now());store.WaitForIdleForTesting();
    Require(store.LookupMetadata(std::string(32,'b'))->context.scope.kind==CaptureScopeKind::kPage,"later page scope persists separately from basic shot");
    const auto retained=store.MarkSnapshots(std::string(32,'b')).marks.size();
    sample.availability=PageAvailability::kUnavailable;raw.generation=marks->window_observation().generation+1;raw.observed=Now();
    [coordinator publishForegroundObservation:raw];PumpUntil([&]{return ![coordinator pagePending]&&ChromePhysicalWorkCountForTesting()==0;});
    Require(marks->marks().size()==1&&store.MarkSnapshots(std::string(32,'b')).marks.size()==retained,
        "page failure hides only page shot while app shot remains and page data is retained");
    sample.availability=PageAvailability::kReady;raw.generation=marks->window_observation().generation+1;raw.observed=Now();
    [coordinator publishForegroundObservation:raw];PumpUntil([&]{return ![coordinator pagePending]&&ChromePhysicalWorkCountForTesting()==0;});
    Require(marks->marks().size()==2,"fresh A recovery restores original page plus app marks");
    // A real raw-window switch withdraws A before delayed provider work. The
    // timed-out physical A result owns no publication authority over fresh B.
    generation=marks->Begin(std::string(32,'d'));Require(marks->BindContext(generation,*page),"pending A starts from its frozen fresh page");
    [coordinator bindPendingContext:*page generation:generation];
    {std::lock_guard lock(fake->mutex);fake->hold_provider=true;}
    raw.window_id=501;raw.bounds={10,0,100,100};raw.generation=marks->window_observation().generation+1;raw.observed=Now();
    const auto beforeSwitchReads=pageReads.load();[coordinator publishForegroundObservation:raw];
    PumpUntil([&]{return pageReads>beforeSwitchReads;});
    Require(marks->marks().size()==1&&!marks->PresentedReferenceVisible(*store.Collection(std::string(32,'b')).shots[0].metadata),
        "changed same-PID raw window must hide old page ink/hit eligibility while provider is blocked");
    PumpUntil([&]{return ![coordinator pagePending];});
    Require(ChromePhysicalWorkCountForTesting()==1&&marks->marks().size()==1,"logical provider timeout retains physical A and cannot restore old window projection");
    sample.window_id=501;sample.window_bounds=raw.bounds;sample.opaque_tab_id="owned-B";sample.url="https://example.test/B";
    sample.content_window->window_id=501;sample.content_window->bounds=raw.bounds;
    raw.generation=marks->window_observation().generation+1;raw.observed=Now();[coordinator publishForegroundObservation:raw];
    PumpUntil([&]{return ChromePhysicalWorkCountForTesting()==2;});
    fake->ReleaseProvider();PumpUntil([&]{return ![coordinator pagePending]&&ChromePhysicalWorkCountForTesting()==0;});
    Require(marks->page_observation().identity&&marks->page_observation().identity->opaque_tab_id=="owned-B"&&!marks->Pending(),
        "late physical A cannot roll back B; positively observed B cancels pending A page capture");
    const auto stalePoint=FreezeCaptureScope(context,point,marks->window_observation(),marks->page_observation(),true);
    Require(stalePoint&&stalePoint->scope.kind==CaptureScopeKind::kApplication,
        "cached positive page tuple cannot replace a different point-selected window with overlapping bounds");
    auto contextB=context;contextB.window_id=501;contextB.window=raw.bounds;
    const auto b=FreezeCaptureScope(contextB,point,marks->window_observation(),marks->page_observation(),true);
    Require(b&&b->scope.kind==CaptureScopeKind::kPage&&b->window_id==501,"fresh healthy B can draw after A timeout");
    generation=marks->Begin(std::string(32,'c'));Require(marks->BindContext(generation,*b),"B page captures independently");
    marks->Captured(generation,capture);marks->Release(generation,std::vector<std::vector<DisplayPoint>>{{point,{1,CoordinateUnit::kLogicalPoints,{60,20},1}}},Now());store.WaitForIdleForTesting();
    sample.window_id=500;sample.window_bounds=context.window;sample.opaque_tab_id="owned-A";sample.url="https://example.test/A";
    sample.content_window->window_id=500;sample.content_window->bounds=context.window;
    raw.window_id=999;raw.bounds={0,0,12,9};raw.generation=marks->window_observation().generation+1;raw.observed=Now();
    [coordinator publishForegroundObservation:raw];PumpUntil([&]{return ![coordinator pagePending]&&ChromePhysicalWorkCountForTesting()==0;});
    Require(marks->PresentedReferenceVisible(*store.Collection(std::string(32,'b')).shots[0].metadata)&&
        !marks->PresentedReferenceVisible(*store.Collection(std::string(32,'c')).shots[0].metadata),"return A restores only original page A while B data remains stored");
    const auto reads=pageReads.load();{std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kDenied,-1743};}
    RefreshChromeConnection();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
    raw.generation=marks->window_observation().generation+1;raw.observed=Now();[coordinator publishForegroundObservation:raw];
    Require(pageReads==reads,"Denied Automation must perform zero native page reads");
    auto fallback=FreezeCaptureScope(context,point,marks->window_observation(),marks->page_observation(),false);
    Require(fallback&&fallback->scope.kind!=CaptureScopeKind::kPage,"Denied page access still permits explicitly labeled basic scope");
    Require(marks->window_observation().window_id==999&&fallback->window_id==0&&fallback->window_pid==0&&
        fallback->scope.kind==CaptureScopeKind::kApplication,
        "Denied point/main versus raw auxiliary mismatch freezes explicit app scope without guessing ownership");
    for(unsigned round=0;round<2;++round) {
      const std::string id(32,static_cast<char>('e'+round));generation=marks->Begin(id);
      Require(marks->BindContext(generation,*fallback),"Denied+Find fallback binds for consecutive rounds");
      [coordinator bindPendingContext:*fallback generation:generation];marks->Captured(generation,capture);
      raw.generation=marks->window_observation().generation+1;raw.observed=Now();[coordinator publishForegroundObservation:raw];
      Require(marks->AllowsDrawing(),"Denied+Find application fallback remains drawable through raw auxiliary refresh");
      marks->Release(generation,std::vector<std::vector<DisplayPoint>>{{point,{1,CoordinateUnit::kLogicalPoints,{60,20},1}},
          {{1,CoordinateUnit::kLogicalPoints,{20,50},1},{1,CoordinateUnit::kLogicalPoints,{60,50},1}}},Now());
      store.WaitForIdleForTesting();const auto shot=store.Collection(id);
      Require(store.LookupMetadata(id)->state==ReferenceJobState::kReady&&shot.shots.size()==1&&
          shot.shots[0].metadata->context.scope.kind==CaptureScopeKind::kApplication&&
          marks->PresentedReferenceVisible(*shot.shots[0].metadata)&&marks->LiveMarkSnapshots(id).marks.size()==2,
          "Denied+Find newly released two-stroke shot persists and visibly projects its frozen app scope");
    }
    Require(pageReads==reads,"Denied auxiliary fallback does not initiate native page reads");
    {std::lock_guard lock(fake->mutex);fake->preflight={ChromeConsent::kNotRequested,-1744};fake->hold_native=true;}
    ConnectChromeExplicit();PumpUntil([&]{return fake->asks==1&&ChromeConnectionStatus().consent_pending;});
    raw.generation=marks->window_observation().generation+1;raw.observed=Now();[coordinator publishForegroundObservation:raw];
    const auto pendingFallback=FreezeCaptureScope(context,point,marks->window_observation(),marks->page_observation(),false);
    Require(pendingFallback&&pendingFallback->scope.kind==CaptureScopeKind::kApplication,
        "Pending optional Automation plus auxiliary mismatch freezes application scope");
    const std::string pendingID(32,'9');generation=marks->Begin(pendingID);
    Require(marks->BindContext(generation,*pendingFallback),"Pending optional permission admits reliable basic capture");
    [coordinator bindPendingContext:*pendingFallback generation:generation];marks->Captured(generation,capture);
    marks->Release(generation,std::vector<std::vector<DisplayPoint>>{{point,{1,CoordinateUnit::kLogicalPoints,{60,20},1}}},Now());
    store.WaitForIdleForTesting();const auto pendingShot=store.Collection(pendingID);
    Require(pendingShot.status==MarkNotesStatus::kReady&&marks->PresentedReferenceVisible(*pendingShot.shots[0].metadata)&&pageReads==reads,
        "Pending+Find newly released basic capture remains visible with zero native page reads");
    fake->Release();PumpUntil([]{return !ChromeConnectionStatus().consent_pending&&ChromePhysicalWorkCountForTesting()==0;});
    Require(store.LookupMetadata(pendingID)->context.scope.kind==CaptureScopeKind::kApplication,
        "later consent completion cannot narrow the Pending-created application shot");
  }
  std::filesystem::remove_all(directory);fake->Release();StopChromeConnection();PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
  std::cout<<"Production empty-store refresh/frozen scope/hover/page failure recovery/optional permission checks PASS\n";
}

@interface STProcessIdentityFixture : NSObject
@property(nonatomic) pid_t processIdentifier;
@property(nonatomic,copy) NSString* bundleIdentifier;
@property(nonatomic,strong) NSDate* launchDate;
@property(nonatomic,getter=isTerminated) BOOL terminated;
@end
@implementation STProcessIdentityFixture
@end
static void TestFinderInstanceProducer() {
  using namespace seethis::core;
  auto* app=[STProcessIdentityFixture new];app.processIdentifier=42;app.bundleIdentifier=@"com.apple.finder";
  unsigned reads=0;const auto observed=Now();
  const auto read=[&](KernelProcessIdentityRecord record,STProcessIdentityFixture* after=nil) {
    return ReadProcessIdentityForTesting((__bridge const void*)app,(__bridge const void*)(after?:app),
        [&](std::int64_t pid){++reads;Require(pid==42,"producer reads only the current exact PID");return record;},observed);
  };
  const KernelProcessIdentityRecord good{sizeof(proc_bsdinfo),42,1,234567};
  auto sample=read(good);
  Require(sample.coherent&&sample.launch_start_identity_us==0&&sample.kernel_start_identity_us==1234567&&reads==1,
      "nil launchDate with valid typed public kernel identity must reach actual capture identity producer");
  app.launchDate=[NSDate dateWithTimeIntervalSince1970:2];sample=read(good);
  Require(sample.launch_start_identity_us==2000000&&sample.kernel_start_identity_us==1234567,
      "one observation retains distinct launchDate and kernel values without changing a frozen source");
  app.launchDate=[NSDate dateWithTimeIntervalSince1970:double(observed.utc_us)/1000000+60];sample=read(good);
  Require(!sample.launch_start_identity_us&&sample.kernel_start_identity_us==1234567,
      "future launchDate is unavailable only for that source and new capture may select valid kernel fallback");
  app.launchDate=[NSDate dateWithTimeIntervalSince1970:2];
  for(const auto bad:std::vector<KernelProcessIdentityRecord>{{-1,42,1,2},{0,42,1,2},
      {sizeof(proc_bsdinfo)-1,42,1,2},{sizeof(proc_bsdinfo)+1,42,1,2},
      {sizeof(proc_bsdinfo),43,1,2},{sizeof(proc_bsdinfo),42,0,1},
      {sizeof(proc_bsdinfo),42,1,1000000},{sizeof(proc_bsdinfo),42,UINT64_MAX,0},
      {sizeof(proc_bsdinfo),42,std::uint64_t(INT64_MAX/1000000),999999},
      {sizeof(proc_bsdinfo),42,std::uint64_t(observed.utc_us/1000000+60),0}}) {
    const auto before=reads;sample=read(bad);
    Require(reads==before+1&&sample.coherent&&!sample.kernel_start_identity_us&&sample.launch_start_identity_us==2000000,
        "denied/short/wrongPID/invalid range/overflow/future kernel read never poisons valid old launchDate or retries");
  }
  auto* replacement=[STProcessIdentityFixture new];replacement.processIdentifier=42;
  replacement.bundleIdentifier=@"com.apple.finder";replacement.launchDate=app.launchDate;
  sample=read(good,replacement);Require(!sample.coherent&&!sample.launch_start_identity_us&&!sample.kernel_start_identity_us,
      "same PID and bundle with another NSRunningApplication instance cannot authorize replacement");
  sample=ReadProcessIdentityForTesting((__bridge const void*)app,(__bridge const void*)app,[&](auto) {
    ++reads;app.terminated=YES;return good;
  },observed);Require(!sample.coherent&&!sample.kernel_start_identity_us,"termination during kernel read is rejected");
  app.terminated=NO;const auto before=reads;app.bundleIdentifier=@"";sample=read(good);
  Require(!sample.coherent&&reads==before,"missing bundle does not initiate a kernel read or pid-only fallback");
  app.bundleIdentifier=@"com.apple.finder";app.launchDate=nil;
  sample=ReadProcessIdentityForTesting((__bridge const void*)app,(__bridge const void*)app,[&](auto) {
    ++reads;app.launchDate=[NSDate dateWithTimeIntervalSince1970:3];return good;
  },observed);Require(sample.coherent&&!sample.launch_start_identity_us&&sample.kernel_start_identity_us==1234567,
      "launchDate appearing mid-sample cannot replace the already observed kernel source");
  std::cout<<"Actual process producer nil/future launchDate, typed kernel validation, replacement and bounded-query checks PASS\n";
}

struct KernelCaptureFixture {
  using Kind=seethis::core::CaptureScopeKind;
  seethis::core::ReferenceStore store;
  seethis::core::SettingsStore settings;
  seethis::core::MarkController marks;
  seethis::core::InteractionController gesture;
  seethis::platform::AppendCaptureMode mode;
  seethis::core::NativeWindowLifetimeTracker launchTracker{"kernel-fixture-L"},kernelTracker{"kernel-fixture-K"};
  STProcessIdentityFixture* app=[STProcessIdentityFixture new];
  Kind kind;bool denyKernel=false;unsigned reads=0,ids=0;std::uint64_t inventory=0,generation=0;
  seethis::core::InteractionState previous=seethis::core::InteractionState::kIdle;
  std::int64_t tick=1000;std::string root;
  explicit KernelCaptureFixture(const std::filesystem::path& path,Kind scope):store(path/"refs"),settings(path/"settings.json"),
      marks(store,[](const auto&){return true;}),kind(scope) {
    app.processIdentifier=42;app.bundleIdentifier=@"com.apple.finder";observe();marks.SetViewportContinuity(1,true);
  }
  static seethis::core::DisplayPoint point(double x=20,double y=20) {return {1,seethis::core::CoordinateUnit::kLogicalPoints,{x,y},1};}
  ProcessIdentitySample sample(seethis::core::Stamp stamp) {
    return ReadProcessIdentityForTesting((__bridge const void*)app,(__bridge const void*)app,[&](auto pid) {
      ++reads;Require(pid==42,"capture producer must query only current process");
      return KernelProcessIdentityRecord{denyKernel?0:int(sizeof(proc_bsdinfo)),42,1,0};
    },stamp);
  }
  std::vector<seethis::core::FrozenDisplay> displays() const {
    return {{"owned-kernel-display",1,{0,0,100,100},{0,0,100,100},1,{1,-1,0,100}}};
  }
  seethis::core::WindowObservation fresh() {
    using namespace seethis::core;WindowObservation w;w.generation=marks.window_observation().generation+1;w.observed=Now();
    const auto identity=sample(w.observed);w.pid=42;w.bundle_id="com.apple.finder";
    w.application_available=identity.coherent&&(identity.launch_start_identity_us||identity.kernel_start_identity_us);
    w.application_start_identity_us=identity.launch_start_identity_us;w.kernel_application_start_identity_us=identity.kernel_start_identity_us;
    w.native_window={1,identity.launch_start_identity_us,{},0};w.kernel_native_window={2,identity.kernel_start_identity_us,{},0};
    if(kind==Kind::kWindow) {
      w.availability=WindowAvailability::kReady;w.window_id=500;w.bounds={0,0,100,100};
      w.window_title="Finder fixture folder";w.title={1,TitleAvailability::kReady};
      if(identity.launch_start_identity_us) {
        launchTracker.Observe(42,identity.launch_start_identity_us,{500},++inventory);w.native_window=launchTracker.Identity(42,identity.launch_start_identity_us,500);
      }
      if(identity.kernel_start_identity_us) {
        kernelTracker.Observe(42,identity.kernel_start_identity_us,{500},++inventory);w.kernel_native_window=kernelTracker.Identity(42,identity.kernel_start_identity_us,500);w.kernel_native_window.version=2;
      }
    }
    return w;
  }
  void observe() {marks.SetDisplayObservation(displays());marks.SetWindowObservation(fresh());}
  seethis::core::Context snapshot() {
    using namespace seethis::core;const auto w=fresh();Context c;c.pid=42;c.self_pid=99;
    c.app_name="Finder fixture";c.bundle_id="com.apple.finder";c.observed=w.observed;c.displays=displays();
    c.quartz_to_appkit_top=100;c.excluded_window_ids={700};c.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
    const bool kernel=w.application_start_identity_us==0;
    c.scope={kernel?2u:1u,kind,1,kernel?w.kernel_application_start_identity_us:w.application_start_identity_us,true,kind==Kind::kWindow};
    if(kernel)c.scope.process_source=ProcessStartSource::kKernelBSDStart;
    if(kind==Kind::kWindow) {
      c.window_pid=42;c.window_id=500;c.window=w.bounds;c.window_title=w.window_title;c.title=w.title;
      c.native_window=kernel?w.kernel_native_window:w.native_window;c.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    } else c.selection_method="frontmost-application/selected-display";
    return c;
  }
  seethis::core::CaptureResult pixels() {
    seethis::core::CaptureResult p;p.requested=seethis::core::Now();p.completed=seethis::core::Now();p.pixels={100,100,seethis::core::Bytes(40000,255)};return p;
  }
  void refresh() {
    using namespace seethis::core;
    STRefreshCaptureTransition(gesture,previous,generation,[&](const auto& input,std::uint64_t& owned) {
      const auto start=STBeginModeCapture(marks,store,mode,std::string(32,static_cast<char>('a'+ids++)),8,input.key_down_monotonic_ms);
      generation=start.generation;owned=generation;root=start.rootID;
      const auto frozen=FreezeCaptureScope(snapshot(),input.session_origin,marks.window_observation(),{},false);
      Require(frozen&&marks.BindContext(generation,*frozen),"actual nil-launchDate producer feeds native capture admission");
      marks.BindCaptureDisplay(generation,1);gesture.SetSelectedDisplayGeometry({1,{0,0},{100,100},1});
      STObserveAndScheduleCapture(gesture,marks,generation,generation,[&]{observe();},[&]{marks.Captured(generation,pixels());});
    },[&](const auto& input){marks.Release(generation,input.subpaths,Now(),input.key_up_monotonic_ms);},[&](const auto&){marks.Abort("fixture idle");});
  }
  void capture() {
    using namespace seethis::core;const auto before=reads;
    Require(gesture.ShortcutKeyDown(++tick,point())==StartResult::kStarted,"kernel capture fresh gesture starts");refresh();
    Require(marks.AllowsDrawing()&&reads==before+2,"one capture snapshot and one foreground refresh share one kernel read each");
    for(unsigned stroke=0;stroke<2;++stroke) {
      Require(gesture.PointerDown(point(20,20+stroke*30),++tick)&&gesture.PointerMoved(point(60,20+stroke*30),++tick)&&
          gesture.PointerUp(point(60,20+stroke*30),++tick),"kernel source accepts both strokes");refresh();
    }
    Require(reads==before+2,"pointer/paint capture transitions never query process identity");
    Require(gesture.ShortcutKeyUp(++tick),"kernel source release completes capture");refresh();store.WaitForIdleForTesting();
    Require(marks.phase()==ReferencePhase::kCommitted,"kernel-source screenshot is durably Ready after actual native scheduling boundary");
  }
};
static void TestKernelCaptureOrchestration() {
  using namespace seethis::core;
  for(const auto kind:{CaptureScopeKind::kWindow,CaptureScopeKind::kApplication})for(const bool append:{false,true}) {
    char pattern[]="/tmp/seethis-kernel-source-XXXXXX";const char* path=mkdtemp(pattern);Require(path,"kernel owned fixture");
    {
      KernelCaptureFixture f(path,kind);
      if(append){f.capture();Require(f.mode.Enable(f.root,true),"kernel Append seed binds");}
      f.capture();f.capture();const auto total=f.marks.marks().size();
      Require(total==(append?3:2),"kernel folder/desktop consecutive rounds preserve all source shots");
      const auto collection=f.store.Collection(f.root);const auto notes=f.store.MarkSnapshots(f.root);
      for(const auto& shot:collection.shots)Require(shot.metadata->context.scope.version==2&&
          shot.metadata->context.scope.process_source==ProcessStartSource::kKernelBSDStart,"each appended shot freezes actual kernel producer source");
      const auto badges=STPresentedNoteBadges(f.store,f.marks,f.root,f.settings.Visibility(),f.displays());
      Require(badges.size()==notes.marks.size()&&f.marks.HitIdentity(f.point(),2,f.displays())==f.root,
          "kernel ink, number buttons and hit share scope eligibility");
      const auto before=f.reads;for(unsigned i=0;i<100;++i){(void)f.marks.marks();(void)f.marks.HitIdentity(f.point(),2,f.displays());}
      Require(f.reads==before,"presentation/history/hit queries never call native process producer");
      f.app.launchDate=[NSDate dateWithTimeIntervalSince1970:2];f.observe();Require(f.marks.marks().size()==total,
          "launchDate later appears with a different value without rebinding old kernel captures");
      f.denyKernel=true;f.observe();Require(f.marks.marks().empty(),"kernel unavailability cannot borrow healthy launchDate source");
      f.denyKernel=false;f.observe();Require(f.marks.marks().size()==total,"same kernel read recovery restores unchanged saved shots");
      if(kind==CaptureScopeKind::kApplication) {
        ViewportContinuity viewport;viewport.Observe(ViewportInput::kHealthy);unsigned queries=0;
        const auto preserved=STSourceFocusPreservesPresentation(ViewportInput::kPrimaryDown,f.point(),true,f.store,f.marks,f.settings.Visibility(),f.displays(),{},
            [](auto){return std::uint64_t{500};},[&](const Context&){++queries;return f.fresh();});
        STObservePresentationInput(viewport,f.marks,ViewportInput::kPrimaryDown,false,preserved);
        Require(queries==0&&f.marks.marks().size()==total,"desktop app click does not invent a window relationship or hide saved application marks");
      }
      auto other=f.fresh();other.pid=43;other.bundle_id="test.other";f.marks.SetWindowObservation(other);
      Require(f.marks.marks().empty(),"kernel desktop/folder switch hides source");f.observe();Require(f.marks.marks().size()==total,"return to same application source restores markers");
      f.marks.SetPresentationPermitted(false);Require(f.marks.marks().empty()&&!f.marks.HitIdentity(f.point(),2,f.displays()),"global hide aligns kernel ink and hit");
      f.marks.SetPresentationPermitted(true);Require(f.store.Delete(f.root)==DeleteResult::kDeleted&&f.marks.LiveShots(f.root).empty(),"kernel mixed capture reference deletion remains whole-reference");
    }
    std::filesystem::remove_all(path);
  }
  std::cout<<"Actual producer to native folder/desktop two-round multi-stroke Append, namespace recovery, click/paint/hit bounds PASS\n";
}

static void TestMixedSourceOwnUIQueryBound() {
  using namespace seethis::core;
  char pattern[]="/tmp/seethis-kernel-own-ui-XXXXXX";const char* path=mkdtemp(pattern);Require(path,"mixed source fixture");
  {
    KernelCaptureFixture f(path,CaptureScopeKind::kWindow);
    f.app.launchDate=[NSDate dateWithTimeIntervalSince1970:1];f.observe();f.capture();
    Require(f.store.Collection(f.root).shots[0].metadata->context.scope.version==1,"mixed first shot retains original launchDate binding");
    Require(f.mode.Enable(f.root,true),"mixed source Append binds original root");
    f.app.launchDate=nil;f.observe();f.capture();f.capture();
    Require(f.store.Collection(f.root).shots.size()==3&&f.marks.marks().size()==2,
        "mixed Append keeps old LS data while only two kernel shots are currently eligible");
    const auto target=f.marks.BeginOwnUIPresentationSource();Require(target&&CaptureProcessSource(target->context)==ProcessStartSource::kKernelBSDStart,
        "actual own-UI query target derives from a currently healthy kernel source");
    f.marks.SetOwnInputFocus(true);f.app.launchDate=[NSDate dateWithTimeIntervalSince1970:1];
    auto both=f.fresh();Require(STPublishOwnUIPresentation(f.marks,*target,both,{},true,1,2),
        "kernel own-UI target accepts both typed sources without unrelated Chrome consent coupling");
    f.marks.SetWindowObservation({WindowAvailability::kUnavailable,f.marks.window_observation().generation+2,Now()});
    Require(f.marks.marks().size()==3,"own-UI cached pair can contain original LS-first and kernel shots together");
    f.app.launchDate=nil;unsigned queries=0;const auto before=f.reads;
    const auto classify=[&] {
      return STSourceFocusPreservesPresentation(ViewportInput::kPrimaryDown,f.point(90,90),true,f.store,f.marks,
          f.settings.Visibility(),f.displays(),{},[](auto){return std::uint64_t{500};},[&](const Context& candidate) {
        ++queries;Require(CaptureProcessSource(candidate)==ProcessStartSource::kLaunchDate,"LS-first ordering must exercise source failure before kernel success");
        const auto* authority=STSourceFocusObservationContext(candidate,target,true);
        Require(authority&&CaptureProcessSource(*authority)==ProcessStartSource::kKernelBSDStart,
            "source query uses current kernel own-UI authority rather than filtering by the first historical LS source");
        return f.fresh();
      });
    };
    Require(classify()&&queries==1&&f.reads==before+1,
        "one dual source sample serves LS-unavailable then healthy kernel candidates at the same destination");
    f.denyKernel=true;queries=0;const auto failedBefore=f.reads;
    Require(!classify()&&queries==1&&f.reads==failedBefore+1,
        "failed dual source sample is cached once instead of issuing per-shot retries");
  }
  std::filesystem::remove_all(path);
  std::cout<<"Mixed LS-first/kernel own-UI source availability and one-query destination bound PASS\n";
}

static void TestChromePropertiesRequestContract() {
  const auto unique=[](OSType type,NSString* id,NSAppleEventDescriptor* container) {
    auto* value=NSAppleEventDescriptor.recordDescriptor;
    [value setDescriptor:[NSAppleEventDescriptor descriptorWithTypeCode:type] forKeyword:keyAEDesiredClass];
    [value setDescriptor:[NSAppleEventDescriptor descriptorWithEnumCode:formUniqueID] forKeyword:keyAEKeyForm];
    [value setDescriptor:[NSAppleEventDescriptor descriptorWithString:id] forKeyword:keyAEKeyData];
    [value setDescriptor:container forKeyword:keyAEContainer];
    return [value coerceToDescriptorType:typeObjectSpecifier];
  };
  const auto window=[&](NSString* browser=@"browser-A",NSString* tab=@"tab-A",NSString* title=@"owned-title") {
    auto* record=NSAppleEventDescriptor.recordDescriptor;::Rect bounds{10,20,800,1200};
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithString:browser] forKeyword:'ID  '];
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithString:title] forKeyword:'pnam'];
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithDescriptorType:typeQDRectangle bytes:&bounds length:sizeof(bounds)] forKeyword:'pbnd'];
    [record setDescriptor:unique('CrTb',tab,unique('cwin',browser,NSAppleEventDescriptor.nullDescriptor)) forKeyword:'acTa'];
    // Extra pALL metadata is transported but never consumed or persisted.
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithString:@"incognito"] forKeyword:'mode'];
    return record;
  };
  const auto tab=[](NSString* id=@"tab-A",NSString* url=@"https://example.test/a") {
    auto* record=NSAppleEventDescriptor.recordDescriptor;
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithString:id] forKeyword:'ID  '];
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithString:url] forKeyword:'URL '];
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithString:@"unused-owned-title"] forKeyword:'pnam'];
    [record setDescriptor:[NSAppleEventDescriptor descriptorWithBoolean:NO] forKeyword:'ldng'];return record;
  };
  auto* expected=NSAppleEventDescriptor.listDescriptor;::Rect bounds{10,20,800,1200};
  for(auto* value:{[NSAppleEventDescriptor descriptorWithString:@"browser-A"],[NSAppleEventDescriptor descriptorWithString:@"owned-title"],
                  [NSAppleEventDescriptor descriptorWithDescriptorType:typeQDRectangle bytes:&bounds length:sizeof(bounds)],
                  [NSAppleEventDescriptor descriptorWithString:@"tab-A"],[NSAppleEventDescriptor descriptorWithString:@"https://example.test/a"]})
    [expected insertDescriptor:value atIndex:0];
  // This is the receiver-type extraction Cocoa's intrinsic Get requires. The
  // old heterogeneous AEList batch is not an object specifier.
  auto* old_batch=NSAppleEventDescriptor.listDescriptor;for(int i=0;i<5;++i)[old_batch insertDescriptor:unique('cwin',@"browser-A",NSAppleEventDescriptor.nullDescriptor) atIndex:0];
  auto* old_event=[NSAppleEventDescriptor appleEventWithEventClass:kAECoreSuite eventID:kAEGetData targetDescriptor:[NSAppleEventDescriptor descriptorWithProcessIdentifier:123] returnID:kAutoGenerateReturnID transactionID:kAnyTransactionID];
  [old_event setParamDescriptor:old_batch forKeyword:keyDirectObject];
  AEDesc bad{};const auto old_status=AEGetParamDesc(old_event.aeDesc,keyDirectObject,typeObjectSpecifier,&bad);AEDisposeDesc(&bad);
  Require(old_status==errAECoercionFail,"actual SDK rejects the old list-valued Get receiver with native -1700");
  const auto run=[&](std::vector<NSAppleEventDescriptor*> records,bool twice,int delay_ms=0,int budget_ms=250,int ae_error=0) {
    unsigned sends=0;bool valid_wire=true;std::vector<double> timeouts;bool result=false;
    ChromeFrontSend send=[&](NSAppleEventDescriptor* event,NSAppleEventSendOptions options,NSTimeInterval remaining,NSError** error) {
      ++sends;if(error)*error=nil;timeouts.push_back(remaining);
      const auto same=[&](NSAppleEventDescriptor* a,NSAppleEventDescriptor* b){return a.descriptorType==b.descriptorType&&[a.data isEqualToData:b.data];};
      valid_wire&=same([event attributeDescriptorForKeyword:keyAddressAttr],[NSAppleEventDescriptor descriptorWithProcessIdentifier:123]);
      valid_wire&=options==(NSAppleEventSendWaitForReply|NSAppleEventSendNeverInteract|NSAppleEventSendDontRecord);
      AEDesc receiver{};const auto status=AEGetParamDesc(event.aeDesc,keyDirectObject,typeObjectSpecifier,&receiver);AEDisposeDesc(&receiver);
      valid_wire&=status==noErr;
      auto* property=[event paramDescriptorForKeyword:keyDirectObject];
      valid_wire&=[property descriptorForKeyword:keyAEDesiredClass].typeCodeValue==typeProperty&&
          [property descriptorForKeyword:keyAEKeyForm].enumCodeValue==formPropertyID&&
          [property descriptorForKeyword:keyAEKeyData].typeCodeValue=='pALL';
      auto* target=[property descriptorForKeyword:keyAEContainer];
      if(sends%3==2) {
        auto* browser=[target descriptorForKeyword:keyAEContainer];
        valid_wire&=[target descriptorForKeyword:keyAEDesiredClass].typeCodeValue=='CrTb'&&
            [target descriptorForKeyword:keyAEKeyForm].enumCodeValue==formUniqueID&&
            [[target descriptorForKeyword:keyAEKeyData].stringValue isEqualToString:@"tab-A"]&&
            [browser descriptorForKeyword:keyAEDesiredClass].typeCodeValue=='cwin'&&
            [browser descriptorForKeyword:keyAEKeyForm].enumCodeValue==formUniqueID&&
            [[browser descriptorForKeyword:keyAEKeyData].stringValue isEqualToString:@"browser-A"]&&
            [browser descriptorForKeyword:keyAEContainer].descriptorType==typeNull;
      } else valid_wire&=[target descriptorForKeyword:keyAEDesiredClass].typeCodeValue=='cwin'&&
          [target descriptorForKeyword:keyAEKeyForm].enumCodeValue==formAbsolutePosition&&
          [target descriptorForKeyword:keyAEKeyData].int32Value==1;
      if(delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      auto* reply=[NSAppleEventDescriptor appleEventWithEventClass:'test' eventID:'test' targetDescriptor:NSAppleEventDescriptor.nullDescriptor returnID:kAutoGenerateReturnID transactionID:kAnyTransactionID];
      [reply setParamDescriptor:[NSAppleEventDescriptor descriptorWithInt32:ae_error] forKeyword:keyErrorNumber];
      if(sends<=records.size())[reply setParamDescriptor:records[sends-1] forKeyword:keyDirectObject];return reply;
    };
    std::thread worker([&]{@autoreleasepool{result=ChromeFrontPropertiesReadForTesting(send,(__bridge const void*)expected,budget_ms,twice);}});worker.join();
    Require(valid_wire,"every actual helper request must use exactPID/no-interaction/single pALL specifier and canonical tab/window anchors");
    Require(sends<=6,"two complete snapshots admit at most six native sends");
    for(std::size_t i=1;i<timeouts.size();++i)Require(timeouts[i]<timeouts[i-1],"all sends consume one strictly decreasing absolute deadline");
    return std::pair{result,sends};
  };
  Require(run({window(),tab(),window(),window(),tab(),window()},true)==std::pair{true,6u},"native typed records form two coherent tuples through actual SDK sender/extraction/parser");
  Require(run({window(),tab(),window()},false)==std::pair{true,3u},"single coherent properties read succeeds");
  auto* missing=window();[missing setDescriptor:NSAppleEventDescriptor.nullDescriptor forKeyword:'pnam'];
  Require(run({missing},false)==std::pair{false,1u},"wrong title property type fails before another send");
  auto* bad_anchor=window();[bad_anchor setDescriptor:unique('ctab',@"tab-A",unique('cwin',@"browser-A",NSAppleEventDescriptor.nullDescriptor)) forKeyword:'acTa'];
  Require(run({bad_anchor},false)==std::pair{false,1u},"generic or foreign tab class is rejected");
  auto* foreign=window();[foreign setDescriptor:unique('CrTb',@"tab-A",unique('cwin',@"browser-B",NSAppleEventDescriptor.nullDescriptor)) forKeyword:'acTa'];
  Require(run({foreign},false)==std::pair{false,1u},"active-tab browser container must equal record browserID");
  Require(run({window(),tab(@"tab-B")},false)==std::pair{false,2u},"returned tab ID must equal requested anchored tab");
  Require(run({window(),tab(),window(@"browser-B")},false)==std::pair{false,3u},"midread front browser change is unavailable");
  Require(run({window(),tab(),window(@"browser-A",@"tab-B")},false)==std::pair{false,3u},"midread active-tab change is unavailable");
  Require(run({window(),tab(),window(@"browser-A",@"tab-A",@"changed")},false)==std::pair{false,3u},"midread title change is unavailable");
  auto* moved=window();::Rect other{11,20,800,1200};[moved setDescriptor:[NSAppleEventDescriptor descriptorWithDescriptorType:typeQDRectangle bytes:&other length:sizeof(other)] forKeyword:'pbnd'];
  Require(run({window(),tab(),moved},false)==std::pair{false,3u},"midread bounds change is unavailable");
  Require(run({window(),tab(),window(),window(),tab(@"tab-A",@"https://example.test/b"),window()},true)==std::pair{false,6u},"second tuple page change cannot be published as first page");
  Require(run({window()},false,0,250,-1700)==std::pair{false,1u},"native coercion error still fails closed without retry");
  const auto timed=run({window(),tab(),window(),window(),tab(),window()},true,30,50);
  Require(!timed.first&&timed.second==2,"cumulative deadline stops later sends, not a fresh budget per native request");
  std::cout<<"Actual SDK Chrome properties request/record/anchor/mutation/shared-deadline contract PASS\n";
}

int main(int argc,char** argv) {
  @autoreleasepool {
    try {
      Require(NSThread.isMainThread,"test must own main thread");
      if(argc==2&&std::string_view(argv[1])=="--finder-instance-producer-only") {TestFinderInstanceProducer();TestKernelCaptureOrchestration();TestMixedSourceOwnUIQueryBound();return 0;}
      if(argc==2&&std::string_view(argv[1])=="--context-fallback-only") {
        TestFrozenScopeNativePipeline();TestChromePropertiesRequestContract();
        TestPhysicalConsentHold(true,false);TestPhysicalConsentHold(false,false);
        TestCaptureRawReleaseIngress();StopChromeConnection();return 0;
      }
      if(argc==2&&std::string_view(argv[1])=="--permission-ordering-component-only") {
        TestCapturedConsentPublicationOrdering();StopChromeConnection();return 0;
      }
      TestColdCorruptNativePresentation();
      if(argc==2&&std::string_view(argv[1])=="--cold-corrupt-presentation-component-only")return 0;
      TestCurrentContextNativeClick();
      if(argc==2&&std::string_view(argv[1])=="--active-context-click-component-only")return 0;
      TestSourceFocusEventIngress();
      if(argc==2&&std::string_view(argv[1])=="--focus-pointer-component-only")return 0;
      TestCaptureRawReleaseIngress();
      if(argc==2&&std::string_view(argv[1])=="--capture-rearm-component-only")return 0;
      TestCapturedConsentPublicationOrdering();
      TestBlockedPreflightAndFreshness();TestSharedPhysicalSaturation();
      TestConsentBlockedPreflightInvalidation(false);TestConsentBlockedPreflightInvalidation(true);
      TestLatePageIdentity(false);TestLatePageIdentity(true);
      TestMainConsumerAndOffMainAdmission();
      std::cout<<"Shared physical admission, blocked ask=false, expiry, slow main and recovery checks PASS\n";
      TestActionsAndMainProgress(false);TestActionsAndMainProgress(true);
      TestQueuedStaleTarget();TestQueuedStaleTarget(true);TestQueuedStaleTarget(false,true);
      TestOutstandingInvalidation(false,true,false);
      TestOutstandingInvalidation(false,false,false);
      TestOutstandingInvalidation(false,false,true);
      TestOutstandingInvalidation(true,false,false);
      TestOutcomes();
      TestPhysicalConsentHold(true,false);TestPhysicalConsentHold(false,false);
      TestPhysicalConsentHold(true,true);TestPhysicalFocusAndBinding();
      TestPhysicalTargetAndPassivePaths();TestPhysicalDrawingInterruption();
      TestPhysicalQueuedTargetAbandonment(false);TestPhysicalQueuedTargetAbandonment(true);
      TestPhysicalNewHoldWhilePending();
      StopChromeConnection();
      PumpUntil([]{return ChromePhysicalWorkCountForTesting()==0;});
      std::cout<<"Chrome consent production actions/input, queue responsiveness, physical holds, fresh gestures, identity, lifecycle and outcomes PASS\n";
    } catch(const std::exception& error) {
      if(current_fake)current_fake->Release();
      std::cerr<<error.what()<<'\n';return 1;
    }
  }
  return 0;
}
