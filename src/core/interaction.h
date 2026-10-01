#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/geometry.h"

namespace seethis::core {

enum class InteractionState {
  kIdle,
  kDrawing,
  kFinished,
};

[[nodiscard]] constexpr const char* InteractionStateName(
    InteractionState state) {
  switch (state) {
    case InteractionState::kIdle: return "idle";
    case InteractionState::kDrawing: return "drawing";
    case InteractionState::kFinished: return "finished";
  }
  return "unknown";
}

enum class CancelReason {
  kNone,
  kUser,
  kShortcutInterrupted,
  kLostKeyUp,
  kWatchdogTimeout,
  kApplicationSwitched,
  kApplicationResignedActive,
  kDisplayConfigurationChanged,
  kInputMonitorInterrupted,
  kEmptySession,
};

[[nodiscard]] constexpr const char* CancelReasonName(CancelReason reason) {
  switch (reason) {
    case CancelReason::kNone: return "none";
    case CancelReason::kUser: return "escape";
    case CancelReason::kShortcutInterrupted: return "shortcut_interrupted";
    case CancelReason::kLostKeyUp: return "lost_key_up";
    case CancelReason::kWatchdogTimeout: return "hold_timeout";
    case CancelReason::kApplicationSwitched: return "application_switched";
    case CancelReason::kApplicationResignedActive: return "application_resigned";
    case CancelReason::kDisplayConfigurationChanged: return "display_changed";
    case CancelReason::kInputMonitorInterrupted: return "input_monitor_interrupted";
    case CancelReason::kEmptySession: return "empty_session";
  }
  return "unknown";
}

[[nodiscard]] constexpr const char* CancelReasonDescription(
    CancelReason reason) {
  switch (reason) {
    case CancelReason::kNone: return "none";
    case CancelReason::kUser: return "Escape pressed";
    case CancelReason::kShortcutInterrupted: return "capture shortcut interrupted";
    case CancelReason::kLostKeyUp: return "capture key release was lost";
    case CancelReason::kWatchdogTimeout: return "capture hold timed out";
    case CancelReason::kApplicationSwitched: return "foreground application changed";
    case CancelReason::kApplicationResignedActive: return "SeeThis lost focus";
    case CancelReason::kDisplayConfigurationChanged: return "display configuration changed";
    case CancelReason::kInputMonitorInterrupted: return "input monitoring was interrupted";
    case CancelReason::kEmptySession: return "no non-degenerate region was drawn";
  }
  return "unknown capture cancellation";
}

enum class CompletionReason {
  kNone,
  kShortcutKeyReleased,
  kModifierReleased,
  kWatchdogObservedRelease,
  kWatchdogObservedModifierRelease,
};

[[nodiscard]] constexpr const char* CompletionReasonName(
    CompletionReason reason) {
  switch (reason) {
    case CompletionReason::kNone: return "none";
    case CompletionReason::kShortcutKeyReleased: return "capture_key_first";
    case CompletionReason::kModifierReleased: return "modifier_first";
    case CompletionReason::kWatchdogObservedRelease:
      return "watchdog_observed_key_release";
    case CompletionReason::kWatchdogObservedModifierRelease:
      return "watchdog_observed_modifier_release";
  }
  return "unknown";
}

enum class StartResult {
  kStarted,
  kIgnoredRepeat,
  kInvalidPoint,
};

// The inspector is a presentation of one selected reference.  These small
// policy helpers keep selection and panel visibility deterministic without
// introducing another capture or permission state machine.
enum class InspectorSelectionTrigger {
  kNewCircle,
  kMarkClick,
  kHover,
  kBackgroundCompletion,
  kSelectedDelete,
};

[[nodiscard]] constexpr bool InspectorSelectsReference(
    InspectorSelectionTrigger trigger) {
  return trigger == InspectorSelectionTrigger::kNewCircle ||
         trigger == InspectorSelectionTrigger::kMarkClick;
}

[[nodiscard]] constexpr bool InspectorOpensForTrigger(
    InspectorSelectionTrigger trigger) {
  return InspectorSelectsReference(trigger);
}

[[nodiscard]] constexpr bool InspectorAutoOpenAllowed(
    bool user_hidden, InspectorSelectionTrigger trigger) {
  return !user_hidden && InspectorOpensForTrigger(trigger);
}

[[nodiscard]] constexpr bool InspectorClearsForTrigger(
    InspectorSelectionTrigger trigger) {
  return trigger == InspectorSelectionTrigger::kSelectedDelete;
}

[[nodiscard]] constexpr bool InspectorConsumesPointer(bool visible) {
  return visible;
}

[[nodiscard]] constexpr bool InspectorMetadataBodyScrollable() {
  return true;
}

[[nodiscard]] constexpr bool InspectorMetadataReachable(bool long_content) {
  return !long_content || InspectorMetadataBodyScrollable();
}

struct InteractionSnapshot {
  InteractionState state = InteractionState::kIdle;
  CancelReason cancel_reason = CancelReason::kNone;
  CompletionReason completion_reason = CompletionReason::kNone;
  DisplayPoint session_origin;
  std::vector<std::vector<DisplayPoint>> regions;
  // One logical region may contain multiple contiguous in-display subpaths.
  // `regions` remains the legacy flattened projection for existing callers;
  // new consumers must use this field to preserve gaps.
  using Subpath = std::vector<DisplayPoint>;
  using LogicalRegion = std::vector<Subpath>;
  std::vector<LogicalRegion> subpaths;
  bool region_active = false;
  // Observed process-session monotonic milliseconds from the input adapter.
  // These are not hardware event timestamps or capture timestamps.
  std::int64_t key_down_monotonic_ms = 0;
  std::int64_t key_up_monotonic_ms = 0;

  [[nodiscard]] bool captures_pointer() const {
    return state == InteractionState::kDrawing;
  }
};

class InteractionController {
 public:
  static constexpr std::int64_t kDefaultMaximumHoldMilliseconds = 30'000;
  // A listen-only physical-state sample can precede the queued Carbon/CG
  // release callback. Keep the session alive for a bounded callback drain.
  static constexpr std::int64_t kReleaseReconciliationMilliseconds = 250;

  [[nodiscard]] StartResult ShortcutKeyDown(std::int64_t monotonic_ms,
                                            DisplayPoint initial_point,
                                            bool autorepeat = false);
  void SetSelectedDisplayGeometry(DisplayGeometry geometry);
  [[nodiscard]] bool PointerMoved(DisplayPoint point);
  [[nodiscard]] bool PointerDown(DisplayPoint point);
  [[nodiscard]] bool PointerUp(DisplayPoint point);
  [[nodiscard]] bool ShortcutKeyUp(std::int64_t monotonic_ms);
  [[nodiscard]] bool ShortcutModifierUp(std::int64_t monotonic_ms);
  [[nodiscard]] bool Cancel(CancelReason reason);
  [[nodiscard]] bool Watchdog(
      std::int64_t monotonic_ms, bool shortcut_key_is_down,
      std::int64_t maximum_hold_ms = kDefaultMaximumHoldMilliseconds,
      bool shortcut_modifiers_present = true);

  [[nodiscard]] InteractionSnapshot Snapshot() const;
  [[nodiscard]] bool HitTestFinishedMark(DisplayPoint point,
                                         double radius_logical) const;

 private:
  [[nodiscard]] bool Complete(std::int64_t monotonic_ms,
                              CompletionReason reason);
  void FinishActiveRegion();
  InteractionState state_ = InteractionState::kIdle;
  CancelReason cancel_reason_ = CancelReason::kNone;
  CompletionReason completion_reason_ = CompletionReason::kNone;
  std::int64_t drawing_started_ms_ = 0;
  std::int64_t drawing_finished_ms_ = 0;
  DisplayId selected_display_id_ = 0;
  double selected_backing_scale_ = 0;
  DisplayPoint session_origin_;
  std::vector<std::vector<DisplayPoint>> regions_;
  std::vector<InteractionSnapshot::LogicalRegion> subpaths_;
  bool region_active_ = false;
  bool physical_release_observed_ = false;
  std::int64_t physical_release_observed_ms_ = 0;
  std::optional<DisplayGeometry> selected_display_geometry_;
  std::optional<DisplayPoint> last_pointer_;
};

enum class DeleteGestureState {
  kUnavailable,
  kIdle,
  kCandidate,
  kArmed,
  kDrain,
};

[[nodiscard]] constexpr const char* DeleteGestureStateName(
    DeleteGestureState state) {
  switch (state) {
    case DeleteGestureState::kUnavailable: return "unavailable";
    case DeleteGestureState::kIdle: return "idle";
    case DeleteGestureState::kCandidate: return "candidate";
    case DeleteGestureState::kArmed: return "armed";
    case DeleteGestureState::kDrain: return "drain";
  }
  return "unknown";
}

struct DeleteGestureSnapshot {
  DeleteGestureState state = DeleteGestureState::kUnavailable;
  std::uint64_t registration_generation = 0;
  bool registration_ready = false;
  bool observation_ready = false;
  bool key_down = false;
  bool exact_modifiers = false;

  [[nodiscard]] bool armed() const {
    return state == DeleteGestureState::kArmed;
  }
  [[nodiscard]] bool blocks_mark_copy() const {
    return state == DeleteGestureState::kCandidate ||
           state == DeleteGestureState::kArmed ||
           state == DeleteGestureState::kDrain;
  }
};

// Carbon event times are floating-point seconds since boot. Native callers
// provide the narrow interval represented by that value, including its
// uptime-dependent quantization. `required` distinguishes unavailable native
// provenance (which must fail closed) from timestamp-free reducer fixtures.
struct DeleteEventProvenance {
  std::uint64_t earliest_ns = 0;
  std::uint64_t latest_ns = 0;
  bool required = false;

  [[nodiscard]] bool valid() const {
    return earliest_ns != 0 && latest_ns >= earliest_ns;
  }
};

// Convert Carbon's documented floating-point seconds-since-boot unit into
// the full integral-nanosecond interval it can represent at this uptime.
// Invalid or signed-integer-out-of-range input remains required but invalid.
[[nodiscard]] DeleteEventProvenance DeleteEventProvenanceFromSeconds(
    double seconds_since_boot);

// Pure reducer for the separately registered delete chord. Native code supplies
// observed physical transitions and Carbon hot-key confirmations.
class DeleteGestureController {
 public:
  void Configure(std::uint64_t registration_generation,
                 bool registration_ready, bool observation_ready,
                 bool physical_key_down = false,
                 DeleteEventProvenance eligibility_boundary = {});
  void ObservationReadinessChanged(
      bool ready, bool physical_key_down,
      DeleteEventProvenance eligibility_boundary = {});
  void KeyDown(bool exact_modifiers, bool autorepeat,
               DeleteEventProvenance event_provenance = {});
  void KeyUp(DeleteEventProvenance eligibility_boundary = {});
  void ModifiersChanged(bool exact_modifiers);
  void HotKeyPressed(std::uint64_t registration_generation,
                     bool physical_key_down, bool exact_modifiers,
                     DeleteEventProvenance event_provenance = {});
  void HotKeyReleased(
      std::uint64_t registration_generation,
      DeleteEventProvenance event_provenance = {});
  void Interrupt(bool physical_key_down,
                 DeleteEventProvenance eligibility_boundary = {});
  void Reconcile(bool physical_key_down, bool exact_modifiers,
                 DeleteEventProvenance eligibility_boundary = {});
  [[nodiscard]] bool ConsumeDeleteClick(bool physical_key_down,
                                        bool exact_modifiers);
  [[nodiscard]] DeleteGestureSnapshot Snapshot() const;

 private:
  void Recompute();
  void RecordEligibilityBoundary(DeleteEventProvenance boundary);
  bool EventIsAfterEligibilityBoundary(DeleteEventProvenance event) const;
  bool EventMayBelongToPress(DeleteEventProvenance event,
                             DeleteEventProvenance boundary) const;
  DeleteGestureState state_ = DeleteGestureState::kUnavailable;
  std::uint64_t registration_generation_ = 0;
  bool registration_ready_ = false;
  bool observation_ready_ = false;
  bool key_down_ = false;
  bool exact_modifiers_ = false;
  bool fresh_candidate_ = false;
  bool hotkey_confirmed_ = false;
  bool drain_ = false;
  bool provenance_required_ = false;
  DeleteEventProvenance eligibility_boundary_;
  DeleteEventProvenance press_boundary_;
};

}  // namespace seethis::core
