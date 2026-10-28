#include <CoreGraphics/CGEventTypes.h>

#include "core/interaction.h"

// The event tap and overlay can deliver the same drag in either order. Both
// routes must carry time so the first meaningful sample renews inactivity.
static bool HandleCapturePointerMotion(
    seethis::core::InteractionController* controller, CGEventType type,
    const std::optional<seethis::core::DisplayPoint>& pointer,
    std::int64_t observed_ms) {
  return type == kCGEventLeftMouseDragged && pointer.has_value() &&
      controller->PointerMoved(*pointer, observed_ms);
}

#if !defined(SEETHIS_CAPTURE_POINTER_TESTING)
#import "privacy_action_mac.h"
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
        deletion_(deletion) {
    prefix_ingress_.SetActions({
      [this]{return overlay_->BeginCapturePrefix();},
      [this]{overlay_->ConfirmCapturePrefix();},
      [this]{overlay_->CancelCapturePrefix();},
      [this]{overlay_->ObserveViewportInput(seethis::platform::ViewportInput::kInterrupted);},
      [this]{SchedulePrefixDeadline();}});
    seethis::platform::SetChromeConsentInterruptionHandler(this,[this] {
      Cancel(seethis::core::CancelReason::kShortcutInterrupted);
      ResetCaptureReleaseTracking();foreground_process_=0;active_key_code_.reset();
      overlay_->UpdatePointerPolicy({});
    });
  }

  ~MacInputAdapter() override { seethis::platform::ClearChromeConsentInterruptionHandler(this);Stop(); }

#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  struct TestPhysicalInput {
    std::function<bool(CGKeyCode)> key_down;
    std::function<CGEventFlags()> flags;
    std::function<pid_t()> foreground_pid;
    std::function<double()> quartz_to_appkit_top;
  };
  // External registration/physical snapshots only; callback decisions stay real.
  void ConfigureCaptureForTesting(
      TestPhysicalInput input, seethis::core::DisplayPoint pointer) {
    test_input_ = std::move(input);
    capture_registered_ = true;
    ++capture_generation_;
    const auto settings = settings_->Get();
    active_capture_key_ = settings.shortcut_key_code;
    active_capture_modifiers_ = NativeModifiers(settings.shortcut_modifiers);
    effective_capture_modifiers_ = settings.shortcut_modifiers;
    last_pointer_ = pointer;
    RecordCaptureEligibilityBoundary(CurrentCarbonEventProvenance());
  }
  OSStatus HotKeyForTesting(EventRef event) {
    return HotKeyCallback(nullptr, event, this);
  }
  void RawEventForTesting(CGEventType type, CGEventRef event) {
    HandleEvent(type, event);
  }
  void WatchdogForTesting() {
    // Skip unrelated native permission/tap maintenance in this isolated fixture.
    readiness_tick_ = 0;
    RunWatchdog();
  }
  void FocusChangedForTesting() { ApplicationActivated(); }
  bool CaptureHeldForTesting() const {return capture_hold_.key().has_value();}
  seethis::core::DeleteEventProvenance CaptureBoundaryForTesting() const {return capture_boundary_;}
  void CaptureRegisteredForTesting(bool registered) {capture_registered_=registered;}
  std::uint64_t CaptureGenerationForTesting() const {
    return capture_generation_;
  }
#endif

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
    RetirePrefixReadiness();
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
           CGEventMaskBit(kCGEventOtherMouseDragged) |
           CGEventMaskBit(kCGEventScrollWheel) |
           CGEventMaskBit(kCGEventLeftMouseDown) |
           CGEventMaskBit(kCGEventRightMouseDown) |
           CGEventMaskBit(kCGEventOtherMouseDown);
  }

  void DestroyTap() {
    RetirePrefixReadiness();
    overlay_->ObserveViewportInput(seethis::platform::ViewportInput::kInterrupted);
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
    RetirePrefixReadiness();
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
    RetirePrefixReadiness();
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
    RecordCaptureEligibilityBoundary(CurrentCarbonEventProvenance());
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
      (void)self->RefreshPrefixReadiness();
      if (!self->capture_registered_ || !self->settings_->usable() ||
          !seethis::platform::InputMonitorReady(
              self->readiness_->input_monitor.load())) {
        return eventNotHandledErr;
      }
      const auto carbon_provenance =
          CarbonEventProvenance(GetEventTime(event));
      const CGEventFlags current_flags = self->PhysicalModifiers();
      const bool exact_chord = self->CaptureKeyIsDown() &&
          self->CaptureModifiersAreExact(current_flags);
      const auto pointer = self->CurrentPointer();
      const auto observed_ms = MonotonicMilliseconds();
      self->ReconcileCaptureHold();
      if (GetEventKind(event) == kEventHotKeyPressed) {
        if (self->capture_hold_.key()) {
          self->LogCaptureRearm("carbon_press","rejected","held_repeat");
          self->LogCaptureStage("capture-hotkey-down", "held-repeat", observed_ms);
          return noErr;
        }
        if (exact_chord && self->CapturePressEligible(carbon_provenance)) {
          if(self->capture_hold_.Begin(self->active_capture_key_))
            self->capture_hold_evidence_=CaptureHoldEvidence{++self->next_capture_hold_id_,
                self->active_capture_key_,carbon_provenance};
          // Capture is a basic screen operation. Optional Chrome Automation is
          // requested only by an explicit Connect action, never by this hold.

        }
        if(exact_chord&&self->CapturePressEligible(carbon_provenance)&&pointer) {
          self->prefix_ingress_.Confirm(self->PrefixReadinessSnapshot(),observed_ms);
        } else self->InvalidateCapturePrefix();
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
          self->foreground_process_ = self->CurrentForegroundProcess();
          self->active_key_code_ = self->active_capture_key_;
          self->active_modifiers_ = self->active_capture_modifiers_;
          self->capture_release_continuity_.Begin(self->active_capture_modifiers_);
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

  bool PhysicalKeyIsDown(CGKeyCode key) const {
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
    if (test_input_) return test_input_->key_down(key);
#endif
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, key);
  }
  CGEventFlags PhysicalModifiers() const {
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
    if (test_input_) return test_input_->flags();
#endif
    return CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
  }
  pid_t CurrentForegroundProcess() const {
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
    if (test_input_) return test_input_->foreground_pid();
#endif
    return FrontmostProcessIdentifier();
  }
  seethis::platform::CapturePrefixReadiness PrefixReadinessSnapshot() const {
    const auto settings=settings_->Get();
    seethis::platform::CapturePrefixReadiness state;
    state.running=started_;state.settings_usable=settings_->usable();
    state.registered=capture_registered_&&capture_hotkey_!=nullptr;
    state.handler_ready=hotkey_handler_!=nullptr;
    state.monitor_ready=seethis::platform::InputMonitorReady(readiness_->input_monitor.load())&&
        event_tap_!=nullptr&&run_loop_source_!=nullptr&&CGEventTapIsEnabled(event_tap_);
    state.screen_ready=readiness_->screen_recording.load()==seethis::platform::ScreenRecordingState::kReady;
    state.shortcut_ready=seethis::platform::CaptureShortcutReady(readiness_->capture_shortcut.load());
    state.conflict=active_capture_conflict_||seethis::core::ShortcutBindingsConflict(settings);
    state.secure_input=IsSecureEventInputEnabled();
    state.configured_key=settings.shortcut_key_code;state.registered_key=active_capture_key_;
    state.configured_modifiers=settings.shortcut_modifiers;state.registered_modifiers=effective_capture_modifiers_;
    state.native_binding_matches=active_capture_modifiers_==NativeModifiers(settings.shortcut_modifiers);
    state.registration_generation=capture_generation_;
    return state;
  }
  bool RefreshPrefixReadiness() {return prefix_ingress_.Readiness(PrefixReadinessSnapshot());}
  void RetirePrefixReadiness() {prefix_ingress_.LoseReadiness();}
  void InvalidateCapturePrefix() {prefix_ingress_.Invalidate();}
  void SchedulePrefixDeadline() {
    const auto alive=std::weak_ptr<int>(prefix_lifetime_);
    MacInputAdapter* owner=this;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
        seethis::platform::CapturePrefixContinuity::kDeadlineMs*NSEC_PER_MSEC),
        dispatch_get_main_queue(),^{
      if(!alive.expired())owner->prefix_ingress_.Tick(MonotonicMilliseconds());
    });
  }
  void ReconcileCaptureHold() {
    const auto key = capture_hold_.key();
    if (key && capture_hold_.ObserveRelease(PhysicalKeyIsDown(*key))) {
      capture_hold_evidence_.reset();
      RecordCaptureEligibilityBoundary(CurrentCarbonEventProvenance());
      LogCaptureRearm("sample_false","rearmed","sample_release");
    }
  }
  void ObserveRawCaptureRelease(CGKeyCode key,seethis::core::DeleteEventProvenance release) {
    const auto held=capture_hold_evidence_;
    if(!held||!capture_hold_.key()) {
      if(key==active_capture_key_)LogCaptureRearm("raw_key_up","rejected","no_hold");
      return;
    }
    // This is the ORIGINAL key/press, even after rebinding. A raw CGEvent has no
    // registration generation and must never be labelled with the current one.
    if(key!=held->key) {
      if(key==active_capture_key_)LogCaptureRearm("raw_key_up","rejected","key_mismatch");
      return;
    }
    const auto observed=CurrentCarbonEventProvenance();
    if(!release.valid()||!held->press.valid()||!observed.valid()||
       release.latest_ns>observed.latest_ns) {
      LogCaptureRearm("raw_key_up","rejected","invalid_provenance");return;
    }
    if(release.earliest_ns<=held->press.latest_ns) {
      LogCaptureRearm("raw_key_up","rejected","not_after_press");return;
    }
    if(!capture_hold_evidence_||capture_hold_evidence_->id!=held->id||
       capture_hold_.key()!=std::optional<CGKeyCode>(held->key)) {
      LogCaptureRearm("raw_key_up","rejected","hold_changed");return;
    }
    if(!capture_hold_.ObserveRelease(false))return;
    capture_hold_evidence_.reset();
    const bool advanced=RecordCaptureEligibilityBoundary(release);
    LogCaptureRearm("raw_key_up","rearmed",advanced?"matching_release":"boundary_not_advanced");
  }
  bool DeleteKeyIsDown() const { return PhysicalKeyIsDown(active_delete_key_); }

  bool DeleteModifiersAreExact(CGEventFlags flags) const {
    return ShortcutModifierFlags(flags) == active_delete_modifiers_;
  }

  bool CaptureKeyIsDown() const {
    return PhysicalKeyIsDown(active_capture_key_);
  }

  bool CaptureModifiersAreExact(CGEventFlags flags) const {
    return ShortcutModifierFlags(flags) == active_capture_modifiers_;
  }

  bool CapturePressEligible(
      seethis::core::DeleteEventProvenance provenance) const {
    return capture_eligibility_.AcceptPress(
        CaptureKeyIsDown(),
        CaptureModifiersAreExact(PhysicalModifiers()), provenance);
  }

  bool RecordCaptureEligibilityBoundary(
      seethis::core::DeleteEventProvenance boundary) {
    if(!boundary.valid()||(capture_boundary_.valid()&&
        boundary.latest_ns<=capture_boundary_.latest_ns))return false;
    capture_boundary_=boundary;
    capture_eligibility_.ObserveBoundary(boundary);return true;
  }
  void LogCaptureRearm(const char* source,const char* outcome,const char* reason) const {
    const auto snapshot=controller_->Snapshot();
    os_log_with_type(OS_LOG_DEFAULT,OS_LOG_TYPE_INFO,
        "seethis capture stage=hold-rearm source=%{public}s outcome=%{public}s reason=%{public}s "
        "state=%{public}s completion=%{public}s cancel=%{public}s",
        source,outcome,reason,seethis::core::InteractionStateName(snapshot.state),
        seethis::core::CompletionReasonName(snapshot.completion_reason),
        seethis::core::CancelReasonName(snapshot.cancel_reason));
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
    (void)RefreshPrefixReadiness();
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
    (void)RefreshPrefixReadiness();
    if(readiness_->screen_recording.load()!=seethis::platform::ScreenRecordingState::kReady)
      overlay_->ObserveViewportInput(seethis::platform::ViewportInput::kInterrupted);
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
    overlay_->ObserveViewportInput(seethis::platform::InputMonitorReady(state)?
        seethis::platform::ViewportInput::kHealthy:seethis::platform::ViewportInput::kInterrupted);
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
    if(event==nullptr){last_pointer_.reset();return {};}
    double primary_top=0;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
    if(test_input_&&test_input_->quartz_to_appkit_top)
      primary_top=test_input_->quartz_to_appkit_top();
    else
#endif
    {
      NSScreen* primary=NSScreen.screens.firstObject;
      if(!primary){last_pointer_.reset();return {};}
      primary_top=NSMaxY(primary.frame);
    }
    const CGPoint location=CGEventGetLocation(event);
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
    // Latch the delivered event location before readiness or presentation callbacks.
    // A later mouseLocation sample is not evidence of this pointer-down target.
    const bool pointer_event=type==kCGEventMouseMoved||type==kCGEventLeftMouseDown||
        type==kCGEventRightMouseDown||type==kCGEventOtherMouseDown||
        type==kCGEventLeftMouseDragged||type==kCGEventRightMouseDragged||
        type==kCGEventOtherMouseDragged||type==kCGEventScrollWheel;
    const auto viewport_pointer=pointer_event?PointerFromEvent(event):std::nullopt;
    bool fresh_primary=false;
    if(type==kCGEventLeftMouseDown) {
      const auto delivered=RawEventProvenance(event),now=CurrentCarbonEventProvenance();
      fresh_primary=delivered.valid()&&now.valid()&&delivered.latest_ns<=now.latest_ns&&
          now.latest_ns-delivered.earliest_ns<=2'000'000'000ull;
    }
    // Preserve the delivered release edge before any synchronous presentation work.
    if(type==kCGEventKeyUp&&event)ObserveRawCaptureRelease(
        static_cast<CGKeyCode>(CGEventGetIntegerValueField(event,kCGKeyboardEventKeycode)),
        RawEventProvenance(event));
    using seethis::platform::ViewportInput;
    const bool prefix_ready=RefreshPrefixReadiness();
    if(type==kCGEventMouseMoved||type==kCGEventLeftMouseDown||type==kCGEventRightMouseDown||
       type==kCGEventOtherMouseDown||type==kCGEventLeftMouseDragged||type==kCGEventRightMouseDragged||
       type==kCGEventOtherMouseDragged||type==kCGEventScrollWheel||
       type==kCGEventTapDisabledByTimeout||type==kCGEventTapDisabledByUserInput)InvalidateCapturePrefix();
    if(type==kCGEventTapDisabledByTimeout||type==kCGEventTapDisabledByUserInput)
      overlay_->ObserveViewportInput(ViewportInput::kInterrupted);
    else if(type==kCGEventScrollWheel)
      overlay_->ObserveViewportInput(ViewportInput::kScroll,viewport_pointer);
    else if(type==kCGEventLeftMouseDown||type==kCGEventRightMouseDown||type==kCGEventOtherMouseDown||
            type==kCGEventLeftMouseDragged||type==kCGEventRightMouseDragged||type==kCGEventOtherMouseDragged) {
      if(controller_->Snapshot().state!=seethis::core::InteractionState::kDrawing)
        overlay_->ObserveViewportInput(fresh_primary?ViewportInput::kPrimaryDown:ViewportInput::kPointer,viewport_pointer);
    } else if(type==kCGEventKeyDown||type==kCGEventKeyUp||type==kCGEventFlagsChanged) {
      const auto key=static_cast<CGKeyCode>(CGEventGetIntegerValueField(event,kCGKeyboardEventKeycode));
      const auto settings=settings_->Get();
      const auto modifiers=ShortcutModifierFlags(CGEventGetFlags(event));
      const bool release_tail=capture_release_continuity_.Observe(key==active_capture_key_,
          type==kCGEventKeyUp,type==kCGEventFlagsChanged,modifiers);
      const bool capture_key=prefix_ready&&type==kCGEventKeyDown && key==settings.shortcut_key_code &&
          modifiers==NativeModifiers(settings.shortcut_modifiers);
      bool prefix_modifier=false;
      if(!release_tail&&type==kCGEventFlagsChanged)
        prefix_modifier=prefix_ingress_.ModifierEvent(PrefixReadinessSnapshot(),modifiers,
            active_capture_modifiers_,MonotonicMilliseconds());
      if(type==kCGEventFlagsChanged)prefix_ingress_.Released(modifiers);
      if(!capture_key&&!release_tail&&!prefix_modifier) {
        InvalidateCapturePrefix();overlay_->ObserveViewportInput(ViewportInput::kKeyboard);
      }
    }
    ReconcileCaptureHold();
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
      const auto event_pointer = viewport_pointer;
      if (HandleCapturePointerMotion(controller_, type, event_pointer,
                                     MonotonicMilliseconds())) {
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
              PhysicalKeyIsDown(*active_key_code_);
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
    InvalidateCapturePrefix();
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
    (void)RefreshPrefixReadiness();
    ReconcileCaptureHold();
    prefix_ingress_.Tick(MonotonicMilliseconds());
    if(prefix_ingress_.pending()&&
       !prefix_ingress_.PhysicalModifiersValid(ShortcutModifierFlags(PhysicalModifiers())))
      InvalidateCapturePrefix();
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
      (void)RefreshPrefixReadiness();
      overlay_->ObserveViewportInput(seethis::platform::InputMonitorReady(decision.state)?
          seethis::platform::ViewportInput::kHealthy:seethis::platform::ViewportInput::kInterrupted);
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
    const auto physical_flags = PhysicalModifiers();
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
      const pid_t frontmost = CurrentForegroundProcess();
      if (foreground_process_ != 0 && frontmost != foreground_process_) {
        Cancel(seethis::core::CancelReason::kApplicationSwitched);
        return;
      }
      const bool shortcut_is_down = active_key_code_.has_value() &&
          PhysicalKeyIsDown(*active_key_code_);
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
    InvalidateCapturePrefix();
    if (controller_->Snapshot().state ==
            seethis::core::InteractionState::kDrawing &&
        foreground_process_ != 0 &&
        CurrentForegroundProcess() != foreground_process_) {
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
  seethis::platform::CaptureReleaseContinuity capture_release_continuity_;
  seethis::platform::CapturePrefixIngress prefix_ingress_;
  std::shared_ptr<int> prefix_lifetime_=std::make_shared<int>(0);
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
  seethis::platform::CaptureShortcutHold capture_hold_;
  struct CaptureHoldEvidence {
    std::uint64_t id;CGKeyCode key;seethis::core::DeleteEventProvenance press;
  };
  std::optional<CaptureHoldEvidence> capture_hold_evidence_;
  std::uint64_t next_capture_hold_id_=0;
  seethis::core::DeleteEventProvenance capture_boundary_;
#if defined(SEETHIS_CHROME_CONSENT_TESTING)
  std::optional<TestPhysicalInput> test_input_;
#endif
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

@interface STApplicationDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate> {
  std::unique_ptr<seethis::core::InteractionController> _controller;
  std::unique_ptr<seethis::core::DeleteGestureController> _deletion;
  std::unique_ptr<seethis::core::SettingsStore> _settings;
  std::unique_ptr<seethis::core::ReferenceStore> _references;
  std::unique_ptr<seethis::service::ReferenceServer> _server;
  std::unique_ptr<seethis::platform::OverlayAdapter> _overlay;
  std::unique_ptr<seethis::platform::InputAdapter> _input;
  std::shared_ptr<seethis::platform::PermissionReadinessState> _readiness;
  __strong NSStatusItem* _statusItem;
  __strong STPrivacyPolicyAction* _privacyPolicyAction;
  __strong NSMenuItem* _startupStateItem;
  __strong NSMenuItem* _chromeStateItem;
  __strong NSTimer* _statusTimer;
  __strong NSPanel* _entryPanel;
  __strong NSTextField* _entryStatus;
  __strong NSTextView* _entryDetails;
  __strong NSButton* _entryInspectorButton;
  __strong NSString* _chromeStatus;
  __strong NSString* _chromeActionError;
  seethis::platform::ChromeCompactStatus _chromeCompactStatus;
  std::int64_t _chromeLastCheckMs;
}
@end

@implementation STApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  seethis::platform::StartChromeConnection();
  os_log(OS_LOG_DEFAULT,
      "seethis.entry event=did_finish_launching_enter effective_policy=%{public}ld",
      static_cast<long>(NSApplication.sharedApplication.activationPolicy));
  NSURL* support=[[NSFileManager defaultManager]
      URLsForDirectory:NSApplicationSupportDirectory
             inDomains:NSUserDomainMask].firstObject;
  const auto root=std::filesystem::path(support.path.UTF8String)/"SeeThis";
  _settings=std::make_unique<seethis::core::SettingsStore>(root/"settings-v1.json");
  const auto visibility=_settings->Visibility();
  if(!visibility.permits_presentation() && !visibility.error.empty())
    std::cerr<<"seethis: global visibility="<<visibility.name()
      <<" detail="<<visibility.error<<'\n';
  if(!_settings->usable())
    std::cerr<<"seethis: settings state="
      <<seethis::core::SettingsLoadStateName(_settings->load_state())
      <<" file="<<_settings->path()<<" detail="<<_settings->load_error()<<'\n';
  _references=std::make_unique<seethis::core::ReferenceStore>(root/"references-v1");
  _readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
  _chromeCompactStatus=seethis::platform::ChromeCompactStatus::kUnchecked;
  _chromeLastCheckMs=0;
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
  menu.delegate=self;
  _startupStateItem=[[NSMenuItem alloc] initWithTitle:@"SeeThis"
      action:@selector(showPermissionEntry:) keyEquivalent:@""];
  _startupStateItem.target=self;
  [menu addItem:_startupStateItem];
  _chromeStateItem=[[NSMenuItem alloc]
      initWithTitle:@"Chrome — Not ready"
      action:@selector(showPermissionEntry:) keyEquivalent:@""];
  _chromeStateItem.target=self;
  [menu addItem:_chromeStateItem];
  NSMenuItem* inspectorItem=[[NSMenuItem alloc] initWithTitle:@"Show Inspector"
      action:@selector(showInspector:) keyEquivalent:@""];
  inspectorItem.target=self;
  [menu addItem:inspectorItem];
  _privacyPolicyAction=[[STPrivacyPolicyAction alloc] initWithOpener:^BOOL(NSURL* url) {
    return [NSWorkspace.sharedWorkspace openURL:url];
  }];
  [menu addItem:[_privacyPolicyAction menuItem]];
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

- (void)menuWillOpen:(NSMenu*)menu {
  (void)menu;
  _chromeLastCheckMs=0;
  [self refreshPermissionMenu];
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
  // These preflights do not ask for permission or touch the operational tap.
  _readiness->input_monitor_permission.store(CGPreflightListenEventAccess()
      ? seethis::platform::InputMonitoringPermissionState::kGranted
      : seethis::platform::InputMonitoringPermissionState::kDenied);
  _readiness->screen_recording.store(seethis::platform::ScreenRecordingReady()
      ? seethis::platform::ScreenRecordingState::kReady
      : seethis::platform::ScreenRecordingState::kDenied);
  const auto inputPermission=_readiness->input_monitor_permission.load();
  const auto input=_readiness->input_monitor.load();
  const auto screen=_readiness->screen_recording.load();
  const auto startup_status=seethis::platform::PermissionStartupStatus(
      inputPermission,screen);
  _statusItem.button.toolTip=[NSString stringWithUTF8String:
      std::string(startup_status).c_str()];
  _startupStateItem.title=_statusItem.button.toolTip;
  _statusItem.button.accessibilityLabel=_startupStateItem.title;
  // Chrome is optional. Keep its compact status current without asking for
  // Automation consent or trying to acquire a page. Consent cannot establish
  // the page/provider identity used by the actual capture flow.
  const auto now=MonotonicMilliseconds();
  if(_chromeLastCheckMs==0 || now-_chromeLastCheckMs>=5000) {
    _chromeLastCheckMs=now;
    const auto connection=seethis::platform::RefreshChromeConnection();
    if(connection.target.pid<=0) {
      _chromeCompactStatus=seethis::platform::ChromeCompactStatus::kNotRunning;
      _chromeStatus=@"Google Chrome is not running. Optional.";
    } else if(connection.target.valid() && connection.permission.consent==
        seethis::platform::ChromeConsent::kGranted) {
      _chromeCompactStatus=seethis::platform::ChromeCompactStatus::kAutomationAllowedPageUnchecked;
      _chromeStatus=@"Automation is allowed for running Google Chrome. The current page and provider identity have not been verified. Optional.";
      // This is a display of an existing observation, never a page check.
      if(connection.observation_active &&
          connection.provider==seethis::platform::ChromeProviderState::kReady &&
          connection.page==seethis::core::PageAvailability::kReady) {
        _chromeCompactStatus=seethis::platform::ChromeCompactStatus::kReady;
        _chromeStatus=@"Running Google Chrome has a verified current page and provider observation. Optional.";
      }
    } else {
      _chromeCompactStatus=seethis::platform::ChromeCompactStatus::kAutomationUnavailable;
      _chromeStatus=@"Automation is unavailable or not granted. Check SeeThis → Google Chrome in System Settings > Privacy & Security > Automation. Optional.";
    }
  }
  _chromeStateItem.title=[NSString stringWithUTF8String:std::string(
      seethis::platform::ChromeCompactStatusLabel(_chromeCompactStatus)).c_str()];
  if(!_entryDetails)return;

  // The optional connection exposes richer state only in Show details. Reading
  // it never requests Automation consent or captures a Chrome page.
  const auto chromeConnection=seethis::platform::RefreshChromeConnection();
  auto chromeText=[](std::string_view value) {
    return [NSString stringWithUTF8String:std::string(value).c_str()];
  };
  NSString* chromeDetails=[NSString stringWithFormat:
      @"%@\nChrome: %@\nAutomation: %@\nProvider: %@\nPage identity: %@\nContext observation: %@\nRecovery: %@%@",
      _chromeStatus?:@"Not checked. Optional.",
      chromeText(seethis::platform::ChromeRunningName(chromeConnection.target)),
      chromeText(chromeConnection.consent_pending?"request in progress":
          seethis::platform::ChromeConsentName(chromeConnection.permission.consent)),
      chromeText(seethis::platform::ChromeProviderName(chromeConnection.provider)),
      chromeText(seethis::platform::ChromePageReadinessName(chromeConnection.page)),
      chromeConnection.observation_active?@"active":@"inactive",
      chromeText(seethis::platform::ChromeRecoveryGuidance(chromeConnection)),
      _chromeActionError?:@""];

  NSString* permissionName=[NSString stringWithUTF8String:
      std::string(seethis::platform::InputMonitoringPermissionStateName(
          inputPermission)).c_str()];
  NSString* permissionGuidance=inputPermission==
      seethis::platform::InputMonitoringPermissionState::kGranted
      ? @"System permission granted."
      : @"Enable Input Monitoring for this exact app in System Settings.";
  NSString* inputName=[NSString stringWithUTF8String:
      std::string(seethis::platform::InputMonitorStateName(input)).c_str()];
  NSString* screenName=[NSString stringWithUTF8String:
      std::string(seethis::platform::ScreenRecordingStateName(screen)).c_str()];
  const auto capture=_readiness->capture_shortcut.load();
  NSString* captureName=[NSString stringWithUTF8String:
      std::string(seethis::platform::CaptureShortcutStateName(capture)).c_str()];
  const auto deletion=_readiness->delete_shortcut.load();
  NSString* deletionName=[NSString stringWithUTF8String:
      std::string(seethis::platform::DeleteShortcutStateName(deletion)).c_str()];
  const auto settingsState=_settings->load_state();
  NSString* settingsName=[NSString stringWithUTF8String:
      seethis::core::SettingsLoadStateName(settingsState)];
  NSString* inputGuidance=[NSString stringWithUTF8String:
      std::string(seethis::platform::InputMonitorGuidance(input)).c_str()];
  NSString* screenGuidance=[NSString stringWithUTF8String:
      std::string(seethis::platform::ScreenRecordingGuidance(screen)).c_str()];
  NSString* captureGuidance=[NSString stringWithUTF8String:
      std::string(seethis::platform::CaptureShortcutGuidance(capture)).c_str()];
  NSString* deleteGuidance=[NSString stringWithUTF8String:
      std::string(seethis::platform::DeleteShortcutGuidance(deletion)).c_str()];
  NSString* settingsGuidance=_settings->usable()
      ? @"Settings are ready."
      : [NSString stringWithUTF8String:_settings->load_error().c_str()];
  NSString* identityGuidance=[NSString stringWithUTF8String:
      std::string(seethis::platform::ExactAppRelaunchGuidance()).c_str()];
  NSString* bundleIdentifier=NSBundle.mainBundle.bundleIdentifier?:@"unknown bundle";
  NSString* bundlePath=NSBundle.mainBundle.bundlePath?:@"unknown location";
  NSMutableString* details=[NSMutableString stringWithFormat:
      @"BASE PERMISSIONS\nInput Monitoring: %@ — %@\nScreen Recording: %@ — %@\n\n"
       "KEYBOARD AND SHORTCUTS\nEvent tap: %@ — %@\n"
       "Capture shortcut: %@ — %@\nDelete shortcut: %@ — %@\n\n"
       "SETTINGS\nState: %@ — %@\nFile: %s\n\n"
       "CHROME (OPTIONAL)\n%@\n\n"
       "THIS APP\nBundle: %@\nLocation: %@\n%@",
      permissionName,permissionGuidance,screenName,screenGuidance,
      inputName,inputGuidance,captureName,captureGuidance,
      deletionName,deleteGuidance,settingsName,settingsGuidance,
      _settings->path().c_str(),chromeDetails,
      bundleIdentifier,bundlePath,identityGuidance];
  // Refresh existing details only; polling never presents or reopens a panel.
  _entryStatus.stringValue=_startupStateItem.title;
  if(![_entryDetails.string isEqualToString:details])_entryDetails.string=details;
  _entryInspectorButton.title=_overlay&&_overlay->InspectorVisible()?
      @"Hide Inspector":@"Show Inspector…";
}

- (void)presentPermissionEntry:(seethis::platform::PermissionEntryTrigger)trigger {
  if(!_readiness || !seethis::platform::PermissionEntryShouldPresent(
      trigger,_readiness->input_monitor_permission.load(),
      _readiness->screen_recording.load()))
    return;
  if(!_entryPanel) {
    _entryPanel=[[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,640,620)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    _entryPanel.title=@"SeeThis";
    _entryPanel.releasedWhenClosed=NO;
    _entryPanel.hidesOnDeactivate=NO;
    _entryPanel.collectionBehavior=NSWindowCollectionBehaviorMoveToActiveSpace;
    NSView* content=_entryPanel.contentView;
    NSTextField* introduction=[NSTextField wrappingLabelWithString:
        @"SeeThis is running. Ready reflects the two macOS capture permissions. Operational issues and optional Chrome access appear below."];
    introduction.frame=NSMakeRect(22,565,596,42);
    [content addSubview:introduction];
    _entryStatus=[NSTextField labelWithString:@""];
    _entryStatus.font=[NSFont boldSystemFontOfSize:16];
    _entryStatus.frame=NSMakeRect(22,535,596,24);
    [content addSubview:_entryStatus];
    NSScrollView* scroll=[[NSScrollView alloc]
        initWithFrame:NSMakeRect(22,225,596,300)];
    scroll.borderType=NSBezelBorder;
    scroll.hasVerticalScroller=YES;
    scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    _entryDetails=[[NSTextView alloc]
        initWithFrame:NSMakeRect(0,0,596,300)];
    _entryDetails.editable=NO;
    _entryDetails.selectable=YES;
    _entryDetails.font=[NSFont systemFontOfSize:12];
    _entryDetails.textContainerInset=NSMakeSize(8,8);
    _entryDetails.textContainer.widthTracksTextView=YES;
    scroll.documentView=_entryDetails;
    [content addSubview:scroll];
    NSButton* (^addButton)(NSString*,SEL,NSRect)=
        ^NSButton*(NSString* title,SEL action,NSRect frame) {
          NSButton* button=[NSButton buttonWithTitle:title target:self action:action];
          button.bezelStyle=NSBezelStyleRounded;button.frame=frame;
          [content addSubview:button];return button;
        };
    addButton(@"Open Input Monitoring Settings…",@selector(openInputMonitoringSettings:),
        NSMakeRect(22,185,292,30));
    addButton(@"Open Screen Recording Settings…",@selector(openScreenRecordingSettings:),
        NSMakeRect(326,185,292,30));
    addButton(@"Retry Input Monitoring",@selector(retryInputMonitoring:),
        NSMakeRect(22,147,292,30));
    _entryInspectorButton=addButton(@"Show Inspector…",@selector(toggleInspector:),
        NSMakeRect(326,147,292,30));
    addButton(@"Connect Chrome",@selector(connectChrome:),
        NSMakeRect(22,109,292,30));
    addButton(@"Retry Chrome",@selector(retryChrome:),
        NSMakeRect(326,109,292,30));
    addButton(@"Open Chrome Automation Settings…",
        @selector(openChromeAutomationSettings:),NSMakeRect(22,71,292,30));
    addButton(@"Open Settings…",@selector(openSettings:),
        NSMakeRect(326,71,292,30));
    addButton(@"Reveal This App in Finder",@selector(revealCurrentApp:),
        NSMakeRect(22,33,292,30));
    addButton(@"Quit SeeThis",@selector(quit:),NSMakeRect(326,33,292,30));
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
  _chromeLastCheckMs=0;
  [self refreshPermissionMenu];
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

- (void)showInspector:(id)sender {
  (void)sender;
  if(!_overlay)return;
  if(!_overlay->InspectorVisible())_overlay->ToggleInspector();
  Class inspectorClass=NSClassFromString(@"STInspectorPanel");
  for(NSWindow* window in NSApplication.sharedApplication.windows) {
    if(inspectorClass && [window isKindOfClass:inspectorClass] &&
        window.isVisible) {
      [window orderFrontRegardless];
      break;
    }
  }
  [self refreshPermissionMenu];
}

- (void)retryInputMonitoring:(id)sender {
  (void)sender;if(_input)_input->RetryInputMonitoring();[self refreshPermissionMenu];
}

- (void)connectChrome:(id)sender {
  (void)sender;
  _chromeActionError=nil;
  seethis::platform::ConnectChromeExplicit();
  [self refreshPermissionMenu];
}
- (void)retryChrome:(id)sender {
  (void)sender;
  _chromeActionError=nil;
  seethis::platform::RetryChromeExplicit();
  [self refreshPermissionMenu];
}
- (void)openChromeAutomationSettings:(id)sender {
  (void)sender;
  if(!seethis::platform::OpenChromeAutomationSettingsExplicit())
    _chromeActionError=@"\nCould not open Settings. Open System Settings > Privacy & Security > Automation > SeeThis > Google Chrome, then choose Retry Chrome.";
  else
    _chromeActionError=nil;
  [self refreshPermissionMenu];
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
  seethis::platform::StopChromeConnection();
  [_statusTimer invalidate];_statusTimer=nil;
  [_entryPanel close];_entryPanel=nil;
  _entryStatus=nil;_entryDetails=nil;_entryInspectorButton=nil;
  _chromeStatus=nil;_chromeActionError=nil;_chromeLastCheckMs=0;
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
  _statusItem=nil;_startupStateItem=nil;_chromeStateItem=nil;
  _privacyPolicyAction=nil;
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

#endif  // !SEETHIS_CAPTURE_POINTER_TESTING
