// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_damage.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cmath>

namespace guest::cof
{
namespace
{
constexpr std::size_t MaxBytes = 64 * 1024;
bool DamageValue(const rapidjson::Value& value, float& result)
{
    if (!value.IsNumber()) return false;
    const double number = value.GetDouble();
    if (!std::isfinite(number) || number < 0 || number > 1'000'000) return false;
    result = static_cast<float>(number);
    return true;
}
}

bool ScenarioDamageConfig::Parse(std::string_view json, std::string& error)
{
    error.clear();
    const auto reject = [&](const char* message) { error = message; return false; };
    if (json.empty() || json.size() > MaxBytes || json.find('\0') != json.npos)
        return reject("CoF scenario damage must contain 1..65536 bytes");

    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject() || document.MemberCount() != 2 ||
        !document.HasMember("version") || !document["version"].IsInt() ||
        document["version"].GetInt() < 1 || document["version"].GetInt() > 4)
        return reject("Invalid CoF scenario damage envelope");

    const bool legacy=document["version"].GetInt()==1;
    const char* group=legacy?"pistols":"firearms";
    if(!document.HasMember(group)||!document[group].IsObject())
        return reject("Missing explicit CoF damage group");
    const auto& entries=document[group];
    const std::size_t required=legacy?PistolKeys.size():document["version"].GetInt()==2?
        static_cast<unsigned>(Firearm::MP5):document["version"].GetInt()==3?
        static_cast<unsigned>(Firearm::M16):FirearmKeys.size();
    if(entries.MemberCount()!=required)
        return reject("CoF damage requires every declared weapon exactly once");
    std::array<ScenarioDamage,FirearmKeys.size()> prepared{};
    std::uint32_t available{};
    for(auto it=entries.MemberBegin();it!=entries.MemberEnd();++it)
    {
        const std::string_view key(it->name.GetString(),it->name.GetStringLength());
        const auto end=FirearmKeys.begin()+required;
        const auto found=std::find(FirearmKeys.begin(),end,key);
        if(found==end)return reject("Unknown CoF firearm damage identity");
        const auto index=static_cast<std::size_t>(found-FirearmKeys.begin());
        const auto bit=1u<<index;
        if(available&bit)return reject("Duplicate CoF firearm damage identity");
        available|=bit;
        const auto& entry=it->value;
        if(!entry.IsObject()||(entry.MemberCount()!=2&&entry.MemberCount()!=3)||
            !entry.HasMember("shot")||!entry.HasMember("melee")||
            !DamageValue(entry["shot"],prepared[index].shot)||
            !DamageValue(entry["melee"],prepared[index].melee))
            return reject("Each CoF firearm requires finite shot and melee damage in 0..1000000");
        if(entry.MemberCount()==3)
        {
            if(index!=static_cast<std::size_t>(Firearm::Revolver)||!entry.HasMember("penetration")||
                !entry["penetration"].IsInt()||entry["penetration"].GetInt()<0||entry["penetration"].GetInt()>32)
                return reject("Only Revolver supports an explicit CS penetration count in 0..32");
            prepared[index].penetration=entry["penetration"].GetInt();
        }
    }

    values_ = prepared;
    available_ = available;
    loaded_ = true;
    return true;
}

const ScenarioDamage* ScenarioDamageConfig::Find(Pistol weapon) const noexcept
{
    const auto index=static_cast<unsigned>(weapon);
    return index<PistolKeys.size()?Find(static_cast<Firearm>(index)):nullptr;
}
const ScenarioDamage* ScenarioDamageConfig::Find(Firearm weapon) const noexcept
{
    const auto index=static_cast<unsigned>(weapon);
    return loaded_&&index<values_.size()&&(available_&(1u<<index))?&values_[index]:nullptr;
}
}
