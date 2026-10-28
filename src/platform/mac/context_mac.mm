#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <os/log.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <libproc.h>
#include <sys/proc_info.h>
#include "core/reference.h"
#include "platform/platform.h"

namespace seethis::platform {
namespace {
std::string String(NSString* s) {return s.UTF8String?s.UTF8String:"";}
using KernelIdentityRead=std::function<KernelProcessIdentityRecord(std::int64_t)>;
using ApplicationLookup=std::function<NSRunningApplication*(std::int64_t)>;
KernelProcessIdentityRecord ReadKernelProcessIdentity(std::int64_t pid) {
  proc_bsdinfo info{};
  const int bytes=proc_pidinfo(static_cast<int>(pid),PROC_PIDTBSDINFO,0,&info,sizeof(info));
  // Only identity fields leave this stack frame; names/UIDs are not decoded.
  return {bytes,info.pbi_pid,info.pbi_start_tvsec,info.pbi_start_tvusec};
}
ProcessIdentitySample SampleProcessIdentity(NSRunningApplication* app,core::Stamp observed,
    KernelIdentityRead read={},ApplicationLookup lookup={}) {
  ProcessIdentitySample result;
  if(!app||app.terminated||app.processIdentifier<=0||!app.bundleIdentifier.length)return result;
  const auto pid=app.processIdentifier;const auto bundle=String(app.bundleIdentifier);
  const auto launch=app.launchDate?ChromeProcessStartIdentity(app.launchDate.timeIntervalSince1970).value_or(0):0;
  const auto kernel=read?read(pid):ReadKernelProcessIdentity(pid);
  NSRunningApplication* after=lookup?lookup(pid):[NSRunningApplication runningApplicationWithProcessIdentifier:pid];
  if(!after||after.terminated||app.terminated||after.processIdentifier!=pid||
     String(after.bundleIdentifier)!=bundle||![app isEqual:after])return result;
  result.coherent=true;
  const auto after_launch=after.launchDate?ChromeProcessStartIdentity(after.launchDate.timeIntervalSince1970).value_or(0):0;
  if(launch>0&&launch==after_launch&&observed.utc_us>0&&launch<=observed.utc_us)result.launch_start_identity_us=launch;
  if(kernel.returned_bytes!=sizeof(proc_bsdinfo)||kernel.pid!=pid||kernel.seconds==0||
     kernel.microseconds>=1000000||kernel.seconds>static_cast<std::uint64_t>(INT64_MAX/1000000))return result;
  const auto seconds=static_cast<std::int64_t>(kernel.seconds)*1000000;
  if(kernel.microseconds>static_cast<std::uint64_t>(INT64_MAX-seconds))return result;
  const auto start=seconds+static_cast<std::int64_t>(kernel.microseconds);
  if(start>0&&observed.utc_us>0&&start<=observed.utc_us)result.kernel_start_identity_us=start;
  return result;
}
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
core::NativeWindowLifetimeTracker& KernelWindowLifetimes() {
  static core::NativeWindowLifetimeTracker tracker(String(NSUUID.UUID.UUIDString));return tracker;
}
struct NativeIdentityPair {core::NativeWindowIdentity launch,kernel;};
NativeIdentityPair NativeIdentities(NSRunningApplication* app,std::uint64_t window_id,
    NSArray* visible,const ProcessIdentitySample& sample) {
  const auto launch=sample.launch_start_identity_us,kernel=sample.kernel_start_identity_us;
  NativeIdentityPair result{{1,launch,{},0},{2,kernel,{},0}};
  if(!app||!sample.coherent||(!launch&&!kernel)||!ScreenRecordingReady())return result;
  // A single complete inventory feeds separate lifetime/source namespaces.
  NSArray* all=CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionAll,kCGNullWindowID));
  const auto ids=ParseWindowInventory(all,app.processIdentifier);
  const auto visible_ids=ParseWindowInventory(visible,app.processIdentifier);
  if(!ids||!visible_ids||std::any_of(visible_ids->begin(),visible_ids->end(),
      [&](const auto id){return std::find(ids->begin(),ids->end(),id)==ids->end();}))return result;
  static std::uint64_t launch_generation=0,kernel_generation=0;
  if(launch) {
    WindowLifetimes().Observe(app.processIdentifier,launch,*ids,++launch_generation);
    result.launch=WindowLifetimes().Identity(app.processIdentifier,launch,window_id);
  }
  if(kernel) {
    KernelWindowLifetimes().Observe(app.processIdentifier,kernel,*ids,++kernel_generation);
    result.kernel=KernelWindowLifetimes().Identity(app.processIdentifier,kernel,window_id);result.kernel.version=2;
  }
  return result;
}
core::NativeWindowIdentity NativeIdentity(NSRunningApplication* app,std::uint64_t window_id,NSArray* visible) {
  // Existing Chrome page/consent consumers retain launchDate meaning. They do
  // not acquire a new kernel source or change the native properties budget.
  ProcessIdentitySample sample;sample.coherent=app&&!app.terminated;
  sample.launch_start_identity_us=app.launchDate?
      ChromeProcessStartIdentity(app.launchDate.timeIntervalSince1970).value_or(0):0;
  return NativeIdentities(app,window_id,visible,sample).launch;
}
bool StillForeground(NSRunningApplication* captured) {
  NSRunningApplication* current=NSWorkspace.sharedWorkspace.frontmostApplication;
  return current&&captured&&!captured.terminated&&!current.terminated&&
      current.processIdentifier==captured.processIdentifier&&
      String(current.bundleIdentifier)==String(captured.bundleIdentifier)&&[current isEqual:captured];
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
thread_local std::shared_ptr<std::atomic_bool> chrome_worker_lifetime;
struct AutomationCheck {
  bool allowed = false;
  core::PageAvailability failure = core::PageAvailability::kUnavailable;
  ChromePermissionResult permission{ChromeConsent::kUnavailable, 0};
};
AutomationCheck CheckChromeAutomation(std::int64_t browser_pid,bool ask) {
  // Prompt-free Automation IPC may still block. Never enter it on main.
  if(NSThread.isMainThread || !chrome_worker_lifetime || !chrome_worker_lifetime->load())
    return {false,core::PageAvailability::kUnavailable,
      {ChromeConsent::kUnavailable,paramErr}};
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
ChromePhysicalWorkGate chrome_work_gate;
std::shared_ptr<std::atomic_bool> connection_lifetime=
    std::make_shared<std::atomic_bool>(true);
// All fields below are main-thread owned. Never reset physical admission on
// target/lifecycle invalidation; a stuck native call still owns its record.
struct ChromeWorkRecord {
  bool logical_finished=false;
  __strong NSTimer* timer=nil;
  ~ChromeWorkRecord() { chrome_work_gate.Leave(); }
};
bool RunChromeWork(std::function<void()> worker,
                   std::function<void(bool)> completed,
                   std::function<void()> timeout={},
                   std::function<void()> after_completed={}) {
  if(!NSThread.isMainThread || !chrome_work_gate.TryEnter())return false;
  __block auto record=std::make_shared<ChromeWorkRecord>();
  if(timeout) {
    record->timer=[NSTimer timerWithTimeInterval:0.25 repeats:NO block:^(NSTimer*) {
      record->timer=nil;
      if(record->logical_finished)return;
      record->logical_finished=true;
      timeout();
    }];
    [NSRunLoop.mainRunLoop addTimer:record->timer forMode:NSRunLoopCommonModes];
  }
  const auto lifetime=connection_lifetime;
  dispatch_block_t work=dispatch_block_create(static_cast<dispatch_block_flags_t>(0),^{
    @autoreleasepool {
      chrome_worker_lifetime=lifetime;
      try { worker(); } catch(...) { /* completion remains failclosed */ }
      chrome_worker_lifetime.reset();
    }
  });
  // notify runs after the entire dispatched block exits, not merely after its
  // native provider returns. Its record keeps admission through main delivery.
  dispatch_block_notify(work,dispatch_get_main_queue(),^{
    [record->timer invalidate];record->timer=nil;
    const bool timed_out=record->logical_finished;
    record->logical_finished=true;
    completed(timed_out);
    // Main owns the only mutable record reference. Invalidation is synchronous
    // on this runloop; no timer/cancellation callback remains queued. Consume
    // the completion and destroy its record before a bounded follow-up enters.
    record.reset();
    if(after_completed)after_completed();
  });
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),work);
  return true;
}
struct PermissionCache {
  ChromeRunningTarget target;
  ChromePermissionResult result;
  std::uint64_t authorization_generation=0;
  std::chrono::steady_clock::time_point observed{};
} permission_cache;
// Publication ordering is independent of native return time and UI generation.
// Even an unchanged unavailable observation supersedes an older consent result.
std::uint64_t permission_publication_epoch=0;
std::shared_ptr<std::atomic_bool> consent_native_entry;
void SupersedePermissionPublication() {
  ++permission_publication_epoch;
  if(consent_native_entry)consent_native_entry->store(false);
}
struct PermissionRefresh {
  ChromeRunningTarget target;
  std::uint64_t authorization_generation=0;
  std::shared_ptr<std::atomic_bool> entry_allowed;
};
std::shared_ptr<PermissionRefresh> permission_refresh;

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
// The controller remains a main-thread state machine. Its permission adapter
// reads only this cache; all real permission adapters run inside RunChromeWork.
ChromeConnectionAdapters CachedConnectionAdapters() {
  const auto native=ConnectionAdapters();
  return {native.discover,[](std::int64_t pid,bool) {
    const auto state=connection.snapshot();
    return permission_cache.target==state.target && pid==state.target.pid &&
        permission_cache.authorization_generation==state.authorization_generation
        ?permission_cache.result:ChromePermissionResult{};
  }};
}
void RefreshCachedConnection() {
  connection.Refresh(CachedConnectionAdapters());
  // Ordinary pending checks preserve a fresh grant. Expiry is explicit and is
  // enforced at every state entry, including Connect, Retry and completions.
  if(!connection.stopped() && permission_cache.target==connection.snapshot().target &&
      permission_cache.authorization_generation==connection.snapshot().authorization_generation &&
      permission_cache.result.consent==ChromeConsent::kGranted &&
      std::chrono::steady_clock::now()-permission_cache.observed>std::chrono::seconds(1)) {
    SupersedePermissionPublication();
    permission_cache.result={ChromeConsent::kUnavailable,errAETimeout};
    connection.Refresh(CachedConnectionAdapters());
    permission_cache.authorization_generation=connection.snapshot().authorization_generation;
  }
}
void CachePermission(ChromeRunningTarget target,ChromePermissionResult result,
    std::chrono::steady_clock::time_point observed=std::chrono::steady_clock::now()) {
  if(connection.stopped() || connection.snapshot().target!=target)return;
  if(permission_cache.target==target &&
      permission_cache.authorization_generation==connection.snapshot().authorization_generation &&
      observed<permission_cache.observed)return; // newer observed revocation wins
  SupersedePermissionPublication();
  permission_cache={target,result,connection.snapshot().authorization_generation,observed};
  RefreshCachedConnection();
  permission_cache.authorization_generation=connection.snapshot().authorization_generation;
}
bool PermissionRefreshCurrent(const std::shared_ptr<PermissionRefresh>& request) {
  return !connection.stopped() && request->entry_allowed->load() &&
      connection.snapshot().target==request->target &&
      connection.snapshot().authorization_generation==request->authorization_generation;
}
ChromeProviderState ProbeChromeProvider(ChromeRunningTarget target);
void BeginPermissionRefresh(bool check_provider=false) {
  if(connection.stopped() || permission_refresh || !connection.snapshot().target.valid() ||
      (check_provider && connection.snapshot().consent_pending))return;
  const auto adapters=ConnectionAdapters();
  auto request=std::make_shared<PermissionRefresh>(PermissionRefresh{
      connection.snapshot().target,connection.snapshot().authorization_generation,
      std::make_shared<std::atomic_bool>(true)});
  auto result=std::make_shared<ChromePermissionResult>();
  auto observed=std::make_shared<std::chrono::steady_clock::time_point>();
  auto provider_state=std::make_shared<ChromeProviderState>(ChromeProviderState::kFailed);
  std::function<ChromeProviderState(ChromeRunningTarget)> provider=ProbeChromeProvider;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(test_adapters)provider=test_adapters->provider;
#endif
  const auto lifetime=connection_lifetime;
  const bool admitted=RunChromeWork([request,result,observed,adapters,lifetime,check_provider,provider,provider_state] {
    try {
      if(lifetime->load() && request->entry_allowed->load() && adapters.discover &&
          adapters.discover()==request->target && lifetime->load() &&
          request->entry_allowed->load() && adapters.permission) {
        *result=adapters.permission(request->target.pid,false);
        if(check_provider && result->consent==ChromeConsent::kGranted &&
            lifetime->load() && request->entry_allowed->load() &&
            adapters.discover()==request->target && provider) {
          *provider_state=provider(request->target);
          if(!lifetime->load() || !request->entry_allowed->load() ||
              adapters.discover()!=request->target) {
            request->entry_allowed->store(false);return;
          }
          *result=adapters.permission(request->target.pid,false);
        }
        *observed=std::chrono::steady_clock::now();
        if(!lifetime->load() || !adapters.discover ||
            adapters.discover()!=request->target)request->entry_allowed->store(false);
      } else request->entry_allowed->store(false);
    } catch(...) {
      *result={ChromeConsent::kUnknown,paramErr};*observed=std::chrono::steady_clock::now();
    }
  },[request,result,observed,check_provider,provider_state](bool timed_out) {
    RefreshCachedConnection();
    if(!timed_out && PermissionRefreshCurrent(request)) {
      CachePermission(request->target,*result,*observed);
      if(check_provider) {
        const auto probe=connection.BeginProbe();
        connection.CompleteProbe(probe,*provider_state);
      }
    }
    if(permission_refresh==request)permission_refresh.reset();
  },[request] {
    RefreshCachedConnection();
    if(PermissionRefreshCurrent(request))
      CachePermission(request->target,{ChromeConsent::kUnavailable,errAETimeout});
    request->entry_allowed->store(false);
  });
  if(admitted)permission_refresh=request;
  else CachePermission(request->target,{ChromeConsent::kUnavailable,errAETimeout});
}
ChromeProviderState ProbeChromeProvider(ChromeRunningTarget target) {
  if(NSThread.isMainThread)return ChromeProviderState::kFailed;
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
  if(!chrome_worker_lifetime || !chrome_worker_lifetime->load() ||
      DiscoverRunningChrome()!=target)return ChromeProviderState::kFailed;
  NSAppleScript* script=[[NSAppleScript alloc] initWithSource:
      @"tell application id \"com.google.Chrome\"\n"
       "if (count of windows) is 0 then return \"no-page\"\n"
       "if (count of tabs of front window) is 0 then return \"no-page\"\n"
       "if (id of active tab of front window) is missing value then return \"no-page\"\n"
       "return \"ready\"\nend tell"];
  NSDictionary* error=nil;
  const auto result=[script executeAndReturnError:&error];
  if(!chrome_worker_lifetime->load() || DiscoverRunningChrome()!=target)
    return ChromeProviderState::kFailed;
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
  auto state=std::make_shared<ChromeProviderState>(ChromeProviderState::kFailed);
  std::function<ChromeProviderState(ChromeRunningTarget)> provider=ProbeChromeProvider;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(test_adapters)provider=test_adapters->provider;
#endif
  const auto adapters=ConnectionAdapters();
  const auto lifetime=connection_lifetime;
  const bool admitted=RunChromeWork([probe,state,provider,adapters,lifetime] {
    try {
      if(lifetime->load() && adapters.discover && adapters.discover()==probe.target &&
          lifetime->load() && provider) {
        *state=provider(probe.target);
        if(!lifetime->load() || adapters.discover()!=probe.target)
          *state=ChromeProviderState::kFailed;
      }
    } catch(...) { *state=ChromeProviderState::kFailed; }
  },[probe,state](bool timed_out) {
    RefreshCachedConnection();
    if(!timed_out)connection.CompleteProbe(probe,*state);
  },[probe] {
    RefreshCachedConnection();
    connection.CompleteProbe(probe,ChromeProviderState::kTimedOut);
  });
  if(!admitted)connection.CompleteProbe(probe,ChromeProviderState::kFailed);
}
}
bool ScreenRecordingReady() { return CGPreflightScreenCaptureAccess(); }
ChromeConnectionSnapshot RefreshChromeConnection() {
  RefreshCachedConnection();
  BeginPermissionRefresh();
  return connection.snapshot();
}
ChromeConnectionSnapshot ChromeConnectionStatus() { return connection.snapshot(); }
void StartChromeConnection() {
  connection_lifetime->store(false);
  connection_lifetime=std::make_shared<std::atomic_bool>(true);
  if(permission_refresh)permission_refresh->entry_allowed->store(false);
  permission_cache={};connection.Start();
}
void StopChromeConnection() {
  connection_lifetime->store(false);
  if(permission_refresh)permission_refresh->entry_allowed->store(false);
  permission_cache={};connection.Stop();
}
#if defined(SEETHIS_CAPTURE_TESTING) || defined(SEETHIS_CHROME_CONSENT_TESTING)
unsigned ChromePhysicalWorkCountForTesting() { return chrome_work_gate.admitted(); }
#endif
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
ProcessIdentitySample ReadProcessIdentityForTesting(const void* before,const void* after,
    std::function<KernelProcessIdentityRecord(std::int64_t)> read,core::Stamp observed) {
  return SampleProcessIdentity((__bridge NSRunningApplication*)before,observed,std::move(read),
      [after](std::int64_t){return (__bridge NSRunningApplication*)after;});
}
#endif
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
void SetChromeConnectionTestAdapters(ChromeConnectionTestAdapters adapters) {
  test_adapters=std::move(adapters);
}
#endif
namespace {
void BeginExplicitChromeConsent(std::optional<ChromeRunningTarget> required_target = {}) {
  // Do not schedule a passive refresh before this explicit operation: its own
  // fresh preflight and final reconciliation use the same physical admission.
  RefreshCachedConnection();
  const auto adapters=ConnectionAdapters();
  const auto previous=connection.snapshot();
  const bool cache_current=permission_cache.target==previous.target &&
      permission_cache.authorization_generation==previous.authorization_generation &&
      std::chrono::steady_clock::now()-permission_cache.observed<=std::chrono::seconds(1);
  const auto request=connection.BeginConsent(CachedConnectionAdapters(),required_target);
  if(!request.valid()) {
    if(!required_target || *required_target==connection.snapshot().target)
      BeginConnectionProbe();
    return;
  }
  if(permission_refresh)permission_refresh->entry_allowed->store(false);
  if(cache_current && previous.target==request.target)
    permission_cache.authorization_generation=connection.snapshot().authorization_generation;
  const auto lifetime=connection_lifetime;
  const auto request_authorization=connection.snapshot().authorization_generation;
  const auto request_publication=permission_publication_epoch;
  consent_native_entry=request.native_entry_allowed;
  auto result=std::make_shared<std::optional<ChromePermissionResult>>();
  auto observed=std::make_shared<std::chrono::steady_clock::time_point>();
  auto accepted=std::make_shared<bool>(false);
  auto reconcile=std::make_shared<bool>(false);
  const auto current=[request,adapters,lifetime] {
    return lifetime->load() && request.native_entry_allowed->load() &&
        adapters.discover && adapters.discover()==request.target &&
        lifetime->load() && request.native_entry_allowed->load();
  };
  const bool admitted=RunChromeWork([request,adapters,result,observed,current] {
    try {
      if(!current() || !adapters.permission)return;
      const auto preflight=adapters.permission(request.target.pid,false);
      // The preflight itself can block across quit/relaunch or Stop/Start.
      if(!current())return;
      if(preflight.consent!=ChromeConsent::kGranted) {
        (void)adapters.permission(request.target.pid,true);
        if(!current())return;
      }
      *result=adapters.permission(request.target.pid,false);
      *observed=std::chrono::steady_clock::now();
      if(!current())result->reset();
    } catch(...) {
      *result=ChromePermissionResult{ChromeConsent::kUnknown,paramErr};
      *observed=std::chrono::steady_clock::now();
    }
  },[request,result,observed,accepted,reconcile,request_authorization,request_publication](bool) {
    RefreshCachedConnection();
    // CompleteConsent owns target/lifecycle eligibility; do not let a stale
    // result enter the cache before that gate has accepted it.
    const bool current_request=connection.CompleteConsent(request,connection.snapshot().permission);
    if(consent_native_entry==request.native_entry_allowed)consent_native_entry.reset();
    if(!current_request)return;
    *accepted=true;
    // The native call may have captured its value long before returning. Only
    // the post-BeginConsent authorization/publication epochs can authorize it;
    // a newer observation (including same-state busy/timeout) always wins.
    if(*result && request_authorization==connection.snapshot().authorization_generation &&
        request_publication==permission_publication_epoch)
      CachePermission(request.target,**result,*observed);
    else *reconcile=true;
  },{},[accepted,reconcile,lifetime,target=request.target] {
    if(!*accepted || !lifetime->load() || connection.snapshot().target!=target)return;
    // A new prompt-free observation can recover a legitimate current grant;
    // the superseded gesture can never re-enter ask=true.
    if(*reconcile)BeginPermissionRefresh(true);
    else BeginConnectionProbe();
  });
  if(!admitted) {
    connection.CompleteConsent(request,std::nullopt);
    if(consent_native_entry==request.native_entry_allowed)consent_native_entry.reset();
    CachePermission(request.target,{ChromeConsent::kUnavailable,errAETimeout});
  }
}
}  // namespace
namespace {
const void* consent_interruption_owner=nullptr;
std::function<void()> consent_interruption;
}
void SetChromeConsentInterruptionHandler(const void* owner,std::function<void()> interrupt) {
  consent_interruption_owner=owner;consent_interruption=std::move(interrupt);
}
void ClearChromeConsentInterruptionHandler(const void* owner) {
  if(consent_interruption_owner==owner){consent_interruption={};consent_interruption_owner=nullptr;}
}
void ConnectChromeExplicit() {
  if(consent_interruption)consent_interruption();
  BeginExplicitChromeConsent();
}
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
  if(foreground_target.valid()){if(consent_interruption)consent_interruption();BeginExplicitChromeConsent(foreground_target);}
}
void RetryChromeExplicit() {
  RefreshCachedConnection();
  // Retry is one admitted preflight+probe unit, never permission to prompt.
  BeginPermissionRefresh(true);
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
  if(d==context.displays.end() || !initial.IsValid() || initial.unit!=core::CoordinateUnit::kLogicalPoints || initial.backing_scale!=d->scale ||
     initial.position.x<0 || initial.position.y<0 || initial.position.x>d->logical.width || initial.position.y>d->logical.height) {
    error="Initial display geometry unavailable";return {};
  }
  const double primaryTop=PrimaryTop();
  context.quartz_to_appkit_top=primaryTop;
  const auto sample=SampleProcessIdentity(app,context.observed);
  const bool kernel=sample.launch_start_identity_us==0;
  const auto value=kernel?sample.kernel_start_identity_us:sample.launch_start_identity_us;
  const auto start=value?std::optional<std::int64_t>(value):std::nullopt;
  const auto scope_version=kernel?2u:1u;
  if(!start || context.bundle_id.empty()) {error="Foreground application instance unavailable";return {};}
  const auto application=[&]() -> std::optional<core::Context> {
    if(!StillForeground(app)){error="Foreground application changed during context capture";return {};}
    context.window_id=0;context.window_pid=0;context.window={};context.window_title.clear();
    context.native_window={};context.title={1,core::TitleAvailability::kUnavailable};
    context.selection_method="frontmost-application/selected-display";
    context.scope={scope_version,core::CaptureScopeKind::kApplication,initial.display_id,*start,true};
    if(kernel)context.scope.process_source=core::ProcessStartSource::kKernelBSDStart;
    return context;
  };
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
    const auto identities=NativeIdentities(app,context.window_id,windows,sample);
    context.native_window=kernel?identities.kernel:identities.launch;
    if(!StillForeground(app)){error="Foreground application changed during context capture";return {};}
    if(context.native_window.observation_session.empty() || !context.native_window.incarnation)return application();
    context.scope={scope_version,core::CaptureScopeKind::kWindow,initial.display_id,*start,true};
    if(kernel)context.scope.process_source=core::ProcessStartSource::kKernelBSDStart;
    context.scope.title_required=context.bundle_id!="com.google.Chrome"&&core::CapturedTitleAvailable(context);
    return context;
  }
  return application();
}

#if defined(SEETHIS_WINDOW_OBSERVATION_API)
core::WindowObservation ObserveOwnUIPresentationSource(
    const core::Context& source,std::uint64_t generation) {
  core::WindowObservation unavailable;unavailable.generation=generation;unavailable.observed=core::Now();
  const auto front=NSWorkspace.sharedWorkspace.frontmostApplication;
  if(!front||front.processIdentifier!=NSProcessInfo.processInfo.processIdentifier||
     !ScreenRecordingReady()||(source.scope.kind!=core::CaptureScopeKind::kApplication&&source.native_window.version!=1&&source.native_window.version!=2)||
     !core::DisplayTopologyMatches(source.displays,SnapshotDisplays()))return unavailable;
  const auto app=[NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(source.pid)];
  if(!app||app.terminated||String(app.bundleIdentifier)!=source.bundle_id)return unavailable;
  const auto sample=SampleProcessIdentity(app,unavailable.observed);
  const bool kernel=core::CaptureProcessSource(source)==core::ProcessStartSource::kKernelBSDStart;
  const auto required=kernel?sample.kernel_start_identity_us:sample.launch_start_identity_us;
  const auto captured_start=source.scope.policy_present?source.scope.process_start_identity_us:source.native_window.process_start_identity_us;
  if(!required||required!=captured_start)return unavailable;
  if(source.scope.kind==core::CaptureScopeKind::kApplication) {
    if(!StillForeground(front))return unavailable;
    unavailable.pid=source.pid;unavailable.bundle_id=source.bundle_id;unavailable.application_available=true;
    unavailable.application_start_identity_us=sample.launch_start_identity_us;
    unavailable.kernel_application_start_identity_us=sample.kernel_start_identity_us;
    return unavailable;
  }
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
  const auto identities=NativeIdentities(app,source.window_id,windows,sample);
  observation.native_window=identities.launch;observation.kernel_native_window=identities.kernel;
  observation.application_available=sample.coherent&&(sample.launch_start_identity_us>0||sample.kernel_start_identity_us>0);
  observation.application_start_identity_us=sample.launch_start_identity_us;
  observation.kernel_application_start_identity_us=sample.kernel_start_identity_us;
  if(source.bundle_id!="com.google.Chrome") {
    const auto title=ObserveBoundTitle(matched,source.pid,source.window_id);
    observation.window_title=title.first;observation.title=title.second;
  }
  observation.observed=core::Now();
  const auto& native=core::WindowIdentityForContext(source,observation);
  if(!StillForeground(front)||!core::WindowObservationMatchesContext(source,observation)||
     native.observation_session!=source.native_window.observation_session||
     native.incarnation!=source.native_window.incarnation||
     (source.bundle_id!="com.google.Chrome"&&(!source.scope.policy_present||source.scope.title_required)&&!core::TitleContextVisible(source,observation)))return unavailable;
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
  const auto sample=SampleProcessIdentity(app,observation.observed);
  if(!sample.coherent||(!sample.launch_start_identity_us&&!sample.kernel_start_identity_us)||!StillForeground(app))return observation;
  observation.pid=app.processIdentifier;observation.bundle_id=String(app.bundleIdentifier);
  observation.application_available=!observation.bundle_id.empty();
  observation.application_start_identity_us=sample.launch_start_identity_us;
  observation.kernel_application_start_identity_us=sample.kernel_start_identity_us;
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
    const auto identities=NativeIdentities(app,observation.window_id,windows,sample);
    observation.native_window=identities.launch;observation.kernel_native_window=identities.kernel;
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
  (void)NativeIdentities(app,0,windows,sample);
  if(!StillForeground(app)) {core::WindowObservation unavailable;unavailable.generation=generation;unavailable.observed=core::Now();return unavailable;}
  return observation;
}
#endif

namespace {
// Titles are compared only inside one native operation. They are never copied
// into a Context, PageIdentity, captured metadata, diagnostic, or cache.
std::optional<std::string> TransientTitle(id value,std::size_t maximum=4096) {
  if(![value isKindOfClass:NSString.class])return {};
  NSData* bytes=[value dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
  if(!bytes || bytes.length>maximum)return {};
  if(bytes.length==0)return std::string{};
  std::string title(static_cast<const char*>(bytes.bytes),bytes.length);
  if(title.find('\0')!=std::string::npos)return {};
  return title; // an observed empty title is a value, not a blacklist
}
bool SameRect(core::Rect a,core::Rect b) {
  return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;
}
std::optional<ChromeWindowCandidate> CorrelateChromeContent(
    NSArray* windows,std::int64_t pid,std::string_view title,core::Rect bounds,double top) {
  if(!windows || !std::isfinite(top) || !std::isfinite(bounds.x)||!std::isfinite(bounds.y)||
      !std::isfinite(bounds.width)||!std::isfinite(bounds.height)||bounds.width<=0||bounds.height<=0)return {};
  std::optional<ChromeWindowCandidate> match;
  for(NSUInteger index=0;index<windows.count;++index) {
    NSDictionary* info=windows[index];
    if(![info isKindOfClass:NSDictionary.class] ||
       [info[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=pid ||
       [info[(__bridge NSString*)kCGWindowLayer] intValue]!=0 ||
       [info[(__bridge NSString*)kCGWindowAlpha] doubleValue]<=0)continue;
    CGRect frame;
    if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds],&frame))continue;
    const core::Rect native{frame.origin.x,top-CGRectGetMaxY(frame),frame.size.width,frame.size.height};
    if(!SameRect(bounds,native))continue;
    const auto observed=TransientTitle(info[(__bridge NSString*)kCGWindowName]);
    // Missing comparison evidence at the same geometry is not permission to
    // choose a different same-bounds window with a readable title.
    if(!observed)return {};
    if(*observed!=title)continue;
    const auto identifier=[info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue];
    if(!identifier||match)return {};
    match=ChromeWindowCandidate{pid,identifier,native,static_cast<std::int64_t>(index),true};
  }
  return match;
}
bool ConfirmContentWindow(core::WindowObservation& window) {
  if(!NSThread.isMainThread)return false;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(test_adapters&&test_adapters->confirm_content)return test_adapters->confirm_content(window);
#endif
  NSRunningApplication* front=NSWorkspace.sharedWorkspace.frontmostApplication;
  if(!front||front.processIdentifier!=window.pid||
     ![front.bundleIdentifier isEqualToString:@"com.google.Chrome"]||!front.launchDate||
     ChromeProcessStartIdentity(front.launchDate.timeIntervalSince1970)!=window.native_window.process_start_identity_us||
     !ScreenRecordingReady())return false;
  NSArray* windows=CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,kCGNullWindowID));
  bool found=false;
  for(NSDictionary* info in windows) {
    if([info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue]!=window.window_id)continue;
    if(found||[info[(__bridge NSString*)kCGWindowOwnerPID] longLongValue]!=window.pid||
       [info[(__bridge NSString*)kCGWindowLayer] intValue]!=0||
       [info[(__bridge NSString*)kCGWindowAlpha] doubleValue]<=0)return false;
    CGRect frame;
    if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds],&frame)||
       !SameRect(window.bounds,{frame.origin.x,PrimaryTop()-CGRectGetMaxY(frame),frame.size.width,frame.size.height}))return false;
    found=true;
  }
  if(!found||!StillForeground(front))return false;
  window.browser_content=true;
  window.application_available=true;window.application_start_identity_us=window.native_window.process_start_identity_us;
  ProcessIdentitySample identities;identities.coherent=true;
  identities.launch_start_identity_us=window.native_window.process_start_identity_us;
  identities.kernel_start_identity_us=window.kernel_application_start_identity_us;
  const auto native=NativeIdentities(front,window.window_id,windows,identities);
  window.native_window=native.launch;window.kernel_native_window=native.kernel;
  return !window.native_window.observation_session.empty()&&window.native_window.incarnation!=0;
}
}
#if defined(SEETHIS_CAPTURE_TESTING) || defined(SEETHIS_CHROME_CONSENT_TESTING)
std::optional<ChromeWindowCandidate> CorrelateChromeContentForTesting(
    CFArrayRef windows,std::int64_t pid,std::string_view title,core::Rect bounds,double top) {
  return CorrelateChromeContent((__bridge NSArray*)windows,pid,title,bounds,top);
}
#endif
core::WindowObservation ObserveChromeContentSource(const core::Context& source,std::uint64_t generation) {
  core::WindowObservation result;result.generation=generation;result.observed=core::Now();
  result.availability=core::WindowAvailability::kReady;result.pid=source.window_pid;
  result.bundle_id=source.bundle_id;result.window_id=source.window_id;result.bounds=source.window;
  result.native_window=source.native_window;
  if(source.bundle_id=="com.google.Chrome"&&ConfirmContentWindow(result)&&
      core::WindowObservationMatchesContext(source,result))return result;
  core::WindowObservation unavailable;unavailable.generation=generation;unavailable.observed=core::Now();return unavailable;
}
static void AcquireChromeObservationAsync(ChromePageAcquire acquire,
                            std::uint64_t generation,
                            ChromeContentCompletion completion,bool require_content) {
  // Unsupported off-main callers are rejected without executing native work or
  // moving their callback to a different thread/creating a main-queue backlog.
  if(!NSThread.isMainThread)return;
  auto observation=std::make_shared<core::PageObservation>();
  auto window=std::make_shared<std::optional<core::WindowObservation>>();
  observation->generation=generation;
  const auto lifetime=connection_lifetime;
  const auto authorization=connection.snapshot().authorization_generation;
  const auto unavailable=[generation,completion](core::PageAvailability state) {
    core::PageObservation result;result.availability=state;
    result.generation=generation;result.observed=core::Now();completion({{},std::move(result)});
  };
  const bool admitted=RunChromeWork([acquire,observation,window,lifetime] {
    ChromePageSample sample;
    try { if(lifetime->load())sample=acquire?acquire():ChromePageSample{}; }
    catch(...) { sample.availability=core::PageAvailability::kUnavailable; }
    observation->observed=core::Now();observation->availability=sample.availability;
    *window=std::move(sample.content_window);
    if(*window)(*window)->observed=observation->observed;
    if(sample.availability==core::PageAvailability::kReady) {
      observation->identity=MakeChromePageIdentity(sample);
      if(observation->identity)
        observation->captured_page=MakeChromeCapturedPageMetadata(sample,observation->observed);
      else observation->availability=core::PageAvailability::kUnavailable;
    }
  },[observation,window,lifetime,authorization,completion,unavailable,require_content](bool timed_out) {
    if(timed_out)return;
    if(connection.snapshot().permission.consent==ChromeConsent::kGranted &&
        std::chrono::steady_clock::now()-permission_cache.observed>std::chrono::seconds(1))
      CachePermission(connection.snapshot().target,{ChromeConsent::kUnavailable,errAETimeout});
    if(!lifetime->load() || authorization!=connection.snapshot().authorization_generation) {
      unavailable(core::PageAvailability::kUnavailable);return;
    }
    if(require_content && observation->availability==core::PageAvailability::kReady &&
       (!*window||!ConfirmContentWindow(**window))) {
      unavailable(core::PageAvailability::kUnavailable);return;
    }
    completion({*window?**window:core::WindowObservation{},std::move(*observation)});
  },[unavailable] { unavailable(core::PageAvailability::kTimedOut); });
  if(!admitted)unavailable(core::PageAvailability::kUnavailable);
}
void AcquireChromePageAsync(ChromePageAcquire acquire,std::uint64_t generation,
                            ChromePageCompletion completion) {
  AcquireChromeObservationAsync(std::move(acquire),generation,
      [completion=std::move(completion)](ChromeContentObservation result) {
        completion(std::move(result.page));
      },false);
}
void AcquireChromeContentAsync(ChromePageAcquire acquire,std::uint64_t generation,
                               ChromeContentCompletion completion) {
  AcquireChromeObservationAsync(std::move(acquire),generation,std::move(completion),true);
}

bool ChromeAutomationPreflight(std::int64_t browser_pid) {
  if(!NSThread.isMainThread)return false;
  const auto state=RefreshChromeConnection();
  return state.target.pid==browser_pid && state.target.valid() &&
      state.permission.consent==ChromeConsent::kGranted;
}

namespace {
struct ChromeFrontTuple {
  std::string browser_window,title,tab,url;
  core::Rect bounds;
  bool operator==(const ChromeFrontTuple& other) const {
    return browser_window==other.browser_window&&title==other.title&&tab==other.tab&&
        url==other.url&&SameRect(bounds,other.bounds);
  }
};
std::optional<ChromeFrontTuple> ParseChromeFront(NSAppleEventDescriptor* values,double primary_top) {
  if(!std::isfinite(primary_top)||values.descriptorType!=typeAEList||values.numberOfItems!=5) {
    return {};
  }
  const auto text=[](NSAppleEventDescriptor* value) {
    return value.descriptorType==typeUnicodeText||value.descriptorType==typeUTF8Text||value.descriptorType==typeChar;
  };
  if(!text([values descriptorAtIndex:2]))return {};
  if(!text([values descriptorAtIndex:5]))return {};
  if([values descriptorAtIndex:3].descriptorType!=typeQDRectangle)return {};
  const auto id=TransientTitle([values descriptorAtIndex:1].stringValue);
  const auto title=TransientTitle([values descriptorAtIndex:2].stringValue);
  auto* rectangle=[[values descriptorAtIndex:3] coerceToDescriptorType:typeQDRectangle];
  const auto tab_id=TransientTitle([values descriptorAtIndex:4].stringValue);
  const auto url=TransientTitle([values descriptorAtIndex:5].stringValue,std::numeric_limits<std::size_t>::max());
  if(!id||id->empty())return {};
  if(!title)return {};
  if(!tab_id||tab_id->empty())return {};
  if(!url||url->empty())return {};
  const auto rectangle_length=rectangle.data.length;
  if(rectangle_length!=sizeof(::Rect)) {
    return {};
  }
  ::Rect qd{};std::memcpy(&qd,rectangle.data.bytes,sizeof(qd));
  core::Rect bounds{static_cast<double>(qd.left),primary_top-qd.bottom,
      static_cast<double>(qd.right)-qd.left,static_cast<double>(qd.bottom)-qd.top};
  if(bounds.width<=0||bounds.height<=0)return {};
  return ChromeFrontTuple{*id,*title,*tab_id,*url,bounds};
}
NSAppleEventDescriptor* Property(OSType code,NSAppleEventDescriptor* container) {
  auto* result=NSAppleEventDescriptor.recordDescriptor;
  [result setDescriptor:[NSAppleEventDescriptor descriptorWithTypeCode:typeProperty] forKeyword:keyAEDesiredClass];
  [result setDescriptor:[NSAppleEventDescriptor descriptorWithEnumCode:formPropertyID] forKeyword:keyAEKeyForm];
  [result setDescriptor:[NSAppleEventDescriptor descriptorWithTypeCode:code] forKeyword:keyAEKeyData];
  [result setDescriptor:container forKeyword:keyAEContainer];
  return [result coerceToDescriptorType:typeObjectSpecifier];
}
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
using ChromeFrontSend = std::function<NSAppleEventDescriptor*(NSAppleEventDescriptor*,NSAppleEventSendOptions,NSTimeInterval,NSError**)>;
thread_local const ChromeFrontSend* chrome_front_send=nullptr;
#endif
NSAppleEventDescriptor* ReadChromeProperty(std::int64_t pid,
    NSAppleEventDescriptor* property,std::chrono::steady_clock::time_point deadline) {
  if(NSThread.isMainThread||!chrome_worker_lifetime||!chrome_worker_lifetime->load()) {
    return nil;
  }
  const double remaining=std::chrono::duration<double>(deadline-std::chrono::steady_clock::now()).count();
  if(remaining<=0){return nil;}
  auto* event=[NSAppleEventDescriptor appleEventWithEventClass:kAECoreSuite eventID:kAEGetData
      targetDescriptor:[NSAppleEventDescriptor descriptorWithProcessIdentifier:static_cast<pid_t>(pid)]
      returnID:kAutoGenerateReturnID transactionID:kAnyTransactionID];
  [event setParamDescriptor:property forKeyword:keyDirectObject];
  NSError* error=nil;
  constexpr auto options=NSAppleEventSendWaitForReply|NSAppleEventSendNeverInteract|NSAppleEventSendDontRecord;
  NSAppleEventDescriptor* reply;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(chrome_front_send)reply=(*chrome_front_send)(event,options,remaining,&error);
  else
#endif
  reply=[event sendEventWithOptions:options timeout:remaining error:&error];
  if(!reply||error) {
    return nil;
  }
  const auto ae_error_code=[reply paramDescriptorForKeyword:keyErrorNumber].int32Value;
  if(ae_error_code!=0) {
    return nil;
  }
  if(std::chrono::steady_clock::now()>=deadline||!chrome_worker_lifetime->load()) {
    return nil;
  }
  return [reply paramDescriptorForKeyword:keyDirectObject];
}
bool ChromeTextDescriptor(NSAppleEventDescriptor* value) {
  return value.descriptorType==typeUnicodeText||value.descriptorType==typeUTF8Text||value.descriptorType==typeChar;
}
std::optional<std::string> ChromeOpaqueID(NSAppleEventDescriptor* value) {
  if(!ChromeTextDescriptor(value))return {};
  auto id=TransientTitle(value.stringValue);
  return id&&!id->empty()?id:std::nullopt;
}
bool ChromeSpecifierCode(NSAppleEventDescriptor* value,AEKeyword key,OSType type,OSType code) {
  auto* field=[value descriptorForKeyword:key];
  return field.descriptorType==type&&
      (type==typeEnumerated?field.enumCodeValue:field.typeCodeValue)==code;
}
struct ChromeTabAnchor {
  std::string window,tab;
  bool operator==(const ChromeTabAnchor&) const = default;
};
std::optional<ChromeTabAnchor> ParseChromeTabAnchor(NSAppleEventDescriptor* value) {
  // Accept only Chrome's finite unique-ID tab -> unique-ID browser -> root
  // chain. Neither ID is a CGWindowID. Never reuse a relative front-tab query.
  if(value.descriptorType!=typeObjectSpecifier||
     !ChromeSpecifierCode(value,keyAEDesiredClass,typeType,'CrTb')||
     !ChromeSpecifierCode(value,keyAEKeyForm,typeEnumerated,formUniqueID))return {};
  const auto tab=ChromeOpaqueID([value descriptorForKeyword:keyAEKeyData]);
  auto* window=[value descriptorForKeyword:keyAEContainer];
  if(window.descriptorType!=typeObjectSpecifier||
     !ChromeSpecifierCode(window,keyAEDesiredClass,typeType,'cwin')||
     !ChromeSpecifierCode(window,keyAEKeyForm,typeEnumerated,formUniqueID))return {};
  const auto browser=ChromeOpaqueID([window descriptorForKeyword:keyAEKeyData]);
  if(!tab||!browser||[window descriptorForKeyword:keyAEContainer].descriptorType!=typeNull)return {};
  return ChromeTabAnchor{*browser,*tab};
}
NSAppleEventDescriptor* ChromeUniqueElement(OSType type,std::string_view id,NSAppleEventDescriptor* container) {
  auto* result=NSAppleEventDescriptor.recordDescriptor;
  [result setDescriptor:[NSAppleEventDescriptor descriptorWithTypeCode:type] forKeyword:keyAEDesiredClass];
  [result setDescriptor:[NSAppleEventDescriptor descriptorWithEnumCode:formUniqueID] forKeyword:keyAEKeyForm];
  auto* text=[[NSString alloc] initWithBytes:id.data() length:id.size() encoding:NSUTF8StringEncoding];
  [result setDescriptor:[NSAppleEventDescriptor descriptorWithString:text] forKeyword:keyAEKeyData];
  [result setDescriptor:container forKeyword:keyAEContainer];
  return [result coerceToDescriptorType:typeObjectSpecifier];
}
struct ChromeWindowProperties {
  std::string id,title;
  __strong NSAppleEventDescriptor* bounds;
  ChromeTabAnchor anchor;
  bool operator==(const ChromeWindowProperties& other) const {
    return id==other.id&&title==other.title&&anchor==other.anchor&&[bounds.data isEqualToData:other.bounds.data];
  }
};
std::optional<ChromeWindowProperties> ParseChromeWindowProperties(NSAppleEventDescriptor* record) {
  if(!record||!AECheckIsRecord(record.aeDesc))return {};
  auto* id_value=[record descriptorForKeyword:'ID  '];
  auto* title_value=[record descriptorForKeyword:'pnam'];
  auto* bounds=[record descriptorForKeyword:'pbnd'];
  const auto id=ChromeOpaqueID(id_value);
  if(!id||!ChromeTextDescriptor(title_value)||bounds.descriptorType!=typeQDRectangle||bounds.data.length!=sizeof(::Rect))return {};
  const auto title=TransientTitle(title_value.stringValue);
  const auto anchor=ParseChromeTabAnchor([record descriptorForKeyword:'acTa']);
  if(!title||!anchor||anchor->window!=*id)return {};
  // pALL also transports unused metadata (including mode/given name/status).
  // Do not decode, log, cache or persist any unused record property.
  return ChromeWindowProperties{*id,*title,bounds,*anchor};
}
std::optional<ChromeFrontTuple> ReadChromeFront(std::int64_t pid,double primary_top,
    std::chrono::steady_clock::time_point deadline) {
  auto* window=NSAppleEventDescriptor.recordDescriptor;
  [window setDescriptor:[NSAppleEventDescriptor descriptorWithTypeCode:'cwin'] forKeyword:keyAEDesiredClass];
  [window setDescriptor:[NSAppleEventDescriptor descriptorWithEnumCode:formAbsolutePosition] forKeyword:keyAEKeyForm];
  [window setDescriptor:[NSAppleEventDescriptor descriptorWithInt32:1] forKeyword:keyAEKeyData];
  [window setDescriptor:NSAppleEventDescriptor.nullDescriptor forKeyword:keyAEContainer];
  window=[window coerceToDescriptorType:typeObjectSpecifier];
  // Get's receiver is one object specifier, not an AEList of references.
  // Three native reads per tuple, all sharing the caller's absolute deadline.
  auto* before_record=ReadChromeProperty(pid,Property('pALL',window),deadline);
  if(!before_record)return {};
  const auto before=ParseChromeWindowProperties(before_record);
  if(!before)return {};
  auto* browser=ChromeUniqueElement('cwin',before->anchor.window,NSAppleEventDescriptor.nullDescriptor);
  auto* tab=ChromeUniqueElement('CrTb',before->anchor.tab,browser);
  auto* tab_record=ReadChromeProperty(pid,Property('pALL',tab),deadline);
  if(!tab_record)return {};
  if(!AECheckIsRecord(tab_record.aeDesc))return {};
  const auto tab_id=ChromeOpaqueID([tab_record descriptorForKeyword:'ID  ']);
  auto* url=[tab_record descriptorForKeyword:'URL '];
  if(!tab_id||*tab_id!=before->anchor.tab||!ChromeTextDescriptor(url))return {};
  const auto after=ParseChromeWindowProperties(ReadChromeProperty(pid,Property('pALL',window),deadline));
  if(!after||!(*before==*after))return {};
  auto* values=NSAppleEventDescriptor.listDescriptor;
  for(auto* value:{[NSAppleEventDescriptor descriptorWithString:[NSString stringWithUTF8String:before->id.c_str()]],
                  [NSAppleEventDescriptor descriptorWithString:[NSString stringWithUTF8String:before->title.c_str()]],
                  before->bounds,[NSAppleEventDescriptor descriptorWithString:[NSString stringWithUTF8String:tab_id->c_str()]],url})
    [values insertDescriptor:value atIndex:0];
  return ParseChromeFront(values,primary_top);
}
}
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
bool ChromeFrontPropertiesReadForTesting(const ChromeFrontSend& send,const void* expected,int budget_ms,bool twice) {
  if(NSThread.isMainThread)return false;
  struct Reset {
    std::shared_ptr<std::atomic_bool> lifetime=chrome_worker_lifetime;
    const ChromeFrontSend* sender=chrome_front_send;
    ~Reset(){chrome_worker_lifetime=lifetime;chrome_front_send=sender;}
  } reset;
  chrome_worker_lifetime=std::make_shared<std::atomic_bool>(true);chrome_front_send=&send;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(budget_ms);
  const auto a=ReadChromeFront(123,900,deadline);
  const auto want=ParseChromeFront((__bridge NSAppleEventDescriptor*)expected,900);
  if(!a||!want||!(*a==*want))return false;
  if(!twice)return true;
  const auto b=ReadChromeFront(123,900,deadline);
  return b&&*a==*b;
}
bool ChromeFrontDescriptorsMatchForTesting(const void* first,const void* last,double top) {
  const auto a=ParseChromeFront((__bridge NSAppleEventDescriptor*)first,top);
  const auto b=ParseChromeFront((__bridge NSAppleEventDescriptor*)last,top);
  return a&&b&&*a==*b;
}
#endif
static ChromePageSample AcquireChromePage(const core::Context& captured,bool own_ui,bool resolve_front=false) {
  ChromePageSample unavailable;unavailable.browser_pid=captured.pid;
  unavailable.window_id=captured.window_id;unavailable.window_bounds=captured.window;
  unavailable.availability=core::PageAvailability::kUnavailable;
  const auto lifetime=chrome_worker_lifetime;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
  if(NSThread.isMainThread || !lifetime || !lifetime->load())return unavailable;
  const auto running_before=[NSRunningApplication runningApplicationWithProcessIdentifier:
      static_cast<pid_t>(captured.pid)];
  const auto initial_start=running_before.launchDate?ChromeProcessStartIdentity(
      running_before.launchDate.timeIntervalSince1970):std::optional<std::int64_t>{};
  if(!initial_start || captured.native_window.process_start_identity_us<=0 ||
      captured.native_window.process_start_identity_us!=*initial_start)return unavailable;
  if(captured.bundle_id!="com.google.Chrome") {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=target-validation cause=non-chrome pid=%{public}lld window=%{public}llu",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    return unavailable;
  }
  const auto permission=CheckChromeAutomation(captured.pid,false);
  if(!lifetime->load())return unavailable;
  const auto after_permission=[NSRunningApplication runningApplicationWithProcessIdentifier:
      static_cast<pid_t>(captured.pid)];
  if(!after_permission || after_permission.terminated || !after_permission.launchDate ||
      ChromeProcessStartIdentity(after_permission.launchDate.timeIntervalSince1970)!=initial_start)
    return unavailable;
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
  // Browser session IDs are not CGWindowIDs. Compare transient provider title
  // and geometry with native evidence; never blacklist auxiliary dimensions.
  const auto first=ReadChromeFront(captured.pid,captured.quartz_to_appkit_top,deadline);
  if(!first)return unavailable;

  NSArray* windows=CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,
      kCGNullWindowID));
  const auto correlated=CorrelateChromeContent(windows,captured.pid,first->title,
      first->bounds,captured.quartz_to_appkit_top);
  if(!correlated) {
    unavailable.availability=core::PageAvailability::kAmbiguous;
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=window-correlation cause=%{public}s pid=%{public}lld window=%{public}llu",
        "no-unique-content-match",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    return unavailable;
  }
  if(!resolve_front&&!ChromeWindowMatches(captured,*correlated)) {
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis chrome phase=window-correlation cause=capture-not-content-window pid=%{public}lld window=%{public}llu",
        static_cast<long long>(captured.pid),
        static_cast<unsigned long long>(captured.window_id));
    unavailable.availability=core::PageAvailability::kUnavailable;return unavailable;
  }
  const auto last=ReadChromeFront(captured.pid,captured.quartz_to_appkit_top,deadline);
  if(!last||!(*first==*last))return unavailable;
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
  if(!lifetime->load() || *start_identity!=*initial_start || !StillForeground(foreground) ||
      !CheckChromeAutomation(captured.pid,false).allowed || !lifetime->load() ||
      (own_ui&&*start_identity!=captured.native_window.process_start_identity_us))return unavailable;
  const auto final_running=[NSRunningApplication runningApplicationWithProcessIdentifier:
      static_cast<pid_t>(captured.pid)];
  if(!final_running || final_running.terminated || !final_running.launchDate ||
      ChromeProcessStartIdentity(final_running.launchDate.timeIntervalSince1970)!=initial_start)
    return unavailable;
  ChromePageSample ready;ready.availability=core::PageAvailability::kReady;
  ready.browser_pid=captured.pid;ready.window_id=correlated->window_id;
  ready.window_bounds=correlated->bounds;
  ready.process_start_identity_us=*start_identity;
  ready.opaque_tab_id=last->tab;
  ready.url=last->url;
  if(resolve_front) {
    core::WindowObservation window;window.availability=core::WindowAvailability::kReady;
    window.pid=captured.pid;window.bundle_id="com.google.Chrome";
    window.window_id=correlated->window_id;window.bounds=correlated->bounds;
    window.native_window={1,*start_identity,{},0};
    ready.content_window=std::move(window);
  }
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
  context.window=foreground.bounds;context.native_window=foreground.native_window;
  context.quartz_to_appkit_top=PrimaryTop();
  if(foreground.availability!=core::WindowAvailability::kReady)
    return ChromePageSample{};
  return AcquireCurrentChromePage(context);
}
ChromePageSample AcquireCurrentChromeContent(const core::WindowObservation& foreground) {
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  if(test_adapters&&test_adapters->acquire_content)return test_adapters->acquire_content(foreground);
#endif
  core::Context context;context.observed=foreground.observed;
  context.pid=foreground.pid;context.window_pid=foreground.pid;
  context.bundle_id=foreground.bundle_id;context.window_id=foreground.window_id;
  context.window=foreground.bounds;context.native_window=foreground.native_window;
  context.quartz_to_appkit_top=PrimaryTop();
  if(foreground.availability!=core::WindowAvailability::kReady)return {};
  auto result=AcquireChromePage(context,false,true);
  if(result.content_window)result.content_window->kernel_application_start_identity_us=foreground.kernel_application_start_identity_us;
  return result;
}
#endif
}  // namespace seethis::platform
