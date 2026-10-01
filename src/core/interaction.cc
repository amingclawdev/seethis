#include "core/interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seethis::core {
namespace {

bool IsLogicalPoint(const DisplayPoint& point) {
  return point.IsValid() && point.unit == CoordinateUnit::kLogicalPoints;
}

bool SamePoint(const DisplayPoint& a, const DisplayPoint& b) {
  return a.display_id == b.display_id && a.backing_scale == b.backing_scale &&
         a.position.x == b.position.x && a.position.y == b.position.y;
}

bool NonDegenerate(const std::vector<DisplayPoint>& path) {
  if (path.size() < 2) return false;
  return std::any_of(path.begin() + 1, path.end(), [&](const auto& point) {
    return point.position.x != path.front().position.x ||
           point.position.y != path.front().position.y;
  });
}

bool ClipSegment(Point2D start, Point2D end, const DisplayGeometry& geometry,
                 Point2D* clipped_start, Point2D* clipped_end) {
  const double right = geometry.logical_size.width;
  const double bottom = geometry.logical_size.height;
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  double first = 0.0;
  double last = 1.0;
  const auto clip = [&](double coefficient, double bound) {
    if (coefficient == 0.0) return bound >= 0.0;
    const double crossing = bound / coefficient;
    if (coefficient < 0.0) {
      if (crossing > last) return false;
      first = std::max(first, crossing);
    } else {
      if (crossing < first) return false;
      last = std::min(last, crossing);
    }
    return true;
  };
  if (!clip(-dx, start.x) || !clip(dx, right - start.x) ||
      !clip(-dy, start.y) || !clip(dy, bottom - start.y) || first > last)
    return false;
  *clipped_start = {std::clamp(start.x + first * dx, 0.0, right),
                    std::clamp(start.y + first * dy, 0.0, bottom)};
  *clipped_end = {std::clamp(start.x + last * dx, 0.0, right),
                  std::clamp(start.y + last * dy, 0.0, bottom)};
  return true;
}

void Flatten(const std::vector<InteractionSnapshot::LogicalRegion>& source,
             std::vector<std::vector<DisplayPoint>>* result) {
  result->clear();
  result->reserve(source.size());
  for (const auto& region : source) {
    if (region.size() > 1) {
      // The legacy field cannot represent a gap. Leave it unavailable instead
      // of publishing a flattened path that would imply a connecting chord.
      result->clear();
      return;
    }
    std::vector<DisplayPoint> path;
    for (const auto& subpath : region)
      path.insert(path.end(), subpath.begin(), subpath.end());
    result->push_back(std::move(path));
  }
}

double SquaredDistance(Point2D first, Point2D second) {
  const double dx = first.x - second.x;
  const double dy = first.y - second.y;
  return dx * dx + dy * dy;
}

double SquaredDistanceToSegment(Point2D point, Point2D start, Point2D end) {
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const double length_squared = dx * dx + dy * dy;
  if (length_squared <= std::numeric_limits<double>::epsilon()) {
    return SquaredDistance(point, start);
  }

  const double projection =
      ((point.x - start.x) * dx + (point.y - start.y) * dy) /
      length_squared;
  const double bounded = std::clamp(projection, 0.0, 1.0);
  return SquaredDistance(point,
                         {start.x + bounded * dx, start.y + bounded * dy});
}

}  // namespace

DeleteEventProvenance DeleteEventProvenanceFromSeconds(
    double seconds_since_boot) {
  DeleteEventProvenance result;
  result.required = true;
  constexpr long double kNanosecondsPerSecond = 1'000'000'000.0L;
  const long double scaled =
      static_cast<long double>(seconds_since_boot) * kNanosecondsPerSecond;
  if (!std::isfinite(seconds_since_boot) || seconds_since_boot <= 0 ||
      scaled < 1 ||
      scaled >= static_cast<long double>(
                   std::numeric_limits<long long>::max())) {
    return result;
  }

  const double previous = std::nextafter(seconds_since_boot, 0.0);
  const double next = std::nextafter(
      seconds_since_boot, std::numeric_limits<double>::infinity());
  const long double lower_step =
      static_cast<long double>(seconds_since_boot - previous);
  const long double upper_step =
      static_cast<long double>(next - seconds_since_boot);
  const long double full_ulp =
      lower_step > upper_step ? lower_step : upper_step;
  const auto uncertainty = static_cast<std::uint64_t>(
      std::ceil(full_ulp * kNanosecondsPerSecond / 2.0L)) + 1;
  const auto center = static_cast<std::uint64_t>(std::llround(scaled));
  result.earliest_ns = center > uncertainty ? center - uncertainty : 1;
  result.latest_ns = center + uncertainty;
  return result;
}

StartResult InteractionController::ShortcutKeyDown(
    std::int64_t monotonic_ms, DisplayPoint initial_point, bool autorepeat) {
  if (autorepeat || state_ == InteractionState::kDrawing) {
    return StartResult::kIgnoredRepeat;
  }
  if (!IsLogicalPoint(initial_point)) {
    return StartResult::kInvalidPoint;
  }

  state_ = InteractionState::kDrawing;
  cancel_reason_ = CancelReason::kNone;
  completion_reason_ = CompletionReason::kNone;
  drawing_started_ms_ = monotonic_ms;
  drawing_finished_ms_ = 0;
  selected_display_id_ = initial_point.display_id;
  selected_backing_scale_ = initial_point.backing_scale;
  session_origin_ = initial_point;
  regions_.clear();
  subpaths_.clear();
  region_active_ = false;
  physical_release_observed_ = false;
  physical_release_observed_ms_ = 0;
  selected_display_geometry_.reset();
  last_pointer_.reset();
  return StartResult::kStarted;
}

void InteractionController::SetSelectedDisplayGeometry(DisplayGeometry geometry) {
  if (!geometry.IsValid() || geometry.display_id != selected_display_id_ ||
      geometry.backing_scale != selected_backing_scale_) return;
  selected_display_geometry_ = geometry;
}

bool InteractionController::PointerDown(DisplayPoint point) {
  if (state_ != InteractionState::kDrawing || region_active_ ||
      !IsLogicalPoint(point) || point.display_id != selected_display_id_ ||
      point.backing_scale != selected_backing_scale_) {
    return false;
  }
  if (selected_display_geometry_ &&
      !selected_display_geometry_->ContainsLocalLogical(point.position)) return false;
  regions_.push_back({point});
  subpaths_.push_back({{point}});
  region_active_ = true;
  last_pointer_ = point;
  return true;
}

bool InteractionController::PointerMoved(DisplayPoint point) {
  if (state_ != InteractionState::kDrawing || !region_active_ ||
      !IsLogicalPoint(point) || point.display_id != selected_display_id_ ||
      point.backing_scale != selected_backing_scale_) {
    return false;
  }
  const auto previous = last_pointer_;
  last_pointer_ = point;
  if (!selected_display_geometry_ || !previous) {
    auto& path = subpaths_.back().back();
    if (!SamePoint(path.back(), point)) path.push_back(point);
    regions_.back().push_back(point);
    return true;
  }
  const bool previous_in =
      selected_display_geometry_->ContainsLocalLogical(previous->position);
  const bool current_in =
      selected_display_geometry_->ContainsLocalLogical(point.position);
  if (previous_in && current_in) {
    auto& path = subpaths_.back().back();
    if (SamePoint(path.back(), point)) return false;
    path.push_back(point);
    regions_.back().push_back(point);
    return true;
  }
  Point2D clipped_start, clipped_end;
  if (!ClipSegment(previous->position, point.position,
                  *selected_display_geometry_, &clipped_start,
                  &clipped_end)) {
    return false;
  }
  const DisplayPoint entry{point.display_id, point.unit, clipped_start,
                           point.backing_scale};
  const DisplayPoint exit{point.display_id, point.unit, clipped_end,
                          point.backing_scale};
  if (previous_in && !current_in) {
    auto& path = subpaths_.back().back();
    if (!SamePoint(path.back(), exit)) path.push_back(exit);
    regions_.back().push_back(exit);
    return true;
  }
  if (!previous_in && current_in) {
    auto& region = subpaths_.back();
    region.push_back({});
    region.back().push_back(entry);
    if (!SamePoint(entry, point)) region.back().push_back(point);
    regions_.back().push_back(entry);
    regions_.back().push_back(point);
    return true;
  }
  if (SamePoint(entry, exit)) {
    return false;
  }
  subpaths_.back().push_back({entry, exit});
  regions_.back().push_back(entry);
  regions_.back().push_back(exit);
  return true;
}

bool InteractionController::PointerUp(DisplayPoint point) {
  if (state_ != InteractionState::kDrawing || !region_active_) return false;
  if (!IsLogicalPoint(point) || point.display_id != selected_display_id_ ||
      point.backing_scale != selected_backing_scale_) {
    FinishActiveRegion();
    return true;
  }
  const auto previous = last_pointer_;
  if (selected_display_geometry_ && previous) {
    const bool previous_in =
        selected_display_geometry_->ContainsLocalLogical(previous->position);
    const bool current_in =
        selected_display_geometry_->ContainsLocalLogical(point.position);
    if (!previous_in || !current_in) {
      Point2D clipped_start, clipped_end;
      if (ClipSegment(previous->position, point.position,
                      *selected_display_geometry_, &clipped_start,
                      &clipped_end)) {
        const DisplayPoint entry{point.display_id, point.unit, clipped_start,
                                 point.backing_scale};
        const DisplayPoint exit{point.display_id, point.unit, clipped_end,
                                point.backing_scale};
        if (previous_in && !current_in) {
          auto& path = subpaths_.back().back();
          if (!SamePoint(path.back(), exit)) path.push_back(exit);
          regions_.back().push_back(exit);
        } else if (!previous_in && current_in) {
          auto& region = subpaths_.back();
          region.push_back({entry});
          if (!SamePoint(entry, point)) region.back().push_back(point);
          regions_.back().push_back(entry);
          regions_.back().push_back(point);
        } else if (!SamePoint(entry, exit)) {
          subpaths_.back().push_back({entry, exit});
          regions_.back().push_back(entry);
          regions_.back().push_back(exit);
        }
      }
      last_pointer_ = point;
      FinishActiveRegion();
      return true;
    }
  }
  if (selected_display_geometry_ &&
      !selected_display_geometry_->ContainsLocalLogical(point.position)) {
    FinishActiveRegion();
    return true;
  }
  auto& path = subpaths_.back().back();
  if (!SamePoint(path.back(), point)) {
    path.push_back(point);
    regions_.back().push_back(point);
  }
  last_pointer_ = point;
  FinishActiveRegion();
  return true;
}

void InteractionController::FinishActiveRegion() {
  if (!region_active_) return;
  auto& region = subpaths_.back();
  region.erase(std::remove_if(region.begin(), region.end(),
                              [](const auto& path) { return !NonDegenerate(path); }),
               region.end());
  if (region.empty()) {
    subpaths_.pop_back();
    regions_.pop_back();
  }
  region_active_ = false;
}

bool InteractionController::ShortcutKeyUp(std::int64_t monotonic_ms) {
  return Complete(monotonic_ms, CompletionReason::kShortcutKeyReleased);
}

bool InteractionController::ShortcutModifierUp(std::int64_t monotonic_ms) {
  return Complete(monotonic_ms, CompletionReason::kModifierReleased);
}

bool InteractionController::Complete(std::int64_t monotonic_ms,
                                     CompletionReason reason) {
  if (state_ != InteractionState::kDrawing ||
      monotonic_ms < drawing_started_ms_) {
    return false;
  }
  FinishActiveRegion();
  if (regions_.empty()) return Cancel(CancelReason::kEmptySession);
  state_ = InteractionState::kFinished;
  cancel_reason_ = CancelReason::kNone;
  completion_reason_ = reason;
  drawing_finished_ms_ = monotonic_ms;
  physical_release_observed_ = false;
  physical_release_observed_ms_ = 0;
  return true;
}

bool InteractionController::Cancel(CancelReason reason) {
  if (state_ != InteractionState::kDrawing) {
    return false;
  }
  state_ = InteractionState::kIdle;
  cancel_reason_ = reason;
  completion_reason_ = CompletionReason::kNone;
  drawing_started_ms_ = 0;
  drawing_finished_ms_ = 0;
  regions_.clear();
  subpaths_.clear();
  region_active_ = false;
  selected_display_id_ = 0;
  selected_backing_scale_ = 0;
  session_origin_ = {};
  physical_release_observed_ = false;
  physical_release_observed_ms_ = 0;
  selected_display_geometry_.reset();
  last_pointer_.reset();
  return true;
}

bool InteractionController::Watchdog(std::int64_t monotonic_ms,
                                     bool shortcut_key_is_down,
                                     std::int64_t maximum_hold_ms,
                                     bool shortcut_modifiers_present) {
  if (state_ != InteractionState::kDrawing) {
    return false;
  }
  if (maximum_hold_ms >= 0 && monotonic_ms >= drawing_started_ms_ &&
      monotonic_ms - drawing_started_ms_ > maximum_hold_ms) {
    return Cancel(CancelReason::kWatchdogTimeout);
  }
  const bool shortcut_released =
      !shortcut_key_is_down || !shortcut_modifiers_present;
  if (shortcut_released) {
    if (!physical_release_observed_) {
      physical_release_observed_ = true;
      physical_release_observed_ms_ = monotonic_ms;
      return false;
    }
    if (monotonic_ms < physical_release_observed_ms_ ||
        monotonic_ms - physical_release_observed_ms_ <
            kReleaseReconciliationMilliseconds) {
      return false;
    }
    // No queued release arrived within the bounded drain. A valid retained
    // session has enough evidence to finish from the physical observation;
    // an empty/stuck session remains an honest lost-key cancellation.
    FinishActiveRegion();
    if (!regions_.empty()) {
      return Complete(
          monotonic_ms,
          shortcut_key_is_down
              ? CompletionReason::kWatchdogObservedModifierRelease
              : CompletionReason::kWatchdogObservedRelease);
    }
    return Cancel(CancelReason::kLostKeyUp);
  }
  // A transient physical-state read can race a queued key-down/repeat or a
  // queued modifier transition. It is no longer release evidence once the
  // required capture modifiers are observed again. Extra modifiers do not
  // count as a release while the required modifier remains present.
  physical_release_observed_ = false;
  physical_release_observed_ms_ = 0;
  return false;
}

InteractionSnapshot InteractionController::Snapshot() const {
  InteractionSnapshot snapshot{state_, cancel_reason_, completion_reason_,
                               session_origin_, regions_, subpaths_, region_active_,
                               drawing_started_ms_, drawing_finished_ms_};
  Flatten(subpaths_, &snapshot.regions);
  return snapshot;
}

bool InteractionController::HitTestFinishedMark(DisplayPoint point,
                                                double radius_logical) const {
  if (state_ != InteractionState::kFinished || !IsLogicalPoint(point) ||
      !std::isfinite(radius_logical) || radius_logical < 0.0) {
    return false;
  }

  const double radius_squared = radius_logical * radius_logical;
  for (const auto& region : subpaths_) for (const auto& path : region) {
   for (std::size_t index = 0; index < path.size(); ++index) {
    const DisplayPoint& sample = path[index];
    if (sample.display_id != point.display_id) {
      continue;
    }
    if (SquaredDistance(point.position, sample.position) <= radius_squared) {
      return true;
    }
    if (index == 0) {
      continue;
    }
    const DisplayPoint& previous = path[index - 1];
    if (previous.display_id == point.display_id &&
        SquaredDistanceToSegment(point.position, previous.position,
                                 sample.position) <= radius_squared) {
      return true;
    }
   }
  }
  return false;
}

void DeleteGestureController::Configure(std::uint64_t registration_generation,
                                        bool registration_ready,
                                        bool observation_ready,
                                        bool physical_key_down,
                                        DeleteEventProvenance boundary) {
  registration_generation_ = registration_generation;
  registration_ready_ = registration_ready;
  observation_ready_ = observation_ready;
  key_down_ = physical_key_down;
  exact_modifiers_ = false;
  fresh_candidate_ = false;
  hotkey_confirmed_ = false;
  drain_ = physical_key_down;
  provenance_required_ = boundary.required;
  eligibility_boundary_ = boundary;
  press_boundary_ = {};
  Recompute();
}

void DeleteGestureController::ObservationReadinessChanged(
    bool ready, bool physical_key_down, DeleteEventProvenance boundary) {
  if (ready == observation_ready_) {
    if (!ready) {
      Interrupt(physical_key_down, boundary);
    } else if (!physical_key_down) {
      KeyUp(boundary);
    }
    return;
  }
  observation_ready_ = ready;
  if (!ready) {
    Interrupt(physical_key_down, boundary);
    return;
  }
  key_down_ = physical_key_down;
  if (physical_key_down) {
    drain_ = true;
  } else {
    RecordEligibilityBoundary(boundary);
  }
  Recompute();
}

void DeleteGestureController::KeyDown(
    bool exact_modifiers, bool autorepeat,
    DeleteEventProvenance event_provenance) {
  if (!registration_ready_ || !observation_ready_) return;
  if (event_provenance.required) provenance_required_ = true;
  if (provenance_required_ &&
      !EventIsAfterEligibilityBoundary(event_provenance)) {
    return;
  }
  if (press_boundary_.valid() &&
      (!event_provenance.valid() ||
       !EventMayBelongToPress(event_provenance, press_boundary_))) {
    return;
  }
  if (autorepeat) {
    if (state_ == DeleteGestureState::kArmed && key_down_ &&
        exact_modifiers && exact_modifiers_ && fresh_candidate_ &&
        hotkey_confirmed_ && !drain_) {
      return;
    }
    key_down_ = true;
    exact_modifiers_ = exact_modifiers;
    fresh_candidate_ = false;
    hotkey_confirmed_ = false;
    drain_ = true;
    Recompute();
    return;
  }
  if (key_down_) {
    key_down_ = true;
    exact_modifiers_ = exact_modifiers;
    Recompute();
    return;
  }
  key_down_ = true;
  exact_modifiers_ = exact_modifiers;
  if (!hotkey_confirmed_ && event_provenance.valid()) {
    press_boundary_ = event_provenance;
  }
  fresh_candidate_ = exact_modifiers && !drain_;
  Recompute();
}

void DeleteGestureController::RecordEligibilityBoundary(
    DeleteEventProvenance boundary) {
  if (boundary.required) provenance_required_ = true;
  if (!provenance_required_) return;
  eligibility_boundary_ = boundary;
}

void DeleteGestureController::KeyUp(DeleteEventProvenance boundary) {
  if (boundary.required) provenance_required_ = true;
  if (provenance_required_ && !boundary.valid()) return;
  if (press_boundary_.valid() &&
      (!boundary.valid() || !EventMayBelongToPress(boundary, press_boundary_))) {
    return;
  }
  key_down_ = false;
  exact_modifiers_ = false;
  fresh_candidate_ = false;
  hotkey_confirmed_ = false;
  drain_ = false;
  press_boundary_ = {};
  RecordEligibilityBoundary(boundary);
  Recompute();
}

void DeleteGestureController::ModifiersChanged(bool exact_modifiers) {
  exact_modifiers_ = exact_modifiers;
  if (key_down_ && !exact_modifiers) {
    fresh_candidate_ = false;
    hotkey_confirmed_ = false;
    drain_ = true;
  }
  Recompute();
}

void DeleteGestureController::HotKeyPressed(
    std::uint64_t registration_generation, bool physical_key_down,
    bool exact_modifiers, DeleteEventProvenance event_provenance) {
  if (registration_generation != registration_generation_ ||
      !registration_ready_ || !observation_ready_ || drain_ ||
      !physical_key_down || !exact_modifiers) {
    return;
  }
  if (event_provenance.required) provenance_required_ = true;
  if (provenance_required_) {
    // Both values use Carbon EventTime. A press is current only when its full
    // quantization interval is later than the last eligibility boundary.
    // Equal or overlapping intervals are ambiguous and therefore fail closed.
    if (!EventIsAfterEligibilityBoundary(event_provenance)) {
      return;
    }
  }
  if (press_boundary_.valid() &&
      (!event_provenance.valid() ||
       !EventMayBelongToPress(event_provenance, press_boundary_))) {
    return;
  }
  if (!press_boundary_.valid() && event_provenance.valid()) {
    press_boundary_ = event_provenance;
  }
  hotkey_confirmed_ = true;
  Recompute();
}

void DeleteGestureController::HotKeyReleased(
    std::uint64_t registration_generation,
    DeleteEventProvenance event_provenance) {
  if (registration_generation != registration_generation_) return;
  if (event_provenance.required) provenance_required_ = true;
  if (provenance_required_ && !event_provenance.valid()) return;
  if (press_boundary_.valid() &&
      (!event_provenance.valid() ||
       !EventMayBelongToPress(event_provenance, press_boundary_))) {
    return;
  }
  hotkey_confirmed_ = false;
  fresh_candidate_ = false;
  drain_ = key_down_;
  Recompute();
}

void DeleteGestureController::Interrupt(bool physical_key_down,
                                        DeleteEventProvenance boundary) {
  key_down_ = physical_key_down;
  exact_modifiers_ = false;
  fresh_candidate_ = false;
  hotkey_confirmed_ = false;
  drain_ = physical_key_down;
  press_boundary_ = {};
  RecordEligibilityBoundary(boundary);
  Recompute();
}

void DeleteGestureController::Reconcile(bool physical_key_down,
                                        bool exact_modifiers,
                                        DeleteEventProvenance boundary) {
  if (!physical_key_down) {
    KeyUp(boundary);
    return;
  }
  // A physical-state sample is recovery evidence, not a fresh key event.
  // Keep the raw-event boundary authoritative so a timer poll cannot create
  // or drain a new invocation before the queued key-down is delivered.
  if (!key_down_) {
    // Carbon may be delivered before the raw key-down. Preserve that pending
    // confirmation while the exact chord remains physically present, but a
    // modifier loss invalidates it rather than manufacturing a drain state.
    if (!exact_modifiers && hotkey_confirmed_) {
      hotkey_confirmed_ = false;
      fresh_candidate_ = false;
      exact_modifiers_ = false;
      drain_ = false;
      press_boundary_ = {};
      RecordEligibilityBoundary(boundary);
      Recompute();
    }
    return;
  }
  if (!exact_modifiers) {
    ModifiersChanged(false);
    return;
  }
  exact_modifiers_ = true;
  Recompute();
}

bool DeleteGestureController::ConsumeDeleteClick(bool physical_key_down,
                                                 bool exact_modifiers) {
  if (state_ != DeleteGestureState::kArmed || !physical_key_down ||
      !exact_modifiers) {
    if (state_ == DeleteGestureState::kArmed ||
        state_ == DeleteGestureState::kCandidate) {
      Interrupt(physical_key_down);
    }
    return false;
  }
  // A click is a deliberate unit of work, so consuming it must not end the
  // still-valid shortcut hold. Raw key-up, modifier loss, interruption, or
  // generation/readiness changes remain the only exits.
  return true;
}

bool DeleteGestureController::EventMayBelongToPress(
    DeleteEventProvenance event, DeleteEventProvenance boundary) const {
  // Raw CG and Carbon intervals for one physical press may overlap because
  // they are observed through different queues. Reject only an interval that
  // is definitely older than the current press.
  return event.valid() && boundary.valid() &&
      event.latest_ns >= boundary.earliest_ns;
}

bool DeleteGestureController::EventIsAfterEligibilityBoundary(
    DeleteEventProvenance event) const {
  return event.valid() && eligibility_boundary_.valid() &&
      event.earliest_ns > eligibility_boundary_.latest_ns;
}

DeleteGestureSnapshot DeleteGestureController::Snapshot() const {
  return {state_, registration_generation_, registration_ready_,
          observation_ready_, key_down_, exact_modifiers_};
}

void DeleteGestureController::Recompute() {
  if (!registration_ready_ || !observation_ready_) {
    state_ = DeleteGestureState::kUnavailable;
  } else if (drain_) {
    state_ = DeleteGestureState::kDrain;
  } else if (key_down_ && exact_modifiers_ && fresh_candidate_ &&
             hotkey_confirmed_) {
    state_ = DeleteGestureState::kArmed;
  } else if (fresh_candidate_ || hotkey_confirmed_) {
    state_ = DeleteGestureState::kCandidate;
  } else {
    state_ = DeleteGestureState::kIdle;
  }
}

}  // namespace seethis::core
