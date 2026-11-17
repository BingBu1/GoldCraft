// SPDX-License-Identifier: GPL-3.0-or-later
#include "../include/goldcraft/cof_resource_paths.hpp"
#include <metahook.h>
#include <IFileSystem.h>
#include <bcrypt.h>
#include "resource_overlay.hpp"
#include "resource_cache.hpp"
#include "goldcraft/resource_sync.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <intrin.h>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <sys/stat.h>
#pragma comment(lib, "bcrypt.lib")

namespace goldcraft::resource_overlay {
namespace {
// Evidence: docs/2026-11-05_分析-客户端资源更新与下载生命周期-report.md.
// This file is host adaptation, not original CSOL game behavior.
constexpr char FileSystemSha[] = "07a432b6214c6ea5ea4a988d5c8384d3ab35a531b49fe71c354f3fed5ce9f316";
struct Entry { std::string name, absolute; };
std::map<std::string, Entry> paths;
std::shared_mutex access;
thread_local unsigned readDepth{};
struct ReadAccess {
    const bool outer = readDepth == 0;
    ReadAccess() { if (outer) access.lock_shared(); ++readDepth; }
    ~ReadAccess() { --readDepth; if (outer) access.unlock_shared(); }
    ReadAccess(const ReadAccess&) = delete;
    ReadAccess& operator=(const ReadAccess&) = delete;
};
std::string root, pathId, error;
unsigned generation{};
metahook_api_t* api{};
IFileSystem* fs{};
std::array<hook_t*, 8> hooks{};
using OpenFn = FileHandle_t(__thiscall*)(void*, const char*, const char*, const char*);
using ExistsFn = bool(__thiscall*)(void*, const char*);
using SizeFn = unsigned(__thiscall*)(void*, const char*);
using TimeFn = long(__thiscall*)(void*, const char*);
using LocalFn = const char*(__thiscall*)(void*, const char*, char*, int);
OpenFn originalOpen{};
ExistsFn originalExists{};
SizeFn originalSize{};
TimeFn originalTime{};
LocalFn originalLocal{};
using FilenoFn = int(__cdecl*)(void*);
using OsHandleFn = intptr_t(__cdecl*)(int);
FilenoFn hostFileno{};
OsHandleFn hostOsHandle{};
using FopenFn = void*(__thiscall*)(void*, const char*, const char*, bool);
using FcloseFn = void(__thiscall*)(void*, void*);
using StatFn = int(__thiscall*)(void*, const char*, struct _stat64i32*);
using FstatFn = int(__cdecl*)(int, struct _stat64i32*);
FopenFn originalFopen{};
FcloseFn originalFclose{};
StatFn originalStat{};
FstatFn hostFstat{};
void** statImport{};
void* nativeStatImport{};
void* findFileStatReturn{};
static_assert(sizeof(struct _stat64i32) == 48 && offsetof(struct _stat64i32, st_size) == 20 &&
    offsetof(struct _stat64i32, st_mtime) == 32 && offsetof(struct _stat64i32, st_ctime) == 40);

std::string Lower(std::string value) {
    for (auto& c : value) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return value;
}
bool Fail(const char* text) { error = text; return false; }
bool EligibleId(const char* id) {
    return !id || !_stricmp(id, "GAME") || !_stricmp(id, "GAMEDOWNLOAD");
}
const Entry* Find(const char* name) {
    if (!name || paths.empty()) return nullptr;
    const auto found = paths.find(Lower(name));
    return found == paths.end() ? nullptr : &found->second;
}
bool ReadMode(const char* mode) {
    return mode && (!std::strcmp(mode, "r") || !std::strcmp(mode, "rb") || !std::strcmp(mode, "rt"));
}
struct ReadOpen;
thread_local ReadOpen* readOpen{};
struct ReadOpen {
    ReadOpen* previous;
    void* stream{};
    std::array<char, MAX_PATH> name;
    ReadOpen() : previous(readOpen) { readOpen = this; }
    ~ReadOpen() { readOpen = previous; }
    void Remember(void* file, const char* path) {
        stream = nullptr;
        if (!file || !path) return;
        const auto length = std::strlen(path);
        if (length >= name.size()) return;
        std::memcpy(name.data(), path, length + 1);
        stream = file;
    }
    void* Take(const char* path) {
        auto* file = stream;
        stream = nullptr;
        return file && path && !std::strcmp(name.data(), path) ? file : nullptr;
    }
};
FileHandle_t NativeOpen(void* object, const char* name, const char* mode, const char* id) {
    if (object != fs || !hostFstat || !ReadMode(mode)) return originalOpen(object, name, mode, id);
    ReadOpen scope;
    return originalOpen(object, name, mode, id);
}
void* __fastcall Fopen(void* object, void*, const char* name, const char* mode, bool cached) {
    auto* scope = object == fs ? readOpen : nullptr;
    if (scope) scope->stream = nullptr;
    auto* stream = originalFopen(object, name, mode, cached);
    if (scope && ReadMode(mode)) scope->Remember(stream, name);
    return stream;
}
void __fastcall Fclose(void* object, void*, void* stream) {
    if (object == fs) for (auto* scope = readOpen; scope; scope = scope->previous)
        if (scope->stream == stream) scope->stream = nullptr;
    originalFclose(object, stream);
}
int __fastcall Stat(void* object, void*, const char* name, struct _stat64i32* output) {
    // Exact FindFile caller consumes only size/mtime/ctime after Trace_FOpen.
    // Other callers and intervening hooks retain the native path query.
    if (object == fs && _ReturnAddress() == findFileStatReturn && readOpen && output && *statImport == nativeStatImport)
        if (auto* stream = readOpen->Take(name)) {
            const auto descriptor = hostFileno(stream);
            struct _stat64i32 value{};
            if (descriptor >= 0 && !hostFstat(descriptor, &value) && (value.st_mode & _S_IFMT) == _S_IFREG) {
                *output = value;
                return 0;
            }
        }
    return originalStat(object, name, output);
}
FileHandle_t OpenEntry(const Entry& entry, const char* mode) {
    // Original Open rejects absolute names. Its non-null pathID branch filters
    // the search list, so a tail-mounted private ID cannot read an older PAK.
    return NativeOpen(fs, entry.name.c_str(), mode, pathId.c_str());
}
FileHandle_t __fastcall Open(void* object, void*, const char* name, const char* mode, const char* id) {
    ReadAccess lock;
    if (object == fs && ReadMode(mode) && EligibleId(id))
        if (const auto* entry = Find(name)) return OpenEntry(*entry, mode);
    return NativeOpen(object, name, mode, id);
}
bool __fastcall Exists(void* object, void*, const char* name) {
    ReadAccess lock;
    if (object == fs) if (const auto* entry = Find(name)) {
        auto handle = OpenEntry(*entry, "rb");
        if (!handle) return false;
        fs->Close(handle); return true;
    }
    return originalExists(object, name);
}
unsigned __fastcall Size(void* object, void*, const char* name) {
    ReadAccess lock;
    if (object == fs) if (const auto* entry = Find(name)) {
        auto handle = OpenEntry(*entry, "rb");
        if (!handle) return unsigned(-1);
        const auto result = fs->Size(handle); fs->Close(handle); return result;
    }
    return originalSize(object, name);
}
long __fastcall Time(void* object, void*, const char* name) {
    ReadAccess lock;
    if (object == fs) if (const auto* entry = Find(name)) {
        struct _stat64 value{};
        // Original FindFile at 10002A9A..10002AB5 returns max(mtime, ctime).
        return _stat64(entry->absolute.c_str(), &value) == 0 ? long(std::max(value.st_mtime, value.st_ctime)) : 0;
    }
    return originalTime(object, name);
}
const char* __fastcall Local(void* object, void*, const char* name, char* out, int size) {
    ReadAccess lock;
    if (object == fs) if (const auto* entry = Find(name)) {
        if (!out || size <= 0) return nullptr;
        out[0] = 0;
        if (entry->absolute.size() >= size_t(size) || GetFileAttributesA(entry->absolute.c_str()) == INVALID_FILE_ATTRIBUTES)
            return nullptr;
        std::memcpy(out, entry->absolute.c_str(), entry->absolute.size() + 1); return out;
    }
    return originalLocal(object, name, out, size);
}
std::string Absolute(const std::string& name) {
    if (name.size() < 3 || name[1] != ':' || (name[2] != '/' && name[2] != '\\')) return {};
    std::array<char, MAX_PATH> out{};
    const auto count = GetFullPathNameA(name.c_str(), DWORD(out.size()), out.data(), nullptr);
    if (!count || count >= out.size()) return {};
    std::string result(out.data());
    while (result.size() > 3 && (result.back() == '/' || result.back() == '\\')) result.pop_back();
    return result;
}
// `plain` holds ancestor directories already proven ordinary during the same
// mount. Hundreds of objects share the cache root's ancestors; querying each
// of them again per object was most of a 0.5 s mount in live samples.
bool PlainPath(const std::string& name, bool directory, std::unordered_set<std::string>* plain = nullptr) {
    std::string current = name;
    bool leaf = true;
    std::vector<std::string> walked;
    while (current.size() >= 3) {
        if (!leaf && plain && plain->contains(current)) break;
        const auto attributes = GetFileAttributesA(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            bool(attributes & FILE_ATTRIBUTE_DIRECTORY) != (leaf ? directory : true)) return false;
        if (!leaf && plain) walked.push_back(current);
        const auto slash = current.find_last_of("/\\");
        if (slash == std::string::npos || slash <= 2) break;
        current.resize(slash); leaf = false;
    }
    if (plain) for (auto& value : walked) plain->insert(std::move(value));
    return true;
}
bool MatchModule(void** table) {
    HMODULE module{};
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const char*>(table[10]), &module)) return false;
    constexpr std::array<std::pair<unsigned, uintptr_t>, 13> slots{{
        {4,0x1ad0}, {5,0x6470}, {8,0x2420}, {10,0x54a0}, {14,0x6f70},
        {15,0x6f20}, {16,0x38f0}, {17,0x24b0}, {32,0x3990}, {53,0x1dd0},
        {55,0x7640}, {56,0x75a0}, {66,0x7740}}};
    for (const auto& [slot, rva] : slots)
        if (uintptr_t(table[slot]) != uintptr_t(module) + rva) return false;
    std::array<char, MAX_PATH> name{};
    const auto count = GetModuleFileNameA(module, name.data(), DWORD(name.size()));
    if (!count || count >= name.size()) return false;
    std::ifstream stream(name.data(), std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(stream)), {});
    if (data.size() != 64352) return false;
    std::array<unsigned char, 32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(data.data()), ULONG(data.size()),
        digest.data(), ULONG(digest.size())) < 0) return false;
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < digest.size(); ++i)
        if (FileSystemSha[2*i] != hex[digest[i] >> 4] || FileSystemSha[2*i+1] != hex[digest[i] & 15]) return false;
    // Exact matched module IAT, not this plugin's statically linked CRT.
    hostFileno = *reinterpret_cast<FilenoFn*>(uintptr_t(module) + 0xa11c);
    HMODULE crt{};
    if (!hostFileno || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<const char*>(hostFileno), &crt)) return false;
    hostOsHandle = reinterpret_cast<OsHandleFn>(GetProcAddress(crt, "_get_osfhandle"));
    if (!hostOsHandle) return false;
    statImport = reinterpret_cast<void**>(uintptr_t(module) + 0xa0b0);
    nativeStatImport = *statImport;
    HMODULE statCrt{};
    if (!nativeStatImport || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<const char*>(nativeStatImport), &statCrt)) return false;
    hostFstat = reinterpret_cast<FstatFn>(GetProcAddress(statCrt, "_fstat64i32"));
    findFileStatReturn = reinterpret_cast<void*>(uintptr_t(module) + 0x2a82);
    return true;
}
void ClearLocked() {
    paths.clear(); pathId.clear();
    if (fs && !root.empty()) fs->RemoveSearchPath(root.c_str());
    root.clear();
}
}

bool SafeName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name.front() == '/') return false;
    for (unsigned char c : name)
        if (c < 32 || c > 126 || std::strchr("\\:%?<>\"|*~", c)) return false;
    const auto lower = Lower(name);
    size_t begin = 0;
    do {
        const auto end = lower.find('/', begin);
        const auto part = lower.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part.front() == '.' || part.back() == '.' || part.back() == ' ') return false;
        const auto stem = part.substr(0, part.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' && stem[3] <= '9')) return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    const auto slash = lower.find('/'), dot = lower.rfind('.');
    if (dot == std::string::npos) return false;
    const auto ext = lower.substr(dot), top = lower.substr(0, slash);
    if (slash == std::string::npos)
        return ext == ".wad" && lower != "cached.wad" && lower != "tempdecal.wad" && lower != "pldecal.wad" && lower != "spraypaint.wad";
    return (top == "maps" && (ext == ".bsp" || ext == ".res" || ext == ".txt")) || lower == "maps/zbs_message.cso" ||
        (top == "models" && (ext == ".mdl" || ext == ".seq" || ext == ".json" || ext == ".tga" || ext == ".bmp")) ||
        (top == "sound" && (ext == ".wav" || ext == ".mp3")) || lower == "sound/sentences.json" ||
        (top == "bgm" && ext == ".mp3") ||
        (top == "resource" && (ext == ".tga" || (ext == ".ttf" && (lower.starts_with("resource/font/") || lower.starts_with("resource/csol/"))))) ||
        lower == "resource/csol/hud/v1/csolhudscheme.res" ||
        lower == "resource/csol/wall/v1/wallupgradescheme.res" ||
        lower == "resource/csol/zecounter/v1/zecounterscheme.res" || lower == "resource/cso_chn.txt" ||
        (top == "sprites" && (ext == ".spr" || ext == ".txt" || ext == ".tga" || ext == ".webm")) ||
        (top == "gfx" && (ext == ".tga" || ext == ".bmp")) || lower == "gfx/tattoo/tattoo.cso" ||
        (top == "materials" && (ext == ".json" || ext == ".tga" || ext == ".bmp")) ||
        (top == "shader" && ext == ".txt") ||
        (top == "overviews" && (ext == ".txt" || ext == ".bmp" || ext == ".tga")) || (top == "events" && ext == ".sc") ||
        goldcraft::cof::DownloadDataPath(lower);
}
bool Install(metahook_api_s* incoming, mh_interface_s* interfaces) {
    if (fs) return true;
    if (!incoming || !incoming->VFTHook || !incoming->UnHook || !interfaces || !interfaces->FileSystem || interfaces->FileSystem_HL25)
        return Fail("Resource overlay requires the reviewed IFileSystem009 ABI");
    auto* object = interfaces->FileSystem;
    if (!MatchModule(*reinterpret_cast<void***>(object))) return Fail("Resource overlay filesystem identity differs");
    api = incoming; fs = object;
    const std::array<int,8> slots{10,8,14,16,32,55,56,66};
    const std::array<void*,8> replacements{reinterpret_cast<void*>(Open), reinterpret_cast<void*>(Exists),
        reinterpret_cast<void*>(Size), reinterpret_cast<void*>(Time), reinterpret_cast<void*>(Local),
        reinterpret_cast<void*>(Fopen), reinterpret_cast<void*>(Fclose), reinterpret_cast<void*>(Stat)};
    const std::array<void**,8> originals{reinterpret_cast<void**>(&originalOpen), reinterpret_cast<void**>(&originalExists),
        reinterpret_cast<void**>(&originalSize), reinterpret_cast<void**>(&originalTime), reinterpret_cast<void**>(&originalLocal),
        reinterpret_cast<void**>(&originalFopen), reinterpret_cast<void**>(&originalFclose), reinterpret_cast<void**>(&originalStat)};
    for (size_t i=0; i<hooks.size(); ++i) {
        hooks[i] = api->VFTHook(fs, 0, slots[i], replacements[i], originals[i]);
        if (!hooks[i]) { Shutdown(); return Fail("Cannot install resource overlay filesystem hooks"); }
    }
    error.clear(); return true;
}
bool Begin(const std::string& directory, const Paths& verifiedPaths) {
    if (!fs) return Fail("Resource overlay is not installed");
    const auto absoluteRoot = Absolute(directory);
    std::unordered_set<std::string> plain;
    if (absoluteRoot.empty() || !PlainPath(absoluteRoot, true, &plain)) return Fail("Invalid resource overlay root");
    const auto prefix = Lower(absoluteRoot) + "/objects/";
    std::map<std::string, Entry> candidate;
    for (const auto& [name, absolute] : verifiedPaths) {
        if (!SafeName(name)) return Fail("Unsafe resource overlay name");
        const auto actual = Absolute(absolute);
        const auto normalized = Lower(actual);
        if (actual.empty() || !normalized.starts_with(prefix) || !PlainPath(actual, false, &plain))
            return Fail("Resource overlay path is not an independent file under its root");
        const auto object = normalized.substr(prefix.size());
        if (object.size() <= 65 || object[64] != '/' || object.substr(65) != Lower(name) ||
            !std::all_of(object.begin(), object.begin()+64, [](char c) { return (c>='0'&&c<='9') || (c>='a'&&c<='f'); }))
            return Fail("Resource overlay path does not match its content-addressed logical name");
        const auto relative = actual.substr(absoluteRoot.size()+1);
        if (!candidate.emplace(Lower(name), Entry{relative, actual}).second) return Fail("Duplicate resource overlay name");
    }
    std::unique_lock lock(access);
    ClearLocked();
    root = absoluteRoot; pathId = "GOLDCRAFT_RESOURCE_" + std::to_string(++generation);
    fs->AddSearchPathNoWrite(root.c_str(), pathId.c_str());
    for (const auto& [name, entry] : candidate) {
        auto handle = OpenEntry(entry, "rb");
        if (!handle) { ClearLocked(); return Fail("Cannot read mounted resource overlay file"); }
        const auto size = fs->Size(handle); fs->Close(handle);
        if (!size || size > resource_sync::max_file_size) { ClearLocked(); return Fail("Invalid resource overlay file size"); }
    }
    paths = std::move(candidate); error.clear(); return true;
}
Snapshot Capture() {
    ReadAccess lock;
    Snapshot value{root, {}};
    for (const auto& [name, entry] : paths) value.paths.emplace(name, entry.absolute);
    return value;
}
bool Restore(const Snapshot& snapshot) {
    if (!snapshot.root.empty()) return Begin(snapshot.root, snapshot.paths);
    if (!snapshot.paths.empty()) return Fail("Resource overlay snapshot has no root");
    Clear(); return true;
}
void Clear() { std::unique_lock lock(access); ClearLocked(); }
void Shutdown() {
    Clear();
    if (api) for (auto& hook : hooks) if (hook) { api->UnHook(hook); hook = nullptr; }
    fs = nullptr; api = nullptr;
}
bool Read(const char* name, std::vector<std::uint8_t>& bytes) {
    bytes.clear();
    if (!fs || !name) return false;
    auto handle = fs->Open(name, "rb");
    if (!handle) return false;
    const auto size = fs->Size(handle);
    bool good = size && size <= resource_sync::max_file_size;
    try { if (good) { bytes.resize(size); good = fs->Read(bytes.data(), int(size), handle) == int(size); } }
    catch (...) { fs->Close(handle); throw; }
    fs->Close(handle); if (!good) bytes.clear(); return good;
}
std::unique_ptr<Reader> OpenReader(const char* name) {
    if (!fs || !name) return {};
    auto handle = fs->Open(name, "rb");
    if (!handle) return {};
    const auto size = fs->Size(handle);
    if (!size || size > resource_sync::max_file_size) { fs->Close(handle); return {}; }
    try { return std::unique_ptr<Reader>(new Reader(fs, handle, size)); }
    catch (...) { fs->Close(handle); throw; }
}
Reader::~Reader() { if (handle_) filesystem_->Close(handle_); }
std::unique_ptr<resource_cache::PendingSource> Reader::DeferredSource() const {
    if (!handle_ || !hostFileno || !hostOsHandle) return {};
    // IDA Find10002820 / Seek10006D00, exact FileSystemSha above.
    void* stream{}; std::uint64_t start{}, length{};
    std::memcpy(&stream, handle_, sizeof(stream));
    std::memcpy(&start, static_cast<const char*>(handle_) + 8, sizeof(start));
    std::memcpy(&length, static_cast<const char*>(handle_) + 16, sizeof(length));
    if (!stream || length != size_) return {};
    const auto fd = hostFileno(stream);
    if (fd < 0) return {};
    const auto os = hostOsHandle(fd);
    if (os == -1) return {};
    return resource_cache::PendingSource::Duplicate(reinterpret_cast<void*>(os), start, size_);
}
int Reader::ReadAt(std::uint32_t offset, std::span<std::uint8_t> output) {
    if (!handle_ || offset > size_ || output.size() > size_ - offset || output.size() > 2u*1024u*1024u) return -1;
    if (output.empty()) return 0;
    filesystem_->Seek(handle_, int(offset), FILESYSTEM_SEEK_HEAD);
    const auto count = filesystem_->Read(output.data(), int(output.size()), handle_);
    return count == int(output.size()) ? count : -1;
}
const char* Error() { return error.c_str(); }
}
