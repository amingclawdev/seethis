#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <os/log.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "platform/platform.h"
#include "service/reference_server.h"

namespace { seethis::platform::EffectiveShortcutPublication effective_shortcuts; }

constexpr CGKeyCode kEscapeKeyCode = 53;
constexpr UInt32 kCaptureHotKeySignature = 0x53544350;  // STCP
constexpr UInt32 kDeleteHotKeySignature = 0x5354444c;   // STDL

CGEventFlags NativeModifiers(std::uint32_t modifiers) {
  CGEventFlags flags = 0;
  if (modifiers & seethis::core::kShortcutCommand) flags |= kCGEventFlagMaskCommand;
  if (modifiers & seethis::core::kShortcutControl) flags |= kCGEventFlagMaskControl;
  if (modifiers & seethis::core::kShortcutOption) flags |= kCGEventFlagMaskAlternate;
  if (modifiers & seethis::core::kShortcutShift) flags |= kCGEventFlagMaskShift;
  return flags;
}

UInt32 CarbonModifiers(std::uint32_t modifiers) {
  UInt32 flags = 0;
  if (modifiers & seethis::core::kShortcutCommand) flags |= cmdKey;
  if (modifiers & seethis::core::kShortcutControl) flags |= controlKey;
  if (modifiers & seethis::core::kShortcutOption) flags |= optionKey;
  if (modifiers & seethis::core::kShortcutShift) flags |= shiftKey;
  return flags;
}

CGEventFlags ShortcutModifierFlags(CGEventFlags flags) {
  return flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl |
                  kCGEventFlagMaskAlternate | kCGEventFlagMaskShift);
}

std::int64_t MonotonicMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

seethis::core::DeleteEventProvenance CarbonEventProvenance(
    EventTime seconds_since_boot) {
  return seethis::core::DeleteEventProvenanceFromSeconds(seconds_since_boot);
}

seethis::core::DeleteEventProvenance CurrentCarbonEventProvenance() {
  return CarbonEventProvenance(GetCurrentEventTime());
}

seethis::core::DeleteEventProvenance RawEventProvenance(CGEventRef event) {
  if (event == nullptr) return {0, 0, true};
  const std::uint64_t timestamp = CGEventGetTimestamp(event);
  return {timestamp, timestamp, true};
}

pid_t FrontmostProcessIdentifier() {
  NSRunningApplication* application = NSWorkspace.sharedWorkspace.frontmostApplication;
  return application == nil ? 0 : application.processIdentifier;
}

class MacInputAdapter final : public seethis::platform::InputAdapter {
 public:
  MacInputAdapter(seethis::core::InteractionController* controller,
                  seethis::platform::OverlayAdapter* overlay,
                  seethis::core::SettingsStore* settings,
                  std::shared_ptr<seethis::platform::PermissionReadinessState>
                      readiness,
                  seethis::core::DeleteGestureController* deletion)
      : controller_(controller),
        overlay_(overlay),
        settings_(settings),
        readiness_(std::move(readiness)),
        deletion_(deletion) {}

  ~MacInputAdapter() override { Stop(); }

  bool Start() override {
    if (started_) {
      return seethis::platform::InputMonitorReady(
          readiness_->input_monitor.load());
    }
    started_ = true;
    observed_retry_generation_ = readiness_->retry_generation.load();
    RefreshScreenRecording();
    InstallHotKeyHandler();
    RegisterCaptureHotKey();
    RegisterDeleteHotKey();

    MacInputAdapter* self_pointer = this;
    watchdog_timer_ = [NSTimer timerWithTimeInterval:0.10
                                               repeats:YES
                                                 block:^(NSTimer*) {
                                                   MacInputAdapter* self = self_pointer;
                                                   if (self != nullptr) {
                                                     self->RunWatchdog();
                                                   }
                                                 }];
    // Common modes keep the bounded recovery active while AppKit is tracking
    // a drag or other event sequence.
    [NSRunLoop.mainRunLoop addTimer:watchdog_timer_
                            forMode:NSRunLoopCommonModes];

    NSNotificationCenter* app_center = NSNotificationCenter.defaultCenter;
    resign_observer_ = [app_center
        addObserverForName:NSApplicationDidResignActiveNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                  MacInputAdapter* self = self_pointer;
                  if (self != nullptr) {
                    self->Cancel(
                        seethis::core::CancelReason::kApplicationResignedActive);
                  }
                }];
    display_observer_ = [app_center
        addObserverForName:NSApplicationDidChangeScreenParametersNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                  MacInputAdapter* self = self_pointer;
                  if (self != nullptr) {
                    self->DisplayConfigurationChanged();
                  }
                }];
    workspace_observer_ = [NSWorkspace.sharedWorkspace.notificationCenter
        addObserverForName:NSWorkspaceDidActivateApplicationNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                  MacInputAdapter* self = self_pointer;
                  if (self != nullptr) {
                    self->ApplicationActivated();
                  }
                }];
    const bool ready = InstallTap(false);
    RefreshDeleteReadiness();
    return ready;
  }

  void RetryInputMonitoring() override {
    if (!started_) {
      Start();
      return;
    }
    readiness_latch_.Reset();
    interruption_state_.Clear();
    InstallTap(true);
    RegisterCaptureHotKey();
    RegisterDeleteHotKey();
  }

  void Stop() override {
    interruption_state_.Clear();
    if (watchdog_timer_ != nil) {
      [watchdog_timer_ invalidate];
      watchdog_timer_ = nil;
    }
    if (resign_observer_ != nil) {
      [NSNotificationCenter.defaultCenter removeObserver:resign_observer_];
      resign_observer_ = nil;
    }
    if (display_observer_ != nil) {
      [NSNotificationCenter.defaultCenter removeObserver:display_observer_];
      display_observer_ = nil;
    }
    if (workspace_observer_ != nil) {
      [NSWorkspace.sharedWorkspace.notificationCenter
          removeObserver:workspace_observer_];
      workspace_observer_ = nil;
    }
    DestroyTap();
    DestroyDeleteHotKey();
    deletion_->Configure(++delete_generation_, false, false, false,
                         CurrentCarbonEventProvenance());
    readiness_->delete_shortcut.store(
        seethis::platform::DeleteShortcutState::kUnknown);
    readiness_->capture_shortcut.store(
        seethis::platform::CaptureShortcutState::kUnknown);
    effective_shortcuts.Stop();
    started_ = false;
  }

 private:
  static constexpr CGEventMask KeyboardMask() {
    return CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
           CGEventMaskBit(kCGEventFlagsChanged);
  }

  static constexpr CGEventMask RequestedMask() {
    return KeyboardMask() | CGEventMaskBit(kCGEventMouseMoved) |
           CGEventMaskBit(kCGEventLeftMouseDragged) |
           CGEventMaskBit(kCGEventRightMouseDragged) |
           CGEventMaskBit(kCGEventOtherMouseDragged);
  }

  void DestroyTap() {
    if (run_loop_source_ != nullptr) {
      CFRunLoopRemoveSource(CFRunLoopGetMain(), run_loop_source_,
                            kCFRunLoopCommonModes);
      CFRelease(run_loop_source_);
      run_loop_source_ = nullptr;
    }
    if (event_tap_ != nullptr) {
      CFRelease(event_tap_);
      event_tap_ = nullptr;
    }
  }

  void DestroyDeleteHotKey() {
    if (capture_hotkey_ != nullptr) {
      UnregisterEventHotKey(capture_hotkey_);
      capture_hotkey_ = nullptr;
    }
    if (delete_hotkey_ != nullptr) {
      UnregisterEventHotKey(delete_hotkey_);
      delete_hotkey_ = nullptr;
    }
    if (hotkey_handler_ != nullptr) {
      RemoveEventHandler(hotkey_handler_);
      hotkey_handler_ = nullptr;
    }
    delete_registered_ = false;
    capture_registered_ = false;
  }

  void InstallHotKeyHandler() {
    if (hotkey_handler_ != nullptr) return;
    const EventTypeSpec events[] = {
        {kEventClassKeyboard, kEventHotKeyPressed},
        {kEventClassKeyboard, kEventHotKeyReleased},
    };
    if (InstallApplicationEventHandler(&MacInputAdapter::HotKeyCallback,
          static_cast<UInt32>(std::size(events)), events, this,
          &hotkey_handler_) != noErr) {
      hotkey_handler_ = nullptr;
    }
  }

  void RegisterCaptureHotKey() {
    if (capture_hotkey_ != nullptr) {
      UnregisterEventHotKey(capture_hotkey_);
      capture_hotkey_ = nullptr;
    }
    capture_registered_ = false;
    ++capture_generation_;
    active_capture_conflict_ = false;
    if (!settings_->usable()) {
      RefreshCaptureReadiness();
      LogCaptureStage("capture-registration", "settings-unavailable",
                      MonotonicMilliseconds());
      return;
    }
    const auto settings = settings_->Get();
    capture_eligibility_.ObserveBoundary(CurrentCarbonEventProvenance());
    active_capture_conflict_ =
        seethis::core::ShortcutBindingsConflict(settings);
    active_capture_key_ = static_cast<CGKeyCode>(settings.shortcut_key_code);
    active_capture_modifiers_ = NativeModifiers(settings.shortcut_modifiers);
    effective_capture_modifiers_ = settings.shortcut_modifiers;
    if (active_capture_conflict_) {
      RefreshCaptureReadiness();
      LogCaptureStage("capture-registration", "conflict",
                      MonotonicMilliseconds());
      return;
    }
    EventHotKeyID identity{kCaptureHotKeySignature,
                           static_cast<UInt32>(capture_generation_)};
    const OSStatus registered = hotkey_handler_ == nullptr ? eventInternalErr :
        RegisterEventHotKey(settings.shortcut_key_code,
                            CarbonModifiers(settings.shortcut_modifiers),
                            identity, GetApplicationEventTarget(),
                            kEventHotKeyExclusive, &capture_hotkey_);
    capture_registered_ = registered == noErr && capture_hotkey_ != nullptr;
    RefreshCaptureReadiness();
    LogCaptureStage("capture-registration",
                    capture_registered_ ? "ready" : "failed",
                    MonotonicMilliseconds());
  }

  void RegisterDeleteHotKey() {
    if (delete_hotkey_ != nullptr) {
      UnregisterEventHotKey(delete_hotkey_);
      delete_hotkey_ = nullptr;
    }
    delete_registered_ = false;
    ++delete_generation_;
    if (!settings_->usable()) {
      active_delete_conflict_ = false;
      readiness_->delete_shortcut.store(
          seethis::platform::DeleteShortcutState::kSettingsUnavailable);
      deletion_->Configure(delete_generation_, false,
          seethis::platform::InputMonitorReady(
              readiness_->input_monitor.load()),
          DeleteKeyIsDown(), CurrentCarbonEventProvenance());
      PublishDeleteBinding();
      LogDeleteStage("registration-settings-unavailable");
      return;
    }
    const auto settings = settings_->Get();
    active_delete_conflict_ =
        seethis::core::ShortcutBindingsConflict(settings);
    active_delete_key_ = static_cast<CGKeyCode>(
        settings.delete_shortcut_key_code);
    active_delete_modifiers_ = NativeModifiers(
        settings.delete_shortcut_modifiers);
    effective_delete_modifiers_ = settings.delete_shortcut_modifiers;
    if (active_delete_conflict_) {
      readiness_->delete_shortcut.store(
          seethis::platform::DeleteShortcutState::kConflict);
      deletion_->Configure(delete_generation_, false, false,
                           DeleteKeyIsDown(), CurrentCarbonEventProvenance());
      PublishDeleteBinding();
      LogDeleteStage("registration-conflict");
      return;
    }
    EventHotKeyID identity{kDeleteHotKeySignature,
                           static_cast<UInt32>(delete_generation_)};
    const OSStatus registered = hotkey_handler_ == nullptr ? eventInternalErr :
        RegisterEventHotKey(settings.delete_shortcut_key_code,
                            CarbonModifiers(settings.delete_shortcut_modifiers),
                            identity, GetApplicationEventTarget(),
                            kEventHotKeyExclusive, &delete_hotkey_);
    delete_registered_ = registered == noErr && delete_hotkey_ != nullptr;
    deletion_->Configure(delete_generation_, delete_registered_,
        seethis::platform::InputMonitorReady(readiness_->input_monitor.load()),
        DeleteKeyIsDown(), CurrentCarbonEventProvenance());
    RefreshDeleteReadiness();
    LogDeleteStage(delete_registered_ ? "registration-ready"
                                      : "registration-failed");
  }

  static OSStatus HotKeyCallback(EventHandlerCallRef next_handler,
                                 EventRef event, void* user_info) {
    (void)next_handler;
    auto* self = static_cast<MacInputAdapter*>(user_info);
    EventHotKeyID identity{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
          nullptr, sizeof(identity), nullptr, &identity) != noErr) {
      return eventNotHandledErr;
    }
    const bool is_capture = identity.signature == kCaptureHotKeySignature;
    const bool is_delete = identity.signature == kDeleteHotKeySignature;
    if ((!is_capture || identity.id !=
             static_cast<UInt32>(self->capture_generation_)) &&
        (!is_delete || identity.id !=
             static_cast<UInt32>(self->delete_generation_))) {
      return eventNotHandledErr;
    }
    if (is_capture) {
      if (!self->capture_registered_ || !self->settings_->usable() ||
          !seethis::platform::InputMonitorReady(
              self->readiness_->input_monitor.load())) {
        return eventNotHandledErr;
      }
      const auto carbon_provenance =
          CarbonEventProvenance(GetEventTime(event));
      const CGEventFlags current_flags = CGEventSourceFlagsState(
          kCGEventSourceStateCombinedSessionState);
      const bool exact_chord = self->CaptureKeyIsDown() &&
          self->CaptureModifiersAreExact(current_flags);
      const auto pointer = self->CurrentPointer();
      const auto observed_ms = MonotonicMilliseconds();
      if (GetEventKind(event) == kEventHotKeyPressed) {
        const auto start = exact_chord &&
            self->CapturePressEligible(carbon_provenance) && pointer
            ? self->controller_->ShortcutKeyDown(observed_ms, *pointer)
            : seethis::core::StartResult::kInvalidPoint;
        self->LogCaptureStage("capture-hotkey-down",
            start == seethis::core::StartResult::kStarted ? "started" :
            start == seethis::core::StartResult::kIgnoredRepeat ? "repeat" :
            "invalid-point", observed_ms);
        if (start == seethis::core::StartResult::kStarted) {
          self->ResetCaptureReleaseTracking();
          self->foreground_process_ = FrontmostProcessIdentifier();
          self->active_key_code_ = self->active_capture_key_;
          self->active_modifiers_ = self->active_capture_modifiers_;
          self->overlay_->Refresh();
          self->overlay_->UpdatePointerPolicy(pointer);
        }
      } else if (GetEventKind(event) == kEventHotKeyReleased) {
        self->RecordCaptureEligibilityBoundary(
            CurrentCarbonEventProvenance());
        if (pointer) (void)self->controller_->PointerUp(*pointer);
        const bool finished = self->controller_->ShortcutKeyUp(observed_ms);
        self->LogCaptureStage("capture-hotkey-release",
                              finished ? "completed" : "ignored", observed_ms);
        if (finished) {
          self->foreground_process_ = 0;
          self->active_key_code_.reset();
          self->pending_capture_key_release_ = false;
          self->pending_capture_modifier_release_ = false;
          self->overlay_->Refresh();
          self->overlay_->UpdatePointerPolicy(pointer);
        }
      }
      return noErr;
    }
    const CGEventFlags current_flags = CGEventSourceFlagsState(
        kCGEventSourceStateCombinedSessionState);
    const auto carbon_provenance = CarbonEventProvenance(GetEventTime(event));
    const bool carbon_provenance_valid = carbon_provenance.valid();
    if (GetEventKind(event) == kEventHotKeyPressed)
      self->deletion_->HotKeyPressed(
          self->delete_generation_,
          carbon_provenance_valid && self->DeleteKeyIsDown(),
          carbon_provenance_valid &&
              self->DeleteModifiersAreExact(current_flags), carbon_provenance);
    else if (GetEventKind(event) == kEventHotKeyReleased)
      self->deletion_->HotKeyReleased(self->delete_generation_,
                                      carbon_provenance);
    else return eventNotHandledErr;
    self->LogDeleteStage(GetEventKind(event) == kEventHotKeyPressed
                             ? "carbon-pressed" : "carbon-released");
    self->overlay_->Refresh();
    self->overlay_->UpdatePointerPolicy(self->CurrentPointer());
    return noErr;
  }

  bool DeleteKeyIsDown() const {
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                 active_delete_key_);
  }

  bool DeleteModifiersAreExact(CGEventFlags flags) const {
    return ShortcutModifierFlags(flags) == active_delete_modifiers_;
  }

  bool CaptureKeyIsDown() const {
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                 active_capture_key_);
  }

  bool CaptureModifiersAreExact(CGEventFlags flags) const {
    return ShortcutModifierFlags(flags) == active_capture_modifiers_;
  }

  bool CapturePressEligible(
      seethis::core::DeleteEventProvenance provenance) const {
    return capture_eligibility_.AcceptPress(
        CaptureKeyIsDown(),
        CaptureModifiersAreExact(CGEventSourceFlagsState(
            kCGEventSourceStateCombinedSessionState)), provenance);
  }

  void RecordCaptureEligibilityBoundary(
      seethis::core::DeleteEventProvenance boundary) {
    capture_eligibility_.ObserveBoundary(boundary);
  }

  void RefreshDeleteReadiness() {
    const bool input_ready = seethis::platform::InputMonitorReady(
        readiness_->input_monitor.load());
    const bool secure = IsSecureEventInputEnabled();
    seethis::platform::DeleteShortcutState state;
    if (!settings_->usable())
      state = seethis::platform::DeleteShortcutState::kSettingsUnavailable;
    else if (seethis::core::ShortcutBindingsConflict(settings_->Get()))
      state = seethis::platform::DeleteShortcutState::kConflict;
    else if (!delete_registered_)
      state = seethis::platform::DeleteShortcutState::kRegistrationFailed;
    else if (secure)
      state = seethis::platform::DeleteShortcutState::kSecureInput;
    else if (!input_ready)
      state = seethis::platform::DeleteShortcutState::kInputUnavailable;
    else
      state = seethis::platform::DeleteShortcutState::kReady;
    readiness_->delete_shortcut.store(state);
    PublishDeleteBinding();
    deletion_->ObservationReadinessChanged(
        state == seethis::platform::DeleteShortcutState::kReady,
        DeleteKeyIsDown(), CurrentCarbonEventProvenance());
  }

  void RefreshCaptureReadiness() {
    const bool settings_available = settings_->usable();
    const bool conflict = settings_available &&
        seethis::core::ShortcutBindingsConflict(settings_->Get());
    readiness_->capture_shortcut.store(
        seethis::platform::CaptureShortcutReadiness(
            settings_available, conflict, capture_registered_,
            seethis::platform::InputMonitorReady(
                readiness_->input_monitor.load())));
    effective_shortcuts.Capture({active_capture_key_,effective_capture_modifiers_,
        capture_registered_},readiness_->capture_shortcut.load());
  }

  void PublishDeleteBinding() {
    effective_shortcuts.Delete({active_delete_key_,effective_delete_modifiers_,
        delete_registered_},readiness_->delete_shortcut.load());
  }

  void RefreshScreenRecording() {
    readiness_->screen_recording.store(
        seethis::platform::ScreenRecordingReady()
            ? seethis::platform::ScreenRecordingState::kReady
            : seethis::platform::ScreenRecordingState::kDenied);
    effective_shortcuts.Screen(readiness_->screen_recording.load());
  }

  seethis::platform::InputMonitorProbe ProbeTap(bool retry_attempt) const {
    seethis::platform::InputMonitorProbe probe;
    probe.listen_authorized = CGPreflightListenEventAccess();
    probe.tap_created = event_tap_ != nullptr && run_loop_source_ != nullptr;
    probe.retry_attempt = retry_attempt;
    if (!probe.tap_created) return interruption_state_.Apply(probe);
    probe.tap_enabled = CGEventTapIsEnabled(event_tap_);
    uint32_t count = 0;
    if (CGGetEventTapList(0, nullptr, &count) != kCGErrorSuccess || count == 0) {
      return interruption_state_.Apply(probe);
    }
    std::vector<CGEventTapInformation> taps(count);
    if (CGGetEventTapList(count, taps.data(), &count) != kCGErrorSuccess) {
      return interruption_state_.Apply(probe);
    }
    const pid_t process = NSProcessInfo.processInfo.processIdentifier;
    for (uint32_t index = 0; index < count; ++index) {
      const auto& tap = taps[index];
      if (tap.tappingProcess == process && tap.tapPoint == kCGSessionEventTap &&
          tap.options == kCGEventTapOptionListenOnly &&
          (tap.eventsOfInterest & KeyboardMask()) == KeyboardMask()) {
        probe.keyboard_events_present = true;
        probe.tap_enabled = probe.tap_enabled && tap.enabled;
        break;
      }
    }
    return interruption_state_.Apply(probe);
  }

  bool InstallTap(bool retry_attempt) {
    Cancel(seethis::core::CancelReason::kInputMonitorInterrupted);
    DestroyTap();
    RefreshScreenRecording();
    if (!CGPreflightListenEventAccess()) {
      readiness_->input_monitor.store(seethis::platform::ClassifyInputMonitor(
          {.retry_attempt = retry_attempt}));
      RefreshCaptureReadiness();
      return false;
    }
    event_tap_ = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
        RequestedMask(), &MacInputAdapter::EventTapCallback, this);
    if (event_tap_ != nullptr) {
      run_loop_source_ =
          CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap_, 0);
      if (run_loop_source_ != nullptr) {
        CFRunLoopAddSource(CFRunLoopGetMain(), run_loop_source_,
                           kCFRunLoopCommonModes);
        CGEventTapEnable(event_tap_, true);
      }
    }
    const auto state =
        seethis::platform::ClassifyInputMonitor(ProbeTap(retry_attempt));
    readiness_->input_monitor.store(state);
    RefreshDeleteReadiness();
    RefreshCaptureReadiness();
    return seethis::platform::InputMonitorReady(state);
  }

  static CGEventRef EventTapCallback(CGEventTapProxy proxy, CGEventType type,
                                     CGEventRef event, void* user_info) {
    (void)proxy;
    auto* self = static_cast<MacInputAdapter*>(user_info);
    self->HandleEvent(type, event);
    return event;
  }

  std::optional<seethis::core::DisplayPoint> CurrentPointer() {
    if (last_pointer_.has_value()) return last_pointer_;
    const NSPoint point = NSEvent.mouseLocation;
    last_pointer_ = overlay_->DisplayPointAtGlobalLogical({point.x, point.y});
    return last_pointer_;
  }

  std::optional<seethis::core::DisplayPoint> PointerFromEvent(
      CGEventRef event) {
    if (event == nullptr || NSScreen.screens.firstObject == nil) {
      last_pointer_.reset();
      return {};
    }
    const CGPoint location = CGEventGetLocation(event);
    const double primary_top = NSMaxY(NSScreen.screens.firstObject.frame);
    const auto global = seethis::platform::AppKitGlobalPointFromQuartz(
        {location.x, location.y}, primary_top);
    last_pointer_ = global
        ? overlay_->DisplayPointAtGlobalLogical(*global)
        : std::optional<seethis::core::DisplayPoint>{};
    return last_pointer_;
  }

  void LogDeleteStage(const char* stage, bool autorepeat = false) const {
    const auto snapshot = deletion_->Snapshot();
    std::cerr << "seethis: physical-delete stage=" << stage
              << " generation=" << snapshot.registration_generation
              << " state="
              << seethis::core::DeleteGestureStateName(snapshot.state)
              << " key_down=" << snapshot.key_down
              << " exact_modifiers=" << snapshot.exact_modifiers
              << " autorepeat=" << autorepeat << '\n';
  }

  void LogCaptureStage(const char* stage, const char* result,
                       std::int64_t observed_ms,
                       std::int64_t release_pair_delta_ms = -1,
                       std::optional<std::size_t> region_count = {}) const {
    const auto snapshot = controller_->Snapshot();
    os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_INFO,
        "seethis capture stage=%{public}s result=%{public}s state=%{public}s "
        "completion=%{public}s cancel=%{public}s regions=%{public}llu "
        "observed_ms=%{public}lld pair_delta_ms=%{public}lld",
        stage, result,
        seethis::core::InteractionStateName(snapshot.state),
        seethis::core::CompletionReasonName(snapshot.completion_reason),
        seethis::core::CancelReasonName(snapshot.cancel_reason),
        static_cast<unsigned long long>(region_count.value_or(
            snapshot.regions.size())),
        static_cast<long long>(observed_ms),
        static_cast<long long>(release_pair_delta_ms));
  }

  void ResetCaptureReleaseTracking() {
    pending_capture_key_release_ = false;
    pending_capture_key_release_after_watchdog_ = false;
    pending_capture_modifier_release_ = false;
    capture_release_first_ms_ = 0;
  }

  void LogReleasePair(const char* order, std::int64_t observed_ms) const {
    const std::int64_t delta = observed_ms >= capture_release_first_ms_
        ? observed_ms - capture_release_first_ms_ : -1;
    LogCaptureStage(delta >= 0 && delta <= kNearSimultaneousReleaseMs
                        ? "release-pair-near-simultaneous"
                        : "release-pair-delayed",
                    order, observed_ms, delta);
  }

  void HandleEvent(CGEventType type, CGEventRef event) {
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
      interruption_state_.Record(
          type == kCGEventTapDisabledByTimeout
              ? seethis::platform::InputInterruptionKind::kTimeout
              : seethis::platform::InputInterruptionKind::kUserInput);
      Cancel(seethis::core::CancelReason::kInputMonitorInterrupted);
      readiness_->input_monitor.store(
          seethis::platform::InputMonitorState::kDisabled);
      last_pointer_.reset();
      overlay_->UpdatePointerPolicy({});
      RefreshDeleteReadiness();
      RefreshCaptureReadiness();
      return;
    }

    if (type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
        type == kCGEventRightMouseDragged ||
        type == kCGEventOtherMouseDragged) {
      const auto event_pointer = PointerFromEvent(event);
      if (event_pointer.has_value() &&
          controller_->PointerMoved(*event_pointer)) {
        overlay_->Refresh();
      }
      overlay_->UpdatePointerPolicy(event_pointer);
      return;
    }

    if (!seethis::platform::InputMonitorReady(
            readiness_->input_monitor.load())) {
      return;
    }
    if (!settings_->usable()) return;
    const auto event_pointer = CurrentPointer();

    const auto key_code = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    const CGEventFlags flags = CGEventGetFlags(event);
    const auto settings = settings_->Get();
    const auto shortcut_key = static_cast<CGKeyCode>(settings.shortcut_key_code);
    const auto shortcut_modifiers = NativeModifiers(settings.shortcut_modifiers);
    if (type == kCGEventKeyDown && key_code == kEscapeKeyCode) {
      deletion_->Interrupt(DeleteKeyIsDown(), CurrentCarbonEventProvenance());
      Cancel(seethis::core::CancelReason::kUser);
      return;
    }
    if (key_code == active_delete_key_) {
      if (type == kCGEventKeyDown) {
        const bool repeat = CGEventGetIntegerValueField(
            event, kCGKeyboardEventAutorepeat) != 0;
        deletion_->KeyDown(DeleteModifiersAreExact(flags), repeat,
                           RawEventProvenance(event));
        LogDeleteStage("raw-down", repeat);
      } else if (type == kCGEventKeyUp) {
        deletion_->KeyUp(RawEventProvenance(event));
        LogDeleteStage("raw-up");
      }
      overlay_->Refresh();
      overlay_->UpdatePointerPolicy(event_pointer);
      return;
    }
    if (!capture_registered_ && type == kCGEventKeyDown &&
        key_code == shortcut_key &&
        (flags & shortcut_modifiers) == shortcut_modifiers) {
      LogCaptureStage("capture-key-down", "registration-unavailable",
                      MonotonicMilliseconds());
      return;
    }
    if (!capture_registered_ && type == kCGEventKeyUp &&
        key_code == shortcut_key &&
        ((active_key_code_.has_value() && key_code == *active_key_code_) ||
         pending_capture_key_release_)) {
      const auto observed_ms = MonotonicMilliseconds();
      const bool was_pending = pending_capture_key_release_;
      if (event_pointer) (void)controller_->PointerUp(*event_pointer);
      const bool finished = controller_->ShortcutKeyUp(observed_ms);
      if (was_pending) {
        LogReleasePair(pending_capture_key_release_after_watchdog_
                           ? "watchdog-before-queued-capture-key"
                           : "modifier-before-capture-key",
                       observed_ms);
        pending_capture_key_release_ = false;
        pending_capture_key_release_after_watchdog_ = false;
      } else {
        LogCaptureStage("capture-key-release",
                        finished ? "completed" : "ignored", observed_ms);
      }
      if (finished) {
        capture_release_first_ms_ = observed_ms;
        pending_capture_modifier_release_ =
            (flags & active_modifiers_) == active_modifiers_;
        if (!pending_capture_modifier_release_)
          LogReleasePair("combined-observation-order-unknown", observed_ms);
        foreground_process_ = 0;
        active_key_code_.reset();
        overlay_->Refresh();
        overlay_->UpdatePointerPolicy(event_pointer);
      }
      return;
    }
    if (type == kCGEventFlagsChanged &&
        (flags & active_modifiers_) != active_modifiers_) {
      const auto observed_ms = MonotonicMilliseconds();
      RecordCaptureEligibilityBoundary(CurrentCarbonEventProvenance());
      if (controller_->Snapshot().state ==
          seethis::core::InteractionState::kDrawing) {
        if (event_pointer) (void)controller_->PointerUp(*event_pointer);
        const bool finished = controller_->ShortcutModifierUp(observed_ms);
        LogCaptureStage("modifier-release",
                        finished ? "completed" : "ignored", observed_ms);
        if (finished) {
          capture_release_first_ms_ = observed_ms;
          pending_capture_key_release_ = active_key_code_.has_value() &&
              CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                    *active_key_code_);
          pending_capture_key_release_after_watchdog_ = false;
          if (!pending_capture_key_release_)
            LogReleasePair("combined-observation-order-unknown", observed_ms);
          pending_capture_modifier_release_ = false;
          foreground_process_ = 0;
          active_key_code_.reset();
          overlay_->Refresh();
          overlay_->UpdatePointerPolicy(event_pointer);
        }
      } else if (pending_capture_modifier_release_) {
        (void)controller_->ShortcutModifierUp(observed_ms);
        LogReleasePair("capture-key-before-modifier", observed_ms);
        pending_capture_modifier_release_ = false;
      }
    }
    if (type == kCGEventFlagsChanged) {
      deletion_->ModifiersChanged(DeleteModifiersAreExact(flags));
      LogDeleteStage("raw-flags");
      overlay_->Refresh();
      overlay_->UpdatePointerPolicy(event_pointer);
    }
  }

  void Cancel(seethis::core::CancelReason reason) {
    deletion_->Interrupt(DeleteKeyIsDown(), CurrentCarbonEventProvenance());
    LogDeleteStage("interrupted");
    RecordCaptureEligibilityBoundary(CurrentCarbonEventProvenance());
    const auto before = controller_->Snapshot();
    const auto observed_ms = MonotonicMilliseconds();
    if (controller_->Cancel(reason)) {
      LogCaptureStage("cancelled", seethis::core::CancelReasonName(reason),
                      observed_ms, -1, before.regions.size());
      ResetCaptureReleaseTracking();
      foreground_process_ = 0;
      active_key_code_.reset();
      overlay_->Refresh();
      overlay_->UpdatePointerPolicy(CurrentPointer());
    }
  }

  void RunWatchdog() {
    const auto requested_retry = readiness_->retry_generation.load();
    if (requested_retry != observed_retry_generation_) {
      observed_retry_generation_ = requested_retry;
      RetryInputMonitoring();
    }
    if (++readiness_tick_ >= 10) {
      readiness_tick_ = 0;
      RefreshScreenRecording();
      const auto now_ms = MonotonicMilliseconds();
      const auto initial_probe = ProbeTap(false);
      auto decision = readiness_latch_.ObserveFresh(initial_probe, now_ms);
      const auto observed_state = decision.state;
      const auto interruption = initial_probe.interruption;
      const auto recovery_action = decision.action;
      const auto recovery_attempts = decision.recovery_attempts;
      readiness_->input_monitor.store(decision.state);
      if (recovery_action != seethis::platform::InputRecoveryAction::kNone) {
        Cancel(seethis::core::CancelReason::kInputMonitorInterrupted);
        interruption_state_.Clear();
        if (recovery_action ==
                seethis::platform::InputRecoveryAction::kReenableTap &&
            event_tap_ != nullptr) {
          CGEventTapEnable(event_tap_, true);
        } else if (recovery_action ==
                   seethis::platform::InputRecoveryAction::kRecreateTap) {
          InstallTap(true);
        }
        decision = readiness_latch_.ObserveFresh(ProbeTap(true),
                                                 MonotonicMilliseconds());
        readiness_->input_monitor.store(decision.state);
      }
      if (interruption != seethis::platform::InputInterruptionKind::kNone ||
          recovery_action != seethis::platform::InputRecoveryAction::kNone) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_INFO,
            "seethis input-readiness reason=%{public}s state=%{public}s "
            "observed_ms=%{public}lld interruption=%{public}s "
            "recovery_attempts=%{public}u",
            seethis::platform::InputMonitorStateName(observed_state).data(),
            seethis::platform::InputMonitorStateName(decision.state).data(),
            static_cast<long long>(now_ms),
            seethis::platform::InputInterruptionKindName(interruption).data(),
            recovery_attempts);
        interruption_state_.Clear();
      }
      const auto settings = settings_->Get();
      const bool delete_conflict =
          seethis::core::ShortcutBindingsConflict(settings);
      const bool capture_changed =
          active_capture_key_ != settings.shortcut_key_code ||
          active_capture_modifiers_ !=
              NativeModifiers(settings.shortcut_modifiers) ||
          active_capture_conflict_ != delete_conflict;
      if (capture_changed) {
        Cancel(seethis::core::CancelReason::kShortcutInterrupted);
        RegisterCaptureHotKey();
      }
      if (active_delete_key_ != settings.delete_shortcut_key_code ||
          active_delete_modifiers_ !=
              NativeModifiers(settings.delete_shortcut_modifiers) ||
          active_delete_conflict_ != delete_conflict) {
        deletion_->Interrupt(DeleteKeyIsDown(), CurrentCarbonEventProvenance());
        RegisterDeleteHotKey();
      }
      RefreshDeleteReadiness();
      RefreshCaptureReadiness();
    }
    const auto physical_flags = CGEventSourceFlagsState(
        kCGEventSourceStateCombinedSessionState);
    const auto delete_before = deletion_->Snapshot();
    deletion_->Reconcile(DeleteKeyIsDown(),
                         DeleteModifiersAreExact(physical_flags),
                         CurrentCarbonEventProvenance());
    const auto delete_after = deletion_->Snapshot();
    if (delete_before.state != delete_after.state ||
        delete_before.key_down != delete_after.key_down ||
        delete_before.exact_modifiers != delete_after.exact_modifiers) {
      const char* recovery =
          !delete_after.key_down && delete_before.key_down
              ? "reconcile-release-recovery"
              : "reconcile-event-recovery";
      LogDeleteStage(recovery);
    }
    if (controller_->Snapshot().state ==
        seethis::core::InteractionState::kDrawing) {
      const pid_t frontmost = FrontmostProcessIdentifier();
      if (foreground_process_ != 0 && frontmost != foreground_process_) {
        Cancel(seethis::core::CancelReason::kApplicationSwitched);
        return;
      }
      const bool shortcut_is_down = active_key_code_.has_value() &&
          CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                *active_key_code_);
      const bool shortcut_modifiers_present = active_key_code_.has_value() &&
          (physical_flags & active_capture_modifiers_) ==
              active_capture_modifiers_;
      const auto watchdog_before = controller_->Snapshot();
      const auto watchdog_observed_ms = MonotonicMilliseconds();
      if (controller_->Watchdog(watchdog_observed_ms, shortcut_is_down,
                                settings_->Get().maximum_hold_ms,
                                shortcut_modifiers_present)) {
        const auto snapshot = controller_->Snapshot();
        if (snapshot.state == seethis::core::InteractionState::kFinished) {
          const char* recovery_stage =
              snapshot.completion_reason ==
                      seethis::core::CompletionReason::
                          kWatchdogObservedModifierRelease
                  ? "watchdog-observed-modifier-release"
                  : "watchdog-observed-key-release";
          LogCaptureStage(recovery_stage, "completed", watchdog_observed_ms);
          ResetCaptureReleaseTracking();
          foreground_process_ = 0;
          active_key_code_.reset();
        } else {
          LogCaptureStage("watchdog-cancelled",
                          seethis::core::CancelReasonName(
                              snapshot.cancel_reason),
                          watchdog_observed_ms, -1,
                          watchdog_before.regions.size());
          ResetCaptureReleaseTracking();
          foreground_process_ = 0;
          active_key_code_.reset();
        }
        overlay_->Refresh();
      }
    }
    overlay_->UpdatePointerPolicy(CurrentPointer());
  }

  void ApplicationActivated() {
    if (controller_->Snapshot().state ==
            seethis::core::InteractionState::kDrawing &&
        foreground_process_ != 0 &&
        FrontmostProcessIdentifier() != foreground_process_) {
      Cancel(seethis::core::CancelReason::kApplicationSwitched);
    }
  }

  void DisplayConfigurationChanged() {
    deletion_->Interrupt(DeleteKeyIsDown(), CurrentCarbonEventProvenance());
    Cancel(seethis::core::CancelReason::kDisplayConfigurationChanged);
    last_pointer_.reset();
    overlay_->RebuildDisplays();
    overlay_->Refresh();
    overlay_->UpdatePointerPolicy(CurrentPointer());
  }

  seethis::core::InteractionController* controller_;
  seethis::platform::OverlayAdapter* overlay_;
  seethis::core::SettingsStore* settings_;
  std::shared_ptr<seethis::platform::PermissionReadinessState> readiness_;
  seethis::core::DeleteGestureController* deletion_;
  CFMachPortRef event_tap_ = nullptr;
  CFRunLoopSourceRef run_loop_source_ = nullptr;
  __strong NSTimer* watchdog_timer_ = nil;
  __strong id resign_observer_ = nil;
  __strong id display_observer_ = nil;
  __strong id workspace_observer_ = nil;
  pid_t foreground_process_ = 0;
  std::optional<CGKeyCode> active_key_code_;
  std::optional<seethis::core::DisplayPoint> last_pointer_;
  CGEventFlags active_modifiers_ = 0;
  std::uint64_t observed_retry_generation_ = 0;
  int readiness_tick_ = 0;
  seethis::platform::InputReadinessLatch readiness_latch_;
  seethis::platform::InputInterruptionState interruption_state_;
  bool started_ = false;
  EventHandlerRef hotkey_handler_ = nullptr;
  EventHotKeyRef capture_hotkey_ = nullptr;
  EventHotKeyRef delete_hotkey_ = nullptr;
  CGKeyCode active_capture_key_ = 0;
  CGEventFlags active_capture_modifiers_ = kCGEventFlagMaskAlternate;
  std::uint32_t effective_capture_modifiers_ = 0;
  std::uint32_t effective_delete_modifiers_ = 0;
  std::uint64_t capture_generation_ = 0;
  bool capture_registered_ = false;
  bool active_capture_conflict_ = false;
  seethis::platform::CaptureShortcutEligibility capture_eligibility_;
  CGKeyCode active_delete_key_ = 2;
  CGEventFlags active_delete_modifiers_ = kCGEventFlagMaskAlternate;
  std::uint64_t delete_generation_ = 0;
  bool delete_registered_ = false;
  bool active_delete_conflict_ = false;
  static constexpr std::int64_t kNearSimultaneousReleaseMs = 50;
  bool pending_capture_key_release_ = false;
  bool pending_capture_key_release_after_watchdog_ = false;
  bool pending_capture_modifier_release_ = false;
  std::int64_t capture_release_first_ms_ = 0;
};

@interface STApplicationDelegate : NSObject <NSApplicationDelegate> {
  std::unique_ptr<seethis::core::InteractionController> _controller;
  std::unique_ptr<seethis::core::DeleteGestureController> _deletion;
  std::unique_ptr<seethis::core::SettingsStore> _settings;
  std::unique_ptr<seethis::core::ReferenceStore> _references;
  std::unique_ptr<seethis::service::ReferenceServer> _server;
  std::unique_ptr<seethis::platform::OverlayAdapter> _overlay;
  std::unique_ptr<seethis::platform::InputAdapter> _input;
  std::shared_ptr<seethis::platform::PermissionReadinessState> _readiness;
  __strong NSStatusItem* _statusItem;
  __strong NSMenuItem* _startupStateItem;
  __strong NSMenuItem* _inputStateItem;
  __strong NSMenuItem* _screenStateItem;
  __strong NSMenuItem* _captureStateItem;
  __strong NSMenuItem* _deleteStateItem;
  __strong NSMenuItem* _settingsStateItem;
  __strong NSMenuItem* _inspectorItem;
  __strong NSArray<NSMenuItem*>* _chromeStateItems;
  __strong NSMenuItem* _chromeGuidanceItem;
  __strong NSMenuItem* _guidanceItem;
  __strong NSMenuItem* _screenGuidanceItem;
  __strong NSMenuItem* _identityGuidanceItem;
  __strong NSMenuItem* _settingsGuidanceItem;
  __strong NSTimer* _statusTimer;
  __strong NSPanel* _entryPanel;
  __strong NSTextField* _entryStatus;
  __strong NSTextField* _entryInput;
  __strong NSTextField* _entryScreen;
  __strong NSButton* _entryInspectorButton;
}
@end

@implementation STApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  os_log(OS_LOG_DEFAULT,
      "seethis.entry event=did_finish_launching_enter effective_policy=%{public}ld",
      static_cast<long>(NSApplication.sharedApplication.activationPolicy));
  NSURL* support=[[NSFileManager defaultManager]
      URLsForDirectory:NSApplicationSupportDirectory
             inDomains:NSUserDomainMask].firstObject;
  const auto root=std::filesystem::path(support.path.UTF8String)/"SeeThis";
  _settings=std::make_unique<seethis::core::SettingsStore>(root/"settings-v1.json");
  if(!_settings->usable())
    std::cerr<<"seethis: settings state="
      <<seethis::core::SettingsLoadStateName(_settings->load_state())
      <<" file="<<_settings->path()<<" detail="<<_settings->load_error()<<'\n';
  _references=std::make_unique<seethis::core::ReferenceStore>(root/"references-v1");
  _readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  const char* app_name=NSProcessInfo.processInfo.processName.UTF8String;
  const char* bundle_id=NSBundle.mainBundle.bundleIdentifier.UTF8String;
  const char* bundle_path=NSBundle.mainBundle.bundlePath.UTF8String;
  _readiness->app_name=app_name?app_name:"SeeThis";
  _readiness->bundle_identifier=bundle_id?bundle_id:"";
  _readiness->bundle_path=bundle_path?bundle_path:"";
  NSString* resources=NSBundle.mainBundle.resourcePath;
  const auto web=std::filesystem::path(resources.UTF8String)/"settings";
  _server=std::make_unique<seethis::service::ReferenceServer>(
      *_references,*_settings,web,_readiness);
  std::string server_error;
  if (!_server->Start(&server_error)) {
    std::cerr << "seethis: local reference service unavailable: "
              << server_error << '\n';
  }
  _controller = std::make_unique<seethis::core::InteractionController>();
  _deletion = std::make_unique<seethis::core::DeleteGestureController>();
  _overlay = seethis::platform::MakeOverlayAdapter(
      _controller.get(),_references.get(),_settings.get(),_server.get(),
      _deletion.get());
  _input = seethis::platform::MakeInputAdapter(
      _controller.get(), _overlay.get(), _settings.get(), _readiness,
      _deletion.get());
  if (!_input->Start()) {
    const auto state=_readiness->input_monitor.load();
    std::cerr << "seethis: Input Monitoring "
              << seethis::platform::InputMonitorStateName(state) << ": "
              << seethis::platform::InputMonitorGuidance(state) << " app="
              << _readiness->bundle_identifier << " path="
              << _readiness->bundle_path << '\n';
  }
  _statusItem=[NSStatusBar.systemStatusBar
      statusItemWithLength:32.0];
  NSImage* entryImage=[NSImage imageWithSystemSymbolName:@"viewfinder"
      accessibilityDescription:@"SeeThis"];
  [entryImage setTemplate:YES];
  entryImage.size=NSMakeSize(18.0,18.0);
  _statusItem.button.image=entryImage;
  _statusItem.button.title=entryImage?@"":@"ST";
  [_statusItem.button setAccessibilityLabel:@"SeeThis"];
  os_log(OS_LOG_DEFAULT,
      "seethis.entry event=status_item_created effective_policy=%{public}ld item_exists=%{public}d length=%{public}f visible=%{public}d button_exists=%{public}d",
      static_cast<long>(NSApplication.sharedApplication.activationPolicy),
      _statusItem!=nil,static_cast<double>(_statusItem.length),
      _statusItem.isVisible,_statusItem.button!=nil);
  NSMenu* menu=[NSMenu new];
  _startupStateItem=[[NSMenuItem alloc] initWithTitle:@"SeeThis"
      action:nil keyEquivalent:@""];
  [menu addItem:_startupStateItem];
  NSMenuItem* entryItem=[[NSMenuItem alloc] initWithTitle:@"Show SeeThis…"
      action:@selector(showPermissionEntry:) keyEquivalent:@""];
  entryItem.target=self;[menu addItem:entryItem];
  [menu addItem:NSMenuItem.separatorItem];
  _inputStateItem=[[NSMenuItem alloc] initWithTitle:@"Input Monitoring: checking…"
      action:nil keyEquivalent:@""];
  [menu addItem:_inputStateItem];
  _screenStateItem=[[NSMenuItem alloc] initWithTitle:@"Screen Recording: checking…"
      action:nil keyEquivalent:@""];
  [menu addItem:_screenStateItem];
  _captureStateItem=[[NSMenuItem alloc] initWithTitle:@"Capture shortcut: checking…"
      action:nil keyEquivalent:@""];
  [menu addItem:_captureStateItem];
  _deleteStateItem=[[NSMenuItem alloc] initWithTitle:@"Delete shortcut: checking…"
      action:nil keyEquivalent:@""];
  [menu addItem:_deleteStateItem];
  _settingsStateItem=[[NSMenuItem alloc] initWithTitle:@"Settings: checking…"
      action:nil keyEquivalent:@""];
  [menu addItem:_settingsStateItem];
  _inspectorItem=[[NSMenuItem alloc] initWithTitle:@"Show Inspector…"
      action:@selector(toggleInspector:) keyEquivalent:@"i"];
  _inspectorItem.target=self;[menu addItem:_inspectorItem];
  [menu addItem:NSMenuItem.separatorItem];
  NSMutableArray<NSMenuItem*>* chromeItems=[NSMutableArray array];
  for(NSUInteger index=0;index<5;++index) {
    NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:@"Chrome: checking…"
        action:nil keyEquivalent:@""];
    [menu addItem:item];[chromeItems addObject:item];
  }
  _chromeStateItems=chromeItems;
  for(NSArray* action in @[@[@"Connect Chrome",NSStringFromSelector(@selector(connectChrome:))],
                           @[@"Retry Chrome",NSStringFromSelector(@selector(retryChrome:))],
                           @[@"Open Automation Settings…",NSStringFromSelector(@selector(openChromeAutomationSettings:))]]) {
    NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:action[0]
        action:NSSelectorFromString(action[1]) keyEquivalent:@""];
    item.target=self;[menu addItem:item];
  }
  _chromeGuidanceItem=[[NSMenuItem alloc] initWithTitle:@"Chrome recovery: checking…"
      action:nil keyEquivalent:@""];
  [menu addItem:_chromeGuidanceItem];
  [menu addItem:NSMenuItem.separatorItem];
  _guidanceItem=[[NSMenuItem alloc] initWithTitle:@"Readiness guidance unavailable"
      action:nil keyEquivalent:@""];
  [menu addItem:_guidanceItem];
  _screenGuidanceItem=[[NSMenuItem alloc]
      initWithTitle:@"Screen Recording guidance unavailable"
      action:nil keyEquivalent:@""];
  [menu addItem:_screenGuidanceItem];
  _identityGuidanceItem=[[NSMenuItem alloc]
      initWithTitle:@"App identity guidance unavailable"
      action:nil keyEquivalent:@""];
  [menu addItem:_identityGuidanceItem];
  _settingsGuidanceItem=[[NSMenuItem alloc]
      initWithTitle:@"Settings guidance unavailable"
      action:nil keyEquivalent:@""];
  [menu addItem:_settingsGuidanceItem];
  [menu addItem:NSMenuItem.separatorItem];
  NSMenuItem* retryItem=[[NSMenuItem alloc] initWithTitle:@"Retry Input Monitoring"
      action:@selector(retryInputMonitoring:) keyEquivalent:@"r"];
  retryItem.target=self;[menu addItem:retryItem];
  NSMenuItem* inputSettingsItem=[[NSMenuItem alloc]
      initWithTitle:@"Open Input Monitoring Settings…"
      action:@selector(openInputMonitoringSettings:) keyEquivalent:@""];
  inputSettingsItem.target=self;[menu addItem:inputSettingsItem];
  NSMenuItem* screenSettingsItem=[[NSMenuItem alloc]
      initWithTitle:@"Open Screen Recording Settings…"
      action:@selector(openScreenRecordingSettings:) keyEquivalent:@""];
  screenSettingsItem.target=self;[menu addItem:screenSettingsItem];
  NSString* bundleIdentifier=NSBundle.mainBundle.bundleIdentifier;
  if(bundleIdentifier==nil)bundleIdentifier=@"unknown bundle";
  NSMenuItem* appItem=[[NSMenuItem alloc]
      initWithTitle:[NSString stringWithFormat:@"App: %@",bundleIdentifier]
      action:nil keyEquivalent:@""];
  [menu addItem:appItem];
  NSMenuItem* locationItem=[[NSMenuItem alloc]
      initWithTitle:[NSString stringWithFormat:@"Location: %@",
        NSBundle.mainBundle.bundlePath]
      action:nil keyEquivalent:@""];
  [menu addItem:locationItem];
  NSMenuItem* revealItem=[[NSMenuItem alloc]
      initWithTitle:@"Reveal This App in Finder"
      action:@selector(revealCurrentApp:) keyEquivalent:@""];
  revealItem.target=self;[menu addItem:revealItem];
  [menu addItem:NSMenuItem.separatorItem];
  NSMenuItem* settingsItem=[[NSMenuItem alloc] initWithTitle:@"Open Settings…"
      action:@selector(openSettings:) keyEquivalent:@","];
  settingsItem.target=self;[menu addItem:settingsItem];
  [menu addItem:NSMenuItem.separatorItem];
  NSMenuItem* quitItem=[[NSMenuItem alloc] initWithTitle:@"Quit SeeThis"
      action:@selector(quit:) keyEquivalent:@"q"];
  quitItem.target=self;[menu addItem:quitItem];_statusItem.menu=menu;
  [self refreshPermissionMenu];
  [self presentPermissionEntry:seethis::platform::PermissionEntryTrigger::kLaunch];
  __weak STApplicationDelegate* weakSelf=self;
  // One sample after AppKit has had time to lay out the initial status item.
  // Keep this outside the permission refresh timer to avoid repeated logging.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),
      dispatch_get_main_queue(),^{
        STApplicationDelegate* owner=weakSelf;
        if(owner)[owner recordEntryLayoutDiagnostic];
      });
  _statusTimer=[NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES
      block:^(NSTimer*) {[weakSelf refreshPermissionMenu];}];
}

- (void)recordEntryLayoutDiagnostic {
  NSStatusBarButton* button=_statusItem.button;
  NSWindow* window=button.window;
  NSScreen* screen=window.screen;
  os_log(OS_LOG_DEFAULT,
      "seethis.entry event=initial_layout_delayed effective_policy=%{public}ld length=%{public}f item_visible=%{public}d button_exists=%{public}d window_exists=%{public}d window_visible=%{public}d window_frame=%{public}@ button_frame=%{public}@ screen_frame=%{public}@ title=%{public}@",
      static_cast<long>(NSApplication.sharedApplication.activationPolicy),
      static_cast<double>(_statusItem.length),_statusItem.isVisible,
      button!=nil,window!=nil,window.isVisible,
      window?NSStringFromRect(window.frame):@"unavailable",
      button?NSStringFromRect(button.frame):@"unavailable",
      screen?NSStringFromRect(screen.frame):@"unavailable",
      button?button.title:@"unavailable");
}

- (void)refreshPermissionMenu {
  if(!_readiness)return;
  const auto chrome=seethis::platform::RefreshChromeConnection();
  auto chromeText=[](std::string_view value) {
    return [NSString stringWithUTF8String:std::string(value).c_str()];
  };
  const NSArray<NSString*>* chromeTitles=@[
    [NSString stringWithFormat:@"Chrome: %@",chromeText(seethis::platform::ChromeRunningName(chrome.target))],
    [NSString stringWithFormat:@"Chrome Automation: %@",chromeText(
        seethis::platform::ChromeConsentName(chrome.permission.consent))],
    [NSString stringWithFormat:@"Chrome provider: %@",chromeText(
        seethis::platform::ChromeProviderName(chrome.provider))],
    [NSString stringWithFormat:@"Chrome page identity: %@",chromeText(
        seethis::platform::ChromePageReadinessName(chrome.page))],
    chrome.observation_active?@"Chrome context observation: active":@"Chrome context observation: inactive"];
  for(NSUInteger index=0;index<_chromeStateItems.count;++index)
    _chromeStateItems[index].title=chromeTitles[index];
  _chromeGuidanceItem.title=[@"Chrome recovery: " stringByAppendingString:
      chromeText(seethis::platform::ChromeRecoveryGuidance(chrome))];
  const auto input=_readiness->input_monitor.load();
  const auto screen=_readiness->screen_recording.load();
  const auto startup_status=seethis::platform::PermissionStartupStatus(input, screen);
  _statusItem.button.toolTip=[NSString stringWithUTF8String:
      std::string(startup_status).c_str()];
  _startupStateItem.title=_statusItem.button.toolTip;
  NSString* inputName=[NSString stringWithUTF8String:
      std::string(seethis::platform::InputMonitorStateName(input)).c_str()];
  _inputStateItem.title=[NSString stringWithFormat:@"Input Monitoring: %@%@",
      inputName,seethis::platform::InputMonitorReady(input)?@" (ready)":@" (not ready)"];
  NSString* screenName=[NSString stringWithUTF8String:
      std::string(seethis::platform::ScreenRecordingStateName(screen)).c_str()];
  _screenStateItem.title=[NSString stringWithFormat:@"Screen Recording: %@%@",
      screenName,screen==seethis::platform::ScreenRecordingState::kReady?
          @" (ready)":@" (not ready)"];
  const auto capture=_readiness->capture_shortcut.load();
  NSString* captureName=[NSString stringWithUTF8String:
      std::string(seethis::platform::CaptureShortcutStateName(capture)).c_str()];
  _captureStateItem.title=[NSString stringWithFormat:@"Capture shortcut: %@%@",
      captureName,seethis::platform::CaptureShortcutReady(capture)?
          @" (ready)":@" (not ready)"];
  const auto deletion=_readiness->delete_shortcut.load();
  NSString* deletionName=[NSString stringWithUTF8String:
      std::string(seethis::platform::DeleteShortcutStateName(deletion)).c_str()];
  _deleteStateItem.title=[NSString stringWithFormat:@"Delete shortcut: %@%@",
      deletionName,seethis::platform::DeleteShortcutReady(deletion)?
          @" (ready)":@" (not ready)"];
  const auto settingsState=_settings->load_state();
  NSString* settingsName=[NSString stringWithUTF8String:
      seethis::core::SettingsLoadStateName(settingsState)];
  _settingsStateItem.title=[NSString stringWithFormat:@"Settings: %@%@",
      settingsName,_settings->usable()?@" (ready)":@" (preserved, unusable)"];
  const std::string input_guidance=std::string(
      seethis::platform::InputMonitorGuidance(input));
  _guidanceItem.title=[NSString stringWithFormat:@"Input Monitoring guidance: %@",
      [NSString stringWithUTF8String:input_guidance.c_str()]];
  const std::string screen_guidance=std::string(
      seethis::platform::ScreenRecordingGuidance(screen));
  _screenGuidanceItem.title=[NSString stringWithFormat:@"Screen Recording guidance: %@",
      [NSString stringWithUTF8String:screen_guidance.c_str()]];
  _identityGuidanceItem.title=[NSString stringWithFormat:@"App identity guidance: %@",
      [NSString stringWithUTF8String:
          std::string(seethis::platform::ExactAppRelaunchGuidance()).c_str()]];
  const std::string settings_guidance = _settings->usable()
      ? "Settings are ready."
      : _settings->load_error();
  _settingsGuidanceItem.title=[NSString stringWithFormat:@"Settings guidance: %@",
      [NSString stringWithUTF8String:settings_guidance.c_str()]];
  if(_inspectorItem)
    _inspectorItem.title=_overlay&&_overlay->InspectorVisible()?
      @"Hide Inspector":@"Show Inspector…";
  // Refresh existing details only; polling never presents or reopens a panel.
  _entryStatus.stringValue=_startupStateItem.title;
  _entryInput.stringValue=_inputStateItem.title;
  _entryScreen.stringValue=_screenStateItem.title;
  _entryInspectorButton.title=_inspectorItem.title;
}

- (void)presentPermissionEntry:(seethis::platform::PermissionEntryTrigger)trigger {
  if(!_readiness || !seethis::platform::PermissionEntryShouldPresent(
      trigger,_readiness->input_monitor.load(),_readiness->screen_recording.load()))
    return;
  if(!_entryPanel) {
    _entryPanel=[[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,480,350)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    _entryPanel.title=@"SeeThis";
    _entryPanel.releasedWhenClosed=NO;
    _entryPanel.hidesOnDeactivate=NO;
    _entryPanel.collectionBehavior=NSWindowCollectionBehaviorMoveToActiveSpace;
    NSView* content=_entryPanel.contentView;
    NSTextField* introduction=[NSTextField wrappingLabelWithString:
        @"SeeThis is running. Use the menu bar icon to open actions and permission details."];
    introduction.frame=NSMakeRect(22,289,436,42);
    [content addSubview:introduction];
    _entryStatus=[NSTextField labelWithString:@""];
    _entryStatus.font=[NSFont boldSystemFontOfSize:13];
    _entryStatus.frame=NSMakeRect(22,254,436,24);
    [content addSubview:_entryStatus];
    _entryInput=[NSTextField labelWithString:@""];
    _entryInput.frame=NSMakeRect(22,224,436,24);
    [content addSubview:_entryInput];
    _entryScreen=[NSTextField labelWithString:@""];
    _entryScreen.frame=NSMakeRect(22,196,436,24);
    [content addSubview:_entryScreen];
    NSButton* (^addButton)(NSString*,SEL,NSRect)=
        ^NSButton*(NSString* title,SEL action,NSRect frame) {
          NSButton* button=[NSButton buttonWithTitle:title target:self action:action];
          button.bezelStyle=NSBezelStyleRounded;button.frame=frame;
          [content addSubview:button];return button;
        };
    addButton(@"Open Input Monitoring Settings…",@selector(openInputMonitoringSettings:),
        NSMakeRect(22,153,436,30));
    addButton(@"Open Screen Recording Settings…",@selector(openScreenRecordingSettings:),
        NSMakeRect(22,115,436,30));
    _entryInspectorButton=addButton(@"Show Inspector…",@selector(toggleInspector:),
        NSMakeRect(22,77,210,30));
    addButton(@"Retry Input Monitoring",@selector(retryInputMonitoring:),
        NSMakeRect(248,77,210,30));
    addButton(@"Open Settings…",@selector(openSettings:),NSMakeRect(22,29,210,30));
    addButton(@"Quit SeeThis",@selector(quit:),NSMakeRect(248,29,210,30));
    [_entryPanel center];
  }
  [self refreshPermissionMenu];
  // A saved window position may belong to a disconnected display.
  BOOL onScreen=NO;
  for(NSScreen* screen in NSScreen.screens)
    if(NSIntersectsRect(_entryPanel.frame,screen.visibleFrame))onScreen=YES;
  if(!onScreen)[_entryPanel center];
  [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
  [_entryPanel makeKeyAndOrderFront:nil];
}

- (void)showPermissionEntry:(id)sender {
  (void)sender;
  [self presentPermissionEntry:seethis::platform::PermissionEntryTrigger::kExplicitOpen];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)application
                   hasVisibleWindows:(BOOL)hasVisibleWindows {
  (void)application;(void)hasVisibleWindows;
  [self presentPermissionEntry:seethis::platform::PermissionEntryTrigger::kExplicitOpen];
  return NO;
}

- (void)toggleInspector:(id)sender {
  (void)sender;
  if(_overlay)_overlay->ToggleInspector();
  [self refreshPermissionMenu];
}

- (void)retryInputMonitoring:(id)sender {
  (void)sender;if(_input)_input->RetryInputMonitoring();[self refreshPermissionMenu];
}

- (void)connectChrome:(id)sender {
  (void)sender;seethis::platform::ConnectChromeExplicit();[self refreshPermissionMenu];
}
- (void)retryChrome:(id)sender {
  (void)sender;seethis::platform::RetryChromeExplicit();[self refreshPermissionMenu];
}
- (void)openChromeAutomationSettings:(id)sender {
  (void)sender;
  if(!seethis::platform::OpenChromeAutomationSettingsExplicit())
    _chromeGuidanceItem.title=@"Chrome recovery: Could not open Settings. Open System Settings > Privacy & Security > Automation > SeeThis > Google Chrome, then choose Retry Chrome.";
}

- (void)openInputMonitoringSettings:(id)sender {
  (void)sender;[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
      @"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent"]];
}

- (void)openScreenRecordingSettings:(id)sender {
  (void)sender;[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
      @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}

- (void)revealCurrentApp:(id)sender {
  (void)sender;[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:
      @[NSBundle.mainBundle.bundleURL]];
}

- (void)openSettings:(id)sender {
  (void)sender;if(!_server || _server->port()==0)return;
  const auto url=_server->settings_url();
  [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
      [NSString stringWithUTF8String:url.c_str()]]];
}

- (void)quit:(id)sender {(void)sender;[NSApplication.sharedApplication terminate:nil];}

- (void)applicationWillTerminate:(NSNotification*)notification {
  (void)notification;
  [_statusTimer invalidate];_statusTimer=nil;
  [_entryPanel close];_entryPanel=nil;
  _entryStatus=nil;_entryInput=nil;_entryScreen=nil;_entryInspectorButton=nil;
  if (_input) {
    _input->Stop();
  }
  _input.reset();
  _overlay.reset();
  _deletion.reset();
  _controller.reset();
  if (_server) _server->Stop();
  _server.reset();
  _references.reset();
  _settings.reset();
  _readiness.reset();
  if(_statusItem)[NSStatusBar.systemStatusBar removeStatusItem:_statusItem];
  _statusItem=nil;_inputStateItem=nil;_screenStateItem=nil;
  _startupStateItem=nil;
  _deleteStateItem=nil;_settingsStateItem=nil;_guidanceItem=nil;
  _inspectorItem=nil;
  _chromeStateItems=nil;_chromeGuidanceItem=nil;
  _screenGuidanceItem=nil;_identityGuidanceItem=nil;_settingsGuidanceItem=nil;
}
@end

namespace seethis::platform {

EffectiveShortcutSnapshot InputShortcutStatus() {
  return effective_shortcuts.Snapshot();
}

std::unique_ptr<InputAdapter> MakeInputAdapter(
    core::InteractionController* controller, OverlayAdapter* overlay,
    core::SettingsStore* settings,
    std::shared_ptr<PermissionReadinessState> readiness,
    core::DeleteGestureController* deletion) {
  return std::make_unique<MacInputAdapter>(controller, overlay, settings,
                                           std::move(readiness), deletion);
}

int RunApplication() {
  @autoreleasepool {
    NSApplication* application = NSApplication.sharedApplication;
    const BOOL policy_set=[application
        setActivationPolicy:NSApplicationActivationPolicyAccessory];
    os_log(OS_LOG_DEFAULT,
        "seethis.entry event=activation_policy_request requested_policy=%{public}ld result=%{public}d effective_policy=%{public}ld",
        static_cast<long>(NSApplicationActivationPolicyAccessory),policy_set,
        static_cast<long>(application.activationPolicy));
    STApplicationDelegate* delegate = [[STApplicationDelegate alloc] init];
    application.delegate = delegate;
    [application run];
    application.delegate = nil;
  }
  return 0;
}

}  // namespace seethis::platform
