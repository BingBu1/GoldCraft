// SPDX-License-Identifier: GPL-3.0-or-later
#include "resource_update.hpp"
#include "resource_cache.hpp"
#include "resource_download.hpp"
#include "resource_overlay.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace goldcraft::resource_update {
namespace {
using Clock = std::chrono::steady_clock;
using resource_timing::Measure;
constexpr std::size_t ChunkSize = 256 * 1024;
// The pump runs once per host frame while admission is paused. Live samples
// of the earlier 12 ms slice measured one pump every 32-37 ms, so about 60%
// of the admission was spent outside it: 345 pumps and 4.8 s of work took
// 12.3 s. This is a measured host-frame interval, not a claim about how the
// engine paces frames. Use most of that interval; the dialog still redraws.
constexpr auto PumpBudget = std::chrono::milliseconds(30);
constexpr std::size_t InFlight = 32;
class HashPool {
public:
    struct Job {
        // The worker obtains the source itself (plain Win32, no engine):
        // from the engine's duplicated VFS winner, or a content cache object.
        std::unique_ptr<resource_cache::PendingSource> pending;
        std::unique_ptr<resource_cache::SourceFile> source;
        const resource_cache::Cache* cache{};
        std::string name;
        resource_sync::Entry expected{};
        resource_sync::Digest digest{};
        std::uint32_t size{};
        std::uint64_t bytes{};
        bool success{}, reused{}, leased{};
        Profile profile;
        std::atomic<bool> cancelled{}, done{};
    };
    HashPool() {
        for (unsigned i = 0; i < 4; ++i) threads_.emplace_back([this](std::stop_token stop) { Run(stop); });
    }
    ~HashPool() {
        for (auto& thread : threads_) thread.request_stop();
        ready_.notify_all();
        // Join before destroying the queue, cache or mutex.
        threads_.clear();
    }
    std::shared_ptr<Job> Submit(std::unique_ptr<resource_cache::PendingSource> pending,
                                const resource_sync::Entry& expected,
                                std::unique_ptr<resource_cache::SourceFile>& verifiedLease) {
        auto job = std::make_shared<Job>();
        job->pending = std::move(pending);
        job->expected = expected;
        job->size = expected.size;
        std::lock_guard lock(mutex_);
        if (identities_.size() >= 64) return {};
        job->source = std::move(verifiedLease);
        identities_.push_back(job); ready_.notify_one(); return job;
    }
    // Bounded by the manifest: at most one per resource of the live session.
    // A cache object costs milliseconds; an identity about 0.1 ms. Workers
    // take identities first so a burst of wrong files cannot stall the VFS pass.
    std::shared_ptr<Job> SubmitCache(const resource_cache::Cache& cache, const std::string& name,
                                     const resource_sync::Entry& expected) {
        auto job = std::make_shared<Job>();
        job->cache = &cache; job->name = name; job->expected = expected; job->size = expected.size;
        std::lock_guard lock(mutex_);
        objects_.push_back(job); ready_.notify_one(); return job;
    }
    std::uint64_t Completions() { std::lock_guard lock(mutex_); return completions_; }
    // Sleeps the paused admission thread inside its slice instead of
    // returning to the next host frame while four workers sit idle.
    bool Wait(std::uint64_t seen, Clock::time_point deadline) {
        std::unique_lock lock(mutex_);
        return finished_.wait_until(lock, deadline, [&] { return completions_ != seen; });
    }
private:
    void Verify(Job& job, std::span<std::uint8_t> buffer, std::stop_token stop) {
        if (job.cancelled || stop.stop_requested()) return;
        // A rejected lease match can include missing or ambiguous file IDs.
        const bool useDigestCache = !job.source;
        if (job.pending) {
            if (job.source && Measure(job.profile.leaseCheck, [&] { return job.pending->Matches(*job.source); })) {
                if (job.cancelled || stop.stop_requested()) return;
                job.digest = job.expected.sha256;
                job.reused = job.leased = job.success = true;
                return;
            }
            job.source = Measure(job.profile.leaseOpen, [&] { return job.pending->Lease(); });
        } else if (job.cache) {
            job.source = Measure(job.profile.cacheOpen, [&] { return job.cache->Open(job.name, job.expected); });
        }
        if (!job.source || job.cancelled || stop.stop_requested()) return;
        const auto stamp = job.source->Identity();
        if (useDigestCache) {
            std::lock_guard lock(mutex_);
            if (const auto found = digests_.find(stamp); found != digests_.end()) {
                job.digest = found->second;
                job.reused = true;
            }
        }
        if (!job.reused) {
            resource_cache::Hash hash;
            for (std::uint32_t offset = 0; offset < job.size;) {
                if (job.cancelled || stop.stop_requested()) break;
                const auto count = std::min<std::size_t>(buffer.size(), job.size - offset);
                auto span = buffer.first(count);
                if (!Measure(job.profile.readAt, [&] { return job.source->ReadAt(offset, span); })) break;
                Measure(job.profile.hashUpdate, [&] { hash.Update(span); });
                offset += std::uint32_t(count);
                job.bytes += count;
            }
            if (job.bytes == job.size)
                job.digest = Measure(job.profile.hashFinish, [&] { return hash.Finish(); });
        }
        job.success = !job.cancelled && !stop.stop_requested() &&
            (job.reused || job.bytes == job.size) && Measure(job.profile.unchanged, [&] { return job.source->Unchanged(); });
        if (job.success && useDigestCache && !job.reused) {
            std::lock_guard lock(mutex_);
            if (digests_.size() >= 32768) digests_.clear();
            digests_[stamp] = job.digest;
        }
    }
    void Run(std::stop_token stop) {
        std::array<std::uint8_t, ChunkSize> buffer{};
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, stop, [this] { return !identities_.empty() || !objects_.empty(); });
                if (stop.stop_requested()) return;
                auto& queue = identities_.empty() ? objects_ : identities_;
                job = std::move(queue.front()); queue.pop_front();
            }
            try { Verify(*job, buffer, stop); }
            catch (...) { job->success = false; }
            // A proven source keeps its read lease for the final pass.
            job->pending.reset();
            if (!job->success) job->source.reset();
            job->done.store(true, std::memory_order_release);
            { std::lock_guard lock(mutex_); ++completions_; }
            finished_.notify_all();
        }
    }
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::condition_variable finished_;
    std::uint64_t completions_{};
    std::deque<std::shared_ptr<Job>> identities_, objects_;
    std::map<resource_cache::SourceFile::Stamp, resource_sync::Digest> digests_;
    std::vector<std::jthread> threads_;
};
std::string Lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::string UrlPath(const std::string& name) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string value;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '_' || c == '-' || c == '.') value += char(c);
        else { value += '%'; value += hex[c >> 4]; value += hex[c & 15]; }
    }
    return value;
}
}
struct Session::Impl {
    resource_cache::Cache cache;
    resource_download::Worker worker;
    std::vector<Resource> resources;
    resource_overlay::Paths mounted;
    resource_overlay::Snapshot previous;
    std::vector<std::string> changed;
    bool transaction{};
    std::vector<std::size_t> pending;
    struct Active { std::size_t index; std::shared_ptr<resource_download::Job> job; };
    std::vector<Active> active;
    Status status;
    Clock::time_point started{};
    std::string url;
    std::size_t cursor{}, nextPending{};
    HashPool hashes;
    struct Checking { std::size_t index; bool cache; std::shared_ptr<HashPool::Job> job; };
    std::vector<Checking> checking;
    // By resource index: the read lease (writers, renames and deletion denied)
    // under which the first pass proved that VFS winner's SHA.
    std::vector<std::unique_ptr<resource_cache::SourceFile>> leases;
    std::size_t carried{};
    bool remounted{};
    explicit Impl(std::string root) : cache(std::move(root)) {}
    void ResetReader() {
        for (auto& item : checking) item.job->cancelled.store(true, std::memory_order_release);
        checking.clear(); leases.clear();
    }
    bool Rollback() {
        if (!transaction) return true;
        // Readers and jobs are closed by the caller before changing the mount.
        // An admission that never replaced the mount has nothing to restore.
        if (remounted && !resource_overlay::Restore(previous)) return false;
        transaction = remounted = false; previous = {}; return true;
    }
    bool Reset() {
        worker.Cancel(); active.clear(); pending.clear(); mounted.clear(); ResetReader();
        if (!Rollback()) {
            status.phase = Phase::failed;
            status.error = std::string("Cannot restore previous resource overlay: ") + resource_overlay::Error();
            return false;
        }
        resources.clear(); status = {}; cursor = nextPending = carried = 0; url.clear();
        changed.clear(); return true;
    }
    void Fail(std::string reason) {
        ResetReader(); worker.Cancel(); active.clear();
        if (!Rollback()) reason += std::string("; cannot restore previous resource overlay: ") + resource_overlay::Error();
        status.phase = Phase::failed; status.error = std::move(reason);
    }
    void Mismatch(std::size_t index, bool cached) {
        const auto& resource = resources[index];
        if (status.phase == Phase::verifying)
            throw std::runtime_error("Final VFS body differs from manifest: " + resource.name);
        if (cached) { pending.push_back(index); status.queued = pending.size(); ++status.checked; return; }
        changed.push_back(resource.name);
        // Ancestor pinning, the open and the SHA of a content-cache object are
        // ordinary Win32 work: about 2 ms a file when done on this thread.
        checking.push_back({index, true, Measure(status.profile.submitHash,
            [&] { return hashes.SubmitCache(cache, resource.name, resource.expected); })});
    }
    void Checks(Clock::time_point deadline) {
        for (;;) {
            const auto seen = hashes.Completions();
            for (std::size_t i = 0; i < checking.size() && Clock::now() < deadline;) {
                if (!checking[i].job->done.load(std::memory_order_acquire)) { ++i; continue; }
                auto item = std::move(checking[i]); checking.erase(checking.begin() + i);
                const auto& resource = resources[item.index];
                status.hashedBytes += item.job->bytes;
                status.profile.leaseOpen.Merge(item.job->profile.leaseOpen);
                status.profile.leaseCheck.Merge(item.job->profile.leaseCheck);
                status.profile.cacheOpen.Merge(item.job->profile.cacheOpen);
                status.profile.readAt.Merge(item.job->profile.readAt);
                status.profile.hashUpdate.Merge(item.job->profile.hashUpdate);
                status.profile.hashFinish.Merge(item.job->profile.hashFinish);
                status.profile.unchanged.Merge(item.job->profile.unchanged);
                if (item.job->reused) ++status.reused;
                if (!item.job->success || item.job->digest != resource.expected.sha256) {
                    Mismatch(item.index, item.cache); continue;
                }
                if (item.cache) {
                    mounted[resource.name] = cache.Path(resource.name, resource.expected); ++status.cached;
                } else {
                    if (status.phase == Phase::checking) {
                        // Keep a binding that was supplied by the previous verified overlay.
                        const auto found = previous.paths.find(Lower(resource.name));
                        if (found != previous.paths.end()) { mounted[resource.name] = found->second; ++carried; }
                    }
                    if (item.job->leased) ++status.leased;
                    leases[item.index] = std::move(item.job->source);
                }
                ++status.checked;
            }
            while (cursor < resources.size() && checking.size() < InFlight && Clock::now() < deadline) {
                const auto index = cursor;
                const auto& resource = resources[index]; status.current = resource.name;
                auto reader = Measure(status.profile.openReader, [&] { return resource_overlay::OpenReader(resource.name.c_str()); });
                auto source = Measure(status.profile.backgroundSource, [&] {
                    return reader && reader->Size() == resource.expected.size ? reader->DeferredSource() : nullptr;
                });
                Measure(status.profile.closeReader, [&] { reader.reset(); }); // Engine Close stays on the main thread.
                if (!source) { ++cursor; Mismatch(index, false); continue; }
                auto job = Measure(status.profile.submitHash, [&] {
                    return hashes.Submit(std::move(source), resource.expected, leases[index]);
                });
                if (!job) break; // A cancelled generation may still be draining.
                checking.push_back({index, false, std::move(job)}); ++cursor;
            }
            if (Clock::now() >= deadline || (cursor == resources.size() && checking.empty())) return;
            // Drain finished identities in this pump; do not impose a
            // files-per-frame cap on metadata-only verification.
            if (std::any_of(checking.begin(), checking.end(),
                [](const Checking& item) { return item.job->done.load(std::memory_order_acquire); })) continue;
            // A refused submit with nothing of ours in flight retries next frame.
            if (checking.empty() || !hashes.Wait(seen, deadline)) return;
        }
    }
    void Downloads() {
        for (std::size_t i = 0; i < active.size();) {
            auto& item = active[i];
            if (!item.job->done.load(std::memory_order_acquire)) { ++i; continue; }
            const auto& resource = resources[item.index];
            if (!item.job->success)
                throw std::runtime_error(resource.name + ": " + item.job->error);
            if (!item.job->cacheStored)
                throw std::runtime_error(resource.name + ": verified cache publication was not completed");
            status.profile.cacheStore.Add(item.job->cacheStoreMs);
            mounted[resource.name] = cache.Path(resource.name, resource.expected);
            ++status.downloaded; active.erase(active.begin() + i);
        }
        std::size_t reserved{};
        for (const auto& item : active) reserved += item.job->expected.size;
        while (active.size() < resource_download::Concurrency && nextPending < pending.size()) {
            const auto index = pending[nextPending];
            const auto& resource = resources[index];
            if (reserved + resource.expected.size > resource_download::MemoryBudget) break;
            auto job = std::make_shared<resource_download::Job>();
            job->url = url + UrlPath(resource.name); job->expected = resource.expected;
            job->cacheRoot = cache.Root(); job->cacheName = resource.name;
            if (!worker.Submit(job)) break; // Cancelled work may still be draining.
            active.push_back({index, std::move(job)}); ++nextPending;
            reserved += resource.expected.size;
        }
        if (!active.empty()) status.current = resources[active.front().index].name;
    }
    void Pump() {
        if (status.phase == Phase::idle || status.phase == Phase::failed || status.phase == Phase::ready) return;
        const auto begin = Clock::now();
        // Admission is paused here. VFS resolution must stay on this thread;
        // file bodies, cache objects and SHA remain on the pool.
        const auto deadline = begin + PumpBudget;
        struct Timing {
            Status& status; Clock::time_point begin, started;
            ~Timing() {
                const auto now = Clock::now();
                ++status.pumps;
                status.elapsedMs = std::chrono::duration<double, std::milli>(now - started).count();
                status.pumpMaxMs = std::max(status.pumpMaxMs,
                    std::chrono::duration<double, std::milli>(now - begin).count());
                status.pumpWorkMs += std::chrono::duration<double, std::milli>(now - begin).count();
            }
        } timing{status, begin, started};
        try {
            Downloads();
            if (status.phase == Phase::checking || status.phase == Phase::verifying) {
                Checks(deadline);
                if (cursor != resources.size() || !checking.empty()) return;
                if (status.phase == Phase::verifying) {
                    leases.clear();
                    status.phase = Phase::ready; status.current.clear(); return;
                }
                status.phase = Phase::downloading;
                // A transfer can last minutes. Do not deny other writers for
                // that long; the final pass then resolves every name again.
                if (!pending.empty()) for (auto& lease : leases) lease.reset();
            }
            Downloads();
            if (nextPending != pending.size() || !active.empty()) return;
            // Every binding was inherited from the mount that is still active,
            // and none was dropped: the first pass already read the final view.
            const bool retained = carried == mounted.size() && mounted.size() == previous.paths.size();
            if (!retained) Measure(status.profile.mount, [&] {
                remounted = true;
                if (mounted.empty()) resource_overlay::Clear();
                else if (!resource_overlay::Begin(cache.Root(), mounted))
                    throw std::runtime_error(resource_overlay::Error());
            });
            // Prove the actual consumer path after mounting, not only a file
            // written under cstrike_downloads that an older GAME/PAK can hide.
            // A read lease pins bytes, not the winner of a VFS name lookup:
            // another search path can acquire a higher-priority same-name file.
            // Resolve every name again. The worker can reuse a retained lease
            // only when the newly resolved handle proves the same file and range.
            status.phase = Phase::verifying; cursor = 0; status.checked = 0;
        } catch (const std::exception& failure) { Fail(failure.what()); }
    }
};
Session::Session(std::string root) : impl_(std::make_unique<Impl>(std::move(root))) {}
Session::~Session() { impl_->Reset(); }
bool Session::Reset() { return impl_->Reset(); }
bool Session::Commit() {
    if (impl_->status.phase != Phase::ready || !impl_->transaction) return false;
    impl_->transaction = impl_->remounted = false; impl_->previous = {}; return true;
}
const Status& Session::GetStatus() const { return impl_->status; }
DownloadProgress Session::GetDownloadProgress() const noexcept {
    if (impl_->active.empty()) return {};
    const auto& item = impl_->active.front();
    return {item.index, item.job->transferred.load(std::memory_order_relaxed), item.job->expected.size};
}
const std::vector<Resource>& Session::Resources() const { return impl_->resources; }
const std::vector<std::string>& Session::Changed() const { return impl_->changed; }
bool Session::Begin(std::uint64_t generation, std::string url, std::vector<Resource> incoming) {
    if (!impl_->Reset()) return false;
    try {
        impl_->previous = resource_overlay::Capture(); impl_->transaction = true;
        if (!generation || incoming.size() > 16384) throw std::runtime_error("Invalid resource sync manifest");
        if (url.size() < 8 || url.size() >= 128 || (!url.starts_with("http://") && !url.starts_with("https://")) ||
            url.find_first_of("?#\\\r\n\t") != std::string::npos)
            throw std::runtime_error("Server did not provide a valid HTTP resource URL");
        if (url.back() != '/') url += '/';
        std::map<std::string, resource_sync::Entry> unique;
        for (auto& resource : incoming) {
            if (!resource.expected.required) continue;
            if (!resource_overlay::SafeName(resource.name) || !resource.expected.size ||
                resource.expected.size > resource_sync::max_file_size)
                throw std::runtime_error("Invalid resource path or size: " + resource.name);
            const auto [where, inserted] = unique.emplace(Lower(resource.name), resource.expected);
            if (!inserted && where->second != resource.expected)
                throw std::runtime_error("Conflicting resource identities: " + resource.name);
            if (inserted) impl_->resources.push_back(std::move(resource));
        }
        for (const auto& [name, path] : impl_->previous.paths)
            if (!unique.contains(name)) impl_->changed.push_back(name);
        impl_->leases.resize(impl_->resources.size());
        impl_->url = std::move(url); impl_->status.total = impl_->resources.size();
        impl_->started = Clock::now();
        impl_->status.phase = Phase::checking;
        return true;
    } catch (const std::exception& failure) { impl_->Fail(failure.what()); return false; }
}
void Session::Pump() { impl_->Pump(); }
}
