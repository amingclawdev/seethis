#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace seethis::service {
// One bounded child per read, connected explicitly by the local plugin. Never
// accepts a log path or shell command from the browser.
std::string ReadAgentRuns(std::string_view reference, std::string_view run = {},
                         const std::filesystem::path& binding = {});
}
