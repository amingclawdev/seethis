#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace seethis::core {

enum ShortcutModifier : std::uint32_t {
  kShortcutCommand = 1U << 0,
  kShortcutControl = 1U << 1,
  kShortcutOption = 1U << 2,
  kShortcutShift = 1U << 3,
};

struct Settings {
  std::uint16_t shortcut_key_code = 0;  // macOS ANSI A.
  std::uint32_t shortcut_modifiers = kShortcutOption;
  std::uint16_t delete_shortcut_key_code = 2;  // macOS ANSI D.
  std::uint32_t delete_shortcut_modifiers = kShortcutOption;
  std::int64_t maximum_hold_ms = 30'000;
  double crop_margin_points = 8.0;
  double mark_hit_radius_points = 10.0;
  std::int64_t reference_ttl_seconds = 7 * 24 * 60 * 60;
  std::size_t maximum_visible_references = 500;
};

enum class SettingsLoadState {
  kReady,
  kUnsupported,
  kInvalid,
};

[[nodiscard]] constexpr const char* SettingsLoadStateName(
    SettingsLoadState state) {
  switch (state) {
    case SettingsLoadState::kReady: return "ready";
    case SettingsLoadState::kUnsupported: return "unsupported";
    case SettingsLoadState::kInvalid: return "invalid";
  }
  return "invalid";
}

[[nodiscard]] bool ValidateSettings(const Settings& settings,
                                    std::string* error = nullptr);
[[nodiscard]] bool ShortcutBindingsConflict(const Settings& settings);
[[nodiscard]] std::string SettingsJson(const Settings& settings);
[[nodiscard]] std::optional<Settings> ParseSettingsJson(
    const std::string& json, std::string* error = nullptr);

class SettingsStore {
 public:
  explicit SettingsStore(std::filesystem::path path);

  [[nodiscard]] Settings Get() const;
  [[nodiscard]] SettingsLoadState load_state() const { return load_state_; }
  [[nodiscard]] bool usable() const {
    return load_state_ == SettingsLoadState::kReady;
  }
  [[nodiscard]] const std::string& load_error() const { return load_error_; }
  [[nodiscard]] std::string ReferenceCapability(
      const std::string& reference_id) const;
  [[nodiscard]] bool Update(const Settings& settings,
                            std::string* error = nullptr);
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  bool Persist(const Settings& settings, const std::string& secret,
               std::string* error);

  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::mutex update_mutex_;
  Settings settings_;
  std::string reference_secret_;
  SettingsLoadState load_state_ = SettingsLoadState::kReady;
  std::string load_error_;
  bool reference_capability_available_ = false;
};

}  // namespace seethis::core
