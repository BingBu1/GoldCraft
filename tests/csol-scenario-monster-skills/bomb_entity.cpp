// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK entity/export/dispatch/Session/Pool. The damage provider is an
// explicit observer; this suite does not claim player damage or full gameplay.
#include "trap_fixture.hpp"
#include "../../native/csol/server/scenario_zombie_bomb.h"
#include "../../native/csol/server/scenario_grenade_blockers.h"
#include "func_break.h"
#include "soundent.h"
#include <bit>
#include <cmath>

#pragma clang fp contract(off)
namespace {
using namespace sdk_test;
int assertions{}, taskFailures{};
void Expect(bool value,const char* name) { ++assertions;if(!value){++taskFailures;std::cerr<<"ASSERT "<<name<<'\n';} }
bool Bits(float a,float b){return std::bit_cast<unsigned>(a)==std::bit_cast<unsigned>(b);}
struct Packet {int destination{},id{};Vector origin{};std::vector<int> bytes;std::vector<float> coordinates;bool complete{};};
struct Case {
    inline static Case* active{};
    Engine& e;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    trap_test::NoDeath executor;
    CCSOLMonsterDeathRequests deaths{executor};
    trap_test::Waves waves;
    csol::scenario::MonsterType type;
    CCSOLScenarioMonster* owner{};
    EHANDLE bomb;
    csol::scenario::ConfigError error;
    std::vector<std::string> order,models,sounds;
    std::vector<std::pair<int,int>> random;
    std::vector<Packet> packets;
    std::function<void(edict_t*,const char*)> onSetModel;
    std::function<void(CBaseEntity*)> onNamed;
    std::function<void()> onRandom,onDamage;
    std::function<void(char)> onMessage;
    bool exhaust{}, damageResult{true}, failDamage{}, capture{};
    int damages{}, randomChoice{}, lastBombIndex{};
    CBaseEntity* credited{};
    float damage{};
    explicit Case(Engine& engine):e(engine) {
        active=this;e.files=trap_test::Resources(0);e.globals.time=100;
        for (const char* path : {CSOLZombieBombModel, csol::monster_effects::ZombieBombSprite,
            "models/grenade.mdl", CSOLZombieBombEvent}) e.files[path] = "fixture";
        for (const char* sound : CSOLZombieBombBounceSounds)
            e.files[std::string("sound/") + sound] = "fixture";
        e.Prepare("2");
        g_pGameRules = InstallGameRules();
        Require(g_pGameRules && !CSOL_RulesRejected(), "real rules registration");
        {
            CSOLScenarioSdkScope scope;
            runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
            Require(scope.Current() && runtime, "real registered runtime");
        }
        session = runtime->session;
        bool ok=session->Precache("map_session_test",1,CSOLScenarioResourceScope::Selection);
        if(!ok){std::cerr<<"PRECACHE "<<session->Error().path<<' '<<session->Error().detail.message<<'\n';for(const auto& event:e.events)std::cerr<<event<<'\n';}
        Require(ok,"real selection resource preparation");
        for(const char* name:{"playerspawn","zombiespawn","zbstrigger"})for(int id:{1,2,30}) {
            if(std::strcmp(name,"zbstrigger")&&id!=1)continue;
            auto* ed=Engine::Create(Engine::String(name));auto* base=CBaseEntity::Instance(ed);
            if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(base)){p->m_iId=1;p->m_iTeam=2;}
            if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(base)){p->m_iId=7;p->m_iTeam=1;p->m_iMaxSpawn=1000;}
            if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(base)){p->m_iId=id;ed->v.model=Engine::String("*1");}
            Require(e.api.pfnSpawn(ed)==0,"actual marker spawn");ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
        }
        Require(session->Activate(),"actual session activation without gameplay services");
        Require(session->Monsters().Initialize(),"actual session pool initialized independently");
        Require(session->Map().BeginRound(0,100,0,waves),"actual map round established independently");
        type.id=24;type.appearance="zombi_origin";
        owner=session->Monsters().Acquire(type,7,session->Map().SpawnPoints().Points());
        Require(owner,"actual monster lease");owner->pev->angles=Vector(10,20,30);owner->pev->health=100;
        CSOL_BeginBreakableGrenadeMap(session->Map().EntityAccess());
        e.Set("sv_maxvelocity","2000");g_psv_maxvelocity=&e.Var("sv_maxvelocity").storage;
        auto* soundEd=Engine::Create(Engine::String("soundent"));pSoundEnt=dynamic_cast<CSoundEnt*>(CBaseEntity::Instance(soundEd));
        Require(pSoundEnt,"actual sound list owner");pSoundEnt->Spawn();
        g_engfuncs.pfnCreateNamedEntity=[](int name)->edict_t* {
            auto& s=*active;
            if(std::strcmp(STRING(name),"csol_zombiebomb"))return Engine::Create(name);
            if(s.exhaust)return nullptr;
            auto* ed=Engine::Create(name);s.lastBombIndex=Engine::Index(ed);const auto cb=s.onNamed;if(cb)cb(CBaseEntity::Instance(ed));return ed;
        };
        g_engfuncs.pfnSetModel=[](edict_t* ed,const char* name) {
            auto& s=*active;ed->v.model=Engine::String(name);ed->v.modelindex=Engine::Model(name);
            if(s.capture){s.models.emplace_back(name);s.order.emplace_back(std::string("model:")+name);}
            const auto cb=s.onSetModel;if(cb)cb(ed,name);
        };
        g_engfuncs.pfnRandomLong=[](int32 low,int32 high)->int32 {
            auto& s=*active;s.random.emplace_back(low,high);s.order.emplace_back("random");
            const auto cb=s.onRandom;if(cb)cb();return low==high?low:low+s.randomChoice;
        };
        g_engfuncs.pfnEmitSound=[](edict_t* ed,int channel,const char* sample,float volume,float attenuation,int flags,int pitch) {
            auto& s=*active;s.sounds.emplace_back(sample);s.order.emplace_back("bounce-sound");
            Expect(ed==s.bomb.Get()&&channel==CHAN_VOICE&&volume==.25f&&attenuation==.8f&&flags==0&&pitch==100,"air bounce sound ABI and original parameters");
            const auto cb=s.e.onSound;if(cb)cb(ed,sample);
        };
        g_engfuncs.pfnMessageBegin=[](int destination,int id,const float* origin,edict_t*) {
            auto& s=*active;s.packets.push_back({destination,id,origin?Vector(origin):Vector{}});s.order.emplace_back("message-begin");const auto cb=s.onMessage;if(cb)cb('b');
        };
        g_engfuncs.pfnWriteByte=[](int value){auto& s=*active;s.packets.back().bytes.push_back(value);const auto cb=s.onMessage;if(cb)cb('w');};
        g_engfuncs.pfnWriteCoord=[](float value){auto& s=*active;s.packets.back().coordinates.push_back(value);const auto cb=s.onMessage;if(cb)cb('c');};
        g_engfuncs.pfnMessageEnd=[](){auto& s=*active;s.packets.back().complete=true;s.order.emplace_back("message-end");const auto cb=s.onMessage;if(cb)cb('e');};
        capture=true;
    }
    ~Case(){
        onSetModel={};onNamed={};onRandom={};onDamage={};onMessage={};e.onOrigin={};e.onSize={};e.onSound={};
        e.onOffset={};e.onIndexLookup={};e.onModel={};capture=false;
        CSOL_RetireBreakableGrenadeMap(session->Map().EntityAccess());deaths.Reset();session->Deactivate();pSoundEnt=nullptr;
        e.Clear();session->Monsters().ForgetMap();runtime->session.reset();session.reset();runtime.reset();g_psv_maxvelocity=nullptr;active=nullptr;
    }
    bool Create(float amount=37.25f) {
        CSOLMonsterSpawnCall call(session->Map(),session->Monsters(),deaths,*owner);
        const bool result=CCSOLZombieBomb::Create(call,runtime,Vector(11,13,47),Vector(120,160,80),amount,error);
        if(lastBombIndex)bomb=CBaseEntity::Instance(Engine::At(lastBombIndex));return result;
    }
    CCSOLZombieBomb& Bomb(){auto* p=bomb.Get<CCSOLZombieBomb>();Require(p,"live bomb private object");return *p;}
    bool Removed(){auto* p=bomb.Get<CBaseEntity>();return !p||(p->pev->flags&FL_KILLME);}
    void Tick(float now){e.globals.time=now;auto* ed=bomb.Get();Require(ed,"tick has real edict");ed->v.nextthink=0;e.api.pfnThink(ed);}
    CBaseEntity& Target(bool typed=false,const char* classname="func_breakable") {
        auto* ed=typed?Engine::Create(Engine::String(classname)):e.Reserve(e.nextEntity++,classname);
        if(!typed)GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&ed->v));
        return *CBaseEntity::Instance(ed);
    }
    void Touch(CBaseEntity& target){e.api.pfnTouch(bomb.Get(),target.edict());}
    int DangerCount(){int result=0;for(int n=CSoundEnt::ActiveList();n!=SOUNDLIST_EMPTY;){auto* p=CSoundEnt::SoundPointerForIndex(n);if(p->m_iType==bits_SOUND_DANGER)++result;n=p->m_iNext;}return result;}
    const CSound& NewestSound(){return *CSoundEnt::SoundPointerForIndex(CSoundEnt::ActiveList());}
    void Explode(){Tick(Bomb().pev->dmgtime);Expect(damages==0,"deadline tick only changes next think");Tick(Bomb().pev->nextthink);}
};
void SpawnCases(Engine& e) {
    for(int sequence:{0,3}){
        Case s(e);s.randomChoice=sequence;Expect(s.Create(),"create succeeds");auto& b=s.Bomb();
        Expect(s.models==std::vector<std::string>{"models/grenade.mdl","models/w_zombibomb.mdl"},"ordinary temporary model then zombie bomb model");
        Expect(s.order==std::vector<std::string>{"model:models/grenade.mdl","random","model:models/w_zombibomb.mdl"},"model/random/model ordering");
        Expect(s.random==std::vector<std::pair<int,int>>{{3,6}}&&b.pev->sequence==3+sequence,"inclusive sequence 3..6");
        Expect(b.pev->movetype==MOVETYPE_BOUNCE&&b.pev->solid==SOLID_BBOX&&b.pev->mins==g_vecZero&&b.pev->maxs==g_vecZero,"real Spawn movement and zero hull");
        Expect(b.pev->origin==Vector(11,13,47)&&b.pev->velocity==Vector(120,160,80)&&b.pev->angles==Vector(10,20,30),"launch origin velocity angles");
        Expect(b.pev->owner==s.owner->edict()&&b.DamageCreator()==s.owner&&b.pev->team==1,"creator exact pool lease");
        Expect(b.pev->dmg==999&&b.pev->health==37.25f&&b.pev->gravity==.55f&&b.pev->friction==.7f&&b.pev->framerate==1,"marker damage and physics");
        Expect(b.pev->dmgtime==101.5f&&Bits(b.pev->nextthink,static_cast<float>(double(e.globals.time)+.1)),"fuse float add and nextthink double add");
    }
    {Case s(e);s.exhaust=true;Expect(s.Create()&&s.error.message.empty()&&!s.lastBombIndex&&s.models.empty(),"edict exhaustion is normal successful nonemission");}
}
void FlightCases(Engine& e) {
    for(int water:{0,1,3}){
        Case s(e);Require(s.Create(),"flight bomb created");auto& b=s.Bomb();b.pev->waterlevel=water;b.pev->velocity=Vector(30,40,0);
        b.pev->animtime=100;b.m_flFrameRate=10;b.pev->frame=3;
        s.Tick(100.5f);Expect(s.DangerCount()==0,"danger threshold strict greater than fuse minus one");
        Expect(b.pev->velocity==(water?Vector(7.5f,10,0):Vector(30,40,0)),"water applies two half velocity reductions");
        Expect(Bits(b.pev->framerate,water?.2f:2.0f)&&b.pev->frame==13,"frame advance uses pre-water speed rate");
        b.pev->velocity=Vector(30,40,80);s.Tick(101);
        Expect(s.DangerCount()==1&&s.NewestSound().m_iVolume==400&&s.NewestSound().m_vecOrigin==Vector(26,33,87),"danger predicts original pre-water velocity and remaining fuse");
        Expect(Bits(s.NewestSound().m_flExpireTime,101.1f),"danger sound duration point one");
        s.Tick(101.5f);Expect(s.damages==0&&!s.Removed(),"at deadline entity has only scheduled detonation");
        const float next=b.pev->nextthink;s.Tick(next);
        Expect(s.damages==1&&s.damage==37.25f&&s.credited==s.owner&&s.Removed(),"next think enters real explode then damage boundary then removes");
        Expect(s.packets.size()==1&&s.packets[0].destination==MSG_PVS&&s.packets[0].bytes==std::vector<int>{3}&&s.packets[0].coordinates==std::vector<float>{11,13,47}&&s.packets[0].complete,"real sender uses zombie opcode and unshifted PVS coordinates");
    }
    for(int axis=0;axis<3;++axis)for(int side:{-1,1})for(bool velocity:{false,true}){
        Case s(e);Require(s.Create(),"limit bomb created");auto& b=s.Bomb();
        if(velocity)b.pev->velocity[axis]=side*2000.0f;else b.pev->origin[axis]=side*4096.0f;
        s.Tick(100.1f);Expect(s.Removed()&&s.damages==0&&s.packets.empty(),"strict origin/velocity world limit removes without explosion");
    }
}
void TouchCases(Engine& e) {
    {Case s(e);Require(s.Create(),"owner touch bomb");auto old=s.Bomb().pev->velocity;s.Touch(*s.owner);Expect(s.Bomb().pev->velocity==old&&s.random.size()==1&&s.sounds.empty(),"owner touch ignored");}
    for(int mode:{0,2})for(bool blocked:{false,true})for(bool typed:{false,true}){
        Case s(e);Require(s.Create(),"breakable touch bomb");auto& t=s.Target(typed);t.pev->rendermode=mode;t.pev->takedamage=DAMAGE_NO;
        CSOL_SetBreakableGrenadeBlock(t,blocked);s.Bomb().pev->flags|=FL_ONGROUND;s.Bomb().pev->velocity=Vector(100,200,300);s.random.clear();
        s.Touch(t);bool bounce=mode&&!blocked&&typed;
        Expect(s.Bomb().pev->velocity==(bounce?Vector(-200,-400,-600):Vector(80,160,240)),"special bounce requires render mode, real type, and no blockgrenade (independent of takedamage)");
        Expect(s.random.size()==(bounce?0:1)&&s.sounds.empty(),"special bounce skips grounded random and sound");
    }
    for(int choice:{0,1}){
        Case s(e);Require(s.Create(),"air bounce bomb");auto& t=s.Target(false,"worldspawn");s.random.clear();s.randomChoice=choice;s.Touch(t);
        Expect(s.random==std::vector<std::pair<int,int>>{{0,1}}&&s.sounds==std::vector<std::string>{CSOLZombieBombBounceSounds[choice]},"air chooses original two bounce samples");
    }
    for(float speed:{60.0f,std::nextafter(60.0f,61.0f)}){
        Case s(e);Require(s.Create(),"slow bounce bomb");auto& t=s.Target(false,"worldspawn");s.Bomb().pev->velocity=Vector(speed,0,0);s.Bomb().pev->flags|=FL_ONGROUND;s.random.clear();
        s.Touch(t);Expect(s.DangerCount()==(speed==60?1:0),"low-speed danger includes exact 3600 square threshold");
        if(speed==60){Expect(s.NewestSound().m_iVolume==2497&&Bits(s.NewestSound().m_flExpireTime,100.3f),"low-speed sound radius damage marker over double point four");s.Touch(t);Expect(s.DangerCount()==1,"low-speed danger only registered once");}
        Expect(s.random.front()==std::pair<int,int>{1,1},"grounded branch still calls random 1..1");
    }
}
void LifetimeCases(Engine& e) {
    for(int kind=0;kind<4;++kind){
        Case s(e);Require(s.Create(),"ownership bomb");auto old=s.owner;
        if(kind==0){s.owner->pev->deadflag=DEAD_DEAD;s.owner->pev->health=0;}
        if(kind==1)s.session->Monsters().Free(*s.owner);
        if(kind==2){auto& pool=s.session->Monsters();while(pool.AvailableCount()){s.type.priority=pool.CategoryCount(0)<39?0:pool.CategoryCount(1)<4?1:2;Require(pool.Acquire(s.type,7,s.session->Map().SpawnPoints().Points()),"fill real pool");}Require(pool.Free(*s.owner),"return creator");Require(pool.Acquire(s.type,7,s.session->Map().SpawnPoints().Points())==old,"creator edict reused with new lease");}
        if(kind==3)Engine::Remove(s.owner->edict());
        Expect(s.Bomb().DamageCreator()==(kind==0?old:nullptr),"creator death retains identity, return/reuse/free lose credit");
        s.Tick(100.2f);Expect(!s.Removed()&&s.Bomb().pev->owner==(kind==0?old->edict():nullptr),"flight independent of creator survival");
    }
    for(int stage=0;stage<5;++stage){
        Case s(e);int calls=0;
        const auto cancel=[&]{++calls;s.deaths.Reset();};
        if(stage==0)s.onNamed=[&](CBaseEntity*){cancel();};
        if(stage==1)s.onSetModel=[&](edict_t*,const char* name){if(std::strcmp(name,"models/grenade.mdl")==0)cancel();};
        if(stage==2)s.e.onSize=[&](edict_t*){cancel();};
        if(stage==3)s.onRandom=cancel;
        if(stage==4)s.onSetModel=[&](edict_t*,const char* name){if(std::strcmp(name,CSOLZombieBombModel)==0)cancel();};
        Expect(!s.Create()&&calls==1&&s.Removed(),"creation cancels and retires allocation at actual callback");
    }
    for(char edge:{'b','w','c','e'}){
        Case s(e);Require(s.Create(),"message cancel bomb");bool once=false;
        s.onMessage=[&](char at){if(at==edge&&!once){once=true;s.runtime->session=std::make_shared<CCSOLScenarioSession>();}};
        s.Explode();Expect(once&&s.damages==0&&s.Removed(),"message callback session replacement skips damage and retires bomb");
    }
    for(int kind=0;kind<3;++kind){
        Case s(e);Require(s.Create(),"revocation bomb");
        if(kind==0)s.session->Map().BeginRound(0,101,0,s.waves);
        if(kind==1)s.runtime->session=std::make_shared<CCSOLScenarioSession>();
        if(kind==2)s.session->RevokeEntityAccess();
        s.Tick(101);Expect(s.damages==0&&s.packets.empty()&&!s.Bomb().IsDamageSource(),"round/session/map invalidation denies effect/damage");
        if(kind!=2)Expect(s.Removed(),"valid old-map storage is retired on round/session cancellation");
    }
}
void EdgeCases(Engine& e) {
    {
        Case s(e);e.globals.time=std::bit_cast<float>(0x3951b717u);Require(s.Create(),"numeric boundary bomb");
        Expect(std::bit_cast<unsigned>(s.Bomb().pev->nextthink)==0x3dcd35a8u,
            "launch nextthink uses original double point one, not float point one");
        s.Tick(e.globals.time);
        Expect(std::bit_cast<unsigned>(s.Bomb().pev->nextthink)==0x3dcd35a8u,
            "flight nextthink uses original double point one, not float point one");
    }
    for(int stage=0;stage<4;++stage){
        Case s(e);int callbacks=0;
        const auto returned=[&]{++callbacks;Expect(s.session->Monsters().Free(*s.owner),"creation callback returns real owner lease");};
        if(stage==0)s.onNamed=[&](CBaseEntity*){returned();};
        if(stage==1)s.e.onOrigin=[&](edict_t*){returned();};
        if(stage==2)s.onRandom=returned;
        if(stage==3)s.onSetModel=[&](edict_t*,const char* name){if(std::strcmp(name,CSOLZombieBombModel)==0)returned();};
        Expect(!s.Create()&&callbacks==1&&s.Removed(),"creation rejects a returned creator lease at every late boundary");
    }
    for(int stage=0;stage<3;++stage){
        Case s(e);bool once=false;
        const auto freed=[&](CBaseEntity* bomb){if(!once){once=true;Engine::Remove(bomb->edict());}};
        if(stage==0)s.onNamed=freed;
        if(stage==1)s.onSetModel=[&](edict_t* ed,const char* name){if(std::strcmp(name,"models/grenade.mdl")==0)freed(CBaseEntity::Instance(ed));};
        if(stage==2)s.onSetModel=[&](edict_t* ed,const char* name){if(std::strcmp(name,CSOLZombieBombModel)==0)freed(CBaseEntity::Instance(ed));};
        // CREATE_NAMED_ENTITY may return a free edict: actual SDK FNullEnt
        // treats it like exhaustion, which is a successful non-emission.
        Expect(s.Create()==(stage==0)&&once&&s.Removed(),
            "free allocation is normal nonemission; free after Spawn cancels safely");
    }
    {
        Case s(e);Require(s.Create(),"nested flight bomb");
        s.onMessage=[&](char edge){if(edge=='b')s.e.api.pfnThink(s.bomb.Get());};
        s.Explode();Expect(s.damages==1&&s.packets.size()==1&&s.Removed(),"nested Think during outgoing packet is ignored");
    }
    for(int stage=0;stage<3;++stage){
        Case s(e);Require(s.Create(),"touch callback bomb");auto& t=s.Target(false,"worldspawn");
        if(stage==0)s.onRandom=[&]{s.runtime->session=std::make_shared<CCSOLScenarioSession>();};
        if(stage==1)s.onRandom=[&]{Engine::Remove(s.bomb.Get());};
        if(stage==2)s.e.onSound=[&](edict_t* ed,const char*){Engine::Remove(ed);};
        s.Touch(t);
        Expect(s.damages==0&&(stage==2?s.sounds.size()==1:s.sounds.empty()),"touch cancellation never continues to unwanted sound or damage");
        if(stage)Expect(s.Removed(),"actual free from touch RNG or sound callback is safe");
    }
    for(int kind=0;kind<4;++kind){
        Case s(e);Require(s.Create(),"damage callback bomb");
        if(kind==0)s.onDamage=[&]{s.runtime->session=std::make_shared<CCSOLScenarioSession>();};
        if(kind==1)s.onDamage=[&]{Engine::Remove(s.bomb.Get());};
        if(kind==2)s.damageResult=false;
        if(kind==3){s.damageResult=false;s.failDamage=true;}
        s.Explode();Expect(s.damages==1&&s.Removed(),"damage cancellation or failure always retires the bomb");
        Expect(CSOL_RulesRejected()==(kind==3),"only explicit live damage failure rejects the map");
    }
}
}
bool CSOL_ApplyZombieBombDamage(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const CSOLZombieBombHitSource& source,float damage,csol::scenario::ConfigError& error) {
    auto& s=*Case::active;++s.damages;s.damage=damage;s.credited=source.Creator();s.order.emplace_back("damage-observer");
    Expect(runtime==s.runtime&&source.Current()==s.bomb.Get<CBaseEntity>(),"damage observer receives current actual entity/runtime");
    Expect(!s.packets.empty()&&s.packets.back().complete,"effect message completes before damage");
    Expect(source.Current()->pev->owner==nullptr&&source.Current()->pev->solid==SOLID_NOT,"explode clears engine owner and collision before damage");
    const auto cb=s.onDamage;if(cb)cb();if(s.failDamage)error.message="fixture damage provider failure";return s.damageResult;
}
int main(int argc,char** argv){
    try{auto e=std::make_unique<sdk_test::Engine>();std::string group=argc>1?argv[1]:"all";
        if(group=="all"||group=="spawn")SpawnCases(*e);if(group=="all"||group=="flight")FlightCases(*e);
        if(group=="all"||group=="touch")TouchCases(*e);if(group=="all"||group=="lifetime")LifetimeCases(*e);
        if(group=="all"||group=="edges")EdgeCases(*e);
    }catch(const std::exception& error){++taskFailures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"entity assertions="<<assertions<<" failures="<<taskFailures<<" SDK boundary checks="<<sdk_test::checks<<" SDK failures="<<sdk_test::failures<<'\n';
    return taskFailures||sdk_test::failures?1:0;
}
