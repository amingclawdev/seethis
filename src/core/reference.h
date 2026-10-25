#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/interaction.h"

namespace seethis::core {
using Bytes = std::vector<std::uint8_t>;
struct Rect { double x = 0, y = 0, width = 0, height = 0; };
struct Stamp { std::int64_t utc_us = 0, monotonic_us = 0; };
// All logical rectangles use global AppKit points (Y up). Image rectangles use
// top-left pixels (Y down). Transforms are x'=a*x+tx, y'=d*y+ty.
struct Transform { double a = 1, d = 1, tx = 0, ty = 0; };
struct FrozenDisplay {
  std::string uuid;
  DisplayId id = 0;
  Rect logical, pixels;
  double scale = 1;
  Transform logical_to_pixels;
};
// Additive persistence extension; version zero is genuinely historical data.
// The observation session/incarnation proves only disappearance actually seen by
// this SeeThis process, never an OS window-creation identity.
struct NativeWindowIdentity {
  std::uint64_t version = 0;
  std::int64_t process_start_identity_us = 0;
  std::string observation_session;
  std::uint64_t incarnation = 0;
};
class NativeWindowLifetimeTracker {
 public:
  explicit NativeWindowLifetimeTracker(std::string session) : session_(std::move(session)) {}
  // Call only with a successful complete (including offscreen) process inventory.
  void Observe(std::int64_t pid, std::int64_t start,
               const std::vector<std::uint64_t>& window_ids,
               std::uint64_t generation, bool complete = true);
  NativeWindowIdentity Identity(std::int64_t pid, std::int64_t start,
                                std::uint64_t window_id) const;
 private:
  struct Entry { std::int64_t pid, start; std::uint64_t id, incarnation; bool present; };
  std::string session_;
  std::uint64_t next_ = 0, generation_ = 0;
  std::vector<Entry> entries_;
};
// title-scoped-v1: exact UTF-8 bytes, at most 1024, never normalized. Ready
// means a consistent provider observation, not proof against silent provider
// truncation. Identical native tuple/title sessions remain indistinguishable.
enum class TitleAvailability : std::uint64_t { kUnavailable, kReady, kDenied,
  kStale, kInconsistent, kKnownTruncated, kAmbiguous, kInvalidOrOverLimit };
struct TitleEvidence {
  // Zero is immutable legacy capture evidence, interpreted without rewriting it.
  std::uint64_t version = 0;
  TitleAvailability availability = TitleAvailability::kUnavailable;
  // Retains even a malformed zero-version policy tail without legacy downgrade.
  bool policy_present = false;
};
struct Context {
  std::int64_t pid = 0, window_pid = 0, self_pid = 0;
  std::uint64_t window_id = 0;
  std::string app_name, bundle_id, executable, window_title;
  std::string selection_method;
  std::int64_t window_layer = 0;
  Rect window;
  double quartz_to_appkit_top = 0;
  Stamp observed;
  std::vector<FrozenDisplay> displays;
  std::vector<std::uint64_t> excluded_window_ids;
  std::string exclusion_method;
  NativeWindowIdentity native_window;
  TitleEvidence title;
};
// Page identity is separate from the frozen app/window context. The
// navigation digest is domain-separated; display URL metadata is stored separately.
struct PageIdentity {
  std::string provider = "chrome";
  // Version 1 records used a projected monotonic lifetime. Version 2 uses the
  // stable browser process-start identity derived from launchDate.
  std::uint32_t provider_version = 2;
  std::string bundle_id = "com.google.Chrome";
  std::int64_t browser_pid = 0;
  std::uint64_t window_id = 0;
  Rect window_bounds;
  std::int64_t process_start_identity_us = 0;
  std::string opaque_tab_id;
  std::string navigation_digest;
};
enum class PageAvailability { kUnavailable, kDenied, kReady, kExpired,
  kAmbiguous, kTimedOut };
enum class SourceFieldAvailability { kCaptured, kLegacyUnavailable, kNotApplicable,
  kInvalidOrOverLimit };
const char* SourceFieldAvailabilityName(SourceFieldAvailability availability);
// Display metadata only: these bounds never decide capture eligibility.
bool ValidSourceContextText(std::string_view value, std::size_t maximum_bytes);
struct CapturedPageMetadata {
  std::optional<std::string> url;
  SourceFieldAvailability availability = SourceFieldAvailability::kLegacyUnavailable;
  std::optional<std::int64_t> observed_utc_us;
};
struct PageObservation {
  PageAvailability availability = PageAvailability::kUnavailable;
  std::uint64_t generation = 0;
  Stamp observed;
  std::optional<PageIdentity> identity;
  std::optional<CapturedPageMetadata> captured_page;
};
#define SEETHIS_WINDOW_OBSERVATION_API 1
enum class WindowAvailability { kUnavailable, kReady };
struct WindowObservation {
  WindowAvailability availability = WindowAvailability::kUnavailable;
  std::uint64_t generation = 0;
  Stamp observed;
  std::int64_t pid = 0;
  std::string bundle_id;
  std::uint64_t window_id = 0;
  Rect bounds;
  NativeWindowIdentity native_window;
  std::string window_title;
  TitleEvidence title{1, TitleAvailability::kUnavailable};
};
struct Pixels {
  std::uint32_t width = 0, height = 0;
  Bytes rgba;
};
struct Image {
  std::uint32_t width = 0, height = 0;
  std::string format = "image/png", color_space = "sRGB", sha256;
  // Ready-sidecar v4 persists these separately from the legacy record layout.
  // Old sidecars are explicitly mapped to the deterministic stored-block codec.
  std::string encoding = "png-zlib-v1";
  std::string pixel_hash_contract;
  std::string pixel_sha256;
  Bytes bytes;
};
struct ReferenceRegion {
  // `path` is the schema-1/schema-2 legacy projection. New schema-2 records
  // use explicit contiguous subpaths and leave this field empty.
  std::vector<DisplayPoint> path;
  Rect region;
  Rect crop_pixels;
  std::vector<std::vector<DisplayPoint>> subpaths;
};
struct Reference {
  // Collection-local rendering offset; immutable legacy serialization is unchanged.
  std::size_t first_mark_slot = 0;
  DisplayId capture_display_id = 0;
  Stamp operation_started;
  std::uint32_t schema = 1;
  std::string id, mark_id;
  Context context;
  Stamp requested, image_completed, circle_completed;
  std::string clock = "UTC:system_clock/us;monotonic:steady_clock/us;process-session";
  std::string capture_api = "ScreenCaptureKit.SCScreenshotManager.exact-window";
  std::string provenance = "historical;context-before-overlay;pixels-at-async-completion;no-semantic-object-id";
  std::vector<DisplayPoint> path;
  std::vector<ReferenceRegion> regions;
  // Zero means the legacy region encoding; one is the explicit subpath tail.
  std::uint64_t subpath_version = 0;
  Rect region, crop_pixels;
  Transform global_to_source;
  double crop_margin = 8;
  Image source, crop;
  // Empty denotes a legacy app/window-scoped reference.
  std::optional<PageIdentity> page_identity;
  std::optional<CapturedPageMetadata> captured_page;
};
struct StoredReference {
  Reference value;
  // Exactly these representations are written on both initial copy and re-copy.
  Bytes record;
  Bytes clipboard_text;
  Bytes clipboard_png;
  // Process-local bridge text. It is never part of the immutable store record.
  // The canonical v1 envelope remains embedded verbatim in this v2 handoff.
  Bytes agent_clipboard_text;
};
struct CaptureResult {
  Pixels pixels;
  Stamp requested, completed;
  std::string error;
};
Stamp Now();
std::string Sha256(const Bytes& bytes);
Bytes EncodePng(const Pixels& pixels);
bool ValidPixels(const Pixels& pixels);
bool Validate(const Reference& reference, std::string* error = nullptr);
Bytes Serialize(const Reference& reference);
std::optional<Reference> Deserialize(const Bytes& bytes);
StoredReference Canonical(const Reference& reference);
std::optional<Rect> CropBounds(const Context& context,
                              const std::vector<DisplayPoint>& path,
                              std::uint32_t width, std::uint32_t height,
                              double margin);
std::vector<ReferenceRegion> EffectiveRegions(const Reference& reference);
template <typename Function>
void ForEachSubpath(const ReferenceRegion& region, Function&& function) {
  if (!region.subpaths.empty()) {
    for (const auto& subpath : region.subpaths) function(subpath);
  } else if (!region.path.empty()) {
    function(region.path);
  }
}
bool DisplayTopologyMatches(const std::vector<FrozenDisplay>& frozen,
                            const std::vector<FrozenDisplay>& current);
bool ValidPageIdentity(const PageIdentity& identity, std::string* error = nullptr);
bool SamePageIdentity(const PageIdentity& left, const PageIdentity& right);
bool PageIdentityMatchesContext(const PageIdentity& identity,
                                const Context& context);
bool PageIdentityVisible(const std::optional<PageIdentity>& required,
                         const PageObservation& current);
bool WindowObservationMatchesContext(const Context& required,
                                     const WindowObservation& current);
// Diagnostic capability for View details; never a unique-session guarantee.
const char* TitleScopeCapabilityName(const Context& context);
bool CapturedTitleAvailable(const Context& context);
bool TitleContextVisible(const Context& required, const WindowObservation& current);
bool ReferenceVisible(const Context& context,
                      const std::optional<PageIdentity>& page_identity,
                      const WindowObservation& window_observation,
                      const PageObservation& page_observation);

enum class SaveResult {
  kSaved,
  kIdempotent,
  kConflict,
  kDeleted,
  kInvalid,
  kIOError,
};
enum class ReferenceJobState {
  kPending,
  kIndexing,
  kReady,
  kFailed,
  kExpired,
  kDeleted,
};
enum class DeleteResult {
  kDeleted,
  kAlreadyDeleted,
  kNotFound,
  kCleanupFailed,
};
struct ReferenceJobSnapshot {
  std::string id;
  ReferenceJobState state = ReferenceJobState::kPending;
  std::string code;
  std::int64_t accepted_utc_us = 0;
  std::int64_t deadline_utc_us = 0;
  std::int64_t accepted_monotonic_us = 0;
  std::int64_t deadline_monotonic_us = 0;
  std::int64_t terminal_utc_us = 0;
  std::int64_t expired_utc_us = 0;
  std::int64_t key_down_monotonic_ms = 0;
  std::int64_t key_up_monotonic_ms = 0;
  Stamp circle_completed;
  double crop_margin = 8;
  Context context;
  std::vector<DisplayPoint> path;
  std::vector<ReferenceRegion> regions;
  std::optional<PageIdentity> page_identity;
  std::shared_ptr<const Reference> metadata;
  std::shared_ptr<const StoredReference> ready;
  std::size_t newer_ready = 0;
  std::optional<CapturedPageMetadata> captured_page;
};
struct SourceContextProjection {
  std::optional<std::string> app_name, bundle_id, window_title, page_url;
  SourceFieldAvailability app_name_availability = SourceFieldAvailability::kLegacyUnavailable;
  SourceFieldAvailability bundle_id_availability = SourceFieldAvailability::kLegacyUnavailable;
  SourceFieldAvailability window_title_availability = SourceFieldAvailability::kLegacyUnavailable;
  SourceFieldAvailability page_url_availability = SourceFieldAvailability::kLegacyUnavailable;
  std::optional<std::uint64_t> window_id;
  std::optional<std::int64_t> context_observed_utc_us, image_completed_utc_us,
      page_url_observed_utc_us;
};
SourceContextProjection ProjectSourceContext(const Reference& reference);
SourceContextProjection ProjectSourceContext(const ReferenceJobSnapshot& job);
struct ReferenceStoreTestHooks {
  std::function<void(std::string_view stage, std::string_view id)> stage;
  std::function<void(std::string_view id)> before_derive_crop;
  std::function<void(std::string_view id)> before_derive_annotated;
  std::function<Stamp()> now;
  std::function<bool(const std::filesystem::path&)> remove_owned_file;
  bool fail_save = false;
  bool fail_note_after_rename = false;
  bool fail_append_before_publish = false;
  bool fail_append_after_rename = false;
};
enum class ReadyAssetState { kReady, kPending, kFailed };
struct ReadyAssetResult {
  ReadyAssetState state = ReadyAssetState::kFailed;
  std::shared_ptr<const Bytes> bytes;
  std::string code;
  // Hash of the delivered PNG bytes. Demand-derived v4 crops keep their stable
  // RGBA digest in Image::pixel_sha256 instead of mislabeling it as this hash.
  std::string byte_sha256;
};
enum class MarkNotesStatus {
  kReady, kPending, kFailed, kNotFound, kDeleted, kExpired, kCorrupt, kUnsupported,
  kIOError,
};
struct MarkSnapshot {
  std::string id;
  // The immutable outer gesture slot, including gestures hidden by the UI.
  std::size_t slot = 0;
  std::uint32_t number = 0;
  DisplayPoint anchor;
  // Source-image pixels, including room for the delivered number badge.
  Rect delivery_bounds;
  std::string note;
  bool visible = true;
  std::uint64_t revision = 0;
  std::string shot_id;
  std::size_t local_slot = 0;
};
struct MarkNotesSnapshot {
  MarkNotesStatus status = MarkNotesStatus::kNotFound;
  std::uint64_t revision = 0;
  std::vector<MarkSnapshot> marks;
};
enum class MarkNoteUpdateStatus {
  kUpdated, kRevisionConflict, kNotFound, kDeleted, kExpired, kInvalid,
  kCorrupt, kUnsupported, kIOError, kCommitUncertain,
};
struct MarkNoteUpdateResult {
  MarkNoteUpdateStatus status = MarkNoteUpdateStatus::kInvalid;
  std::uint64_t revision = 0;
};
std::string StableMarkId(std::string_view reference_id, std::size_t slot);
struct ReferenceShot {
  std::string id;
  std::size_t first_mark_slot = 0;
  std::shared_ptr<const Reference> metadata;
};
struct ReferenceCollection {
  MarkNotesStatus status = MarkNotesStatus::kNotFound;
  std::uint64_t revision = 1, notes_revision = 0;
  std::vector<ReferenceShot> shots;
  std::vector<std::string> notes;
  std::vector<bool> visible;
  [[nodiscard]] bool multi() const { return shots.size() > 1; }
};
enum class AppendStatus { kCommitted, kIdempotent, kStale, kUnavailable, kCancelled,
  kInvalid, kBusy, kIOError, kCommitUncertain };
struct AppendResult {
  AppendStatus status = AppendStatus::kInvalid;
  std::string reference_id, shot_id;
  std::uint64_t revision = 0;
};
class ReferenceStore {
 public:
  using Completion = std::function<void(ReferenceJobSnapshot)>;
  static constexpr std::size_t kMaximumUnfinishedJobs = 4;
  static constexpr std::size_t kMaximumReservedBytes = 512ULL * 1024 * 1024;
  static constexpr std::size_t kMaximumCachedReadyPayloads = 8;
  static constexpr std::string_view kDisplayUnderlayGeometryVersion = "stroke-raster-v1";

  explicit ReferenceStore(std::filesystem::path directory);
  ~ReferenceStore();
  ReferenceStore(const ReferenceStore&) = delete;
  ReferenceStore& operator=(const ReferenceStore&) = delete;
  SaveResult Save(const StoredReference& reference, std::string* error = nullptr);
  std::vector<StoredReference> Load(std::vector<std::string>* errors = nullptr) const;
  void SetRetentionPolicy(std::int64_t ttl_seconds,
                          std::size_t maximum_visible);
  ReferenceJobSnapshot AcceptPending(Reference reference,
                                     std::int64_t key_down_monotonic_ms,
                                     std::int64_t key_up_monotonic_ms);
  void FinalizeAsync(std::string id, CaptureResult capture,
                     Completion completion = {});
  void Fail(std::string_view id, std::string code,
            Completion completion = {});
  [[nodiscard]] DeleteResult Delete(std::string_view id);
  [[nodiscard]] std::optional<ReferenceJobSnapshot> LookupMetadata(
      std::string_view id) const;
  [[nodiscard]] std::optional<Bytes> ReadReadyAsset(
      std::string_view id, bool crop) const;
  // Cold schema-2 crops are derived on the bounded store executor so a client
  // can poll truthfully without holding one HTTP request open for image work.
  [[nodiscard]] ReadyAssetResult RequestReadyCrop(std::string_view id);
  // Full source-size freehand image. Cold generation and cache loading run on
  // the bounded store executor; callers poll while the result is pending.
  [[nodiscard]] ReadyAssetResult RequestReadyAnnotated(std::string_view id);
  // Owned full-size source plus the same strokes, without number badges/leaders.
  // This representation is derived in memory and never reads/writes numbered disk assets.
  [[nodiscard]] ReadyAssetResult RequestReadyDisplayUnderlay(std::string_view id);
  // One immutable outer gesture, composited over its own source-screenshot crop.
  // The index is the stored drawing order, never a filtered presentation index.
  [[nodiscard]] ReadyAssetResult RequestReadyMark(std::string_view id,
                                                   std::size_t region_index);
  [[nodiscard]] MarkNotesSnapshot MarkSnapshots(std::string_view id) const;
  [[nodiscard]] MarkNoteUpdateResult UpdateMarkNote(
      std::string_view id, std::string_view mark_id, std::string note,
      std::uint64_t expected_revision);
  // Visibility shares the revisioned mark sidecar with notes. The result uses
  // the same write statuses, including kCommitUncertain after a rename.
  [[nodiscard]] MarkNoteUpdateResult UpdateMarkVisibility(
      std::string_view id, std::string_view mark_id, bool visible,
      std::uint64_t expected_revision);
  [[nodiscard]] std::optional<ReferenceJobSnapshot> Lookup(
      std::string_view id) const;
  [[nodiscard]] std::vector<ReferenceJobSnapshot> Jobs() const;
  [[nodiscard]] std::vector<ReferenceJobSnapshot> MetadataJobs() const;
  [[nodiscard]] ReferenceCollection Collection(std::string_view id) const;
  using AppendCompletion = std::function<void(AppendResult)>;
  void AppendAsync(std::string root_id, std::string selected_mark_id,
      std::uint64_t expected_content_revision, std::string operation_shot_id,
      Reference capture_metadata, CaptureResult capture, AppendCompletion completion);
  void AppendReferenceAsync(std::string root_id,std::uint64_t expected_content_revision,
      std::string operation_shot_id,Reference capture_metadata,CaptureResult capture,
      AppendCompletion completion);
  void CancelAppend(std::string_view operation_shot_id);
  [[nodiscard]] ReadyAssetResult RequestShotAnnotated(std::string_view root_id,
      std::string_view shot_id, bool numbers = true);
  void SetTestHooks(ReferenceStoreTestHooks hooks);
  void WaitForIdleForTesting();
 private:
  struct Impl;
  enum class AnnotatedRepresentation { kNumbered, kDisplayUnderlay, kSourceCrop };
  ReadyAssetResult RequestAnnotated(std::string_view id,
                                     std::optional<std::size_t> region_index,
                                     AnnotatedRepresentation representation =
                                         AnnotatedRepresentation::kNumbered);
  void Prune();
  void ScheduleLegacyMigrations();
  void MigrateLegacyReady(std::string id);
  bool CleanupDeletedFiles(std::string_view id);
  void ReleaseOwner(std::string_view id);
  std::filesystem::path directory_;
  std::unique_ptr<Impl> impl_;
};

enum class ReferencePhase { kIdle, kContextPending, kCapturePending,
  kReleasedPendingCapture, kReady, kCommitted, kAborted };
enum class CopyResult { kNone, kCopied, kPersistedClipboardFailed, kFailed };
struct ReferenceMark { Reference value; };
// Saved display is independent of capture/presentation leases. Exclusions name
// positive coordinate/lifetime evidence for an immutable root+shot+source.
enum class SavedProjectionState { kVisible, kTemporarilyHidden, kCoordinateInvalid, kTerminal };
enum class SavedCoordinateLoss { kWindowGeometry, kDisplayTopology, kNativeLifetime };

// Query identity only; observations must be freshly acquired by the native owner.
// epoch records the originating capture witness, not a saved-display lease.
struct OwnUIPresentationSourceTarget {
  std::uint64_t generation=0,epoch=0;
  Context context;
  std::optional<PageIdentity> page;
};
class MarkController {
 public:
  using Clipboard = std::function<bool(const StoredReference&)>;
  using PendingClipboard =
      std::function<std::optional<std::int64_t>(std::string_view id)>;
  using ReadyClipboard = std::function<bool(
      std::shared_ptr<const StoredReference>,
      std::int64_t expected_change_count)>;
  using Dispatcher = std::function<void(std::function<void()>)>;
  MarkController(ReferenceStore& store, Clipboard clipboard);
  MarkController(ReferenceStore& store, PendingClipboard pending_clipboard,
                 ReadyClipboard ready_clipboard, Dispatcher dispatcher);
  ~MarkController();
  std::uint64_t Begin(std::string id, double crop_margin = 8.0,
                      std::int64_t key_down_monotonic_ms = 0);
  std::uint64_t BeginAppend(std::string root_id,std::string selected_mark_id,
      std::uint64_t expected_revision,std::string operation_shot_id,
      double crop_margin = 8.0,std::int64_t key_down_monotonic_ms = 0);
  std::uint64_t BeginAppendReference(std::string root_id,std::uint64_t expected_revision,
      std::string operation_shot_id,double crop_margin=8.0,
      std::int64_t key_down_monotonic_ms=0);
  void SetAppendCompletion(std::function<void(AppendResult,std::uint64_t)> callback) {
    append_completion_=std::move(callback);
  }
  void SetViewportContinuity(std::uint64_t epoch,bool healthy);
  void RetireLiveShot();
  void RetireReferencePresentation(std::string_view root_id);
  [[nodiscard]] std::optional<OwnUIPresentationSourceTarget> BeginOwnUIPresentationSource();
  [[nodiscard]] std::optional<OwnUIPresentationSourceTarget> own_ui_presentation_source() const;
  bool PublishOwnUIPresentationObservation(const OwnUIPresentationSourceTarget& target,
      WindowObservation window,PageObservation page);
  void EndOwnUIPresentationSource();
  // Only the exact admitted/presented member may use the private channel.
  [[nodiscard]] bool PresentedReferenceVisible(const Reference& reference) const;
  [[nodiscard]] SavedProjectionState SavedProjection(const Reference& reference) const;
  void SetDisplayObservation(std::vector<FrozenDisplay> displays);

  void SetOwnInputFocus(bool own) { own_input_focus_=own;if(!own)own_ui_query_identity_.reset(); }
  void SetPresentationPermitted(bool permitted);
  bool BeginPresentationPrefix();
  bool ConfirmPresentationPrefix();
  void CancelPresentationPrefix();
  [[nodiscard]] bool PresentationHitsAllowed() const { return !presentation_prefix_; }
  void RecordPresentedSingle(const Reference& reference,
      const std::vector<FrozenDisplay>& displays);
  [[nodiscard]] std::vector<ReferenceShot> LiveShots(std::string_view root_id) const;
  // Compatibility name: current saved projection, never capture authorization.
  [[nodiscard]] bool IsShotAdmitted(std::string_view root_id,std::string_view shot_id) const;

  [[nodiscard]] std::optional<ReferenceShot> LiveShot(std::string_view root_id) const;
  void BindCaptureDisplay(std::uint64_t generation,DisplayId id);
  bool BindContext(std::uint64_t generation, Context context);
  bool BindPageIdentity(std::uint64_t generation, PageIdentity identity,
                        std::optional<CapturedPageMetadata> captured_page = std::nullopt);
  void Captured(std::uint64_t generation, CaptureResult result);
  void Release(std::uint64_t generation,
               std::vector<std::vector<DisplayPoint>> regions,
               Stamp time, std::int64_t key_up_monotonic_ms = 0);
  void Release(std::uint64_t generation,
               std::vector<InteractionSnapshot::LogicalRegion> regions,
               Stamp time, std::int64_t key_up_monotonic_ms = 0);
  void Release(std::uint64_t generation, std::vector<DisplayPoint> path,
               Stamp time, std::int64_t key_up_monotonic_ms = 0);
  void Abort(std::string reason);
  void Expire(Stamp now);
  bool AllowsDrawing() const;
  void SetWindowObservation(WindowObservation observation);
  [[nodiscard]] WindowObservation window_observation() const {
    return window_observation_;
  }
  void SetPageObservation(PageObservation observation);
  [[nodiscard]] PageObservation page_observation() const {
    return page_observation_;
  }
  bool Pending() const;
  ReferencePhase phase() const { return phase_; }
  const std::string& status() const { return status_; }
  CopyResult copy_result() const { return copy_result_; }
  const std::string& last_copy_id() const { return last_copy_id_; }
  [[nodiscard]] std::vector<ReferenceMark> marks() const;
  [[nodiscard]] std::vector<ReferenceJobSnapshot> jobs() const;
  [[nodiscard]] std::vector<ReferenceJobSnapshot> inspector_jobs() const;
  [[nodiscard]] bool NeedsChromePageObservation() const;
  [[nodiscard]] bool AwaitingPageIdentity(std::uint64_t generation) const;
  std::optional<std::size_t> Hit(DisplayPoint point, double radius,
                               const std::vector<FrozenDisplay>& displays) const;
  std::optional<std::string> HitJob(
      DisplayPoint point, double radius,
      const std::vector<FrozenDisplay>& displays) const;
  std::optional<std::string> HitIdentity(
      DisplayPoint point, double radius,
      const std::vector<FrozenDisplay>& displays) const;
  bool Recopy(std::size_t index);
  bool RecopyJob(std::string_view id);
  // Explicit Inspector history copy of a persisted ready reference. Unlike
  // mark recopy, this does not depend on the current foreground window/page.
  bool CopyHistoryJob(std::string_view id);
  DeleteResult DeleteJob(std::string_view id);
  // Explicit Inspector history action; it does not use the live page gate.
  DeleteResult DeleteHistoryJob(std::string_view id);
 private:
  struct Transaction;
  bool Current(std::uint64_t generation) const;
  void CheckPendingWindowContinuity(const WindowObservation& observation);
  void CheckPendingPageContinuity(const PageObservation& observation);
  bool LiveActionVisible(const ReferenceJobSnapshot& job) const;
  void PrunePresentations();
  void PreparePresentationObservation();
  void ReleasePrepared(Stamp time, std::int64_t key_up_monotonic_ms);
  void AcceptReleased();
  void SubmitIfReady(std::uint64_t generation);
  void Completed(ReferenceJobSnapshot job);
  DeleteResult DeleteStoredJob(std::string_view id);
  ReferenceStore& store_;
  Clipboard clipboard_;
  PendingClipboard pending_clipboard_;
  ReadyClipboard ready_clipboard_;
  Dispatcher dispatcher_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  std::uint64_t generation_ = 0;
  std::uint64_t copy_generation_ = 0;
  ReferencePhase phase_ = ReferencePhase::kIdle;
  CopyResult copy_result_ = CopyResult::kNone;
  std::string last_copy_id_;
  std::string status_;
  std::unique_ptr<Transaction> current_;
  std::vector<std::unique_ptr<Transaction>> accepted_;
  std::function<void(AppendResult,std::uint64_t)> append_completion_;
  std::uint64_t input_epoch_ = 0;
  bool input_healthy_ = false, own_input_focus_ = false;
  struct AdmittedShot { ReferenceShot shot;std::uint64_t epoch=0; };
  std::vector<AdmittedShot> admitted_,fresh_singles_,presented_singles_;
  bool presentation_prefix_=false,presentation_permitted_=true;
  std::uint64_t presentation_generation_=0;
  std::optional<OwnUIPresentationSourceTarget> own_ui_source_;
  // Identity custody only, never a freshness/admission/capture witness. Kept
  // across a private refresh failure only while the exact own UI stays active.
  std::optional<OwnUIPresentationSourceTarget> own_ui_query_identity_;
  WindowObservation own_ui_window_;
  PageObservation own_ui_page_;
  std::uint64_t own_ui_generation_=0;
  struct SavedCoordinateExclusion {
    std::string root,shot,source_sha256;
    SavedCoordinateLoss reason;
  };
  std::vector<SavedCoordinateExclusion> coordinate_exclusions_;
  std::optional<std::vector<FrozenDisplay>> display_observation_;
  void RetireCaptureWitnesses();
  void ExcludeSavedShot(const ReferenceShot& shot,SavedCoordinateLoss reason);
  void ObserveSavedWindowGeometry(const WindowObservation& observation);
  WindowObservation window_observation_;
  PageObservation page_observation_;
};
}  // namespace seethis::core

namespace seethis::platform {
// Native providers return only owned C++ values. Capture callbacks run on main.
std::vector<core::FrozenDisplay> SnapshotDisplays();
std::optional<core::Context> SnapshotContext(core::DisplayPoint initial,
    const std::vector<std::uint64_t>& overlay_window_ids, std::string& error);
void CaptureDisplay(core::Context context, core::DisplayId display_id,
                   std::function<void(core::CaptureResult)> completion);
bool CopyReference(const core::StoredReference& reference);
std::optional<std::int64_t> CopyReferenceText(std::string_view text);
bool CompleteReferenceClipboard(
    std::shared_ptr<const core::StoredReference> reference,
    std::string_view text, std::int64_t expected_change_count);
}  // namespace seethis::platform
