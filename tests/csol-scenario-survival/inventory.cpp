// SPDX-License-Identifier: GPL-3.0-or-later
// Offline inventory through the actual type decoder and appearance catalog.
// A listed model/skill is not evidence that a map's gameplay is implemented.
#include "../../native/csol/scenario/monsters/monster_types.hpp"
#include "../../native/csol/scenario/monsters/monster_appearances.hpp"
#include "config_codec.hpp"
#include "../../native/csol/server/scenario/monsters/scenario_monster_resources.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>

int main(int argc,char** argv)
{
    try
    {
        if(argc!=3)throw std::runtime_error("usage: survival_inventory common-root map-name");
        const std::string map=argv[2];
        if(map.empty()||map.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=map.npos)
            throw std::runtime_error("invalid map name");
        const auto path=std::filesystem::path(argv[1])/"maps"/("ZBS_ZT_"+map+".cso");
        std::ifstream input(path,std::ios::binary);
        if(!input)throw std::runtime_error("missing monster type table");
        std::string bytes{std::istreambuf_iterator<char>(input),{}};
        csol::scenario::MonsterTypeTable types;csol::scenario::ConfigError error;
        if(!csol::DecodeConfiguration(std::as_writable_bytes(std::span(bytes))))
            throw std::runtime_error("incomplete encrypted monster type table");
        while(!bytes.empty()&&bytes.back()=='\0')bytes.pop_back();
        if(!csol::scenario::ReadMonsterTypes(bytes,types,error))
            throw std::runtime_error(error.message);
        std::set<std::string> models{CSOLScenarioInitialMonsterModel},sounds;
        std::cout<<"{\"map\":"<<std::quoted(map)<<",\"sourceRows\":"<<types.rows.size()<<",\"types\":[";
        bool separator{};
        for(const auto& [id,index]:types.byId)
        {
            const auto& type=types.rows.at(index);
            const auto* appearance=csol::scenario::FindMonsterAppearance(type.appearance);
            if(!appearance)throw std::runtime_error("unresolved appearance "+type.appearance);
            models.emplace(appearance->model);
            for(const auto sound:{appearance->deathSound,appearance->ambientSound})
                if(!sound.empty())sounds.emplace("sound/"+std::string(sound));
            if(appearance->name=="zombiedog")sounds.emplace("sound/zombi/zombiedog_howls.wav");
            if(separator)std::cout<<',';
            separator=true;
            std::cout<<"{\"id\":"<<id<<",\"row\":"<<index<<",\"skill\":"<<type.skill<<",\"appearance\":"
                <<std::quoted(type.appearance)<<",\"model\":"<<std::quoted(std::string(appearance->model))<<'}';
        }
        std::cout<<"]";
        for(const auto& [name,entries]:{std::pair{"models",&models},std::pair{"sounds",&sounds}})
        {
            std::cout<<",\""<<name<<"\":[";separator=false;
            for(const auto& entry:*entries){if(separator)std::cout<<',';separator=true;std::cout<<std::quoted(entry);}
            std::cout<<']';
        }
        std::cout<<"}\n";
        return std::cout?0:1;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
