#pragma once

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
};
// Page identity is separate from the frozen app/window context. The
// navigation digest is domain-separated; raw URL/title data never persists.
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
struct PageObservation {
  PageAvailability availability = PageAvailability::kUnavailable;
  std::uint64_t generation = 0;
  Stamp observed;
  std::optional<PageIdentity> identity;
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
};
struct ReferenceStoreTestHooks {
  std::function<void(std::string_view stage, std::string_view id)> stage;
  std::function<void(std::string_view id)> before_derive_crop;
  std::function<void(std::string_view id)> before_derive_annotated;
  std::function<Stamp()> now;
  std::function<bool(const std::filesystem::path&)> remove_owned_file;
  bool fail_save = false;
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
class ReferenceStore {
 public:
  using Completion = std::function<void(ReferenceJobSnapshot)>;
  static constexpr std::size_t kMaximumUnfinishedJobs = 4;
  static constexpr std::size_t kMaximumReservedBytes = 512ULL * 1024 * 1024;
  static constexpr std::size_t kMaximumCachedReadyPayloads = 8;

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
  [[nodiscard]] std::optional<ReferenceJobSnapshot> Lookup(
      std::string_view id) const;
  [[nodiscard]] std::vector<ReferenceJobSnapshot> Jobs() const;
  [[nodiscard]] std::vector<ReferenceJobSnapshot> MetadataJobs() const;
  void SetTestHooks(ReferenceStoreTestHooks hooks);
  void WaitForIdleForTesting();
 private:
  struct Impl;
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
  bool BindContext(std::uint64_t generation, Context context);
  bool BindPageIdentity(std::uint64_t generation, PageIdentity identity);
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
