// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "goldcraft/resource_sync.hpp"
#include "resource_timing.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace goldcraft::resource_update {
struct Resource { std::string name; resource_sync::Entry expected; };
enum class Phase { idle, checking, downloading, verifying, ready, failed };
struct Profile {
    resource_timing::Counter openReader, backgroundSource, closeReader, cacheOpen, submitHash;
    resource_timing::Counter readAt, hashUpdate, hashFinish, unchanged, cacheStore, mount;
    resource_timing::Counter leaseOpen, leaseCheck;
};
struct Status {
    Phase phase{};
    std::size_t checked{}, cached{}, downloaded{}, total{};
    std::string current, error;
    std::uint64_t hashedBytes{}, pumps{};
    double elapsedMs{}, pumpMaxMs{};
    std::size_t queued{};
    std::size_t reused{};
    // Final VFS matches that reused their first-pass OS read lease.
    std::size_t leased{};
    double pumpWorkMs{};
    Profile profile;
};
struct DownloadProgress {
    std::size_t resourceIndex{};
    std::uint32_t received{}, size{};
    bool operator==(const DownloadProgress&) const = default;
};
// Main-thread gate before the engine begins resource precaching. Network jobs
// own value copies only; Reset cancels a connection without borrowing any
// resource_t/model_t, or continuing an obsolete connection.
class Session {
public:
    explicit Session(std::string cacheRoot);
    ~Session();
    bool Begin(std::uint64_t generation, std::string baseUrl, std::vector<Resource> resources);
    void Pump();
    // Reset cancels uncommitted work and restores its prior VFS generation.
    // Commit follows native/cache-owner retirement and consumer refresh. Once
    // committed, the caller owns removal of the active overlay at disconnect.
    bool Reset();
    bool Commit();
    const Status& GetStatus() const;
    // The oldest in-flight HTTP file. A zero size means there is no transfer.
    DownloadProgress GetDownloadProgress() const noexcept;
    const std::vector<Resource>& Resources() const;
    const std::vector<std::string>& Changed() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
