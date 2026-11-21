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
#include <cstdio>
#include <filesystem>
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
    // The previous cache root can itself exceed the engine's ANSI limit.
    // It is only used via extended Win32 paths until a readable body is promoted.
    std::string value = std::filesystem::path(input).lexically_normal().make_preferred().string();
    while (value.size() > 3 && (value.back() == '/' || value.back() == '\\')) value.pop_back();
    return value;
}
std::wstring NativeName(const std::string& path);
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
            if (!create || (!CreateDirectoryW(NativeName(directory).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
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
    FILE_ID_INFO identity{};
    const bool strong = GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)) != FALSE;
    std::uint64_t id[2]{};
    std::memcpy(id, identity.FileId.Identifier, sizeof(id));
    const auto length = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (offset > length || size > length - offset) return false;
    stamp = {info.dwVolumeSerialNumber, (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow,
        std::uint64_t(basic.CreationTime.QuadPart), std::uint64_t(basic.LastWriteTime.QuadPart),
        std::uint64_t(basic.ChangeTime.QuadPart), length, offset, size, info.dwFileAttributes,
        identity.VolumeSerialNumber, id[0], id[1], std::uint64_t(strong)};
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
    if (!lease.HasStableIdentity() || offset_ != lease.offset_ || size_ != lease.size_) return false;
    SourceFile::Stamp current{};
    return SourceFile::Inspect(handle_, offset_, size_, current) && current == lease.stamp_;
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
Cache::Cache(std::string root, std::string previousRoot, std::string downloadRoot)
    : root_(Absolute(root)), previousRoot_(previousRoot.empty() ? root_ : Absolute(previousRoot)),
      downloadRoot_(downloadRoot.empty() ? std::string{} : Absolute(downloadRoot)) {}
const std::string& Cache::Root() const { return root_; }
const std::string& Cache::Error() const { return error_; }

namespace {
constexpr char Hex[] = "0123456789abcdef";
constexpr std::size_t KeyBytes = 4;
std::string HexKey(std::span<const std::uint8_t> bytes) {
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto b : bytes) { result += Hex[b >> 4]; result += Hex[b & 15]; }
    return result;
}
std::string Extension(const std::string& name) {
    std::string result = name.substr(name.rfind('.'));
    for (char& c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}
void Valid(const std::string& name, const resource_sync::Entry& expected) {
    Check(resource_overlay::SafeName(name) && expected.required && expected.size &&
          expected.size <= resource_sync::max_file_size, "Invalid cached resource identity");
}
std::wstring NativeName(const std::string& path) {
    const auto count = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path.data(), int(path.size()), nullptr, 0);
    if (!count) return {};
    std::wstring full = L"\\\\?\\";
    full.resize(4 + count);
    if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path.data(), int(path.size()), full.data()+4, count)) return {};
    std::replace(full.begin()+4, full.end(), L'/', L'\\');
    return full;
}
bool UnderlyingPath(HANDLE file, const std::wstring& expected) {
    std::wstring actual(expected.size() + 1, L'\0');
    auto count = GetFinalPathNameByHandleW(file, actual.data(), DWORD(actual.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    return count && count == expected.size() &&
        CompareStringOrdinal(actual.data(), int(count), expected.data(), int(expected.size()), TRUE) == CSTR_EQUAL;
}
enum class Occupant { missing, same, corrupt, collision };
Occupant InspectSlot(const std::string& path, const resource_sync::Entry& expected) {
    File file{CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!file.Valid()) {
        const auto error = GetLastError();
        Check(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND, "Cache slot is unavailable");
        return Occupant::missing;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    Check(GetFileInformationByHandle(file.handle, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)), "Unsafe resource cache target");
    if (info.nFileSizeHigh || !info.nFileSizeLow || info.nFileSizeLow > resource_sync::max_file_size)
        return Occupant::corrupt;
    Hash hash;
    std::array<std::uint8_t, 64 * 1024> buffer;
    for (DWORD left = info.nFileSizeLow; left;) {
        const auto size = std::min<DWORD>(left, DWORD(buffer.size()));
        DWORD count{};
        Check(ReadFile(file.handle, buffer.data(), size, &count, nullptr) && count == size, "Cannot verify occupied cache slot");
        hash.Update(std::span(buffer).first(count)); left -= count;
    }
    const auto digest = hash.Finish();
    if (digest == expected.sha256 && info.nFileSizeLow == expected.size) return Occupant::same;
    return std::equal(digest.begin(), digest.begin()+KeyBytes, expected.sha256.begin()) ? Occupant::collision : Occupant::corrupt;
}
void WriteBody(HANDLE target, const resource_sync::Entry& expected, std::span<const std::uint8_t> bytes, SourceFile* source) {
    DWORD written{};
    if (!source) {
        Check(WriteFile(target, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size(),
              "Cannot write resource cache file");
    } else {
        std::array<std::uint8_t, 64 * 1024> buffer;
        for (std::uint32_t offset = 0; offset < expected.size;) {
            auto chunk = std::span(buffer).first(std::min<std::size_t>(buffer.size(), expected.size-offset));
            Check(source->ReadAt(offset, chunk) && WriteFile(target, chunk.data(), DWORD(chunk.size()), &written, nullptr) &&
                  written == chunk.size(), "Cannot copy verified cache resource");
            offset += written;
        }
    }
    Check(FlushFileBuffers(target), "Cannot flush resource cache file");
}
std::string Publish(const Cache& cache, const std::string& name, const resource_sync::Entry& expected,
                    std::span<const std::uint8_t> bytes, SourceFile* source) {
    const auto primary = cache.Path(name, expected);
    Parents parents;
    Check(parents.Lock(primary, true), "Resource cache parent is unavailable or a reparse point");
    // One shared, empty file. Lock only this prefix, across all client processes.
    File gate{CreateFileA((cache.Root()+"/.lock").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    OVERLAPPED range{};
    std::memcpy(&range.Offset, expected.sha256.data(), KeyBytes);
    Check(gate.Valid() && Regular(gate.handle, 0) && LockFileEx(gate.handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &range),
          "Cannot lock resource cache slot");
    std::string target = primary;
    auto state = InspectSlot(target, expected);
    if (state == Occupant::collision) {
        const auto key = HexKey(std::span(expected.sha256).first(KeyBytes));
        const auto ext = Extension(name);
        for (std::uint32_t slot=1; state == Occupant::collision; ++slot) {
            Check(slot != 0, "Resource cache collision slots exhausted");
            char suffix[10];
            std::snprintf(suffix, sizeof(suffix), "-%08x", unsigned(slot));
            target = cache.Root() + "/x/" + key + suffix + ext;
            Check(target.size() < MAX_PATH, "Cached resource path exceeds host filesystem limit");
            if (slot == 1) Check(parents.Lock(target, true), "Cannot pin resource collision directory");
            state = InspectSlot(target, expected);
        }
    }
    if (state == Occupant::same) return target;
    const auto temporary = NativeName(cache.Root()+"/.tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial));
    bool created = false;
    try {
        {
            File file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
            Check(file.Valid(), "Cannot create independent resource cache file");
            created = true;
            WriteBody(file.handle, expected, bytes, source);
        }
        const auto flags = MOVEFILE_WRITE_THROUGH | (state == Occupant::corrupt ? MOVEFILE_REPLACE_EXISTING : 0);
        Check(MoveFileExW(temporary.c_str(), NativeName(target).c_str(), flags), "Cannot publish verified cache file");
        return target;
    } catch (...) {
        if (created) DeleteFileW(temporary.c_str());
        throw;
    }
}
}

std::string Cache::Path(const std::string& name, const resource_sync::Entry& expected) const {
    Valid(name, expected);
    auto path = root_ + "/" + HexKey(std::span(expected.sha256).first(KeyBytes)) + Extension(name);
    Check(path.size() < MAX_PATH, "Cached resource path exceeds host filesystem limit");
    return path;
}
std::vector<std::string> Cache::OtherSlots(const std::string& primary) const {
    std::vector<std::string> result;
    const auto leaf = primary.substr(primary.find_last_of("/\\")+1);
    const auto stem = leaf.substr(0, KeyBytes*2), ext = leaf.substr(KeyBytes*2);
    const auto folder = root_ + "/x/";
    WIN32_FIND_DATAA entry{};
    const auto search = FindFirstFileA((folder + stem + "-????????" + ext).c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return result;
    struct Search { HANDLE handle; ~Search() { FindClose(handle); } } guard{search};
    do {
        const std::string_view found(entry.cFileName);
        if (found.size() == stem.size()+9+ext.size() && found.starts_with(stem+"-") && found.ends_with(ext) &&
            found.substr(stem.size()+1,8).find_first_not_of(Hex) == std::string_view::npos)
            result.emplace_back(folder + entry.cFileName);
    } while (FindNextFileA(search, &entry));
    return result;
}
std::unique_ptr<SourceFile> OpenCandidate(const std::string& path, std::uint32_t size) {
    // The final name of the opened handle binds the read to the requested path
    // even if an ancestor is concurrently exchanged. No per-parent open on hits.
    auto native = NativeName(path);
    if (native.empty()) return {};
    File file{CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!file.Valid() || !UnderlyingPath(file.handle, native)) return {};
    // This handle already has an independent position and denies writers and deletion.
    auto source = std::unique_ptr<SourceFile>(new SourceFile(file.handle, 0, size));
    file.handle = INVALID_HANDLE_VALUE;
    if (!SourceFile::Inspect(source->handle_, 0, size, source->stamp_) || source->stamp_[5] != size) return {};
    return source;
}
std::unique_ptr<SourceFile> Cache::Open(const std::string& name, const resource_sync::Entry& expected,
                                       Layout layout, std::string& path) const {
    if (layout == Layout::gameDownload) {
        if (downloadRoot_.empty()) return {};
        Valid(name, expected);
        path = downloadRoot_ + "/" + name;
    } else if (layout == Layout::shortName) path = Path(name, expected);
    else {
        Valid(name, expected);
        path = previousRoot_ + "/objects/" + HexKey(expected.sha256) +
            (layout == Layout::flatSha ? Extension(name) : "/" + name);
    }
    return OpenCandidate(path, expected.size);
}
std::vector<std::uint8_t> Cache::ReadCatalog() const {
    constexpr DWORD maxBytes = 8 + 16384 * (1 + 255 + 4 + 32);
    const auto path = NativeName(root_ + "/index.bin");
    File file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    LARGE_INTEGER size{};
    if (!file.Valid() || !GetFileSizeEx(file.handle,&size) || size.QuadPart < 4 || size.QuadPart > maxBytes ||
        !Regular(file.handle, DWORD(size.QuadPart)) || !UnderlyingPath(file.handle,path)) return {};
    std::vector<std::uint8_t> bytes(std::size_t(size.QuadPart));
    DWORD count{};
    if (!ReadFile(file.handle, bytes.data(), DWORD(bytes.size()), &count, nullptr) || count != bytes.size()) return {};
    return bytes;
}
void Cache::WriteCatalog(std::span<const std::uint8_t> bytes) const {
    const auto path = root_ + "/index.bin";
    Parents parents;
    if (!parents.Lock(path,true)) return;
    const auto temporary = NativeName(root_ + "/.i" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial));
    {
        File file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (!file.Valid()) return;
        DWORD count{};
        if (WriteFile(file.handle,bytes.data(),DWORD(bytes.size()),&count,nullptr) && count == bytes.size()) {
            CloseHandle(file.handle); file.handle = INVALID_HANDLE_VALUE;
            if (MoveFileExW(temporary.c_str(),NativeName(path).c_str(),MOVEFILE_REPLACE_EXISTING)) return;
        }
    }
    DeleteFileW(temporary.c_str());
}
std::string Cache::EnginePath(const std::string& name, const resource_sync::Entry& expected,
                              std::string verifiedPath, SourceFile& source) const {
    if (verifiedPath.size() < MAX_PATH) return verifiedPath;
    return Publish(*this, name, expected, {}, &source);
}
bool Cache::Store(const std::string& name, const resource_sync::Entry& expected,
                  std::span<const std::uint8_t> bytes, std::string* storedPath) {
    error_.clear();
    try {
        Check(bytes.size() == expected.size, "Cannot cache truncated resource");
        Hash hash; hash.Update(bytes);
        Check(hash.Finish() == expected.sha256, "Cannot cache resource with incorrect SHA-256");
        auto actual = Publish(*this, name, expected, bytes, nullptr);
        if (storedPath) *storedPath = std::move(actual);
        return true;
    } catch (const std::exception& failure) { error_ = failure.what(); return false; }
}
}
