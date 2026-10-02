// Compile the real action methods and their adapters; do not start the app.
#import "../src/platform/mac/input_mac.mm"
#import "../src/platform/mac/overlay_mac.mm"
#include <condition_variable>
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
  explicit InputOverlay(seethis::core::InteractionController* value):controller(value) {}
  void Refresh() override {
    ++refreshes;
    const auto state=controller->Snapshot().state;
    if(state==seethis::core::InteractionState::kDrawing)++drawing;
    if(state==seethis::core::InteractionState::kFinished)++finished;
  }
  void RebuildDisplays() override {}
  void UpdatePointerPolicy(const std::optional<seethis::core::DisplayPoint>&) override {}
  std::optional<seethis::core::DisplayPoint> DisplayPointAtGlobalLogical(
      seethis::core::Point2D point) const override {
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
  std::unique_ptr<MacInputAdapter> input;
  const seethis::core::DisplayPoint pointer{1,seethis::core::CoordinateUnit::kLogicalPoints,{10,10},1};
  InputFixture() {
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
    EventHotKeyID identity{kCaptureHotKeySignature,
        generation.value_or(static_cast<UInt32>(input->CaptureGenerationForTesting()))};
    Require(SetEventParameter(event,kEventParamDirectObject,typeEventHotKeyID,
        sizeof(identity),&identity)==noErr,"could not identify Carbon event");
    const auto result=input->HotKeyForTesting(event);ReleaseEvent(event);return result;
  }
  void Raw(CGEventType type,CGKeyCode key) {
    CGEventRef event=CGEventCreateKeyboardEvent(nullptr,key,type==kCGEventKeyDown);
    Require(event!=nullptr,"could not create local keyboard event");
    CGEventSetFlags(event,flags);
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

int main() {
  @autoreleasepool {
    try {
      Require(NSThread.isMainThread,"test must own main thread");
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
