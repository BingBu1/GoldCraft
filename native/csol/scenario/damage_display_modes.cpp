// SPDX-License-Identifier: GPL-3.0-or-later
#include "damage_display_modes.hpp"
#include "../config_codec.hpp"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <algorithm>
#include <utility>

namespace csol::scenario
{
namespace
{
const rapidjson::Value* UniqueMember(const rapidjson::Value& object, std::string_view name)
{
    const rapidjson::Value* result{};
    for (const auto& member : object.GetObject())
    {
        if (std::string_view(member.name.GetString(), member.name.GetStringLength()) != name) continue;
        if (result) return nullptr;
        result = &member.value;
    }
    return result;
}
}

bool DamageDisplayModes::Contains(std::int32_t mode) const
{
    // Preserve original int32 equality and list order; there is no mode-range
    // clamp, sort, de-duplication or implicit allowance for scenario mode15.
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

bool ReadDamageDisplayModes(std::string_view json, DamageDisplayModes& output, ConfigError& error)
{
    error = {};
    const auto fail = [&](const char* message) { error.message = message; return false; };
    if (json.empty() || json.size() > 8 * 1024 * 1024 || json.find('\0') != json.npos)
        return fail("invalid floating damage configuration size or embedded NUL");
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    rapidjson::Document document;
    // The common resource contains CP949 comments. Only ASCII keys and int32
    // modes are consumed here; no translated or lossy copy is required.
    document.Parse<rapidjson::kParseCommentsFlag>(json.data(), json.size());
    if (document.HasParseError())
    {
        error = {document.GetErrorOffset(), rapidjson::GetParseError_En(document.GetParseError())};
        return false;
    }
    if (!document.IsObject()) return fail("floating damage root must be an object");
    const auto* root = UniqueMember(document, "FLOATING_DMG_STYLE");
    if (!root || !root->IsObject())
        return fail("floating damage style configuration is missing or invalid");
    const auto* modes = UniqueMember(*root, "MODE");
    if (!modes || !modes->IsArray())
        return fail("floating damage mode list is missing or invalid");
    DamageDisplayModes candidate;
    candidate.modes.reserve(modes->Size());
    for (const auto& value : modes->GetArray())
    {
        // The original typed conversion's malformed-input behavior is not a
        // host contract. Reject non-int32 values instead of guessing coercion.
        if (!value.IsInt()) return fail("floating damage mode must be a signed 32-bit integer");
        candidate.modes.push_back(value.GetInt());
    }
    output = std::move(candidate);
    return true;
}

bool ReadDamageDisplayModesResource(std::span<const std::byte> encoded,
    DamageDisplayModes& output, ConfigError& error)
{
    error = {};
    if (encoded.empty() || encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    { error.message = "invalid encrypted floating damage configuration size"; return false; }
    std::string plain(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    return ReadDamageDisplayModes(plain, output, error);
}

bool DamageDisplayModeResources::Load(ResourceFileApi files, ResourceError& error)
{
    error = {};
    if (loading_)
    { error = {DamageDisplayModesPath, {0, "floating damage resource loader is busy"}}; return false; }
    struct Loading { bool& value; ~Loading() { value = false; } } scope{loading_};
    loading_ = true;
    table_.reset();
    if (!files.load || !files.release)
    { error = {DamageDisplayModesPath, {0, "floating damage resource file API is unavailable"}}; return false; }
    auto candidate = std::make_shared<DamageDisplayModes>();
    bool parsed{};
    ConfigError diagnostic;
    {
        int length{};
        std::unique_ptr<unsigned char, decltype(files.release)> bytes(
            files.load(DamageDisplayModesPath, &length), files.release);
        if (!bytes || length <= 0 || length > 8 * 1024 * 1024 || length % 8)
            diagnostic.message = "missing or invalid encrypted floating damage resource";
        else
            parsed = ReadDamageDisplayModesResource(std::as_bytes(std::span(bytes.get(),
                static_cast<std::size_t>(length))), *candidate, diagnostic);
        // Keep the loading guard through the engine's release callback, and
        // publish only after it returns. A recursive Load/Clear cannot install
        // or erase a table halfway through this operation.
    }
    // Restore this operation's diagnostic after the release callback; a
    // recursive request may have used the same caller-owned error object.
    if (!parsed) { error = {DamageDisplayModesPath, std::move(diagnostic)}; return false; }
    error = {};
    table_ = std::move(candidate);
    return true;
}

bool DamageDisplayModeResources::Clear()
{
    if (loading_) return false;
    table_.reset();
    return true;
}
}
