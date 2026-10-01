#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/reference.h"
#include "core/settings.h"

namespace seethis::platform {
struct PermissionReadinessState;
}

namespace seethis::service {

class ReferenceServer {
 public:
  static constexpr std::size_t kMaximumClipboardTextBytes = 2048;

  ReferenceServer(core::ReferenceStore& references,
                  core::SettingsStore& settings,
                  std::filesystem::path settings_web_root,
                  std::shared_ptr<platform::PermissionReadinessState> readiness);
  ~ReferenceServer();

  ReferenceServer(const ReferenceServer&) = delete;
  ReferenceServer& operator=(const ReferenceServer&) = delete;

  [[nodiscard]] bool Start(std::string* error = nullptr);
  void Stop();

  [[nodiscard]] std::uint16_t port() const { return port_; }
  [[nodiscard]] std::string base_url() const;
  [[nodiscard]] std::string settings_url() const;
  [[nodiscard]] const std::string& settings_capability_for_testing() const {
    return settings_capability_;
  }
  [[nodiscard]] std::string ClipboardText(
      const core::StoredReference& reference) const;
  [[nodiscard]] std::string ClipboardText(std::string_view id) const;
  [[nodiscard]] std::string ViewerUrl(std::string_view id) const;

 private:
  void Serve();
  void Handle(int client);

  core::ReferenceStore& references_;
  core::SettingsStore& settings_;
  std::filesystem::path settings_web_root_;
  std::shared_ptr<platform::PermissionReadinessState> readiness_;
  std::string settings_capability_;
  std::atomic<bool> stopping_{false};
  int listener_ = -1;
  std::uint16_t port_ = 0;
  std::thread thread_;
  std::mutex clients_mutex_;
  std::condition_variable clients_ready_;
  std::deque<int> clients_;
  std::vector<std::thread> client_workers_;
};

}  // namespace seethis::service
