// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace parallel_download {
struct Job {
    std::string name, url;
    std::filesystem::path temporary, destination;
    std::atomic<bool> cancelled{}, done{}, active{};
    std::atomic<std::uint64_t> received{}, length{};
    bool success{}; // Read only after done.acquire.
    bool submitted{}; // Main thread only.
    unsigned status{}, error{}, attempts{};
    ~Job();
};
struct Limits {
    unsigned workers{8}, timeout_ms{8000}, total_ms{180000}, retries{1};
    std::uint64_t max_bytes{512ull * 1024 * 1024};
};
bool ValidPath(const std::string& path);
class Pool {
public:
    explicit Pool(Limits limits = {});
    ~Pool();
    bool Submit(const std::shared_ptr<Job>& job);
    void Cancel();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
