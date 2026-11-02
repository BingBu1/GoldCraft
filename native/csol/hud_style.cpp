// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud_style.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>

namespace csol::hud_style
{
namespace
{
void Require(bool condition)
{ if (!condition) throw std::runtime_error("invalid CSOL HUD catalog"); }
std::string Text(const rapidjson::Value& object, const char* name, unsigned limit)
{
    Require(object.IsObject() && object.HasMember(name));
    const auto& value = object[name];
    Require(value.IsString() && value.GetStringLength() <= limit);
    std::string text(value.GetString(), value.GetStringLength());
    Require(text.find('\0') == text.npos);
    return text;
}
std::string Image(const rapidjson::Value& object, const char* name, bool optional = false)
{
    auto path = Text(object, name, 63);
    if (path.empty() && optional) return path;
    Require(path.starts_with("resource/csol/hud/v1/") && path.ends_with(".tga") &&
        path.find("..") == path.npos && path.find_first_of(":\\\"\r\n\t ") == path.npos);
    return path;
}
bool Scalar(std::span<const std::byte> bytes, unsigned legacySize, int& result)
{
    if (bytes.size() != legacySize && bytes.size() != 4) return false;
    std::uint32_t value{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        value |= std::uint32_t(std::to_integer<unsigned>(bytes[i])) << (8 * i);
    if (value > std::uint32_t(std::numeric_limits<int>::max())) return false;
    result = int(value);
    return true;
}
}
Catalog ParseCatalog(std::string_view json)
{
    Require(!json.empty() && json.size() <= 1024 * 1024);
    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    Require(!document.HasParseError() && document.IsObject() && document.HasMember("version") &&
        document["version"].IsInt() && document["version"].GetInt() == 1);
    for (const char* key : {"weapons", "portraits"})
        Require(document.HasMember(key) && document[key].IsArray() && document[key].Size() <= 1024);
    Catalog result;
    for (const auto& row : document["weapons"].GetArray())
    {
        auto name = Text(row, "name", 63);
        Require(name.starts_with("weapon_") && name.size() > 7 &&
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == name.npos);
        WeaponArt art{Text(row,"title",192),Image(row,"weapon"),Image(row,"ammo",true),Image(row,"ammo2",true)};
        Require(!art.title.empty() && art.title.find_first_of("\r\n\t") == art.title.npos);
        Require(result.weapons.emplace(std::move(name), std::move(art)).second);
    }
    for (const auto& row : document["portraits"].GetArray())
    {
        Require(row.IsObject() && row.HasMember("id") && row["id"].IsUint());
        const auto id = row["id"].GetUint();
        Require(id > 0 && id <= 65535);
        Require(result.portraits.emplace(id, Image(row,"path")).second);
    }
    Require(!result.weapons.empty());
    return result;
}
std::vector<std::string> ResourcePaths(const Catalog& catalog)
{
    std::set<std::string> paths{CatalogPath,FontPath};
    for(const auto& resource:Resources)paths.emplace(resource.path);
    for(const auto& [name,weapon]:catalog.weapons)
    {
        paths.emplace(weapon.weapon);
        if(!weapon.ammo.empty())paths.emplace(weapon.ammo);
        if(!weapon.ammo2.empty())paths.emplace(weapon.ammo2);
    }
    for(const auto& [id,path]:catalog.portraits)paths.emplace(path);
    return {paths.begin(),paths.end()};
}
bool Vitals::Health(std::span<const std::byte> bytes)
{ if (!Scalar(bytes,1,health)) return false; healthKnown=true; return true; }
bool Vitals::Armor(std::span<const std::byte> bytes)
{ if (!Scalar(bytes,2,armor)) return false; armorKnown=true; return true; }
Layout MakeLayout(int width, int height, float userScale)
{
    if (width <= 0 || height <= 0) return {};
    if (!std::isfinite(userScale)) userScale = 1;
    const float fit = std::min(float(width)/640, float(height)/360);
    const float scale = std::min(fit, std::min(float(width)/1280, float(height)/720) *
        std::clamp(userScale, .5f, 2.f));
    // Original client panel owner retains an eight-unit bottom margin.
    return {scale, {0,height-93*scale,288*scale,85*scale},
        {0,height-156*scale,99*scale,116*scale},
        {width-288*scale,height-84*scale,288*scale,76*scale},
        {width-174*scale,height-149*scale,170*scale,61*scale}};
}
}
