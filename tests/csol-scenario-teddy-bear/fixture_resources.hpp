// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-monster-skills/trap_fixture.hpp"
#include "../../native/csol/cso_tent.hpp"
#include "../../native/csol/scenario/core/scenario_respawn_resources.hpp"
#include "../../native/csol/scenario/core/scenario_round_hud.hpp"
#include "../../native/csol/scenario/core/status_icons.hpp"
#include "../../native/csol/scenario/core/buy_zone_hint.hpp"
#include "../../native/csol/scenario/bosses/boss_health.hpp"
#include "../../native/csol/scenario/monsters/frozen_death.hpp"
#include "../../native/csol/scenario/monsters/frozen_smoke.hpp"
#include "../../native/csol/scenario/monsters/elite_visual_resources.hpp"
#include "../../native/csol/character_skill_resources.hpp"
#include "../../native/csol/scenario_part_effects.hpp"
#include "../../native/csol/server/scenario/monsters/scenario_zombie_bomb.h"
#include "../../native/csol/server/scenario/bosses/scenario_teddy_resources.h"
#include "../../native/csol/server/scenario/bosses/scenario_teddy_reward.h"
#include "../../native/csol/server/scenario/bosses/scenario_oberon_resources.h"
#include "../../native/csol/scenario/bosses/oberon_hint.hpp"
#include "../../native/csol/server/scenario/bosses/scenario_tentacle_resources.h"
#include "../../native/csol/scenario/core/wall_upgrade.hpp"
#include "../../native/csol/server/scenario/core/scenario_death_services.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_damage.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_services.h"

namespace teddy_sdk_test
{
using namespace sdk_test;
inline void FixtureResources(std::map<std::string,std::string>& files)
{
    // Explicit existence fixtures for the complete server08 resource gate.
    // The SDK test does not claim to decode/render any of these asset bytes.
    namespace ui=csol::scenario_respawn_ui;
    for(const auto& v:ui::Outcomes)files[v.path]="fixture";
    for(const auto& v:ui::RetryImages)files[v.path]="fixture";
    for(const auto* v:ui::RetrySounds)files[v]="fixture";
    for(const auto* v:{ui::ItemSprite,ui::LocalizationPath,ui::CenterBackground.path})files[v]="fixture";
    for(std::size_t i=0;i<csol::status::ImageCount;++i)files[std::string(csol::status::ImagePrefix)+std::to_string(i)+".tga"]="fixture";
    for(const auto* v:csol::buy_hint::Images)files[v]="fixture";
    for(const auto* v:csol::boss_health::Resources)files[v]="fixture";
    for(const auto* v:csol::boss_health::AbilityResources)files[v]="fixture";
    for(const auto& v:csol::scenario_hud::Resources)files[v.path]="fixture";
    for(const auto* v:{csol::scenario_hud::LocalizationPath,csol::scenario_hud::FontPath,
        csol::buy_hint::Font,csol::frozen_death::IceModel,csol::frozen_smoke::SmokeSprite,
        CSOLZombieBombModel,csol::monster_effects::ZombieBombSprite,"models/grenade.mdl"})files[v]="fixture";
    files[csol::scenario_hud::MessagePath]=difficulty_test::Encrypt("id,unused,duration,text,image,sound\r\n");
    for(const auto* v:csol::part_effects::Sprites)files[v]="fixture";
    for(const auto* v:CSOLZombieBombBounceSounds)files[std::string("sound/")+v]="fixture";
    files[CSOLZombieBombEvent]="fixture";
    files[csol::smoke::EventPath]="fixture";
    for(const auto* path:{csol::wall_upgrade::Model,csol::wall_upgrade::Sprite,
        "resource/Hud/ZBS/zbslevelupwallbg.tga","resource/csol/wall/v1/WallUpgradeScheme.res"})files[path]="fixture";
    for(const auto* v:csol::elitefx::Models)files[v]="fixture";
    for(const auto* v:csol::elitefx::Sounds)files[v]="fixture";
    for(const auto* v:csol::skillfx::Models)files[v]="fixture";
    for(const auto* v:csol::skillfx::Dependencies)files[v]="fixture";
    for(const auto* v:csol::skillfx::Sounds)files[v]="fixture";
    for(const auto* v:csol::skillfx::ClientResources)files[v]="fixture";
    files["maps/ZBS_Champ_ZT_Default.cso"]=difficulty_test::Encrypt(
        ";Difficulty,spawnstarttime,interval,spawnendtime,maxnum,zombitype,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n");
    files["maps/ZBS.json"]=R"({"zbs_add_attribute":{"restrictWeaponDrop":{"default":{"Difficulty":7}}},"ZBS_Elite":{"Properties":[{"difficulty":1}],"Colors":[255,255,255],"Skills":[{},{},{},{},{},{},{},{},{},{},{},{},{},{}]}})";
    const auto* elite=csol::scenario::FindMonsterAppearance("zbs_elite_01");
    Require(elite!=nullptr,"actual synthetic elite appearance is known");files[std::string(elite->model)]="fixture";
    // This baseline precaches validated sequence records before constructing
    // the pool. Install the bounded controlled studio at that real boundary.
    struct Studio { studiohdr_t header; std::array<mstudioseqdesc_t,csol::scenario::MonsterSequenceCount> sequence; };
    static Studio studio{};studio={};studio.header.length=sizeof(studio);
    studio.header.numseq=static_cast<int>(studio.sequence.size());studio.header.seqindex=offsetof(Studio,sequence);
    const auto names=csol::scenario::MonsterSequenceNames();
    for(std::size_t i=0;i<names.size();++i)
    {auto& s=studio.sequence[i];std::snprintf(s.label,sizeof(s.label),"%s",names[i].data());s.fps=10;s.numframes=11;s.flags=STUDIO_LOOPING;}
    g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return &studio;};
}
inline void GroundFixtureResources(std::map<std::string,std::string>& files)
{
    FixtureResources(files);
    // Include Teddy in the authored effective map table so the real Session
    // resource gate and Models::Precache prepare the ground cache themselves.
    // The normal type1 pool fixture remains available for selected-state tests.
    files["maps/ZBS_ZT_map_session_test.cso"]=difficulty_test::Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,3,255 255 255,0,0,0,0,7,1\r\n"
        "32100,bearboss,1,1,1,100,0,100,1,0,1003,255 255 255,0,0,0,0,7,1\r\n");
    const auto* appearance=csol::scenario::FindMonsterAppearance("bearboss");
    Require(appearance!=nullptr,"actual Teddy appearance is known");
    files[std::string(appearance->model)]="fixture";
    if(const auto* image=csol::boss_health::LeftImage(appearance->index))files[image]="fixture";
    for(const auto* path:CSOLTeddyGroundModels)files[path]="fixture";
    for(const auto* path:CSOLTeddyMeteorEntityModels)files[path]="fixture";
    for(const auto* path:CSOLTeddyWeaponRewardModels)files[path]="fixture";
    for(const auto* path:CSOLTeddySpawnSprites)files[path]="fixture";
    files[CSOLTeddySlidingSprite]="fixture";
    for(const auto* path:CSOLTeddyMeteorSprites)files[path]="fixture";
    for(const auto* path:CSOLTentaclePrecacheModels)files[path]="fixture";
    files[CSOLTeddyTentacleModels[3]]="fixture";
    for(const auto* sound:CSOLTentacleSounds)files[std::string("sound/")+sound]="fixture";
    files[std::string("sound/")+CSOLTeddyGroundSound]="fixture";
    files[std::string("sound/")+CSOLTeddyGiftFireworkSound]="fixture";
    // Actual service assembly accepts supported map identities only. Use an
    // authored zs_deadend/98 component map; no production gate is bypassed.
    auto authored=std::move(files);files.clear();
    for(auto& [path,bytes]:authored)
    {
        auto name=path;const auto at=name.find("map_session_test");
        if(at!=std::string::npos)name.replace(at,std::strlen("map_session_test"),"zs_deadend");
        files.emplace(std::move(name),std::move(bytes));
    }
    std::vector<std::string> header(59),row(59,"0");
    header[0]=";map_id";header[1]="map_name";header[2]="map_filename";header[58]="SubType";
    row[0]="98";row[1]="fixture";row[2]="zs_deadend";row[58]="1";
    std::string catalog;for(const auto& fields:{header,row})
    {for(std::size_t i=0;i<fields.size();++i){if(i)catalog+=',';catalog+=fields[i];}catalog+="\r\n";}
    files["resource/maplist.csv"]=std::move(catalog);
}
inline void BlackholeFixtureResources(std::map<std::string,std::string>& files)
{
    GroundFixtureResources(files);
    // Actual Nightmare tables include both Teddy and Oberon. Author both types
    // explicitly: existing event0 uses the Oberon-owned cold blackhole cache.
    files["maps/ZBS_ZT_zs_deadend.cso"]=difficulty_test::Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,3,255 255 255,0,0,0,0,7,1\r\n"
        "32100,bearboss,1,1,1,100,0,100,1,0,1003,255 255 255,0,0,0,0,7,1\r\n"
        "30200,zbs_bossL_big02,1,1,1,100,0,100,1,0,1002,255 255 255,0,0,0,0,7,1\r\n");
    const auto* appearance=csol::scenario::FindMonsterAppearance("zbs_bossL_big02");
    Require(appearance!=nullptr,"actual Oberon appearance is known");
    files[std::string(appearance->model)]="fixture";
    files[std::string("sound/")+std::string(appearance->deathSound)]="fixture";
    if(const auto* image=csol::boss_health::LeftImage(appearance->index))files[image]="fixture";
    for(const auto* path:{CSOLOberonBlackHoleModel,CSOLOberonKnifeEffectModel,csol::oberon_hint::Sprite})files[path]="fixture";
    for(const auto* path:CSOLOberonBlackHoleDependencies)files[path]="fixture";
    for(const auto* path:CSOLOberonAttackModels)files[path]="fixture";
    for(const auto* sound:CSOLOberonAttackSounds)files[std::string("sound/")+sound]="fixture";
}
inline void AssembleGroundServices(trap_test::Fixture& f)
{
    f.runtime->monsterDamageServices=std::make_shared<CCSOLScenarioDamageServices>(f.runtime);
    auto deaths=f.runtime->deathServices=std::make_shared<CCSOLScenarioDeathServices>(f.runtime);
    auto services=f.runtime->monsterServices=std::make_shared<CCSOLScenarioMonsterServices>(f.runtime,f.session,deaths);
    csol::scenario::ResourceError resourceError;
    Require(deaths->Precache({Engine::Load,Engine::Free},resourceError)&&deaths->Golden().EmptyPlan(),
        "actual death provider precaches an empty Golden plan before assembly");
    csol::scenario::ConfigError error;
    Require(services->Assemble(f.runtime->monsterDamageServices,error),
        "actual monster provider assembles gameplay from fresh supported-map combat resources");
}
}
