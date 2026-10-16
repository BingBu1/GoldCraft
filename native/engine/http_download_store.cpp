#include "http_download_store.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace goldcraft::http {
struct Store::Impl {
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::unordered_map<std::string, std::size_t> files;
    std::unordered_map<std::string, std::shared_ptr<Load>> cache;
    std::deque<std::shared_ptr<Load>> pending;
    std::shared_ptr<std::atomic<std::size_t>> used = std::make_shared<std::atomic<std::size_t>>(0);
    std::size_t budget;
    std::uint64_t epoch = 0, tick = 0, hits = 0, loads = 0, failures = 0;
    explicit Impl(std::size_t b) : budget(b) {}
};
Store::Store(std::size_t budget) : impl_(new Impl(budget)) {}
Store::~Store() { publish({}); }
void Store::publish(const std::vector<Resource>& resources) {
    auto& c = *impl_;
    std::lock_guard<std::mutex> lock(c.mutex);
    ++c.epoch;
    for (auto& row : c.cache) row.second->phase = Load::Phase::failed;
    c.cache.clear(); c.pending.clear(); c.files.clear();
    for (auto& resource : resources) {
        if (c.files.size() == 16384) break;
        if (resource_name(resource.name) && resource.size && resource.size <= max_file_size)
            c.files.emplace(resource.name, resource.size);
    }
    c.ready.notify_all();
}
Body Store::request(const std::string& name) {
    auto& c = *impl_;
    std::unique_lock<std::mutex> lock(c.mutex);
    const auto resource = c.files.find(name);
    if (resource == c.files.end()) return {};
    auto found = c.cache.find(name);
    if (found == c.cache.end()) {
        const auto size = resource->second;
        if (size > c.budget) return {};
        while (c.used->load() + size > c.budget || c.cache.size() >= 256) {
            auto victim = c.cache.end();
            for (auto it = c.cache.begin(); it != c.cache.end(); ++it) {
                if (it->second.use_count() == 1 && it->second->bytes.use_count() == 1 &&
                    (victim == c.cache.end() || it->second->touched < victim->second->touched)) victim = it;
            }
            if (victim == c.cache.end()) { ++c.failures; return {}; }
            c.cache.erase(victim);
        }
        auto load = std::make_shared<Load>();
        load->name = name; load->size = size; load->epoch = c.epoch;
        c.used->fetch_add(size);
        load->bytes = std::shared_ptr<Bytes>(new Bytes, [used = c.used, size](Bytes* bytes) {
            delete bytes; used->fetch_sub(size);
        });
        c.pending.push_back(load);
        found = c.cache.emplace(name, std::move(load)).first;
        ++c.loads;
    } else ++c.hits;
    auto load = found->second;
    load->touched = ++c.tick;
    if (!c.ready.wait_for(lock, std::chrono::seconds(10), [&] { return load->phase != Load::Phase::pending; })) {
        ++c.failures; return {};
    }
    return load->phase == Load::Phase::ready ? load->bytes : Body{};
}
std::shared_ptr<Load> Store::take() {
    auto& c = *impl_;
    std::lock_guard<std::mutex> lock(c.mutex);
    if (c.pending.empty()) return {};
    auto load = c.pending.front(); c.pending.pop_front();
    return load;
}
void Store::finish(const std::shared_ptr<Load>& load, bool success) {
    auto& c = *impl_;
    std::lock_guard<std::mutex> lock(c.mutex);
    success = success && load->epoch == c.epoch && load->bytes->size() == load->size;
    load->phase = success ? Load::Phase::ready : Load::Phase::failed;
    if (!success) ++c.failures;
    c.ready.notify_all();
}
StoreStats Store::stats() const {
    auto& c = *impl_;
    std::lock_guard<std::mutex> lock(c.mutex);
    return {c.files.size(), c.used->load(), c.hits, c.loads, c.failures};
}
} // namespace goldcraft::http
