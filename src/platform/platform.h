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

[[nodiscard]] constexpr std::string_view PermissionStartupStatus(
    InputMonitorState input, ScreenRecordingState screen) {
  const bool input_ready = InputMonitorReady(input);
  const bool screen_ready = ScreenRecordingReady(screen);
  if (input_ready && screen_ready) return "SeeThis";
  if (!input_ready && !screen_ready) return "SeeThis — permissions not ready";
  if (!input_ready) return "SeeThis — Input Monitoring not ready";
  return "SeeThis — Screen Recording not ready";
}

enum class PermissionEntryTrigger {
  kLaunch,
  kPermissionRefresh,
  kExplicitOpen,
};

// Permission polling must never reopen a dismissed entry window. Opening the
// app again is an explicit request even when capture permissions are ready.
[[nodiscard]] constexpr bool PermissionEntryShouldPresent(
    PermissionEntryTrigger trigger, InputMonitorState input,
    ScreenRecordingState screen) {
  if (trigger == PermissionEntryTrigger::kExplicitOpen) return true;
  if (trigger == PermissionEntryTrigger::kPermissionRefresh) return false;
  return !InputMonitorReady(input) || !ScreenRecordingReady(screen);
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

class OverlayAdapter {
 public:
  virtual ~OverlayAdapter() = default;

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
  // Ephemeral input only. It is immediately reduced to navigation_digest.
  std::string url;
};
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
using ChromePageAcquire = std::function<ChromePageSample()>;
using ChromePageCompletion = std::function<void(core::PageObservation)>;

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

// Implemented by the macOS adapter with bounded background dispatch. The
// callback is delivered on the main thread and receives no raw URL/title.
void AcquireChromePageAsync(ChromePageAcquire acquire,
                            std::uint64_t generation,
                            ChromePageCompletion completion);
bool ChromeAutomationPreflight(std::int64_t browser_pid);
// Explicit user-triggered action only. Polling and tests must call the
// prompt-free preflight above instead.
bool RequestChromeAutomationPermission(std::int64_t browser_pid);
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
       state.permission.consent==ChromeConsent::kNotRequested);
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
class ChromeConnectionController {
 public:
  void Refresh(const ChromeConnectionAdapters& adapters) {
    const auto target = adapters.discover ? adapters.discover() : ChromeRunningTarget{};
    if (target != snapshot_.target) {
      ++generation_; active_ = {}; snapshot_ = {}; snapshot_.target = target;
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
  void Connect(const ChromeConnectionAdapters& adapters) {
    Refresh(adapters);
    snapshot_.settings_open_failed = false;
    if (!snapshot_.target.valid() || !adapters.permission) return;
    if (snapshot_.permission.consent != ChromeConsent::kGranted) {
      ++generation_; active_ = {};
      snapshot_.authorization_generation = ++authorization_generation_;
      snapshot_.permission = adapters.permission(snapshot_.target.pid, true);
      snapshot_.provider = ChromeProviderState::kUnknown;
      snapshot_.page = core::PageAvailability::kUnavailable;
      snapshot_.observation_active = false;
    }
  }
  [[nodiscard]] ChromeConnectionProbe BeginProbe() {
    snapshot_.settings_open_failed = false;
    if (!snapshot_.target.valid() ||
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
  [[nodiscard]] const ChromeConnectionSnapshot& snapshot() const { return snapshot_; }
 private:
  ChromeConnectionSnapshot snapshot_;
  ChromeConnectionProbe active_;
  std::uint64_t generation_ = 0;
  std::uint64_t authorization_generation_ = 0;
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
// prompt-free; only ConnectChromeExplicit invokes askIfNeeded=true.
ChromeConnectionSnapshot RefreshChromeConnection();
ChromeConnectionSnapshot ChromeConnectionStatus();
void ConnectChromeExplicit();
void RetryChromeExplicit();
bool OpenChromeAutomationSettingsExplicit();
void PublishChromeObservation(std::int64_t pid, bool active,
                              core::PageAvailability page,
                              bool provider_result = false);
ChromePageSample AcquireCurrentChromePage(const core::Context& captured);
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
core::WindowObservation ObserveForegroundWindow(std::uint64_t generation);
ChromePageSample AcquireCurrentChromePage(
    const core::WindowObservation& foreground);
#endif

}  // namespace seethis::platform
