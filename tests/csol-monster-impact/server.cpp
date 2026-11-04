// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production SDK objects; controlled engine allocation/message edges.
// Damage aggregation, registered rules, Session, Pool and TraceAttack are real.
#include "../csol-scenario-monster-skills/trap_fixture.hpp"
#include "../../native/csol/monster_impact.hpp"
#include <limits>

namespace {
using namespace sdk_test;
int assertions{},failed{};
void Expect(bool value,const char* label){++assertions;if(!value){++failed;std::cerr<<"ASSERT "<<label<<'\n';}}
struct Behavior final:CCSOLMonsterBehavior {
    int damageCalls{};
    void Think(CCSOLScenarioMonster&) override {}
    BOOL TakeDamage(CCSOLScenarioMonster&,entvars_t*,entvars_t*,float,int) override {++damageCalls;return TRUE;}
    void Killed(CCSOLScenarioMonster&,entvars_t*,int) override {}
};
class FlushTarget:public CBaseEntity {
public:
    std::function<void()> action;
    int calls{};
    BOOL TakeDamage(entvars_t*,entvars_t*,float,int) override {++calls;const auto f=action;if(f)f();return TRUE;}
    void OnDestroy() override { action={};CBaseEntity::OnDestroy(); }
};
struct Packet {int destination{},id{};Vector origin{};std::vector<int> bytes;std::vector<float> coordinates;bool complete{};};
struct Fixture {
    inline static Fixture* active{};
    Engine& e;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    trap_test::Waves waves;
    csol::scenario::MonsterType type;
    CCSOLScenarioMonster* monster{};
    Behavior* behavior{};
    FlushTarget* flush{};
    std::vector<Packet> packets;
    std::function<void()> onMessage;
    explicit Fixture(Engine& engine):e(engine) {
        active=this;e.files=trap_test::Resources(0);e.globals.time=100;e.Prepare("2");
        g_pGameRules=InstallGameRules();Require(g_pGameRules&&!CSOL_RulesRejected(),"actual registered rules");
        {CSOLScenarioSdkScope scope;runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());Require(scope.Current()&&runtime,"actual runtime registry");}
        session=runtime->session;
        const bool loaded=session->Precache("map_session_test",1,CSOLScenarioResourceScope::Selection);
        if(!loaded)std::cerr<<"PRECACHE "<<session->Error().path<<' '<<session->Error().detail.message<<'\n';
        Require(loaded,"actual selection resource loader");
        for(const char* name:{"playerspawn","zombiespawn","zbstrigger"})for(int number:{1,2,30}) {
            if(std::strcmp(name,"zbstrigger")&&number!=1)continue;
            auto* ed=Engine::Create(Engine::String(name));auto* base=CBaseEntity::Instance(ed);
            if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(base)){p->m_iId=1;p->m_iTeam=2;}
            if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(base)){p->m_iId=7;p->m_iTeam=1;p->m_iMaxSpawn=1000;}
            if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(base)){p->m_iId=number;ed->v.model=Engine::String("*1");}
            Require(e.api.pfnSpawn(ed)==0,"real marker dispatch");ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
        }
        Require(session->Activate(),"actual activation");Require(session->Monsters().Initialize(),"actual pool");
        Require(session->Map().BeginRound(0,100,0,waves),"actual map round generation");
        type.id=24;type.appearance="zombi_origin";
        monster=session->Monsters().Acquire(type,7,session->Map().SpawnPoints().Points());Require(monster,"actual borrowed monster");
        auto impl=std::make_unique<Behavior>();behavior=impl.get();Require(monster->InstallBehavior(std::move(impl)),"actual behavior installation");
        monster->pev->health=100;monster->pev->takedamage=DAMAGE_AIM;monster->pev->model=Engine::String("models/player/zombi_origin/zombi_origin.mdl");
        auto* ed=e.Reserve(e.nextEntity++,"fixture_flush");flush=GetClassPtr<CCSEntity>(reinterpret_cast<FlushTarget*>(&ed->v));
        g_engfuncs.pfnMessageBegin=[](int destination,int id,const float* origin,edict_t*){auto& f=*active;f.packets.push_back({destination,id,origin?Vector(origin):Vector{}});const auto cb=f.onMessage;if(cb)cb();};
        g_engfuncs.pfnWriteByte=[](int value){active->packets.back().bytes.push_back(value);};
        g_engfuncs.pfnWriteCoord=[](float value){active->packets.back().coordinates.push_back(value);};
        g_engfuncs.pfnMessageEnd=[](){active->packets.back().complete=true;};
        ClearMultiDamage();
    }
    ~Fixture(){onMessage={};if(flush)flush->action={};ClearMultiDamage();e.Clear();session.reset();runtime.reset();active=nullptr;}
    void Hit(int bits=DMG_BULLET,float amount=10,int hitgroup=HITGROUP_CHEST,edict_t* target=nullptr){
        TraceResult trace{};trace.pHit=target?target:monster->edict();trace.iHitgroup=hitgroup;trace.flFraction=.5f;trace.vecEndPos=Vector(12.125f,-34.25f,56.5f);
        monster->TraceAttack(flush->pev,amount,Vector(1,0,0),&trace,bits);
    }
};
void Positive(Engine& e){
    Fixture f(e);
    for(int bits:{DMG_BULLET,DMG_SLASH,DMG_CLUB,DMG_BULLET|DMG_NEVERGIB,DMG_BULLET|DMG_ALWAYSGIB}){
        ClearMultiDamage();f.packets.clear();f.Hit(bits);
        Expect(f.packets.size()==1,"one packet per confirmed physical hit");
        if(!f.packets.empty()){const auto& p=f.packets.back();Expect(p.destination==MSG_PVS&&p.id==e.messages.at("CsoMonFX")&&p.complete,"PVS unreliable registered message completes");Expect(p.bytes==std::vector<int>{4,4}&&p.coordinates==std::vector<float>{12.125f,-34.25f,56.5f},"blood exact default amount and impact origin");}
        Expect(gMultiDamage.hEntity==f.monster&&gMultiDamage.amount==15&&gMultiDamage.type==bits,"real aggregation preserves scaled damage and bits");
        Expect(f.behavior->damageCalls==0,"TraceAttack defers actual TakeDamage");
    }
    ClearMultiDamage();f.packets.clear();for(int i=0;i<6;++i)f.Hit(DMG_BULLET,10,HITGROUP_GENERIC);
    Expect(gMultiDamage.amount==60&&f.packets.size()==6,"shotgun retains six hit positions with one aggregate");
    ApplyMultiDamage(f.flush->pev,f.flush->pev);Expect(f.behavior->damageCalls==1,"actual ApplyMultiDamage calls behavior once");
    for(const auto model:csol::monster_impact::MetalModels){ClearMultiDamage();f.packets.clear();f.monster->pev->model=Engine::String(std::string(model).c_str());f.Hit();Expect(f.packets.size()==1&&f.packets[0].bytes==std::vector<int>{5,16},"metal model sparks through shared entry");}
    f.monster->pev->model=Engine::String("models/monster/zbs_bossL_big08.mdl");f.packets.clear();f.Hit(DMG_BULLET,10,8);Expect(f.packets.size()==1&&f.packets[0].bytes==std::vector<int>{5,16},"shield hitgroup sparks");
    f.packets.clear();f.Hit(DMG_BULLET,10,1);Expect(f.packets.size()==1&&f.packets[0].bytes==std::vector<int>{4,4},"shield flesh bleeds");
    f.monster->pev->model=Engine::String("models/player/zg_goliath/zg_goliath2.mdl");f.packets.clear();f.Hit();Expect(f.packets.empty(),"bloodless model suppressed");
}
void Exclusions(Engine& e){
    Fixture f(e);
    for(int bits:{DMG_GENERIC,DMG_BURN,DMG_BLAST,DMG_EXPLOSION,DMG_ENERGYBEAM,0x08000000,DMG_BULLET|DMG_BLAST,DMG_BULLET|DMG_BURN,DMG_BULLET|0x08000000}){
        ClearMultiDamage();f.packets.clear();f.Hit(bits);Expect(f.packets.empty(),"nonphysical and mixed area/DOT bits never create impact blood");Expect(gMultiDamage.amount==15,"effect exclusion preserves existing damage aggregation");
    }
    for(float amount:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){ClearMultiDamage();f.packets.clear();f.Hit(DMG_BULLET,amount);Expect(f.packets.empty(),"nonpositive/nonfinite hits no blood");}
    f.packets.clear();f.Hit(DMG_BULLET,10,1,f.flush->edict());Expect(f.packets.empty(),"mismatched hit entity no blood");
    f.monster->pev->takedamage=DAMAGE_NO;ClearMultiDamage();f.Hit();Expect(f.packets.empty()&&!gMultiDamage.hEntity,"protected monster retains original rejection");
    f.monster->pev->takedamage=DAMAGE_AIM;Require(f.session->Monsters().Free(*f.monster),"actual pool return");ClearMultiDamage();f.Hit();Expect(f.packets.empty()&&!gMultiDamage.hEntity,"parked monster rejected");
}
void Reentry(Engine& e){
    for(int mode=0;mode<6;++mode){
        Fixture f(e);auto* monster=f.monster;auto* pool=&f.session->Monsters();const auto lease=pool->Lease(*monster);
        // Actual AddMultiDamage flushes this different previous victim and
        // enters its virtual TakeDamage, which performs a lifecycle transition.
        AddMultiDamage(f.flush->pev,f.flush,1,DMG_BULLET);
        f.flush->action=[&]{
            if(mode==0)f.runtime->RequestClose();
            if(mode==1)Require(f.session->Map().BeginRound(0,101,0,f.waves),"reentrant same-map round restart");
            if(mode==2)Require(pool->Free(*monster),"reentrant pool return");
            if(mode==3){
                Require(pool->Free(*monster),"return before recycling");bool reused=false;
                for(std::size_t i=0;i<CCSOLMonsterPool::Capacity;++i){auto* next=pool->Acquire(f.type,7,f.session->Map().SpawnPoints().Points());if(!next)break;if(next==monster){reused=true;break;}Require(pool->Free(*next),"return other slot to preserve category quota");}
                Require(reused&&pool->Lease(*monster)!=lease,"real same-slot new lease");
            }
            if(mode==4)Require(monster->InstallBehavior(std::make_unique<Behavior>()),"reentrant behavior replacement");
            if(mode==5)Engine::Remove(monster->edict());
        };
        f.Hit();Expect(f.flush->calls==1,"actual prior-target flush executed once");Expect(f.packets.empty(),"cancelled map/round/entity/lease/behavior suppresses late effect");
    }
    {Fixture f(e);f.onMessage=[&]{f.runtime->RequestClose();};f.Hit();Expect(f.packets.size()==1&&f.packets[0].complete,"message finishes from copied values after owner retirement");}
}
}
int main(int argc,char** argv){try{auto e=std::make_unique<Engine>();std::string mode=argc>1?argv[1]:"all";if(mode=="all"||mode=="positive")Positive(*e);if(mode=="all"||mode=="excluded")Exclusions(*e);if(mode=="all"||mode=="reentry")Reentry(*e);}catch(const std::exception& error){++failed;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
std::cout<<"monster-hit-server assertions="<<assertions<<" failures="<<failed<<" SDK checks="<<sdk_test::checks<<" SDK failures="<<sdk_test::failures<<'\n';return failed||sdk_test::failures?1:0;}
