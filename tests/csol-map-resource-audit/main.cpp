// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only inventory through the production map, champion and dialogue readers.
#include "../../native/csol/scenario/core/map_resources.hpp"
#include "../../native/csol/scenario/core/map_support.hpp"
#include "../../native/csol/scenario/core/round_settings.hpp"
#include "../../native/csol/scenario/core/scenario_music.hpp"
#include "../../native/csol/scenario/core/dialogue_resources.hpp"
#include "../../native/csol/scenario/core/scenario_round_hud.hpp"
#include "../../native/csol/scenario/monsters/champions.hpp"
#include "../../native/csol/scenario/monsters/monster_appearances.hpp"
#include "../../native/csol/scenario/bosses/boss_skills.hpp"
#include "../../native/csol/dependencies.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <set>
using namespace csol::scenario;
namespace {
std::filesystem::path root;
std::set<std::string> reads;
unsigned char* Load(const char* path,int* length) {
    reads.insert(path); *length=0;
    std::ifstream f(root/path,std::ios::binary|std::ios::ate);
    if(!f)return nullptr;
    const auto size=f.tellg();if(size<=0||size>8*1024*1024)return nullptr;
    auto* data=new unsigned char[std::size_t(size)];f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(data),size)){delete[] data;return nullptr;}
    *length=int(size);return data;
}
void Free(void* data){delete[] static_cast<unsigned char*>(data);}
std::vector<std::byte> Read(const std::string& name) {
    int size{};auto* p=Load(name.c_str(),&size);
    if(!p)throw std::runtime_error("missing "+name);
    std::vector<std::byte> out(size);std::memcpy(out.data(),p,size);Free(p);return out;
}
}
int wmain(int argc,wchar_t** argv) {
 try {
    if(argc!=3)throw std::runtime_error("usage: audit COMMON MAP");
    if(std::wstring_view(argv[1])==L"--plan") {
        std::ifstream f(std::filesystem::path(argv[2]),std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)),{});
        rapidjson::Document doc;doc.Parse(text.c_str());
        if(doc.HasParseError()||!doc.HasMember("files")||!doc.HasMember("models"))throw std::runtime_error("invalid plan");
        std::map<std::string,std::filesystem::path> files;
        for(const auto& row:doc["files"].GetArray())
            files.emplace(row["destination"].GetString(),std::filesystem::u8path(row["source"].GetString()));
        const auto read=[&](const std::string& name){
            const auto it=files.find(name);if(it==files.end())throw std::runtime_error("absent planned dependency: "+name);
            std::ifstream stream(it->second,std::ios::binary|std::ios::ate);
            if(!stream)throw std::runtime_error("missing planned source: "+name);
            const auto size=stream.tellg();if(size<0||size>128*1024*1024)throw std::runtime_error("invalid source size");
            std::vector<std::byte> bytes(size);stream.seekg(0);stream.read(reinterpret_cast<char*>(bytes.data()),size);
            if(!stream)throw std::runtime_error("short source read");return bytes;
        };
        const auto exists=[&](const std::string& name){return files.contains(name);};
        std::set<std::string> closure;
        for(const auto& model:doc["models"].GetArray()) {
            const auto found=csol::WeaponDependencies(model.GetString(),read,exists);
            closure.insert(found.begin(),found.end());
        }
        std::cout<<"{\"models\":"<<doc["models"].Size()<<",\"dependencies\":"<<closure.size()<<",\"success\":true}\n";return 0;
    }
    root=argv[1];const auto name=std::filesystem::path(argv[2]).string();
    MapResources map;ResourceError error;ConfigError detail;
    if(!map.BeginMapFromCatalog(RulesProfile::StandardVer2,name,{Load,Free},error))
        throw std::runtime_error(error.path+": "+error.detail.message);
    const auto& t=*map.Tables();
    ChampionTable champions;std::string championPath;
    if(!LoadChampions(name,{Load,Free},champions,championPath,error))
        throw std::runtime_error(error.path+": "+error.detail.message);
    MessageTable messages;DialogueResources dialogue;
    if(!ReadMessageTableResource(Read(csol::scenario_hud::MessagePath),messages,detail)||
       !CollectDialogueResources(t.waves,messages,dialogue,detail))throw std::runtime_error(detail.message);
    BossSkillTable skills;
    if(!ReadBossSkillResource(Read("maps/zbs_sk_"+name+".cso"),skills,detail))throw std::runtime_error(detail.message);
    std::set<std::string> paths,models,bgm;
    const auto appearance=[&](const MonsterType& type){
        const auto* a=FindMonsterAppearance(type.appearance);
        if(!a)throw std::runtime_error("unknown appearance "+type.appearance);
        models.emplace(a->model);paths.emplace(a->model);
        if(!a->deathSound.empty())paths.emplace("sound/"+std::string(a->deathSound));
        if(!a->ambientSound.empty())paths.emplace("sound/"+std::string(a->ambientSound));
        if(a->name=="zombiedog")paths.emplace("sound/zombi/zombiedog_howls.wav");
    };
    for(const auto& [id,index]:t.monsterTypes.byId)appearance(t.monsterTypes.rows[index]);
    for(const auto& row:champions.rows)appearance(row.type);
    bgm.emplace(*csol::music::ResourcePath("Scenario_Ready.mp3"));
    for(std::size_t i=0;i<t.rounds.rows.size();++i){
        RoundSettings setting;if(!ReadRoundSettings(t.rounds,i,setting,detail))throw std::runtime_error(detail.message);
        const auto track=csol::music::FindTrack(setting.music);
        if(!track)throw std::runtime_error("unsupported round music");
        if(!track->filename.empty())bgm.emplace(*csol::music::ResourcePath(track->filename));
    }
    paths.insert(dialogue.images.begin(),dialogue.images.end());
    for(const auto& sound:dialogue.sounds)paths.insert("sound/"+sound);
    rapidjson::StringBuffer buffer;rapidjson::Writer w(buffer);w.StartObject();
    w.Key("map");w.String(name.c_str());w.Key("id");w.Int(t.mapId.value_or(0));
    w.Key("subtype");w.Int(t.scenarioType);w.Key("supported");w.Bool(HasMapGameplay(name,t.scenarioType));
    w.Key("rounds");w.Uint(unsigned(t.rounds.rows.size()));w.Key("waves");w.Uint(unsigned(t.waves.rows.size()));
    w.Key("champions");w.Uint(unsigned(champions.rows.size()));w.Key("championPath");w.String(championPath.c_str());
    const auto strings=[&](const char* key,const auto& values){w.Key(key);w.StartArray();for(const auto& x:values)w.String(x.c_str());w.EndArray();};
    strings("resources",paths);strings("models",models);strings("bgm",bgm);strings("tableReads",reads);
    w.Key("effectiveTypes");w.StartArray();for(const auto& [id,index]:t.monsterTypes.byId){const auto& x=t.monsterTypes.rows[index];w.StartObject();w.Key("id");w.Int(x.id);w.Key("skill");w.Int(x.skill);w.Key("appearance");w.String(x.appearance.c_str());w.EndObject();}w.EndArray();
    w.Key("bossSkillTypes");w.StartArray();for(const auto& x:skills.rows){w.StartObject();w.Key("group");w.Int(x.id);w.Key("type");w.Int(x.type);w.EndObject();}w.EndArray();
    w.EndObject();std::cout<<buffer.GetString()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
