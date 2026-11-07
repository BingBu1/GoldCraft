// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production SDK and real Session/monster/wall damage. Only engine
// world/filesystem and message transport are controlled by the shared fixture.
#define main gate_fixture_main
#include "../csol-scenario-2gate/session.cpp"
#undef main
#include "../../native/csol/server/damage_notice.h"
#include "../../native/csol/server/damage_numbers.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_ordinary.h"
#include <bit>

namespace attack_test
{
using namespace sdk_test;
struct Packet { int destination{}, recipient{}, id{}; std::vector<std::uint8_t> bytes; };
struct Capture
{
    inline static Capture* active{};
    std::optional<Packet> pending;
    std::vector<Packet> packets;
    std::function<void()> beforeEnd;
    Capture()
    {
        active=this;
        g_engfuncs.pfnMessageBegin=[](int dest,int id,const float*,edict_t* ed) {
            Require(!active->pending,"engine message cannot nest");
            active->pending=Packet{dest,ed?Engine::Index(ed):0,id,{}};
        };
        g_engfuncs.pfnMessageEnd=[] {
            Require(active->pending.has_value(),"message end has begin");
            const auto callback=active->beforeEnd;if(callback)callback();
            active->packets.push_back(std::move(*active->pending));active->pending.reset();
        };
        g_engfuncs.pfnWriteByte=g_engfuncs.pfnWriteChar=[](int n){Write(n,1);};
        g_engfuncs.pfnWriteShort=g_engfuncs.pfnWriteEntity=[](int n){Write(n,2);};
        g_engfuncs.pfnWriteLong=[](int n){Write(n,4);};
        g_engfuncs.pfnWriteCoord=[](float n){Write(int(n*8),2);};
        g_engfuncs.pfnWriteAngle=[](float n){Write(int(n*256/360),1);};
        g_engfuncs.pfnWriteString=[](const char* s){for(;*s;++s)Write(*s,1);Write(0,1);};
    }
    ~Capture(){beforeEnd={};active=nullptr;}
    static void Write(int value,int count)
    {Require(active&&active->pending.has_value(),"write in packet");for(int i=0;i<count;++i)active->pending->bytes.push_back(std::uint32_t(value)>>(8*i));}
    std::vector<Packet> Numbers() const
    {std::vector<Packet> result;for(const auto& p:packets)if(p.bytes.size()==9&&p.bytes[0]==206)result.push_back(p);return result;}
    void Clear(){packets.clear();}
    void Expect(CBaseEntity& target,int value,int style=0,int flags=0,int recipient=1)
    {
        const auto result=Numbers();Check(result.size()==1,"one approved attack produces exactly one number");
        if(result.size()!=1)return;
        const auto expected=CSOLDamageNumberBytes({static_cast<std::int16_t>(Engine::Index(target.edict())),static_cast<std::uint8_t>(style),static_cast<std::uint8_t>(flags),value});
        Check(result[0].bytes==std::vector<std::uint8_t>(expected.begin(),expected.end()),"actual authoritative value/style/flags payload");
        Check(result[0].recipient==recipient&&result[0].destination==MSG_ONE_UNRELIABLE,"attacker-only unreliable delivery");
    }
};

void Scenario(const std::filesystem::path& resources,std::string_view mode)
{
    gate_session::Fixture f(resources);
    Capture capture;
    gate_session::Startup(f);
    CBaseEntity* target{};
    if(mode.starts_with("wall"))
    {
        auto* protectedWall=f.walls.front().Get<CBaseEntity>();
        Require(protectedWall,"original defense wall exists");
        const float protectedHealth=protectedWall->pev->health;
        capture.Clear();
        protectedWall->TakeDamage(f.survivor->pev,f.survivor->pev,10,DMG_BULLET);
        Check(protectedWall->pev->health==protectedHealth&&capture.Numbers().empty(),
            "original zero player-ratio wall neither loses health nor sends a number");
        // zs_2gate's original walls have hmdmgratio=0: players cannot damage
        // them. Explicit controlled map input exercises a player-damageable
        // wall through the real Spawn/scaling/reward/CBreakable path.
        auto* ed=Engine::Create(Engine::String("zbsbreak"));
        for(const auto& [key,value]:std::array<std::pair<const char*,const char*>,5>{{
            {"hmdmgratio","1"},{"zbdmgratio","0"},{"health","1000"},{"material","1"},{"model","*1"}}})
        {
            KeyValueData field{const_cast<char*>("zbsbreak"),const_cast<char*>(key),const_cast<char*>(value),FALSE};
            f.engine.api.pfnKeyValue(ed,&field);Require(field.fHandled,"controlled player wall key consumed");
        }
        Require(f.engine.api.pfnSpawn(ed)==0,"actual wall Spawn");
        auto* wall=dynamic_cast<CCSOLScenarioBreakable*>(CBaseEntity::Instance(ed));
        Require(wall,"actual wall type");
        // Original 2gate rewards have threshold zero because humans cannot
        // damage its defense walls. Pair the controlled player-damageable wall
        // with explicit positive metadata, retaining real rules and players.
        auto rounds=f.runtime->session->Map().Tables()->rounds;
        rounds.source.records.at(2).fields.at(1)="125";
        csol::scenario::ConfigError error;
        auto rewards=CCSOLScenarioWallRewards::Create(rounds,f.runtime->session->Difficulty(),
            f.runtime->session->Players(),error);
        Require(bool(rewards),"actual wall rewards with controlled threshold");
        rewards->BindEntityAccess(f.runtime->session->Map().EntityAccess());
        wall->BindRules(rewards);target=wall;
    }
    else for(auto& ed:f.engine.edicts)if(!ed.free&&ed.pvPrivateData)
        if(auto* monster=dynamic_cast<CCSOLScenarioMonster*>(CBaseEntity::Instance(&ed));monster&&monster->Pool()->Borrowed(*monster))
        {target=monster;break;}
    Require(target,"actual damage target exists");
    auto* monster=dynamic_cast<CCSOLScenarioMonster*>(target);
    target->pev->health=1000;target->pev->armorvalue=0;
    if(monster)monster->HostState().lastHitgroup=HITGROUP_CHEST;
    if(mode=="wall-restart")
    {
        auto* wall=dynamic_cast<CCSOLScenarioBreakable*>(target);Require(wall,"restart wall type");
        int restarts{};
        f.engine.onSound=[&](edict_t* ed,const char*) {
            if(ed==wall->edict()&&!restarts){++restarts;wall->Restart();}
        };
        capture.Clear();wall->TakeDamage(f.survivor->pev,f.survivor->pev,10,DMG_BULLET);
        f.engine.onSound={};
        Check(restarts==1&&capture.Numbers().empty(),"wall Restart during native damage sound cancels old number");
        f.engine.Clear();return;
    }
    if(mode=="reset"||mode=="retire")
    {
        auto* ordinary=dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(monster->Behavior());
        Require(ordinary,"real ordinary behavior");
        auto state=f.runtime->gameplay->Factory().State(*monster)->shared_from_this();
        const auto lease=monster->Pool()->Lease(*monster);
        int resets{};
        f.engine.onOffset=[&](const edict_t*) {
            if(resets||monster->pev->health>=1000)return;
            ++resets;
            if(mode=="retire") { f.runtime->RequestClose();return; }
            CSOLMonsterSpawnCall call(f.runtime->session->Map(),f.runtime->session->Monsters(),
                f.runtime->gameplay->Deaths().Requests(),*monster,ordinary,f.runtime->monsterServices);
            csol::scenario::ConfigError error;
            Require(ordinary->ResetForSpawn(call,*std::const_pointer_cast<CSOLMonsterSpawnState>(state),error),
                "real same-lease ResetForSpawn in post-health engine callback");
        };
        capture.Clear();monster->TakeDamage(f.survivor->pev,f.survivor->pev,10,DMG_BULLET);
        f.engine.onOffset={};
        if(mode=="reset")Check(resets==1&&monster->Pool()->Lease(*monster)==lease&&monster->Behavior()==ordinary,
            "reset preserves pool lease and behavior identity");
        else Check(resets==1&&f.runtime->Closing(),"actual callback retires runtime after health damage");
        Check(capture.Numbers().empty(),"replaced same-lease spawn state cancels old damage number");
        f.engine.Clear();return;
    }
    capture.Clear();float before=target->pev->health;
    target->TakeDamage(f.survivor->pev,f.survivor->pev,10,DMG_BULLET);
    Check(target->pev->health<before,"real accepted damage changes health");
    capture.Expect(*target,int(before-target->pev->health));
    if(mode=="wall")
    {
        capture.Clear();before=target->pev->health;
        target->TakeDamage(f.survivor->pev,f.survivor->pev,10,DMG_BULLET);
        capture.Expect(*target,int(before-target->pev->health));
    }
    else
    {
        capture.Clear();before=target->pev->health;
        ClearMultiDamage();TraceResult trace{};trace.pHit=target->edict();trace.iHitgroup=HITGROUP_HEAD;
        trace.vecEndPos=target->pev->origin;
        target->TraceAttack(f.survivor->pev,2,Vector(1,0,0),&trace,DMG_BULLET);
        target->TraceAttack(f.survivor->pev,2,Vector(1,0,0),&trace,DMG_BULLET);
        Check(capture.Numbers().empty(),"trace/pellets do not independently send damage numbers");
        ApplyMultiDamage(f.survivor->pev,f.survivor->pev);
        capture.Expect(*target,int(before-target->pev->health),4);
        monster->HostState().lastHitgroup=HITGROUP_CHEST;
        capture.Clear();f.runtime->room.floatingDamageDisplay=0;
        target->TakeDamage(f.survivor->pev,f.survivor->pev,1,DMG_BULLET);
        Check(capture.Numbers().empty(),"server room option suppresses actual attack notice");
        f.runtime->room.floatingDamageDisplay=1;
        capture.Clear();target->pev->takedamage=DAMAGE_NO;
        target->TakeDamage(f.survivor->pev,f.survivor->pev,10,DMG_BULLET);
        Check(capture.Numbers().empty(),"rejected invulnerable target has no number");target->pev->takedamage=DAMAGE_AIM;
    }
    capture.Clear();target->pev->health=3;before=target->pev->health;
    const auto result=target->TakeDamage(f.survivor->pev,f.survivor->pev,1000,DMG_BULLET);
    if(monster)Check(result==FALSE,"real lethal monster return remains FALSE");
    capture.Expect(*target,int(before-target->pev->health));
    f.engine.Clear();
}

void Ordinary(const std::filesystem::path& resources)
{
    // Reuse the complete engine boundary setup, then install actual ordinary
    // rules. No fake damage virtual or patched health implementation.
    gate_session::Fixture f(resources);
    f.engine.Clear();f.runtime.reset();f.engine.Prepare("0");
    g_pGameRules=InstallGameRules();Require(g_pGameRules&&!CSOL_RulesRejected(),"ordinary installed rules");
    f.engine.files["resource/FloatingDamage/FloatingDamageInfo.jsoo"]=difficulty_test::Encrypt(R"({"FLOATING_DMG_STYLE":{"MODE":[0]}})");
    CSOL_ActivateDamageNumbers();
    Capture capture;
    const auto player=[&](int slot,TeamName team) {
        auto* ed=f.engine.Reserve(slot,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags=FL_CLIENT;ed->v.health=ed->v.max_health=1000;ed->v.deadflag=DEAD_NO;ed->v.takedamage=DAMAGE_AIM;
        p->m_iTeam=team;p->m_iJoiningState=JOINED;p->m_iNumSpawns=1;p->has_disconnected=false;p->m_bJustConnected=false;
        ed->v.netname=Engine::String("damage-sdk");return p;
    };
    auto* source=player(1,CT);auto* target=player(2,TERRORIST);
    // Enter the actual shared post-armor commit for both ReGameDLL branches.
    // The preceding armor calculation is separately covered by SDK damage tests.
    for(bool explosion:{false,true})
    {
        capture.Clear();target->m_LastHitGroup=HITGROUP_CHEST;
        target->GoldCraft_CommitEquipmentDamage(source->pev,source->pev,12.75f,DMG_BULLET,source,FALSE,0,explosion);
        capture.Expect(*target,12);
    }
    capture.Clear();target->m_LastHitGroup=HITGROUP_HEAD;
    target->GoldCraft_CommitEquipmentDamage(source->pev,source->pev,4,DMG_BULLET,source,FALSE,0,false);
    capture.Expect(*target,4,4);
    capture.Clear();f.engine.Set("csol_damage_numbers","0");
    target->GoldCraft_CommitEquipmentDamage(source->pev,source->pev,4,DMG_BULLET,source,FALSE,0,false);
    Check(capture.Numbers().empty(),"ordinary room option off");
    f.engine.Set("csol_damage_numbers","1");
    for(const auto bits:{0x08000000u,0x08000002u,0x80000000u})
    {
        capture.Clear();CSOLAttackNumberCall call(*target,source->pev);
        Require(call.Notify(12.75f,std::bit_cast<int>(bits),HITGROUP_CHEST,8,false),"actual common sender accepts flags");
        capture.Expect(*target,12,bits==0x08000000u?1:0,2|int(bits>>31));
    }
    capture.Clear();CSOLAttackNumberCall suppressed(*target,source->pev);
    Require(suppressed.Notify(20,DMG_BULLET,0,0,true),"context suppression retains current call");
    Check(capture.Numbers().empty(),"suppressed context has no message");
    auto* botEd=f.engine.Reserve(3,"player");
    auto* bot=GetClassPtr<CCSPlayer>(reinterpret_cast<CCSBot*>(&botEd->v));
    botEd->v.flags=FL_CLIENT|FL_FAKECLIENT;botEd->v.health=100;botEd->v.deadflag=DEAD_NO;
    bot->m_iTeam=CT;bot->m_iNumSpawns=1;bot->has_disconnected=false;
    Require(bot->IsBot()!=FALSE,"actual SDK Bot virtual reports Bot identity");
    CSOLAttackNumberCall botCall(*target,bot->pev);
    Require(botCall.Notify(20,DMG_BULLET,0,0,false),"Bot source remains valid");
    Check(capture.Numbers().empty(),"Bot source receives no number");
    CSOLAttackNumberCall pending(*target,source->pev);
    ++target->m_iNumSpawns;
    Check(!pending.Notify(50,DMG_BULLET,0,0,false),"same-edict player respawn cancels pending notice");
    capture.Clear();CSOLAttackNumberCall duringMessage(*target,source->pev);
    capture.beforeEnd=[&] {
        CSOLAttackNumberCall nested(*target,source->pev);
        nested.Notify(99,DMG_BULLET,0,0,false);
        CSOL_ResetDamageNumbers();
    };
    Check(!duringMessage.Notify(7,DMG_BULLET,0,0,false),"message callback retires captured map");
    capture.beforeEnd={};capture.Expect(*target,7);
    capture.Clear();CSOLAttackNumberCall mapPending(*target,source->pev);CSOL_ResetDamageNumbers();
    Check(!mapPending.Notify(50,DMG_BULLET,0,0,false)&&capture.Numbers().empty(),"deactivate cancels captured map");
    f.engine.Clear();
}
}

int main(int argc,char** argv)
{
    try
    {
        sdk_test::Require(argc==3,"mode and frozen resource directory required");
        if(std::string_view(argv[1])=="player")attack_test::Ordinary(argv[2]);
        else attack_test::Scenario(argv[2],argv[1]);
    }
    catch(const std::exception& error){++sdk_test::failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"attack SDK checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
    return sdk_test::failures?1:0;
}
