// SPDX-License-Identifier: GPL-3.0-or-later
// Real ReHLDS collision runs in a separate DLL: engine and GameDLL SDK layouts
// are intentionally never aliased. Only the scalar trace result crosses it.
#include "precompiled.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_skills.h"
#include "../csol-monster-recovery/fixture.hpp"
#include "../../native/csol/scenario/core/scenario_respawn_resources.hpp"
#include "../../native/csol/scenario/core/status_icons.hpp"
#include "../../native/csol/scenario/core/buy_zone_hint.hpp"
#include "../../native/csol/scenario/bosses/boss_health.hpp"
#include "../../native/csol/scenario/monsters/frozen_death.hpp"
#include "../../native/csol/scenario/monsters/frozen_smoke.hpp"
#include "../../native/csol/scenario/monsters/elite_visual_resources.hpp"
#include "../../native/csol/character_skill_resources.hpp"
#include "../../native/csol/scenario_part_effects.hpp"
#include "../../native/csol/server/scenario/monsters/scenario_zombie_bomb.h"
#include "../../native/csol/server/characters.h"
#include "bridge.h"
#include <fstream>
#include <filesystem>

namespace {
using namespace sdk_test;
using RecoveryFixture=monster_recovery_test::Fixture;
bool Refresh(CCSOLScenarioMonster& monster) {
#if defined(SHIELD_BASELINE)
    throw std::runtime_error("new helper reached by old test");
#else
    return monster.RefreshTraceHull();
#endif
}
int assertions{},failed{};
void Expect(bool value,const char* label){++assertions;if(!value){++failed;std::cerr<<"ASSERT "<<label<<'\n';}}
struct TraceModel {
    std::vector<byte> raw,animated;
    std::string path;
    TraceModel(const std::filesystem::path& source,std::string name,const RecoveryFixture::Model& fixture):path(std::move(name)) {
        std::ifstream stream(source,std::ios::binary);Require(bool(stream),"real model file opens");
        raw.assign(std::istreambuf_iterator<char>(stream),{});
        Require(raw.size()>=sizeof(studiohdr_t),"real model header complete");
        const auto& h=*reinterpret_cast<const studiohdr_t*>(raw.data());
        Require(h.numhitboxes>0&&h.numhitboxes<=128&&h.hitboxindex>=0&&
            size_t(h.hitboxindex)+size_t(h.numhitboxes)*sizeof(mstudiobbox_t)<=raw.size(),"real model hitboxes bounded");
        // Factory/animation uses the existing controlled sequence fixture.
        // Its header and collision boxes remain exactly those of the model.
        animated.resize(sizeof(RecoveryFixture::Model)+h.numhitboxes*sizeof(mstudiobbox_t));
        std::memcpy(animated.data(),&fixture,sizeof(fixture));
        auto& a=*reinterpret_cast<studiohdr_t*>(animated.data());
        a.numhitboxes=h.numhitboxes;a.hitboxindex=sizeof(fixture);a.length=int(animated.size());
        std::memcpy(animated.data()+a.hitboxindex,raw.data()+h.hitboxindex,h.numhitboxes*sizeof(mstudiobbox_t));
    }
};
struct Packet {int destination{},id{};std::vector<int> bytes;std::vector<float> coordinates;bool complete{};};
constexpr std::array OrdinaryNames{"zombi_origin","zombi_host","speed_zombi_host","heavy_zombi_origin",
    "deimos_zombi_host","stonedeimos2_host","y19s4psycho_origin","zbs_elite_01"};
void ConfigureModels(std::map<std::string,std::string>& files) {
    // Exact current production resource catalogs; existence-only engine edges
    // are explicit. Unknown assets are still rejected by the real file map.
    namespace ui=csol::scenario_respawn_ui;
    for(const auto& v:ui::Outcomes)files[v.path]="fixture";
    for(const auto& v:ui::RetryImages)files[v.path]="fixture";
    for(const auto* v:ui::RetrySounds)files[v]="fixture";
    for(const auto* v:{ui::ItemSprite,ui::LocalizationPath,ui::CenterBackground.path})files[v]="fixture";
    for(std::size_t i=0;i<csol::status::ImageCount;++i)files[std::string(csol::status::ImagePrefix)+std::to_string(i)+".tga"]="fixture";
    for(const auto* v:csol::buy_hint::Images)files[v]="fixture";
    for(const auto* v:csol::boss_health::Resources)files[v]="fixture";
    for(const auto* v:csol::boss_health::AbilityResources)files[v]="fixture";
    for(const auto* v:{csol::buy_hint::Font,csol::frozen_death::IceModel,csol::frozen_smoke::SmokeSprite,
        CSOLZombieBombModel,csol::monster_effects::ZombieBombSprite,"models/grenade.mdl"})files[v]="fixture";
    for(const auto* v:csol::part_effects::Sprites)files[v]="fixture";
    for(const auto* v:CSOLZombieBombBounceSounds)files[std::string("sound/")+v]="fixture";
    files[CSOLZombieBombEvent]="fixture";
    for(const auto* v:csol::elitefx::Models)files[v]="fixture";
    for(const auto* v:csol::elitefx::Sounds)files[v]="fixture";
    for(const auto* v:csol::skillfx::Models)files[v]="fixture";
    for(const auto* v:csol::skillfx::Dependencies)files[v]="fixture";
    for(const auto* v:csol::skillfx::Sounds)files[v]="fixture";
    for(const auto* v:csol::skillfx::ClientResources)files[v]="fixture";
    files["resource/ModeEvent/modeevent.csv"]=";id,name,event\r\n";
    files["sound/sentences.json"]=R"({"MRAD_BUFFCLASS_SKILL":{}})";
    std::array<std::string,35> emotion;emotion[0]="model_id";emotion[8]="wa05";emotion[18]="seq05";emotion[24]="frame01";emotion[34]="vmodelIndex";
    std::string header;for(std::size_t i=0;i<emotion.size();++i){if(i)header+=',';header+=emotion[i];}
    files["classes2/emotion.csv"]=header+"\r\n";
    files["models/csol/characters.json"]=R"({"version":1,"characters":[{"id":1,"team":2,"skin":0,"scenarioClass":1,"male":true,"allowPlayerStyle":false,"handStyle":0,"name":"fixture","model":"gign","hand":"","portrait":"gfx/csol/p/fixture.tga"}]})";
    files["shader/csol/characters.txt"]="models/player/gign/gign.mdl\ngfx/csol/p/fixture.tga\n";
    files["models/player/gign/gign.mdl"]="fixture-player-model";
    files["gfx/csol/p/fixture.tga"]="fixture-portrait";
    std::string table="id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n";
    for(std::size_t i=0;i<OrdinaryNames.size();++i){const auto name=std::string(OrdinaryNames[i]);
        table+=std::to_string(i+1)+","+name+",1,1,1,100,0,100,1,0,0,255 255 255,0,1,0,0,0,1\r\n";
        const auto* appearance=csol::scenario::FindMonsterAppearance(name);Require(appearance!=nullptr,"collision model has actual appearance record");
        files[std::string(appearance->model)]="fixture-model-existence";
        if(!appearance->deathSound.empty())files["sound/"+std::string(appearance->deathSound)]="fixture";
        if(!appearance->ambientSound.empty())files["sound/"+std::string(appearance->ambientSound)]="fixture";
    }
    files["maps/ZBS_ZT_zs_deadend.cso"]=difficulty_test::Encrypt(table);
    files["maps/ZBS_Champ_ZT_Default.cso"]=difficulty_test::Encrypt(
        ";Difficulty,spawnstarttime,interval,spawnendtime,maxnum,zombitype,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n");
}
struct Fixture:RecoveryFixture {
    inline static Fixture* current{};
    HMODULE library{};ShieldTraceFunction traceFunction{};
    std::vector<TraceModel> models;
    std::vector<Packet> packets;
    std::function<void(edict_t*)> onGetModel;
    CCSOLScenarioMonster* target{};
    CBaseEntity* shooter{};
    ShieldTraceResult last{};
    int traces{};
    Fixture(const std::filesystem::path& root):RecoveryFixture(ConfigureModels) {
        current=this;
        library=LoadLibraryW((root/L"trace.dll").c_str());Require(library!=nullptr,"separate real engine collision DLL loads");
        traceFunction=reinterpret_cast<ShieldTraceFunction>(GetProcAddress(library,"ShieldTrace"));
        Require(traceFunction!=nullptr,"typed scalar collision bridge exists");
        for(const char* name:OrdinaryNames)
            models.emplace_back(root/(std::string(name)+".mdl"),std::string(csol::scenario::FindMonsterAppearance(name)->model),model);
        models.emplace_back(root/L"oberon.mdl","models/monster/zbs_bossL_big02.mdl",model);
        g_engfuncs.pfnGetModelPtr=[](edict_t* ed)->void* {
            auto& f=*current;auto* selected=&f.Find(STRING(ed->v.model));
            const auto callback=f.onGetModel;if(callback)callback(ed);
            return selected->animated.data();
        };
        g_engfuncs.pfnRandomFloat=[](float lo,float hi){return (lo+hi)*.5f;};
        g_engfuncs.pfnTraceTexture=[](edict_t*,const float*,const float*)->const char*{return nullptr;};
        g_engfuncs.pfnMessageBegin=[](int destination,int id,const float*,edict_t*) {current->packets.push_back({destination,id});};
        g_engfuncs.pfnWriteByte=[](int value){current->packets.back().bytes.push_back(value);};
        g_engfuncs.pfnWriteCoord=[](float value){current->packets.back().coordinates.push_back(value);};
        g_engfuncs.pfnMessageEnd=[](){current->packets.back().complete=true;};
        auto* ed=engine.Reserve(engine.nextEntity++,"shield_shooter");
        shooter=GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&ed->v));shooter->pev->team=CT;
    }
    TraceModel& Find(std::string_view path) {
        for(auto& value:models)if(value.path==path)return value;
        return models[0]; // Noncombat actor animation uses the controlled base model.
    }
    CCSOLScenarioMonster& Spawn(int type=1) {
        if(target)Require(session->Monsters().Free(*target),"return previous damage target before next model");target=nullptr;
        const auto result=runtime->gameplay->Factory().Spawn(TERRORIST,type,1,0);
        if(result!=CSOLMonsterSpawnResult::Spawned)std::cerr<<"FACTORY "<<runtime->gameplay->Factory().Error().message<<'\n';
        Require(result==CSOLMonsterSpawnResult::Spawned,"real factory creates ordinary behavior and damage owner");
        for(std::size_t i=0;i<CCSOLMonsterPool::Capacity;++i)
            if(auto* m=session->Monsters().At(i);m&&session->Monsters().Borrowed(*m))target=m;
        Require(target&&dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(target->Behavior()),"actual ordinary damage behavior installed");
        target->pev->health=target->pev->max_health=1000;
        target->HostState().flinchEnabled=false; // Exclude unrelated controlled animation timing.
        return *target;
    }
    ShieldTraceResult Trace(TraceModel& m,int state,int player,float height,int pose=0) {
        ShieldTraceResult t{};const float from[]{-100,0,height},to[]{100,0,height};
        Require(traceFunction(m.raw.data(),state,player,pose,from,to,&t)==1,"actual ReHLDS hull/clip returns trace");return t;
    }
    void Fire(float height,CBaseEntity* source=nullptr) {
        traces=0;last={};packets.clear();ClearMultiDamage();
        g_engfuncs.pfnTraceLine=[](const float* begin,const float* end,int,edict_t*,TraceResult* out) {
            auto& f=*current;Require(f.target!=nullptr,"trace has actual pooled target");
            auto& m=f.Find(STRING(f.target->pev->model));
            ShieldTraceResult t{};
            Require(f.traceFunction(m.raw.data(),f.target->pev->gamestate,0,0,begin,end,&t)==1,"real engine collision executes inside actual bullet loop");
            ++f.traces;f.last=t;*out={};out->flFraction=t.fraction;out->iHitgroup=t.group;
            out->fStartSolid=t.startsolid;out->fAllSolid=t.allsolid;
            out->vecEndPos=Vector(t.end);out->vecPlaneNormal=Vector(t.normal);out->pHit=t.hit?f.target->edict():Engine::At(0);
        };
        auto* actor=source?source:shooter;
        Vector origin(-100,0,height),direction(1,0,0);
        actor->FireBullets3(origin,direction,0,200,1,BULLET_PLAYER_57MM,10,1,actor->pev,true,137);
    }
    int Blood()const {
        const auto id=engine.messages.at("CsoMonFX");int count{};
        for(const auto& p:packets)if(p.id==id&&p.bytes==std::vector<int>{4,4}&&p.complete)++count;
        return count;
    }
    int Sparks()const {
        int count{};for(const auto& p:packets)if(p.id==SVC_TEMPENTITY&&!p.bytes.empty()&&p.bytes[0]==TE_SPARKS&&p.complete)++count;return count;
    }
    ~Fixture(){onGetModel={};ClearMultiDamage();if(library)FreeLibrary(library);current=nullptr;}
};
void CollisionCases(Fixture& f) {
    for(std::size_t i=0;i<OrdinaryNames.size();++i)for(float height:{40.f,64.f}) {
        const auto old=f.Trace(f.models[i],0,0,height),fresh=f.Trace(f.models[i],1,0,height);
        const int count=reinterpret_cast<const studiohdr_t*>(f.models[i].raw.data())->numhitboxes;
        Expect(old.hit&&old.group==8&&old.count==count,"old ordinary model trace falsely hits shield");
        // Deimos's hand flesh shares the shield bone and precedes the torso
        // in this controlled pose. It remains a valid body hit after removal.
        const bool flesh=height==64?fresh.group==1:fresh.group>=1&&fresh.group<=7;
        Expect(fresh.hit&&flesh&&fresh.count==count-1,"skip shield preserves real flesh/head hitbox");
        const auto player=f.Trace(f.models[i],0,1,height);
        Expect(player.hit&&player.group==8&&player.count==count,"ordinary player shield collision remains enabled");
    }
    const auto boss=f.Trace(f.models.back(),0,0,40,1);
    Expect(boss.hit&&boss.group==0&&boss.count==28,"Oberon retains all 28 real collision boxes");
}
void DamageCases(Fixture& f,bool expectOld) {
    for(int type=1;type<=int(OrdinaryNames.size());++type){auto& monster=f.Spawn(type);
    Expect(monster.pev->gamestate==1,"factory model application sets ordinary shield exclusion");
    for(float height:{40.f,64.f}) {
        const auto oldHealth=monster.pev->health;
        f.Fire(height);
        const bool flesh=height==64?f.last.group==1:f.last.group>=1&&f.last.group<=7;
        Expect(f.traces==1&&flesh,"actual FireBullets3 consumes physical flesh/head trace");
        Expect(monster.pev->health<oldHealth,"real ordinary damage owner subtracts health");
        Expect(f.Blood()==1&&f.Sparks()==0,"one confirmed blood packet and no shield sparks");
        Expect(monster.Behavior()->Failure().empty(),"damage chain remains valid after hit");
        std::cout<<"DAMAGE model="<<OrdinaryNames[type-1]<<" height="<<height<<" group="<<f.last.group<<" hp="<<oldHealth<<"->"<<monster.pev->health<<" blood="<<f.Blood()<<" sparks="<<f.Sparks()<<'\n';
    }
    if(expectOld)break;
    }
    if(expectOld)return;
    auto& monster=*f.target;
    // Same full bullet and real damage chain, with only the historical engine
    // flag restored: reproduces the reported sparks and zero HP change.
    monster.pev->gamestate=0;const float old=monster.pev->health;f.Fire(40);
    Expect(f.last.group==8&&monster.pev->health==old&&f.Blood()==0&&f.Sparks()==1,"historical shield branch sparks and skips TraceAttack/HP");
    Require(Refresh(monster),"restore current model collision policy");
    auto& player=f.Player(1,true);player.m_iJoiningState=JOINED;
    g_engfuncs.pfnCmd_Argc=[](){return 2;};
    g_engfuncs.pfnCmd_Argv=[](int i)->const char*{return i==1?"1":"csol_character_select";};
    g_engfuncs.pfnInfoKeyValue=[](char*,const char* key)->char*
        {return const_cast<char*>(std::strcmp(key,"csol_protocol")==0?"1":"");};
    CSOL_PrecacheCharacters();
    Require(CSOL_CharacterCommand(player.edict(),"csol_character_select"),"human executes real character selection command");
    int classId{-1};
    Require(CSOL_CharacterScenarioClass(&player,classId,f.session->Map().EntityAccess())&&classId==1,"real catalog selection resolves class1");
    const bool prepared=f.runtime->players->Prepare(player);
    if(!prepared)std::cerr<<"PREPARE "<<f.runtime->players->Error().message<<'\n';
    Require(prepared,"human's real preparation initializes class mods and active ability selection");
    auto* ed=Engine::Create(Engine::String("weapon_skull1"));
    auto* weapon=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));
    Require(weapon!=nullptr,"real weapon_skull1 export constructs concrete weapon");
    weapon->m_iId=130;weapon->m_pPlayer=&player;player.m_pActiveItem=weapon;
    const float before=monster.pev->health;f.Fire(40,&player);
    Expect(monster.pev->health<before&&f.Blood()==1&&f.Sparks()==0,"actual configured human with SKULL1 source damages and bleeds target");
    Expect(monster.Behavior()->Failure().empty(),"SKULL1 player upgrade/reward/source chain remains valid");
    std::cout<<"PLAYER hp="<<before<<"->"<<monster.pev->health<<" error="<<monster.Behavior()->Failure()<<'\n';
}
void LifecycleCases(Fixture& f) {
    CCSOLMonsterModels models;csol::scenario::MonsterTypeTable types;
    for(int i=0;i<2;++i){csol::scenario::MonsterType t;t.id=100+i;t.appearance=i?"zbs_bossL_big02":"zombi_origin";t.health=100;t.speed=100;types.byId.emplace(t.Key(),types.rows.size());types.rows.push_back(t);}
    csol::scenario::ConfigError error;Require(models.Precache(types,error),"actual model table precaches ordinary and Boss");
    auto& pool=f.session->Monsters();
    CCSOLScenarioMonster* retained{};
    for(int kind:{0,1,0}) {
        if(retained)Require(pool.Free(*retained),"return previous model lease");
        CCSOLScenarioMonster* next{};
        for(std::size_t i=0;i<=CCSOLMonsterPool::Capacity;++i){next=pool.Acquire(types.rows[kind],1,f.session->Map().SpawnPoints().Points());Require(next!=nullptr,"model cycle obtains inactive lease");if(!retained||next==retained)break;Require(pool.Free(*next),"rotate other leases without exhausting quota");}
        Require(!retained||next==retained,"ordinary/Boss/ordinary uses identical pooled pointer");retained=next;
        csol::scenario::MonsterAttributes output;
        Require(models.Apply(*next,{},output,error),"actual model application recomputes trace state");
        Expect(next->pev->gamestate==(kind?0:1),"pool model reuse never inherits prior shield state");
        const auto result=f.Trace(f.Find(STRING(next->pev->model)),next->pev->gamestate,0,40,kind);
        Expect(result.hit&&result.group==(kind?0:2)&&result.count==(kind?28:20),"reused model retains correct physical hull count and group");
    }
    Require(pool.Free(*retained),"model cycle returns final lease");
}
void CallbackCases(const std::filesystem::path& root,int mode) {
    Fixture f(root);auto& monster=f.Spawn();monster.pev->gamestate=99;
    const auto access=f.session->Map().EntityAccess();
    f.onGetModel=[&](edict_t* ed){f.onGetModel={};if(mode==0)f.runtime->RequestClose();else if(mode==1)++ed->v.modelindex;else Engine::Remove(ed);};
    EHANDLE identity(&monster);const bool refreshed=Refresh(monster);
    Expect(!refreshed,"retired/replaced/deleted model query cannot publish trace flag");
    if(mode!=2)Expect(identity.Get<CBaseEntity>()==&monster&&monster.pev->gamestate==99,"cancelled callback leaves engine trace state unchanged");
    if(mode==0)Expect(!access->Allowed(),"retirement test revoked actual entity access");
}
}
int main(int argc,char**argv){try {
    Require(argc>=3,"arguments: resource directory and group");const std::filesystem::path root=argv[1];const std::string group=argv[2];
    if(group=="collision"||group=="damage"||group=="old"||group=="lifecycle") {
        Fixture f(root);if(group=="collision")CollisionCases(f);else if(group=="lifecycle")LifecycleCases(f);else DamageCases(f,group=="old");
    }else if(group=="callbacks")for(int mode=0;mode<3;++mode)CallbackCases(root,mode);
    else throw std::runtime_error("unknown group");
}catch(const std::exception&e){++failed;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
std::cout<<"monster-shield assertions="<<assertions<<" failures="<<failed<<" SDK="<<sdk_test::checks<<" SDKfail="<<sdk_test::failures<<'\n';return failed||sdk_test::failures?1:0;}
