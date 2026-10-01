#include "core/settings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <sys/stat.h>
#include <sys/random.h>
#include <unistd.h>

#include "core/reference.h"

namespace seethis::core {
namespace {

constexpr std::size_t kMaxSettingsBytes = 16 * 1024;

std::string Hex(const unsigned char* bytes, std::size_t size) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (std::size_t index = 0; index < size; ++index) {
    result.push_back(digits[bytes[index] >> 4]);
    result.push_back(digits[bytes[index] & 15]);
  }
  return result;
}

std::string RandomSecret() {
  std::array<unsigned char, 32> bytes{};
  if (getentropy(bytes.data(), bytes.size()) != 0)
    throw std::runtime_error("secure randomness unavailable");
  return Hex(bytes.data(), bytes.size());
}

bool SafeSecret(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

struct JsonValue {
  std::string text;
  bool quoted = false;
};

using FlatObject = std::map<std::string, JsonValue>;

std::optional<FlatObject> ParseFlatObject(const std::string& json) {
  std::size_t position = 0;
  const auto space = [&] {
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])))
      ++position;
  };
  space();
  if (position == json.size() || json[position++] != '{') return {};
  FlatObject values;
  space();
  if (position < json.size() && json[position] == '}') {
    ++position;
    space();
    return position == json.size() ? std::optional(values) : std::nullopt;
  }
  for (;;) {
    space();
    if (position == json.size() || json[position++] != '"') return {};
    const auto key_start = position;
    while (position < json.size() && json[position] != '"') {
      const unsigned char character = json[position];
      if (!std::isalnum(character) && character != '_') return {};
      ++position;
    }
    if (position == json.size() || position == key_start) return {};
    const std::string key = json.substr(key_start, position - key_start);
    ++position;
    space();
    if (position == json.size() || json[position++] != ':') return {};
    space();
    if (position == json.size()) return {};
    JsonValue value;
    if (json[position] == '"') {
      value.quoted = true;
      const auto start = ++position;
      while (position < json.size() && json[position] != '"') {
        const unsigned char character = json[position];
        if (!std::isalnum(character)) return {};
        ++position;
      }
      if (position == json.size()) return {};
      value.text = json.substr(start, position - start);
      ++position;
    } else {
      const auto start = position;
      if (json[position] == '-') ++position;
      const auto integer_start = position;
      while (position < json.size() &&
             std::isdigit(static_cast<unsigned char>(json[position])))
        ++position;
      if (position == integer_start) return {};
      if (position < json.size() && json[position] == '.') {
        const auto fraction_start = ++position;
        while (position < json.size() &&
               std::isdigit(static_cast<unsigned char>(json[position])))
          ++position;
        if (position == fraction_start) return {};
      }
      value.text = json.substr(start, position - start);
    }
    if (!values.emplace(key, std::move(value)).second) return {};
    space();
    if (position == json.size()) return {};
    if (json[position] == '}') {
      ++position;
      space();
      return position == json.size() ? std::optional(values) : std::nullopt;
    }
    if (json[position++] != ',') return {};
  }
}

bool Number(const FlatObject& values, std::string_view name, double* value) {
  const auto found = values.find(std::string(name));
  if (found == values.end() || found->second.quoted) return false;
  try {
    std::size_t used = 0;
    *value = std::stod(found->second.text, &used);
    return used == found->second.text.size() && std::isfinite(*value);
  } catch (...) {
    return false;
  }
}

bool Integer(const FlatObject& values, std::string_view name,
             std::int64_t* value) {
  double number = 0;
  if (!Number(values, name, &number) || std::floor(number) != number ||
      number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
      number > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
    return false;
  }
  *value = static_cast<std::int64_t>(number);
  return true;
}

bool String(const FlatObject& values, std::string_view name, std::string* value) {
  const auto found = values.find(std::string(name));
  if (found == values.end() || !found->second.quoted) return false;
  *value = found->second.text;
  return true;
}

std::set<std::string> Keys(const FlatObject& values) {
  std::set<std::string> keys;
  for (const auto& [key, value] : values) {
    (void)value;
    keys.insert(key);
  }
  return keys;
}

std::optional<Settings> ParsePublic(const FlatObject& values,
                                    bool allow_legacy,
                                    bool* migrated_legacy,
                                    std::string* error) {
  std::int64_t schema = 0;
  if (!Integer(values, "schema", &schema) ||
      (schema != 1 && schema != 2)) {
    if (error) *error = "unsupported settings schema";
    return {};
  }
  if (schema == 1 && !allow_legacy) {
    if (error) *error = "settings schema 1 must be refreshed before update";
    return {};
  }
  const std::set<std::string> legacy_allowed = {
      "schema", "shortcut_key_code", "shortcut_modifiers",
      "maximum_hold_ms", "crop_margin_points", "mark_hit_radius_points",
      "reference_ttl_seconds", "maximum_visible_references"};
  auto allowed = legacy_allowed;
  if (schema == 2) {
    allowed.insert("delete_shortcut_key_code");
    allowed.insert("delete_shortcut_modifiers");
  }
  const auto keys = Keys(values);
  if (keys != allowed) {
    if (error) *error = "settings keys must match schema exactly";
    return {};
  }
  std::int64_t key = 0, modifiers = 0, delete_key = 2,
               delete_modifiers = kShortcutOption, hold = 0, ttl = 0,
               maximum = 0;
  double crop = 0, hit = 0;
  if (!Integer(values, "shortcut_key_code", &key) ||
      !Integer(values, "shortcut_modifiers", &modifiers) ||
      (schema == 2 &&
       (!Integer(values, "delete_shortcut_key_code", &delete_key) ||
        !Integer(values, "delete_shortcut_modifiers", &delete_modifiers))) ||
      !Integer(values, "maximum_hold_ms", &hold) ||
      !Number(values, "crop_margin_points", &crop) ||
      !Number(values, "mark_hit_radius_points", &hit) ||
      !Integer(values, "reference_ttl_seconds", &ttl) ||
      !Integer(values, "maximum_visible_references", &maximum) || key < 0 ||
      key > std::numeric_limits<std::uint16_t>::max() || delete_key < 0 ||
      delete_key > std::numeric_limits<std::uint16_t>::max() || modifiers < 0 ||
      modifiers > std::numeric_limits<std::uint32_t>::max() ||
      delete_modifiers < 0 ||
      delete_modifiers > std::numeric_limits<std::uint32_t>::max() || maximum < 0) {
    if (error) *error = "settings contain invalid numbers";
    return {};
  }
  Settings settings;
  settings.shortcut_key_code = static_cast<std::uint16_t>(key);
  settings.shortcut_modifiers = static_cast<std::uint32_t>(modifiers);
  settings.delete_shortcut_key_code = static_cast<std::uint16_t>(delete_key);
  settings.delete_shortcut_modifiers =
      static_cast<std::uint32_t>(delete_modifiers);
  settings.maximum_hold_ms = hold;
  settings.crop_margin_points = crop;
  settings.mark_hit_radius_points = hit;
  settings.reference_ttl_seconds = ttl;
  settings.maximum_visible_references = static_cast<std::size_t>(maximum);
  if (!ValidateSettings(settings, error)) return {};
  if (migrated_legacy) *migrated_legacy = schema == 1;
  return settings;
}

std::string StoredJson(const Settings& settings, const std::string& secret) {
  std::string json = SettingsJson(settings);
  json.pop_back();
  return json.substr(0, json.size() - 1) +
         ",\"reference_secret\":\"" + secret + "\"}\n";
}

}  // namespace

bool ValidateSettings(const Settings& settings, std::string* error) {
  auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  constexpr std::uint32_t kAllModifiers =
      kShortcutCommand | kShortcutControl | kShortcutOption | kShortcutShift;
  if (settings.shortcut_key_code > 127) return fail("shortcut key out of range");
  if (settings.shortcut_modifiers == 0 ||
      (settings.shortcut_modifiers & ~kAllModifiers) != 0)
    return fail("shortcut modifiers out of range");
  if (settings.delete_shortcut_key_code > 127)
    return fail("delete shortcut key out of range");
  if (settings.delete_shortcut_modifiers == 0 ||
      (settings.delete_shortcut_modifiers & ~kAllModifiers) != 0)
    return fail("delete shortcut modifiers out of range");
  if (settings.maximum_hold_ms < 1'000 ||
      settings.maximum_hold_ms > 120'000)
    return fail("maximum hold must be between 1000 and 120000 ms");
  if (!std::isfinite(settings.crop_margin_points) ||
      settings.crop_margin_points < 0 || settings.crop_margin_points > 128)
    return fail("crop margin must be between 0 and 128 points");
  if (!std::isfinite(settings.mark_hit_radius_points) ||
      settings.mark_hit_radius_points < 2 ||
      settings.mark_hit_radius_points > 64)
    return fail("mark hit radius must be between 2 and 64 points");
  if (settings.reference_ttl_seconds < 60 ||
      settings.reference_ttl_seconds > 365LL * 24 * 60 * 60)
    return fail("reference lifetime must be between 60 seconds and 365 days");
  if (settings.maximum_visible_references < 1 ||
      settings.maximum_visible_references > 10'000)
    return fail("maximum visible references must be between 1 and 10000");
  return true;
}

bool ShortcutBindingsConflict(const Settings& settings) {
  return settings.shortcut_key_code == settings.delete_shortcut_key_code &&
         settings.shortcut_modifiers == settings.delete_shortcut_modifiers;
}

std::string SettingsJson(const Settings& settings) {
  std::ostringstream out;
  out << "{\"schema\":2,\"shortcut_key_code\":"
      << settings.shortcut_key_code << ",\"shortcut_modifiers\":"
      << settings.shortcut_modifiers << ",\"delete_shortcut_key_code\":"
      << settings.delete_shortcut_key_code
      << ",\"delete_shortcut_modifiers\":"
      << settings.delete_shortcut_modifiers << ",\"maximum_hold_ms\":"
      << settings.maximum_hold_ms << ",\"crop_margin_points\":"
      << settings.crop_margin_points << ",\"mark_hit_radius_points\":"
      << settings.mark_hit_radius_points << ",\"reference_ttl_seconds\":"
      << settings.reference_ttl_seconds
      << ",\"maximum_visible_references\":"
      << settings.maximum_visible_references << "}\n";
  return out.str();
}

std::optional<Settings> ParseSettingsJson(const std::string& json,
                                          std::string* error) {
  if (json.empty() || json.size() > kMaxSettingsBytes || json.front() != '{' ||
      json.find('\0') != std::string::npos) {
    if (error) *error = "malformed settings document";
    return {};
  }
  auto values = ParseFlatObject(json);
  if (!values) {
    if (error) *error = "malformed settings document";
    return {};
  }
  return ParsePublic(*values, false, nullptr, error);
}

SettingsStore::SettingsStore(std::filesystem::path path)
    : path_(std::move(path)), reference_secret_(RandomSecret()) {
  if (!std::filesystem::exists(path_)) {
    std::string error;
    if (Persist(settings_, reference_secret_, &error)) {
      reference_capability_available_ = true;
    } else {
      load_state_ = SettingsLoadState::kInvalid;
      load_error_ = std::move(error);
    }
    return;
  }
  try {
    const auto size = std::filesystem::file_size(path_);
    if (size == 0 || size > kMaxSettingsBytes)
      throw std::runtime_error("settings document size is invalid");
    std::ifstream input(path_, std::ios::binary);
    std::string json(size, '\0');
    if (!input.read(json.data(), static_cast<std::streamsize>(json.size())))
      throw std::runtime_error("settings document could not be read");
    auto values = ParseFlatObject(json);
    if (!values) throw std::runtime_error("settings document syntax is invalid");
    std::string secret;
    if (!String(*values, "reference_secret", &secret) || !SafeSecret(secret))
      throw std::runtime_error("settings capability root is invalid");
    reference_secret_ = std::move(secret);
    reference_capability_available_ = true;
    std::int64_t schema = 0;
    if (!Integer(*values, "schema", &schema) || schema < 1)
      throw std::runtime_error("settings schema is invalid");
    if (schema > 2) {
      load_state_ = SettingsLoadState::kUnsupported;
      load_error_ = "unsupported settings schema; existing file preserved";
      return;
    }
    values->erase("reference_secret");
    std::string error;
    bool migrated_legacy = false;
    auto parsed = ParsePublic(*values, true, &migrated_legacy, &error);
    if (!parsed) {
      load_state_ = error == "settings keys must match schema exactly"
          ? SettingsLoadState::kUnsupported : SettingsLoadState::kInvalid;
      load_error_ = error + "; existing file preserved";
      return;
    }
    settings_ = *parsed;
    if (migrated_legacy) {
      std::string ignored;
      (void)Persist(settings_, reference_secret_, &ignored);
    }
  } catch (const std::exception& exception) {
    load_state_ = SettingsLoadState::kInvalid;
    load_error_ = std::string(exception.what()) + "; existing file preserved";
  }
}

Settings SettingsStore::Get() const {
  std::lock_guard lock(mutex_);
  return settings_;
}

std::string SettingsStore::ReferenceCapability(
    const std::string& reference_id) const {
  std::lock_guard lock(mutex_);
  if (!reference_capability_available_) return {};
  const std::string material = reference_secret_ + ":reference-read:" + reference_id;
  return Sha256(Bytes(material.begin(), material.end()));
}

bool SettingsStore::Update(const Settings& settings, std::string* error) {
  if (!ValidateSettings(settings, error)) return false;
  if (ShortcutBindingsConflict(settings)) {
    if (error) *error = "capture and delete shortcuts conflict";
    return false;
  }
  std::lock_guard update_lock(update_mutex_);
  if (!usable()) {
    if (error) *error = load_error_;
    return false;
  }
  std::string secret;
  {std::lock_guard lock(mutex_);secret=reference_secret_;}
  if (!Persist(settings, secret, error)) return false;
  {std::lock_guard lock(mutex_);settings_=settings;}
  return true;
}

bool SettingsStore::Persist(const Settings& settings,
                            const std::string& secret,
                            std::string* error) {
  int descriptor = -1;
  std::filesystem::path temporary;
  try {
    std::filesystem::create_directories(path_.parent_path());
    std::string pattern = (path_.parent_path() / ".settings-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    descriptor = mkstemp(name.data());
    if (descriptor < 0) throw std::runtime_error("create temporary settings failed");
    temporary = name.data();
    if (fchmod(descriptor, S_IRUSR | S_IWUSR) != 0)
      throw std::runtime_error("settings permissions failed");
    const std::string json = StoredJson(settings, secret);
    std::size_t written = 0;
    while (written < json.size()) {
      const auto count = write(descriptor, json.data() + written,
                               json.size() - written);
      if (count <= 0) throw std::runtime_error("settings write failed");
      written += static_cast<std::size_t>(count);
    }
    if (fsync(descriptor) != 0) throw std::runtime_error("settings fsync failed");
    close(descriptor);
    descriptor = -1;
    if (rename(temporary.c_str(), path_.c_str()) != 0)
      throw std::runtime_error("atomic settings replace failed");
    const int directory = open(path_.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) throw std::runtime_error("settings directory open failed");
    const bool synced = fsync(directory) == 0;
    close(directory);
    if (!synced) throw std::runtime_error("settings directory fsync failed");
    return true;
  } catch (const std::exception& exception) {
    if (descriptor >= 0) close(descriptor);
    if (!temporary.empty()) unlink(temporary.c_str());
    if (error) *error = exception.what();
    return false;
  }
}

}  // namespace seethis::core
