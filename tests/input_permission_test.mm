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

}  // namespace

int main() {
  try {
    CheckReadinessLatch();
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
    Check(seethis::platform::PermissionStartupStatus(
              InputMonitorState::kGranted,
              seethis::platform::ScreenRecordingState::kReady) == "SeeThis",
          "ready startup keeps the menu-bar label unobtrusive");
    Check(seethis::platform::PermissionStartupStatus(
              InputMonitorState::kPartialTap,
              seethis::platform::ScreenRecordingState::kReady)
                  .find("Input Monitoring not ready") != std::string_view::npos,
          "input-only blocked startup is discoverable before opening the menu");
    Check(seethis::platform::PermissionStartupStatus(
              InputMonitorState::kGranted,
              seethis::platform::ScreenRecordingState::kDenied)
                  .find("Screen Recording not ready") != std::string_view::npos,
          "screen-only blocked startup is discoverable before opening the menu");
    Check(seethis::platform::PermissionStartupStatus(
              InputMonitorState::kDenied,
              seethis::platform::ScreenRecordingState::kDenied)
                  .find("permissions not ready") != std::string_view::npos,
          "both blocked permissions are discoverable before opening the menu");
    using seethis::platform::PermissionEntryShouldPresent;
    using seethis::platform::PermissionEntryTrigger;
    using seethis::platform::ScreenRecordingState;
    for(const auto input : {InputMonitorState::kUnknown,InputMonitorState::kDenied,
        InputMonitorState::kTapCreationFailed,InputMonitorState::kPartialTap,
        InputMonitorState::kDisabled,InputMonitorState::kRestartRequired,
        InputMonitorState::kGranted,InputMonitorState::kRecovered}) {
      for(const auto screen : {ScreenRecordingState::kUnknown,
          ScreenRecordingState::kDenied,ScreenRecordingState::kReady}) {
        Check(!PermissionEntryShouldPresent(
            PermissionEntryTrigger::kPermissionRefresh,input,screen),
            "permission polling never reopens a dismissed entry window");
        Check(PermissionEntryShouldPresent(
            PermissionEntryTrigger::kExplicitOpen,input,screen),
            "Finder reopen can reach the entry regardless of permission state");
      }
    }
    Check(!PermissionEntryShouldPresent(PermissionEntryTrigger::kLaunch,
              InputMonitorState::kGranted,ScreenRecordingState::kReady) &&
          !PermissionEntryShouldPresent(PermissionEntryTrigger::kLaunch,
              InputMonitorState::kRecovered,ScreenRecordingState::kReady),
          "ready startup preserves quiet behavior");
    Check(PermissionEntryShouldPresent(PermissionEntryTrigger::kLaunch,
              InputMonitorState::kUnknown,ScreenRecordingState::kUnknown) &&
          PermissionEntryShouldPresent(PermissionEntryTrigger::kLaunch,
              InputMonitorState::kPartialTap,ScreenRecordingState::kReady) &&
          PermissionEntryShouldPresent(PermissionEntryTrigger::kLaunch,
              InputMonitorState::kGranted,ScreenRecordingState::kDenied),
          "first launch offers guidance for unknown or blocked permissions");
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
