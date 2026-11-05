// SPDX-License-Identifier: GPL-3.0-or-later
#include "resource_update.hpp"
#include "resource_cache.hpp"
#include "resource_download.hpp"
#include "resource_overlay.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <stdexcept>

namespace goldcraft::resource_update {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t ChunkSize = 256 * 1024;
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
    std::string url;
    std::size_t cursor{}, nextPending{};
    std::uint32_t offset{};
    bool cacheRead{};
    std::unique_ptr<resource_overlay::Reader> reader;
    std::unique_ptr<resource_cache::Hash> hash;
    std::array<std::uint8_t, ChunkSize> bytes{};
    explicit Impl(std::string root) : cache(std::move(root)) {}
    void ResetReader() { reader.reset(); hash.reset(); offset = 0; cacheRead = false; }
    bool Rollback() {
        if (!transaction) return true;
        // Readers and jobs are closed by the caller before changing the mount.
        if (!resource_overlay::Restore(previous)) return false;
        transaction = false; previous = {}; return true;
    }
    bool Reset() {
        worker.Cancel(); active.clear(); pending.clear(); mounted.clear(); ResetReader();
        if (!Rollback()) {
            status.phase = Phase::failed;
            status.error = std::string("Cannot restore previous resource overlay: ") + resource_overlay::Error();
            return false;
        }
        resources.clear(); status = {}; cursor = nextPending = 0; url.clear();
        changed.clear(); return true;
    }
    void Fail(std::string reason) {
        ResetReader(); worker.Cancel(); active.clear();
        if (!Rollback()) reason += std::string("; cannot restore previous resource overlay: ") + resource_overlay::Error();
        status.phase = Phase::failed; status.error = std::move(reason);
    }
    void Next() { ResetReader(); ++cursor; status.checked = cursor; }
    void Mismatch() {
        reader.reset(); hash.reset(); offset = 0;
        if (status.phase == Phase::verifying)
            throw std::runtime_error("Final VFS body differs from manifest: " + resources[cursor].name);
        if (cacheRead) { pending.push_back(cursor); Next(); }
        else {
            changed.push_back(resources[cursor].name);
            cacheRead = true; hash = std::make_unique<resource_cache::Hash>();
        }
    }
    void CheckChunk() {
        const auto& resource = resources[cursor];
        status.current = resource.name;
        if (!hash) {
            reader = resource_overlay::OpenReader(resource.name.c_str());
            if (!reader || reader->Size() != resource.expected.size) { Mismatch(); return; }
            hash = std::make_unique<resource_cache::Hash>();
        }
        const auto count = std::min<std::size_t>(bytes.size(), resource.expected.size - offset);
        const auto buffer = std::span<std::uint8_t>(bytes.data(), count);
        const bool read = cacheRead ? cache.Read(resource.name, resource.expected, buffer, offset) :
            reader && reader->ReadAt(offset, buffer) == int(count);
        if (!read) { Mismatch(); return; }
        hash->Update(buffer); offset += std::uint32_t(count);
        if (offset != resource.expected.size) return;
        if (hash->Finish() != resource.expected.sha256) { Mismatch(); return; }
        if (cacheRead) {
            mounted[resource.name] = cache.Path(resource.name, resource.expected); ++status.cached;
        } else if (status.phase == Phase::checking) {
            // A matching file can already be supplied by the previous server's
            // verified overlay. Keep that binding in the next generation.
            const auto found = previous.paths.find(Lower(resource.name));
            if (found != previous.paths.end()) mounted[resource.name] = found->second;
        }
        Next();
    }
    void Downloads() {
        for (std::size_t i = 0; i < active.size();) {
            auto& item = active[i];
            if (!item.job->done.load(std::memory_order_acquire)) { ++i; continue; }
            const auto& resource = resources[item.index];
            if (!item.job->success)
                throw std::runtime_error(resource.name + ": " + item.job->error);
            if (!cache.Store(resource.name, resource.expected, item.job->bytes))
                throw std::runtime_error(resource.name + ": " + cache.Error());
            mounted[resource.name] = cache.Path(resource.name, resource.expected);
            ++status.downloaded; active.erase(active.begin() + i);
        }
        while (active.size() < 2 && nextPending < pending.size()) {
            const auto index = pending[nextPending];
            const auto& resource = resources[index];
            auto job = std::make_shared<resource_download::Job>();
            job->url = url + UrlPath(resource.name); job->expected = resource.expected;
            if (!worker.Submit(job)) break; // Cancelled work may still be draining.
            active.push_back({index, std::move(job)}); ++nextPending;
        }
        if (!active.empty()) status.current = resources[active.front().index].name;
    }
    void Pump() {
        if (status.phase == Phase::idle || status.phase == Phase::failed || status.phase == Phase::ready) return;
        const auto deadline = Clock::now() + std::chrono::milliseconds(3);
        try {
            Downloads();
            if (status.phase == Phase::checking || status.phase == Phase::verifying) {
                std::size_t chunks{};
                while (cursor < resources.size() && chunks++ < 8 && Clock::now() < deadline) CheckChunk();
                if (cursor != resources.size()) return;
                if (status.phase == Phase::verifying) {
                    status.phase = Phase::ready; status.current.clear(); return;
                }
                status.phase = Phase::downloading;
            }
            Downloads();
            if (nextPending != pending.size() || !active.empty()) return;
            if (mounted.empty()) resource_overlay::Clear();
            else if (!resource_overlay::Begin(cache.Root(), mounted))
                throw std::runtime_error(resource_overlay::Error());
            // Prove the actual consumer path after mounting, not only a file
            // written under cstrike_downloads that an older GAME/PAK can hide.
            status.phase = Phase::verifying; cursor = 0; status.checked = 0;
        } catch (const std::exception& failure) { Fail(failure.what()); }
    }
};
Session::Session(std::string root) : impl_(std::make_unique<Impl>(std::move(root))) {}
Session::~Session() { impl_->Reset(); }
bool Session::Reset() { return impl_->Reset(); }
bool Session::Commit() {
    if (impl_->status.phase != Phase::ready || !impl_->transaction) return false;
    impl_->transaction = false; impl_->previous = {}; return true;
}
const Status& Session::GetStatus() const { return impl_->status; }
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
        impl_->url = std::move(url); impl_->status.total = impl_->resources.size();
        impl_->status.phase = Phase::checking;
        return true;
    } catch (const std::exception& failure) { impl_->Fail(failure.what()); return false; }
}
void Session::Pump() { impl_->Pump(); }
}
