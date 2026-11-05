// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include "resource_cache.hpp"
#include "resource_overlay.hpp"
#include <array>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <vector>

namespace goldcraft::resource_cache {
namespace {
void Check(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}
std::string Absolute(const std::string& input) {
    Check(input.size() >= 3 && input[1] == ':' && (input[2] == '/' || input[2] == '\\'),
          "Resource cache requires an absolute local directory");
    std::array<char, MAX_PATH> path{};
    const DWORD count = GetFullPathNameA(input.c_str(), DWORD(path.size()), path.data(), nullptr);
    Check(count && count < path.size(), "Resource cache path exceeds host filesystem limit");
    std::string value(path.data());
    while (value.size() > 3 && (value.back() == '/' || value.back() == '\\')) value.pop_back();
    return value;
}
bool Plain(const std::string& path, bool directory) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           bool(attributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
}
struct Parents {
    std::vector<HANDLE> handles;
    ~Parents() { for (auto handle : handles) CloseHandle(handle); }
    bool Lock(const std::string& path, bool create) {
    // Deny rename/delete of each opened ancestor while publishing a child.
    // This also keeps a directory from being exchanged for a junction between
    // checking attributes and creating or replacing the file below it.
    const auto pin = [&](const std::string& directory) {
        const auto handle = CreateFileA(directory.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle); return false;
        }
        handles.push_back(handle); return true;
    };
    if (!pin(path.substr(0, 3))) return false;
    for (std::size_t i = 3; i < path.size(); ++i) {
        if (path[i] != '/' && path[i] != '\\') continue;
        const auto directory = path.substr(0, i);
        if (!Plain(directory, true)) {
            if (!create || !CreateDirectoryA(directory.c_str(), nullptr) || !Plain(directory, true)) return false;
        }
        if (!pin(directory)) return false;
    }
    return true;
    }
};
struct File {
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Valid() const { return handle != INVALID_HANDLE_VALUE; }
};
bool Regular(HANDLE file, std::uint32_t size) {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        !info.nFileSizeHigh && info.nFileSizeLow == size;
}
std::atomic<unsigned long> serial{};
}

struct Hash::Impl {
    BCRYPT_HASH_HANDLE handle{};
    bool finished{};
    Impl() { Check(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &handle, nullptr, 0, nullptr, 0, 0) >= 0,
                   "Cannot initialize resource SHA-256"); }
    ~Impl() { if (handle) BCryptDestroyHash(handle); }
};
Hash::Hash() : impl_(std::make_unique<Impl>()) {}
Hash::~Hash() = default;
void Hash::Update(std::span<const std::uint8_t> bytes) {
    Check(!impl_->finished && bytes.size() <= std::numeric_limits<ULONG>::max() &&
        BCryptHashData(impl_->handle, const_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()), 0) >= 0,
        "Cannot hash resource body");
}
resource_sync::Digest Hash::Finish() {
    resource_sync::Digest value{};
    Check(!impl_->finished && BCryptFinishHash(impl_->handle, value.data(), ULONG(value.size()), 0) >= 0,
          "Cannot finalize resource SHA-256");
    impl_->finished = true;
    return value;
}
Cache::Cache(std::string root) : root_(Absolute(root)) {}
const std::string& Cache::Root() const { return root_; }
const std::string& Cache::Error() const { return error_; }
std::string Cache::Path(const std::string& name, const resource_sync::Entry& expected) const {
    Check(resource_overlay::SafeName(name) && expected.required && expected.size &&
          expected.size <= resource_sync::max_file_size, "Invalid cached resource identity");
    constexpr char hex[] = "0123456789abcdef";
    std::string digest;
    digest.reserve(64);
    for (const auto value : expected.sha256) { digest += hex[value >> 4]; digest += hex[value & 15]; }
    const auto path = root_ + "/objects/" + digest + "/" + name;
    // The reviewed engine filesystem uses MAX_PATH buffers, including the
    // temporary suffix. Do not let an OS long-path form bypass that contract.
    Check(path.size() + 40 < MAX_PATH, "Cached resource path exceeds host filesystem limit");
    return path;
}
bool Cache::Read(const std::string& name, const resource_sync::Entry& expected,
                 std::span<std::uint8_t> out, std::uint32_t offset) const {
    const auto path = Path(name, expected);
    Parents parents;
    if (offset > expected.size || out.size() > expected.size - offset || !parents.Lock(path, false)) return false;
    File file{CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file.Valid() || !Regular(file.handle, expected.size)) return false;
    LARGE_INTEGER seek{}; seek.QuadPart = offset;
    DWORD count{};
    return SetFilePointerEx(file.handle, seek, nullptr, FILE_BEGIN) &&
        ReadFile(file.handle, out.data(), DWORD(out.size()), &count, nullptr) && count == out.size();
}
bool Cache::Store(const std::string& name, const resource_sync::Entry& expected,
                  std::span<const std::uint8_t> bytes) {
    error_.clear();
    std::string temporary;
    bool created = false;
    Parents parents;
    try {
        const auto target = Path(name, expected);
        Check(bytes.size() == expected.size, "Cannot cache truncated resource");
        Hash hash; hash.Update(bytes);
        Check(hash.Finish() == expected.sha256, "Cannot cache resource with incorrect SHA-256");
        Check(parents.Lock(target, true), "Resource cache parent is unavailable or a reparse point");
        const DWORD existing = GetFileAttributesA(target.c_str());
        Check(existing == INVALID_FILE_ATTRIBUTES ||
              !(existing & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)), "Unsafe resource cache target");
        temporary = target + ".tmp-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial);
        {
            File file{CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
            Check(file.Valid(), "Cannot create independent resource cache file");
            created = true;
            DWORD count{};
            Check(WriteFile(file.handle, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size() &&
                  FlushFileBuffers(file.handle), "Cannot write complete resource cache file");
        }
        Check(MoveFileExA(temporary.c_str(), target.c_str(),
              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Cannot commit resource cache file");
        temporary.clear();
        return true;
    } catch (const std::exception& failure) {
        error_ = failure.what();
        // Only the exact CREATE_NEW temporary owned by this operation is
        // eligible for cleanup; an old destination or linked source is kept.
        if (created && !temporary.empty()) DeleteFileA(temporary.c_str());
        return false;
    }
}
}
