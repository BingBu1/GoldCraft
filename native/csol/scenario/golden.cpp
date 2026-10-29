// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden.hpp"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

namespace csol::scenario
{
bool GoldenResources::Load(ResourceFileApi files, ResourceError& error)
{
    constexpr const char* path = "maps/ZBS_GoldenZombie_dedi.json";
    if (loading_ || state_ != GoldenConfiguration::Unloaded || !files.load || !files.release)
    { error = {path, {0, "Golden configuration requires a fresh owner and file API"}}; return false; }
    loading_ = true;
    struct Loading { bool& value; ~Loading() { value = false; } } loading{loading_};
    int length{};
    auto* allocation = files.load(path, &length);
    if (!allocation)
    { state_ = GoldenConfiguration::Missing; return true; }
    std::string bytes;
    {
        struct Allocation
        {
            unsigned char* bytes;
            void (*release)(void*);
            ~Allocation() { release(bytes); }
        } owner{allocation, files.release};
        if (length > 0) bytes.assign(reinterpret_cast<const char*>(allocation), static_cast<std::size_t>(length));
    }
    if (length < 0)
    { error = {path, {0, "Golden file API returned a negative length"}}; return false; }
    if (bytes.empty())
    { state_ = GoldenConfiguration::Empty; return true; }
    rapidjson::Document document;
    document.Parse(bytes.data(), bytes.size());
    if (document.HasParseError())
    {
        // Original exceptions are logged and return to the caller; a fresh
        // empty owner is retained. This is not a successful nonempty parse.
        state_ = GoldenConfiguration::ParseError;
        diagnostic_ = rapidjson::GetParseError_En(document.GetParseError());
        return true;
    }
    if (document.IsNull() || (document.IsObject() && !document.HasMember("ZBS_GoldenZombie")))
    { state_ = GoldenConfiguration::Empty; return true; }
    if (document.IsObject())
    {
        const auto& root = document["ZBS_GoldenZombie"];
        // The original parser returns before any writes if monster_info is
        // absent. This class deliberately does not implement hot reload.
        if (root.IsObject() && !root.HasMember("monster_info"))
        { state_ = GoldenConfiguration::Empty; return true; }
    }
    state_ = GoldenConfiguration::Unsupported;
    error = {path, {0, "nonempty Golden configuration requires its plan and replacement services"}};
    return false;
}

bool GoldenResources::EmptyPlan() const
{
    return state_ == GoldenConfiguration::Missing || state_ == GoldenConfiguration::Empty ||
        state_ == GoldenConfiguration::ParseError;
}
}
