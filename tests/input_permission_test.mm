#include "platform/platform.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

namespace {

int checks = 0;

void Check(bool condition, std::string_view message) {
  ++checks;
  if (!condition) throw std::runtime_error(std::string(message));
}

using seethis::platform::ClassifyInputMonitor;
using seethis::platform::InputMonitorProbe;
using seethis::platform::InputMonitorReady;
using seethis::platform::InputMonitorState;

struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    char name[] = "/tmp/seethis-input-test-XXXXXX";
    const char* created = mkdtemp(name);
    if (created == nullptr) throw std::runtime_error("mkdtemp failed");
    path = created;
  }
  ~TempDirectory() { std::filesystem::remove_all(path); }
};

std::string Read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string_view Between(std::string_view source, std::string_view begin,
                         std::string_view end) {
  const auto first = source.find(begin);
  if (first == std::string_view::npos) return {};
  const auto last = source.find(end, first + begin.size());
  if (last == std::string_view::npos) return {};
  return source.substr(first, last - first);
}

template <typename Status>
bool HasReadyChromeLabel() {
  if constexpr (requires { Status::kReady; }) {
    return seethis::platform::ChromeCompactStatusLabel(Status::kReady) ==
           "Chrome — Ready";
  }
  return false;
}

void CheckNativeCompactMenu() {
  // This target links seethis_core only. Read the production AppKit source to
  // catch menu wiring regressions that pure permission helpers cannot see.
  const auto source_path = std::filesystem::path(__FILE__).parent_path()
                               .parent_path() / "src/platform/mac/input_mac.mm";
  const std::string source = Read(source_path);
  Check(!source.empty(), "native menu source is available to this test");
  const auto menu = Between(source, "NSMenu* menu=[NSMenu new];",
                            "_statusItem.menu=menu;");
  Check(!menu.empty(), "native outward menu construction is present");
  Check(menu.find("menu.delegate=self") != std::string_view::npos &&
            source.find("- (void)menuWillOpen:(NSMenu*)menu {") !=
                std::string_view::npos,
        "CRD-06: opening the menu refreshes compact state");
  const auto menu_open = Between(source, "- (void)menuWillOpen:(NSMenu*)menu {",
                                  "- (void)");
  Check(menu_open.find("_chromeLastCheckMs=0;") <
            menu_open.find("[self refreshPermissionMenu];") &&
            menu_open.find("[self refreshPermissionMenu];") != std::string_view::npos,
        "CRD-06: opening the menu forces a current compact snapshot without waiting for polling");
  const auto ready_item = Between(menu, "_startupStateItem=[[NSMenuItem alloc]",
                                  "[menu addItem:_startupStateItem];");
  Check(ready_item.find("action:@selector(showPermissionEntry:)") !=
                std::string_view::npos &&
            ready_item.find("_startupStateItem.target=self") !=
                std::string_view::npos &&
            ready_item.find("action:nil") == std::string_view::npos &&
            source.find("_startupStateItem.enabled=NO") ==
                std::string_view::npos,
        "CRD-02/06: actual Ready row opens Show in both states");
  const auto chrome_item = Between(menu, "_chromeStateItem=[[NSMenuItem alloc]",
                                   "[menu addItem:_chromeStateItem];");
  Check(chrome_item.find("Chrome — Not ready") !=
                std::string_view::npos &&
            chrome_item.find("_chromeStateItem.target=self") !=
                std::string_view::npos,
        "MUI-02: Chrome has its own compact Ready/Not ready outward item");
  const auto inspector_title = menu.find("@\"Show Inspector");
  const auto inspector_end = inspector_title == std::string_view::npos
      ? std::string_view::npos : menu.find("[menu addItem:", inspector_title);
  const auto inspector_item = inspector_end == std::string_view::npos
      ? std::string_view{} : menu.substr(inspector_title,
                                         inspector_end - inspector_title);
  Check(inspector_item.find("action:@selector(showInspector:)") !=
                std::string_view::npos &&
            inspector_item.find("target=self") != std::string_view::npos &&
            inspector_item.find("action:nil") == std::string_view::npos,
        "MUI-01: outward menu has a directly actionable Show Inspector item");
  const auto inspector_show = Between(source,
      "- (void)showInspector:(id)sender {", "- (void)");
  Check(!inspector_show.empty() &&
            inspector_show.find("InspectorVisible()") !=
                std::string_view::npos &&
            inspector_show.find("ToggleInspector()") !=
                std::string_view::npos &&
            inspector_show.find("RequestChromeAutomationPermission") ==
                std::string_view::npos,
        "MUI-01/04: direct Inspector action guards the toggle and does not request Chrome access");
  Check(menu.find("initWithTitle:@\"Quit SeeThis\"") !=
                std::string_view::npos &&
            menu.find("action:@selector(quit:)") != std::string_view::npos,
        "MUI-04: Quit SeeThis remains directly available in the menu");
  for (const auto diagnostic : {"[menu addItem:_inputStateItem]",
                                "[menu addItem:_screenStateItem]",
                                "[menu addItem:_captureStateItem]",
                                "[menu addItem:_deleteStateItem]",
                                "[menu addItem:_settingsStateItem]",
                                "[menu addItem:_guidanceItem]"}) {
    Check(menu.find(diagnostic) == std::string_view::npos,
          "CRD-01/06: long diagnostics are absent from the outward menu");
  }

  const auto refresh = Between(source, "- (void)refreshPermissionMenu {",
                               "- (void)presentPermissionEntry:");
  Check(!refresh.empty(), "native permission refresh is present");
  const auto compact_refresh = Between(refresh, "- (void)refreshPermissionMenu {",
                                       "if(!_entryDetails)return;");
  Check(compact_refresh.find("_startupStateItem.title=") !=
                std::string_view::npos &&
            compact_refresh.find("_chromeStateItem.title=") !=
                std::string_view::npos &&
            compact_refresh.find("RefreshChromeConnection()") !=
                std::string_view::npos &&
            compact_refresh.find("RefreshChromeConnection()") <
                compact_refresh.find("_chromeStateItem.title="),
        "CRD-06/07: both compact items update before Show details exist");
  // A prompt-free asynchronous refresh returns the current snapshot. Initial
  // unknown/pending permission is conservative; a still-fresh cached grant can
  // remain usable while another check runs. Grant alone never means page Ready.
  const auto granted = Between(compact_refresh,
      "} else if(connection.target.valid() &&", "} else {");
  const auto ready = Between(granted, "if(connection.observation_active &&",
      "_chromeCompactStatus=seethis::platform::ChromeCompactStatus::kReady;");
  const auto ungranted = Between(compact_refresh, "} else {", "_chromeStateItem.title=");
  Check(granted.find("connection.permission.consent==") != std::string_view::npos &&
            granted.find("ChromeConsent::kGranted)") != std::string_view::npos &&
            granted.find("ChromeCompactStatus::kAutomationAllowedPageUnchecked") != std::string_view::npos &&
            ungranted.find("ChromeCompactStatus::kAutomationUnavailable") != std::string_view::npos,
        "CRD-06/07: unchecked or ungranted pending snapshots stay Not ready; a current grant is only page-unchecked");
  Check(!ready.empty() &&
            ready.find("connection.provider==seethis::platform::ChromeProviderState::kReady &&") != std::string_view::npos &&
            ready.find("connection.page==seethis::core::PageAvailability::kReady") != std::string_view::npos,
        "MUI-02: compact Chrome Ready additionally requires active observation, ready provider and current page");

  // This target is source-wiring coverage, not an AppKit runtime fixture. The
  // Chrome async executable exercises the real API with blocked fake backends.
  const auto context = Read(source_path.parent_path() / "context_mac.mm");
  const auto connection_refresh = Between(context,
      "ChromeConnectionSnapshot RefreshChromeConnection() {",
      "ChromeConnectionSnapshot ChromeConnectionStatus()");
  const auto passive = Between(context,
      "void BeginPermissionRefresh(bool check_provider=false) {",
      "ChromeProviderState ProbeChromeProvider(ChromeRunningTarget target) {");
  const auto worker = Between(passive, "RunChromeWork(", "},[request,result");
  Check(connection_refresh.find("RefreshCachedConnection();") != std::string_view::npos &&
            connection_refresh.find("BeginPermissionRefresh();") != std::string_view::npos &&
            connection_refresh.find("return connection.snapshot();") != std::string_view::npos &&
            connection_refresh.find("CheckChromeAutomation(") == std::string_view::npos &&
            !worker.empty() && worker.find("adapters.permission(request->target.pid,false)") != std::string_view::npos &&
            worker.find("adapters.permission(request->target.pid,true)") == std::string_view::npos &&
            worker.find("if(check_provider &&") != std::string_view::npos,
        "CRD-05/07: actual compact refresh schedules prompt-free permission work with provider checking disabled by default");
  Check(refresh.find("CGRequestListenEventAccess") == std::string_view::npos &&
            refresh.find("RequestChromeAutomationPermission") ==
                std::string_view::npos &&
            refresh.find("RetryInputMonitoring(") == std::string_view::npos &&
            refresh.find("ConnectChromeExplicit(") == std::string_view::npos &&
            refresh.find("RetryChromeExplicit(") == std::string_view::npos &&
            refresh.find("AcquireChromePageAsync(") == std::string_view::npos &&
            compact_refresh.find("ChromeAutomationPreflight(") == std::string_view::npos,
        "CRD-05/07: reading compact status does not prompt or retry");
  const auto details = refresh.substr(compact_refresh.size());
  for (const auto heading : {"BASE PERMISSIONS", "KEYBOARD AND SHORTCUTS",
                             "SETTINGS", "CHROME (OPTIONAL)", "THIS APP"}) {
    Check(details.find(heading) != std::string_view::npos,
          "CRD-03: expanded diagnostics remain in Show");
  }
  const auto show = Between(source, "- (void)showPermissionEntry:(id)sender {",
                            "- (BOOL)applicationShouldHandleReopen:");
  Check(show.find("PermissionEntryTrigger::kExplicitOpen") !=
            std::string_view::npos &&
            show.find("RetryInputMonitoring(") == std::string_view::npos &&
            show.find("RequestChromeAutomationPermission") ==
                std::string_view::npos,
        "CRD-02/05: Show is explicit and does not recover automatically");
}

void Write(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary);
  output << bytes;
}

std::string StoredSettings(int schema, std::string_view extra = {}) {
  return "{\"schema\":" + std::to_string(schema) +
      ",\"shortcut_key_code\":0,\"shortcut_modifiers\":4,"
      "\"delete_shortcut_key_code\":2,\"delete_shortcut_modifiers\":4,"
      "\"maximum_hold_ms\":30000,\"crop_margin_points\":8,"
      "\"mark_hit_radius_points\":10,\"reference_ttl_seconds\":604800,"
      "\"maximum_visible_references\":500" + std::string(extra) +
      ",\"reference_secret\":\"" + std::string(64, 'a') + "\"}\n";
}

void CheckPointerAndOverlayPolicy() {
  const auto appkit = seethis::platform::AppKitGlobalPointFromQuartz(
      {120.5, 30.25}, 1000);
  Check(appkit && appkit->x == 120.5 && appkit->y == 969.75,
        "CG event location converts from Quartz Y-down to AppKit Y-up");
  Check(!seethis::platform::AppKitGlobalPointFromQuartz(
            {0, 0}, std::numeric_limits<double>::infinity()),
        "non-finite pointer transform is rejected");

  const std::vector<seethis::core::DisplayPoint> stroke = {
      {7, seethis::core::CoordinateUnit::kLogicalPoints, {10, 20}, 2},
      {7, seethis::core::CoordinateUnit::kLogicalPoints, {20, 30}, 2},
      {8, seethis::core::CoordinateUnit::kLogicalPoints, {100, 100}, 1}};
  const auto dirty = seethis::platform::StrokeDirtyBounds(stroke, 7, 6);
  Check(dirty && dirty->x == 4 && dirty->y == 14 && dirty->width == 22 &&
            dirty->height == 22,
        "hover redraw is bounded to the selected display stroke plus padding");
  Check(!seethis::platform::StrokeDirtyBounds(stroke, 9, 6),
        "missing display produces no dirty region");
  Check(seethis::platform::MarkStrokeWidth(false) == 4 &&
            seethis::platform::MarkStrokeWidth(true) == 7,
        "hovered stroke has a discriminating visible width");

  seethis::core::DeleteGestureSnapshot deletion;
  deletion.state = seethis::core::DeleteGestureState::kIdle;
  Check(seethis::platform::OverlayAcceptsPointer(false, true, deletion),
        "ordinary mark click remains accepted for copy");
  deletion.state = seethis::core::DeleteGestureState::kCandidate;
  Check(!seethis::platform::OverlayAcceptsPointer(false, true, deletion),
        "unconfirmed delete candidate passes a click to the foreground");
  deletion.state = seethis::core::DeleteGestureState::kArmed;
  Check(seethis::platform::OverlayAcceptsPointer(false, true, deletion),
        "only an armed hovered mark accepts the delete click");
  deletion.state = seethis::core::DeleteGestureState::kDrain;
  Check(!seethis::platform::OverlayAcceptsPointer(false, true, deletion) &&
            seethis::platform::OverlayAcceptsPointer(true, false, deletion),
        "drain passes later clicks while drawing still captures pointer movement");
}

void CheckSettingsPreservation() {
  TempDirectory temp;
  const auto supported_path = temp.path / "supported.json";
  const auto newer_path = temp.path / "newer.json";
  const auto unknown_path = temp.path / "unknown.json";
  const auto corrupt_path = temp.path / "corrupt.json";
  Write(supported_path, StoredSettings(2));
  const std::string newer_bytes = StoredSettings(3, ",\"future_mode\":7");
  const std::string unknown_bytes = StoredSettings(2, ",\"future_mode\":7");
  const std::string corrupt_bytes = "{\"schema\":2";
  Write(newer_path, newer_bytes);
  Write(unknown_path, unknown_bytes);
  Write(corrupt_path, corrupt_bytes);

  seethis::core::SettingsStore supported(supported_path);
  seethis::core::SettingsStore newer(newer_path);
  seethis::core::SettingsStore unknown(unknown_path);
  seethis::core::SettingsStore corrupt(corrupt_path);
  const std::string reference_id(32, 'f');
  Check(supported.usable() &&
            supported.load_state() == seethis::core::SettingsLoadState::kReady,
        "supported schema remains usable");
  Check(supported.Get().shortcut_key_code == 0 &&
            supported.Get().shortcut_modifiers == 4 &&
            supported.Get().delete_shortcut_key_code == 2 &&
            supported.Get().delete_shortcut_modifiers == 4,
        "supported capture and delete shortcuts remain preserved");
  Check(!newer.usable() && newer.load_state() ==
            seethis::core::SettingsLoadState::kUnsupported &&
            newer.ReferenceCapability(reference_id) ==
                supported.ReferenceCapability(reference_id),
        "newer schema is explicit while preserving capability continuity");
  Check(!unknown.usable() && unknown.load_state() ==
            seethis::core::SettingsLoadState::kUnsupported &&
            unknown.ReferenceCapability(reference_id) ==
                supported.ReferenceCapability(reference_id),
        "unknown schema-2 fields preserve bytes and capability continuity");
  std::string error;
  Check(!newer.Update(newer.Get(), &error) &&
            error.find("preserved") != std::string::npos &&
            Read(newer_path) == newer_bytes && Read(unknown_path) == unknown_bytes,
        "unsupported settings cannot be silently rewritten by an update");
  Check(!corrupt.usable() && corrupt.load_state() ==
            seethis::core::SettingsLoadState::kInvalid &&
            corrupt.ReferenceCapability(reference_id).empty() &&
            Read(corrupt_path) == corrupt_bytes,
        "corrupt settings stay preserved and expose no replacement capability");
  using namespace seethis::core;
  Check(supported.Visibility().permits_presentation() &&
        newer.Visibility().state==GlobalVisibilityState::kUnavailable &&
        unknown.Visibility().state==GlobalVisibilityState::kUnavailable &&
        corrupt.Visibility().state==GlobalVisibilityState::kUnavailable,
        "only a durably initialized valid settings document permits global presentation");
  const auto uninitialized_path=temp.path/"uninitialized.json";
  Write(uninitialized_path,StoredSettings(4,",\"visibility_initialized\":0,\"global_reference_visible\":0"));
  SettingsStore uninitialized(uninitialized_path);
  Check(uninitialized.Visibility().permits_presentation()&&
        uninitialized.Visibility().initialized_this_session&&
        uninitialized.ReferenceCapability(reference_id)==supported.ReferenceCapability(reference_id),
        "valid uninitialized schema4 defaults Visible durably while retaining its secret");
  const auto failed_path=temp.path/"failed-init.json";
  SettingsStore failed(failed_path,[](SettingsPersistPoint p){return p!=SettingsPersistPoint::kBeforeRename;});
  Check(failed.Visibility().state==GlobalVisibilityState::kUnavailable &&
        !std::filesystem::exists(failed_path),
        "pre-rename initialization failure cannot expose unconfirmed default Visible");
  SettingsStore retry(failed_path);
  Check(retry.Visibility().permits_presentation()&&retry.Visibility().initialized_this_session,
        "normal retry durably initializes default Visible once");
  const auto uncertain_path=temp.path/"uncertain-init.json";
  SettingsStore uncertain(uncertain_path,[](SettingsPersistPoint p){return p!=SettingsPersistPoint::kDirectorySync;});
  Check(uncertain.Visibility().state==GlobalVisibilityState::kUncertain&&
        std::filesystem::exists(uncertain_path),"post-rename initialization is uncertain and hidden");
  SettingsStore uncertain_restart(uncertain_path,[](SettingsPersistPoint p){return p!=SettingsPersistPoint::kDirectorySync;});
  Check(uncertain_restart.Visibility().state==GlobalVisibilityState::kUncertain,
        "restart must actually establish durability before exposing renamed initialization");
  SettingsStore durable_restart(uncertain_path);
  Check(durable_restart.Visibility().permits_presentation()&&
        !durable_restart.Visibility().initialized_this_session,
        "successful restart sync confirms existing initialized Visible without repeating migration");
  Check(newer.load_error().find(std::string(64, 'a')) == std::string::npos &&
            seethis::core::SettingsLoadStateName(newer.load_state()) ==
                std::string_view("unsupported"),
        "settings error state does not expose the capability root");
}

void CheckReadinessLatch() {
  using seethis::platform::InputInterruptionKind;
  using seethis::platform::InputInterruptionKindName;
  using seethis::platform::InputInterruptionState;
  using seethis::platform::InputReadinessLatch;
  using seethis::platform::InputRecoveryAction;
  InputReadinessLatch latch;
  const InputMonitorProbe healthy{.listen_authorized=true,.tap_created=true,
      .keyboard_events_present=true,.tap_enabled=true};
  Check(latch.ObserveFresh(healthy,0).state==InputMonitorState::kGranted,
        "healthy fresh tap is granted");
  auto transient=latch.ObserveFresh({.listen_authorized=true,.tap_created=true,
      .keyboard_events_present=true,.tap_enabled=false,
      .interruption=InputInterruptionKind::kTimeout},100);
  Check(transient.state==InputMonitorState::kDisabled&&
            transient.action==InputRecoveryAction::kReenableTap,
        "interrupted enabled-capable tap requests bounded re-enable");
  Check(latch.ObserveFresh(healthy,101).state==InputMonitorState::kRecovered,
        "healthy fresh probe supersedes stale cached non-ready state");

  InputReadinessLatch recreate;
  auto missing=recreate.ObserveFresh({.listen_authorized=true},0);
  Check(missing.action==InputRecoveryAction::kRecreateTap,
        "authorized missing tap requests controlled recreation");
  Check(recreate.ObserveFresh({.listen_authorized=true},100).action==
            InputRecoveryAction::kNone,
        "recovery backoff prevents a tight loop");
  (void)recreate.ObserveFresh({.listen_authorized=true},500);
  (void)recreate.ObserveFresh({.listen_authorized=true},1500);
  Check(recreate.ObserveFresh({.listen_authorized=true},3000).action==
            InputRecoveryAction::kNone,
        "repeated failure is bounded at three automatic attempts");

  InputReadinessLatch denied;
  Check(denied.ObserveFresh({},0).state==InputMonitorState::kDenied&&
            denied.ObserveFresh({},1000).action==InputRecoveryAction::kNone,
        "real denial remains denied and never loops recovery");
  Check(denied.ObserveFresh(healthy,1001).state==InputMonitorState::kRecovered,
        "authorization restoration converges from a fresh usable probe");

  InputReadinessLatch partial;
  const auto incomplete=partial.ObserveFresh({.listen_authorized=true,
      .tap_created=true,.tap_enabled=true},0);
  Check(incomplete.state==InputMonitorState::kPartialTap&&
            incomplete.action==InputRecoveryAction::kRecreateTap,
        "non-keyboard tap cannot be latched ready");

  InputInterruptionState interruption;
  interruption.Record(InputInterruptionKind::kTimeout);
  const auto timeout_probe=interruption.Apply(healthy);
  Check(timeout_probe.interruption==InputInterruptionKind::kTimeout&&
            InputInterruptionKindName(timeout_probe.interruption)=="timeout"&&
            ClassifyInputMonitor(timeout_probe)==InputMonitorState::kDisabled,
        "timeout callback cause survives into the fresh readiness probe");
  interruption.Clear();
  Check(interruption.kind()==InputInterruptionKind::kNone&&
            ClassifyInputMonitor(interruption.Apply(healthy))==
                InputMonitorState::kGranted,
        "reported timeout cause clears without stale replay");
  interruption.Record(InputInterruptionKind::kUserInput);
  const auto user_input_probe=interruption.Apply(healthy);
  Check(user_input_probe.interruption==InputInterruptionKind::kUserInput&&
            InputInterruptionKindName(user_input_probe.interruption)==
                "user_input"&&
            ClassifyInputMonitor(user_input_probe)==InputMonitorState::kDisabled,
        "user-input callback remains distinct through the readiness probe");
  const auto user_input_decision=
      latch.ObserveFresh(user_input_probe,200);
  interruption.Clear();
  Check(user_input_decision.action==InputRecoveryAction::kReenableTap&&
            latch.ObserveFresh(interruption.Apply(healthy),201).state==
                InputMonitorState::kRecovered,
        "cleared user-input cause permits one fresh recovered state");
}

void CheckCompactPermissionEntry() {
  using seethis::platform::BasePermissionReady;
  using seethis::platform::InputMonitoringPermissionState;
  using seethis::platform::PermissionEntryShouldPresent;
  using seethis::platform::PermissionEntryTrigger;
  using seethis::platform::PermissionStartupStatus;
  using seethis::platform::ScreenRecordingState;
  using seethis::platform::ChromeCompactStatus;
  using seethis::platform::ChromeCompactStatusLabel;

  for (const auto input : {InputMonitoringPermissionState::kUnknown,
                           InputMonitoringPermissionState::kDenied,
                           InputMonitoringPermissionState::kGranted}) {
    for (const auto screen : {ScreenRecordingState::kUnknown,
                              ScreenRecordingState::kDenied,
                              ScreenRecordingState::kReady}) {
      const bool granted = input == InputMonitoringPermissionState::kGranted &&
                           screen == ScreenRecordingState::kReady;
      Check(BasePermissionReady(input, screen) == granted,
            "CRD-04: only both granted required permissions are Ready");
      Check(PermissionStartupStatus(input, screen) ==
                (granted ? "SeeThis — Ready" : "SeeThis — Not ready"),
            "CRD-04: compact label follows permission authorization");
      Check(PermissionEntryShouldPresent(
                PermissionEntryTrigger::kExplicitOpen, input, screen),
            "CRD-02: explicit open reaches both compact states");
      Check(!PermissionEntryShouldPresent(
                PermissionEntryTrigger::kPermissionRefresh, input, screen),
            "CRD-06: quiet polling never reopens a dismissed panel");
      Check(PermissionEntryShouldPresent(
                PermissionEntryTrigger::kLaunch, input, screen) == !granted,
            "CRD-06: launch stays quiet only when required grants are ready");
    }
  }

  for (const auto chrome : {ChromeCompactStatus::kUnchecked,
                            ChromeCompactStatus::kNotRunning,
                            ChromeCompactStatus::kAutomationUnavailable,
                            ChromeCompactStatus::kAutomationAllowedPageUnchecked}) {
    Check(BasePermissionReady(InputMonitoringPermissionState::kGranted,
                              ScreenRecordingState::kReady) &&
              PermissionStartupStatus(InputMonitoringPermissionState::kGranted,
                                      ScreenRecordingState::kReady) ==
                  "SeeThis — Ready" &&
              ChromeCompactStatusLabel(chrome) == "Chrome — Not ready",
          "MUI-02/03: optional unready Chrome state cannot revoke base readiness");
  }
  Check(ChromeCompactStatusLabel(ChromeCompactStatus::kUnchecked) ==
                "Chrome — Not ready" &&
            ChromeCompactStatusLabel(ChromeCompactStatus::kNotRunning) ==
                "Chrome — Not ready" &&
            ChromeCompactStatusLabel(
                ChromeCompactStatus::kAutomationUnavailable) ==
                "Chrome — Not ready" &&
            ChromeCompactStatusLabel(
                ChromeCompactStatus::kAutomationAllowedPageUnchecked) ==
                "Chrome — Not ready",
        "MUI-02: outward Chrome labels stay compact and cannot treat consent as page readiness");
  Check(HasReadyChromeLabel<ChromeCompactStatus>(),
        "MUI-02: verified existing Chrome capability has the exact Ready label");

  // Authorization is independent of whether the input tap is operational.
  // The expanded detail must continue to carry the exact fault and remedy.
  for (const auto tap : {InputMonitorState::kTapCreationFailed,
                         InputMonitorState::kPartialTap,
                         InputMonitorState::kDisabled,
                         InputMonitorState::kRestartRequired}) {
    seethis::platform::PermissionReadinessState snapshot;
    snapshot.input_monitor_permission.store(
        InputMonitoringPermissionState::kGranted);
    snapshot.screen_recording.store(ScreenRecordingState::kReady);
    snapshot.input_monitor.store(tap);
    snapshot.capture_shortcut.store(
        seethis::platform::CaptureShortcutState::kInputUnavailable);
    snapshot.delete_shortcut.store(
        seethis::platform::DeleteShortcutState::kInputUnavailable);
    Check(BasePermissionReady(InputMonitoringPermissionState::kGranted,
                              ScreenRecordingState::kReady) &&
              PermissionStartupStatus(snapshot.input_monitor_permission.load(),
                                      snapshot.screen_recording.load()) ==
                  "SeeThis — Ready" &&
              !InputMonitorReady(snapshot.input_monitor.load()) &&
              seethis::platform::InputMonitorStateName(
                  snapshot.input_monitor.load()) != "granted" &&
              !seethis::platform::InputMonitorGuidance(
                  snapshot.input_monitor.load()).empty() &&
              !seethis::platform::CaptureShortcutReady(
                  snapshot.capture_shortcut.load()) &&
              !seethis::platform::DeleteShortcutReady(
                  snapshot.delete_shortcut.load()),
          "CRD-04: tap faults do not change base permission grants");
  }

  using seethis::platform::CaptureShortcutReadiness;
  using seethis::platform::CaptureShortcutState;
  using seethis::platform::DeleteShortcutState;
  for (const auto shortcut : {CaptureShortcutState::kSettingsUnavailable,
                              CaptureShortcutState::kConflict,
                              CaptureShortcutState::kRegistrationFailed,
                              CaptureShortcutState::kInputUnavailable}) {
    Check(!seethis::platform::CaptureShortcutReady(shortcut) &&
              !seethis::platform::CaptureShortcutGuidance(shortcut).empty() &&
              BasePermissionReady(InputMonitoringPermissionState::kGranted,
                                  ScreenRecordingState::kReady),
          "CRD-04: shortcut fault does not revoke grants");
  }
  Check(CaptureShortcutReadiness(false, false, false, true) ==
            CaptureShortcutState::kSettingsUnavailable &&
            CaptureShortcutReadiness(true, true, false, true) ==
                CaptureShortcutState::kConflict &&
            CaptureShortcutReadiness(true, false, false, true) ==
                CaptureShortcutState::kRegistrationFailed &&
            CaptureShortcutReadiness(true, false, true, false) ==
                CaptureShortcutState::kInputUnavailable,
        "capture shortcut faults remain distinct");
  for (const auto shortcut : {DeleteShortcutState::kSettingsUnavailable,
                              DeleteShortcutState::kConflict,
                              DeleteShortcutState::kRegistrationFailed,
                              DeleteShortcutState::kInputUnavailable}) {
    Check(!seethis::platform::DeleteShortcutReady(shortcut) &&
              !seethis::platform::DeleteShortcutGuidance(shortcut).empty() &&
              BasePermissionReady(InputMonitoringPermissionState::kGranted,
                                  ScreenRecordingState::kReady),
          "delete shortcut fault has detail without changing grants");
  }
}

void CheckPrefixReadinessIngress() {
  using namespace seethis::platform;
  CapturePrefixReadiness good;
  good.running=good.settings_usable=good.registered=good.handler_ready=true;
  good.monitor_ready=good.screen_ready=good.shortcut_ready=good.native_binding_matches=true;
  good.configured_key=good.registered_key=0;
  good.configured_modifiers=good.registered_modifiers=3;good.registration_generation=7;
  for(unsigned fault=0;fault<14;++fault) {
    auto unavailable=good;
    switch(fault) {
      case 0:unavailable.settings_usable=false;break;
      case 1:unavailable.registered=false;break;
      case 2:++unavailable.configured_key;break;
      case 3:++unavailable.configured_modifiers;break;
      case 4:unavailable.native_binding_matches=false;break;
      case 5:unavailable.registration_generation=0;break;
      case 6:unavailable.handler_ready=false;break;
      case 7:unavailable.monitor_ready=false;break;
      case 8:unavailable.screen_ready=false;break;
      case 9:unavailable.shortcut_ready=false;break;
      case 10:unavailable.conflict=true;break;
      case 11:unavailable.secure_input=true;break;
      case 12:unavailable.running=false;break;
      case 13:++unavailable.registration_generation;break;
    }
    CapturePrefixIngress ingress;unsigned begins=0,confirms=0,cancels=0;
    bool paint=true,hit=true;ViewportContinuity epoch;epoch.Observe(ViewportInput::kHealthy);
    ingress.SetActions({[&]{++begins;hit=false;return true;},[&]{++confirms;hit=true;},
      [&]{++cancels;paint=hit=false;},[&]{epoch.Observe(ViewportInput::kKeyboard);},{}});
    // A generation mismatch is meaningful only after a prefix froze generation7.
    if(fault!=13) {
      Check(!ingress.ModifierEvent(unavailable,1,3,100)&&begins==0&&!ingress.pending(),
          "actual prefix ingress refuses unavailable modifier before calling Begin");
      ingress.Confirm(unavailable,101);
      Check(confirms==0,"unavailable matching key cannot call prefix Confirm");
      ingress.Released(0);
    }
    Check(ingress.ModifierEvent(good,1,3,200)&&begins==1&&paint&&!hit,
        "healthy exact binding begins pending carry with paint kept and hit suspended");
    const auto prior=epoch.epoch();
    Check(!ingress.Readiness(unavailable)&&!ingress.pending()&&cancels==1&&
        epoch.epoch()>prior&&!paint&&!hit,
        "live readiness loss immediately cancels pending carry and retires presentation epoch without another event/deadline");
    ingress.Confirm(good,201);
    Check(confirms==0&&begins==1&&!paint,"readiness recovery cannot confirm or restore old carry");
    ingress.Released(0);
    Check(ingress.ModifierEvent(good,1,3,300)&&begins==2,"ready fresh modifier after actual release can start a new prefix");
    ingress.Confirm(good,301);
    Check(confirms==1&&!ingress.pending(),"ready exact fresh hotkey reaches confirmation after readiness loss");
  }
  {
    CapturePrefixIngress ingress;unsigned cancels=0,retirements=0;
    ingress.SetActions({[]{return true;},{},[&]{++cancels;},[&]{++retirements;},{}});
    Check(ingress.ModifierEvent(good,1,3,100),"binding teardown fixture is pending");
    ingress.LoseReadiness();
    Check(!ingress.pending()&&cancels==1&&retirements==1,
        "actual unregister/stop/config replacement ingress retires without waiting for input");
  }
  // Source binding proves the production caller uses this tested ingress BEFORE
  // provisional preservation; this is not an OS event-delivery assertion.
  const auto source=Read(std::filesystem::path(__FILE__).parent_path().parent_path()/"src/platform/mac/input_mac.mm");
  const auto event=Between(source,"void HandleEvent(CGEventType type", "    ReconcileCaptureHold();");
  Check(event.find("RefreshPrefixReadiness()")<event.find("prefix_ingress_.ModifierEvent")&&
      event.find("ModifierEvent(PrefixReadinessSnapshot()")!=std::string_view::npos,
      "production raw modifier ingress uses live readiness before tested Begin ordering");
  const auto registration=Between(source,"void RegisterCaptureHotKey()", "void RegisterDeleteHotKey()");
  Check(registration.find("RetirePrefixReadiness()")<registration.find("UnregisterEventHotKey"),
      "production binding replacement retires carry before unregistering");
  const auto watchdog=Between(source,"void RunWatchdog()", "    const auto requested_retry");
  const auto stop=Between(source,"void Stop() override", "    interruption_state_.Clear();");
  Check(watchdog.find("RefreshPrefixReadiness()")!=std::string_view::npos&&
      stop.find("RetirePrefixReadiness()")!=std::string_view::npos,
      "production watchdog and stop publish loss without another user input");
  Check(source.find("prefix_ingress_.Confirm(self->PrefixReadinessSnapshot(),observed_ms)")!=std::string::npos,
      "production existing hotkey confirms only through readiness-gated tested ingress");
}

}  // namespace

int main(int argc,char** argv) {
  if(argc==2&&std::string_view(argv[1])=="--p4-component-only") {
    using namespace seethis::platform;
    CheckPrefixReadinessIngress();
    CapturePrefixContinuity prefix;
    Check(prefix.Modifiers(1,3,100)==CapturePrefixAction::kStarted&&prefix.PhysicalModifiersValid(1),
        "legal partial multi-modifier prefix survives the production watchdog predicate");
    Check(prefix.Modifiers(3,3,400)==CapturePrefixAction::kPending&&prefix.PhysicalModifiersValid(3)&&
        prefix.Confirm(599)==CapturePrefixAction::kConfirmed,"stepwise modifiers confirm before first-edge deadline without extension");
    prefix.Released(0);
    Check(prefix.Modifiers(1,3,1000)==CapturePrefixAction::kStarted&&
        prefix.Modifiers(3,3,1499)==CapturePrefixAction::kPending&&
        prefix.Tick(1500)==CapturePrefixAction::kRetired,"500ms first-edge deadline cannot be extended by another modifier");
    Check(prefix.Confirm(1501)==CapturePrefixAction::kNone,"late hotkey has no retired carry checkpoint");
    prefix.Released(0);
    Check(prefix.Modifiers(1,3,2000)==CapturePrefixAction::kStarted&&
        prefix.Modifiers(0,3,2001)==CapturePrefixAction::kRetired,"abandoned prefix keyup retires");
    prefix.Released(0);
    Check(prefix.Modifiers(1,3,3000)==CapturePrefixAction::kStarted&&
        prefix.Modifiers(5,3,3001)==CapturePrefixAction::kRetired,"extra modifier retires carry");
    for(const auto cause:{"pointer","scroll","nonmatch","context","permission","monitor"}) {
      (void)cause;prefix.Released(0);
      Check(prefix.Modifiers(1,3,4000)==CapturePrefixAction::kStarted&&
          prefix.Retire()==CapturePrefixAction::kRetired&&prefix.Confirm(4001)==CapturePrefixAction::kNone,
          "production invalidating-event transition irreversibly rejects prefix carry");
    }
    ViewportContinuity continuity;
    Check(!continuity.healthy(),"restart cannot inherit input health or lease");
    continuity.Observe(ViewportInput::kHealthy);const auto initial=continuity.epoch();
    continuity.Observe(ViewportInput::kPointer,true);continuity.Observe(ViewportInput::kKeyboard,true);
    Check(continuity.healthy()&&continuity.epoch()==initial,"positively owned badge and note input preserve observed continuity");
    for(const auto event:{ViewportInput::kScroll,ViewportInput::kScroll,ViewportInput::kPointer,ViewportInput::kKeyboard}) {
      const auto before=continuity.epoch();continuity.Observe(event);
      Check(continuity.epoch()>before,"scroll momentum unknown pointer and noncapture keyboard invalidate");
    }
    continuity.Observe(ViewportInput::kInterrupted);const auto retired=continuity.epoch();
    Check(!continuity.healthy(),"tap interruption loses health");continuity.Observe(ViewportInput::kHealthy);
    Check(continuity.epoch()==retired&&continuity.healthy(),"recovery changes health without restoring capture epoch");
    for(bool modifier_first:{false,true}) {
      CaptureReleaseContinuity release;release.Begin(3);
      ViewportContinuity epoch;epoch.Observe(ViewportInput::kHealthy);const auto began=epoch.epoch();
      const auto observe=[&](bool key,bool up,bool flags,std::uint64_t modifiers) {
        if(!release.Observe(key,up,flags,modifiers))epoch.Observe(ViewportInput::kKeyboard);
      };
      if(modifier_first) {observe(false,false,true,2);observe(false,false,true,0);observe(true,true,false,0);}
      else {observe(true,true,false,3);observe(false,false,true,2);observe(false,false,true,0);}
      Check(epoch.epoch()==began,"both physical release orders including idle trailing modifier preserve capture epoch");
      observe(false,false,false,0);
      Check(epoch.epoch()!=began,"later unrelated key still retires continuity");
    }
    std::cout<<checks<<" P4 continuity pure component checks passed\n";return 0;
  }
  try {
    CheckReadinessLatch();
    CheckCompactPermissionEntry();
    CheckNativeCompactMenu();
    Check(ClassifyInputMonitor({}) == InputMonitorState::kDenied,
          "missing listen authorization is denied");
    Check(ClassifyInputMonitor({.listen_authorized = true}) ==
              InputMonitorState::kTapCreationFailed,
          "authorized tap creation failure remains explicit");
    Check(ClassifyInputMonitor({.listen_authorized = true,
                                .tap_created = true,
                                .tap_enabled = true}) ==
              InputMonitorState::kPartialTap,
          "a non-null mouse or modifier-only tap is partial");
    Check(ClassifyInputMonitor({.listen_authorized = true,
                                .tap_created = true,
                                .keyboard_events_present = true,
                                .tap_enabled = false}) ==
              InputMonitorState::kDisabled,
          "an installed disabled tap is not ready");
    Check(ClassifyInputMonitor({.listen_authorized = true,
                                .tap_created = true,
                                .keyboard_events_present = true,
                                .tap_enabled = true,
                                .interruption =
                                    seethis::platform::InputInterruptionKind::
                                        kTimeout}) ==
              InputMonitorState::kDisabled,
          "an interruption is disabled even if stale tap facts look ready");
    Check(ClassifyInputMonitor({.listen_authorized = true,
                                .retry_attempt = true}) ==
              InputMonitorState::kRestartRequired,
          "failed authorized retry requests a relaunch");
    Check(ClassifyInputMonitor({.listen_authorized = true,
                                .tap_created = true,
                                .tap_enabled = true,
                                .retry_attempt = true}) ==
              InputMonitorState::kRestartRequired,
          "partial retry requests a relaunch");
    const auto granted = ClassifyInputMonitor({.listen_authorized = true,
                                                .tap_created = true,
                                                .keyboard_events_present = true,
                                                .tap_enabled = true});
    Check(granted == InputMonitorState::kGranted && InputMonitorReady(granted),
          "authorized usable initial keyboard tap is granted");
    const auto recovered = ClassifyInputMonitor({.listen_authorized = true,
                                                  .tap_created = true,
                                                  .keyboard_events_present = true,
                                                  .tap_enabled = true,
                                                  .retry_attempt = true});
    Check(recovered == InputMonitorState::kRecovered &&
              InputMonitorReady(recovered),
          "authorized usable retry is recovered");
    Check(!InputMonitorReady(InputMonitorState::kPartialTap) &&
              !InputMonitorReady(InputMonitorState::kRestartRequired),
          "non-null and restart-required states never imply readiness");
    using seethis::platform::ScreenRecordingState;
    Check(seethis::platform::InputMonitorStateName(
              InputMonitorState::kRestartRequired) == "restart_required" &&
              seethis::platform::InputMonitorGuidance(
                  InputMonitorState::kRestartRequired)
                      .find("exact app") != std::string_view::npos,
          "restart-required state has exact-app guidance");
    Check(seethis::platform::InputMonitorStateName(InputMonitorState::kUnknown) ==
              "unknown" &&
              seethis::platform::InputMonitorGuidance(InputMonitorState::kUnknown)
                      .find("not been checked") != std::string_view::npos,
          "unknown input readiness remains explicit");
    Check(seethis::platform::ScreenRecordingStateName(
              seethis::platform::ScreenRecordingState::kDenied) == "denied" &&
              seethis::platform::ScreenRecordingStateName(
                  seethis::platform::ScreenRecordingState::kReady) == "ready",
          "screen readiness is represented separately");
    Check(seethis::platform::ScreenRecordingGuidance(
              seethis::platform::ScreenRecordingState::kUnknown)
                  .find("not been checked") != std::string_view::npos,
        "unknown Screen Recording readiness remains explicit");
    Check(seethis::platform::InputMonitorGuidance(InputMonitorState::kDenied)
                  .find("Input Monitoring") != std::string_view::npos &&
              seethis::platform::ScreenRecordingGuidance(
                  seethis::platform::ScreenRecordingState::kDenied)
                      .find("Screen Recording") != std::string_view::npos &&
              seethis::platform::InputMonitorGuidance(InputMonitorState::kDenied) !=
                  seethis::platform::ScreenRecordingGuidance(
                      seethis::platform::ScreenRecordingState::kDenied),
          "input and screen recovery guidance stays independent");
    const auto relaunch_guidance =
        seethis::platform::ExactAppRelaunchGuidance();
    Check(relaunch_guidance.find("same bundle identifier") !=
                  std::string_view::npos &&
              relaunch_guidance.find("Quit & Reopen") != std::string_view::npos &&
              relaunch_guidance.find("quit extra copies") != std::string_view::npos &&
              relaunch_guidance.find("exact displayed path") !=
                  std::string_view::npos,
          "exact-path guidance covers multiple copies and manual reopen");
    Check(seethis::platform::DeleteShortcutStateName(
              seethis::platform::DeleteShortcutState::kSettingsUnavailable) ==
              "settings_unavailable",
          "delete readiness reports preserved unusable settings separately");
    CheckPointerAndOverlayPolicy();
    CheckSettingsPreservation();
    std::cout << checks << " input permission state checks passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "FAILED after " << checks << " checks: " << exception.what()
              << '\n';
    return 1;
  }
}
