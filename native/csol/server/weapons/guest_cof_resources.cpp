// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_resources.hpp"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
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

template<class Resources>
bool Metadata(std::string_view text, std::string_view classname, Resources& value) noexcept
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
        if (bit == 16) { if (data != classname) return false; }
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

bool ClipTexture(std::span<const byte> bytes) noexcept
{
    if (bytes.size() < sizeof(studiohdr_t) ||
        bytes.size() > std::size_t(std::numeric_limits<int>::max())) return false;
    studiohdr_t header;
    std::memcpy(static_cast<void*>(&header), bytes.data(), sizeof(header));
    if (std::memcmp(bytes.data(), "IDST", 4) || header.version != 10 ||
        header.length != static_cast<int>(bytes.size()) || header.numtextures <= 0 ||
        header.texturedataindex < int(sizeof(header)) ||
        header.texturedataindex > header.length ||
        header.numskinref <= 0 || header.numskinfamilies <= 0) return false;
    const auto prefix = std::size_t(header.texturedataindex);
    const auto table = [&](int offset, int count, std::size_t stride) {
        return offset >= int(sizeof(header)) && count > 0 &&
            std::size_t(offset) <= prefix &&
            std::size_t(count) <= (prefix - std::size_t(offset)) / stride;
    };
    if (!table(header.textureindex, header.numtextures, sizeof(mstudiotexture_t)) ||
        !table(header.skinindex, header.numskinref,
            std::size_t(header.numskinfamilies) * sizeof(short))) return false;
    const auto skins = std::size_t(header.numskinref) * std::size_t(header.numskinfamilies);
    for (std::size_t i = 0; i < skins; ++i)
    {
        unsigned short texture;
        std::memcpy(&texture, bytes.data() + header.skinindex + i * sizeof(texture), sizeof(texture));
        if (texture >= header.numtextures) return false;
    }
    // ReHLDS l_studio.cpp reads pixels and 256 RGB palette entries sequentially
    // from texturedataindex. The client also consumes each descriptor's index.
    auto cursor = prefix;
    for (int i = 0; i < header.numtextures; ++i)
    {
        mstudiotexture_t texture;
        std::memcpy(&texture, bytes.data() + header.textureindex +
            std::size_t(i) * sizeof(texture), sizeof(texture));
        if (texture.width <= 0 || texture.height <= 0 ||
            !std::memchr(texture.name, '\0', sizeof(texture.name)) ||
            std::size_t(texture.width) > std::size_t(std::numeric_limits<int>::max()) /
                std::size_t(texture.height)) return false;
        const auto pixels = std::size_t(texture.width) * std::size_t(texture.height);
        constexpr std::size_t PaletteBytes = 256 * 3;
        if (cursor > bytes.size() || pixels > bytes.size() - cursor ||
            PaletteBytes > bytes.size() - cursor - pixels ||
            texture.index < header.texturedataindex || std::size_t(texture.index) > bytes.size() ||
            pixels > bytes.size() - std::size_t(texture.index) ||
            PaletteBytes > bytes.size() - std::size_t(texture.index) - pixels) return false;
        cursor += pixels + PaletteBytes;
    }
    return true;
}
template<class Resources, std::size_t Models, std::size_t Sounds, std::size_t Waves, std::size_t Events>
ResourceResult PrepareResources(const ResourceAuthority& authority, Resources& output,
    const std::array<ResourcePath, Models>& models, const std::array<ResourcePath, Sounds>& sounds,
    const std::array<ResourcePath, Waves>& waves, const std::array<ResourcePath, Events>& events,
    const ResourcePath& inventory, const ResourcePath& particle, const ResourcePath& clipTexture,
    unsigned sequenceCount, std::string_view classname) noexcept
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
    for (const auto& p : models)
        if (!exists(p.host)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    for (const auto& p : sounds)
    {
        std::array<char, 128> full{};
        std::memcpy(full.data(), "sound/", 6);
        std::memcpy(full.data() + 6, p.host, std::strlen(p.host) + 1);
        if (!exists(full.data())) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    }
    for (const auto& p : waves)
        if (!exists(p.host)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    for (const auto& p : events)
        if (!exists(p.host)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};
    for (const auto* p : {inventory.host, particle.host})
        if (!exists(p)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p};
    if (!exists(clipTexture.host))
        return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, clipTexture.host};
    if constexpr (requires { output.decalDownloads; })
        for (const auto& p : ShotDecals)
            if (!exists(p.host)) return {current() ? ResourceStatus::MissingFile : ResourceStatus::Cancelled, p.host};

    Resources value{};
    value.generation = lease.generation;
    {
        File file{nullptr, 0, g_engfuncs.pfnFreeFile};
        file.bytes = g_engfuncs.pfnLoadFileForMe(inventory.host, &file.size);
        if (!current()) return {ResourceStatus::Cancelled, inventory.host};
        if (!file.bytes || file.size <= 0 || !Metadata(
            {reinterpret_cast<const char*>(file.bytes), static_cast<std::size_t>(file.size)}, classname, value))
            return {ResourceStatus::InvalidMetadata, inventory.host};
    }
    if (!current()) return {ResourceStatus::Cancelled};
    {
        const auto* path = models[0].host;
        File file{nullptr, 0, g_engfuncs.pfnFreeFile};
        file.bytes = g_engfuncs.pfnLoadFileForMe(path, &file.size);
        if (!current()) return {ResourceStatus::Cancelled, path};
        if (!file.bytes || file.size <= 0 || guest::cof::PrepareMagazineSequenceTimes(
            {reinterpret_cast<const std::byte*>(file.bytes), static_cast<std::size_t>(file.size)},
            sequenceCount, value.sequences) != guest::cof::MagazineModelResult::Prepared)
            return {ResourceStatus::InvalidModel, path};
    }
    if (!current()) return {ResourceStatus::Cancelled};

    {
        File file{nullptr, 0, g_engfuncs.pfnFreeFile};
        file.bytes = g_engfuncs.pfnLoadFileForMe(clipTexture.host, &file.size);
        if (!current()) return {ResourceStatus::Cancelled, clipTexture.host};
        if (!file.bytes || file.size <= 0 || !ClipTexture(
            {file.bytes, static_cast<std::size_t>(file.size)}))
            return {ResourceStatus::InvalidModel, clipTexture.host};
    }
    if (!current()) return {ResourceStatus::Cancelled};

    for (std::size_t i = 0; i < models.size(); ++i)
    {
        value.models[i] = g_engfuncs.pfnPrecacheModel(models[i].host);
        if (!current()) return {ResourceStatus::Cancelled, models[i].host};
        if (value.models[i] <= 0) return {ResourceStatus::EngineRejected, models[i].host};
    }
    for (std::size_t i = 0; i < sounds.size(); ++i)
    {
        value.sounds[i] = g_engfuncs.pfnPrecacheSound(sounds[i].host);
        if (!current()) return {ResourceStatus::Cancelled, sounds[i].host};
        if (value.sounds[i] <= 0) return {ResourceStatus::EngineRejected, sounds[i].host};
    }
    for (std::size_t i = 0; i < waves.size(); ++i)
    {
        value.localWaveDownloads[i] = g_engfuncs.pfnPrecacheGeneric(waves[i].host);
        if (!current()) return {ResourceStatus::Cancelled, waves[i].host};
        if (value.localWaveDownloads[i] < 0) return {ResourceStatus::EngineRejected, waves[i].host};
    }
    if constexpr (requires { value.decalDownloads; })
        for (std::size_t i = 0; i < ShotDecals.size(); ++i)
        {
            value.decalDownloads[i] = g_engfuncs.pfnPrecacheGeneric(ShotDecals[i].host);
            if (!current()) return {ResourceStatus::Cancelled, ShotDecals[i].host};
            if (value.decalDownloads[i] < 0) return {ResourceStatus::EngineRejected, ShotDecals[i].host};
        }
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        value.events[i] = g_engfuncs.pfnPrecacheEvent(1, events[i].host);
        if (!current()) return {ResourceStatus::Cancelled, events[i].host};
        if (!value.events[i]) return {ResourceStatus::EngineRejected, events[i].host};
    }
    for (const auto [path, destination] : {
        std::pair{inventory.host, &value.inventoryDownload},
        std::pair{particle.host, &value.particleDownload}})
    {
        *destination = g_engfuncs.pfnPrecacheGeneric(path);
        if (!current()) return {ResourceStatus::Cancelled, path};
        // Actual PF_precache_generic_I/AddGeneric uses zero-based indices;
        // zero also means an existing model/sound already supplies the file.
        if (*destination < 0) return {ResourceStatus::EngineRejected, path};
    }
    for (const auto [path, destination] : {
        std::pair{models[0].host, &value.viewModelString},
        std::pair{models[1].host, &value.worldModelString},
        std::pair{models[2].host, &value.playerModelString},
        std::pair{inventory.host, &value.inventoryItemString},
        std::pair{particle.host, &value.particleScriptString}})
    {
        *destination = g_engfuncs.pfnAllocString(path);
        if (!current()) return {ResourceStatus::Cancelled, path};
        if (*destination <= 0) return {ResourceStatus::EngineRejected, path};
    }
    output = value;
    return {ResourceStatus::Prepared};
}
}

ResourceResult PrepareP345Resources(const ResourceAuthority& authority, P345Resources& output) noexcept
{
    return PrepareResources(authority, output, P345Models, P345Sounds, P345LocalWaves, P345Events,
        P345Inventory, P345Particle, P345ClipTexture, 34, "weapon_p345");
}

ResourceResult PrepareRevolverResources(const ResourceAuthority& authority, RevolverResources& output) noexcept
{
    // Actual 22 view sequences; cylinder reload remains its separate state machine.
    return PrepareResources(authority, output, RevolverModels, RevolverSounds, RevolverLocalWaves, RevolverEvents,
        RevolverInventory, RevolverParticle, RevolverClipTexture, 22, "weapon_revolver");
}

ResourceResult PrepareGlockResources(const ResourceAuthority& authority, GlockResources& output) noexcept
{
    return PrepareResources(authority, output, GlockModels, GlockSounds, GlockLocalWaves, GlockEvents,
        GlockInventory, GlockParticle, GlockClipTexture, 43, "weapon_glock");
}
}
