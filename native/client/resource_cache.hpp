// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "goldcraft/resource_sync.hpp"
#include <memory>
#include <array>
#include <compare>
#include <span>
#include <string>

namespace goldcraft::resource_cache {
// Independent OS file position and read lease. No engine calls are permitted
// on worker threads. The range may be one entry inside a PAK.
class SourceFile {
public:
    using Stamp = std::array<std::uint64_t, 9>;
    static std::unique_ptr<SourceFile> Reopen(void* handle, std::uint64_t offset, std::uint32_t size);
    ~SourceFile();
    SourceFile(const SourceFile&) = delete;
    SourceFile& operator=(const SourceFile&) = delete;
    const Stamp& Identity() const { return stamp_; }
    bool Unchanged() const;
    bool ReadAt(std::uint32_t offset, std::span<std::uint8_t> output);
private:
    SourceFile(void* handle, std::uint64_t offset, std::uint32_t size);
    friend class PendingSource;
    static bool Inspect(void* handle, std::uint64_t offset, std::uint32_t size, Stamp& stamp);
    void* handle_{};
    std::uint64_t offset_{};
    std::uint32_t size_{};
    Stamp stamp_{};
};
// Main thread only: a second reference to the engine's own open file. It has
// neither an independent position nor a lease. Lease() converts it on any
// thread, after the engine has closed its handle, so the reopen and identity
// queries (about 0.1 ms a file) leave the paused admission thread.
class PendingSource {
public:
    static std::unique_ptr<PendingSource> Duplicate(void* handle, std::uint64_t offset, std::uint32_t size);
    ~PendingSource();
    PendingSource(const PendingSource&) = delete;
    PendingSource& operator=(const PendingSource&) = delete;
    std::unique_ptr<SourceFile> Lease() const;
    // Check the actual newly resolved handle against an uninterrupted read lease.
    bool Matches(const SourceFile& lease) const;
private:
    PendingSource(void* handle, std::uint64_t offset, std::uint32_t size);
    void* handle_{};
    std::uint64_t offset_{};
    std::uint32_t size_{};
};
// These paths belong to the updater. Original GAME files and PAKs are never
// opened for writing; a new body is committed as an independent file.
class Cache {
public:
    explicit Cache(std::string root);
    const std::string& Root() const;
    std::string Path(const std::string& name, const resource_sync::Entry& expected) const;
    bool Read(const std::string& name, const resource_sync::Entry& expected,
              std::span<std::uint8_t> out, std::uint32_t offset) const;
    std::unique_ptr<SourceFile> Open(const std::string& name, const resource_sync::Entry& expected) const;
    bool Store(const std::string& name, const resource_sync::Entry& expected,
               std::span<const std::uint8_t> bytes);
    const std::string& Error() const;
private:
    std::string root_, error_;
};

class Hash {
public:
    Hash();
    ~Hash();
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;
    void Update(std::span<const std::uint8_t> bytes);
    resource_sync::Digest Finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
