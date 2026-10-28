#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/interaction.h"
#include "core/reference.h"
#include "core/settings.h"

namespace seethis::service {
class ReferenceServer;
}

namespace seethis::platform {

enum class Backend {
  kMacOS,
  kWindowsUnavailable,
  kLinuxUnavailable,
  kUnsupported,
};

enum class InputMonitorState {
  kUnknown,
  kDenied,
  kTapCreationFailed,
  kPartialTap,
  kDisabled,
  kRestartRequired,
  kGranted,
  kRecovered,
};

enum class ScreenRecordingState {
  kUnknown,
  kDenied,
  kReady,
};

// This records the nonprompting system permission preflight independently of
// the event tap. A granted permission does not imply that a tap is healthy.
enum class InputMonitoringPermissionState {
  kUnknown,
  kDenied,
  kGranted,
};

enum class DeleteShortcutState {
  kUnknown,
  kInputUnavailable,
  kSettingsUnavailable,
  kConflict,
  kRegistrationFailed,
  kSecureInput,
  kReady,
};

enum class CaptureShortcutState {
  kUnknown,
  kInputUnavailable,
  kSettingsUnavailable,
  kConflict,
  kRegistrationFailed,
  kReady,
};

[[nodiscard]] constexpr bool CaptureShortcutReady(CaptureShortcutState state) {
  return state == CaptureShortcutState::kReady;
}

[[nodiscard]] constexpr std::string_view CaptureShortcutStateName(
    CaptureShortcutState state) {
  switch (state) {
    case CaptureShortcutState::kUnknown: return "unknown";
    case CaptureShortcutState::kInputUnavailable: return "input_unavailable";
    case CaptureShortcutState::kSettingsUnavailable:
      return "settings_unavailable";
    case CaptureShortcutState::kConflict: return "conflict";
    case CaptureShortcutState::kRegistrationFailed:
      return "registration_failed";
    case CaptureShortcutState::kReady: return "ready";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view CaptureShortcutGuidance(
    CaptureShortcutState state) {
  switch (state) {
    case CaptureShortcutState::kUnknown:
      return "Capture shortcut readiness has not been checked yet.";
    case CaptureShortcutState::kInputUnavailable:
      return "Restore Input Monitoring before using the capture shortcut.";
    case CaptureShortcutState::kSettingsUnavailable:
      return "Repair or replace the preserved settings file before using shortcuts.";
    case CaptureShortcutState::kConflict:
      return "Choose distinct capture and delete shortcuts in settings.";
    case CaptureShortcutState::kRegistrationFailed:
      return "The capture shortcut could not be reserved; change the binding or retry.";
    case CaptureShortcutState::kReady:
      return "Hold the capture shortcut and draw a region.";
  }
  return "Capture shortcut readiness has not been checked yet.";
}

[[nodiscard]] constexpr CaptureShortcutState CaptureShortcutReadiness(
    bool settings_available, bool conflict, bool registration_ready,
    bool input_ready) {
  if (!settings_available) return CaptureShortcutState::kSettingsUnavailable;
  if (conflict) return CaptureShortcutState::kConflict;
  if (!registration_ready) return CaptureShortcutState::kRegistrationFailed;
  if (!input_ready) return CaptureShortcutState::kInputUnavailable;
  return CaptureShortcutState::kReady;
}

enum class InputInterruptionKind { kNone, kTimeout, kUserInput };

[[nodiscard]] constexpr std::string_view InputInterruptionKindName(
    InputInterruptionKind kind) {
  switch (kind) {
    case InputInterruptionKind::kNone: return "none";
    case InputInterruptionKind::kTimeout: return "timeout";
    case InputInterruptionKind::kUserInput: return "user_input";
  }
  return "none";
}

struct InputMonitorProbe {
  bool listen_authorized = false;
  bool tap_created = false;
  bool keyboard_events_present = false;
  bool tap_enabled = false;
  bool retry_attempt = false;
  InputInterruptionKind interruption = InputInterruptionKind::kNone;
};

[[nodiscard]] constexpr InputMonitorState ClassifyInputMonitor(
    const InputMonitorProbe& probe) {
  if (probe.interruption != InputInterruptionKind::kNone ||
      (probe.tap_created && !probe.tap_enabled)) {
    return InputMonitorState::kDisabled;
  }
  if (!probe.listen_authorized) return InputMonitorState::kDenied;
  if (!probe.tap_created) {
    return probe.retry_attempt ? InputMonitorState::kRestartRequired
                               : InputMonitorState::kTapCreationFailed;
  }
  if (!probe.keyboard_events_present) {
    return probe.retry_attempt ? InputMonitorState::kRestartRequired
                               : InputMonitorState::kPartialTap;
  }
  return probe.retry_attempt ? InputMonitorState::kRecovered
                             : InputMonitorState::kGranted;
}

// Main-thread native callbacks retain only this bounded cause until the next
// fresh readiness diagnostic. No event payload or key data crosses this seam.
class InputInterruptionState {
 public:
  void Record(InputInterruptionKind kind) {
    if (kind != InputInterruptionKind::kNone) kind_ = kind;
  }

  [[nodiscard]] InputInterruptionKind kind() const { return kind_; }

  [[nodiscard]] InputMonitorProbe Apply(InputMonitorProbe probe) const {
    probe.interruption = kind_;
    return probe;
  }

  void Clear() { kind_ = InputInterruptionKind::kNone; }

 private:
  InputInterruptionKind kind_ = InputInterruptionKind::kNone;
};

[[nodiscard]] constexpr bool InputMonitorReady(InputMonitorState state) {
  return state == InputMonitorState::kGranted ||
         state == InputMonitorState::kRecovered;
}

enum class InputRecoveryAction { kNone, kReenableTap, kRecreateTap };
struct InputReadinessDecision {
  InputMonitorState state = InputMonitorState::kUnknown;
  InputRecoveryAction action = InputRecoveryAction::kNone;
  std::uint32_t recovery_attempts = 0;
};

// A fresh-probe latch. The native adapter owns the tap and executes the
// returned action; this reducer only bounds and spaces recovery attempts.
class InputReadinessLatch {
 public:
  static constexpr std::uint32_t kMaximumRecoveryAttempts = 3;
  static constexpr std::int64_t kRecoveryBackoffMilliseconds = 500;

  [[nodiscard]] InputReadinessDecision ObserveFresh(
      InputMonitorProbe probe, std::int64_t monotonic_ms) {
    probe.retry_attempt = false;
    const auto observed = ClassifyInputMonitor(probe);
    if (InputMonitorReady(observed)) {
      const bool recovered = ever_non_ready_ || recovery_attempts_ > 0;
      recovery_attempts_ = 0;
      next_recovery_ms_ = 0;
      ever_non_ready_ = false;
      return {recovered ? InputMonitorState::kRecovered
                        : InputMonitorState::kGranted,
              InputRecoveryAction::kNone, 0};
    }
    ever_non_ready_ = true;
    if (observed == InputMonitorState::kDenied ||
        monotonic_ms < next_recovery_ms_ ||
        recovery_attempts_ >= kMaximumRecoveryAttempts) {
      return {observed, InputRecoveryAction::kNone, recovery_attempts_};
    }
    ++recovery_attempts_;
    next_recovery_ms_ = monotonic_ms +
        kRecoveryBackoffMilliseconds * recovery_attempts_;
    return {observed,
            observed == InputMonitorState::kDisabled && probe.tap_created
                ? InputRecoveryAction::kReenableTap
                : InputRecoveryAction::kRecreateTap,
            recovery_attempts_};
  }

  void Reset() {
    recovery_attempts_ = 0;
    next_recovery_ms_ = 0;
    ever_non_ready_ = false;
  }

 private:
  std::uint32_t recovery_attempts_ = 0;
  std::int64_t next_recovery_ms_ = 0;
  bool ever_non_ready_ = false;
};

[[nodiscard]] constexpr std::string_view ExactAppRelaunchGuidance() {
  return "Multiple copies with the same bundle identifier may exist. System "
         "Quit & Reopen may choose a different copy; quit extra copies and "
         "manually reopen the exact displayed path.";
}

[[nodiscard]] constexpr bool ScreenRecordingReady(ScreenRecordingState state) {
  return state == ScreenRecordingState::kReady;
}

[[nodiscard]] constexpr bool BasePermissionReady(
    InputMonitoringPermissionState input, ScreenRecordingState screen) {
  return input == InputMonitoringPermissionState::kGranted &&
         ScreenRecordingReady(screen);
}

[[nodiscard]] constexpr std::string_view InputMonitoringPermissionStateName(
    InputMonitoringPermissionState state) {
  switch (state) {
    case InputMonitoringPermissionState::kUnknown: return "unknown";
    case InputMonitoringPermissionState::kDenied: return "denied";
    case InputMonitoringPermissionState::kGranted: return "granted";
  }
  return "unknown";
}

// This is an optional, prompt-free menu observation. Automation consent alone
// does not verify the current page or provider identity.
enum class ChromeCompactStatus {
  kUnchecked,
  kNotRunning,
  kAutomationUnavailable,
  kAutomationAllowedPageUnchecked,
  kReady,
};

[[nodiscard]] constexpr std::string_view ChromeCompactStatusLabel(
    ChromeCompactStatus status) {
  switch (status) {
    case ChromeCompactStatus::kReady: return "Chrome — Ready";
    case ChromeCompactStatus::kUnchecked:
    case ChromeCompactStatus::kNotRunning:
    case ChromeCompactStatus::kAutomationUnavailable:
    case ChromeCompactStatus::kAutomationAllowedPageUnchecked:
      return "Chrome — Not ready";
  }
  return "Chrome — Not ready";
}

[[nodiscard]] constexpr std::string_view PermissionStartupStatus(
    InputMonitoringPermissionState input, ScreenRecordingState screen) {
  return BasePermissionReady(input, screen) ? "SeeThis — Ready"
                                            : "SeeThis — Not ready";
}

enum class PermissionEntryTrigger {
  kLaunch,
  kPermissionRefresh,
  kExplicitOpen,
};

// Permission polling must never reopen a dismissed entry window. Opening the
// app again is an explicit request even when capture permissions are ready.
[[nodiscard]] constexpr bool PermissionEntryShouldPresent(
    PermissionEntryTrigger trigger, InputMonitoringPermissionState input,
    ScreenRecordingState screen) {
  if (trigger == PermissionEntryTrigger::kExplicitOpen) return true;
  if (trigger == PermissionEntryTrigger::kPermissionRefresh) return false;
  return !BasePermissionReady(input, screen);
}

[[nodiscard]] constexpr std::string_view InputMonitorStateName(
    InputMonitorState state) {
  switch (state) {
    case InputMonitorState::kDenied: return "denied";
    case InputMonitorState::kTapCreationFailed: return "tap_creation_failed";
    case InputMonitorState::kPartialTap: return "partial_tap";
    case InputMonitorState::kDisabled: return "disabled";
    case InputMonitorState::kRestartRequired: return "restart_required";
    case InputMonitorState::kGranted: return "granted";
    case InputMonitorState::kRecovered: return "recovered";
    case InputMonitorState::kUnknown: return "unknown";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view InputMonitorGuidance(
    InputMonitorState state) {
  switch (state) {
    case InputMonitorState::kDenied:
      return "Enable Input Monitoring for this exact app in System Settings, then retry.";
    case InputMonitorState::kTapCreationFailed:
      return "Retry monitoring. If creation still fails, quit and reopen this exact app.";
    case InputMonitorState::kPartialTap:
      return "The tap cannot see keyboard events. Reauthorize this exact app, then retry.";
    case InputMonitorState::kDisabled:
      return "Monitoring was interrupted. Retry before using the shortcut.";
    case InputMonitorState::kRestartRequired:
      return "Permission changed but keyboard monitoring is still unavailable. Quit and reopen this exact app.";
    case InputMonitorState::kGranted:
      return "Input Monitoring and usable keyboard events are ready.";
    case InputMonitorState::kRecovered:
      return "Input Monitoring recovered and usable keyboard events are ready.";
    case InputMonitorState::kUnknown:
      return "Input Monitoring readiness has not been checked yet.";
  }
  return "Input Monitoring readiness has not been checked yet.";
}

[[nodiscard]] constexpr std::string_view ScreenRecordingStateName(
    ScreenRecordingState state) {
  switch (state) {
    case ScreenRecordingState::kDenied: return "denied";
    case ScreenRecordingState::kReady: return "ready";
    case ScreenRecordingState::kUnknown: return "unknown";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view ScreenRecordingGuidance(
    ScreenRecordingState state) {
  switch (state) {
    case ScreenRecordingState::kDenied:
      return "Enable Screen Recording for this exact app in System Settings, then retry capture.";
    case ScreenRecordingState::kReady:
      return "Screen Recording is ready for capture.";
    case ScreenRecordingState::kUnknown:
      return "Screen Recording readiness has not been checked yet.";
  }
  return "Screen Recording readiness has not been checked yet.";
}

[[nodiscard]] constexpr bool DeleteShortcutReady(DeleteShortcutState state) {
  return state == DeleteShortcutState::kReady;
}

[[nodiscard]] constexpr std::string_view DeleteShortcutStateName(
    DeleteShortcutState state) {
  switch (state) {
    case DeleteShortcutState::kInputUnavailable: return "input_unavailable";
    case DeleteShortcutState::kSettingsUnavailable:
      return "settings_unavailable";
    case DeleteShortcutState::kConflict: return "conflict";
    case DeleteShortcutState::kRegistrationFailed: return "registration_failed";
    case DeleteShortcutState::kSecureInput: return "secure_input";
    case DeleteShortcutState::kReady: return "ready";
    case DeleteShortcutState::kUnknown: return "unknown";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view DeleteShortcutGuidance(
    DeleteShortcutState state) {
  switch (state) {
    case DeleteShortcutState::kInputUnavailable:
      return "Restore Input Monitoring before using hold Option+D deletion.";
    case DeleteShortcutState::kSettingsUnavailable:
      return "Repair or replace the preserved settings file before using shortcuts.";
    case DeleteShortcutState::kConflict:
      return "Choose distinct capture and delete shortcuts in settings.";
    case DeleteShortcutState::kRegistrationFailed:
      return "The delete shortcut could not be reserved; change the binding or retry.";
    case DeleteShortcutState::kSecureInput:
      return "Deletion is unavailable while secure keyboard input is active.";
    case DeleteShortcutState::kReady:
      return "Hold the delete chord, hover a mark, then left-click once.";
    case DeleteShortcutState::kUnknown:
      return "Delete shortcut readiness has not been checked yet.";
  }
  return "Delete shortcut readiness has not been checked yet.";
}

// The input adapter publishes attempted OS registration outcomes, never raw
// configured settings presented as active shortcuts.
struct EffectiveShortcutBinding {
  std::uint16_t key_code = 0;
  std::uint32_t modifiers = 0;
  bool registered = false;
};
struct EffectiveShortcutSnapshot {
  EffectiveShortcutBinding capture;
  EffectiveShortcutBinding deletion;
  CaptureShortcutState capture_state = CaptureShortcutState::kUnknown;
  DeleteShortcutState delete_state = DeleteShortcutState::kUnknown;
  ScreenRecordingState screen = ScreenRecordingState::kUnknown;
};
class EffectiveShortcutPublication {
 public:
  void Capture(EffectiveShortcutBinding binding, CaptureShortcutState state) {
    std::lock_guard lock(mutex_); snapshot_.capture=binding;
    snapshot_.capture_state=state;
  }
  void Delete(EffectiveShortcutBinding binding, DeleteShortcutState state) {
    std::lock_guard lock(mutex_); snapshot_.deletion=binding;
    snapshot_.delete_state=state;
  }
  void Screen(ScreenRecordingState state) {
    std::lock_guard lock(mutex_); snapshot_.screen=state;
  }
  void Stop() { std::lock_guard lock(mutex_); snapshot_={}; }
  [[nodiscard]] EffectiveShortcutSnapshot Snapshot() const {
    std::lock_guard lock(mutex_); return snapshot_;
  }
 private:
  mutable std::mutex mutex_;
  EffectiveShortcutSnapshot snapshot_;
};
EffectiveShortcutSnapshot InputShortcutStatus();

struct PermissionReadinessState {
  std::atomic<InputMonitoringPermissionState> input_monitor_permission{
      InputMonitoringPermissionState::kUnknown};
  std::atomic<InputMonitorState> input_monitor{InputMonitorState::kUnknown};
  std::atomic<ScreenRecordingState> screen_recording{
      ScreenRecordingState::kUnknown};
  std::atomic<DeleteShortcutState> delete_shortcut{
      DeleteShortcutState::kUnknown};
  std::atomic<CaptureShortcutState> capture_shortcut{
      CaptureShortcutState::kUnknown};
  std::atomic<std::uint64_t> retry_generation{0};
  std::string app_name;
  std::string bundle_identifier;
  std::string bundle_path;
};

// Native capture hot-key callbacks arrive through Carbon while physical
// release and interruption boundaries arrive through the listen-only CG tap.
// This small adapter seam accepts only a current exact chord whose Carbon
// interval is strictly later than the last observed boundary.
class CaptureShortcutEligibility {
 public:
  void ObserveBoundary(core::DeleteEventProvenance boundary) {
    if (boundary.valid()) boundary_ = boundary;
  }

  [[nodiscard]] bool AcceptPress(
      bool physical_key_down, bool exact_modifiers,
      core::DeleteEventProvenance provenance) const {
    return physical_key_down && exact_modifiers && provenance.valid() &&
        (!boundary_.valid() || provenance.earliest_ns > boundary_.latest_ns);
  }

 private:
  core::DeleteEventProvenance boundary_;
};

// A physical hold is one intent even if focus, binding, or consent changes.
// Modifiers alone cannot rearm it; the original configured key must be released.
class CaptureShortcutHold {
 public:
  bool Begin(std::uint16_t key) {
    if (key_) return false;
    key_ = key;
    return true;
  }
  [[nodiscard]] std::optional<std::uint16_t> key() const { return key_; }
  bool ObserveRelease(bool original_key_is_down) {
    if (!key_ || original_key_is_down) return false;
    key_.reset();
    return true;
  }

 private:
  std::optional<std::uint16_t> key_;
};

[[nodiscard]] inline std::optional<core::Point2D>
AppKitGlobalPointFromQuartz(core::Point2D point, double appkit_primary_top) {
  if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
      !std::isfinite(appkit_primary_top)) {
    return {};
  }
  return core::Point2D{point.x, appkit_primary_top - point.y};
}

[[nodiscard]] inline std::optional<core::Rect> StrokeDirtyBounds(
    const std::vector<core::DisplayPoint>& samples, core::DisplayId display,
    double padding) {
  if (!std::isfinite(padding) || padding < 0) return {};
  bool found = false;
  double left = 0, right = 0, bottom = 0, top = 0;
  for (const auto& sample : samples) {
    if (sample.display_id != display || !sample.IsValid() ||
        sample.unit != core::CoordinateUnit::kLogicalPoints) {
      continue;
    }
    if (!found) {
      left = right = sample.position.x;
      bottom = top = sample.position.y;
      found = true;
    } else {
      left = std::min(left, sample.position.x);
      right = std::max(right, sample.position.x);
      bottom = std::min(bottom, sample.position.y);
      top = std::max(top, sample.position.y);
    }
  }
  if (!found) return {};
  return core::Rect{left - padding, bottom - padding,
                    right - left + 2 * padding, top - bottom + 2 * padding};
}

[[nodiscard]] constexpr double MarkStrokeWidth(bool hovered) {
  return hovered ? 7.0 : 4.0;
}

[[nodiscard]] constexpr bool OverlayAcceptsPointer(
    bool drawing, bool hit_on_panel,
    const core::DeleteGestureSnapshot& deletion) {
  return drawing ||
      (hit_on_panel &&
       (!deletion.blocks_mark_copy() || deletion.armed()));
}

[[nodiscard]] constexpr bool ReferenceJobAcceptsPointer(
    core::ReferenceJobState state) {
  return state != core::ReferenceJobState::kExpired &&
         state != core::ReferenceJobState::kDeleted;
}

[[nodiscard]] constexpr bool InspectorMarkIsSelected(
    std::string_view mark_id, std::string_view selected_id) {
  return !mark_id.empty() && mark_id == selected_id;
}

[[nodiscard]] constexpr bool InspectorSelectionChangesPresentation(
    std::string_view previous_id, std::string_view selected_id) {
  return previous_id != selected_id;
}

enum class InspectorLifecycleEvent {
  kReferenceSelection,
  kBackgroundCompletion,
  kUserClose,
  kSelectedDelete,
  kExplicitToggle,
  kExplicitShow,
};

struct InspectorLifecycleState {
  bool user_hidden = false;
  bool panel_visible = false;
  std::string selected_id;
  std::uint64_t selection_generation = 0;
};

// Session-only ownership. Selection/editor/presentation state cannot enable it.
struct AppendCaptureMode {
  std::string root;
  std::uint64_t generation=0;
  [[nodiscard]] bool enabled() const {return !root.empty();}
  bool Enable(std::string reference,bool ready) {
    if(!ready||reference.empty())return false;
    root=std::move(reference);++generation;return true;
  }
  void Disable() {if(enabled()){root.clear();++generation;}}
  void ReferenceSelected(std::string_view selected) {if(enabled()&&root!=selected)Disable();}
};
inline void CancelCaptureAttempt(core::InteractionController& interaction,
    core::MarkController& marks,std::string reason) {
  (void)interaction.Cancel(core::CancelReason::kUser);marks.Abort(std::move(reason));
}

// The native panel calls this same reducer for selection, close, background
// completion, and explicit reopen. It keeps hidden suppression and historical
// selection independent from backend completion state.
[[nodiscard]] inline InspectorLifecycleState ReduceInspectorLifecycle(
    InspectorLifecycleState state, InspectorLifecycleEvent event,
    std::string_view selected_id = {}) {
  switch (event) {
    case InspectorLifecycleEvent::kReferenceSelection:
      if (!selected_id.empty()) {
        state.selected_id = selected_id;
        ++state.selection_generation;
      }
      if (!state.user_hidden) state.panel_visible = true;
      break;
    case InspectorLifecycleEvent::kBackgroundCompletion:
      break;
    case InspectorLifecycleEvent::kUserClose:
      state.user_hidden = true;
      state.panel_visible = false;
      break;
    case InspectorLifecycleEvent::kSelectedDelete:
      state.selected_id = selected_id;
      ++state.selection_generation;
      break;
    case InspectorLifecycleEvent::kExplicitToggle:
      if (state.panel_visible) {
        state.user_hidden = true;
        state.panel_visible = false;
      } else {
        state.user_hidden = false;
        state.panel_visible = true;
      }
      break;
    case InspectorLifecycleEvent::kExplicitShow:
      state.user_hidden = false;
      state.panel_visible = true;
      break;
  }
  return state;
}

// IDs, not a transient menu row, bind the preview and every explicit action.
[[nodiscard]] inline bool InspectorReferenceAvailable(
    const std::vector<core::ReferenceJobSnapshot>& jobs, std::string_view id) {
  return std::any_of(jobs.begin(), jobs.end(), [&](const auto& job) {
    return job.id == id && job.state != core::ReferenceJobState::kDeleted;
  });
}

[[nodiscard]] inline InspectorLifecycleState InspectorAfterSuccessfulDelete(
    InspectorLifecycleState state, core::DeleteResult result,
    const std::vector<core::ReferenceJobSnapshot>& remaining) {
  if (result != core::DeleteResult::kDeleted) return state;
  const core::ReferenceJobSnapshot* newest = nullptr;
  for (const auto& job : remaining) {
    if (job.state == core::ReferenceJobState::kDeleted) continue;
    // Use the store's actual creation clock, with its stable ID ordering for ties.
    if (!newest || job.accepted_utc_us > newest->accepted_utc_us ||
        (job.accepted_utc_us == newest->accepted_utc_us && job.id > newest->id))
      newest = &job;
  }
  return ReduceInspectorLifecycle(std::move(state),
      InspectorLifecycleEvent::kSelectedDelete, newest ? newest->id : "");
}

[[nodiscard]] inline InspectorLifecycleState InspectorAfterCapture(
    InspectorLifecycleState state, std::string_view captured_id,
    const std::vector<core::ReferenceJobSnapshot>& jobs) {
  const auto captured=std::find_if(jobs.begin(),jobs.end(),[&](const auto& job) {
    return !captured_id.empty() && job.id==captured_id &&
        (job.state==core::ReferenceJobState::kPending ||
         job.state==core::ReferenceJobState::kIndexing ||
         job.state==core::ReferenceJobState::kReady);
  });
  if(captured==jobs.end())return state;
  return ReduceInspectorLifecycle(std::move(state),
      InspectorLifecycleEvent::kReferenceSelection, captured_id);
}

[[nodiscard]] inline bool InspectorPresentationIsCurrent(
    const InspectorLifecycleState& state, std::string_view id,
    std::uint64_t generation) {
  return state.selected_id == id && state.selection_generation == generation;
}

enum class InspectorDisplayAssociationState {
  kUnavailable,
  kAssociated,
  kAmbiguous,
};

struct InspectorRegionDisplayProjection {
  std::size_t region_index = 0;
  InspectorDisplayAssociationState association =
      InspectorDisplayAssociationState::kUnavailable;
  struct Display {
    core::DisplayId id = 0;
    std::string uuid;
    core::Rect logical;
    core::Rect pixels;
    double scale = 0;
  };
  // A path may cross displays. Preserve every matched historical display in
  // path order; a caller can render all of them without inventing a primary.
  std::vector<Display> displays;
  // Convenience details for the common one-display case.
  core::DisplayId display_id = 0;
  std::string display_uuid;
  core::Rect display_logical;
  core::Rect display_pixels;
  double display_scale = 0;
  core::Rect region;
  core::Rect crop_pixels;
};

struct InspectorDisplayAssociationProjection {
  std::vector<InspectorRegionDisplayProjection> regions;
};

[[nodiscard]] inline InspectorDisplayAssociationProjection
ProjectInspectorDisplayAssociation(
    const core::Context& context,
    const std::vector<core::ReferenceRegion>& regions,
    const std::vector<core::DisplayPoint>& legacy_path = {}) {
  InspectorDisplayAssociationProjection projection;
  if (regions.empty() && !legacy_path.empty()) {
    InspectorRegionDisplayProjection item;
    item.region_index = 0;
    projection.regions.push_back(std::move(item));
  } else {
    projection.regions.reserve(regions.size());
    for (std::size_t index = 0; index < regions.size(); ++index) {
      const auto& region = regions[index];
      InspectorRegionDisplayProjection item;
      item.region_index = index;
      item.region = region.region;
      item.crop_pixels = region.crop_pixels;
      projection.regions.push_back(std::move(item));
    }
  }
  if (projection.regions.empty()) return projection;
  const auto valid_rect = [](const core::Rect& rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
        std::isfinite(rect.width) && std::isfinite(rect.height) &&
        rect.width > 0 && rect.height > 0;
  };

  // A schema-1 path is the one legacy region. A path may contain more than one
  // display ID, so preserve every matched display rather than selecting the
  // first or primary display merely to make the inspector look complete.
  for (std::size_t index = 0; index < projection.regions.size(); ++index) {
    std::vector<core::DisplayPoint> path;
    if (regions.empty()) path = legacy_path;
    else core::ForEachSubpath(regions[index], [&](const auto& subpath) {
      path.insert(path.end(), subpath.begin(), subpath.end());
    });
    if (path.empty()) continue;
    bool valid_path = true;
    std::vector<core::DisplayId> display_ids;
    for (const auto& point : path) {
      if (!point.IsValid() ||
          point.unit != core::CoordinateUnit::kLogicalPoints) {
        valid_path = false;
        break;
      }
      if (std::find(display_ids.begin(), display_ids.end(), point.display_id) ==
          display_ids.end()) display_ids.push_back(point.display_id);
    }
    if (!valid_path || display_ids.empty()) continue;
    auto& result = projection.regions[index];
    bool all_matched = true;
    for (const auto display_id : display_ids) {
      const auto match = std::find_if(context.displays.begin(),
                                      context.displays.end(),
                                      [display_id](const auto& display) {
                                        return display.id == display_id;
                                      });
      const auto duplicate = match == context.displays.end()
                                 ? context.displays.end()
                                 : std::find_if(
                                       std::next(match), context.displays.end(),
                                       [display_id](const auto& display) {
                                         return display.id == display_id;
                                       });
      if (match == context.displays.end() ||
          duplicate != context.displays.end() || match->uuid.empty() ||
          match->id == 0 || match->scale <= 0 || !std::isfinite(match->scale) ||
          !valid_rect(match->logical) || !valid_rect(match->pixels) ||
          match->pixels.width != match->logical.width * match->scale ||
          match->pixels.height != match->logical.height * match->scale) {
        all_matched = false;
        break;
      }
      result.displays.push_back({match->id, match->uuid, match->logical,
                                 match->pixels, match->scale});
    }
    if (!all_matched) {
      result.displays.clear();
      continue;
    }
    for (const auto& point : path) {
      const auto match = std::find_if(result.displays.begin(),
                                      result.displays.end(),
                                      [&](const auto& display) {
                                        return display.id == point.display_id;
                                      });
      if (match == result.displays.end() || point.backing_scale != match->scale) {
        result.displays.clear();
        break;
      }
    }
    if (result.displays.empty()) continue;
    result.association = InspectorDisplayAssociationState::kAssociated;
    const auto& first = result.displays.front();
    result.display_id = first.id;
    result.display_uuid = first.uuid;
    result.display_logical = first.logical;
    result.display_pixels = first.pixels;
    result.display_scale = first.scale;
  }
  return projection;
}

[[nodiscard]] inline InspectorDisplayAssociationProjection
ProjectInspectorDisplayAssociation(const core::Reference& reference) {
  return ProjectInspectorDisplayAssociation(reference.context,
                                            core::EffectiveRegions(reference),
                                            reference.path);
}

[[nodiscard]] inline InspectorDisplayAssociationProjection
ProjectInspectorSelectedReference(const InspectorLifecycleState& lifecycle,
                                   const core::Reference& reference) {
  if (lifecycle.selected_id.empty() || lifecycle.selected_id != reference.id)
    return {};
  return ProjectInspectorDisplayAssociation(reference);
}

[[nodiscard]] constexpr std::string_view InspectorDisplayAssociationName(
    InspectorDisplayAssociationState state) {
  switch (state) {
    case InspectorDisplayAssociationState::kAssociated: return "associated";
    case InspectorDisplayAssociationState::kAmbiguous: return "ambiguous";
    case InspectorDisplayAssociationState::kUnavailable: return "unavailable";
  }
  return "unavailable";
}

enum class ReferenceClipboardTrigger {
  kDeliberatePublication,
  kAsyncCompletion,
};

struct ReferenceClipboardPlan {
  bool writes_pasteboard = false;
  bool plain_text_only = false;
  std::size_t representation_count = 0;
  bool proves_content_present = false;
};

enum class AnnotatedCopyStatus {
  kCopied,
  kInvalidPng,
  kTooLarge,
  kTimedOut,
  kPasteboardChanged,
  kPasteboardFailed,
};

struct AnnotatedCopyResult {
  AnnotatedCopyStatus status = AnnotatedCopyStatus::kInvalidPng;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool downscaled = false;
  std::optional<std::int64_t> change_count;
};

// Called on the main thread. Completion runs on the main thread exactly once.
// An empty pasteboard name selects the general pasteboard; named boards are
// intended for isolated tests. A timed-out operation cannot publish later.
void CopyAnnotatedPng(std::shared_ptr<const core::Bytes> png,
                      std::chrono::milliseconds timeout,
                      std::function<void(AnnotatedCopyResult)> completion,
                      std::string pasteboard_name = {});

[[nodiscard]] constexpr ReferenceClipboardPlan PlanReferenceClipboardWrite(
    ReferenceClipboardTrigger trigger) {
  if (trigger == ReferenceClipboardTrigger::kDeliberatePublication) {
    return {true, true, 1, true};
  }
  return {};
}

// Passive input observations provide continuity only for events we actually see.
enum class ViewportInput { kHealthy, kInterrupted, kScroll, kPointer, kKeyboard, kPrimaryDown };
// Tracks the physical release tail independently of the drawing state. Carbon
// may finish the transaction before the listen-only tap sees either release.
class CaptureReleaseContinuity {
 public:
  void Begin(std::uint64_t modifiers) {active_=true;key_pending_=true;required_=last_=modifiers;}
  bool Observe(bool capture_key,bool key_up,bool flags_changed,std::uint64_t modifiers) {
    if(!active_)return false;
    if(flags_changed) {
      if((modifiers&~last_)!=0){active_=false;return false;}
      last_=modifiers;
      if(!key_pending_&&last_==0)active_=false;
      return true;
    }
    if(capture_key&&key_up&&key_pending_) {
      key_pending_=false;
      if(modifiers==0)active_=false;
      return true;
    }
    if(capture_key&&!key_up&&key_pending_&&modifiers==required_)return true;
    active_=false;return false;
  }
 private:
  bool active_=false,key_pending_=false;
  std::uint64_t required_=0,last_=0;
};
struct CapturePrefixReadiness {
  bool running=false,settings_usable=false,registered=false,handler_ready=false;
  bool monitor_ready=false,screen_ready=false,shortcut_ready=false,conflict=false,secure_input=false;
  bool native_binding_matches=false,pending=false;
  std::uint16_t configured_key=0,registered_key=0;
  std::uint32_t configured_modifiers=0,registered_modifiers=0;
  std::uint64_t registration_generation=0,prefix_generation=0;
};
[[nodiscard]] inline bool CapturePrefixReady(const CapturePrefixReadiness& state) {
  return state.running&&state.settings_usable&&state.registered&&state.handler_ready&&
      state.monitor_ready&&state.screen_ready&&state.shortcut_ready&&!state.conflict&&!state.secure_input&&
      state.native_binding_matches&&state.configured_key==state.registered_key&&
      state.configured_modifiers==state.registered_modifiers&&state.registration_generation!=0&&
      (!state.pending||state.prefix_generation==state.registration_generation);
}
class CapturePrefixReadinessLatch {
 public:
  // The caller publishes epoch invalidation immediately on loss, without waiting
  // for another input or the presentation deadline.
  bool Observe(bool ready,bool pending) {
    const bool retire=!ready&&(ready_||pending);ready_=ready;return retire;
  }
 private:
  bool ready_=false;
};
enum class CapturePrefixAction { kNone,kStarted,kPending,kConfirmed,kRetired };
// This deadline controls presentation carry only, never physical hotkey entry.
class CapturePrefixContinuity {
 public:
  static constexpr std::int64_t kDeadlineMs=500;
  CapturePrefixAction Modifiers(std::uint64_t modifiers,std::uint64_t required,std::int64_t now,
      bool ready=true) {
    if(!ready)return Retire();
    if(pending_) {
      if(now<began_||now-began_>=kDeadlineMs||required!=required_||
         modifiers==0||(modifiers&~required_)!=0||(modifiers&last_)!=last_)return Retire();
      last_=modifiers;return CapturePrefixAction::kPending;
    }
    if(modifiers==0){blocked_=false;return CapturePrefixAction::kNone;}
    if(blocked_||required==0||(modifiers&~required)!=0)return CapturePrefixAction::kNone;
    pending_=true;required_=required;last_=modifiers;began_=now;return CapturePrefixAction::kStarted;
  }
  CapturePrefixAction Confirm(std::int64_t now) {
    if(!pending_)return CapturePrefixAction::kNone;
    if(now<began_||now-began_>=kDeadlineMs)return Retire();
    pending_=false;blocked_=true;return CapturePrefixAction::kConfirmed;
  }
  CapturePrefixAction Tick(std::int64_t now) {
    return pending_&&(now<began_||now-began_>=kDeadlineMs)?Retire():CapturePrefixAction::kNone;
  }
  CapturePrefixAction Retire() {
    const bool active=pending_;pending_=false;blocked_=true;
    return active?CapturePrefixAction::kRetired:CapturePrefixAction::kNone;
  }
  void Released(std::uint64_t modifiers) {if(!pending_&&modifiers==0)blocked_=false;}
  [[nodiscard]] bool PhysicalModifiersValid(std::uint64_t modifiers) const {
    return pending_&&modifiers!=0&&(modifiers&~required_)==0&&(modifiers&last_)==last_;
  }
  [[nodiscard]] bool pending() const {return pending_;}
 private:
  bool pending_=false,blocked_=false;
  std::uint64_t required_=0,last_=0;
  std::int64_t began_=0;
};
// Source-bound passive ingress: native callbacks and the owned component
// selector execute this same readiness/order/action composition.
class CapturePrefixIngress {
 public:
  struct Actions {
    std::function<bool()> begin;
    std::function<void()> confirm,cancel,retire_epoch,started;
  };
  void SetActions(Actions actions) {actions_=std::move(actions);}
  bool Readiness(CapturePrefixReadiness state) {
    state.pending=prefix_.pending();state.prefix_generation=generation_;
    const bool ready=CapturePrefixReady(state);
    if(readiness_.Observe(ready,state.pending)) {
      Invalidate();if(actions_.retire_epoch)actions_.retire_epoch();
    }
    return ready;
  }
  void LoseReadiness() {
    const bool retire=readiness_.Observe(false,prefix_.pending());
    Invalidate();if(retire&&actions_.retire_epoch)actions_.retire_epoch();
  }
  bool ModifierEvent(CapturePrefixReadiness state,std::uint64_t modifiers,
      std::uint64_t required,std::int64_t now) {
    const bool ready=Readiness(state);
    const auto action=prefix_.Modifiers(modifiers,required,now,ready);
    if(action==CapturePrefixAction::kStarted) {
      generation_=state.registration_generation;
      if(actions_.begin&&actions_.begin()) {
        if(actions_.started)actions_.started();return true;
      }
      Invalidate();return false;
    }
    if(action==CapturePrefixAction::kRetired&&actions_.cancel)actions_.cancel();
    return action==CapturePrefixAction::kPending;
  }
  void Confirm(CapturePrefixReadiness state,std::int64_t now) {
    if(!Readiness(state))return;
    const auto action=prefix_.Confirm(now);
    if(action==CapturePrefixAction::kConfirmed&&actions_.confirm)actions_.confirm();
    else if(action==CapturePrefixAction::kRetired&&actions_.cancel)actions_.cancel();
  }
  void Invalidate() {
    if(prefix_.Retire()==CapturePrefixAction::kRetired&&actions_.cancel)actions_.cancel();
  }
  void Tick(std::int64_t now) {
    if(prefix_.Tick(now)==CapturePrefixAction::kRetired&&actions_.cancel)actions_.cancel();
  }
  void Released(std::uint64_t modifiers) {prefix_.Released(modifiers);}
  [[nodiscard]] bool pending() const {return prefix_.pending();}
  [[nodiscard]] bool PhysicalModifiersValid(std::uint64_t modifiers) const {return prefix_.PhysicalModifiersValid(modifiers);}
 private:
  CapturePrefixContinuity prefix_;
  CapturePrefixReadinessLatch readiness_;
  std::uint64_t generation_=0;
  Actions actions_;
};
class ViewportContinuity {
 public:
  void Observe(ViewportInput event,bool positively_owned=false) {
    if(event==ViewportInput::kHealthy){healthy_=true;return;}
    if(event==ViewportInput::kInterrupted){healthy_=false;++epoch_;return;}
    if(!positively_owned)++epoch_;
  }
  [[nodiscard]] std::uint64_t epoch() const {return epoch_;}
  [[nodiscard]] bool healthy() const {return healthy_;}
 private:
  std::uint64_t epoch_=1;bool healthy_=false;
};
inline core::MarkNotesSnapshot MarksForShot(core::MarkNotesSnapshot snapshot,
                                           std::string_view shot) {
  std::erase_if(snapshot.marks,[&](const auto& mark){return mark.shot_id!=shot;});
  return snapshot;
}
class OverlayAdapter {
 public:
  virtual ~OverlayAdapter() = default;

  virtual bool BeginCapturePrefix() {return false;}
  virtual void ConfirmCapturePrefix() {}
  virtual void CancelCapturePrefix() {}
  virtual void ObserveViewportInput(ViewportInput,
      const std::optional<core::DisplayPoint>& = {}) {}
  virtual void Refresh() = 0;
  virtual void RebuildDisplays() = 0;
  virtual void UpdatePointerPolicy(
      const std::optional<core::DisplayPoint>& pointer) = 0;
  [[nodiscard]] virtual std::optional<core::DisplayPoint>
  DisplayPointAtGlobalLogical(core::Point2D global_point) const = 0;
  virtual void ToggleInspector() = 0;
  [[nodiscard]] virtual bool InspectorVisible() const = 0;
};

class InputAdapter {
 public:
  virtual ~InputAdapter() = default;
  [[nodiscard]] virtual bool Start() = 0;
  virtual void RetryInputMonitoring() = 0;
  virtual void Stop() = 0;
};

[[nodiscard]] constexpr Backend CurrentBackend() {
#if defined(__APPLE__) && defined(__MACH__)
  return Backend::kMacOS;
#elif defined(_WIN32)
  return Backend::kWindowsUnavailable;
#elif defined(__linux__)
  return Backend::kLinuxUnavailable;
#else
  return Backend::kUnsupported;
#endif
}
[[nodiscard]] std::unique_ptr<OverlayAdapter> MakeOverlayAdapter(
    core::InteractionController* controller, core::ReferenceStore* references,
    core::SettingsStore* settings, service::ReferenceServer* server,
    core::DeleteGestureController* deletion);
[[nodiscard]] std::unique_ptr<InputAdapter> MakeInputAdapter(
    core::InteractionController* controller, OverlayAdapter* overlay,
    core::SettingsStore* settings,
    std::shared_ptr<PermissionReadinessState> readiness,
    core::DeleteGestureController* deletion);
[[nodiscard]] bool ScreenRecordingReady();
int RunApplication();

// Chrome page acquisition is an adapter seam. The caller supplies an
// Apple-Events implementation; this layer only validates/redacts its result.
struct ChromePageSample {
  core::PageAvailability availability = core::PageAvailability::kUnavailable;
  std::int64_t browser_pid = 0;
  std::uint64_t window_id = 0;
  core::Rect window_bounds;
  std::int64_t process_start_identity_us = 0;
  std::string opaque_tab_id;
  // Provider input: identity uses its digest; bounded display metadata is separate.
  std::string url;
  // A fresh provider/native association, never a browser session ID used as a
  // CGWindowID. Titles used to establish it remain private to the native call.
  std::optional<core::WindowObservation> content_window;
};
// One current-application metadata sample. The sources are never interchangeable.
struct ProcessIdentitySample {
  std::int64_t launch_start_identity_us=0,kernel_start_identity_us=0;
  bool coherent=false;
};
struct KernelProcessIdentityRecord {
  int returned_bytes=0;
  std::int64_t pid=0;
  std::uint64_t seconds=0,microseconds=0;
};
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
ProcessIdentitySample ReadProcessIdentityForTesting(const void* before,const void* after,
    std::function<KernelProcessIdentityRecord(std::int64_t)> read,core::Stamp observed);
#endif
// Select once from evidence already available before drawing. No native work,
// permission request, waiting, or later callback may refine this shot's scope.
inline std::optional<core::Context> FreezeCaptureScope(core::Context context,
    core::DisplayPoint initial,const core::WindowObservation& source,
    const core::PageObservation& page,bool automation_granted) {
  if(!core::ValidCaptureScope(context)||!context.scope.policy_present ||
     context.scope.display_id!=initial.display_id)return {};
  if(context.bundle_id!="com.google.Chrome")return context;
  const auto display=std::find_if(context.displays.begin(),context.displays.end(),
      [&](const auto& d){return d.id==initial.display_id;});
  if(display==context.displays.end()||!initial.IsValid()||initial.unit!=core::CoordinateUnit::kLogicalPoints||
     initial.backing_scale!=display->scale||initial.position.x<0||initial.position.y<0||
     initial.position.x>display->logical.width||initial.position.y>display->logical.height)return {};
  const auto x=display->logical.x+initial.position.x,y=display->logical.y+initial.position.y;
  if(automation_granted && context.scope.version==1 && context.window_id!=0 && source.browser_content && core::WindowObservationMatchesContext(context,source) && source.pid==context.pid && source.bundle_id==context.bundle_id &&
     source.native_window.process_start_identity_us==context.scope.process_start_identity_us &&
     x>=source.bounds.x && y>=source.bounds.y &&
     x<=source.bounds.x+source.bounds.width && y<=source.bounds.y+source.bounds.height) {
    auto browser=context;browser.window_id=source.window_id;browser.window_pid=source.pid;
    browser.window=source.bounds;browser.window_title.clear();browser.native_window=source.native_window;
    browser.selection_method="frontmost-pid/topmost-normal-containing-initial-point";
    browser.scope.kind=core::CaptureScopeKind::kWindow;
    if(core::WindowObservationMatchesContext(browser,source)) {
      if(automation_granted && page.identity && core::PageIdentityVisible(page.identity,page) &&
         core::PageIdentityMatchesContext(*page.identity,browser))browser.scope.kind=core::CaptureScopeKind::kPage;
      return browser;
    }
  }
  // A point-selected native window is usable as window scope only when the
  // current foreground tuple independently agrees. A different CG candidate
  // may be auxiliary UI, but there is no parent evidence here to assume it.
  if(context.window_id!=0&&core::WindowObservationMatchesContext(context,source)) {
    context.scope.kind=core::CaptureScopeKind::kWindow;return context;
  }
  context.window_id=0;context.window_pid=0;context.window={};context.window_title.clear();
  context.native_window={};context.title={1,core::TitleAvailability::kUnavailable};
  context.selection_method="frontmost-application/selected-display";
  context.scope.kind=core::CaptureScopeKind::kApplication;context.scope.title_required=false;
  return core::ValidCaptureScope(context)?std::optional<core::Context>(context):std::nullopt;
}
inline std::optional<std::int64_t> ChromeProcessStartIdentity(
    double launch_unix_seconds) {
  if (!std::isfinite(launch_unix_seconds) || launch_unix_seconds<=0)
    return {};
  const long double micros=static_cast<long double>(launch_unix_seconds)*1'000'000.0L;
  const long double maximum=static_cast<long double>(
      std::numeric_limits<std::int64_t>::max());
  if (!std::isfinite(micros) || micros<=0 || micros>maximum)return {};
  const auto identity=static_cast<std::int64_t>(micros);
  return identity>0?std::optional<std::int64_t>(identity):std::nullopt;
}
struct ChromeWindowCandidate {
  std::int64_t pid = 0;
  std::uint64_t window_id = 0;
  core::Rect bounds;
  std::int64_t z_order = 0;
  bool eligible = true;
};
inline std::string ChromeNavigationDigest(std::string_view url) {
  static constexpr std::string_view domain="seethis/chrome-navigation/v1\0";
  core::Bytes input(domain.begin(),domain.end());
  input.insert(input.end(),url.begin(),url.end());
  return core::Sha256(input);
}
inline std::optional<core::PageIdentity> MakeChromePageIdentity(
    const ChromePageSample& sample) {
  if (sample.availability!=core::PageAvailability::kReady || sample.url.empty())
    return {};
  core::PageIdentity identity;
  identity.provider_version=2;
  identity.browser_pid=sample.browser_pid;
  identity.window_id=sample.window_id;
  identity.window_bounds=sample.window_bounds;
  identity.process_start_identity_us=sample.process_start_identity_us;
  identity.opaque_tab_id=sample.opaque_tab_id;
  identity.navigation_digest=ChromeNavigationDigest(sample.url);
  if (!core::ValidPageIdentity(identity)) return {};
  return identity;
}
inline core::CapturedPageMetadata MakeChromeCapturedPageMetadata(
    const ChromePageSample& sample, core::Stamp observed) {
  core::CapturedPageMetadata metadata;
  metadata.availability = core::SourceFieldAvailability::kInvalidOrOverLimit;
  if (core::ValidSourceContextText(sample.url, 16384) && observed.utc_us > 0) {
    metadata.url = sample.url;
    metadata.availability = core::SourceFieldAvailability::kCaptured;
    metadata.observed_utc_us = observed.utc_us;
  }
  return metadata;
}
inline bool ChromeWindowMatches(const core::Context& captured,
                                const ChromeWindowCandidate& candidate) {
  return candidate.eligible && candidate.pid==captured.window_pid &&
      candidate.window_id==captured.window_id &&
      candidate.bounds.x==captured.window.x &&
      candidate.bounds.y==captured.window.y &&
      candidate.bounds.width==captured.window.width &&
      candidate.bounds.height==captured.window.height;
}
inline std::optional<ChromeWindowCandidate> CorrelateChromeWindow(
    const core::Context& captured,
    const std::vector<ChromeWindowCandidate>& candidates) {
  std::optional<ChromeWindowCandidate> match;
  for (const auto& candidate : candidates) {
    if (!ChromeWindowMatches(captured,candidate)) continue;
    if (match) return {}; // ambiguous correlation fails closed
    match=candidate;
  }
  return match;
}
// One process-wide admission gate covers permission, consent and provider work.
// A lease belongs to the physical operation, never its timeout/publication latch.
class ChromePhysicalWorkGate {
 public:
  static constexpr unsigned kCapacity = 2;
  [[nodiscard]] bool TryEnter() {
    auto count = admitted_.load();
    while (count < kCapacity) {
      if (admitted_.compare_exchange_weak(count, count + 1)) return true;
    }
    return false;
  }
  void Leave() { admitted_.fetch_sub(1); }
  [[nodiscard]] unsigned admitted() const { return admitted_.load(); }
 private:
  std::atomic<unsigned> admitted_{0};
};
#if defined(SEETHIS_CAPTURE_TESTING) || defined(SEETHIS_CHROME_CONSENT_TESTING)
unsigned ChromePhysicalWorkCountForTesting();
#endif

using ChromePageAcquire = std::function<ChromePageSample()>;
using ChromePageCompletion = std::function<void(core::PageObservation)>;
struct ChromeContentObservation {
  core::WindowObservation window;
  core::PageObservation page;
};
using ChromeContentCompletion = std::function<void(ChromeContentObservation)>;

// Coordinator-lifetime identity for one asynchronous provider request.  The
// epoch invalidates every outstanding callback when observation stops or the
// foreground window changes; generation is never reset, so an old result can
// never become current again after a later start.
struct ChromeObservationRequest {
  std::uint64_t epoch = 0;
  std::uint64_t generation = 0;

  [[nodiscard]] bool valid() const { return epoch != 0 && generation != 0; }
  auto operator<=>(const ChromeObservationRequest&) const = default;
};

class ChromeObservationLifecycle {
 public:
  [[nodiscard]] ChromeObservationRequest BeginRequest() {
    active_ = {epoch_, ++generation_};
    return active_;
  }
  [[nodiscard]] std::uint64_t Invalidate() {
    ++epoch_;
    active_ = {};
    return ++generation_;
  }
  [[nodiscard]] bool Accepts(ChromeObservationRequest request) const {
    return request.valid() && request == active_;
  }
  void Complete(ChromeObservationRequest request) {
    if (Accepts(request)) active_ = {};
  }
  [[nodiscard]] std::uint64_t generation() const { return generation_; }
  [[nodiscard]] std::uint64_t epoch() const { return epoch_; }
  [[nodiscard]] bool pending() const { return active_.valid(); }

 private:
  std::uint64_t epoch_ = 1;
  std::uint64_t generation_ = 0;
  ChromeObservationRequest active_;
};

// Main-thread entry. Shared physical capacity is two, with no waiting queue.
// Timeout delivers once but retains physical admission until native work exits.
// Busy rejection delivers unavailable inline on main. Off-main entry is invalid
// and returns without native work or a callback; it never enqueues main retries.
void AcquireChromePageAsync(ChromePageAcquire acquire,
                            std::uint64_t generation,
                            ChromePageCompletion completion);
// Same admission/timeout contract; publishes a matched window/page envelope.
void AcquireChromeContentAsync(ChromePageAcquire acquire,
                               std::uint64_t generation,
                               ChromeContentCompletion completion);
// Main-thread cached result plus asynchronous refresh; never native IPC here.
// Off-main use fails closed with false.
bool ChromeAutomationPreflight(std::int64_t browser_pid);
// Connection readiness is separate from a mark's frozen foreground identity.
// These adapters never publish a URL or a PageIdentity from a background probe.
enum class ChromeConsent { kUnknown, kGranted, kDenied, kNotRequested, kUnavailable };
enum class ChromeProviderState {
  kUnknown, kChecking, kReady, kNoPage, kDenied, kAmbiguous, kTimedOut, kFailed,
};
struct ChromeRunningTarget {
  std::int64_t pid = 0;
  std::int64_t process_start_identity_us = 0;
  auto operator<=>(const ChromeRunningTarget&) const = default;
  [[nodiscard]] bool valid() const {
    return pid > 0 && process_start_identity_us > 0;
  }
};
struct ChromePermissionResult {
  ChromeConsent consent = ChromeConsent::kUnknown;
  std::int32_t os_status = 0;
};
[[nodiscard]] inline ChromePermissionResult ChromePermissionFromOSStatus(std::int32_t status) {
  // Apple AE OSStatus values; unexpected errors are not evidence of denial.
  return {status == 0 ? ChromeConsent::kGranted :
          status == -1743 ? ChromeConsent::kDenied :
          status == -1744 ? ChromeConsent::kNotRequested :
          status == -600 ? ChromeConsent::kUnavailable : ChromeConsent::kUnknown,
          status};
}
struct ChromeConnectionSnapshot {
  ChromeRunningTarget target;
  std::uint64_t authorization_generation = 0;
  ChromePermissionResult permission;
  ChromeProviderState provider = ChromeProviderState::kUnknown;
  bool observation_active = false;
  core::PageAvailability page = core::PageAvailability::kUnavailable;
  bool settings_open_failed = false;
  bool consent_pending = false;
};
[[nodiscard]] inline bool ChromeConnectionHealthy(const ChromeConnectionSnapshot& state) {
  return state.target.valid() && state.permission.consent==ChromeConsent::kGranted &&
      (state.provider==ChromeProviderState::kReady ||
       state.provider==ChromeProviderState::kNoPage);
}
// Presentation alone depends on the actual current Chrome context. Provider
// failure is not permission denial, and historical references cannot activate it.
[[nodiscard]] inline bool ChromePermissionRecoveryVisible(
    const ChromeConnectionSnapshot& state, std::int64_t foreground_chrome_pid) {
  return state.target.valid() && foreground_chrome_pid==state.target.pid &&
      (state.permission.consent==ChromeConsent::kDenied ||
       state.permission.consent==ChromeConsent::kNotRequested ||
       (state.consent_pending && state.permission.consent!=ChromeConsent::kGranted));
}
struct ChromeConnectionAdapters {
  std::function<ChromeRunningTarget()> discover;
  std::function<ChromePermissionResult(std::int64_t, bool)> permission;
};
struct ChromeConnectionProbe {
  ChromeRunningTarget target;
  std::uint64_t generation = 0;
  auto operator<=>(const ChromeConnectionProbe&) const = default;
};
// This token owns publication eligibility, not cancellation of the OS call.
struct ChromeConsentRequest {
  ChromeRunningTarget target;
  std::uint64_t request_id = 0;
  std::uint64_t target_epoch = 0;
  std::uint64_t lifecycle_epoch = 0;
  // Invalidation can prevent queued entry, never cancel an OS call in progress.
  std::shared_ptr<std::atomic_bool> native_entry_allowed;
  auto operator<=>(const ChromeConsentRequest&) const = default;
  [[nodiscard]] bool valid() const { return request_id != 0 && target.valid(); }
};
class ChromeConnectionController {
 public:
  void Refresh(const ChromeConnectionAdapters& adapters) {
    if (stopped_) return;
    const auto target = adapters.discover ? adapters.discover() : ChromeRunningTarget{};
    if (target != snapshot_.target) {
      if (consent_.native_entry_allowed) consent_.native_entry_allowed->store(false);
      ++target_epoch_;
      ++generation_; active_ = {}; snapshot_ = {}; snapshot_.target = target;
      snapshot_.consent_pending = consent_.valid();
      snapshot_.authorization_generation = ++authorization_generation_;
    }
    const auto permission = target.valid() && adapters.permission
        ? adapters.permission(target.pid, false) : ChromePermissionResult{};
    if (permission.consent != snapshot_.permission.consent) {
      ++generation_; active_ = {};
      snapshot_.authorization_generation = ++authorization_generation_;
      snapshot_.provider = ChromeProviderState::kUnknown;
      snapshot_.page = core::PageAvailability::kUnavailable;
      snapshot_.observation_active = false;
    }
    snapshot_.permission = permission;
  }
  [[nodiscard]] ChromeConsentRequest BeginConsent(
      const ChromeConnectionAdapters& adapters,
      std::optional<ChromeRunningTarget> required_target = {}) {
    Refresh(adapters);
    if (stopped_ || consent_.valid() ||
        (required_target && (!required_target->valid() ||
                             *required_target != snapshot_.target)))
      return {};
    snapshot_.settings_open_failed = false;
    if (!snapshot_.target.valid() || !adapters.permission ||
        snapshot_.permission.consent == ChromeConsent::kGranted) return {};
    ++generation_; active_ = {};
    snapshot_.authorization_generation = ++authorization_generation_;
    snapshot_.provider = ChromeProviderState::kUnknown;
    snapshot_.page = core::PageAvailability::kUnavailable;
    snapshot_.observation_active = false;
    consent_ = {snapshot_.target, ++consent_request_id_, target_epoch_, lifecycle_epoch_,
                std::make_shared<std::atomic_bool>(true)};
    snapshot_.consent_pending = true;
    return consent_;
  }
  // Caller refreshes current target/permission on main before completion. Fresh
  // preflight wins a conflicting native result (for example, grant then revoke).
  bool CompleteConsent(ChromeConsentRequest request,
                       std::optional<ChromePermissionResult> result) {
    if (!request.valid() || request != consent_) return false;
    consent_ = {};
    snapshot_.consent_pending = false;
    if (stopped_ || request.lifecycle_epoch != lifecycle_epoch_ ||
        request.target_epoch != target_epoch_ || request.target != snapshot_.target ||
        !result) return false;
    if (result->consent == snapshot_.permission.consent)
      snapshot_.permission = *result;
    return true;
  }
  void Start() {
    if (consent_.native_entry_allowed) consent_.native_entry_allowed->store(false);
    stopped_ = false;
    ++lifecycle_epoch_; ++target_epoch_; ++generation_; active_ = {};
    snapshot_ = {};
    snapshot_.authorization_generation = ++authorization_generation_;
    snapshot_.consent_pending = consent_.valid();
  }
  void Stop() {
    if (consent_.native_entry_allowed) consent_.native_entry_allowed->store(false);
    stopped_ = true;
    ++lifecycle_epoch_; ++target_epoch_; ++generation_; active_ = {};
    snapshot_ = {};
    snapshot_.authorization_generation = ++authorization_generation_;
    // Keep consent_ until the actual native call returns, including across Start.
  }
  [[nodiscard]] ChromeConnectionProbe BeginProbe() {
    snapshot_.settings_open_failed = false;
    if (stopped_ || consent_.valid() || !snapshot_.target.valid() ||
        snapshot_.permission.consent != ChromeConsent::kGranted) return {};
    active_ = {snapshot_.target, ++generation_};
    snapshot_.provider = ChromeProviderState::kChecking;
    return active_;
  }
  bool CompleteProbe(ChromeConnectionProbe probe, ChromeProviderState state) {
    if (!probe.generation || probe != active_ ||
        probe.target != snapshot_.target ||
        snapshot_.permission.consent != ChromeConsent::kGranted) return false;
    active_ = {}; snapshot_.provider = state; return true;
  }
  void Observe(std::int64_t pid, bool active, core::PageAvailability page,
               bool provider_result = false) {
    snapshot_.observation_active = active && pid == snapshot_.target.pid &&
        snapshot_.target.valid() &&
        snapshot_.permission.consent == ChromeConsent::kGranted;
    snapshot_.page = snapshot_.observation_active
        ? page : core::PageAvailability::kUnavailable;
    if (provider_result && snapshot_.observation_active && !active_.generation) {
      snapshot_.provider = page == core::PageAvailability::kReady ? ChromeProviderState::kReady :
          page == core::PageAvailability::kDenied ? ChromeProviderState::kDenied :
          page == core::PageAvailability::kAmbiguous ? ChromeProviderState::kAmbiguous :
          page == core::PageAvailability::kTimedOut ? ChromeProviderState::kTimedOut :
          ChromeProviderState::kFailed;
    }
  }
  void SettingsOpened(bool opened) { snapshot_.settings_open_failed = !opened; }
  [[nodiscard]] bool stopped() const { return stopped_; }
  [[nodiscard]] const ChromeConnectionSnapshot& snapshot() const { return snapshot_; }
 private:
  ChromeConnectionSnapshot snapshot_;
  ChromeConnectionProbe active_;
  std::uint64_t generation_ = 0;
  std::uint64_t authorization_generation_ = 0;
  ChromeConsentRequest consent_;
  std::uint64_t consent_request_id_ = 0;
  std::uint64_t target_epoch_ = 0;
  std::uint64_t lifecycle_epoch_ = 1;
  bool stopped_ = false;
};
[[nodiscard]] inline std::string_view ChromeConsentName(ChromeConsent state) {
  switch (state) {
    case ChromeConsent::kGranted: return "granted";
    case ChromeConsent::kDenied: return "denied or revoked";
    case ChromeConsent::kNotRequested: return "not requested";
    case ChromeConsent::kUnavailable: return "unavailable";
    case ChromeConsent::kUnknown: return "unknown";
  }
  return "unknown";
}
[[nodiscard]] inline std::string_view ChromeRunningName(ChromeRunningTarget target) {
  return target.pid<=0?"not running":target.valid()?"running":"identity unavailable";
}
[[nodiscard]] inline std::string_view ChromeProviderName(ChromeProviderState state) {
  switch (state) {
    case ChromeProviderState::kChecking: return "checking";
    case ChromeProviderState::kReady: return "available";
    case ChromeProviderState::kNoPage: return "no page";
    case ChromeProviderState::kDenied: return "denied";
    case ChromeProviderState::kAmbiguous: return "ambiguous";
    case ChromeProviderState::kTimedOut: return "timed out";
    case ChromeProviderState::kFailed: return "failed";
    case ChromeProviderState::kUnknown: return "not checked";
  }
  return "unknown";
}
[[nodiscard]] inline std::string_view ChromePageReadinessName(core::PageAvailability state) {
  switch (state) {
    case core::PageAvailability::kReady: return "ready";
    case core::PageAvailability::kDenied: return "denied";
    case core::PageAvailability::kAmbiguous: return "ambiguous";
    case core::PageAvailability::kTimedOut: return "timed out";
    case core::PageAvailability::kExpired: return "expired";
    case core::PageAvailability::kUnavailable: return "not ready";
  }
  return "unknown";
}
[[nodiscard]] inline std::string_view ChromeRecoveryGuidance(const ChromeConnectionSnapshot& state) {
  if (state.consent_pending)
    return "An Automation request is in progress. Respond to the macOS permission dialog; another request will wait until it returns.";
  if (state.settings_open_failed)
    return "Could not open Settings. Open System Settings > Privacy & Security > Automation > SeeThis > Google Chrome, then choose Retry Chrome.";
  if (state.target.pid<=0) return "Open Google Chrome, then choose Connect Chrome.";
  if (!state.target.valid()) return "Chrome process identity is unavailable. Choose Retry Chrome after Chrome finishes opening.";
  if (state.permission.consent == ChromeConsent::kDenied ||
      state.provider == ChromeProviderState::kDenied)
    return "Enable SeeThis > Google Chrome in System Settings > Privacy & Security > Automation, then choose Retry Chrome. A new permission prompt is not guaranteed.";
  if (state.permission.consent != ChromeConsent::kGranted)
    return "Choose Connect Chrome to request Automation access. If access stays unavailable, check Automation settings and retry.";
  if (state.provider == ChromeProviderState::kNoPage)
    return "Open a page in Google Chrome, then choose Retry Chrome.";
  if (state.provider == ChromeProviderState::kTimedOut ||
      state.provider == ChromeProviderState::kFailed ||
      state.provider == ChromeProviderState::kAmbiguous)
    return "Chrome page check did not succeed. Choose Retry Chrome; a mark still needs a current foreground Chrome page.";
  return "Automation access alone does not identify a page. Choose Retry Chrome to check the provider; make Chrome frontmost to create a mark.";
}
// Main-thread connection state shared by the menu and Inspector. Refresh is
// prompt-free and asynchronous; its current snapshot may be unavailable while
// the off-main check runs. Explicit Connect/eligible physical intents may ask.
ChromeConnectionSnapshot RefreshChromeConnection();
ChromeConnectionSnapshot ChromeConnectionStatus();
// The active input adapter interrupts any owned gesture on explicit Connect.
// Registration has no native work and cannot start/replay input.
void SetChromeConsentInterruptionHandler(const void* owner,std::function<void()> interrupt);
void ClearChromeConsentInterruptionHandler(const void* owner);
void ConnectChromeExplicit();
// Legacy target discovery for explicit authorization actions. Basic capture
// does not call it or request optional Automation permission.
std::optional<ChromeRunningTarget> GetChromeCaptureConsentTarget();
void ConnectChromeForCaptureExplicit(ChromeRunningTarget foreground_target);
void StartChromeConnection();
void StopChromeConnection();
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
// Only external inputs are injected; production native/main scheduling is used.
struct ChromeConnectionTestAdapters {
  ChromeConnectionAdapters connection;
  std::function<ChromeProviderState(ChromeRunningTarget)> provider;
  std::function<ChromeRunningTarget()> foreground_chrome;
  std::function<bool(core::WindowObservation&)> confirm_content;
  std::function<ChromePageSample(const core::WindowObservation&)> acquire_content;
};
void SetChromeConnectionTestAdapters(ChromeConnectionTestAdapters adapters);
#endif
void RetryChromeExplicit();
bool OpenChromeAutomationSettingsExplicit();
void PublishChromeObservation(std::int64_t pid, bool active,
                              core::PageAvailability page,
                              bool provider_result = false);
ChromePageSample AcquireCurrentChromePage(const core::Context& captured);
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
core::WindowObservation ObserveForegroundWindow(std::uint64_t generation);
// Exact native revalidation for a source whose current point destination and
// fresh page evidence the caller has already proved. Never selects a window.
core::WindowObservation ObserveChromeContentSource(const core::Context& source,
                                                  std::uint64_t generation);
// Presentation-only exact original-source observation. Caller proves positive
// own Inspector/note ownership at request and completion; never a capture API.
core::WindowObservation ObserveOwnUIPresentationSource(
    const core::Context& source,std::uint64_t generation);
ChromePageSample AcquireOwnUIPresentationChromePage(const core::Context& source);
ChromePageSample AcquireCurrentChromePage(
    const core::WindowObservation& foreground);
ChromePageSample AcquireCurrentChromeContent(
    const core::WindowObservation& foreground);
#endif

}  // namespace seethis::platform
