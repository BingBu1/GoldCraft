// SPDX-License-Identifier: GPL-3.0-or-later
#include "character_catalog.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <stdexcept>
#include <set>

namespace csol {
std::vector<Character> ParseCharacters(std::string_view json)
{
    if(json.empty() || json.size()>512*1024)throw std::runtime_error("character catalog exceeds budget");
    rapidjson::Document doc;doc.Parse(json.data(),json.size());
    if(doc.HasParseError() || !doc.IsObject() || !doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt()!=1 ||
        !doc.HasMember("characters") || !doc["characters"].IsArray() || doc["characters"].Empty() || doc["characters"].Size()>512)
        throw std::runtime_error("invalid character catalog");
    std::vector<Character> result;std::set<unsigned> ids;
    for(const auto& row:doc["characters"].GetArray())
    {
        if(!row.IsObject())throw std::runtime_error("invalid character record");
        auto number=[&](const char* key,int lo,int hi){if(!row.HasMember(key) || !row[key].IsInt() || row[key].GetInt()<lo || row[key].GetInt()>hi)throw std::runtime_error("invalid character number");return row[key].GetInt();};
        auto flag=[&](const char* key){if(!row.HasMember(key) || !row[key].IsBool())throw std::runtime_error("invalid character flag");return row[key].GetBool();};
        auto text=[&](const char* key,size_t maximum){if(!row.HasMember(key) || !row[key].IsString() || row[key].GetStringLength()>maximum)throw std::runtime_error("invalid character text");return std::string(row[key].GetString(),row[key].GetStringLength());};
        Character c;c.id=number("id",1,65535);c.team=number("team",1,2);c.skin=number("skin",0,255);
        if(row.HasMember("scenarioClass"))c.scenarioClass=number("scenarioClass",0,65535);
        c.male=flag("male");c.allowPlayerStyle=flag("allowPlayerStyle");c.handStyle=number("handStyle",-32768,32767);
        c.name=text("name",128);c.model=text("model",23);c.hand=text("hand",40);c.portrait=text("portrait",63);
        auto identifier=[](std::string_view s){return std::all_of(s.begin(),s.end(),[](unsigned char v){return (v>='a'&&v<='z') || (v>='A'&&v<='Z') || (v>='0'&&v<='9') || v=='_' || v=='-';});};
        if(c.name.empty() || c.name.find('\0')!=c.name.npos || c.model.empty() || !identifier(c.model) || !identifier(c.hand) ||
            (c.handStyle!=0 && c.hand.empty()) || !c.portrait.starts_with("gfx/csol/p/") || !c.portrait.ends_with(".tga") ||
            c.portrait.find("..")!=c.portrait.npos || c.portrait.find_first_of(":\\\"\r\n\t ")!=c.portrait.npos ||
            c.portrait.find('\0')!=c.portrait.npos || !ids.insert(c.id).second)
            throw std::runtime_error("invalid or duplicate character identity");
        result.push_back(std::move(c));
    }
    return result;
}
const Character* FindCharacter(const std::vector<Character>& catalog,unsigned id)
{
    for(const auto& c:catalog)if(c.id==id)return &c;return nullptr;
}
bool CharacterTeamAllowed(const Character& c,int team,bool zombieHuman)
{
    return (team==1 || team==2) && (zombieHuman || c.team==team);
}
}
