// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace csol::skillfx
{
inline constexpr char MessageName[] = "CsoSkFx";
inline constexpr unsigned Version = 1;
enum class Kind : std::uint8_t { Model = 1, Radio = 2, World = 3, ManagedWorld = 4, RemoveWorld = 5 };
// Public wire values. This contains no SDK pointer, native enum or private
// CSO structure. Model indices are resolved by the current map's precache.
struct ModelEffect
{
    std::array<float, 3> origin{};
    std::uint16_t model{}, owner{}, excludedOwner{};
    std::uint16_t sequence{}, frameMax{256};
    float life{.4f}, frameRate{1}, alpha{200}, fadeSpeed{8}, scale{1}, fadeIn{.4f};
    std::uint8_t renderMode{5};
};
inline bool Valid(const ModelEffect& value)
{
    if (!value.model || value.owner > 32 || value.excludedOwner > 32 || value.sequence > 4095 || value.renderMode > 5 ||
        value.life < 0 || value.life > 60 || value.frameRate < 0 || value.frameRate > 100 ||
        value.alpha < 0 || value.alpha > 255 || value.fadeSpeed < 0 || value.fadeSpeed > 100 ||
        value.scale < 0 || value.scale > 100 || value.fadeIn < 0 || value.fadeIn > 60) return false;
    for (float f : value.origin) if (!std::isfinite(f) || std::abs(f) > 32768) return false;
    return std::isfinite(value.life) && std::isfinite(value.frameRate) && std::isfinite(value.alpha) &&
        std::isfinite(value.fadeSpeed) && std::isfinite(value.scale) && std::isfinite(value.fadeIn);
}
inline std::vector<std::uint8_t> Encode(const ModelEffect& value)
{
    if (!Valid(value)) return {};
    std::vector<std::uint8_t> bytes{Version, static_cast<std::uint8_t>(Kind::Model)};
    const auto word = [&](unsigned value) { bytes.push_back(value & 255); bytes.push_back((value >> 8) & 255); };
    const auto real = [&](float value) { const auto bits = std::bit_cast<std::uint32_t>(value); word(bits); word(bits >> 16); };
    for (float f : value.origin) real(f);
    word(value.model); word(value.owner); word(value.excludedOwner); word(value.sequence); word(value.frameMax);
    real(value.life); real(value.frameRate); real(value.alpha); real(value.fadeSpeed); real(value.scale); real(value.fadeIn);
    bytes.push_back(value.renderMode);
    return bytes;
}
inline bool Decode(std::span<const std::uint8_t> bytes, ModelEffect& output)
{
    if (bytes.size() != 49 || bytes[0] != Version || bytes[1] != static_cast<std::uint8_t>(Kind::Model)) return false;
    unsigned at = 2;
    const auto word = [&]() { unsigned value = bytes[at] | unsigned(bytes[at + 1]) << 8; at += 2; return value; };
    const auto real = [&]() { const auto lo = word(); const auto hi = word(); return std::bit_cast<float>(lo | (hi << 16)); };
    ModelEffect candidate;
    for (float& f : candidate.origin) f = real();
    candidate.model = word(); candidate.owner = word(); candidate.excludedOwner = word(); candidate.sequence = word(); candidate.frameMax = word();
    candidate.life = real(); candidate.frameRate = real(); candidate.alpha = real(); candidate.fadeSpeed = real();
    candidate.scale = real(); candidate.fadeIn = real(); candidate.renderMode = bytes[at];
    if (!Valid(candidate)) return false;
    output = candidate; return true;
}
// TE145's supported world-effect options have explicit portable meanings.
// Do not send CSO TEMPENTITY flags to the stock client: its layout differs.
enum WorldFlag : std::uint8_t
{ Loop = 1, GroundFollow = 2, NoInterpolation = 4, HideLocalFirstPerson = 8, Persist = 16,
  Indefinite = 32, LocalFirstPersonViewOrigin = 64 };
struct WorldEffect
{
    ModelEffect model;
    std::array<float, 3> angles{};
    std::uint8_t flags{};
};
inline constexpr std::size_t WorldBytes = 62;
inline bool Valid(const WorldEffect& value)
{
    if (!Valid(value.model) || (value.flags & ~(Loop | GroundFollow | NoInterpolation | HideLocalFirstPerson | Persist | Indefinite | LocalFirstPersonViewOrigin)) ||
        ((value.flags & (GroundFollow | HideLocalFirstPerson | LocalFirstPersonViewOrigin)) && !value.model.owner)) return false;
    // Original negative TE145 lifetime sets die=FLT_MAX. Keep the ordinary
    // model packet finite and encode that meaning explicitly for world effects.
    if ((value.flags & Indefinite) && (value.model.life != 0 || value.model.fadeIn != 0 || value.model.fadeSpeed != 0)) return false;
    for (float angle : value.angles)
        if (!std::isfinite(angle) || std::abs(angle) > 360) return false;
    return true;
}
inline std::vector<std::uint8_t> Encode(const WorldEffect& value)
{
    if (!Valid(value)) return {};
    auto bytes = Encode(value.model);
    bytes[1] = static_cast<std::uint8_t>(Kind::World);
    for (float angle : value.angles)
    {
        const auto bits = std::bit_cast<std::uint32_t>(angle);
        for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back((bits >> shift) & 255);
    }
    bytes.push_back(value.flags);
    return bytes;
}
inline bool Decode(std::span<const std::uint8_t> bytes, WorldEffect& output)
{
    if (bytes.size() != WorldBytes || bytes[0] != Version ||
        bytes[1] != static_cast<std::uint8_t>(Kind::World)) return false;
    std::array<std::uint8_t, 49> model;
    std::copy_n(bytes.begin(), model.size(), model.begin());
    model[1] = static_cast<std::uint8_t>(Kind::Model);
    WorldEffect candidate;
    if (!Decode(model, candidate.model)) return false;
    unsigned at = model.size();
    for (float& angle : candidate.angles)
    {
        std::uint32_t bits{};
        for (unsigned shift = 0; shift < 32; shift += 8) bits |= std::uint32_t(bytes[at++]) << shift;
        angle = std::bit_cast<float>(bits);
    }
    candidate.flags = bytes[at];
    if (!Valid(candidate)) return false;
    output = candidate;
    return true;
}
struct ManagedWorldEffect
{
    WorldEffect world;
    std::uint32_t id{};
    std::uint16_t skin{};
};
inline bool Valid(const ManagedWorldEffect& effect)
{
    return effect.id && effect.skin <= 4095 && Valid(effect.world) && effect.world.model.owner &&
        effect.world.model.fadeIn == 0 && effect.world.model.fadeSpeed == 0;
}
inline std::vector<std::uint8_t> Encode(const ManagedWorldEffect& effect)
{
    if (!Valid(effect)) return {};
    auto bytes = Encode(effect.world);
    bytes[1] = static_cast<std::uint8_t>(Kind::ManagedWorld);
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back((effect.id >> shift) & 255);
    bytes.push_back(effect.skin & 255); bytes.push_back(effect.skin >> 8);
    return bytes;
}
inline bool Decode(std::span<const std::uint8_t> bytes, ManagedWorldEffect& output)
{
    if (bytes.size() != WorldBytes + 6 || bytes[0] != Version ||
        bytes[1] != static_cast<std::uint8_t>(Kind::ManagedWorld)) return false;
    std::array<std::uint8_t, WorldBytes> world;
    std::copy_n(bytes.begin(), world.size(), world.begin());
    world[1] = static_cast<std::uint8_t>(Kind::World);
    ManagedWorldEffect next;
    if (!Decode(world, next.world)) return false;
    for (unsigned i = 0; i < 4; ++i) next.id |= std::uint32_t(bytes[WorldBytes + i]) << (8 * i);
    next.skin = bytes[WorldBytes + 4] | std::uint16_t(bytes[WorldBytes + 5]) << 8;
    if (!Valid(next)) return false;
    output = next;
    return true;
}
inline std::vector<std::uint8_t> EncodeRemoveWorld(std::uint32_t id)
{
    if (!id) return {};
    std::vector<std::uint8_t> bytes{Version, static_cast<std::uint8_t>(Kind::RemoveWorld)};
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back((id >> shift) & 255);
    return bytes;
}
inline bool DecodeRemoveWorld(std::span<const std::uint8_t> bytes, std::uint32_t& output)
{
    if (bytes.size() != 6 || bytes[0] != Version ||
        bytes[1] != static_cast<std::uint8_t>(Kind::RemoveWorld)) return false;
    std::uint32_t id{};
    for (unsigned i = 0; i < 4; ++i) id |= std::uint32_t(bytes[2 + i]) << (8 * i);
    if (!id) return false;
    output = id;
    return true;
}
// Original client02A42253..02A42332. Fade-in is measured against remaining
// lifetime; it is not a scale animation. Integer conversion truncates.
inline int Alpha(const ModelEffect& effect, float remaining)
{
    if (remaining < 0)
        return effect.fadeSpeed > 0 ? static_cast<int>((1.0f + remaining * effect.fadeSpeed) * effect.alpha) : 0;
    if (effect.fadeIn > 0 && remaining > 0)
        return std::min(255, static_cast<int>((effect.fadeIn - remaining) / effect.fadeIn * effect.alpha));
    return static_cast<int>(effect.alpha);
}
inline bool ValidRadioPath(std::string_view path)
{
    if (!path.starts_with("vox/") || !path.ends_with(".wav") || path.size() > 150 || path.find("..") != path.npos) return false;
    for (char c : path) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
        !(c >= '0' && c <= '9') && c != '/' && c != '_' && c != '-' && c != '.') return false;
    return true;
}
inline std::string RadioKey(std::string_view source)
{
    if (source.starts_with("buffclass")) source.remove_prefix(9);
    if (source == "19s1ct") return "19s1woman";
    if (source == "19s1tr") return "19s1man";
    if (source == "19s2ct") return "19s2man";
    if (source == "19s2tr") return "19s2woman";
    return std::string(source);
}
inline std::string ExpandRadio(std::string_view pattern, std::string_view source)
{
    constexpr std::string_view marker = "$buffclassname";
    const auto offset = pattern.find(marker);
    if (offset == pattern.npos) return ValidRadioPath(pattern) ? std::string(pattern) : std::string{};
    if (!source.starts_with("buffclass")) return {};
    std::string path(pattern);
    path.replace(offset, marker.size(), source);
    return ValidRadioPath(path) ? path : std::string{};
}
inline std::vector<std::uint8_t> EncodeRadio(std::string_view path)
{
    if (!ValidRadioPath(path)) return {};
    std::vector<std::uint8_t> bytes{Version, static_cast<std::uint8_t>(Kind::Radio)};
    bytes.insert(bytes.end(), path.begin(), path.end()); bytes.push_back(0); return bytes;
}
}
