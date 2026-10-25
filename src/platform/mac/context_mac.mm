#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <os/log.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include "core/reference.h"
#include "platform/platform.h"

namespace seethis::platform {
namespace {
std::string String(NSString* s) {return s.UTF8String?s.UTF8String:"";}
core::NativeWindowLifetimeTracker& WindowLifetimes() {
  static core::NativeWindowLifetimeTracker tracker(String(NSUUID.UUID.UUIDString));
  return tracker;
}
std::optional<std::vector<std::uint64_t>> ParseWindowInventory(NSArray* all,std::int64_t pid) {
  if(!all || ![all isKindOfClass:NSArray.class])return std::nullopt;
  std::vector<std::uint64_t> ids,seen;
  for(id entry in all) {
    if(![entry isKindOfClass:NSDictionary.class])return std::nullopt;
    NSDictionary* info=entry;
    id owner=info[(__bridge NSString*)kCGWindowOwnerPID];
    id number=info[(__bridge NSString*)kCGWindowNumber];
    if(![owner isKindOfClass:NSNumber.class] || ![number isKindOfClass:NSNumber.class] ||
       [owner longLongValue]<=0 || [number longLongValue]<=0 ||
       [owner doubleValue]!=[owner longLongValue] ||
       [number doubleValue]!=[number longLongValue])return std::nullopt;
    const auto identifier=[number unsignedLongLongValue];
    if(std::find(seen.begin(),seen.end(),identifier)!=seen.end())return std::nullopt;
    seen.push_back(identifier);
    if([owner longLongValue]==pid)ids.push_back(identifier);
  }
  return ids;
}
core::NativeWindowIdentity NativeIdentity(NSRunningApplication* app,std::uint64_t window_id,NSArray* visible) {
  const auto start=app.launchDate?
      ChromeProcessStartIdentity(app.launchDate.timeIntervalSince1970).value_or(0):0;
  // The complete CGWindow inventory includes minimized, hidden and other-Space
  // windows. Onscreen-only absence is never lifecycle evidence. Nil, denied or
  // unparseable results do not retire previously observed windows.
  if(!app || !start || !ScreenRecordingReady())return {1,start,{},0};
  NSArray* all=CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionAll,kCGNullWindowID));
  const auto ids=ParseWindowInventory(all,app.processIdentifier);
  const auto visible_ids=ParseWindowInventory(visible,app.processIdentifier);
  if(!ids || !visible_ids || std::any_of(visible_ids->begin(),visible_ids->end(),
      [&](const auto id){return std::find(ids->begin(),ids->end(),id)==ids->end();}))
    return {1,start,{},0};
  static std::uint64_t inventory_generation=0;
  WindowLifetimes().Observe(app.processIdentifier,start,*ids,++inventory_generation);
  return WindowLifetimes().Identity(app.processIdentifier,start,window_id);
}
bool StillForeground(NSRunningApplication* captured) {
  NSRunningApplication* current=NSWorkspace.sharedWorkspace.frontmostApplication;
  return current && captured && !captured.terminated &&
      current.processIdentifier==captured.processIdentifier &&
      String(current.bundleIdentifier)==String(captured.bundleIdentifier) &&
      ((!current.launchDate && !captured.launchDate) ||
       (current.launchDate && captured.launchDate &&
        [current.launchDate isEqualToDate:captured.launchDate]));
}
// Read twice from the selected native ID, rather than an unrelated focused AX
// title. A well-formed silently truncated CGWindowName cannot be detected by
// this provider; completeness of that case remains unknown.
std::pair<std::string,core::TitleEvidence> ObserveBoundTitle(NSDictionary* first,
    std::int64_t pid,std::uint64_t identifier,NSArray* second) {
  using A=core::TitleAvailability;
  if(![first isKindOfClass:NSDictionary.class] || pid<=0 || !identifier ||
     ![first[(__bridge NSString*)kCGWindowOwnerPID] isKindOfClass:NSNumber.class] ||
     ![first[(__bridge NSString*)kCGWindowNumber] isKindOfClass:NSNumber.class] ||
     [first[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=pid ||
     [first[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue]!=identifier)
    return {{},{1,A::kInconsistent}};
  const auto decode=[](id value)->std::optional<std::string> {
    if(![value isKindOfClass:NSString.class])return {};
    NSData* bytes=[value dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    if(!bytes)return {};
    return std::string(static_cast<const char*>(bytes.bytes),bytes.length);
  };
  const auto title=decode(first[(__bridge NSString*)kCGWindowName]);
  if(!title)return {{},{1,A::kUnavailable}};
  if(!core::ValidSourceContextText(*title,1024))return {{},{1,A::kInvalidOrOverLimit}};
  if(![second isKindOfClass:NSArray.class])return {{},{1,A::kUnavailable}};
  NSDictionary* matched=nil;
  for(id entry in second) {
    if(![entry isKindOfClass:NSDictionary.class])return {{},{1,A::kInconsistent}};
    NSDictionary* info=entry;
    if(
      [info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue]==identifier) {
    if(matched)return {{},{1,A::kAmbiguous}};
    matched=info;
    }
  }
  if(!matched)return {{},{1,A::kUnavailable}};
  if([matched[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=pid ||
     ![matched[(__bridge NSString*)kCGWindowBounds] isEqual:
         first[(__bridge NSString*)kCGWindowBounds]])return {{},{1,A::kInconsistent}};
  CGRect frame;
  id bounds=first[(__bridge NSString*)kCGWindowBounds];
  if(![bounds isKindOfClass:NSDictionary.class] ||
     !CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)bounds,&frame) ||
     !std::isfinite(frame.origin.x) || !std::isfinite(frame.origin.y) ||
     !std::isfinite(frame.size.width) || !std::isfinite(frame.size.height) ||
     frame.size.width<=0 || frame.size.height<=0)return {{},{1,A::kInconsistent}};
  const auto confirmed=decode(matched[(__bridge NSString*)kCGWindowName]);
  if(!confirmed || *confirmed!=*title)return {{},{1,A::kInconsistent}};
  return {*title,{1,A::kReady}};
}
std::pair<std::string,core::TitleEvidence> ObserveBoundTitle(NSDictionary* first,
    std::int64_t pid,std::uint64_t identifier) {
  NSArray* second=CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionIncludingWindow,static_cast<CGWindowID>(identifier)));
  return ObserveBoundTitle(first,pid,identifier,second);
}
double PrimaryTop() {
  return NSScreen.screens.count == 0 ? 0.0 :
      NSMaxY(NSScreen.screens.firstObject.frame);
}
const char* PageAvailabilityName(core::PageAvailability availability) {
  switch(availability) {
    case core::PageAvailability::kReady:return "ready";
    case core::PageAvailability::kDenied:return "denied";
    case core::PageAvailability::kAmbiguous:return "ambiguous";
    case core::PageAvailability::kTimedOut:return "timed-out";
    case core::PageAvailability::kExpired:return "expired";
    case core::PageAvailability::kUnavailable:return "unavailable";
  }
  return "unknown";
}
struct AutomationCheck {
  bool allowed = false;
  core::PageAvailability failure = core::PageAvailability::kUnavailable;
  ChromePermissionResult permission{ChromeConsent::kUnavailable, 0};
};
AutomationCheck CheckChromeAutomation(std::int64_t browser_pid,bool ask) {
  const char* action=ask?"explicit-consent":"preflight";
  if(browser_pid<=0) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=%{public}s cause=invalid-pid pid=%{public}lld",
        action,static_cast<long long>(browser_pid));
    return {false,core::PageAvailability::kUnavailable,
        {ChromeConsent::kUnavailable,procNotFound}};
  }
  ProcessSerialNumber psn{kNoProcess,kNoProcess};
  OSStatus status=GetProcessForPID(static_cast<pid_t>(browser_pid),&psn);
  if(status!=noErr) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=%{public}s cause=pid-lookup pid=%{public}lld osstatus=%{public}d",
        action,static_cast<long long>(browser_pid),static_cast<int>(status));
    return {false,core::PageAvailability::kUnavailable,
        {ChromeConsent::kUnavailable,status}};
  }
  AEAddressDesc target{};
  status=AECreateDesc(typeProcessSerialNumber,&psn,sizeof(psn),&target);
  if(status!=noErr) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=%{public}s cause=target-descriptor pid=%{public}lld osstatus=%{public}d",
        action,static_cast<long long>(browser_pid),static_cast<int>(status));
    return {false,core::PageAvailability::kUnavailable,
        {ChromeConsent::kUnavailable,status}};
  }
  status=AEDeterminePermissionToAutomateTarget(
      &target,kAECoreSuite,kAEGetData,ask);
  AEDisposeDesc(&target);
  const auto permission=ChromePermissionFromOSStatus(status);
  if(ask)os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
      "seethis chrome phase=%{public}s cause=automation-permission pid=%{public}lld osstatus=%{public}d result=%{public}s",
      action,static_cast<long long>(browser_pid),static_cast<int>(status),
      std::string(ChromeConsentName(permission.consent)).c_str());
  return {status==noErr,status==noErr?core::PageAvailability::kReady:
      status==errAEEventNotPermitted?core::PageAvailability::kDenied:
      core::PageAvailability::kUnavailable,permission};
}

ChromeConnectionController connection;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
std::optional<ChromeConnectionTestAdapters> test_adapters;
#endif

ChromeRunningTarget DiscoverRunningChrome() {
  NSArray<NSRunningApplication*>* apps=[NSRunningApplication
      runningApplicationsWithBundleIdentifier:@"com.google.Chrome"];
  NSRunningApplication* candidate=nil;
  for(NSRunningApplication* app in apps)
    if(!app.terminated && app.processIdentifier>0 &&
       (!candidate || app.processIdentifier<candidate.processIdentifier))
      candidate=app;
  if(!candidate)return {};
  if(!candidate.launchDate)return {candidate.processIdentifier,0};
  const auto start=ChromeProcessStartIdentity(candidate.launchDate.timeIntervalSince1970);
  return {candidate.processIdentifier,start.value_or(0)};
}
ChromeConnectionAdapters ConnectionAdapters() {
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(test_adapters)return test_adapters->connection;
#endif
  return {DiscoverRunningChrome,[](std::int64_t pid,bool ask) {
    return CheckChromeAutomation(pid,ask).permission;
  }};
}
ChromeProviderState ProbeChromeProvider(ChromeRunningTarget target) {
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(test_adapters)return test_adapters->provider?test_adapters->provider(target):
      ChromeProviderState::kFailed;
#endif
  // This content-free check never supplies a mark's page identity. Only the
  // foreground acquisition below may bind window/tab/navigation identity.
  if(DiscoverRunningChrome()!=target)return ChromeProviderState::kFailed;
  NSUInteger running_count=0;
  for(NSRunningApplication* app in [NSRunningApplication
      runningApplicationsWithBundleIdentifier:@"com.google.Chrome"])
    if(!app.terminated && app.processIdentifier>0)++running_count;
  if(running_count!=1)return running_count>1
      ?ChromeProviderState::kAmbiguous:ChromeProviderState::kFailed;
  const auto permission=CheckChromeAutomation(target.pid,false).permission;
  if(permission.consent!=ChromeConsent::kGranted)
    return permission.consent==ChromeConsent::kDenied
        ?ChromeProviderState::kDenied:ChromeProviderState::kUnknown;
  NSAppleScript* script=[[NSAppleScript alloc] initWithSource:
      @"tell application id \"com.google.Chrome\"\n"
       "if (count of windows) is 0 then return \"no-page\"\n"
       "if (count of tabs of front window) is 0 then return \"no-page\"\n"
       "if (id of active tab of front window) is missing value then return \"no-page\"\n"
       "return \"ready\"\nend tell"];
  NSDictionary* error=nil;
  const auto result=[script executeAndReturnError:&error];
  if(DiscoverRunningChrome()!=target)return ChromeProviderState::kFailed;
  if(error) {
    const auto status=[error[NSAppleScriptErrorNumber] intValue];
    return status==errAEEventNotPermitted?ChromeProviderState::kDenied:
        status==errAETimeout?ChromeProviderState::kTimedOut:ChromeProviderState::kFailed;
  }
  if([result.stringValue isEqualToString:@"no-page"])return ChromeProviderState::kNoPage;
  return [result.stringValue isEqualToString:@"ready"]
      ?ChromeProviderState::kReady:ChromeProviderState::kFailed;
}
void BeginConnectionProbe() {
  const auto probe=connection.BeginProbe();
  if(!probe.generation)return;
  // Main-queue completion and the controller generation make timeout/retry/
  // quit/relaunch outcomes single-winner, without publishing private content.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,250'000'000),
      dispatch_get_main_queue(),^{
    connection.Refresh(ConnectionAdapters());
    if(connection.CompleteProbe(probe,ChromeProviderState::kTimedOut))
      os_log(OS_LOG_DEFAULT,"seethis chrome phase=connection-probe result=timed-out generation=%{public}llu",
          static_cast<unsigned long long>(probe.generation));
  });
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
    @autoreleasepool {
      const auto state=ProbeChromeProvider(probe.target);
      dispatch_async(dispatch_get_main_queue(),^{
        connection.Refresh(ConnectionAdapters());
        if(connection.CompleteProbe(probe,state))
          os_log(OS_LOG_DEFAULT,"seethis chrome phase=connection-probe result=%{public}s generation=%{public}llu",
              std::string(ChromeProviderName(state)).c_str(),
              static_cast<unsigned long long>(probe.generation));
      });
    }
  });
}
}
bool ScreenRecordingReady() { return CGPreflightScreenCaptureAccess(); }
ChromeConnectionSnapshot RefreshChromeConnection() {
  connection.Refresh(ConnectionAdapters());return connection.snapshot();
}
ChromeConnectionSnapshot ChromeConnectionStatus() { return connection.snapshot(); }
void StartChromeConnection() { connection.Start(); }
void StopChromeConnection() { connection.Stop(); }
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
void SetChromeConnectionTestAdapters(ChromeConnectionTestAdapters adapters) {
  test_adapters=std::move(adapters);
}
#endif
namespace {
void BeginExplicitChromeConsent(std::optional<ChromeRunningTarget> required_target = {}) {
  const auto adapters=ConnectionAdapters();
  const auto request=connection.BeginConsent(adapters,required_target);
  if(!request.valid()) {
    // Already granted can probe; an unresolved OS request cannot start another.
    if(!required_target || *required_target==connection.snapshot().target)
      BeginConnectionProbe();
    return;
  }
  static dispatch_queue_t consent_queue=dispatch_queue_create(
      "local.seethis.chrome-consent",dispatch_queue_attr_make_with_qos_class(
          DISPATCH_QUEUE_SERIAL,QOS_CLASS_UTILITY,0));
  dispatch_async(consent_queue,^{
    @autoreleasepool {
      std::optional<ChromePermissionResult> result;
      try {
        // Chrome may have exited/relaunched while this request was queued.
        // A stale PID must never trigger consent for its replacement process.
        if(request.native_entry_allowed->load() && adapters.discover &&
            adapters.discover()==request.target && request.native_entry_allowed->load())
          result=adapters.permission(request.target.pid,true);
      } catch(...) {
        result=ChromePermissionResult{ChromeConsent::kUnknown,paramErr};
      }
      dispatch_async(dispatch_get_main_queue(),^{
        connection.Refresh(ConnectionAdapters());
        if(!connection.CompleteConsent(request,result))return;
        const auto state=connection.snapshot();
        os_log(OS_LOG_DEFAULT,"seethis chrome phase=connection action=explicit-connect pid=%{public}lld consent=%{public}s osstatus=%{public}d",
            static_cast<long long>(state.target.pid),
            std::string(ChromeConsentName(state.permission.consent)).c_str(),state.permission.os_status);
        BeginConnectionProbe();
      });
    }
  });
}
}  // namespace
void ConnectChromeExplicit() { BeginExplicitChromeConsent(); }
std::optional<ChromeRunningTarget> GetChromeCaptureConsentTarget() {
  ChromeRunningTarget foreground;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(test_adapters) {
    foreground=test_adapters->foreground_chrome?test_adapters->foreground_chrome():
        ChromeRunningTarget{};
  } else
#endif
  {
    NSRunningApplication* app=NSWorkspace.sharedWorkspace.frontmostApplication;
    if(![app.bundleIdentifier isEqualToString:@"com.google.Chrome"])return {};
    foreground={app.processIdentifier,app.launchDate?
        ChromeProcessStartIdentity(app.launchDate.timeIntervalSince1970).value_or(0):0};
  }
  if(foreground.pid<=0)return {};
  // A foreground switch may have happened immediately before the physical press.
  // Refresh live discovery/preflight rather than trusting a cached ready snapshot.
  const auto state=RefreshChromeConnection();
  if(state.target==foreground && state.permission.consent==ChromeConsent::kGranted &&
      !state.consent_pending)return {};
  return foreground;
}
void ConnectChromeForCaptureExplicit(ChromeRunningTarget foreground_target) {
  if(foreground_target.valid())BeginExplicitChromeConsent(foreground_target);
}
void RetryChromeExplicit() {
  connection.Refresh(ConnectionAdapters());
  const auto state=connection.snapshot();
  os_log(OS_LOG_DEFAULT,"seethis chrome phase=connection action=explicit-retry pid=%{public}lld consent=%{public}s osstatus=%{public}d",
      static_cast<long long>(state.target.pid),
      std::string(ChromeConsentName(state.permission.consent)).c_str(),state.permission.os_status);
  BeginConnectionProbe();
}
bool OpenChromeAutomationSettingsExplicit() {
  const bool opened=[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
      @"x-apple.systempreferences:com.apple.preference.security?Privacy_Automation"]];
  connection.SettingsOpened(opened);return opened;
}
void PublishChromeObservation(std::int64_t pid,bool active,core::PageAvailability page,
                              bool provider_result) {
  connection.Observe(pid,active,page,provider_result);
}

#if defined(SEETHIS_CAPTURE_TESTING)
std::pair<std::string,core::TitleEvidence> ObserveBoundTitleForTest(
    CFDictionaryRef first,CFArrayRef second,std::int64_t pid,std::uint64_t identifier) {
  return ObserveBoundTitle((__bridge NSDictionary*)first,pid,identifier,(__bridge NSArray*)second);
}
std::optional<std::vector<std::uint64_t>> ParseNativeWindowInventoryForTest(
    CFArrayRef inventory,std::int64_t pid) {
  return ParseWindowInventory((__bridge NSArray*)inventory,pid);
}
#endif

std::vector<core::FrozenDisplay> SnapshotDisplays() {
  std::vector<core::FrozenDisplay> displays;
  for(NSScreen* screen in NSScreen.screens) {
    const CGDirectDisplayID id=[screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    CFUUIDRef uuid=CGDisplayCreateUUIDFromDisplayID(id);
    if(!uuid)continue;
    NSString* name=CFBridgingRelease(CFUUIDCreateString(kCFAllocatorDefault,uuid));CFRelease(uuid);
    const auto f=screen.frame;const double scale=screen.backingScaleFactor;
    // Per-display backing raster, not a fictitious mixed-DPI global pixel plane.
    displays.push_back({String(name),id,{f.origin.x,f.origin.y,f.size.width,f.size.height},
      {0,0,f.size.width*scale,f.size.height*scale},scale,
      {scale,-scale,-f.origin.x*scale,(f.origin.y+f.size.height)*scale}});
  }
  return displays;
}
std::optional<core::Context> SnapshotContext(core::DisplayPoint initial,
    const std::vector<std::uint64_t>& overlayIDs,std::string& error) {
  // First Refresh calls here synchronously, before stroke rendering/pointer capture.
  NSRunningApplication* app=NSWorkspace.sharedWorkspace.frontmostApplication;
  core::Context context;context.observed=core::Now();
  context.title={1,core::TitleAvailability::kUnavailable};
  context.pid=app.processIdentifier;context.self_pid=NSProcessInfo.processInfo.processIdentifier;
  context.app_name=String(app.localizedName);context.bundle_id=String(app.bundleIdentifier);
  context.executable=String(app.executableURL.path);context.displays=SnapshotDisplays();
  context.excluded_window_ids=overlayIDs;
  context.exclusion_method="exact-window-allowlist;self-pid-rejected;child-windows-off";
  if(!app || context.pid<=0 || context.pid==context.self_pid) {error="Original foreground application unavailable";return {};}
  if(!ScreenRecordingReady()) {error="Screen Recording permission unavailable; enable it in System Settings and retry";return {};}
  const auto d=std::find_if(context.displays.begin(),context.displays.end(),[&](const auto& x){return x.id==initial.display_id;});
  if(d==context.displays.end() || initial.unit!=core::CoordinateUnit::kLogicalPoints || initial.backing_scale!=d->scale) {
    error="Initial display geometry unavailable";return {};
  }
  const double primaryTop=PrimaryTop();
  context.quartz_to_appkit_top=primaryTop;
  const CGPoint point=CGPointMake(d->logical.x+initial.position.x,primaryTop-d->logical.y-initial.position.y);
  NSArray* windows=CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,kCGNullWindowID));
  for(NSDictionary* info in windows) {
    if([info[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=context.pid ||
       [info[(__bridge NSString*)kCGWindowLayer] intValue]!=0 ||
       [info[(__bridge NSString*)kCGWindowAlpha] doubleValue]<=0)continue;
    CGRect frame;
    if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds],&frame) ||
       !CGRectContainsPoint(frame,point) || frame.size.width<=0 || frame.size.height<=0)continue;
    context.window_id=[info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue];
    context.window_pid=context.pid;context.window_layer=0;
    if(context.bundle_id=="com.google.Chrome")context.title={};
    else {
      const auto title=ObserveBoundTitle(info,context.pid,context.window_id);
      context.window_title=title.first;context.title=title.second;
    }
    context.window={frame.origin.x,primaryTop-CGRectGetMaxY(frame),frame.size.width,frame.size.height};
    context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    context.native_window=NativeIdentity(app,context.window_id,windows);
    if(!StillForeground(app)){error="Foreground application changed during context capture";return {};}
    return context;
  }
  error="No eligible foreground window contains the starting point";return {};
}

#if defined(SEETHIS_WINDOW_OBSERVATION_API)
core::WindowObservation ObserveOwnUIPresentationSource(
    const core::Context& source,std::uint64_t generation) {
  core::WindowObservation unavailable;unavailable.generation=generation;unavailable.observed=core::Now();
  const auto front=NSWorkspace.sharedWorkspace.frontmostApplication;
  if(!front||front.processIdentifier!=NSProcessInfo.processInfo.processIdentifier||
     !ScreenRecordingReady()||source.native_window.version!=1||
     !core::DisplayTopologyMatches(source.displays,SnapshotDisplays()))return unavailable;
  const auto app=[NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(source.pid)];
  if(!app||app.terminated||String(app.bundleIdentifier)!=source.bundle_id)return unavailable;
  NSArray* windows=CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,kCGNullWindowID));
  if(!ParseWindowInventory(windows,source.pid))return unavailable;
  NSDictionary* matched=nil;
  for(NSDictionary* info in windows) {
    if([info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue]!=source.window_id)continue;
    if(matched||[info[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=source.pid||
       [info[(__bridge NSString*)kCGWindowLayer] intValue]!=0||
       [info[(__bridge NSString*)kCGWindowAlpha] doubleValue]<=0)return unavailable;
    matched=info;
  }
  if(!matched)return unavailable;
  CGRect frame;
  if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)matched[(__bridge NSString*)kCGWindowBounds],&frame))return unavailable;
  core::WindowObservation observation;
  observation.availability=core::WindowAvailability::kReady;observation.generation=generation;
  observation.pid=source.pid;observation.bundle_id=source.bundle_id;observation.window_id=source.window_id;
  observation.bounds={frame.origin.x,PrimaryTop()-CGRectGetMaxY(frame),frame.size.width,frame.size.height};
  observation.native_window=NativeIdentity(app,source.window_id,windows);
  if(source.bundle_id!="com.google.Chrome") {
    const auto title=ObserveBoundTitle(matched,source.pid,source.window_id);
    observation.window_title=title.first;observation.title=title.second;
  }
  observation.observed=core::Now();
  if(!StillForeground(front)||!core::WindowObservationMatchesContext(source,observation)||
     observation.native_window.observation_session!=source.native_window.observation_session||
     observation.native_window.incarnation!=source.native_window.incarnation||
     (source.bundle_id!="com.google.Chrome"&&!core::TitleContextVisible(source,observation)))return unavailable;
  return observation;
}
core::WindowObservation ObserveForegroundWindow(std::uint64_t generation) {
  core::WindowObservation observation;
  observation.generation=generation;observation.observed=core::Now();
  NSRunningApplication* app=NSWorkspace.sharedWorkspace.frontmostApplication;
  const auto self_pid=NSProcessInfo.processInfo.processIdentifier;
  if(!app || app.processIdentifier<=0 || app.processIdentifier==self_pid ||
     !ScreenRecordingReady()) {
    if(app && !ScreenRecordingReady())observation.title.availability=core::TitleAvailability::kDenied;
    return observation;
  }
  NSArray* windows=CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,
      kCGNullWindowID));
  const double primary_top=PrimaryTop();
  for(NSDictionary* info in windows) {
    if([info[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=
           app.processIdentifier ||
       [info[(__bridge NSString*)kCGWindowLayer] intValue]!=0 ||
       [info[(__bridge NSString*)kCGWindowAlpha] doubleValue]<=0)continue;
    CGRect frame;
    if(!CGRectMakeWithDictionaryRepresentation(
          (__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds],
          &frame) || frame.size.width<=0 || frame.size.height<=0)continue;
    observation.availability=core::WindowAvailability::kReady;
    observation.pid=app.processIdentifier;
    observation.bundle_id=String(app.bundleIdentifier);
    observation.window_id=
        [info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue];
    observation.native_window=NativeIdentity(app,observation.window_id,windows);
    if(observation.bundle_id!="com.google.Chrome") {
      const auto title=ObserveBoundTitle(info,observation.pid,observation.window_id);
      observation.window_title=title.first;observation.title=title.second;
    }
    observation.bounds={frame.origin.x,
      primary_top-CGRectGetMaxY(frame),frame.size.width,frame.size.height};
    if(!StillForeground(app)) {
      core::WindowObservation unavailable;unavailable.generation=generation;
      unavailable.observed=core::Now();return unavailable;
    }
    return observation;
  }
  // A valid full inventory can observe disappearance even with no normal window.
  (void)NativeIdentity(app,0,windows);
  return observation;
}
#endif

void AcquireChromePageAsync(ChromePageAcquire acquire,
                            std::uint64_t generation,
                            ChromePageCompletion completion) {
  // The worker owns the provider call and the main queue owns publication.
  // A timeout wins exactly once; late results are discarded by this latch and
  // by MarkController's generation/age checks.
  auto finished=std::make_shared<std::atomic_bool>(false);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,250'000'000),
                 dispatch_get_main_queue(),^{
    if(finished->exchange(true))return;
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=async-acquire cause=timeout generation=%{public}llu",
        static_cast<unsigned long long>(generation));
    core::PageObservation timeout;timeout.availability=core::PageAvailability::kTimedOut;
    timeout.generation=generation;timeout.observed=core::Now();completion(std::move(timeout));
  });
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
    ChromePageSample sample;
    try { sample=acquire?acquire():ChromePageSample{}; }
    catch(...) {
      sample.availability=core::PageAvailability::kUnavailable;
      os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
          "seethis chrome phase=async-acquire cause=provider-exception generation=%{public}llu",
          static_cast<unsigned long long>(generation));
    }
    core::PageObservation observation;observation.generation=generation;
    observation.observed=core::Now();observation.availability=sample.availability;
    if(sample.availability==core::PageAvailability::kReady) {
      observation.identity=MakeChromePageIdentity(sample);
      if(observation.identity)
        observation.captured_page=MakeChromeCapturedPageMetadata(sample,observation.observed);
      if(!observation.identity) {
        observation.availability=core::PageAvailability::kUnavailable;
        os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
            "seethis chrome phase=identity-validation cause=invalid-sample generation=%{public}llu",
            static_cast<unsigned long long>(generation));
      }
    }
    dispatch_async(dispatch_get_main_queue(),^{
      if(finished->exchange(true)) {
        os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
            "seethis chrome phase=async-acquire cause=late-result generation=%{public}llu",
            static_cast<unsigned long long>(generation));
        return;
      }
      os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
          "seethis chrome phase=async-acquire cause=provider-result generation=%{public}llu result=%{public}s",
          static_cast<unsigned long long>(generation),
          PageAvailabilityName(observation.availability));
      completion(std::move(observation));
    });
  });
}

bool ChromeAutomationPreflight(std::int64_t browser_pid) {
  // askIfNeeded=false is intentional: delivery and automated tests are
  // prompt-free. The user can grant SeeThis -> Google Chrome in System
  // Settings > Privacy & Security > Automation and retry the action.
  return CheckChromeAutomation(browser_pid,false).allowed;
}

static ChromePageSample AcquireChromePage(const core::Context& captured,bool own_ui) {
  ChromePageSample unavailable;unavailable.browser_pid=captured.pid;
  unavailable.window_id=captured.window_id;unavailable.window_bounds=captured.window;
  unavailable.availability=core::PageAvailability::kUnavailable;
  if(captured.bundle_id!="com.google.Chrome") {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=target-validation cause=non-chrome pid=%{public}lld window=%{public}llu",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    return unavailable;
  }
  const auto permission=CheckChromeAutomation(captured.pid,false);
  if(!permission.allowed) {
    unavailable.availability=permission.failure;return unavailable;
  }
  NSRunningApplication* foreground=NSWorkspace.sharedWorkspace.frontmostApplication;
  if(!foreground || (own_ui?
      foreground.processIdentifier!=NSProcessInfo.processInfo.processIdentifier:
      (foreground.processIdentifier!=captured.window_pid ||
       ![foreground.bundleIdentifier isEqualToString:@"com.google.Chrome"]))) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=target-validation cause=foreground-mismatch pid=%{public}lld window=%{public}llu",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    unavailable.availability=core::PageAvailability::kUnavailable;return unavailable;
  }
  // This is the only real provider call. It is made only after the prompt-free
  // permission preflight and returns an ephemeral tab ID plus URL; the caller
  // immediately reduces the URL to a digest before publication.
  NSAppleScript* script=[[NSAppleScript alloc]
      initWithSource:@"tell application \"Google Chrome\" to tell active tab of front window to ((id as string) & \"|\" & (URL as string))"];
  NSDictionary* script_error=nil;
  NSAppleEventDescriptor* result=[script executeAndReturnError:&script_error];
  NSString* value=result.stringValue;
  if(value.length==0) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=provider-script cause=empty-result pid=%{public}lld window=%{public}llu script-code=%{public}ld",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id),
        static_cast<long>([script_error[NSAppleScriptErrorNumber] integerValue]));
    return unavailable;
  }
  NSRange separator=[value rangeOfString:@"|"];
  if(separator.location==NSNotFound) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=provider-script cause=invalid-shape pid=%{public}lld window=%{public}llu",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    return unavailable;
  }
  NSString* tab=[value substringToIndex:separator.location];
  NSString* url=[value substringFromIndex:separator.location+1];
  if(tab.length==0||url.length==0) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=provider-script cause=missing-field pid=%{public}lld window=%{public}llu",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    return unavailable;
  }

  NSArray* windows=CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,
      kCGNullWindowID));
  std::vector<ChromeWindowCandidate> candidates;
  for(NSUInteger index=0;index<windows.count;++index) {
    NSDictionary* info=windows[index];
    if([info[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=captured.pid ||
       [info[(__bridge NSString*)kCGWindowLayer] intValue]!=0)continue;
    CGRect frame;
    if(!CGRectMakeWithDictionaryRepresentation(
          (__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds],&frame))continue;
    candidates.push_back({captured.pid,
      [info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue],
      {frame.origin.x,captured.quartz_to_appkit_top-frame.origin.y-frame.size.height,
       frame.size.width,frame.size.height},static_cast<std::int64_t>(index),true});
  }
  const auto correlated=CorrelateChromeWindow(captured,candidates);
  if(!correlated) {
    unavailable.availability=candidates.empty()?core::PageAvailability::kUnavailable:
        core::PageAvailability::kAmbiguous;
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=window-correlation cause=%{public}s pid=%{public}lld window=%{public}llu",
        candidates.empty()?"no-candidates":"no-unique-match",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    return unavailable;
  }
  auto top=std::min_element(candidates.begin(),candidates.end(),
      [](const auto& left,const auto& right) {
        return left.eligible&&(!right.eligible||left.z_order<right.z_order);
      });
  if(top==candidates.end()||top->window_id!=correlated->window_id) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=window-correlation cause=not-front-window pid=%{public}lld window=%{public}llu",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    unavailable.availability=core::PageAvailability::kUnavailable;return unavailable;
  }
  const auto running=[NSRunningApplication runningApplicationWithProcessIdentifier:
      static_cast<pid_t>(captured.pid)];
  if(!running||!running.launchDate) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=process-identity cause=missing-launch-date pid=%{public}lld",
        static_cast<long long>(captured.pid));
    return unavailable;
  }
  const auto start_identity=ChromeProcessStartIdentity(
      running.launchDate.timeIntervalSince1970);
  if(!start_identity) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=process-identity cause=invalid-launch-date pid=%{public}lld",
        static_cast<long long>(captured.pid));
    return unavailable;
  }
  if(!StillForeground(foreground)||!CheckChromeAutomation(captured.pid,false).allowed||
     (own_ui&&*start_identity!=captured.native_window.process_start_identity_us))return unavailable;
  ChromePageSample ready;ready.availability=core::PageAvailability::kReady;
  ready.browser_pid=captured.pid;ready.window_id=correlated->window_id;
  ready.window_bounds=correlated->bounds;
  ready.process_start_identity_us=*start_identity;
  ready.opaque_tab_id=tab.UTF8String?tab.UTF8String:"";
  ready.url=url.UTF8String?url.UTF8String:"";
  return ready;
}

ChromePageSample AcquireCurrentChromePage(const core::Context& captured) {
  return AcquireChromePage(captured,false);
}
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
ChromePageSample AcquireOwnUIPresentationChromePage(const core::Context& source) {
  return AcquireChromePage(source,true);
}
ChromePageSample AcquireCurrentChromePage(
    const core::WindowObservation& foreground) {
  core::Context context;
  context.observed=foreground.observed;
  context.pid=foreground.pid;context.window_pid=foreground.pid;
  context.bundle_id=foreground.bundle_id;context.window_id=foreground.window_id;
  context.window=foreground.bounds;context.quartz_to_appkit_top=PrimaryTop();
  if(foreground.availability!=core::WindowAvailability::kReady)
    return ChromePageSample{};
  return AcquireCurrentChromePage(context);
}
#endif
}  // namespace seethis::platform
