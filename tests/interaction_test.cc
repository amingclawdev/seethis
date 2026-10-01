#include "core/interaction.h"
#include "core/reference.h"
#include "platform/platform.h"

#include <cstdlib>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

#if defined(__APPLE__) && defined(SEETHIS_CLIPBOARD_MAC_TESTING)
#import <AppKit/AppKit.h>
#include <chrono>
#include <thread>
#endif

namespace {

int failures = 0;

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

seethis::core::Context DeleteContext() {
  seethis::core::Context context;
  context.pid = 42;
  context.window_pid = 42;
  context.self_pid = 7;
  context.window_id = 9;
  context.app_name = "delete-test-app";
  context.bundle_id = "test.delete.app";
  context.executable = "/tmp/delete-test-app";
  context.window_title = "delete-test-window";
  context.selection_method = "frontmost-pid/topmost-normal-containing-initial-point";
  context.excluded_window_ids = {700};
  context.exclusion_method = "exact-window-allowlist;self-pid-rejected;child-windows-off";
  context.window = {0, 0, 100, 100};
  context.quartz_to_appkit_top = 100;
  context.observed = {1'000'000, 1'000};
  context.displays.push_back({"delete-test-display", 1, {0, 0, 100, 100},
                               {0, 0, 100, 100}, 1, {1, -1, 0, 100}});
  return context;
}

#if defined(SEETHIS_WINDOW_OBSERVATION_API)
seethis::core::WindowObservation WindowFor(
    const seethis::core::Context& context,std::uint64_t generation) {
  return {seethis::core::WindowAvailability::kReady,generation,
          seethis::core::Now(),context.window_pid,context.bundle_id,
          context.window_id,context.window};
}

seethis::core::PageIdentity ChromePageFor(
    const seethis::core::Context& context,std::string tab,char digest) {
  seethis::core::PageIdentity identity;
  identity.browser_pid=context.window_pid;
  identity.window_id=context.window_id;
  identity.window_bounds=context.window;
  identity.process_start_identity_us=1;
  identity.opaque_tab_id=std::move(tab);
  identity.navigation_digest=std::string(64,digest);
  return identity;
}

bool ApplyChromePageResult(
    seethis::core::MarkController& marks,
    seethis::platform::ChromeObservationLifecycle& lifecycle,
    seethis::platform::ChromeObservationRequest request,
    std::uint64_t capture_generation,
    seethis::core::PageObservation observation) {
  if(!lifecycle.Accepts(request))return false;
  lifecycle.Complete(request);
  marks.SetPageObservation(std::move(observation));
  const auto published=marks.page_observation();
  if(published.availability==seethis::core::PageAvailability::kReady &&
     published.identity) {
    (void)marks.BindPageIdentity(capture_generation,*published.identity);
  } else if(marks.AwaitingPageIdentity(capture_generation)) {
    marks.Abort("Chrome page identity unavailable");
  }
  return true;
}
#endif

std::vector<seethis::core::DisplayPoint> DeletePath(double shift) {
  return {{1, seethis::core::CoordinateUnit::kLogicalPoints, {10 + shift, 10}, 1},
          {1, seethis::core::CoordinateUnit::kLogicalPoints, {30 + shift, 10}, 1},
          {1, seethis::core::CoordinateUnit::kLogicalPoints, {30 + shift, 30}, 1},
          {1, seethis::core::CoordinateUnit::kLogicalPoints, {10 + shift, 30}, 1},
          {1, seethis::core::CoordinateUnit::kLogicalPoints, {10 + shift, 10}, 1}};
}

seethis::core::CaptureResult DeleteCapture() {
  seethis::core::CaptureResult result;
  result.requested = {1'000'001, 1'001};
  result.completed = {1'000'002, 1'002};
  result.pixels.width = 100;
  result.pixels.height = 100;
  result.pixels.rgba.resize(100 * 100 * 4, 255);
  return result;
}

void TestContinuousDeleteAgainstDistinctMarks() {
  const std::filesystem::path root = std::filesystem::path("/tmp") /
      ("seethis-continuous-delete-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  seethis::core::ReferenceStore store(root);
  seethis::core::MarkController marks(store, [](const auto&) { return true; });
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  marks.SetWindowObservation(WindowFor(DeleteContext(),1));
#endif
  const std::array<std::string, 3> ids = {std::string(32, 'a'),
                                           std::string(32, 'b'),
                                           std::string(32, 'c')};
  for (std::size_t index = 0; index < ids.size(); ++index) {
    const auto generation = marks.Begin(ids[index]);
    Check(marks.BindContext(generation, DeleteContext()),
          "delete integration binds each immutable mark context");
    marks.Captured(generation, DeleteCapture());
    marks.Release(generation, DeletePath(static_cast<double>(index) * 30),
                  {1'000'003 + static_cast<std::int64_t>(index),
                   1'003 + static_cast<std::int64_t>(index)});
    store.WaitForIdleForTesting();
  }
  Check(marks.marks().size() == ids.size(),
        "three distinct marks are available before one continuous hold");

  seethis::core::DeleteGestureController deletion;
  deletion.Configure(11, true, true);
  deletion.KeyDown(true, false);
  deletion.HotKeyPressed(11, true, true);
  Check(!seethis::platform::OverlayAcceptsPointer(
            false, false, deletion.Snapshot()),
        "an empty click passes through while delete is armed");
  for (const auto& id : ids) {
    Check(deletion.ConsumeDeleteClick(true, true),
          "each deliberate target click is accepted by the held chord");
    Check(marks.DeleteJob(id) == seethis::core::DeleteResult::kDeleted,
          "each distinct target is deleted exactly once");
    Check(deletion.Snapshot().armed(),
          "deleting one target leaves the chord armed for the next target");
  }
  Check(marks.marks().empty(), "continuous hold removes all three targets");
  deletion.KeyUp();
  Check(deletion.Snapshot().state == seethis::core::DeleteGestureState::kIdle,
        "continuous hold exits cleanly on raw key-up");
  std::filesystem::remove_all(root);
}

seethis::core::DisplayPoint LogicalPoint(seethis::core::DisplayId display,
                                         double x, double y,
                                         double scale = 1.0) {
  return {display, seethis::core::CoordinateUnit::kLogicalPoints, {x, y},
          scale};
}

void DrawRegion(seethis::core::InteractionController& controller,
                seethis::core::DisplayPoint first,
                seethis::core::DisplayPoint second) {
  Check(controller.PointerDown(first), "left-down starts one region");
  Check(controller.PointerMoved(second), "drag appends within active region");
  Check(controller.PointerUp(second), "left-up closes only that region");
}

// Deterministic native-adapter event seam: the real macOS adapter samples the
// physical key in its timer, then dispatches the queued Carbon/CG callbacks.
// Keeping this ordering executable prevents a source-only assertion from
// hiding the field regression.
class CaptureAdapterEventWitness {
 public:
  explicit CaptureAdapterEventWitness(
      seethis::core::InteractionController& controller)
      : controller_(controller) {}

  bool PhysicalSample(std::int64_t observed_ms, bool key_down,
                      std::int64_t maximum_hold_ms =
                          seethis::core::InteractionController::
                              kDefaultMaximumHoldMilliseconds,
                      bool modifiers_present = true) {
    return controller_.Watchdog(observed_ms, key_down, maximum_hold_ms,
                                modifiers_present);
  }
  bool QueuedKeyUp(std::int64_t observed_ms) {
    return controller_.ShortcutKeyUp(observed_ms);
  }
  bool QueuedModifierUp(std::int64_t observed_ms) {
    return controller_.ShortcutModifierUp(observed_ms);
  }

 private:
  seethis::core::InteractionController& controller_;
};

// The production macOS adapter samples physical state from its bounded timer,
// then delivers raw CG and Carbon callbacks independently. This witness keeps
// that ordering executable against the same reducer entry points.
class DeleteAdapterEventWitness {
 public:
  explicit DeleteAdapterEventWitness(
      seethis::core::DeleteGestureController& controller)
      : controller_(controller) {}

  void PhysicalPoll(bool key_down, bool exact_modifiers,
                    seethis::core::DeleteEventProvenance provenance = {}) {
    controller_.Reconcile(key_down, exact_modifiers, provenance);
  }
  void RawDown(bool exact_modifiers, bool autorepeat,
               seethis::core::DeleteEventProvenance provenance = {}) {
    controller_.KeyDown(exact_modifiers, autorepeat, provenance);
  }
  void CarbonPress(std::uint64_t generation, bool key_down,
                   bool exact_modifiers,
                   seethis::core::DeleteEventProvenance provenance = {}) {
    controller_.HotKeyPressed(generation, key_down, exact_modifiers,
                              provenance);
  }
  void CarbonRelease(
      std::uint64_t generation,
      seethis::core::DeleteEventProvenance provenance = {}) {
    controller_.HotKeyReleased(generation, provenance);
  }

 private:
  seethis::core::DeleteGestureController& controller_;
};

void TestHoldMoveRelease() {
  seethis::core::InteractionController controller;
  Check(controller.ShortcutKeyDown(100, LogicalPoint(1, 5, 7, 2)) ==
            seethis::core::StartResult::kStarted,
        "first key-down starts drawing");
  Check(controller.ShortcutKeyDown(101, LogicalPoint(1, 6, 8, 2)) ==
            seethis::core::StartResult::kIgnoredRepeat,
        "autorepeat does not restart drawing");
  Check(!controller.PointerMoved(LogicalPoint(1, 20, 30, 2)),
        "idle movement during held mode draws nothing");
  DrawRegion(controller, LogicalPoint(1, 10, 10, 2),
             LogicalPoint(1, 20, 30, 2));
  Check(!controller.PointerDown(LogicalPoint(2, 4, 9, 1)),
        "selected display deterministically rejects cross-display down");
  DrawRegion(controller, LogicalPoint(1, 40, 40, 2),
             LogicalPoint(1, 60, 70, 2));
  Check(controller.ShortcutKeyUp(150), "key-up finishes drawing");

  const auto snapshot = controller.Snapshot();
  Check(snapshot.state == seethis::core::InteractionState::kFinished,
        "release leaves a finished ephemeral mark");
  Check(!snapshot.captures_pointer(), "finished state releases pointer capture");
  Check(snapshot.regions.size() == 2 && snapshot.regions[0].size() == 2 &&
            snapshot.regions[1].size() == 2,
        "one hold preserves two independent closed regions");
  Check(!controller.HitTestFinishedMark(LogicalPoint(1,30,35,2),2),
        "hit testing never invents a segment across independent regions");
  Check(snapshot.key_down_monotonic_ms == 100 &&
            snapshot.key_up_monotonic_ms == 150,
        "input observation times survive the finished snapshot");
}

void TestActiveAndEmptyRelease() {
  seethis::core::InteractionController active;
  (void)active.ShortcutKeyDown(10,LogicalPoint(1,0,0));
  (void)active.PointerDown(LogicalPoint(1,10,10));
  (void)active.PointerMoved(LogicalPoint(1,20,20));
  Check(active.ShortcutKeyUp(20)&&active.Snapshot().regions.size()==1&&
            !active.Snapshot().region_active,
        "shortcut release closes one valid active drag and publishes once");

  seethis::core::InteractionController empty;
  (void)empty.ShortcutKeyDown(30,LogicalPoint(1,0,0));
  (void)empty.PointerDown(LogicalPoint(1,5,5));
  (void)empty.PointerUp(LogicalPoint(1,5,5));
  Check(empty.ShortcutKeyUp(40)&&
            empty.Snapshot().state==seethis::core::InteractionState::kIdle&&
            empty.Snapshot().cancel_reason==seethis::core::CancelReason::kEmptySession,
        "empty and degenerate sessions leave no publishable reference");
}

void TestCancellationRoutes() {
  using seethis::core::CancelReason;
  const CancelReason reasons[] = {
      CancelReason::kUser,
      CancelReason::kShortcutInterrupted,
      CancelReason::kApplicationSwitched,
      CancelReason::kApplicationResignedActive,
      CancelReason::kDisplayConfigurationChanged,
      CancelReason::kInputMonitorInterrupted,
  };
  for (const CancelReason reason : reasons) {
    seethis::core::InteractionController controller;
    Check(controller.ShortcutKeyDown(10, LogicalPoint(8, 1, 2)) ==
              seethis::core::StartResult::kStarted,
          "cancellation setup starts");
    Check(controller.Cancel(reason), "active drawing accepts cancellation");
    const auto snapshot = controller.Snapshot();
    Check(snapshot.state == seethis::core::InteractionState::kIdle,
          "cancellation restores idle state");
    Check(!snapshot.captures_pointer(), "cancellation releases pointer capture");
    Check(snapshot.regions.empty(), "cancelled regions are discarded");
    Check(snapshot.cancel_reason == reason,
        "cancellation reason remains observable");
    Check(std::string_view(seethis::core::CancelReasonName(reason)) != "none" &&
              std::string_view(
                  seethis::core::CancelReasonDescription(reason)) != "none",
          "each true cancellation has bounded diagnostic and user text");
  }
}

void TestCaptureReleaseOrders() {
  using seethis::core::CancelReason;
  using seethis::core::CompletionReason;
  using seethis::core::InteractionState;

  seethis::core::InteractionController key_first;
  (void)key_first.ShortcutKeyDown(100, LogicalPoint(1, 2, 3));
  DrawRegion(key_first,LogicalPoint(1,2,3),LogicalPoint(1,12,13));
  const int key_first_completions =
      static_cast<int>(key_first.ShortcutKeyUp(150)) +
      static_cast<int>(key_first.ShortcutModifierUp(151));
  auto snapshot = key_first.Snapshot();
  Check(key_first_completions == 1 && snapshot.state == InteractionState::kFinished &&
            snapshot.completion_reason == CompletionReason::kShortcutKeyReleased &&
            snapshot.cancel_reason == CancelReason::kNone && snapshot.regions.size() == 1,
        "A-before-Option completes one valid path exactly once");

  seethis::core::InteractionController modifier_first;
  (void)modifier_first.ShortcutKeyDown(200, LogicalPoint(1, 2, 3));
  DrawRegion(modifier_first,LogicalPoint(1,2,3),LogicalPoint(1,12,13));
  const int modifier_first_completions =
      static_cast<int>(modifier_first.ShortcutModifierUp(250)) +
      static_cast<int>(modifier_first.ShortcutKeyUp(251));
  snapshot = modifier_first.Snapshot();
  Check(modifier_first_completions == 1 &&
            snapshot.state == InteractionState::kFinished &&
            snapshot.completion_reason == CompletionReason::kModifierReleased &&
            snapshot.cancel_reason == CancelReason::kNone && snapshot.regions.size() == 1,
        "Option-before-A completes one valid path exactly once");

  seethis::core::InteractionController nearly_simultaneous;
  (void)nearly_simultaneous.ShortcutKeyDown(300, LogicalPoint(1, 2, 3));
  DrawRegion(nearly_simultaneous,LogicalPoint(1,2,3),LogicalPoint(1,12,13));
  const int near_completions =
      static_cast<int>(nearly_simultaneous.ShortcutModifierUp(350)) +
      static_cast<int>(nearly_simultaneous.ShortcutKeyUp(350));
  Check(near_completions == 1 &&
            nearly_simultaneous.Snapshot().state == InteractionState::kFinished,
        "near-simultaneous release observations still complete only once");

  seethis::core::InteractionController repeated;
  (void)repeated.ShortcutKeyDown(400, LogicalPoint(1, 2, 3));
  DrawRegion(repeated,LogicalPoint(1,2,3),LogicalPoint(1,5,6));
  Check(repeated.ShortcutKeyDown(401, LogicalPoint(1, 3, 4)) ==
            seethis::core::StartResult::kIgnoredRepeat &&
            repeated.ShortcutModifierUp(450) &&
            !repeated.ShortcutKeyUp(451) &&
            repeated.ShortcutKeyDown(452, LogicalPoint(1, 4, 5), true) ==
                seethis::core::StartResult::kIgnoredRepeat &&
            repeated.Snapshot().state == InteractionState::kFinished,
        "autorepeat does not rearm or duplicate modifier-first completion");
}

void TestWatchdog() {
  seethis::core::InteractionController controller;
  (void)controller.ShortcutKeyDown(1'000, LogicalPoint(1, 2, 3));
  DrawRegion(controller,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  Check(!controller.Watchdog(1'100, false) &&
            controller.Snapshot().state ==
                seethis::core::InteractionState::kDrawing &&
            controller.Snapshot().regions.size() == 1,
        "one physical-up sample starts bounded release reconciliation");
  Check(controller.ShortcutKeyUp(1'101) &&
            controller.Snapshot().state ==
                seethis::core::InteractionState::kFinished &&
            controller.Snapshot().regions.size() == 1,
        "queued key-up after watchdog observation retains and commits the region");
  Check(!controller.ShortcutModifierUp(1'102),
        "the later modifier callback cannot duplicate the committed reference");

  (void)controller.ShortcutKeyDown(1'200, LogicalPoint(1, 2, 3));
  DrawRegion(controller,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  Check(!controller.Watchdog(1'300, false) &&
            !controller.Watchdog(
                1'300 + seethis::core::InteractionController::
                    kReleaseReconciliationMilliseconds - 1,
                false),
        "reconciliation remains open through the bounded callback drain");
  Check(controller.Watchdog(
            1'300 + seethis::core::InteractionController::
                kReleaseReconciliationMilliseconds,
            false) &&
            controller.Snapshot().state ==
                seethis::core::InteractionState::kFinished &&
            controller.Snapshot().completion_reason ==
                seethis::core::CompletionReason::kWatchdogObservedRelease,
        "valid retained work commits when the queued release never arrives");

  seethis::core::InteractionController transient;
  (void)transient.ShortcutKeyDown(1'350, LogicalPoint(1, 2, 3));
  DrawRegion(transient,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  Check(!transient.Watchdog(1'400, false) &&
            !transient.Watchdog(1'450, true) &&
            !transient.Watchdog(1'600, false) &&
            transient.Snapshot().state ==
                seethis::core::InteractionState::kDrawing,
        "a false-down-false sample sequence restarts the reconciliation window");
  Check(transient.Watchdog(
            1'600 + seethis::core::InteractionController::
                kReleaseReconciliationMilliseconds,
            false) &&
            transient.Snapshot().state ==
                seethis::core::InteractionState::kFinished,
        "the restarted reconciliation window eventually commits once");

  seethis::core::InteractionController modifier_watchdog;
  (void)modifier_watchdog.ShortcutKeyDown(1'700, LogicalPoint(1, 2, 3));
  DrawRegion(modifier_watchdog,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  CaptureAdapterEventWitness modifier_adapter(modifier_watchdog);
  Check(!modifier_adapter.PhysicalSample(1'800, true, 30'000, false) &&
            !modifier_adapter.PhysicalSample(
                1'800 + seethis::core::InteractionController::
                    kReleaseReconciliationMilliseconds - 1,
                true, 30'000, false),
        "modifier-up evidence starts bounded capture release recovery while A remains down");
  Check(modifier_adapter.PhysicalSample(
            1'800 + seethis::core::InteractionController::
                kReleaseReconciliationMilliseconds,
            true, 30'000, false) &&
            modifier_watchdog.Snapshot().state ==
                seethis::core::InteractionState::kFinished &&
            modifier_watchdog.Snapshot().completion_reason ==
                seethis::core::CompletionReason::kWatchdogObservedModifierRelease,
        "stable modifier-up fallback completes one valid drawing exactly once");
  Check(!modifier_adapter.PhysicalSample(2'100, true, 30'000, false),
        "completed modifier-up recovery is idempotent under repeated polls");
  (void)modifier_watchdog.ShortcutKeyDown(2'200, LogicalPoint(1, 2, 3));
  DrawRegion(modifier_watchdog,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  Check(modifier_watchdog.ShortcutKeyUp(2'201) &&
            modifier_watchdog.Snapshot().state ==
                seethis::core::InteractionState::kFinished,
        "a later valid capture remains available after modifier-up recovery");

  seethis::core::InteractionController extra_modifier;
  (void)extra_modifier.ShortcutKeyDown(2'250, LogicalPoint(1, 2, 3));
  DrawRegion(extra_modifier,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  Check(!extra_modifier.Watchdog(2'350, true, 30'000, true) &&
            !extra_modifier.Watchdog(
                2'350 + seethis::core::InteractionController::
                    kReleaseReconciliationMilliseconds,
                true, 30'000, true) &&
            extra_modifier.Snapshot().state ==
                seethis::core::InteractionState::kDrawing,
        "extra modifiers do not masquerade as release while the required modifier remains held");

  seethis::core::InteractionController empty_modifier_watchdog;
  (void)empty_modifier_watchdog.ShortcutKeyDown(2'300, LogicalPoint(1, 2, 3));
  Check(!empty_modifier_watchdog.Watchdog(2'400, true, 30'000, false) &&
            empty_modifier_watchdog.Watchdog(
                2'400 + seethis::core::InteractionController::
                    kReleaseReconciliationMilliseconds,
                true, 30'000, false) &&
            empty_modifier_watchdog.Snapshot().state ==
                seethis::core::InteractionState::kIdle &&
            empty_modifier_watchdog.Snapshot().cancel_reason ==
                seethis::core::CancelReason::kLostKeyUp,
        "empty modifier-up fallback cancels truthfully without committing");

  seethis::core::InteractionController empty;
  (void)empty.ShortcutKeyDown(1'500, LogicalPoint(1, 2, 3));
  Check(!empty.Watchdog(1'600, false) &&
            empty.Watchdog(
                1'600 + seethis::core::InteractionController::
                    kReleaseReconciliationMilliseconds,
                false) &&
            empty.Snapshot().state ==
                seethis::core::InteractionState::kIdle &&
            empty.Snapshot().cancel_reason ==
                seethis::core::CancelReason::kLostKeyUp,
        "an empty stuck session still escapes as a named lost-key cancellation");

  (void)controller.ShortcutKeyDown(2'000, LogicalPoint(1, 2, 3));
  Check(!controller.Watchdog(2'100, true, 500),
        "watchdog permits a bounded active hold");
  Check(controller.Watchdog(2'501, true, 500),
        "maximum hold prevents indefinite capture even with stale key state");
  Check(controller.Snapshot().cancel_reason ==
            seethis::core::CancelReason::kWatchdogTimeout,
        "timeout reason remains observable");

  seethis::core::InteractionController timeout_release;
  (void)timeout_release.ShortcutKeyDown(3'000, LogicalPoint(1, 2, 3));
  DrawRegion(timeout_release,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  Check(timeout_release.Watchdog(3'501, false, 500) &&
            timeout_release.Snapshot().cancel_reason ==
                seethis::core::CancelReason::kWatchdogTimeout,
        "maximum hold wins over a late physical-up observation");

  seethis::core::InteractionController adapter_order;
  (void)adapter_order.ShortcutKeyDown(4'000, LogicalPoint(1, 2, 3));
  DrawRegion(adapter_order,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  CaptureAdapterEventWitness adapter(adapter_order);
  Check(!adapter.PhysicalSample(4'100, false) &&
            adapter.QueuedModifierUp(4'101) &&
            !adapter.QueuedKeyUp(4'102) &&
            adapter_order.Snapshot().completion_reason ==
                seethis::core::CompletionReason::kModifierReleased,
        "adapter ordering commits Option-first and drains delayed A-up once");

  seethis::core::InteractionController stale_press;
  (void)stale_press.ShortcutKeyDown(4'200, LogicalPoint(1, 2, 3));
  DrawRegion(stale_press,LogicalPoint(1,2,3),LogicalPoint(1,4,5));
  Check(adapter_order.Snapshot().state ==
            seethis::core::InteractionState::kFinished &&
            stale_press.Snapshot().state ==
                seethis::core::InteractionState::kDrawing,
        "a subsequent session remains independent after delayed callback drain");
  seethis::platform::CaptureShortcutEligibility eligibility;
  eligibility.ObserveBoundary({2'000, 2'010, true});
  Check(!eligibility.AcceptPress(true, true, {1'990, 2'000, true}) &&
            !eligibility.AcceptPress(false, true, {2'020, 2'030, true}) &&
            eligibility.AcceptPress(true, true, {2'020, 2'030, true}),
        "adapter eligibility rejects stale or physically incomplete Carbon presses");

  const auto input_source = std::filesystem::path(__FILE__).parent_path()
                                .parent_path() / "src/platform/mac/input_mac.mm";
  std::ifstream input(input_source);
  const std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  Check(input.is_open() && source.find("kEventHotKeyExclusive") !=
            std::string::npos && source.find("RegisterCaptureHotKey") !=
            std::string::npos,
        "capture shortcut is explicitly reserved through exclusive Carbon ownership");
  Check(source.find("!capture_registered_ && type == kCGEventKeyDown") !=
            std::string::npos && source.find("kCGEventTapOptionListenOnly") !=
            std::string::npos,
        "raw observation cannot start an unowned capture chord or suppress ordinary typing");
  Check(source.find("watchdog_before.regions.size()") != std::string::npos &&
            source.find("watchdog-observed-key-release") != std::string::npos &&
            source.find("watchdog-observed-modifier-release") !=
                std::string::npos,
        "native diagnostics preserve release reason, pre-cancel region count and watchdog timing");
  Check(source.find("CapturePressEligible(carbon_provenance)") !=
            std::string::npos && source.find("CaptureKeyIsDown()") !=
            std::string::npos && source.find("CaptureModifiersAreExact") !=
            std::string::npos,
        "queued Carbon capture presses require current physical exact-chord evidence");
  Check(source.find("CapturePressEligible(carbon_provenance)") !=
            std::string::npos && source.find("GetEventTime(event)") !=
            std::string::npos && source.find("timestamp ==") ==
            std::string::npos,
        "native callback ordering uses interval provenance instead of timestamp equality");
  Check(source.find("NSRunLoopCommonModes") != std::string::npos &&
            source.find("timerWithTimeInterval:0.10") != std::string::npos,
        "bounded recovery timer runs in normal and event-tracking run-loop modes");
  Check(source.find("shortcut_modifiers_present") != std::string::npos &&
            source.find("(physical_flags & active_capture_modifiers_)") !=
                std::string::npos,
        "native watchdog wiring supplies physical key and required capture modifiers");
  Check(source.find("RawEventProvenance(event)") != std::string::npos &&
            source.find("HotKeyReleased(self->delete_generation_,") !=
                std::string::npos,
        "native raw and Carbon release callbacks retain event provenance across one hold");
  Check(source.find("physical-bindings") == std::string::npos,
        "native shortcut diagnostics do not log key content or capabilities");
  Check(seethis::platform::CaptureShortcutReadiness(true, false, true, false) ==
            seethis::platform::CaptureShortcutState::kInputUnavailable &&
            seethis::platform::CaptureShortcutReadiness(true, false, true, true) ==
            seethis::platform::CaptureShortcutState::kReady,
        "capture registration success stays unavailable until keyboard input recovers");
  Check(seethis::platform::CaptureShortcutReadiness(true, true, true, true) ==
            seethis::platform::CaptureShortcutState::kConflict &&
            seethis::platform::CaptureShortcutReadiness(true, false, false, true) ==
            seethis::platform::CaptureShortcutState::kRegistrationFailed,
        "capture conflict and registration failure retain their truthful reasons");
  Check(source.find("InstallTap(true);\n    RegisterCaptureHotKey();\n    RegisterDeleteHotKey();") !=
            std::string::npos &&
            source.find("readiness_->input_monitor.store(seethis::platform::ClassifyInputMonitor(\n          {.retry_attempt = retry_attempt}));\n      RefreshCaptureReadiness();\n      return false;") !=
            std::string::npos,
        "startup denial and retry route through the production capture readiness refresh");
}

void TestGeometryAndScale() {
  const seethis::core::DisplayGeometry left{
      7, {-1920, 0}, {1920, 1080}, 1.0};
  const seethis::core::DisplayGeometry retina{
      42, {0, 0}, {1512, 982}, 2.0};
  Check(left.IsValid() && retina.IsValid(),
        "declared multi-display geometries are valid");

  const auto left_local = left.FromGlobalLogical({-100, 500});
  Check(left_local.has_value() && left_local->display_id == 7 &&
            left_local->position.x == 1820 && left_local->position.y == 500,
        "negative global origin maps to display-relative logical points");
  const auto retina_local = retina.FromGlobalLogical({100.25, 40.5});
  Check(retina_local.has_value() && retina_local->backing_scale == 2.0,
        "retina sample declares its backing scale");
  const auto pixels = seethis::core::ConvertCoordinateUnit(
      *retina_local, seethis::core::CoordinateUnit::kBackingPixels);
  Check(pixels.has_value() && pixels->position.x == 200.5 &&
            pixels->position.y == 81.0,
        "logical points convert to backing pixels explicitly");
  const auto logical = seethis::core::ConvertCoordinateUnit(
      *pixels, seethis::core::CoordinateUnit::kLogicalPoints);
  Check(logical.has_value() && logical->position.x == 100.25 &&
            logical->position.y == 40.5,
        "backing pixels round-trip to logical points");
  Check(!left.FromGlobalLogical({0, 20}).has_value(),
        "adjacent display boundary does not alias display identity");
}

void TestValidationAndHitRegions() {
  seethis::core::InteractionController controller;
  auto pixels = LogicalPoint(1, 1, 1, 2);
  pixels.unit = seethis::core::CoordinateUnit::kBackingPixels;
  Check(controller.ShortcutKeyDown(1, pixels) ==
            seethis::core::StartResult::kInvalidPoint,
        "interaction input requires declared logical units");
  Check(controller.ShortcutKeyDown(2, LogicalPoint(1, 0, 0, 2)) ==
            seethis::core::StartResult::kStarted,
        "valid logical input starts");
  DrawRegion(controller,LogicalPoint(1,0,0,2),LogicalPoint(1,100,10,2));
  (void)controller.ShortcutKeyUp(3);
  Check(controller.HitTestFinishedMark(LogicalPoint(1, 50, 5, 2), 6),
        "finished path exposes an explicit segment hit region");
  Check(!controller.HitTestFinishedMark(LogicalPoint(1, 50, 20, 2), 6),
        "empty overlay area is outside the explicit mark hit region");
  Check(!controller.HitTestFinishedMark(LogicalPoint(2, 50, 0), 6),
        "mark hit testing respects display identity");
}

void TestDeleteGestureReducer() {
  using seethis::core::DeleteGestureController;
  using seethis::core::DeleteGestureState;
  DeleteGestureController deletion;
  deletion.Configure(7, true, true);
  Check(deletion.Snapshot().state == DeleteGestureState::kIdle,
        "registered observed delete chord begins idle");

  DeleteGestureController polled;
  polled.Configure(12, true, true, false, {100, 102, true});
  DeleteAdapterEventWitness polled_adapter(polled);
  polled_adapter.PhysicalPoll(true, true);
  Check(polled.Snapshot().state == DeleteGestureState::kIdle &&
            !polled.Snapshot().key_down,
        "physical poll alone never creates a delete invocation");
  polled_adapter.PhysicalPoll(true, true);
  polled_adapter.PhysicalPoll(true, true);
  Check(polled.Snapshot().state == DeleteGestureState::kIdle &&
            !polled.Snapshot().key_down,
        "repeated physical polls neither auto-arm nor drain a fresh candidate");
  polled_adapter.RawDown(true, false, {200, 202, true});
  Check(polled.Snapshot().state == DeleteGestureState::kCandidate,
        "queued raw down remains the fresh event boundary after a poll");
  polled_adapter.PhysicalPoll(true, true);
  Check(polled.Snapshot().state == DeleteGestureState::kCandidate,
        "poll during a candidate preserves the raw event boundary");
  polled_adapter.CarbonPress(12, true, true, {201, 203, true});
  Check(polled.Snapshot().armed() && polled.ConsumeDeleteClick(true, true),
        "overlapping raw and Carbon intervals still arm one intended action");
  polled_adapter.PhysicalPoll(true, true);
  Check(polled.Snapshot().armed(),
        "repeated poll after arming does not drain the held chord");
  polled_adapter.PhysicalPoll(false, false, {300, 304, true});
  Check(polled.Snapshot().state == DeleteGestureState::kIdle &&
            !polled.Snapshot().key_down,
        "stable physical release clears the delete transient state");

  DeleteGestureController carbon_first;
  carbon_first.Configure(13, true, true);
  carbon_first.HotKeyPressed(13, true, true);
  carbon_first.Reconcile(true, true);
  Check(carbon_first.Snapshot().state == DeleteGestureState::kCandidate,
        "Carbon-first confirmation remains pending until a raw down arrives");
  carbon_first.KeyDown(true, false);
  Check(carbon_first.Snapshot().armed(),
        "Carbon-first ordering arms when the fresh raw down arrives");
  carbon_first.HotKeyReleased(13);
  carbon_first.Reconcile(true, true);
  Check(carbon_first.Snapshot().state == DeleteGestureState::kDrain,
        "Carbon release enters a bounded drain until physical release");
  carbon_first.Reconcile(false, false);
  Check(carbon_first.Snapshot().state == DeleteGestureState::kIdle,
        "release recovery clears Carbon-first drain state");

  DeleteGestureController carbon_first_overlap;
  carbon_first_overlap.Configure(15, true, true, false, {100, 102, true});
  carbon_first_overlap.HotKeyPressed(15, true, true, {201, 203, true});
  carbon_first_overlap.KeyDown(true, false, {200, 202, true});
  Check(carbon_first_overlap.Snapshot().armed(),
        "Carbon-first delivery accepts overlapping raw and Carbon press intervals");
  carbon_first_overlap.HotKeyReleased(15, {204, 206, true});
  carbon_first_overlap.Reconcile(false, false, {207, 209, true});
  Check(carbon_first_overlap.Snapshot().state == DeleteGestureState::kIdle,
        "Carbon-first release recovery clears the proven hold");

  DeleteGestureController carbon_first_stale_raw;
  carbon_first_stale_raw.Configure(16, true, true, false, {100, 102, true});
  carbon_first_stale_raw.HotKeyPressed(16, true, true, {900, 902, true});
  carbon_first_stale_raw.KeyDown(true, false, {800, 802, true});
  Check(carbon_first_stale_raw.Snapshot().state == DeleteGestureState::kCandidate &&
            !carbon_first_stale_raw.Snapshot().key_down,
        "definitely older raw input cannot arm a current Carbon-first hold");
  carbon_first_stale_raw.KeyDown(true, false);
  Check(carbon_first_stale_raw.Snapshot().state == DeleteGestureState::kCandidate &&
            !carbon_first_stale_raw.Snapshot().key_down,
        "missing raw provenance fails closed after required Carbon provenance");
  carbon_first_stale_raw.KeyDown(true, false, {900, 902, true});
  Check(carbon_first_stale_raw.Snapshot().armed(),
        "current raw observation can still arm the Carbon-first hold");
  carbon_first_stale_raw.KeyUp({910, 912, true});

  DeleteGestureController required_raw_first;
  required_raw_first.Configure(17, true, true, false, {390, 392, true});
  required_raw_first.KeyDown(true, false);
  const auto missing_raw_first = required_raw_first.Snapshot();
  required_raw_first.HotKeyPressed(17, true, true, {400, 402, true});
  Check(!missing_raw_first.key_down &&
            !required_raw_first.Snapshot().armed(),
        "required raw-first delivery rejects missing provenance before Carbon");
  required_raw_first.KeyDown(true, false, {0, 0, true});
  const auto invalid_raw_first = required_raw_first.Snapshot();
  Check(!invalid_raw_first.key_down &&
            !required_raw_first.Snapshot().armed(),
        "required raw-first delivery rejects invalid provenance before Carbon");
  required_raw_first.KeyDown(true, false, {400, 402, true});
  Check(required_raw_first.Snapshot().armed(),
        "equal current raw and Carbon intervals arm after rejected raw callbacks");
  required_raw_first.KeyUp({410, 412, true});

  DeleteGestureController stale_raw_first;
  stale_raw_first.Configure(18, true, true, false, {500, 502, true});
  stale_raw_first.KeyDown(true, false, {500, 502, true});
  const auto boundary_raw_first = stale_raw_first.Snapshot();
  stale_raw_first.HotKeyPressed(18, true, true, {600, 602, true});
  Check(!boundary_raw_first.key_down &&
            stale_raw_first.Snapshot().state == DeleteGestureState::kCandidate &&
            !stale_raw_first.Snapshot().armed(),
        "raw-first event at the release boundary cannot arm with fresh Carbon");
  stale_raw_first.KeyDown(true, false, {599, 601, true});
  Check(stale_raw_first.Snapshot().armed(),
        "overlapping current raw and Carbon intervals still arm after stale raw rejection");
  stale_raw_first.KeyUp({610, 612, true});

  DeleteGestureController invalid_required_boundary;
  invalid_required_boundary.Configure(19, true, true, false, {0, 0, true});
  invalid_required_boundary.KeyDown(true, false, {700, 702, true});
  invalid_required_boundary.HotKeyPressed(19, true, true, {700, 702, true});
  Check(invalid_required_boundary.Snapshot().state ==
            DeleteGestureState::kIdle &&
            !invalid_required_boundary.Snapshot().key_down,
        "required session fails closed until it has a valid eligibility boundary");

  DeleteGestureController legacy_never_required;
  legacy_never_required.Configure(20, true, true);
  legacy_never_required.KeyDown(true, false);
  legacy_never_required.HotKeyPressed(20, true, true);
  Check(legacy_never_required.Snapshot().armed(),
        "timestamp-free compatibility remains for a never-required controller");
  legacy_never_required.KeyUp();

  DeleteGestureController stale;
  const auto stale_event = [](std::uint64_t time) {
    return seethis::core::DeleteEventProvenance{time, time + 2, true};
  };
  stale.Configure(14, true, true, false, stale_event(100));
  stale.KeyDown(true, false, stale_event(200));
  stale.HotKeyPressed(14, true, true, stale_event(210));
  Check(stale.Snapshot().armed(), "stale-event fixture starts from one armed hold");
  stale.KeyUp(stale_event(220));
  stale.KeyDown(true, false, stale_event(300));
  stale.HotKeyPressed(14, true, true, stale_event(150));
  Check(stale.Snapshot().state == DeleteGestureState::kCandidate,
        "an old same-generation Carbon press cannot arm a new raw hold");
  stale.HotKeyPressed(14, true, true, stale_event(305));
  Check(stale.Snapshot().armed(),
        "a new same-generation hold accepts its current Carbon press");
  stale.HotKeyReleased(14, stale_event(150));
  Check(stale.Snapshot().armed(),
        "old same-generation Carbon press and release cannot terminate a new hold");
  stale.Reconcile(false, false, stale_event(330));
  Check(stale.Snapshot().state == DeleteGestureState::kIdle,
        "physical release recovery clears the new hold safely");

  deletion.KeyDown(true, false);
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate &&
            deletion.Snapshot().blocks_mark_copy(),
        "raw chord candidate blocks accidental ordinary copy");
  deletion.HotKeyPressed(7, true, true);
  Check(deletion.Snapshot().armed(),
        "matching Carbon confirmation arms raw-first chord");
  deletion.KeyDown(true, true);
  deletion.HotKeyPressed(7, true, true);
  deletion.KeyDown(true, true);
  Check(deletion.Snapshot().armed(),
        "long-hold raw and Carbon repeats are idempotent while armed");
  Check(deletion.ConsumeDeleteClick(true, true),
        "first deliberate click is accepted while the hold is armed");
  Check(deletion.Snapshot().armed() &&
            deletion.ConsumeDeleteClick(true, true) &&
            deletion.ConsumeDeleteClick(true, true),
        "three deliberate clicks remain independently deletable in one hold");
  deletion.KeyDown(true, true);
  deletion.HotKeyPressed(7, true, true);
  Check(deletion.Snapshot().armed() && deletion.ConsumeDeleteClick(true, true),
        "autorepeat and duplicate Carbon notifications do not disarm the hold");
  deletion.KeyUp();
  Check(deletion.Snapshot().state == DeleteGestureState::kIdle,
        "D-up exits the continuous delete hold");

  deletion.KeyDown(true, false);
  deletion.HotKeyPressed(7, true, true);
  Check(!deletion.ConsumeDeleteClick(true, false) &&
            deletion.Snapshot().state == DeleteGestureState::kDrain,
        "modifier mismatch at click drains without deletion");
  deletion.KeyUp();

  deletion.HotKeyPressed(7, true, true);
  deletion.KeyDown(true, false);
  Check(deletion.Snapshot().armed(),
        "Carbon-before-raw ordering reaches the same armed state");
  deletion.ModifiersChanged(false);
  Check(deletion.Snapshot().state == DeleteGestureState::kDrain,
        "Option-first release exits immediately and drains held D");
  deletion.ModifiersChanged(true);
  deletion.KeyDown(true, true);
  deletion.HotKeyPressed(7, true, true);
  Check(deletion.Snapshot().state == DeleteGestureState::kDrain,
        "Option re-press and D autorepeat cannot rearm before D-up");
  deletion.KeyUp();

  deletion.KeyDown(true, false);
  deletion.HotKeyPressed(7, true, true);
  deletion.ObservationReadinessChanged(true, true);
  Check(deletion.Snapshot().armed(),
        "periodic ready refresh preserves an armed invocation");
  deletion.HotKeyReleased(7);
  Check(deletion.Snapshot().state == DeleteGestureState::kDrain &&
            !deletion.ConsumeDeleteClick(true, true),
        "Carbon release before raw D-up safely drains the held chord");
  deletion.KeyDown(true, true);
  deletion.HotKeyPressed(7, true, true);
  Check(deletion.Snapshot().state == DeleteGestureState::kDrain &&
            !deletion.ConsumeDeleteClick(true, true),
        "Carbon duplicate press cannot rearm after release");
  deletion.KeyUp();
  deletion.KeyDown(true, true);
  deletion.HotKeyPressed(7, true, true);
  Check(deletion.Snapshot().state == DeleteGestureState::kDrain &&
            !deletion.ConsumeDeleteClick(true, true),
        "idle autorepeat cannot create a fresh invocation");
  deletion.KeyUp();
  deletion.HotKeyPressed(6, true, true);
  Check(deletion.Snapshot().state == DeleteGestureState::kIdle,
        "stale registration generation cannot arm");

  deletion.HotKeyPressed(7, false, false);
  Check(deletion.Snapshot().state == DeleteGestureState::kIdle,
        "delayed same-generation Carbon press after raw D-up is ignored");
  deletion.KeyDown(true, false);
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate &&
            !deletion.Snapshot().armed(),
        "fresh raw hold cannot reuse stale Carbon confirmation");
  deletion.HotKeyPressed(7, true, true);
  Check(deletion.Snapshot().armed() &&
            deletion.ConsumeDeleteClick(true, true),
        "current Carbon confirmation is required before deletion");
  deletion.KeyUp();

  const auto observed = [](std::uint64_t earliest, std::uint64_t latest) {
    return seethis::core::DeleteEventProvenance{
        earliest, latest, true};
  };
  constexpr std::uint64_t current_uptime = 3'608'690'000'000'000ULL;
  deletion.Configure(9, true, true, false,
                     observed(current_uptime - 1, current_uptime + 1));
  deletion.KeyDown(true, false,
                   observed(current_uptime + 99, current_uptime + 101));
  deletion.HotKeyPressed(9, true, true,
                         observed(current_uptime + 100,
                                  current_uptime + 102));
  Check(deletion.Snapshot().armed(),
        "current-uptime raw-first confirmation needs ordering, not equality");
  deletion.KeyUp(observed(current_uptime + 200,
                          current_uptime + 202));
  deletion.HotKeyPressed(9, false, false,
                         observed(current_uptime + 100,
                                  current_uptime + 102));
  deletion.KeyDown(true, false,
                   observed(current_uptime + 300, current_uptime + 302));
  deletion.HotKeyPressed(9, true, true,
                         observed(current_uptime + 100,
                                  current_uptime + 102));
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate &&
            !deletion.Snapshot().armed(),
        "old Carbon press stays behind the raw-up eligibility boundary");
  deletion.HotKeyPressed(9, true, true,
                         observed(current_uptime + 201,
                                  current_uptime + 203));
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate &&
            !deletion.Snapshot().armed(),
        "boundary-overlapping old press is ambiguous and fails closed");
  deletion.HotKeyPressed(9, true, true,
                         observed(current_uptime + 300,
                                  current_uptime + 302));
  Check(deletion.Snapshot().armed(),
        "new hold accepts a later Carbon press without exact timestamp match");
  deletion.KeyUp(observed(current_uptime + 400,
                          current_uptime + 402));
  deletion.HotKeyPressed(9, true, true,
                         observed(current_uptime + 500,
                                  current_uptime + 502));
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate,
        "ordered Carbon-first confirmation waits for raw D-down");
  deletion.KeyDown(true, false,
                   observed(current_uptime + 500, current_uptime + 502));
  Check(deletion.Snapshot().armed(),
        "ordered Carbon-first delivery arms without cross-API equality");
  deletion.KeyUp(observed(current_uptime + 600,
                          current_uptime + 602));

  constexpr std::uint64_t long_uptime =
      365ULL * 86'400ULL * 1'000'000'000ULL;
  const auto converted_long_uptime =
      seethis::core::DeleteEventProvenanceFromSeconds(
          static_cast<double>(long_uptime) / 1'000'000'000.0);
  const auto above_signed_range =
      seethis::core::DeleteEventProvenanceFromSeconds(
          static_cast<double>(
              static_cast<long double>(std::numeric_limits<long long>::max()) /
                  1'000'000'000.0L +
              1.0L));
  const auto represented_signed_limit =
      seethis::core::DeleteEventProvenanceFromSeconds(
          static_cast<double>(std::numeric_limits<long long>::max()) /
          1'000'000'000.0);
  Check(converted_long_uptime.valid() &&
            converted_long_uptime.latest_ns -
                    converted_long_uptime.earliest_ns <=
                8,
        "365-day Carbon conversion exposes only its narrow ULP interval");
  Check(!seethis::core::DeleteEventProvenanceFromSeconds(0).valid() &&
            !seethis::core::DeleteEventProvenanceFromSeconds(
                 std::numeric_limits<double>::quiet_NaN()).valid() &&
            !seethis::core::DeleteEventProvenanceFromSeconds(
                 std::numeric_limits<double>::infinity()).valid() &&
            represented_signed_limit.required &&
            !represented_signed_limit.valid() &&
            above_signed_range.required && !above_signed_range.valid(),
        "invalid and signed-llround-out-of-range Carbon times fail closed");
  deletion.Configure(10, true, true, false,
                     observed(long_uptime - 3, long_uptime + 3));
  deletion.KeyDown(true, false,
                   observed(long_uptime + 9, long_uptime + 15));
  deletion.HotKeyPressed(10, true, true,
                         observed(long_uptime - 9, long_uptime - 3));
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate,
        "definitely old long-uptime interval is rejected at the boundary");
  deletion.HotKeyPressed(10, true, true,
                         observed(long_uptime + 1, long_uptime + 7));
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate,
        "long-uptime ULP overlap fails closed as ambiguous provenance");
  deletion.HotKeyPressed(10, true, true,
                         observed(long_uptime + 10,
                                  long_uptime + 16));
  Check(deletion.Snapshot().armed(),
        "provably later long-uptime interval arms without a broad window");
  deletion.KeyUp(observed(long_uptime + 100,
                          long_uptime + 106));
  deletion.HotKeyPressed(10, true, true,
                         observed(long_uptime + 110,
                                  long_uptime + 116));
  deletion.KeyDown(true, false,
                   observed(long_uptime + 110,
                            long_uptime + 116));
  Check(deletion.Snapshot().armed(),
        "long-uptime Carbon-first order survives double quantization");
  deletion.KeyUp(observed(long_uptime + 200,
                          long_uptime + 206));

  deletion.ObservationReadinessChanged(
      false, false,
      observed(long_uptime + 300, long_uptime + 306));
  deletion.ObservationReadinessChanged(
      true, false,
      observed(long_uptime + 400, long_uptime + 406));
  deletion.KeyDown(true, false,
                   observed(long_uptime + 410, long_uptime + 416));
  deletion.HotKeyPressed(10, true, true,
                         observed(long_uptime + 350,
                                  long_uptime + 356));
  Check(deletion.Snapshot().state == DeleteGestureState::kCandidate,
        "readiness recovery boundary rejects callbacks queued while unavailable");
  deletion.HotKeyPressed(10, true, true,
                         observed(long_uptime + 410,
                                  long_uptime + 416));
  Check(deletion.Snapshot().armed(),
        "readiness recovery accepts a press proven later than restoration");
  deletion.KeyUp(observed(long_uptime + 500,
                          long_uptime + 506));

  deletion.Configure(11, true, true, false, {0, 0, true});
  deletion.KeyDown(true, false);
  deletion.HotKeyPressed(11, true, true, {0, 0, true});
  Check(deletion.Snapshot().state == DeleteGestureState::kIdle &&
            !deletion.Snapshot().key_down &&
            !deletion.ConsumeDeleteClick(true, true),
        "unavailable native provenance cannot enter synthetic compatibility");

  deletion.KeyDown(false, false);
  deletion.ModifiersChanged(true);
  deletion.HotKeyPressed(7, true, true);
  Check(!deletion.Snapshot().armed(),
        "D held before Option cannot become a delete invocation");
  deletion.KeyUp();
  deletion.KeyDown(true, false);
  deletion.HotKeyPressed(7, true, true);
  deletion.Interrupt(true);
  Check(deletion.Snapshot().state == DeleteGestureState::kDrain &&
            !deletion.ConsumeDeleteClick(true, true),
        "lost raw key-up drains the physically held chord without deleting");
  deletion.KeyUp();
  deletion.ObservationReadinessChanged(false, false);
  Check(deletion.Snapshot().state == DeleteGestureState::kUnavailable,
        "observer loss makes deletion unavailable");
  deletion.Configure(8, false, true);
  deletion.KeyDown(true, false);
  deletion.HotKeyPressed(8, true, true);
  Check(!deletion.Snapshot().armed(),
        "registration conflict cannot fall back to raw observation");
  Check(std::string_view(seethis::core::DeleteGestureStateName(
            deletion.Snapshot().state)) == "unavailable",
        "native diagnostics name the unavailable reducer state");
}

void TestReferenceClipboardPolicy() {
  using seethis::platform::PlanReferenceClipboardWrite;
  using seethis::platform::ReferenceClipboardTrigger;
  constexpr auto deliberate = PlanReferenceClipboardWrite(
      ReferenceClipboardTrigger::kDeliberatePublication);
  Check(deliberate.writes_pasteboard && deliberate.plain_text_only &&
            deliberate.representation_count == 1 &&
            deliberate.proves_content_present,
        "deliberate reference publication writes exactly one text representation");
  constexpr auto completion = PlanReferenceClipboardWrite(
      ReferenceClipboardTrigger::kAsyncCompletion);
  Check(!completion.writes_pasteboard && !completion.plain_text_only &&
            completion.representation_count == 0 &&
            !completion.proves_content_present,
        "async completion neither writes nor proves current pasteboard content");
}

#if defined(__APPLE__) && defined(SEETHIS_CLIPBOARD_MAC_TESTING)
seethis::platform::AnnotatedCopyResult AwaitAnnotatedCopy(
    const std::shared_ptr<const seethis::core::Bytes>& png,
    std::chrono::milliseconds timeout, const std::string& board_name,
    std::chrono::milliseconds block_main = std::chrono::milliseconds::zero()) {
  using seethis::platform::AnnotatedCopyResult;
  using seethis::platform::AnnotatedCopyStatus;
  bool finished = false;
  AnnotatedCopyResult result;
  seethis::platform::CopyAnnotatedPng(
      png, timeout,
      [&](AnnotatedCopyResult value) { result = value; finished = true; },
      board_name);
  if (block_main > std::chrono::milliseconds::zero())
    std::this_thread::sleep_for(block_main);
  const auto deadline = [NSDate dateWithTimeIntervalSinceNow:7];
  while (!finished && [deadline timeIntervalSinceNow] > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
  Check(finished, "annotated copy completion arrives on the main run loop");
  if (!finished) result.status = AnnotatedCopyStatus::kTimedOut;
  return result;
}

void TestAnnotatedClipboardOnNamedPasteboard() {
  using seethis::platform::AnnotatedCopyStatus;
  const auto board_name = "seethis-annotated-copy-test-" +
                          std::to_string(getpid());
  NSPasteboard* board = [NSPasteboard pasteboardWithName:
      [NSString stringWithUTF8String:board_name.c_str()]];
  Check(board != nil, "disposable named pasteboard is available");
  if (!board) return;
  [board clearContents];
  [board setString:@"http://127.0.0.1:1234/r/stale" forType:NSPasteboardTypeString];
  seethis::core::Pixels small;
  small.width = 2;
  small.height = 2;
  small.rgba.assign(16, 255);
  const auto png = std::make_shared<const seethis::core::Bytes>(
      seethis::core::EncodePng(small));
  const auto copied = AwaitAnnotatedCopy(png, std::chrono::seconds(5), board_name);
  Check(copied.status == AnnotatedCopyStatus::kCopied &&
            copied.change_count && copied.width == 2 && copied.height == 2 &&
            !copied.downscaled,
        "actual annotated helper copies an unchanged small PNG");
  Check(board.pasteboardItems.count == 1 &&
            [board.pasteboardItems[0].types isEqualToArray:@[NSPasteboardTypePNG]] &&
            [board stringForType:NSPasteboardTypeString] == nil,
        "successful copy replaces the stale URL with exactly one PNG item");

  [board clearContents];
  [board setString:@"keep-on-timeout" forType:NSPasteboardTypeString];
  const auto before_timeout = board.changeCount;
  const auto timed_out = AwaitAnnotatedCopy(
      png, std::chrono::milliseconds(1), board_name,
      std::chrono::milliseconds(30));
  Check(timed_out.status == AnnotatedCopyStatus::kTimedOut &&
            board.changeCount == before_timeout &&
            [[board stringForType:NSPasteboardTypeString]
                isEqualToString:@"keep-on-timeout"],
        "elapsed deadline leaves the named pasteboard untouched");
  [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                           beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
  Check(board.changeCount == before_timeout,
        "late worker completion cannot publish after timeout");

  const auto invalid_png = std::make_shared<const seethis::core::Bytes>(
      seethis::core::Bytes{1, 2, 3});
  const auto invalid = AwaitAnnotatedCopy(
      invalid_png, std::chrono::seconds(5), board_name);
  Check(invalid.status == AnnotatedCopyStatus::kInvalidPng &&
            board.changeCount == before_timeout,
        "invalid PNG is rejected without a pasteboard write");
  const auto oversize_png = std::make_shared<const seethis::core::Bytes>(
      seethis::core::Bytes(16 * 1024 * 1024 + 1, 0));
  const auto oversized = AwaitAnnotatedCopy(
      oversize_png, std::chrono::seconds(5), board_name);
  Check(oversized.status == AnnotatedCopyStatus::kTooLarge &&
            board.changeCount == before_timeout,
        "compressed input cap rejects before decoding or publication");
  auto oversized_dimensions = *png;
  oversized_dimensions[18] = 32;
  oversized_dimensions[22] = 32;
  const auto oversized_image = AwaitAnnotatedCopy(
      std::make_shared<const seethis::core::Bytes>(
          std::move(oversized_dimensions)),
      std::chrono::seconds(5), board_name);
  Check(oversized_image.status == AnnotatedCopyStatus::kTooLarge &&
            board.changeCount == before_timeout,
        "decoded RGBA cap rejects oversized dimensions before decoding");
  auto invalid_huge_dimensions = *png;
  invalid_huge_dimensions[16] = 0x80;
  invalid_huge_dimensions[17] = invalid_huge_dimensions[18] =
      invalid_huge_dimensions[19] = 0;
  invalid_huge_dimensions[20] = 0x80;
  invalid_huge_dimensions[21] = invalid_huge_dimensions[22] =
      invalid_huge_dimensions[23] = 0;
  const auto invalid_huge = AwaitAnnotatedCopy(
      std::make_shared<const seethis::core::Bytes>(
          std::move(invalid_huge_dimensions)),
      std::chrono::seconds(5), board_name);
  Check(invalid_huge.status == AnnotatedCopyStatus::kInvalidPng &&
            board.changeCount == before_timeout &&
            [[board stringForType:NSPasteboardTypeString]
                isEqualToString:@"keep-on-timeout"],
        "out-of-range IHDR dimensions are rejected before decode without a write");
  auto maximum_png_dimensions = *png;
  maximum_png_dimensions[16] = maximum_png_dimensions[20] = 0x7f;
  for (const auto index : {17, 18, 19, 21, 22, 23})
    maximum_png_dimensions[index] = 0xff;
  const auto maximum_png = AwaitAnnotatedCopy(
      std::make_shared<const seethis::core::Bytes>(
          std::move(maximum_png_dimensions)),
      std::chrono::seconds(5), board_name);
  Check(maximum_png.status == AnnotatedCopyStatus::kTooLarge &&
            board.changeCount == before_timeout,
        "spec-range IHDR dimensions still obey the division-based pixel cap");

  seethis::core::Pixels large;
  large.width = 3000;
  large.height = 1000;
  large.rgba.assign(std::size_t(large.width) * large.height * 4, 128);
  const auto large_png = std::make_shared<const seethis::core::Bytes>(
      seethis::core::EncodePng(large));
  const auto scaled = AwaitAnnotatedCopy(
      large_png, std::chrono::seconds(5), board_name);
  Check(scaled.status == AnnotatedCopyStatus::kCopied && scaled.downscaled &&
            scaled.width == 2048 && scaled.height == 682 &&
            board.pasteboardItems.count == 1 &&
            [board stringForType:NSPasteboardTypeString] == nil,
        "actual helper downscales and publishes a single PNG item");
  NSData* scaled_data = [board dataForType:NSPasteboardTypePNG];
  NSBitmapImageRep* scaled_rep = [[NSBitmapImageRep alloc]
      initWithData:scaled_data];
  Check(scaled_rep.pixelsWide == 2048 && scaled_rep.pixelsHigh == 682,
        "published PNG has the bounded output dimensions");

  [board clearContents];
  [board setString:@"keep-on-change" forType:NSPasteboardTypeString];
  bool changed_finished = false;
  seethis::platform::AnnotatedCopyResult changed;
  seethis::platform::CopyAnnotatedPng(
      large_png, std::chrono::seconds(5),
      [&](auto result) { changed = result; changed_finished = true; },
      board_name);
  [board clearContents];
  [board setString:@"new-writer" forType:NSPasteboardTypeString];
  const auto changed_count = board.changeCount;
  const auto change_deadline = [NSDate dateWithTimeIntervalSinceNow:7];
  while (!changed_finished && [change_deadline timeIntervalSinceNow] > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
  Check(changed_finished && changed.status == AnnotatedCopyStatus::kPasteboardChanged &&
            board.changeCount == changed_count &&
            [[board stringForType:NSPasteboardTypeString]
                isEqualToString:@"new-writer"],
        "intervening pasteboard write blocks delayed annotated publication");
  [board releaseGlobally];
}
#endif

void TestPendingReferenceActions() {
  const auto root=std::filesystem::path("/tmp")/
      ("seethis-pending-actions-"+std::to_string(getpid()));
  std::filesystem::remove_all(root);
  seethis::core::ReferenceStore store(root);
  std::vector<std::string> copied;
  bool copy_allowed=true;
  seethis::core::MarkController marks(
      store,
      [&](std::string_view id)->std::optional<std::int64_t> {
        if(!copy_allowed)return {};
        copied.emplace_back(id);return static_cast<std::int64_t>(copied.size());
      },
      [](std::shared_ptr<const seethis::core::StoredReference>,std::int64_t) {
        return true;
      },
      [](std::function<void()> task){task();});
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  marks.SetWindowObservation(WindowFor(DeleteContext(),1));
#endif
  const std::string id(32,'a');
  const auto generation=marks.Begin(id);
  Check(marks.BindContext(generation,DeleteContext()),
        "pending reference binds its frozen context before slow capture");
  marks.Release(generation,DeletePath(0),{1'000'003,1'003},2);
  Check(marks.phase()==seethis::core::ReferencePhase::kReleasedPendingCapture,
        "released slow capture remains pending and interactive");
  const auto pending_listing=marks.inspector_jobs();
  Check(pending_listing.size()==1&&pending_listing.front().id==id&&
            pending_listing.front().state==seethis::core::ReferenceJobState::kPending,
        "metadata inspector list contains the pending reference during background capture");
  Check(marks.RecopyJob(id)&&copied.size()==2,
        "pending URL can be deliberately recopied during slow capture");
  const std::string selected=id;
  copy_allowed=false;
  Check(!marks.RecopyJob(id) &&
            seethis::platform::InspectorMarkIsSelected(id,selected),
        "selected mark remains identifiable when URL recopy fails");
  Check(marks.DeleteJob(id)==seethis::core::DeleteResult::kDeleted,
        "pending reference can be deliberately deleted before capture completes");
  const auto deleted_listing=marks.inspector_jobs();
  Check(store.LookupMetadata(id)->state==seethis::core::ReferenceJobState::kDeleted&&
            deleted_listing.empty(),
        "pending deletion keeps the tombstone truthful while hiding it from the inspector");
  marks.Captured(generation,DeleteCapture());
  store.WaitForIdleForTesting();
  Check(store.LookupMetadata(id)->state==seethis::core::ReferenceJobState::kDeleted&&
            marks.inspector_jobs().empty(),
        "late capture cannot reinsert a logically deleted inspector row");

  const auto commit_ready=[&](const std::string& ready_id,double shift) {
    const auto ready_generation=marks.Begin(ready_id);
    Check(marks.BindContext(ready_generation,DeleteContext()),
          "ready deletion fixture binds its immutable context");
    marks.Captured(ready_generation,DeleteCapture());
    marks.Release(ready_generation,DeletePath(shift),{1'000'004,1'004},3);
    store.WaitForIdleForTesting();
  };
  const std::string ready_id=std::string(32,'b');
  const std::string other_id=std::string(32,'c');
  commit_ready(ready_id,30);
  commit_ready(other_id,60);
  Check(marks.inspector_jobs().size()==2,
        "ready references remain visible in the inspector projection");
  Check(marks.DeleteJob(ready_id)==seethis::core::DeleteResult::kDeleted&&
            store.LookupMetadata(ready_id)->state==
                seethis::core::ReferenceJobState::kDeleted&&
            marks.inspector_jobs().size()==1&&
            marks.inspector_jobs().front().id==other_id,
        "deleting a non-selected ready reference removes only its inspector row");

  const std::string cleanup_id=std::string(32,'d');
  commit_ready(cleanup_id,90);
  seethis::core::ReferenceStoreTestHooks cleanup_hooks;
  cleanup_hooks.remove_owned_file=[&](const std::filesystem::path& path) {
    if(path.filename()==cleanup_id+".source.png")return false;
    errno=0;
    return unlink(path.c_str())==0||errno==ENOENT;
  };
  store.SetTestHooks(std::move(cleanup_hooks));
  Check(marks.DeleteJob(cleanup_id)==seethis::core::DeleteResult::kCleanupFailed&&
            store.LookupMetadata(cleanup_id)->state==
                seethis::core::ReferenceJobState::kDeleted&&
            marks.inspector_jobs().size()==1,
        "cleanup failure preserves diagnostics without restoring the deleted row");
  store.SetTestHooks({});
  Check(marks.DeleteJob(cleanup_id)==seethis::core::DeleteResult::kAlreadyDeleted&&
            marks.inspector_jobs().size()==1,
        "repeated cleanup leaves the deleted inspector projection absent");
  Check(marks.DeleteJob(other_id)==seethis::core::DeleteResult::kDeleted&&
            marks.inspector_jobs().empty(),
        "deleting the remaining selected candidate empties the inspector list");

  {
    seethis::core::ReferenceStore restarted(root);
    seethis::core::MarkController restarted_marks(
        restarted,[](const auto&){return true;});
    Check(restarted.LookupMetadata(id)->state==
                seethis::core::ReferenceJobState::kDeleted&&
            restarted.LookupMetadata(ready_id)->state==
                seethis::core::ReferenceJobState::kDeleted&&
            restarted_marks.inspector_jobs().empty(),
        "startup-recovered deleted tombstones remain absent from the inspector");
  }
  std::filesystem::remove_all(root);
}

void TestInspectorHistoryCopyAcrossApps() {
  using seethis::core::CopyResult;
  using seethis::core::ReferenceJobState;
  const auto root=std::filesystem::path("/tmp")/
      ("seethis-history-copy-"+std::to_string(getpid()));
  std::filesystem::remove_all(root);
  seethis::core::ReferenceStore store(root);
  const std::string ready_id(32,'a'),deleted_id(32,'b');
  const std::string synthetic_capability(64,'1');
  const std::string json_url="http://127.0.0.1:8080/r/"+
      ready_id+synthetic_capability;
  std::vector<std::string> provider_ids;
  std::string published_url;
  bool allow_copy=true;
  bool throw_on_copy=false;
  std::int64_t receipt=0;
  seethis::core::MarkController marks(
      store,
      [&](std::string_view id)->std::optional<std::int64_t> {
        provider_ids.emplace_back(id);
        if(throw_on_copy)throw std::runtime_error("synthetic clipboard failure");
        if(!allow_copy)return {};
        published_url="http://127.0.0.1:8080/r/"+std::string(id)+
            synthetic_capability;
        return ++receipt;
      },
      [](std::shared_ptr<const seethis::core::StoredReference>,std::int64_t) {
        return true;
      },
      [](std::function<void()> task){task();});
  const auto save=[&](const std::string& id,double shift) {
    const auto generation=marks.Begin(id);
    Check(marks.BindContext(generation,DeleteContext()),
          "history copy fixture binds the source app context");
    marks.Captured(generation,DeleteCapture());
    marks.Release(generation,DeletePath(shift),{1'000'004,1'004});
    store.WaitForIdleForTesting();
    const auto persisted=store.Lookup(id);
    Check(persisted&&persisted->state==ReferenceJobState::kReady&&persisted->ready,
          "history copy fixture is a persisted ready reference");
  };
  save(ready_id,0);
  save(deleted_id,30);
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  marks.SetWindowObservation(WindowFor(DeleteContext(),1));
  provider_ids.clear();
  Check(marks.RecopyJob(ready_id)&&provider_ids.size()==1&&
            provider_ids.front()==ready_id,
        "ordinary recopy still publishes a visible ready mark");
  auto other_app=WindowFor(DeleteContext(),2);
  other_app.pid=900;
  other_app.bundle_id="test.other.app";
  other_app.window_id=901;
  marks.SetWindowObservation(other_app);
  provider_ids.clear();
  Check(marks.marks().empty()&&!marks.RecopyJob(ready_id)&&provider_ids.empty(),
        "other foreground app still hides and rejects ordinary mark recopy");
#endif
  provider_ids.clear();
  published_url.clear();
  Check(marks.CopyHistoryJob(ready_id)&&provider_ids.size()==1&&
            provider_ids.front()==ready_id&&published_url==json_url&&
            marks.copy_result()==CopyResult::kCopied,
        "history copy publishes the selected persisted JSON URL once across apps");

  allow_copy=false;
  provider_ids.clear();
  Check(!marks.CopyHistoryJob(ready_id)&&provider_ids.size()==1&&
            marks.copy_result()==CopyResult::kPersistedClipboardFailed,
        "history copy reports an actual clipboard-provider failure");
  allow_copy=true;
  throw_on_copy=true;
  provider_ids.clear();
  Check(!marks.CopyHistoryJob(ready_id)&&provider_ids.size()==1&&
            marks.copy_result()==CopyResult::kPersistedClipboardFailed,
        "history copy catches a clipboard-provider exception without claiming success");
  throw_on_copy=false;

  const std::string pending_id(32,'c');
  const auto pending_generation=marks.Begin(pending_id);
  Check(marks.BindContext(pending_generation,DeleteContext()),
        "pending history copy fixture binds context");
  marks.Release(pending_generation,DeletePath(60),{1'000'005,1'005});
  provider_ids.clear();
  Check(!marks.CopyHistoryJob(pending_id)&&provider_ids.empty(),
        "pending history reference never reaches clipboard provider");
  store.Fail(pending_id,"synthetic_failure");
  Check(store.LookupMetadata(pending_id)->state==ReferenceJobState::kFailed&&
            !marks.CopyHistoryJob(pending_id)&&provider_ids.empty(),
        "failed history reference never reaches clipboard provider");
  Check(marks.DeleteHistoryJob(pending_id)==seethis::core::DeleteResult::kDeleted,
        "failed fixture is removed before expiry retention check");

  Check(marks.DeleteHistoryJob(deleted_id)==seethis::core::DeleteResult::kDeleted,
        "history fixture deletes a distinct persisted reference");
  provider_ids.clear();
  Check(!marks.CopyHistoryJob(deleted_id)&&
            !marks.CopyHistoryJob(std::string(32,'z'))&&provider_ids.empty(),
        "deleted and missing history references never reach clipboard provider");

  seethis::core::ReferenceStoreTestHooks hooks;
  hooks.now=[] {
    auto later=seethis::core::Now();
    later.utc_us+=86'400'000'000;
    return later;
  };
  store.SetTestHooks(std::move(hooks));
  store.SetRetentionPolicy(1,1);
  provider_ids.clear();
  const auto expired=store.LookupMetadata(ready_id);
  Check(expired&&expired->state==ReferenceJobState::kExpired&&
            !marks.CopyHistoryJob(ready_id)&&provider_ids.empty(),
        "expired persisted history reference never reaches clipboard provider");
  store.SetTestHooks({});
  store.WaitForIdleForTesting();
  std::filesystem::remove_all(root);
}

void TestInspectorHistoryDelete() {
  const auto root=std::filesystem::path("/tmp")/
      ("seethis-history-delete-"+std::to_string(getpid()));
  std::filesystem::remove_all(root);
  seethis::core::ReferenceStore store(root);
  seethis::core::MarkController marks(store,[](const auto&){return true;});
  const auto save=[&](const std::string& id,double shift) {
    const auto generation=marks.Begin(id);
    Check(marks.BindContext(generation,DeleteContext()),
          "history fixture binds a disposable reference");
    marks.Captured(generation,DeleteCapture());
    marks.Release(generation,DeletePath(shift),{1'000'004,1'004});
    store.WaitForIdleForTesting();
    Check(store.LookupMetadata(id)->state==seethis::core::ReferenceJobState::kReady,
          "history fixture persists a ready reference");
  };
  const std::string selected(32,'1'),other(32,'2'),blocked(32,'3'),pending(32,'4');
  save(selected,0);save(other,30);save(blocked,60);
  auto unavailable=WindowFor(DeleteContext(),2);
  unavailable.availability=seethis::core::WindowAvailability::kUnavailable;
  marks.SetWindowObservation(unavailable);
  Check(marks.DeleteJob(selected)==seethis::core::DeleteResult::kNotFound &&
            marks.DeleteHistoryJob(selected)==seethis::core::DeleteResult::kDeleted &&
            store.LookupMetadata(selected)->state==
                seethis::core::ReferenceJobState::kDeleted &&
            store.LookupMetadata(other)->state==
                seethis::core::ReferenceJobState::kReady,
        "explicit history deletion is independent of foreground and leaves other references intact");
  Check(marks.DeleteHistoryJob(selected)==
            seethis::core::DeleteResult::kAlreadyDeleted &&
            marks.DeleteHistoryJob(std::string(32,'9'))==
            seethis::core::DeleteResult::kNotFound &&
            store.LookupMetadata(other)->state==
                seethis::core::ReferenceJobState::kReady,
        "repeat and stale selected IDs cannot delete a different reference");

  std::filesystem::create_directory(root/(blocked+".deleted"));
  Check(marks.DeleteHistoryJob(blocked)==
            seethis::core::DeleteResult::kCleanupFailed &&
            store.LookupMetadata(blocked)->state==
                seethis::core::ReferenceJobState::kReady,
        "failed tombstone persistence leaves selected history ready for retry");
  std::filesystem::remove(root/(blocked+".deleted"));
  seethis::core::ReferenceStoreTestHooks hooks;
  hooks.remove_owned_file=[&](const std::filesystem::path& path) {
    if(path.filename()==blocked+".source.png")return false;
    errno=0;return unlink(path.c_str())==0||errno==ENOENT;
  };
  store.SetTestHooks(std::move(hooks));
  Check(marks.DeleteHistoryJob(blocked)==
            seethis::core::DeleteResult::kCleanupFailed &&
            store.LookupMetadata(blocked)->state==
                seethis::core::ReferenceJobState::kDeleted &&
            store.LookupMetadata(other)->state==
                seethis::core::ReferenceJobState::kReady,
        "post-tombstone cleanup failure leaves deleted history revoked and other IDs intact");
  store.SetTestHooks({});
  Check(marks.DeleteHistoryJob(blocked)==
            seethis::core::DeleteResult::kAlreadyDeleted,
        "repeated history action finishes cleanup without resurrecting the record");

  const auto generation=marks.Begin(pending);
  Check(marks.BindContext(generation,DeleteContext()),
        "pending history fixture binds context");
  marks.Release(generation,DeletePath(90),{1'000'005,1'005});
  Check(marks.DeleteHistoryJob(pending)==
            seethis::core::DeleteResult::kDeleted,
        "pending history reference can be deliberately tombstoned");
  marks.Captured(generation,DeleteCapture());store.WaitForIdleForTesting();
  Check(store.LookupMetadata(pending)->state==
            seethis::core::ReferenceJobState::kDeleted &&
            store.LookupMetadata(other)->state==
                seethis::core::ReferenceJobState::kReady,
        "late completion cannot resurrect deleted history or affect a different ID");
  std::filesystem::remove_all(root);
}

void TestInspectorSelectionPolicy() {
  using seethis::core::InspectorClearsForTrigger;
  using seethis::core::InspectorOpensForTrigger;
  using seethis::core::InspectorSelectsReference;
  using seethis::core::InspectorSelectionTrigger;
  Check(InspectorSelectsReference(InspectorSelectionTrigger::kNewCircle) &&
            InspectorOpensForTrigger(InspectorSelectionTrigger::kNewCircle),
        "newly accepted circles select and open their inspector");
  Check(InspectorSelectsReference(InspectorSelectionTrigger::kMarkClick) &&
            InspectorOpensForTrigger(InspectorSelectionTrigger::kMarkClick),
        "ordinary mark clicks select and open their inspector");
  Check(!InspectorSelectsReference(InspectorSelectionTrigger::kHover) &&
            !InspectorOpensForTrigger(InspectorSelectionTrigger::kHover),
        "hover only changes presentation and never selects or opens");
  Check(!InspectorOpensForTrigger(
            InspectorSelectionTrigger::kBackgroundCompletion),
        "background completion never forces the inspector open");
  Check(InspectorClearsForTrigger(InspectorSelectionTrigger::kSelectedDelete),
        "deleting the selected mark clears inspector selection");
  Check(!seethis::core::InspectorConsumesPointer(false) &&
            seethis::core::InspectorConsumesPointer(true),
        "hidden inspector passes through while visible panel controls are interactive");
  Check(!seethis::core::InspectorAutoOpenAllowed(
            true, InspectorSelectionTrigger::kNewCircle) &&
            !seethis::core::InspectorAutoOpenAllowed(
                true, InspectorSelectionTrigger::kMarkClick),
        "a hidden inspector stays hidden across new circles and mark clicks");
  Check(seethis::core::InspectorAutoOpenAllowed(
            false, InspectorSelectionTrigger::kMarkClick),
        "an explicit visible inspector continues updating on mark selection");
  Check(seethis::platform::InspectorMarkIsSelected("mark-a","mark-a") &&
            !seethis::platform::InspectorMarkIsSelected("mark-a","mark-b") &&
            !seethis::platform::InspectorMarkIsSelected("","mark-a"),
        "selected identity independently identifies exactly one overlay mark");
  Check(seethis::platform::InspectorSelectionChangesPresentation("mark-a","mark-b") &&
            !seethis::platform::InspectorSelectionChangesPresentation("mark-a","mark-a"),
        "list selection schedules mark redraw independently of recopy result");
  Check(seethis::core::InspectorMetadataReachable(true),
        "long inspector metadata remains reachable through its scrollable body");
  Check(seethis::platform::ReferenceJobAcceptsPointer(
            seethis::core::ReferenceJobState::kPending) &&
            seethis::platform::ReferenceJobAcceptsPointer(
                seethis::core::ReferenceJobState::kIndexing) &&
            seethis::platform::ReferenceJobAcceptsPointer(
                seethis::core::ReferenceJobState::kFailed) &&
            seethis::platform::ReferenceJobAcceptsPointer(
                seethis::core::ReferenceJobState::kReady) &&
            !seethis::platform::ReferenceJobAcceptsPointer(
                seethis::core::ReferenceJobState::kExpired) &&
            !seethis::platform::ReferenceJobAcceptsPointer(
                seethis::core::ReferenceJobState::kDeleted),
        "active reference identities retain deliberate pointer actions");

  const auto overlay_source = std::filesystem::path(__FILE__).parent_path()
                                  .parent_path() / "src/platform/mac/overlay_mac.mm";
  std::ifstream input(overlay_source);
  const std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  Check(input.is_open() && !source.empty(),
        "inspector formatter source is available to the regression");
  Check(source.find("\\nSelection region: ") == std::string::npos &&
            source.find("\\nCrop pixels: ") == std::string::npos,
        "inspector does not retain ambiguous bare coordinate labels");
  Check(source.find("Loading…") == std::string::npos &&
            source.find("Indexing…") == std::string::npos &&
            source.find("Label([NSString stringWithUTF8String:_marks->status().c_str()") ==
                std::string::npos,
        "overlay stays quiet while backend states remain inspector-visible");
  Check(source.find("Link copied — paste into chat") != std::string::npos,
        "successful URL copy has one brief truthful acknowledgement");
  Check(source.find("inspector_jobs()") != std::string::npos &&
            source.find("inspectorSelectReference") != std::string::npos,
        "inspector exposes a lightweight selectable reference list");
  Check(source.find("server->ClipboardText(id)") != std::string::npos &&
            source.find("_server->ViewerUrl(selected)") != std::string::npos,
        "mark copy uses the JSON URL while View details opens the legacy viewer");
  Check(source.find("InspectorAfterSuccessfulDelete") !=
            std::string::npos &&
            source.find("[self refreshInspector];") != std::string::npos,
        "successful deletion routes through the shared result-aware selection policy");
}

void TestInspectorDisplayAssociationProjection() {
  using seethis::core::CoordinateUnit;
  using seethis::core::DisplayPoint;
  using seethis::platform::InspectorDisplayAssociationState;
  using seethis::platform::ProjectInspectorDisplayAssociation;

  seethis::core::Context context;
  context.displays.push_back({"primary-retina-uuid", 1, {0, 0, 3024, 1964},
                              {0, 0, 6048, 3928}, 2, {2, -2, 0, 3928}});
  context.displays.push_back({"external-uuid", 3, {3024, 0, 2560, 1440},
                              {0, 0, 2560, 1440}, 1, {1, -1, -3024, 1440}});
  const auto point = [](std::uint64_t display, double x, double y) {
    return DisplayPoint{display, CoordinateUnit::kLogicalPoints, {x, y},
                        display == 3 ? 1.0 : 2.0};
  };
  const auto external = std::vector<seethis::core::ReferenceRegion>{
      {{point(3, 20, 30), point(3, 80, 90)}, {3044, 30, 60, 60},
       {40, 50, 120, 120}}};
  const auto selected = ProjectInspectorDisplayAssociation(
      context, external);
  Check(selected.regions.size() == 1 &&
            selected.regions[0].association ==
                InspectorDisplayAssociationState::kAssociated &&
            selected.regions[0].display_id == 3 &&
            selected.regions[0].display_uuid == "external-uuid" &&
            selected.regions[0].display_logical.x == 3024 &&
            selected.regions[0].display_pixels.width == 2560 &&
            selected.regions[0].display_scale == 1 &&
            selected.regions[0].region.x == 3044 &&
            selected.regions[0].crop_pixels.x == 40,
        "inspector associates an external selection with its actual display");

  const auto two_regions = std::vector<seethis::core::ReferenceRegion>{
      {{point(1, 10, 20), point(1, 30, 40)}, {10, 20, 20, 20},
       {10, 20, 20, 20}},
      {{point(3, 50, 60), point(3, 70, 80)}, {3074, 60, 20, 20},
       {70, 80, 20, 20}}};
  const auto multi = ProjectInspectorDisplayAssociation(context, two_regions);
  Check(multi.regions.size() == 2 && multi.regions[0].display_id == 1 &&
            multi.regions[1].display_id == 3 &&
            multi.regions[1].display_uuid == "external-uuid" &&
            multi.regions[1].region.x == 3074 &&
            multi.regions[1].crop_pixels.x == 70,
        "inspector preserves actual display associations for two regions");

  const auto missing = std::vector<seethis::core::ReferenceRegion>{
      {{point(99, 1, 2), point(99, 3, 4)}, {}, {}}};
  const auto unavailable = ProjectInspectorDisplayAssociation(context, missing);
  Check(unavailable.regions[0].association ==
            InspectorDisplayAssociationState::kUnavailable,
        "missing display records remain honestly unavailable");

  const auto ambiguous = std::vector<seethis::core::ReferenceRegion>{
      {{point(1, 1, 2), point(3, 3, 4)}, {}, {}}};
  const auto mixed = ProjectInspectorDisplayAssociation(context, ambiguous);
  Check(mixed.regions[0].association ==
            InspectorDisplayAssociationState::kAssociated &&
            mixed.regions[0].displays.size() == 2 &&
            mixed.regions[0].displays[0].id == 1 &&
            mixed.regions[0].displays[1].id == 3,
        "paths spanning displays preserve every matched display identity");

  auto duplicate_context = context;
  duplicate_context.displays.push_back(context.displays.back());
  const auto duplicate = ProjectInspectorDisplayAssociation(
      duplicate_context, external);
  Check(duplicate.regions[0].association ==
            InspectorDisplayAssociationState::kUnavailable,
        "conflicting display records remain honestly unavailable");

  const auto legacy = ProjectInspectorDisplayAssociation(
      context, {}, {point(3, 4, 5), point(3, 6, 7)});
  Check(legacy.regions.size() == 1 && legacy.regions[0].display_id == 3,
        "legacy paths use their path display association");
  const auto incomplete = ProjectInspectorDisplayAssociation(context, {}, {});
  Check(incomplete.regions.empty(),
        "incomplete records do not invent an association");

  seethis::core::Reference reopened;
  reopened.id = "external-reference";
  reopened.context = context;
  reopened.regions = external;
  const auto reopened_projection =
      ProjectInspectorDisplayAssociation(reopened);
  using seethis::platform::InspectorLifecycleEvent;
  using seethis::platform::InspectorLifecycleState;
  using seethis::platform::ProjectInspectorSelectedReference;
  using seethis::platform::ReduceInspectorLifecycle;
  InspectorLifecycleState lifecycle=ReduceInspectorLifecycle(
      {},InspectorLifecycleEvent::kReferenceSelection,"external-reference");
  Check(lifecycle.panel_visible && !lifecycle.user_hidden &&
            lifecycle.selected_id == "external-reference",
        "historical external selection opens the inspector lifecycle");
  lifecycle=ReduceInspectorLifecycle(lifecycle,
                                     InspectorLifecycleEvent::kUserClose);
  Check(!lifecycle.panel_visible && lifecycle.user_hidden &&
            lifecycle.selected_id == "external-reference",
        "closing the inspector preserves selected historical identity");
  lifecycle=ReduceInspectorLifecycle(
      lifecycle,InspectorLifecycleEvent::kReferenceSelection,"external-reference");
  Check(!lifecycle.panel_visible && lifecycle.user_hidden,
        "hidden inspector suppresses background selection auto-open");
  lifecycle=ReduceInspectorLifecycle(
      lifecycle,InspectorLifecycleEvent::kBackgroundCompletion);
  Check(!lifecycle.panel_visible && lifecycle.user_hidden &&
            lifecycle.selected_id == "external-reference",
        "background completion does not force a hidden inspector open");
  InspectorLifecycleState wrong_selection=lifecycle;
  wrong_selection.selected_id="different-reference";
  Check(ProjectInspectorSelectedReference(wrong_selection,reopened)
            .regions.empty(),
        "refresh projection is gated by the lifecycle selected identity");
  lifecycle=ReduceInspectorLifecycle(
      lifecycle,InspectorLifecycleEvent::kExplicitToggle);
  const auto refreshed_projection=ProjectInspectorSelectedReference(
      lifecycle,reopened);
  Check(lifecycle.panel_visible && !lifecycle.user_hidden &&
            lifecycle.selected_id == reopened.id &&
            refreshed_projection.regions.size() == 1 &&
            refreshed_projection.regions[0].display_id == 3 &&
            refreshed_projection.regions[0].display_uuid == "external-uuid" &&
            refreshed_projection.regions[0].region.x == 3044,
        "explicit reopen refreshes the preserved external historical projection");
}

void TestDisplayLeaveReentrySubpaths() {
  using seethis::core::CoordinateUnit;
  using seethis::core::DisplayGeometry;
  using seethis::core::DisplayPoint;
  using seethis::core::InteractionController;
  using seethis::core::Point2D;
  InteractionController controller;
  const DisplayGeometry geometry{7, {-100, -50}, {100, 80}, 2};
  const auto point = [](double x, double y) {
    return DisplayPoint{7, CoordinateUnit::kLogicalPoints, {x, y}, 2};
  };
  Check(controller.ShortcutKeyDown(1, point(10, 10)) ==
            seethis::core::StartResult::kStarted,
        "subpath fixture starts");
  controller.SetSelectedDisplayGeometry(geometry);
  Check(controller.PointerDown(point(10, 10)), "subpath fixture begins region");
  (void)controller.PointerMoved(point(30, 30));
  (void)controller.PointerMoved(point(120, 30));
  (void)controller.PointerMoved(point(120, 30));
  (void)controller.PointerMoved(point(40, 40));
  (void)controller.PointerMoved(point(50, 50));
  Check(controller.PointerUp(point(60, 60)), "subpath fixture releases in bounds");
  Check(controller.ShortcutKeyUp(10), "subpath fixture completes");
  const auto snapshot = controller.Snapshot();
  Check(snapshot.subpaths.size() == 1 && snapshot.subpaths[0].size() == 2,
        "leave and re-entry retain one logical region with two subpaths");
  Check(snapshot.subpaths[0][0].back().position.x == 100.0 &&
            snapshot.subpaths[0][1].front().position.x == 100.0 &&
            snapshot.subpaths[0][1].size() >= 2,
        "boundary and re-entry samples retain display-local geometry");
  Check(snapshot.regions.empty(),
        "legacy projection does not publish a fake connecting chord");
  Check(!controller.HitTestFinishedMark(point(70, 35), 1.0),
        "hit testing never crosses a leave and re-entry gap");
  Check(controller.HitTestFinishedMark(point(45, 45), 1.0),
        "hit testing still reaches the retained re-entry segment");

  const std::array<std::pair<Point2D, Point2D>, 8> exits = {{
      {{-10, 40}, {0, 40}}, {{110, 40}, {100, 40}},
      {{50, -10}, {50, 0}}, {{50, 90}, {50, 80}},
      {{-50, -40}, {0, 0}}, {{150, -40}, {100, 0}},
      {{-50, 120}, {0, 80}}, {{150, 120}, {100, 80}}}};
  for (const auto& [outside, boundary] : exits) {
    InteractionController edge;
    Check(edge.ShortcutKeyDown(1, point(50, 40)) ==
              seethis::core::StartResult::kStarted,
          "edge and corner fixture starts");
    edge.SetSelectedDisplayGeometry(geometry);
    Check(edge.PointerDown(point(50, 40)), "edge and corner fixture begins");
    Check(edge.PointerMoved(point(outside.x, outside.y)),
          "edge and corner exit clips to the frozen display");
    Check(edge.PointerMoved(point(50, 40)),
          "edge and corner re-entry starts a new contiguous subpath");
    Check(edge.PointerUp(point(50, 40)) && edge.ShortcutKeyUp(10),
          "edge and corner fixture completes");
    const auto edge_snapshot = edge.Snapshot();
    Check(edge_snapshot.subpaths.size() == 1 &&
              edge_snapshot.subpaths[0].size() == 2 &&
              edge_snapshot.subpaths[0][0].size() == 2 &&
              edge_snapshot.subpaths[0][0].back().position.x == boundary.x &&
              edge_snapshot.subpaths[0][0].back().position.y == boundary.y,
          "every edge and corner preserves its clipped boundary without a chord");
  }

  InteractionController outside_crossing;
  Check(outside_crossing.ShortcutKeyDown(1, point(50, 40)) ==
            seethis::core::StartResult::kStarted,
        "outside crossing fixture starts");
  outside_crossing.SetSelectedDisplayGeometry(geometry);
  Check(outside_crossing.PointerDown(point(50, 40)),
        "outside crossing fixture begins");
  Check(outside_crossing.PointerMoved(point(120, 100)),
        "outside crossing fixture exits");
  Check(outside_crossing.PointerMoved(point(-20, 50)),
        "outside-to-outside crossing retains a bounded segment");
  Check(outside_crossing.PointerUp(point(-20, 50)) &&
            outside_crossing.ShortcutKeyUp(10),
        "outside crossing fixture completes");
  const auto crossing_snapshot = outside_crossing.Snapshot();
  Check(crossing_snapshot.subpaths.size() == 1 &&
            crossing_snapshot.subpaths[0].size() == 2 &&
            crossing_snapshot.subpaths[0][1].size() == 2 &&
            crossing_snapshot.subpaths[0][1].front().position.y == 80.0,
        "a true outside-to-outside crossing is a new entry-to-exit subpath");

  InteractionController outside_release_crossing;
  Check(outside_release_crossing.ShortcutKeyDown(1, point(50, 40)) ==
            seethis::core::StartResult::kStarted,
        "outside release crossing fixture starts");
  outside_release_crossing.SetSelectedDisplayGeometry(geometry);
  Check(outside_release_crossing.PointerDown(point(50, 40)),
        "outside release crossing fixture begins");
  (void)outside_release_crossing.PointerMoved(point(120, 100));
  Check(outside_release_crossing.PointerUp(point(-20, 50)) &&
            outside_release_crossing.ShortcutKeyUp(10),
        "outside release crossing completes");
  const auto release_crossing_snapshot = outside_release_crossing.Snapshot();
  Check(release_crossing_snapshot.subpaths.size() == 1 &&
            release_crossing_snapshot.subpaths[0].size() == 2 &&
            release_crossing_snapshot.subpaths[0][1].size() == 2 &&
            release_crossing_snapshot.subpaths[0][1].front().position.y == 80.0,
        "outside release preserves a true crossing as a new subpath");

  InteractionController horizontal;
  Check(horizontal.ShortcutKeyDown(1, point(10, 10)) ==
            seethis::core::StartResult::kStarted,
        "orthogonal fixture starts");
  horizontal.SetSelectedDisplayGeometry(geometry);
  Check(horizontal.PointerDown(point(10, 10)), "orthogonal fixture begins");
  (void)horizontal.PointerMoved(point(30, 10));
  Check(horizontal.PointerUp(point(40, 10)) && horizontal.ShortcutKeyUp(10),
        "horizontal segment completes");
  Check(horizontal.Snapshot().subpaths.size() == 1 &&
            horizontal.Snapshot().subpaths[0][0].size() == 3,
        "horizontal in-display segment is retained as valid geometry");
}

void TestChromeObservationLifecycle() {
  using seethis::core::PageAvailability;
  using seethis::platform::ChromeObservationLifecycle;

  ChromeObservationLifecycle lifecycle;
  const auto first=lifecycle.BeginRequest();
  Check(first.valid() && first.epoch==1 && first.generation==1,
        "Chrome observation starts with a coordinator-lifetime identity");
  Check(lifecycle.Accepts(first) && lifecycle.pending(),
        "the current Chrome provider request is accepted while pending");

  const auto invalidated_generation=lifecycle.Invalidate();
  Check(invalidated_generation==2 && !lifecycle.pending() &&
            !lifecycle.Accepts(first),
        "foreground invalidation rejects an outstanding Chrome callback");
  const auto replacement=lifecycle.BeginRequest();
  Check(replacement.epoch==2 && replacement.generation==3 &&
            lifecycle.Accepts(replacement),
        "a restarted observation advances both the epoch and generation");
  lifecycle.Complete(first);
  Check(lifecycle.Accepts(replacement),
        "a late callback cannot complete the replacement request");
  lifecycle.Complete(replacement);
  Check(!lifecycle.pending() && !lifecycle.Accepts(replacement),
        "a matching provider callback completes exactly once");

  for(const auto terminal:{PageAvailability::kUnavailable,
                           PageAvailability::kDenied,
                           PageAvailability::kAmbiguous,
                           PageAvailability::kTimedOut}) {
    const auto quick_release=lifecycle.BeginRequest();
    Check(lifecycle.Accepts(quick_release),
          "a quick-release Chrome capture keeps its provider request live");
    (void)terminal;
    lifecycle.Complete(quick_release);
    Check(!lifecycle.pending() && !lifecycle.Accepts(quick_release),
          "terminal Chrome acquisition cannot later bind from a stale callback");
  }
  const auto before_stop=lifecycle.generation();
  (void)lifecycle.Invalidate();
  const auto after_stop=lifecycle.BeginRequest();
  Check(after_stop.generation>before_stop && after_stop.epoch>first.epoch,
        "stop and restart never reset Chrome request identity");
}

#if defined(SEETHIS_WINDOW_OBSERVATION_API)
void TestChromeCaptureIdentityFreeze() {
  using seethis::core::PageAvailability;
  const auto root=std::filesystem::path("/tmp")/
      ("seethis-chrome-freeze-"+std::to_string(getpid()));
  std::filesystem::remove_all(root);
  {
    seethis::core::ReferenceStore store(root);
    seethis::core::MarkController marks(store,[](const auto&){return true;});
    seethis::platform::ChromeObservationLifecycle lifecycle;
    auto context=DeleteContext();
    context.bundle_id="com.google.Chrome";
    const auto id=std::string(32,'f');
    const auto generation=marks.Begin(id);
    Check(marks.BindContext(generation,context),
          "Chrome identity-freeze fixture binds its immutable window");
    marks.Captured(generation,DeleteCapture());

    const auto first_request=lifecycle.BeginRequest();
    const auto first=ChromePageFor(context,"tab-a",'a');
    Check(ApplyChromePageResult(
              marks,lifecycle,first_request,generation,
              {PageAvailability::kReady,first_request.generation,
               seethis::core::Now(),first}),
          "a ready Chrome result is accepted while drawing");
    Check(marks.AllowsDrawing() && !marks.AwaitingPageIdentity(generation),
          "the accepted ready identity binds before release without ending drawing");

    const auto later_request=lifecycle.BeginRequest();
    const auto later=ChromePageFor(context,"tab-b",'b');
    Check(ApplyChromePageResult(
              marks,lifecycle,later_request,generation,
              {PageAvailability::kReady,later_request.generation,
               seethis::core::Now(),later}),
          "a later ready Chrome observation completes normally");
    marks.Release(generation,DeletePath(0),seethis::core::Now(),2);
    const auto frozen=store.LookupMetadata(id);
    Check(!marks.Pending() && frozen && frozen->page_identity &&
              seethis::core::SamePageIdentity(*frozen->page_identity,first),
          "release persists the first ready page identity and later B cannot replace A");
  }  // Join the store's async writers before removing their directory.

  const auto overlay_source=std::filesystem::path(__FILE__).parent_path()
                                .parent_path()/"src/platform/mac/overlay_mac.mm";
  std::ifstream input(overlay_source);
  const std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  const auto ready_branch=source.find(
      "if(published.availability==seethis::core::PageAvailability::kReady");
  const auto terminal_branch=source.find("} else if(published.availability!=",
                                         ready_branch);
  const auto ready_body=ready_branch==std::string::npos ||
                                terminal_branch==std::string::npos
                            ? std::string()
                            : source.substr(ready_branch,
                                            terminal_branch-ready_branch);
  Check(input.is_open() &&
            ready_body.find("BindPageIdentity(capture_generation")!=
                std::string::npos &&
            ready_body.find("AwaitingPageIdentity")==std::string::npos &&
            ready_body.find("abortPending")==std::string::npos,
        "the native callback binds ready identity immediately and ignores later replacements");
  std::filesystem::remove_all(root);
}

void TestQuickReleaseChromeTerminalOutcomes() {
  using seethis::core::PageAvailability;
  const auto root=std::filesystem::path("/tmp")/
      ("seethis-chrome-terminal-"+std::to_string(getpid()));
  std::filesystem::remove_all(root);
  seethis::core::ReferenceStore store(root);
  seethis::core::MarkController marks(store,[](const auto&){return true;});
  seethis::platform::ChromeObservationLifecycle lifecycle;
  std::size_t fixture=0;
  for(const auto terminal:{PageAvailability::kUnavailable,
                           PageAvailability::kDenied,
                           PageAvailability::kAmbiguous,
                           PageAvailability::kTimedOut}) {
    auto context=DeleteContext();
    context.bundle_id="com.google.Chrome";
    const auto id=std::string(31,'q')+static_cast<char>('0'+fixture);
    const auto generation=marks.Begin(id);
    Check(marks.BindContext(generation,context),
          "quick-release Chrome fixture binds its immutable window");
    marks.Captured(generation,DeleteCapture());

    const auto drawing_request=lifecycle.BeginRequest();
    Check(ApplyChromePageResult(
              marks,lifecycle,drawing_request,generation,
              {terminal,drawing_request.generation,seethis::core::Now(),
               std::nullopt}),
          "terminal provider result is accepted while Chrome drawing continues");
    Check(marks.Pending() && marks.AllowsDrawing() &&
              !marks.AwaitingPageIdentity(generation),
          "terminal acquisition cannot abort a transaction before release");

    marks.Release(generation,DeletePath(0),seethis::core::Now(),2);
    Check(marks.Pending() && marks.AwaitingPageIdentity(generation),
          "released Chrome capture remains pending for its matching identity");

    const auto request=lifecycle.BeginRequest();
    seethis::core::PageObservation observation{
        terminal,request.generation,seethis::core::Now(),std::nullopt};
    Check(lifecycle.Accepts(request),
          "terminal provider result initially belongs to the pending request");
    Check(ApplyChromePageResult(marks,lifecycle,request,generation,
                                std::move(observation)),
          "terminal provider result is applied to its live request");
    Check(!marks.Pending() && !store.LookupMetadata(id) &&
              marks.phase()==seethis::core::ReferencePhase::kAborted,
          "non-ready Chrome acquisition aborts without persisting a mark");

    seethis::core::PageIdentity late;
    late.browser_pid=context.pid;late.window_id=context.window_id;
    late.window_bounds=context.window;late.process_start_identity_us=1;
    late.opaque_tab_id="late-tab";
    late.navigation_digest=std::string(64,'a');
    Check(!lifecycle.Accepts(request) &&
              !marks.BindPageIdentity(generation,std::move(late)),
          "a stale terminal callback cannot bind a later page identity");
    ++fixture;
  }
  std::filesystem::remove_all(root);
}
#endif

void TestIndependentChromeConnectionRecovery() {
  using namespace seethis::platform;
  ChromeConnectionController connection;
  ChromeRunningTarget running;
  ChromePermissionResult preflight{ChromeConsent::kNotRequested,-1744};
  ChromePermissionResult requested{ChromeConsent::kDenied,-1743};
  int preflights=0, requests=0;
  ChromeConnectionAdapters adapters{
    [&] { return running; },
    [&](std::int64_t pid,bool ask) {
      Check(pid==running.pid,"permission adapter targets discovered Chrome, without a foreground dependency");
      if(ask) {++requests;return requested;}
      ++preflights;return preflight;
    }};
  connection.Connect(adapters);
  Check(requests==0 && preflights==0 && !connection.BeginProbe().generation,
        "Chrome not running cannot request consent, probe, or launch Chrome");
  running={123,0};connection.Connect(adapters);
  Check(requests==0 && preflights==0 &&
            ChromeRunningName(connection.snapshot().target)=="identity unavailable",
        "a running process without launch identity is unknown, not falsely reported absent");
  running={123,1'000};
  for(int refresh=0;refresh<5;++refresh)connection.Refresh(adapters);
  Check(requests==0 && preflights==5 &&
            connection.snapshot().permission.consent==ChromeConsent::kNotRequested,
        "startup and repeated refresh use only prompt-free preflight");
  connection.Connect(adapters);
  Check(requests==1 && connection.snapshot().permission.consent==ChromeConsent::kDenied &&
            !connection.BeginProbe().generation &&
            ChromeRecoveryGuidance(connection.snapshot()).find("not guaranteed")!=std::string_view::npos,
        "explicit independent connection records denial and does not promise a repeated OS prompt");
  preflight={ChromeConsent::kDenied,-1743};
  connection.Refresh(adapters);
  Check(requests==1 &&
            ChromeRecoveryGuidance(connection.snapshot()).find("Automation")!=std::string_view::npos,
        "retry preflight after denial preserves settings guidance without requesting consent");
  requested={ChromeConsent::kGranted,0};
  const auto before_consent=connection.snapshot().authorization_generation;
  connection.Connect(adapters);
  Check(requests==2 && connection.snapshot().permission.consent==ChromeConsent::kGranted,
        "only an explicit Connect action requests normal consent from denied state");
  Check(before_consent!=connection.snapshot().authorization_generation,
        "granting consent invalidates the authorization generation of old foreground results");
  preflight=requested;
  const auto granted_generation=connection.snapshot().authorization_generation;
  connection.Refresh(adapters);
  Check(granted_generation==connection.snapshot().authorization_generation,
        "unchanged granted preflight preserves current foreground acquisition generation");
  auto check=connection.BeginProbe();
  Check(connection.CompleteProbe(check,ChromeProviderState::kReady) &&
            !connection.snapshot().observation_active &&
            connection.snapshot().page==seethis::core::PageAvailability::kUnavailable,
        "background provider success is not foreground observation or a mark page identity");
  connection.Observe(999,true,seethis::core::PageAvailability::kReady,true);
  Check(!connection.snapshot().observation_active,
        "another app or Chrome process cannot claim current Chrome observation");
  connection.Observe(123,true,seethis::core::PageAvailability::kReady,true);
  Check(connection.snapshot().observation_active &&
            connection.snapshot().page==seethis::core::PageAvailability::kReady,
        "actual matching foreground provider publication reports observation separately");
  connection.Observe(123,false,seethis::core::PageAvailability::kReady);
  Check(!connection.snapshot().observation_active &&
            connection.snapshot().page==seethis::core::PageAvailability::kUnavailable,
        "stopping foreground observation clears current page readiness without revoking consent");
  const auto timeout=connection.BeginProbe();
  Check(connection.CompleteProbe(timeout,ChromeProviderState::kTimedOut) &&
            !connection.CompleteProbe(timeout,ChromeProviderState::kReady),
        "connection timeout wins once and rejects a late provider success");
  const auto old=connection.BeginProbe();
  const auto retry=connection.BeginProbe();
  Check(!connection.CompleteProbe(old,ChromeProviderState::kReady) &&
            connection.CompleteProbe(retry,ChromeProviderState::kNoPage) &&
            ChromeRecoveryGuidance(connection.snapshot()).find("Open a page")!=std::string_view::npos,
        "explicit retry supersedes old callback and distinguishes no page from denial");
  const auto before_restart=connection.BeginProbe();
  running.process_start_identity_us=2'000;connection.Refresh(adapters);
  Check(!connection.CompleteProbe(before_restart,ChromeProviderState::kReady) &&
            connection.snapshot().provider==ChromeProviderState::kUnknown &&
            !connection.snapshot().observation_active,
        "same-PID process relaunch invalidates old readiness and observation");
  const auto before_revoke=connection.BeginProbe();
  preflight={ChromeConsent::kDenied,-1743};connection.Refresh(adapters);
  Check(!connection.CompleteProbe(before_revoke,ChromeProviderState::kReady) &&
            !connection.snapshot().observation_active && requests==2 &&
            granted_generation!=connection.snapshot().authorization_generation,
        "revocation detected on refresh rejects outstanding provider and never prompts");
  connection.SettingsOpened(false);connection.Refresh(adapters);
  Check(ChromeRecoveryGuidance(connection.snapshot()).find("Could not open Settings")!=std::string_view::npos,
        "failed settings action remains truthful through periodic refresh");
  Check(ChromePermissionFromOSStatus(-1744).consent==ChromeConsent::kNotRequested &&
            ChromePermissionFromOSStatus(-1743).consent==ChromeConsent::kDenied &&
            ChromePermissionFromOSStatus(-600).consent==ChromeConsent::kUnavailable &&
            ChromePermissionFromOSStatus(-50).consent==ChromeConsent::kUnknown,
        "native permission statuses distinguish unrequested, denied, process unavailable and unknown");
  preflight={ChromeConsent::kGranted,0};connection.Refresh(adapters);
  for(const auto outcome:{ChromeProviderState::kDenied,ChromeProviderState::kAmbiguous,
                          ChromeProviderState::kFailed,ChromeProviderState::kTimedOut}) {
    const auto probe=connection.BeginProbe();
    Check(connection.CompleteProbe(probe,outcome) &&
              connection.snapshot().provider==outcome &&
              !connection.snapshot().observation_active,
          "provider failure categories remain distinct without claiming an active listener");
  }
  running={};connection.Refresh(adapters);
  Check(!connection.snapshot().target.valid() &&
            connection.snapshot().provider==ChromeProviderState::kUnknown && requests==2,
        "Chrome quit clears cached provider without requesting consent");
}

void TestInspectorConnectionHealthAndEffectiveBindings() {
  using namespace seethis::platform;
  ChromeConnectionSnapshot state;
  state.target={42,100};state.permission={ChromeConsent::kGranted,0};
  for(const auto provider:{ChromeProviderState::kReady,ChromeProviderState::kNoPage}) {
    state.provider=provider;
    for(const auto page:{seethis::core::PageAvailability::kUnavailable,
                        seethis::core::PageAvailability::kReady,
                        seethis::core::PageAvailability::kDenied}) {
      state.page=page;state.observation_active=false;
      Check(ChromeConnectionHealthy(state),"health uses successful connectivity, never page or observation");
    }
  }
  for(const auto provider:{ChromeProviderState::kUnknown,ChromeProviderState::kChecking,
                          ChromeProviderState::kDenied,ChromeProviderState::kAmbiguous,
                          ChromeProviderState::kTimedOut,ChromeProviderState::kFailed}) {
    state.provider=provider;
    Check(!ChromeConnectionHealthy(state),"unproven or failed provider remains unhealthy independently of recovery visibility");
  }
  state.provider=ChromeProviderState::kReady;state.target.process_start_identity_us=0;
  Check(!ChromeConnectionHealthy(state),"PID without valid process identity is not healthy");
  state.target={42,100};state.permission.consent=ChromeConsent::kDenied;
  Check(!ChromeConnectionHealthy(state),"revoked permission remains unhealthy");

  for(const auto consent:{ChromeConsent::kGranted,ChromeConsent::kUnknown,
                          ChromeConsent::kUnavailable,ChromeConsent::kDenied,
                          ChromeConsent::kNotRequested}) {
    state.permission.consent=consent;
    for(const auto provider:{ChromeProviderState::kReady,ChromeProviderState::kDenied,
                            ChromeProviderState::kFailed,ChromeProviderState::kNoPage}) {
      state.provider=provider;state.observation_active=false;state.settings_open_failed=true;
      const bool missing=consent==ChromeConsent::kDenied || consent==ChromeConsent::kNotRequested;
      Check(ChromePermissionRecoveryVisible(state,42)==missing,
            "only real current Chrome permission denial/not-requested shows contextual recovery");
      Check(!ChromePermissionRecoveryVisible(state,0) && !ChromePermissionRecoveryVisible(state,99),
            "startup, nonChrome, Inspector and stale historical Chrome contexts hide recovery");
    }
  }
  state.permission.consent=ChromeConsent::kDenied;state.target.process_start_identity_us=0;
  Check(!ChromePermissionRecoveryVisible(state,42),"unproven Chrome target cannot activate permission recovery");
  state.target={42,100};state.permission.consent=ChromeConsent::kGranted;state.provider=ChromeProviderState::kFailed;
  Check(!ChromeConnectionHealthy(state)&&!ChromePermissionRecoveryVisible(state,42),
        "granted failed provider remains unhealthy but entire permission block stays hidden");

  EffectiveShortcutPublication published;
  Check(!published.Snapshot().capture.registered,"before registration no effective shortcut");
  published.Capture({11,seethis::core::kShortcutCommand,true},CaptureShortcutState::kReady);
  published.Delete({7,seethis::core::kShortcutControl,true},DeleteShortcutState::kReady);
  auto live=published.Snapshot();
  Check(live.capture.registered && live.capture.key_code==11 &&
        live.capture.modifiers==seethis::core::kShortcutCommand,
        "publication retains actual nondefault registered capture binding");
  published.Capture({12,seethis::core::kShortcutShift,false},CaptureShortcutState::kRegistrationFailed);
  Check(!published.Snapshot().capture.registered &&
        published.Snapshot().capture_state==CaptureShortcutState::kRegistrationFailed,
        "failed replacement never exposes stale registration as effective");
  published.Capture({12,seethis::core::kShortcutShift,false},CaptureShortcutState::kConflict);
  published.Delete({12,seethis::core::kShortcutShift,false},DeleteShortcutState::kConflict);
  Check(!published.Snapshot().deletion.registered,"conflicting bindings remain unusable");
  published.Capture({12,seethis::core::kShortcutShift,true},CaptureShortcutState::kInputUnavailable);
  Check(published.Snapshot().capture.registered &&
        published.Snapshot().capture_state==CaptureShortcutState::kInputUnavailable,
        "OS registration and observation readiness remain distinct");
  published.Screen(ScreenRecordingState::kDenied);
  Check(published.Snapshot().screen==ScreenRecordingState::kDenied,"screen capability stays distinct");
  published.Stop();
  Check(!published.Snapshot().capture.registered && !published.Snapshot().deletion.registered &&
        published.Snapshot().capture_state==CaptureShortcutState::kUnknown,
        "stop clears both registrations and stale readiness");
}

void TestReferenceDropdownSelectionAfterRealDeletion() {
  using namespace seethis::core;
  using namespace seethis::platform;
  const auto directory=std::filesystem::path(std::getenv("TMPDIR")?
      std::getenv("TMPDIR"):"/tmp")/("seethis-dropdown-"+std::to_string(getpid()));
  std::filesystem::remove_all(directory);
  ReferenceStore store(directory);
  MarkController marks(store,[](const auto&){return true;});
  std::int64_t clock=1'000'000;
  ReferenceStoreTestHooks hooks;
  hooks.now=[&]{return Stamp{clock,clock};};store.SetTestHooks(hooks);
  const auto save=[&](char digit,std::int64_t created) {
    clock=created;
    const std::string id(32,digit);
    const auto generation=marks.Begin(id);
    Check(marks.BindContext(generation,DeleteContext()),"dropdown fixture context binds");
    marks.Captured(generation,DeleteCapture());
    marks.Release(generation,DeletePath(0),{created,created});
    store.WaitForIdleForTesting();
    Check(store.LookupMetadata(id)->accepted_utc_us==created,
          "dropdown chronology is the real store creation timestamp");
    return id;
  };
  // Arrival order, lexical IDs and timestamps deliberately disagree.
  const auto newest=save('1',3'000'000);
  const auto oldest=save('9',1'000'000);
  const auto middle=save('8',2'000'000);
  auto state=ReduceInspectorLifecycle({},InspectorLifecycleEvent::kReferenceSelection,oldest);
  auto jobs=marks.inspector_jobs();
  std::reverse(jobs.begin(),jobs.end());
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kBackgroundCompletion);
  Check(state.selected_id==oldest,"ordinary refresh/reorder preserves explicit selection");
  const auto arrival=save('2',4'000'000);
  const auto before_capture=state.selection_generation;
  state=InspectorAfterCapture(state,arrival,marks.inspector_jobs());
  Check(state.selected_id==arrival && state.selection_generation==before_capture+1,
        "a genuine accepted new capture selects its own ID despite valid old selection");
  const auto selected_capture=state.selection_generation;
  for(const auto& missing:{std::string{},std::string(32,'0')})
    state=InspectorAfterCapture(state,missing,marks.inspector_jobs());
  Check(state.selected_id==arrival && state.selection_generation==selected_capture,
        "missing or unaccepted capture ID never changes selection or generation");
  auto failed_jobs=marks.inspector_jobs();
  for(auto& job:failed_jobs)if(job.id==oldest)job.state=ReferenceJobState::kFailed;
  state=InspectorAfterCapture(state,oldest,failed_jobs);
  Check(state.selected_id==arrival && state.selection_generation==selected_capture,
        "failed captured ID never jumps to another selection");
  for(auto& job:failed_jobs)if(job.id==oldest)job.state=ReferenceJobState::kDeleted;
  state=InspectorAfterCapture(state,oldest,failed_jobs);
  Check(state.selected_id==arrival && state.selection_generation==selected_capture,
        "deleted captured ID never restores its selection");
  // The stored menu item binds oldest, even after the rows reorder/add a ref.
  Check(InspectorReferenceAvailable(marks.inspector_jobs(),oldest),
        "a retained menu ID resolves independently of current row positions");
  const auto apply=[&](const std::string& id) {
    const auto result=marks.DeleteHistoryJob(id);
    state=InspectorAfterSuccessfulDelete(state,result,marks.inspector_jobs());
    return result;
  };
  Check(apply(middle)==DeleteResult::kDeleted && state.selected_id==arrival,
        "successful nonselected deletion selects true newest remaining");
  const auto before_refresh=state.selection_generation;
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kBackgroundCompletion);
  Check(state.selected_id==arrival && state.selection_generation==before_refresh,
        "delete event followed by ordinary refresh does not undo newest selection");
  Check(apply(arrival)==DeleteResult::kDeleted && state.selected_id==newest,
        "deleting current newest chooses next newest, not lexical last");
  Check(apply(oldest)==DeleteResult::kDeleted && state.selected_id==newest,
        "deleting older nonselected reference still resolves remaining chronology");
  Check(!InspectorReferenceAvailable(marks.inspector_jobs(),oldest),
        "a deleted retained menu item cannot become an action target");
  Check(apply(newest)==DeleteResult::kDeleted && state.selected_id.empty(),
        "last genuine deletion clears the shared selection and preview target");
  const auto tied_a=save('3',5'000'000),tied_b=save('4',5'000'000);
  const auto extra=save('5',2'000'000);
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kReferenceSelection,tied_a);
  Check(apply(extra)==DeleteResult::kDeleted && state.selected_id==tied_b,
        "equal real timestamps use deterministic descending ID tie break");
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kReferenceSelection,tied_a);
  const auto old_generation=state.selection_generation;
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kReferenceSelection,tied_b);
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kReferenceSelection,tied_a);
  Check(!InspectorPresentationIsCurrent(state,tied_a,old_generation) &&
         InspectorPresentationIsCurrent(state,tied_a,state.selection_generation),
        "A/B/A rejects an old async publication despite matching ID");
  const auto before_failure=state.selection_generation;
  std::filesystem::create_directory(directory/(tied_a+".deleted"));
  Check(apply(tied_a)==DeleteResult::kCleanupFailed && state.selected_id==tied_a &&
        state.selection_generation==before_failure,
        "failed deletion preserves still valid selection and callback identity");
  std::filesystem::remove(directory/(tied_a+".deleted"));
  Check(apply(std::string(32,'7'))==DeleteResult::kNotFound &&
        state.selected_id==tied_a,"not-found deletion never jumps to newest");
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kBackgroundCompletion);
  Check(state.selected_id==tied_a,"cancellation/no deletion does not infer success");
  hooks.remove_owned_file=[&](const std::filesystem::path& path) {
    if(path.filename()==tied_a+".source.png")return false;
    errno=0;return unlink(path.c_str())==0||errno==ENOENT;
  };
  store.SetTestHooks(hooks);
  Check(apply(tied_a)==DeleteResult::kCleanupFailed &&
        store.LookupMetadata(tied_a)->state==ReferenceJobState::kDeleted &&
        state.selected_id==tied_a,
        "post-tombstone cleanup failure is not mistaken for successful deletion");
  store.SetTestHooks({});
  Check(apply(tied_a)==DeleteResult::kAlreadyDeleted && state.selected_id==tied_a,
        "already-deleted repeat is not a fresh successful deletion event");
  state=ReduceInspectorLifecycle(state,InspectorLifecycleEvent::kReferenceSelection,tied_b);
  Check(apply(tied_b)==DeleteResult::kDeleted && state.selected_id.empty(),
        "continuous deletion ends at genuinely empty selection");
  std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
  TestHoldMoveRelease();
  TestActiveAndEmptyRelease();
  TestCancellationRoutes();
  TestCaptureReleaseOrders();
  TestWatchdog();
  TestGeometryAndScale();
  TestValidationAndHitRegions();
  TestDeleteGestureReducer();
  TestContinuousDeleteAgainstDistinctMarks();
  TestReferenceClipboardPolicy();
#if defined(__APPLE__) && defined(SEETHIS_CLIPBOARD_MAC_TESTING)
  TestAnnotatedClipboardOnNamedPasteboard();
#endif
  TestPendingReferenceActions();
  TestInspectorHistoryCopyAcrossApps();
  TestInspectorHistoryDelete();
  TestInspectorSelectionPolicy();
  TestInspectorDisplayAssociationProjection();
  TestDisplayLeaveReentrySubpaths();
  TestChromeObservationLifecycle();
  TestIndependentChromeConnectionRecovery();
  TestInspectorConnectionHealthAndEffectiveBindings();
  TestReferenceDropdownSelectionAfterRealDeletion();
#if defined(SEETHIS_WINDOW_OBSERVATION_API)
  TestChromeCaptureIdentityFreeze();
  TestQuickReleaseChromeTerminalOutcomes();
#endif
  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "interaction_test: all checks passed\n";
  return EXIT_SUCCESS;
}
