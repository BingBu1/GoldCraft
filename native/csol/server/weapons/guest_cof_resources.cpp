// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_resources.hpp"

#include <charconv>
#include <cmath>
#include <cstring>
#include <span>
#include <utility>

#pragma clang fp contract(off)

namespace csol::server::cof
{
namespace
{
class Tokens
{
public:
    explicit Tokens(std::string_view input) noexcept : input_(input) {}
    bool Next(std::string_view& token) noexcept
    {
        for (;;)
        {
            while (at_ < input_.size() && static_cast<unsigned char>(input_[at_]) <= ' ') ++at_;
            if (at_ + 1 < input_.size() && input_[at_] == '/' && input_[at_ + 1] == '/')
            { while (at_ < input_.size() && input_[at_] != '\n') ++at_; }
            else break;
        }
        if (at_ == input_.size()) return false;
        const auto start = at_++;
        if (input_[start] == '{' || input_[start] == '}') token = input_.substr(start, 1);
        else if (input_[start] == '"')
        {
            const auto value = at_;
            while (at_ < input_.size() && input_[at_] != '"')
            {
                if (input_[at_] == '\0' || input_[at_] == '\n' || input_[at_] == '\r')
                { failed_ = true; return false; }
                ++at_;
            }
            if (at_ == input_.size()) { failed_ = true; return false; }
            token = input_.substr(value, at_++ - value);
        }
        else
        {
            while (at_ < input_.size() && static_cast<unsigned char>(input_[at_]) > ' ' &&
                   input_[at_] != '{' && input_[at_] != '}' && input_[at_] != '"') ++at_;
            token = input_.substr(start, at_ - start);
        }
        return true;
    }
    bool End() const noexcept { return !failed_ && at_ == input_.size(); }
private:
    std::string_view input_;
    std::size_t at_{};
    bool failed_{};
};

bool Metadata(std::string_view text, P345Resources& value) noexcept
{
    if (text.empty() || text.size() > 16384 || text.find('\0') != text.npos) return false;
    Tokens tokens(text);
    std::string_view key, token, data;
    unsigned found{};
    while (tokens.Next(key))
    {
        if (key.empty() || key == "{" || key == "}" || !tokens.Next(token) || token != "{" ||
            !tokens.Next(token) || token != "data" || !tokens.Next(data) || data.empty() ||
            !tokens.Next(token) || token != "}") return false;
        unsigned bit{};
        if (key == "classname") bit = 16;
        else if (key == "ironsight_forward") bit = 1;
        else if (key == "ironsight_right") bit = 2;
        else if (key == "ironsight_up") bit = 4;
        else if (key == "ironsight_zoom") bit = 8;
        if (!bit) continue;
        if (found & bit) return false;
        found |= bit;
        if (bit == 16) { if (data != "weapon_p345") return false; }
        else if (bit == 8)
        {
            int number{};
            const auto parsed = std::from_chars(data.data(), data.data() + data.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != data.data() + data.size() ||
                number < 0 || number > 255) return false;
            value.aimZoom = number;
        }
        else
        {
            double number{};
            const auto parsed = std::from_chars(data.data(), data.data() + data.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != data.data() + data.size() ||
                !std::isfinite(number) || !std::isfinite(static_cast<float>(number))) return false;
            // Original atof returns double, then fstp stores float.
            const float field = static_cast<float>(number);
            if (bit == 1) value.aimOffset.x = field;
            else if (bit == 2) value.aimOffset.y = field;
            else value.aimOffset.z = field;
        }
    }
    return tokens.End() && found == 31;
}

struct File
{
    byte* bytes{};
    int size{};
    void (*release)(void*){};
    ~File() { if (bytes) release(bytes); }
};
}

ResourceResult PrepareP345Resources(const ResourceAuthority& authority, P345Resources& output) noexcept
{
    const auto lease = authority;
    const auto current = [&] { return lease.current(lease.context); };
    if (!lease.current || !lease.generation) return {ResourceStatus::InvalidAuthority};
    if (!current()) return {ResourceStatus::Cancelled};
    if (!g_engfuncs.pfnGetFileSize || !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile ||
        !g_engfuncs.pfnPrecacheModel || !g_engfuncs.pfnPrecacheSound ||
        !g_engfuncs.pfnPrecacheEvent || !g_engfuncs.pfnPrecacheGeneric || !g_engfuncs.pfnAllocString)
        return {ResourceStatus::MissingEngine};

    // Existence preflight uses the normal VFS, including PAK entries. Reject
    // the entire static closure before engine precache can fatal on a missing file.
    const auto exists = [&](const char* path) {
        const int size = g_engfuncs.pfnGetFileSize(path);
        return current() && size > 0;
    };
    for (const auto& p : P345Models)
        if (!exists(p.host)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    for (const auto& p : P345Sounds)
    {
        std::array<char, 128> full{};
        std::memcpy(full.data(), "sound/", 6);
        std::memcpy(full.data() + 6, p.host, std::strlen(p.host) + 1);
        if (!exists(full.data())) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    }
    for (const auto& p : P345LocalWaves)
        if (!exists(p.host)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    for (const auto& p : P345Events)
        if (!exists(p.host)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    for (const auto* p : {P345Inventory.host, P345Particle.host})
        if (!exists(p)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p};

    P345Resources value{};
    value.generation = lease.generation;
    {
        File file{nullptr, 0, g_engfuncs.pfnFreeFile};
        file.bytes = g_engfuncs.pfnLoadFileForMe(P345Inventory.host, &file.size);
        if (!current()) return {ResourceStatus::Cancelled, P345Inventory.host};
        if (!file.bytes || file.size <= 0 || !Metadata(
            {reinterpret_cast<const char*>(file.bytes), static_cast<std::size_t>(file.size)}, value))
            return {ResourceStatus::InvalidMetadata, P345Inventory.host};
    }
    if (!current()) return {ResourceStatus::Cancelled};
    {
        const auto* path = P345Models[static_cast<unsigned>(P345Model::View)].host;
        File file{nullptr, 0, g_engfuncs.pfnFreeFile};
        file.bytes = g_engfuncs.pfnLoadFileForMe(path, &file.size);
        if (!current()) return {ResourceStatus::Cancelled, path};
        if (!file.bytes || file.size <= 0 || guest::cof::PrepareMagazineSequenceTimes(
            {reinterpret_cast<const std::byte*>(file.bytes), static_cast<std::size_t>(file.size)},
            34, value.sequences) != guest::cof::MagazineModelResult::Prepared)
            return {ResourceStatus::InvalidModel, path};
    }
    if (!current()) return {ResourceStatus::Cancelled};

    for (std::size_t i = 0; i < P345Models.size(); ++i)
    {
        value.models[i] = g_engfuncs.pfnPrecacheModel(P345Models[i].host);
        if (!current()) return {ResourceStatus::Cancelled, P345Models[i].host};
        if (value.models[i] <= 0) return {ResourceStatus::EngineRejected, P345Models[i].host};
    }
    for (std::size_t i = 0; i < P345Sounds.size(); ++i)
    {
        value.sounds[i] = g_engfuncs.pfnPrecacheSound(P345Sounds[i].host);
        if (!current()) return {ResourceStatus::Cancelled, P345Sounds[i].host};
        if (value.sounds[i] <= 0) return {ResourceStatus::EngineRejected, P345Sounds[i].host};
    }
    for (std::size_t i = 0; i < P345LocalWaves.size(); ++i)
    {
        value.localWaveDownloads[i] = g_engfuncs.pfnPrecacheGeneric(P345LocalWaves[i].host);
        if (!current()) return {ResourceStatus::Cancelled, P345LocalWaves[i].host};
        if (value.localWaveDownloads[i] < 0) return {ResourceStatus::EngineRejected, P345LocalWaves[i].host};
    }
    for (std::size_t i = 0; i < P345Events.size(); ++i)
    {
        value.events[i] = g_engfuncs.pfnPrecacheEvent(1, P345Events[i].host);
        if (!current()) return {ResourceStatus::Cancelled, P345Events[i].host};
        if (!value.events[i]) return {ResourceStatus::EngineRejected, P345Events[i].host};
    }
    for (const auto [path, destination] : {
        std::pair{P345Inventory.host, &value.inventoryDownload},
        std::pair{P345Particle.host, &value.particleDownload}})
    {
        *destination = g_engfuncs.pfnPrecacheGeneric(path);
        if (!current()) return {ResourceStatus::Cancelled, path};
        // Actual PF_precache_generic_I/AddGeneric uses zero-based indices;
        // zero also means an existing model/sound already supplies the file.
        if (*destination < 0) return {ResourceStatus::EngineRejected, path};
    }
    for (const auto [path, destination] : {
        std::pair{P345Models[0].host, &value.viewModelString},
        std::pair{P345Models[1].host, &value.worldModelString},
        std::pair{P345Models[2].host, &value.playerModelString},
        std::pair{P345Inventory.host, &value.inventoryItemString},
        std::pair{P345Particle.host, &value.particleScriptString}})
    {
        *destination = g_engfuncs.pfnAllocString(path);
        if (!current()) return {ResourceStatus::Cancelled, path};
        if (*destination <= 0) return {ResourceStatus::EngineRejected, path};
    }
    output = value;
    return {ResourceStatus::Prepared};
}
}
