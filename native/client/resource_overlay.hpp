// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct metahook_api_s;
struct mh_interface_s;
class IFileSystem;
namespace goldcraft::resource_overlay {
using Paths = std::map<std::string, std::string>;
struct Snapshot { std::string root; Paths paths; };
// Only the exact reviewed IFileSystem009 implementation is supported. Install
// does not advertise protocol support; the caller owns capability negotiation.
bool Install(metahook_api_s* api, mh_interface_s* interfaces);
// Main-thread connection boundary. Values are immutable files already verified
// against this server's SHA manifest under root/objects/<sha256>/<logical-key>.
bool Begin(const std::string& root, const Paths& verifiedPaths);
Snapshot Capture();
// Restore before consumers recreate their old resources after a cancelled
// update. A false result is fatal to that admission, never a successful abort.
bool Restore(const Snapshot& snapshot);
void Clear();
void Shutdown();
bool Read(const char* name, std::vector<std::uint8_t>& bytes);
class Reader {
public:
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    std::uint32_t Size() const { return size_; }
    // Returns bytes read, or -1 on invalid range/read error. Each call seeks
    // explicitly because multiple original PAK handles share a FILE position.
    int ReadAt(std::uint32_t offset, std::span<std::uint8_t> output);
private:
    friend std::unique_ptr<Reader> OpenReader(const char*);
    Reader(IFileSystem* filesystem, void* handle, std::uint32_t size)
        : filesystem_(filesystem), handle_(handle), size_(size) {}
    IFileSystem* filesystem_{};
    void* handle_{};
    std::uint32_t size_{};
};
std::unique_ptr<Reader> OpenReader(const char* name);
bool SafeName(const std::string& name);
const char* Error();
}
