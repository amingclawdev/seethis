// Compile the real action methods and their adapters; do not start the app.
#import "../src/platform/mac/input_mac.mm"
#import "../src/platform/mac/overlay_mac.mm"
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>

using namespace seethis::platform;

static void Require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
template<class Predicate> static void PumpUntil(Predicate predicate) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!predicate() && std::chrono::steady_clock::now()<deadline)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,false);
  Require(predicate(),"main run-loop test deadline exceeded");
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
  bool publish_result=true;
  std::atomic<int> asks=0,worker_discoveries=0,probes=0;
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
    if(ask==static_cast<bool>(NSThread.isMainThread))wrong_thread=true;
    std::unique_lock lock(mutex);
    if(!ask)return preflight;
    ++asks;
    Require(pid==123,"native permission used unexpected PID");
    ready.wait(lock,[&]{return !hold_native;});
    if(publish_result)preflight=result;
    return result;
  }
  void Release() {
    {std::lock_guard lock(mutex);hold_native=false;hold_discovery=false;}
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
  auto fake=std::make_shared<NativeFake>();
  current_fake=fake;
  SetChromeConnectionTestAdapters({
      {[fake]{return fake->Discover();},
       [fake](std::int64_t pid,bool ask){return fake->Permission(pid,ask);}},
      [fake](ChromeRunningTarget) {
        if(NSThread.isMainThread)fake->wrong_thread=true;
        ++fake->probes;std::lock_guard lock(fake->mutex);return fake->provider;
      },[fake]{return fake->Foreground();}});
  StartChromeConnection();RefreshChromeConnection();
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
  RefreshChromeConnection();RequireHidden(view);
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
  PumpUntil([&]{return fake->worker_discoveries==1;});
  if(restart) {StopChromeConnection();StartChromeConnection();RefreshChromeConnection();}
  else fake->Target(quit?ChromeRunningTarget{}:ChromeRunningTarget{123,2000});
  fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
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
  void Idle() const {
    Require(controller.Snapshot().state==seethis::core::InteractionState::kIdle &&
        controller.Snapshot().regions.empty() && overlay.finished==0,
        "interrupted consent hold produced drawing/reference completion");
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
  PumpUntil([&]{return fake->asks==1;});input.Idle();
  Require(input.overlay.drawing==0,"consent entry refreshed drawing overlay before authorization");
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
    PumpUntil([&]{return fake->asks==2;});
    PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});input.Idle();input.Up();
    Require(ChromeConnectionStatus().permission.consent==ChromeConsent::kDenied,
        "denied outcome was not preserved truthfully");
  }
  Require(!fake->wrong_thread,"capture request/preflight used incorrect queue");
}
static void TestPhysicalFocusAndBinding() {
  auto fake=Configure();InputFixture input;input.keys[0]=true;input.Carbon();
  PumpUntil([&]{return fake->asks==1;});fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  input.foreground=999;fake->Foreground({});input.input->FocusChangedForTesting();
  auto settings=input.settings.Get();settings.shortcut_key_code=8;
  Require(input.settings.Update(settings),"binding change failed");input.Bind();
  input.keys[8]=true;input.Carbon();input.Idle();
  Require(input.overlay.drawing==0 && fake->asks==1,"focus/config switch discarded original hold");
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
  input.keys[0]=true;input.Carbon();PumpUntil([&]{return fake->asks==1;});input.Idle();
  Require(ChromeConnectionStatus().target==ChromeRunningTarget{123,2000},
      "first foreground intent used stale cached Chrome readiness");
  fake->Target({123,3000});RefreshChromeConnection();fake->Release();
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  Require(fake->probes==0,"relaunch published obsolete capture authorization");input.Up();
  fake=Configure();fake->Foreground({456,1000});input.keys[0]=true;input.Carbon();input.Idle();
  Require(fake->asks==0 && !ChromeConnectionStatus().consent_pending,
      "foreground mismatch authorized background Chrome");input.Up();
  fake->Foreground({123,0});input.keys[0]=true;input.Carbon();input.Idle();
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
  input.keys[0]=true;input.Carbon();
  PumpUntil([&]{return fake->worker_discoveries==1;});input.Idle();MainProgress();
  fake->Target(quit?ChromeRunningTarget{}:ChromeRunningTarget{123,2000});
  fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  Require(fake->asks==0 && fake->probes==0,"queued hotkey quit/relaunch entered native permission");
  input.Carbon();input.Idle();input.Up();input.Idle();
}
static void TestPhysicalNewHoldWhilePending() {
  auto fake=Configure();InputFixture input;input.keys[0]=true;input.Carbon();
  PumpUntil([&]{return fake->asks==1;});input.Up();input.Idle();
  input.keys[0]=true;input.Carbon();input.Idle();
  Require(fake->asks==1 && ChromeConnectionStatus().consent_pending,
      "new physical gesture duplicated unresolved native permission");
  fake->Release();PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});
  input.Carbon();input.Idle();
  Require(fake->asks==1,"completion resumed a new hold intercepted during pending permission");
  input.keys[0]=false;input.input->WatchdogForTesting();input.Idle();
  input.keys[0]=true;input.Carbon();
  Require(input.controller.Snapshot().state==seethis::core::InteractionState::kDrawing,
      "watchdog release did not permit a later fresh gesture");input.Up();
}
static void TestPhysicalDrawingInterruption() {
  auto fake=Configure();InputFixture input;
  (void)input.controller.ShortcutKeyDown(MonotonicMilliseconds(),input.pointer);
  (void)input.controller.PointerDown(input.pointer);
  auto point=input.pointer;point.position.x=50;
  (void)input.controller.PointerMoved(point);(void)input.controller.PointerUp(point);
  Require(!input.controller.Snapshot().regions.empty(),"drawing fixture did not contain region");
  input.keys[0]=true;input.Carbon();PumpUntil([&]{return fake->asks==1;});input.Idle();
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
  input.foreground=123;input.Carbon();input.input->WatchdogForTesting();input.Idle();
  Require(fake->asks==1,"canceled normal held gesture became repeat authorization");
  input.keys[0]=false;input.input->WatchdogForTesting();input.Idle();
  input.keys[0]=true;input.Carbon();PumpUntil([&]{return fake->asks==2;});
  PumpUntil([&]{return !ChromeConnectionStatus().consent_pending;});input.Idle();input.Up();
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
    PumpUntil([&]{return fake->asks==1;});
    Require(f.input->CaptureHeldForTesting()&&f.overlay.drawing==0,"R2 consent entry must consume hold without drawing");
    f.flags=0;f.Raw(kCGEventFlagsChanged,58);f.flags=kCGEventFlagMaskAlternate;f.Carbon(kEventHotKeyReleased);f.Carbon();
    Require(fake->asks==1&&f.overlay.drawing==0&&f.input->CaptureHeldForTesting(),"R2 modifier/Carbon release bypassed consent hold");
    rawUp(f,f.settings.Get().shortcut_key_code);
    Require(f.overlay.drawing==0,"R2 consent release auto-started drawing");
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

int main(int argc,char** argv) {
  @autoreleasepool {
    try {
      Require(NSThread.isMainThread,"test must own main thread");
      TestSourceFocusEventIngress();
      if(argc==2&&std::string_view(argv[1])=="--focus-pointer-component-only")return 0;
      TestCaptureRawReleaseIngress();
      if(argc==2&&std::string_view(argv[1])=="--capture-rearm-component-only")return 0;
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
      std::cout<<"Chrome consent production actions/input, queue responsiveness, physical holds, fresh gestures, identity, lifecycle and outcomes PASS\n";
    } catch(const std::exception& error) {
      if(current_fake)current_fake->Release();
      std::cerr<<error.what()<<'\n';return 1;
    }
  }
  return 0;
}
