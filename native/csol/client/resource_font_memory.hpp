// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace csol::client {
struct ResourceFontMemory {
    void** owner{};
    void* handle{};
    std::string resource,family;
    std::shared_ptr<const std::vector<std::byte>> bytes;
};
std::vector<ResourceFontMemory> SnapshotResourceFonts();
bool RemoveResourceFonts(std::span<const ResourceFontMemory>,std::string& error);
bool InstallResourceFont(ResourceFontMemory&,std::shared_ptr<const std::vector<std::byte>>,std::string& error);
}
