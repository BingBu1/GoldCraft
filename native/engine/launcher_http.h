#pragma once
#include "http_download_server.h"
#include <filesystem>
#include <mutex>

namespace goldcraft::http {
// Separate from the map precache allowlist. No game/PAK filesystem access.
bool launcher_name(const std::string& name);
class LauncherFiles {
public:
    bool configure(const std::filesystem::path& root);
    void publish_status(bool online, const std::string& name, const std::string& map,
                        int players, int capacity, const std::string& address);
    Body request(const std::string& name) const;
private:
    std::filesystem::path root_;
    mutable std::mutex mutex_;
    Body status_;
};
} // namespace goldcraft::http
