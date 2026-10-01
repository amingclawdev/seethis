#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <os/log.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include "core/reference.h"
#include "platform/platform.h"

namespace seethis::platform {
namespace {
std::string String(NSString* s) {return s.UTF8String?s.UTF8String:"";}
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
  return {DiscoverRunningChrome,[](std::int64_t pid,bool ask) {
    return CheckChromeAutomation(pid,ask).permission;
  }};
}
ChromeProviderState ProbeChromeProvider(ChromeRunningTarget target) {
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
void ConnectChromeExplicit() {
  connection.Connect(ConnectionAdapters());
  const auto state=connection.snapshot();
  os_log(OS_LOG_DEFAULT,"seethis chrome phase=connection action=explicit-connect pid=%{public}lld consent=%{public}s osstatus=%{public}d",
      static_cast<long long>(state.target.pid),
      std::string(ChromeConsentName(state.permission.consent)).c_str(),state.permission.os_status);
  BeginConnectionProbe();
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
    context.window_title=context.bundle_id=="com.google.Chrome"?"":
        String(info[(__bridge NSString*)kCGWindowName]);
    context.window={frame.origin.x,primaryTop-CGRectGetMaxY(frame),frame.size.width,frame.size.height};
    context.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    return context;
  }
  error="No eligible foreground window contains the starting point";return {};
}

#if defined(SEETHIS_WINDOW_OBSERVATION_API)
core::WindowObservation ObserveForegroundWindow(std::uint64_t generation) {
  core::WindowObservation observation;
  observation.generation=generation;observation.observed=core::Now();
  NSRunningApplication* app=NSWorkspace.sharedWorkspace.frontmostApplication;
  const auto self_pid=NSProcessInfo.processInfo.processIdentifier;
  if(!app || app.processIdentifier<=0 || app.processIdentifier==self_pid ||
     !ScreenRecordingReady())return observation;
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
    observation.bounds={frame.origin.x,
      primary_top-CGRectGetMaxY(frame),frame.size.width,frame.size.height};
    return observation;
  }
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
bool RequestChromeAutomationPermission(std::int64_t browser_pid) {
  // This function is reserved for an explicit user action in existing UI;
  // no polling, startup path or automated test calls it.
  return CheckChromeAutomation(browser_pid,true).allowed;
}

ChromePageSample AcquireCurrentChromePage(const core::Context& captured) {
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
  if(!foreground || foreground.processIdentifier!=captured.window_pid ||
     ![foreground.bundleIdentifier isEqualToString:@"com.google.Chrome"]) {
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
  ChromePageSample ready;ready.availability=core::PageAvailability::kReady;
  ready.browser_pid=captured.pid;ready.window_id=correlated->window_id;
  ready.window_bounds=correlated->bounds;
  ready.process_start_identity_us=*start_identity;
  ready.opaque_tab_id=tab.UTF8String?tab.UTF8String:"";
  ready.url=url.UTF8String?url.UTF8String:"";
  return ready;
}

#if defined(SEETHIS_WINDOW_OBSERVATION_API)
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
