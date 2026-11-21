// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include "resource_cache.hpp"
#include "resource_overlay.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
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
            if (pin(directory)) continue;
            if (!create || (!CreateDirectoryA(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                || !pin(directory)) return false;
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
std::unique_ptr<SourceFile> OpenObject(const std::string& path, std::uint32_t size) {
    File file{CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file.Valid() || !Regular(file.handle, size)) return {};
    return SourceFile::Reopen(file.handle, 0, size);
}

bool HasBody(const std::string& path, std::span<const std::uint8_t> bytes) {
    auto existing = OpenObject(path, std::uint32_t(bytes.size()));
    if (!existing) return false;
    std::array<std::uint8_t, 64 * 1024> buffer;
    for (std::uint32_t offset = 0; offset < bytes.size();) {
        const auto count = std::min(buffer.size(), bytes.size() - offset);
        if (!existing->ReadAt(offset, std::span(buffer).first(count)) ||
            std::memcmp(buffer.data(), bytes.data() + offset, count) != 0) return false;
        offset += std::uint32_t(count);
    }
    return existing->Unchanged();
}
std::atomic<unsigned long> serial{};
}

SourceFile::SourceFile(void* handle, std::uint64_t offset, std::uint32_t size)
    : handle_(handle), offset_(offset), size_(size) {}
SourceFile::~SourceFile() { if (handle_) CloseHandle(handle_); }
bool SourceFile::Inspect(void* handle, std::uint64_t offset, std::uint32_t size, Stamp& stamp) {
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandle(handle, &info) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    const auto length = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (offset > length || size > length - offset) return false;
    stamp = {info.dwVolumeSerialNumber, (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow,
        std::uint64_t(basic.CreationTime.QuadPart), std::uint64_t(basic.LastWriteTime.QuadPart),
        std::uint64_t(basic.ChangeTime.QuadPart), length, offset, size, info.dwFileAttributes};
    return true;
}
std::unique_ptr<SourceFile> SourceFile::Reopen(void* source, std::uint64_t offset, std::uint32_t size) {
    // DuplicateHandle shares the original file position. ReOpenFile does not.
    // Deny writers/deletion for this bounded job, including hardlink aliases.
    auto handle = ReOpenFile(source, GENERIC_READ, FILE_SHARE_READ, FILE_FLAG_SEQUENTIAL_SCAN);
    if (handle == INVALID_HANDLE_VALUE) return {};
    auto result = std::unique_ptr<SourceFile>(new SourceFile(handle, offset, size));
    if (!Inspect(handle, offset, size, result->stamp_)) return {};
    return result;
}
PendingSource::PendingSource(void* handle, std::uint64_t offset, std::uint32_t size)
    : handle_(handle), offset_(offset), size_(size) {}
PendingSource::~PendingSource() { CloseHandle(handle_); }
std::unique_ptr<PendingSource> PendingSource::Duplicate(void* handle, std::uint64_t offset, std::uint32_t size) {
    HANDLE copy{};
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return {};
    return std::unique_ptr<PendingSource>(new PendingSource(copy, offset, size));
}
std::unique_ptr<SourceFile> PendingSource::Lease() const { return SourceFile::Reopen(handle_, offset_, size_); }
bool PendingSource::Matches(const SourceFile& lease) const {
    if (offset_ != lease.offset_ || size_ != lease.size_) return false;
    SourceFile::Stamp current{};
    if (!SourceFile::Inspect(handle_, offset_, size_, current) || current != lease.stamp_) return false;
    // The 64-bit index in the digest stamp alone is not unique on ReFS.
    FILE_ID_INFO winner{}, pinned{};
    return GetFileInformationByHandleEx(handle_, FileIdInfo, &winner, sizeof(winner)) &&
        GetFileInformationByHandleEx(lease.handle_, FileIdInfo, &pinned, sizeof(pinned)) &&
        winner.VolumeSerialNumber == pinned.VolumeSerialNumber &&
        std::memcmp(winner.FileId.Identifier, pinned.FileId.Identifier, sizeof(winner.FileId.Identifier)) == 0;
}
bool SourceFile::Unchanged() const {
    Stamp now{};
    return Inspect(handle_, offset_, size_, now) && now == stamp_;
}
bool SourceFile::ReadAt(std::uint32_t offset, std::span<std::uint8_t> output) {
    if (offset > size_ || output.size() > size_ - offset) return false;
    LARGE_INTEGER at{}; at.QuadPart = offset_ + offset;
    DWORD count{};
    return SetFilePointerEx(handle_, at, nullptr, FILE_BEGIN) &&
        ReadFile(handle_, output.data(), DWORD(output.size()), &count, nullptr) && count == output.size();
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
namespace {
std::string ObjectPath(const std::string& root, const std::string& name, const resource_sync::Entry& expected,
                       Cache::Layout layout) {
    Check(resource_overlay::SafeName(name) && expected.required && expected.size &&
          expected.size <= resource_sync::max_file_size, "Invalid cached resource identity");

    constexpr std::string_view directory = "/objects/";
    constexpr char hex[] = "0123456789abcdef";
    const auto extension = std::string_view(name).substr(name.rfind('.'));
    const bool legacy = layout == Cache::Layout::legacy;
    std::string path;
    path.reserve(root.size() + directory.size() + expected.sha256.size() * 2 +
                 (legacy ? 1 + name.size() : extension.size()));
    path += root;
    path += directory;
    for (const auto value : expected.sha256) {
        path += hex[value >> 4];
        path += hex[value & 15];
    }
    if (legacy) {
        path += '/';
        path += name;
    } else {
        for (const char c : extension) path += c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c;
    }
    return path;
}
}
std::string Cache::Path(const std::string& name, const resource_sync::Entry& expected) const {
    auto path = ObjectPath(root_, name, expected, Layout::compact);
    Check(path.size() < MAX_PATH, "Cached resource path exceeds host filesystem limit");
    return path;
}
bool Cache::Read(const std::string& name, const resource_sync::Entry& expected,
                  std::span<std::uint8_t> out, std::uint32_t offset) const {
    auto source = Open(name, expected, Layout::compact);
    if (!source) source = Open(name, expected, Layout::legacy);
    return source && source->ReadAt(offset, out);
}
std::unique_ptr<SourceFile> Cache::Open(const std::string& name, const resource_sync::Entry& expected,
                                       Layout layout, std::string* openedPath) const {
    if (openedPath) openedPath->clear();
    auto path = ObjectPath(root_, name, expected, layout);
    if (path.size() >= MAX_PATH) return {};
    // Avoid reopening the deeper parent chain if this legacy object is absent.
    if (layout == Layout::legacy && GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    Parents parents;
    if (!parents.Lock(path, false)) return {};
    auto source = OpenObject(path, expected.size);
    if (source && openedPath) *openedPath = std::move(path);
    return source;
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
        if (existing != INVALID_FILE_ATTRIBUTES && HasBody(target, bytes)) return true;
        temporary = root_ + "/.tmp-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial);
        {
            File file{CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
            Check(file.Valid(), "Cannot create independent resource cache file");
            created = true;
            DWORD count{};
            Check(WriteFile(file.handle, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size() &&
                  FlushFileBuffers(file.handle), "Cannot write complete resource cache file");
        }
        if (!MoveFileExA(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            // An alias or another client can publish and pin this content first.
            Check(HasBody(target, bytes), "Cannot commit resource cache file");
            Check(DeleteFileA(temporary.c_str()), "Cannot remove redundant resource cache staging file");
        }
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
