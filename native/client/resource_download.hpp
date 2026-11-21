// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "goldcraft/resource_sync.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace goldcraft::resource_download {
inline constexpr unsigned Concurrency = 8;
inline constexpr std::size_t MemoryBudget = 128u * 1024u * 1024u;
struct Job {
    std::string url;
    std::string cacheRoot, cacheName, storedPath;
    double cacheStoreMs{};
    bool cacheStored{};
    resource_sync::Entry expected;
    std::vector<std::uint8_t> bytes;
    std::atomic<std::uint32_t> transferred{};
    std::atomic<bool> cancelled{}, done{}, submitted{};
    bool success{}; // Published by done(release); consumed after done(acquire).
    std::string error;
    // Telemetry only; completion/bytes remain fenced by done(release/acquire).
    std::atomic<unsigned> attempts{};
    std::atomic<bool> retrying{};
};
// The worker owns network IO only. Filesystem mounting, model invalidation and
// continuing the engine downloader remain on the client's main thread.
class Worker {
public:
    Worker();
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    bool Submit(std::shared_ptr<Job> job);
    void Cancel();
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
