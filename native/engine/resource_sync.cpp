// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "precache.h"
#include "resource_sync.h"
#include "resource_sync_hash.hpp"
#include "http_download_server.h"
#include "goldcraft/resource_stamp.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {
using goldcraft::resource_sync::Entry;
using goldcraft::filesystem::ResourceStamp;
using goldcraft::filesystem::ResourceStampAPI;
struct Cached { ResourceStamp identity; Entry entry; };
std::unordered_map<std::string, Cached> digest_cache;
std::vector<Entry> entries;
std::uint64_t generation{}, next_generation{};
bool ready{};

const ResourceStampAPI* Metadata() {
    if (!g_FileSystemFactory) return nullptr;
    const auto* api = reinterpret_cast<const ResourceStampAPI*>(
        g_FileSystemFactory(goldcraft::filesystem::ResourceStampVersion, nullptr));
    return api && api->size == sizeof(*api) && api->query && api->opened ? api : nullptr;
}
bool Stamp(const ResourceStampAPI* api, const char* name, FileHandle_t file, ResourceStamp& value) {
    return api && (file ? api->opened(g_pFileSystem, file, &value, sizeof(value)) :
                        api->query(g_pFileSystem, name, &value, sizeof(value))) != 0;
}
struct Opened {
    FileHandle_t value{};
    ~Opened() { if (value) FS_Close(value); }
};
bool Ordinary(const resource_t& resource) {
    return resource.nDownloadSize > 0 && !(resource.ucFlags & RES_CUSTOM) &&
        resource.szFileName[0] != '*' && resource.szFileName[0] != '!' &&
        (resource.type == t_sound || resource.type == t_model ||
         resource.type == t_generic || resource.type == t_eventscript);
}
} // namespace

bool GC_ResourceSyncBuild(std::string& error) {
    // User-requested host update stage, once at SV_CreateResourceList. It never
    // changes the original consistency digests or already-loaded model CRCs.
    ready = false; generation = 0; entries.clear(); error.clear();
    if (!GC_ResourceSyncEnabled()) {
        error = "resource sync v4 is disabled in this build";
        return false;
    }
    const auto begin = std::chrono::steady_clock::now();
    const auto* metadata = Metadata();
    std::vector<Entry> candidate(gc_precache.resources.size());
    std::unordered_map<std::string, Entry> batch;
    std::array<unsigned char, 256 * 1024> buffer{};
    std::size_t hits = 0, reads = 0, read_bytes = 0, ordinal = 0;
    for (const auto& resource : gc_precache.resources) {
        auto& entry = candidate[ordinal++];
        if (!Ordinary(resource)) continue;
        const std::string name = std::string(resource.type == t_sound ? "sound/" : "") + resource.szFileName;
        // Keep the existing ordinary-download allowlist: an unrelated legacy
        // generic (for example a config) does not acquire write permission.
        if (!goldcraft::http::resource_name(name)) continue;
        if (resource.nDownloadSize > int(goldcraft::resource_sync::max_file_size)) {
            error = "resource sync file exceeds HTTP limit: " + name; return false;
        }
        if (const auto known = batch.find(name); known != batch.end()) {
            if (known->second.size != unsigned(resource.nDownloadSize)) {
                error = "conflicting resource sizes: " + name; return false;
            }
            entry = known->second; continue;
        }
        ResourceStamp identity{};
        const bool stamped = Stamp(metadata, name.c_str(), nullptr, identity);
        const auto cached = digest_cache.find(name);
        if (stamped && cached != digest_cache.end() && cached->second.identity == identity &&
            cached->second.entry.size == unsigned(resource.nDownloadSize)) {
            entry = cached->second.entry; batch.emplace(name, entry); ++hits; continue;
        }
        Opened file{FS_Open(name.c_str(), "rb")};
        if (!file.value || FS_Size(file.value) != unsigned(resource.nDownloadSize)) {
            error = "resource changed before sync preparation: " + name; return false;
        }
        ResourceStamp before{}, after{}, current{};
        const bool have_before = Stamp(metadata, name.c_str(), file.value, before);
        goldcraft::resource_sync::Hasher hash;
        std::size_t offset = 0;
        while (offset < unsigned(resource.nDownloadSize)) {
            const auto count = (std::min)(buffer.size(), std::size_t(resource.nDownloadSize) - offset);
            // Other VFS operations can seek a shared PAK stream. Every chunk
            // names its own member-relative offset and stays on this thread.
            FS_Seek(file.value, int(offset), FILESYSTEM_SEEK_HEAD);
            if (FS_Read(buffer.data(), int(count), 1, file.value) != int(count) ||
                !FS_IsOk(file.value) || !hash.Update(buffer.data(), count)) {
                error = "resource sync read/hash failed: " + name; return false;
            }
            offset += count;
        }
        entry.required = true; entry.size = unsigned(resource.nDownloadSize);
        if (!hash.Finish(entry.sha256) || FS_Size(file.value) != entry.size) {
            error = "resource sync hash/size failed: " + name; return false;
        }
        const bool have_after = Stamp(metadata, name.c_str(), file.value, after);
        const bool have_current = Stamp(metadata, name.c_str(), nullptr, current);
        if ((have_before && have_after && before != after) ||
            (have_after && have_current && after != current)) {
            error = "resource changed during sync preparation: " + name; return false;
        }
        if (have_before && have_after && have_current && before == after && after == current) {
            // Bounded host cache, no retained resource bodies or engine pointers.
            if (digest_cache.size() >= 16384 && !digest_cache.contains(name))
                digest_cache.erase(digest_cache.begin());
            digest_cache.insert_or_assign(name, Cached{after, entry});
        } else digest_cache.erase(name);
        batch.emplace(name, entry); ++reads; read_bytes += offset;
    }
    entries = std::move(candidate);
    generation = ++next_generation;
    if (!generation) generation = ++next_generation;
    ready = true;
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    Con_Printf("GoldCraft resource sync: generation=%llu files=%u cached=%u reads=%u bytes=%llu time=%.3fms\n",
        static_cast<unsigned long long>(generation), unsigned(batch.size()), unsigned(hits), unsigned(reads),
        static_cast<unsigned long long>(read_bytes), elapsed);
    return true;
}
void GC_ResourceSyncReset() { entries.clear(); generation = 0; ready = false; }
bool GC_ResourceSyncReady() { return GC_ResourceSyncEnabled() && ready && entries.size() == gc_precache.resources.size(); }
std::uint64_t GC_ResourceSyncGeneration() { return GC_ResourceSyncReady() ? generation : 0; }
const Entry* GC_ResourceSyncEntry(std::size_t ordinal) {
    return GC_ResourceSyncReady() && ordinal < entries.size() ? &entries[ordinal] : nullptr;
}
bool GC_ResourceSyncClient(const client_t* client) {
    return GC_ResourceSyncEnabled() && GC_PrecacheClient(client) && !std::strcmp(
        Info_ValueForKey(client->userinfo, goldcraft::resource_sync::capability),
        goldcraft::resource_sync::capability_value);
}
