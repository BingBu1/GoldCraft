// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "goldcraft/resource_sync.hpp"
#include <memory>
#include <span>
#include <string>

namespace goldcraft::resource_cache {
// These paths belong to the updater. Original GAME files and PAKs are never
// opened for writing; a new body is committed as an independent file.
class Cache {
public:
    explicit Cache(std::string root);
    const std::string& Root() const;
    std::string Path(const std::string& name, const resource_sync::Entry& expected) const;
    bool Read(const std::string& name, const resource_sync::Entry& expected,
              std::span<std::uint8_t> out, std::uint32_t offset) const;
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
