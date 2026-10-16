#pragma once
#include "http_download_server.h"
#include <cstddef>

namespace goldcraft::http {
struct Resource { std::string name; std::size_t size = 0; };
struct Load {
    std::string name;
    std::size_t size = 0;
    // Only the main-thread loader may write bytes, until finish().
    std::shared_ptr<Bytes> bytes;
private:
    friend class Store;
    enum class Phase { pending, ready, failed } phase = Phase::pending;
    std::uint64_t epoch = 0, touched = 0;
};
struct StoreStats { std::size_t files = 0, reserved = 0; std::uint64_t hits = 0, loads = 0, failures = 0; };
class Store {
public:
    explicit Store(std::size_t budget = 64 * 1024 * 1024);
    ~Store();
    // Replaces the allowlist atomically and wakes/cancels old pending requests.
    void publish(const std::vector<Resource>& resources);
    Body request(const std::string& name); // worker only; max wait 10 seconds
    std::shared_ptr<Load> take(); // main thread
    void finish(const std::shared_ptr<Load>& load, bool success); // main thread
    StoreStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace goldcraft::http
