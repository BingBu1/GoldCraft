// SPDX-License-Identifier: GPL-3.0-or-later
// Layer B links complete production SDK units, real rules/player constructors,
// real hookchains and API wrappers. Only engine edges and probe entities vary.
#include "sdk_engine.hpp"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../../native/csol/server/scenario_monster_pool.h"
#include "../../native/csol/server/weapons.h"
#include "../csol-scenario-difficulty/support.hpp"
#include "../csol-scenario-navigation-loader/support.hpp"
#include <bit>
#include <cstddef>

extern "C" void worldspawn(entvars_t*);
extern "C" void playerspawn(entvars_t*);
extern "C" void zombiespawn(entvars_t*);
extern "C" void zbstrigger(entvars_t*);
using namespace sdk_test;
void ScenarioDropCases(sdk_test::Engine& engine);
void ScenarioEntityAccessCases(sdk_test::Engine& engine);
void ScenarioProtectionCases(sdk_test::Engine& engine);
void ScenarioDeathRuntimeCases(sdk_test::Engine& engine);
void ScenarioDeathServicesCases(sdk_test::Engine& engine);
void ScenarioMonsterServicesCases(sdk_test::Engine& engine);
void ScenarioWaveServicesCases(sdk_test::Engine& engine);
void ScenarioDamageStatisticsCases(sdk_test::Engine& engine);
void ScenarioDamageBuffCases(sdk_test::Engine& engine);
void ScenarioDamageEffectCases(sdk_test::Engine& engine);
namespace
{
int installs{}, frees{}, deactivates{}, playerSpawns{}, playerPre{}, playerPost{};
CGameRules* InstallHook(IReGameHook_InstallGameRules* chain)
{ ++installs;Engine::self->Record("hook-install-before");auto* result=chain->callNext();Engine::self->Record("hook-install-after");return result; }
void FreeHook(IReGameHook_FreeGameRules* chain,CGameRules** rules)
{ ++frees;chain->callNext(rules); }
void DeactivateHook(IReGameHook_CSGameRules_ServerDeactivate* chain)
{ ++deactivates;chain->callNext(); }
void SpawnHook(IReGameHook_CBasePlayer_Spawn* chain,CBasePlayer* p) { ++playerSpawns;chain->callNext(p); }
void PreHook(IReGameHook_CBasePlayer_PreThink* chain,CBasePlayer* p) { ++playerPre;chain->callNext(p); }
void PostHook(IReGameHook_CBasePlayer_PostThink* chain,CBasePlayer* p) { ++playerPost;chain->callNext(p); }
void Hooks(bool install)
{
    if(install)
    {
        g_ReGameHookchains.m_InstallGameRules.registerHook(InstallHook,128);
        g_ReGameHookchains.m_FreeGameRules.registerHook(FreeHook,128);
        g_ReGameHookchains.m_CSGameRules_ServerDeactivate.registerHook(DeactivateHook,128);
        g_ReGameHookchains.m_CBasePlayer_Spawn.registerHook(SpawnHook,128);
        g_ReGameHookchains.m_CBasePlayer_PreThink.registerHook(PreHook,128);
        g_ReGameHookchains.m_CBasePlayer_PostThink.registerHook(PostHook,128);
    }
    else
    {
        g_ReGameHookchains.m_InstallGameRules.unregisterHook(InstallHook);
        g_ReGameHookchains.m_FreeGameRules.unregisterHook(FreeHook);
        g_ReGameHookchains.m_CSGameRules_ServerDeactivate.unregisterHook(DeactivateHook);
        g_ReGameHookchains.m_CBasePlayer_Spawn.unregisterHook(SpawnHook);
        g_ReGameHookchains.m_CBasePlayer_PreThink.unregisterHook(PreHook);
        g_ReGameHookchains.m_CBasePlayer_PostThink.unregisterHook(PostHook);
    }
}
void Install(Engine& e)
{
    FreeGameRules(&g_pGameRules);const int count=installs;g_pGameRules=InstallGameRules();
    Require(g_pGameRules && installs==count+1,"real installation hook calls actual rules constructor");
    const auto command=std::find(e.events.begin(),e.events.end(),"command:exec game.cfg\n");
    const auto execute=std::find(e.events.begin(),e.events.end(),"execute");
    Check(command!=e.events.end()&&execute!=e.events.end()&&command<execute,"game.cfg submitted then executed");
}
void Rules(Engine& e)
{
    for(const char* mode:{"0","1"})
    {
        e.events.clear();e.Prepare(mode);Install(e);
        Check(!CSOL_RulesRejected()&&!CSOL_UsesScenarioRules()&&typeid(*g_pGameRules)==typeid(CHalfLifeMultiplay),"confirmed native mode keeps exact real Multiplayer class");
        Check(CSOL_ActivateGameRules(),"native rules activation allowed");e.Clear();
    }
    for(const char* mode:{"","-1","3","2.0","nan"})
    {
        e.Prepare(mode,false);Install(e);Check(CSOL_RulesRejected(),"invalid locked mode rejects rules");
        Check(dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules)&&g_pGameRules->IsGameOver(),"rejected rules preserve multiplayer ABI and game-over state");e.Clear();
    }
    e.Prepare("2",false);Check(!e.Modes().ConfirmPluginIsolation(1),"wrong policy cannot confirm mode");Install(e);
    const std::string original=CSOL_RulesRejectionReason();e.Set("csol_game_mode","0");e.Modes().EndMap();CSOL_RejectGameRules("replacement");
    Check(CSOL_RulesRejected()&&original==CSOL_RulesRejectionReason(),"cvar EndMap and later errors cannot unlock or replace rejection");
    e.Prepare("0");Install(e);Check(!CSOL_RulesRejected(),"next install clears old rejection after new policy confirmation");
    CSOL_RejectGameRules(nullptr);Check(std::strlen(CSOL_RulesRejectionReason())>0,"null rejection gets fallback text");
    Install(e);std::string longReason(500,'x');CSOL_RejectGameRules(longReason.c_str());
    Check(std::strlen(CSOL_RulesRejectionReason())==255,"rejection safely truncates to 255 bytes");
    e.globals.deathmatch=0;Install(e);
    Check(!CSOL_RulesRejected()&&dynamic_cast<CHalfLifeTraining*>(g_pGameRules),"training uses actual constructor without multiplayer policy");
    e.Clear();e.globals.deathmatch=1;e.Prepare("2");Install(e);
    auto* scenario=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
    Require(scenario&&CSOL_UsesScenarioRules()&&!CSOL_RulesRejected(),"confirmed mode2 installs real scenario rules without rejecting the map");
    Check(!scenario->IsGameOver()&&!scenario->IsGameStarted()&&scenario->IsFreezePeriod()&&!CSOL_ScenarioSelecting(),
        "installed mode2 is loading rather than game-over, selectable or combat-ready");
    Check(scenario->TeamFull(CT)&&scenario->TeamFull(TERRORIST),
        "loading rules deny both teams until real resources and spawn collection activate selection");
    e.Clear();const int count=frees;FreeGameRules(&g_pGameRules);FreeGameRules(nullptr);
    Check(!g_pGameRules&&frees==count+2,"real free hook tolerates repeated null owners");
}
void CheckFrozen(const edict_t& ed)
{
    Check(ed.v.velocity==g_vecZero&&ed.v.avelocity==g_vecZero&&ed.v.basevelocity==g_vecZero&&ed.v.nextthink==0,"all movement and nextthink cleared");
    Check(ed.v.movetype==MOVETYPE_NONE&&ed.v.takedamage==DAMAGE_NO&&ed.v.button==0&&ed.v.impulse==0,"rejected entity cannot move attack or take damage");
    if(ed.v.flags&(FL_CLIENT|FL_FAKECLIENT))Check(ed.v.solid==SOLID_NOT&&(ed.v.effects&EF_NODRAW)&&ed.v.deadflag==DEAD_DEAD&&ed.v.maxspeed==1,"rejected client hidden frozen and non-solid");
}
void Callbacks(Engine& e)
{
    e.Prepare("1",false);Install(e);Require(CSOL_RulesRejected(),"callback matrix starts rejected");
    for(int kind=0;kind<3;++kind)
    {
        auto* ed=e.Reserve(kind+1,"player");ed->v.flags=FL_CLIENT;ed->v.origin=Vector(12,34,56);ed->v.view_ofs=Vector(1,2,3);
        CBasePlayer* player{};Probe* wrong{};
        if(kind==1)player=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        if(kind==2)wrong=GetClassPtr<CCSEntity>(reinterpret_cast<Probe*>(&ed->v));
        std::vector<unsigned char> wrongBefore;
        if(wrong)wrongBefore.assign(reinterpret_cast<unsigned char*>(wrong),reinterpret_cast<unsigned char*>(wrong)+sizeof(*wrong));
        char reason[130];std::memset(reason,0x71,sizeof(reason));
        Check(!e.api.pfnClientConnect(ed,"fixture","loopback",reason)&&reason[127]=='\0'&&reason[128]==0x71&&reason[129]==0x71,"connect rejection bounded without bridge connection");
        // Data callbacks intentionally run before PutInServer: empty private
        // must be safe too, not silently converted to a valid player first.
        struct WeaponBuffer{std::uint32_t pre{0x12345678};std::array<weapon_data_t,MAX_WEAPONS> data;std::uint32_t post{0x87654321};} weapon;
        std::memset(weapon.data.data(),0x55,sizeof(weapon.data));
        Check(e.api.pfnGetWeaponData(ed,weapon.data.data())==1,"GetWeaponData succeeds with inactive data");
        Check(std::all_of(reinterpret_cast<unsigned char*>(weapon.data.data()),reinterpret_cast<unsigned char*>(weapon.data.data())+sizeof(weapon.data),[](auto c){return c==0;}),"all prediction weapon data cleared");
        Check(weapon.pre==0x12345678&&weapon.post==0x87654321,"weapon data boundary guards preserved");
        clientdata_t data,expected;std::memset(&data,0x55,sizeof(data));std::memset(&expected,0,sizeof(expected));
        expected.flags=FL_CLIENT|FL_SPECTATOR|FL_FROZEN;expected.deadflag=DEAD_DEAD;expected.maxspeed=1;std::memcpy(&expected.origin,&ed->v.origin,sizeof(expected.origin));
        e.api.pfnUpdateClientData(ed,1,&data);Check(std::memcmp(&data,&expected,sizeof(data))==0,"UpdateClientData emits exact inactive snapshot for every private type");
        unsigned char* pvs{},*pas{};e.api.pfnSetupVisibility(nullptr,ed,&pvs,&pas);
        Check(pvs==e.pvs.data()&&pas==e.pas.data()&&e.pvsOrigin==Vector(13,36,59)&&e.pasOrigin==e.pvsOrigin,"visibility initializes PVS and PAS from entity eye origin");
        entity_state_t packet;std::memset(&packet,0x4b,sizeof(packet));const auto packetBefore=packet;
        Check(!e.api.pfnAddToFullPack(&packet,kind+1,ed,ed,0,TRUE,pvs)&&std::memcmp(&packet,&packetBefore,sizeof(packet))==0,"FullPack refuses without touching packet");
        customization_t customization{};customization.resource.type=t_decal;customization.nUserData2=99;
        e.api.pfnPlayerCustomization(ed,&customization);e.api.pfnSpectatorConnect(ed);e.api.pfnSpectatorThink(ed);e.api.pfnSpectatorDisconnect(ed);
        usercmd_t command{};command.buttons=IN_ATTACK;ed->v.groupinfo=7;const int groups=e.groupCalls;
        e.api.pfnCmdStart(ed,&command,12345);Check(e.groupCalls==groups,"rejected CmdStart does not set group trace");
        e.api.pfnCmdEnd(ed);Check(e.groupCalls==groups+1&&e.lastGroupMask==0,"rejected CmdEnd clears previously active group trace");
        const float suicide=player?player->m_fNextSuicideTime:0;e.api.pfnClientKill(ed);
        if(player)Check(player->m_fNextSuicideTime==suicide,"ClientKill cannot arm or execute suicide in rejected mode");
        e.api.pfnClientCommand(ed);InternalCommand(ed,"kill","");e.api.pfnClientUserInfoChanged(ed,e.info.data());
        e.api.pfnPlayerPreThink(ed);e.api.pfnPlayerPostThink(ed);e.api.pfnClientDisconnect(ed);
        if(wrong)Check(std::memcmp(wrong,wrongBefore.data(),wrongBefore.size())==0,"network command spectator disconnect preserve non-player object bytes");
        const int allocationCount=e.allocations;auto* prior=ed->pvPrivateData;
        Check(e.api.pfnSpawn(ed)==0,"rejected client DispatchSpawn does not request client deletion");
        e.api.pfnClientPutInServer(ed);e.api.pfnClientPutInServer(ed);
        Check(e.allocations==allocationCount+(kind==0?1:0),"only an empty private slot allocates one SDK player");
        if(kind!=0)Check(ed->pvPrivateData==prior,"existing private object identity preserved");
        if(wrong)Check(std::memcmp(wrong,wrongBefore.data(),wrongBefore.size())==0,"PrepareRejectedPlayer refuses wrong dynamic type without member writes");
        else
        {
            player=dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(ed));Require(player&&player->m_pEntity,"empty or real player has actual API wrapper");
            Check(player->m_pEntity->m_pContainingEntity==player,"API wrapper points to real containing player");
            const int sp=playerSpawns,pr=playerPre,po=playerPost;
            player->Spawn();player->PreThink();player->PostThink();
            Check(playerSpawns==sp+1&&playerPre==pr+1&&playerPost==po+1,"real player wrappers traverse hookchains");
            player->Spawn_OrigFunc();player->PreThink_OrigFunc();player->PostThink_OrigFunc();
            Check(player->m_iTeam==SPECTATOR&&player->m_iJoiningState==JOINED&&!player->m_bJustConnected&&player->m_iMenu==Menu_OFF,"direct and hooked player methods preserve inactive player state");
        }
        CheckFrozen(*ed);Engine::CheckGuards(ed);Engine::FreePrivate(ed);e.newApi.pfnOnFreeEntPrivateData(ed);
    }
    auto* ed=e.Reserve(50,"fixture_probe");auto* probe=GetClassPtr<CCSEntity>(reinterpret_cast<Probe*>(&ed->v));
    ed->v.solid=SOLID_BSP;ed->v.velocity=Vector(2,3,4);ed->v.avelocity=Vector(1,1,1);ed->v.basevelocity=Vector(4,4,4);ed->v.nextthink=11;
    Check(e.api.pfnSpawn(ed)==-1&&probe->spawns==0,"non-client rejected Spawn skips real entity callback");
    e.api.pfnThink(ed);e.api.pfnTouch(ed,ed);e.api.pfnUse(ed,ed);e.api.pfnBlocked(ed,ed);
    Check(!probe->thinks&&!probe->touches&&!probe->uses&&!probe->blocks,"rejected dispatch callbacks do not reach probe behavior");
    CheckFrozen(*ed);Check(ed->v.solid==SOLID_BSP,"brush solidity retained");e.Clear();
    e.Prepare("0");Install(e);auto* ordinary=e.Reserve(1,"player");
    auto* normalPlayer=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ordinary->v));
    ordinary->v.iuser1=OBS_CHASE_LOCKED;normalPlayer->m_fNextSuicideTime=e.globals.time+2;
    e.api.pfnClientKill(ordinary);Check(normalPlayer->m_fNextSuicideTime==e.globals.time+2,"normal ClientKill retains existing suicide cooldown");
    normalPlayer->m_fNextSuicideTime=0;e.api.pfnClientKill(ordinary);
    Check(normalPlayer->m_fNextSuicideTime==e.globals.time+1,"normal ClientKill reaches real Kill observer guard after setting cooldown");e.Clear();
}
void CombatWorldResources(Engine& e);

enum class ClearTargetAction { Count, RemoveCaller, RespawnCaller };
int clearTargetUses{};
class ClearPointTarget final : public CBaseEntity
{
public:
    int uses{};
    bool removeOnPlayerQuery{};
    EHANDLE activator, caller;
    USE_TYPE use{USE_OFF};
    ClearTargetAction action{ClearTargetAction::Count};
    void Use(CBaseEntity* who,CBaseEntity* from,USE_TYPE kind,float) override
    {
        ++uses;++clearTargetUses;activator=who;caller=from;use=kind;
        const auto next=action;action=ClearTargetAction::Count;
        if(next==ClearTargetAction::RemoveCaller&&from)Engine::Remove(from->edict());
        else if(next==ClearTargetAction::RespawnCaller&&from)
        {
            auto* trigger=dynamic_cast<CBaseTrigger*>(from);
            if(trigger)
            {
                trigger->m_flWait=7;trigger->pev->solid=SOLID_TRIGGER;
                trigger->SetThink(&CBaseEntity::SUB_Remove);trigger->SetTouch(&CBaseTrigger::MultiTouch);
                trigger->SetUse(&CBaseTrigger::CounterUse);
            }
            from->Spawn();
        }
    }
    BOOL IsPlayer() override
    {
        if(removeOnPlayerQuery){removeOnPlayerQuery=false;Engine::Remove(edict());}
        return FALSE;
    }
};
int clearMasterQueries{};
class ClearPointMaster final : public CBaseEntity
{
public:
    bool allowed{},removeOnCaps{};
    int ObjectCaps() override
    {
        if(removeOnCaps){removeOnCaps=false;Engine::Remove(edict());}
        return FCAP_MASTER;
    }
    BOOL IsTriggered(CBaseEntity*) override { ++clearMasterQueries;return allowed?TRUE:FALSE; }
};
edict_t* clearMovedirVictim{};
edict_t* clearMessageVictim{};
char* clearMessageOriginal{};
std::string clearMessageWritten;
int clearMessageEnds{};
edict_t* clearAlertVictim{};
edict_t* clearVarsVictim{};
void ClearPointAlert(ALERT_TYPE type,const char* format,...)
{
    char text[2048];va_list args;va_start(args,format);
    std::vsnprintf(text,sizeof(text),format,args);va_end(args);
    Engine::Alert(type,"%s",text);
    if(std::strstr(text,"Found: clearpoint_boundary_target, firing (")&&clearAlertVictim)
    {
        auto* victim=clearAlertVictim;clearAlertVictim=nullptr;Engine::Remove(victim);
    }
}
class ClearPointRecipient final : public CBaseEntity
{
public:
    EHANDLE removeOnQuery;
    BOOL IsPlayer() override { return TRUE; }
    BOOL IsNetClient() override
    {
        if(auto* entity=removeOnQuery.Get<CBaseEntity>())Engine::Remove(entity->edict());
        return TRUE;
    }
};

void ClearPointCases(Engine& e,CBasePlayer& player)
{
    const auto oldMakeVectors=g_engfuncs.pfnMakeVectors;
    const auto oldMessageBegin=g_engfuncs.pfnMessageBegin;
    const auto oldWriteString=g_engfuncs.pfnWriteString;
    const auto oldMessageEnd=g_engfuncs.pfnMessageEnd;
    const auto oldAlert=g_engfuncs.pfnAlertMessage;
    const auto oldOffsetEntity=g_engfuncs.pfnPEntityOfEntOffset;
    struct ResetEdges
    {
        Engine& engine;float time;decltype(oldMakeVectors) makeVectors;
        decltype(oldMessageBegin) messageBegin;decltype(oldWriteString) writeString;decltype(oldMessageEnd) messageEnd;
        decltype(oldAlert) alert;decltype(oldOffsetEntity) offsetEntity;
        ~ResetEdges()
        {
            engine.onModel={};engine.onOrigin={};engine.onFind={};engine.onSound={};engine.onOffset={};
            engine.globals.time=time;g_engfuncs.pfnMakeVectors=makeVectors;clearMovedirVictim=nullptr;
            g_engfuncs.pfnMessageBegin=messageBegin;g_engfuncs.pfnWriteString=writeString;g_engfuncs.pfnMessageEnd=messageEnd;
            clearMessageVictim=nullptr;clearMessageOriginal=nullptr;clearMessageWritten.clear();
            g_engfuncs.pfnAlertMessage=alert;g_engfuncs.pfnPEntityOfEntOffset=offsetEntity;
            clearAlertVictim=nullptr;clearVarsVictim=nullptr;
        }
    } reset{e,e.globals.time,oldMakeVectors,oldMessageBegin,oldWriteString,oldMessageEnd,oldAlert,oldOffsetEntity};
    auto target=[&](const char* name)
    {
        auto* ed=e.Reserve(e.nextEntity++,"clearpoint_target_probe");ed->v.targetname=Engine::String(name);
        return GetClassPtr<CCSEntity>(reinterpret_cast<ClearPointTarget*>(&ed->v));
    };
    auto create=[&](int flags=0,bool spawn=true)
    {
        // Create resolves the actual DLL export through GetProcAddress.
        auto* ed=Engine::Create(Engine::String("trigger_clearpoint"));ed->v.model=Engine::String("*1");ed->v.spawnflags=flags;
        auto* point=dynamic_cast<CCSOLScenarioClearPoint*>(CBaseEntity::Instance(ed));
        Require(point&&point->m_pEntity&&point->m_pEntity->m_pContainingEntity==point,
            "actual trigger_clearpoint export constructs the SDK class and API wrapper");
        if(spawn)Require(e.api.pfnSpawn(ed)==0,"actual clearpoint DispatchSpawn succeeds");
        return point;
    };
    auto key=[&](CBaseEntity& entity,const char* name,const char* value)
    {
        KeyValueData data{};data.szClassName=const_cast<char*>("trigger_clearpoint");
        data.szKeyName=const_cast<char*>(name);data.szValue=const_cast<char*>(value);
        e.api.pfnKeyValue(entity.edict(),&data);return data.fHandled!=FALSE;
    };
    auto* sink=target("clearpoint_counter");
    auto* point=create();point->pev->target=sink->pev->targetname;
    Check(point->pev->solid==SOLID_NOT&&point->pev->movetype==MOVETYPE_NONE&&
        (point->pev->effects&EF_NODRAW)&&point->m_flWait==-2.0f&&
        !point->m_pfnTouch&&!point->m_pfnUse&&!point->m_pfnThink,
        "clearpoint Spawn is hidden inert and preserves the original wait minus two contract");
    e.api.pfnTouch(point->edict(),player.edict());e.api.pfnUse(point->edict(),player.edict());e.api.pfnThink(point->edict());
    Check(sink->uses==0,"touch use and think cannot trigger clearpoint completion");
    Check(point->OnControls(nullptr)&&sink->uses==1&&sink->activator.Get<CBaseEntity>()==point&&
        sink->caller.Get<CBaseEntity>()==point&&sink->use==USE_TOGGLE,
        "null OnControls uses self and traverses the actual target chain once");
    Check(point->OnControls(player.pev)&&sink->uses==1,"activated clearpoint returns true without firing again");
    point->Restart();Check(point->OnControls(nullptr)&&sink->uses==1,"inherited Restart does not reset clearpoint activation");
    Check(!point->m_pfnThink&&point->pev->nextthink==static_cast<float>(static_cast<double>(e.globals.time)+0.1),
        "wait minus two retains the entity while preserving native nextthink assignment");
    const float initialTime=e.globals.time;e.globals.time=0x1p-28f;
    auto* rounded=create();Check(rounded->OnControls(nullptr)&&std::bit_cast<std::uint32_t>(rounded->pev->nextthink)==0x3dcccccdU,
        "clearpoint wait uses original float time promoted for double constant addition before final float rounding");
    e.globals.time=initialTime;

    for(const char* value:{"0","not-a-number","1","-2"})
    {
        auto* p=create();p->pev->target=sink->pev->targetname;
        Check(key(*p,"nogodmode",value),"exact nogodmode key is handled");
        const int before=sink->uses;const bool protection=std::atoi(value)==0;
        Check((p->OnControls(player.pev)!=FALSE)==protection&&sink->uses==before+1&&
            sink->activator.Get<CBaseEntity>()==&player,"nogodmode uses atoi zero and actual player activator");
        Check(p->OnControls(nullptr)&&sink->uses==before+1,"nogodmode does not change repeated true behavior");
    }
    auto* missingPrivate=e.Reserve(e.nextEntity++,"empty_activator");const int allocations=e.allocations;
    auto* empty=create();empty->pev->target=sink->pev->targetname;empty->pev->message=Engine::String("unused");
    const int beforeEmpty=sink->uses;const int allocatedWithPoint=e.allocations;
    Check(empty->OnControls(&missingPrivate->v)&&sink->uses==beforeEmpty+1&&!sink->activator.Get()&&
        e.allocations==allocatedWithPoint&&allocatedWithPoint==allocations+1,
        "nonnull entvars without private data stays null and does not allocate a replacement player");

    auto* masterEd=e.Reserve(e.nextEntity++,"clearpoint_master_probe");masterEd->v.targetname=Engine::String("clearpoint_master");
    auto* master=GetClassPtr<CCSEntity>(reinterpret_cast<ClearPointMaster*>(&masterEd->v));
    auto* blocked=create();blocked->pev->target=sink->pev->targetname;
    Check(key(*blocked,"master","clearpoint_master"),"master key reaches real CBaseToggle parser");
    const int beforeMaster=sink->uses;clearMasterQueries=0;
    Check(blocked->OnControls(player.pev)&&sink->uses==beforeMaster&&clearMasterQueries==1,
        "master refusal still completes the first OnControls protection decision");
    master->allowed=true;
    Check(blocked->OnControls(player.pev)&&sink->uses==beforeMaster&&clearMasterQueries==1,
        "later master enable does not refire a clearpoint already marked activated");
    auto* waiting=create();waiting->pev->target=sink->pev->targetname;waiting->pev->nextthink=e.globals.time+5;
    Check(waiting->OnControls(nullptr)&&sink->uses==beforeMaster,"future nextthink skips target activation");
    waiting->pev->nextthink=0;
    Check(waiting->OnControls(nullptr)&&sink->uses==beforeMaster,"nextthink refusal still marks the first call activated");
    auto* permitted=create();permitted->pev->target=sink->pev->targetname;permitted->m_sMaster=masterEd->v.targetname;
    const int beforePermitted=sink->uses;
    Check(permitted->OnControls(player.pev)&&sink->uses==beforePermitted+1,
        "enabled real master allows the target chain on a fresh clearpoint");
    auto* killVictim=target("clearpoint_killvictim");auto* killing=create();killing->pev->target=sink->pev->targetname;
    Check(key(*killing,"killtarget","clearpoint_killvictim"),"killtarget key retains the actual CBaseDelay parser");
    const int beforeKill=sink->uses;
    Check(killing->OnControls(nullptr)&&(killVictim->pev->flags&FL_KILLME)&&sink->uses==beforeKill+1,
        "actual killtarget removal precedes the still-valid target chain");
    auto* selfKill=create();selfKill->pev->target=sink->pev->targetname;selfKill->pev->targetname=Engine::String("clearpoint_selfkill");
    Check(key(*selfKill,"killtarget","clearpoint_selfkill"),"self killtarget configured through the SDK parser");
    const int beforeSelfKill=sink->uses;
    Check(!selfKill->OnControls(nullptr)&&(selfKill->pev->flags&FL_KILLME)&&sink->uses==beforeSelfKill,
        "killtarget marking this clearpoint for removal cancels subsequent targets");

    auto* noReset=create(SF_TRIGGER_NORESET);noReset->pev->target=sink->pev->targetname;
    Check(noReset->OnControls(nullptr)&&!noReset->m_pfnThink,"clearpoint bit six cannot arm removal for wait minus two");
    const EHANDLE noResetHandle(noReset);e.globals.time+=0.2f;e.api.pfnThink(noReset->edict());
    Check(noResetHandle.Get<CBaseEntity>()==noReset&&!(noReset->pev->flags&FL_KILLME),"clearpoint remains after its assigned nextthink");
    auto* ordinaryEd=e.Reserve(e.nextEntity++,"trigger");
    auto* ordinary=GetClassPtr<CCSTrigger>(reinterpret_cast<CBaseTrigger*>(&ordinaryEd->v));
    ordinary->m_flWait=-2;ordinary->pev->spawnflags=SF_TRIGGER_NORESET;ordinary->ActivateMultiTrigger(&player);
    Check(ordinary->m_pfnThink!=nullptr,"ordinary trigger retains ReGameDLL NORESET removal behavior");
    const EHANDLE ordinaryHandle(ordinary);e.api.pfnThink(ordinaryEd);
    Check(ordinaryEd->free&&!ordinaryHandle.Get<CBaseEntity>(),"ordinary NORESET trigger still removes through the SDK Think");

    auto* removed=create();auto* removedEd=removed->edict();const EHANDLE removedHandle(removed);
    removed->pev->target=sink->pev->targetname;sink->action=ClearTargetAction::RemoveCaller;
    Check(!removed->OnControls(player.pev)&&!removedHandle.Get<CBaseEntity>()&&removedEd->free,
        "target deleting the clearpoint cancels the base trigger tail and activation write");
    auto* respawned=create();respawned->pev->target=sink->pev->targetname;sink->action=ClearTargetAction::RespawnCaller;
    const int beforeRespawn=sink->uses;
    Check(!respawned->OnControls(nullptr)&&sink->uses==beforeRespawn+1,
        "target respawning the same entity invalidates the previous revision");
    Check(respawned->pev->solid==SOLID_NOT&&respawned->m_flWait==-2&&!respawned->m_pfnThink&&
        !respawned->m_pfnTouch&&!respawned->m_pfnUse,
        "explicit nested Spawn completes a new operation and resets deliberately polluted trigger callbacks and wait");
    respawned->pev->nextthink=0;
    Check(respawned->OnControls(nullptr)&&sink->uses==beforeRespawn+2,
        "new Spawn remains unactivated after its cancelled previous OnControls returns");

    for(bool sound:{false,true})
    {
        auto* p=create();p->pev->target=sink->pev->targetname;auto* ed=p->edict();const EHANDLE handle(p);
        if(sound)
        {
            p->pev->noise=Engine::String("clearpoint.wav");
            e.onSound=[&](edict_t* source,const char*){if(source==ed){e.onSound={};Engine::Remove(ed);}};
        }
        else
        {
            p->m_sMaster=masterEd->v.targetname;
            e.onFind=[&](edict_t*,const char*,const char* name)
            {if(std::strcmp(name,"clearpoint_master")==0){e.onFind={};Engine::Remove(ed);}};
        }
        const int before=sink->uses;
        Check(!p->OnControls(player.pev)&&!handle.Get<CBaseEntity>()&&sink->uses==before,
            sound?"sound callback deletion cancels before targets":"master lookup deletion cancels before returned entity access");
        e.onSound={};e.onFind={};
    }
    auto* capsPoint=create();capsPoint->pev->target=sink->pev->targetname;capsPoint->m_sMaster=masterEd->v.targetname;
    master->removeOnCaps=true;const int queriesBefore=clearMasterQueries,usesBeforeCaps=sink->uses;
    Check(capsPoint->OnControls(player.pev)&&clearMasterQueries==queriesBefore&&sink->uses==usesBeforeCaps,
        "master deleting itself in ObjectCaps cannot receive a stale IsTriggered virtual call");

    // FNullEnt performs a real engine OFFSET callback between FIND and Use.
    // This is inherited operation cancellation, not a simulated map retirement.
    {
        auto* candidate=target("clearpoint_offset_cancel");auto* candidateEd=candidate->edict();
        auto* p=create();p->pev->target=candidateEd->v.targetname;bool allowed=true;
        const int before=clearTargetUses;
        e.onOffset=[&](const edict_t* ed){if(ed==candidateEd){e.onOffset={};allowed=false;}};
        {
            CSOLBreakableScope parentScope([&]{return allowed;});
            Check(!p->OnControls(nullptr)&&!allowed&&clearTargetUses==before,
                "OFFSET cancellation of inherited operation stops before target Use");
        }
        e.onOffset={};
        Check(p->OnControls(nullptr)&&candidate->uses==1,
            "cancelled OFFSET operation leaves clearpoint unactivated for a later valid call");
    }
    for(bool replaceTarget:{false,true})
    {
        const char* name=replaceTarget?"clearpoint_offset_replace":"clearpoint_offset_caller";
        auto* candidate=target(name);auto* candidateEd=candidate->edict();
        const EHANDLE candidateHandle(candidate);auto* p=create();auto* pointEd=p->edict();const EHANDLE pointHandle(p);
        p->pev->target=candidateEd->v.targetname;const int before=clearTargetUses;
        ClearPointTarget* replacement{};
        e.onOffset=[&](const edict_t* ed)
        {
            if(ed!=candidateEd)return;e.onOffset={};
            if(!replaceTarget){Engine::Remove(pointEd);return;}
            const int index=Engine::Index(candidateEd);Engine::Remove(candidateEd);
            auto* fresh=e.Reserve(index,"clearpoint_target_probe");fresh->v.targetname=Engine::String(name);
            replacement=GetClassPtr<CCSEntity>(reinterpret_cast<ClearPointTarget*>(&fresh->v));
        };
        const bool accepted=p->OnControls(nullptr)!=FALSE;e.onOffset={};
        Check(clearTargetUses==before&&(replaceTarget?accepted:!accepted),
            replaceTarget?"OFFSET slot replacement cannot dispatch the stale FIND candidate":"OFFSET deleting caller cancels before target Use");
        if(replaceTarget)Check(!candidateHandle.Get()&&replacement&&replacement->uses==0,
            "same edict index with new serial does not inherit the old target dispatch");
        else Check(!pointHandle.Get()&&pointEd->free,"OFFSET caller removal releases actual SDK private data");
    }
    for(bool removeCaller:{false,true})
    {
        auto* candidate=target("clearpoint_alert_boundary");auto* candidateEd=candidate->edict();
        candidateEd->v.classname=Engine::String("clearpoint_boundary_target");const EHANDLE candidateHandle(candidate);
        auto* p=create();auto* pointEd=p->edict();const EHANDLE pointHandle(p);p->pev->target=candidateEd->v.targetname;
        clearAlertVictim=removeCaller?pointEd:candidateEd;g_engfuncs.pfnAlertMessage=ClearPointAlert;
        const int before=clearTargetUses;const bool accepted=p->OnControls(nullptr)!=FALSE;
        g_engfuncs.pfnAlertMessage=oldAlert;
        Check(!clearAlertVictim&&clearTargetUses==before&&(removeCaller?!accepted:accepted),
            removeCaller?"Found ALERT deleting caller cancels before target Use":"Found ALERT deleting target prevents a stale virtual Use");
        Check(removeCaller?!pointHandle.Get():!candidateHandle.Get(),"ALERT boundary removes the intended actual SDK entity");
    }
    for(int boundary=0;boundary<3;++boundary)
    {
        const char* name=boundary==0?"clearpoint_vars_caller":boundary==1?"clearpoint_vars_victim":"clearpoint_player_query";
        auto* victim=target(name);auto* victimEd=victim->edict();const EHANDLE victimHandle(victim);
        victimEd->v.solid=SOLID_BBOX;const auto victimName=victimEd->v.targetname;
        auto* p=create();auto* pointEd=p->edict();const EHANDLE pointHandle(p);p->pev->target=sink->pev->targetname;
        Check(key(*p,"killtarget",name),"killtarget boundary uses the actual SDK parser");
        if(boundary==2)victim->removeOnPlayerQuery=true;
        else
        {
            clearVarsVictim=boundary==0?pointEd:victimEd;
            g_engfuncs.pfnPEntityOfEntOffset=[](int offset)
            {
                auto* ed=Engine::At(offset/static_cast<int>(sizeof(edict_t)));
                if(offset==0&&clearVarsVictim)
                {auto* removed=clearVarsVictim;clearVarsVictim=nullptr;Engine::Remove(removed);}
                return ed;
            };
        }
        const int before=sink->uses;const bool accepted=p->OnControls(nullptr)!=FALSE;
        g_engfuncs.pfnPEntityOfEntOffset=oldOffsetEntity;
        Check((boundary==0?!accepted:accepted)&&sink->uses==before+(boundary==0?0:1),
            boundary==0?"killtarget VARS world lookup deleting caller cancels remaining targets":
            boundary==1?"killtarget VARS world lookup deleting victim preserves the remaining valid target chain":
                "killtarget IsPlayer deleting itself avoids stale private writes and preserves valid targets");
        Check(victimEd->v.solid==SOLID_BBOX&&victimEd->v.targetname==victimName&&!(victimEd->v.flags&FL_KILLME),
            "cancelled UTIL_Remove does not write its solidity flags or targetname tail");
        Check(boundary==0?!pointHandle.Get():!victimHandle.Get(),"killtarget boundary releases the intended private object");
    }

    auto* recipientEd=e.Reserve(e.nextEntity++,"clearpoint_recipient_probe");
    auto* recipient=GetClassPtr<CCSEntity>(reinterpret_cast<ClearPointRecipient*>(&recipientEd->v));
    auto* queryPoint=create();queryPoint->pev->message=Engine::String("clearpoint query message");recipient->removeOnQuery=queryPoint;
    const auto beforeQuery=e.events.size();
    Check(!queryPoint->OnControls(recipient->pev)&&
        std::none_of(e.events.begin()+static_cast<std::ptrdiff_t>(beforeQuery),e.events.end(),
            [](const auto& event){return event.starts_with("message:");}),
        "IsNetClient callback deleting the trigger cancels before starting a message");
    recipient->removeOnQuery=nullptr;
    auto* messagePoint=create();messagePoint->pev->message=Engine::String("clearpoint owned message");
    clearMessageOriginal=const_cast<char*>(STRING(messagePoint->pev->message));clearMessageVictim=messagePoint->edict();
    clearMessageWritten.clear();clearMessageEnds=0;
    g_engfuncs.pfnMessageBegin=[](int,int,const float*,edict_t*)
    {
        auto* victim=clearMessageVictim;clearMessageVictim=nullptr;if(victim)Engine::Remove(victim);
        if(clearMessageOriginal)clearMessageOriginal[0]='X';
    };
    g_engfuncs.pfnWriteString=[](const char* text){clearMessageWritten=text;};
    g_engfuncs.pfnMessageEnd=[]{++clearMessageEnds;};
    Check(!messagePoint->OnControls(recipient->pev)&&clearMessageWritten=="clearpoint owned message"&&clearMessageEnds==1,
        "MessageBegin cancellation finishes one packet using owned text without re-reading removed entity or mutated engine string");
    g_engfuncs.pfnMessageBegin=oldMessageBegin;g_engfuncs.pfnWriteString=oldWriteString;g_engfuncs.pfnMessageEnd=oldMessageEnd;
    clearMessageOriginal=nullptr;

    for(bool brush:{false,true})
    {
        auto* p=create(0,false);auto* ed=p->edict();const EHANDLE handle(p);
        if(brush)e.onModel=[&]{auto* victim=ed;e.onModel={};Engine::Remove(victim);};
        else {p->pev->model=0;e.onOrigin=[&](edict_t* source){if(source==ed){e.onOrigin={};Engine::Remove(ed);}};}
        p->Spawn();Check(!handle.Get<CBaseEntity>(),brush?"InitTrigger model callback deletion cancels Spawn tail":"InitTrigger origin callback deletion cancels Spawn tail");
        e.onModel={};e.onOrigin={};
    }
    auto* moved=create(0,false);auto* movedEd=moved->edict();movedEd->v.angles=Vector(0,90,0);clearMovedirVictim=movedEd;
    g_engfuncs.pfnMakeVectors=[](const float*)
    {gpGlobals->v_forward=Vector(1,0,0);auto* victim=clearMovedirVictim;clearMovedirVictim=nullptr;if(victim)Engine::Remove(victim);};
    moved->Spawn();Check(movedEd->free&&movedEd->v.angles==Vector(0,90,0)&&movedEd->v.movedir==g_vecZero,
        "SetMovedir cancellation occurs before writing through the removed trigger entvars");
    g_engfuncs.pfnMakeVectors=oldMakeVectors;

    for(bool respawn:{false,true})
    {
        auto* p=create();p->pev->target=sink->pev->targetname;p->m_flDelay=0.25f;
        const int first=e.nextEntity,before=sink->uses;
        Check(p->OnControls(player.pev)&&sink->uses==before,"clearpoint delay retains the real target chain without early dispatch");
        Require(e.nextEntity==first+1,"one actual delayed-use entity allocated");
        auto* delayed=&e.edicts[first];const EHANDLE delayHandle(delayed);
        Require(std::strcmp(STRING(delayed->v.classname),"DelayedUse")==0,"actual delayed-use classname assigned");
        if(respawn)p->Spawn();
        e.globals.time+=0.3f;e.api.pfnThink(delayed);
        Check(sink->uses==before+(respawn?0:1)&&!delayHandle.Get<CBaseEntity>(),
            respawn?"retained delayed guard rejects an old Spawn revision and still cleans up":"delayed guard survives the OnControls stack and dispatches once");
    }
}

void World(Engine& e)
{
    e.Prepare("1",false);Install(e);Require(CSOL_RulesRejected(),"world transition starts with old rejection");
    const auto originalFiles=e.files;
    const auto originalMap=e.globals.mapname;
    struct RestoreMap { globalvars_t& globals; string_t map; ~RestoreMap(){globals.mapname=map;} } restoreMap{e.globals,originalMap};
    // Reject first: a successful world registers native messages once per
    // process, which would otherwise mask an erroneous later LinkUserMessages.
    for(bool validGroup:{false,true})
    {
        // Keep the real world dispatch and failure case while following the
        // newly installed concrete Combat services and their full resources.
        CombatWorldResources(e);
        std::string rounds="prefix0\r\nprefix1\r\nprefix2,100\r\nprefix3,1\r\nprefix4,1\r\nRewardRound,0\r\n"
            "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
            "0,1,0,0,0,0,0,0,0,0,0,";
        rounds+=validGroup?"1\r\n":"99\r\n";
        rounds+="1,0,0,0,0,0,0,0,0,0,0,1\r\n";
        e.files["maps/zbs_zs_deadend.cso"]=difficulty_test::Encrypt(rounds);
        e.Set("csol_scenario_difficulty","1");e.Prepare("2");
        auto* world=e.Reserve(0,"worldspawn");worldspawn(&world->v);
        const auto firstEvent=e.events.size();
        int poolAllocations{};e.onAllocate=[&] {
            if(std::strcmp(STRING(e.edicts[e.nextEntity-1].v.classname),"monster_entity")==0)++poolAllocations;
        };
        struct ClearAllocationHook { Engine& engine; ~ClearAllocationHook(){engine.onAllocate={};} } clearHook{e};
        Require(e.api.pfnSpawn(world)==0,"actual worldspawn clears prior rejection and completes Combat Precache");
        Require(CSOL_UsesScenarioRules()&&!CSOL_RulesRejected()&&!CSOL_ScenarioSelecting(),
            "Combat resources load before entity collection without claiming an active round");
        auto add=[&](const char* name,void(*fn)(entvars_t*)) {auto* ed=e.Reserve(e.nextEntity++,name);if(std::strcmp(name,"zbstrigger")==0)ed->v.model=Engine::String("*1");fn(&ed->v);Require(e.api.pfnSpawn(ed)==0,"marker SDK spawn succeeds");return ed;};
        auto* playerPoint=add("playerspawn",playerspawn);static_cast<CCSOLPlayerSpawn*>(playerPoint->pvPrivateData)->m_iId=1;
        playerPoint->v.origin=Vector(64,128,192);playerPoint->v.angles=Vector(0,90,0);
        auto* monsterPoint=add("zombiespawn",zombiespawn);auto* mp=static_cast<CCSOLMonsterSpawn*>(monsterPoint->pvPrivateData);mp->m_iId=1;mp->m_iTeam=1;
        for(int id:{1,2,30}){auto* ed=add("zbstrigger",zbstrigger);auto* t=static_cast<CCSOLScenarioTrigger*>(ed->pvPrivateData);t->m_iId=id;t->pev->mins=Vector(-8,-8,-8);t->pev->maxs=Vector(8,8,8);}
        auto* p=e.Reserve(e.nextEntity++,"fixture_probe");auto* probe=GetClassPtr<CCSEntity>(reinterpret_cast<Probe*>(&p->v));
        const int originalCount=e.nextEntity;const auto initialMessages=e.messages.size();const int initialDeactivate=deactivates;
        e.api.pfnServerActivate(e.edicts.data(),originalCount,e.globals.maxClients);
        Check((playerPoint->v.flags&FL_KILLME)&&(monsterPoint->v.flags&FL_KILLME),"real classname hash lets collection consume both marker classes");
        Check(e.nextEntity>originalCount,"collection appended proxy after captured edictCount");
        Check(poolAllocations==CCSOLMonsterPool::Capacity&&std::count(
            e.events.begin()+static_cast<std::ptrdiff_t>(firstEvent),e.events.end(),"load:maps/zs_deadend.nav")==1,
            "actual Combat world precaches its 45-slot monster pool and navigation once");
        if(validGroup)
        {
            auto* rules=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
            Require(rules&&!CSOL_RulesRejected()&&!CSOL_ScenarioSelecting(),
                "valid current spawn group activates the installed Combat services");
            Check(probe->activates==1&&e.messages.contains("CurWeapon")&&e.messages.contains("TeamInfo"),
                "accepted Combat world activates normal map entities and retains native client message registration");
            Check(!rules->IsGameOver()&&!rules->IsGameStarted()&&rules->IsFreezePeriod()&&
                !rules->m_bRoundTerminating&&rules->m_flRestartRoundTime==0,
                "Combat waits for an eligible player before scheduling its first round");
            Check(rules->m_iSpawnPointCount_CT==e.globals.maxClients&&rules->m_iSpawnPointCount_Terrorist==0&&
                !rules->TeamFull(CT)&&rules->TeamFull(TERRORIST),
                "one reusable CT marker admits the configured player capacity and keeps T unavailable");
            auto* ed=e.Reserve(1,"player");auto* player=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
            player->m_iTeam=CT;player->m_iJoiningState=JOINED;player->m_bJustConnected=false;
            player->m_iMenu=Menu_ChooseAppearance;
            player->pev->flags=FL_CLIENT;player->pev->deadflag=DEAD_RESPAWNABLE;
            Check(!rules->TeamFull(CT)&&!rules->FPlayerCanRespawn(player),
                "a provisional CT selection does not exhaust one reusable point or permit combat respawn");
            edict_t* observer{};
            Require(CSOL_ScenarioSelectSpawn(*player,observer)&&observer,
                "waiting Combat obtains a real scenario observer proxy without BeginRound");
            Check(std::strcmp(STRING(observer->v.classname),CCSOLPlayerSpawnProxy::Classname)==0&&
                observer->v.origin==Vector(64,128,192)&&observer->v.angles==Vector(0,90,0),
                "observer camera uses collected current-group coordinates rather than native CS spawn fallback");
            const int entities=e.nextEntity;const auto models=e.models.size();
            const auto playback=g_engfuncs.pfnPlaybackEvent;
            struct RestorePlayback { decltype(playback) old; ~RestorePlayback(){g_engfuncs.pfnPlaybackEvent=old;} } restorePlayback{playback};
            g_engfuncs.pfnPlaybackEvent=[](int flags,const edict_t*,unsigned short,float,float*,float*,float,float,int,int,int,int)
            {
                Check(flags==(FEV_GLOBAL|FEV_RELIABLE),"real cleanup emits the reliable global decal-reset event");
                Engine::self->Record("cleanup-decals");
            };
            rules->RestartRound();rules->CheckWinConditions();
            Check(!CSOL_RulesRejected()&&!CSOL_ScenarioSelecting()&&rules->IsGameStarted()&&
                !rules->m_bRoundTerminating&&rules->m_flRestartRoundTime==0&&!rules->FPlayerCanRespawn(player)&&
                e.nextEntity==entities&&e.models.size()==models&&poolAllocations==CCSOLMonsterPool::Capacity&&
                std::count(e.events.begin(),e.events.end(),"cleanup-decals")==1,
                "actual restart begins Combat using its precached pool while an unfinished character selection cannot respawn");

            // The real HE factory and production GameRules gate own the
            // source registration; neither classname nor a fake vtable does.
            auto* grenade=CGrenade::ShootTimed2(player->pev,Vector(16,32,48),Vector(10,0,0),2.0f,CT,0);
            Require(grenade&&!CSOL_RulesRejected(),"actual SDK HE creation succeeds through the current scenario gate");
            Check(CSOL_ScenarioProjectileSource(*grenade)==WEAPON_HEGRENADE&&
                CSOL_DamageWeaponId(grenade,player)==WEAPON_HEGRENADE,
                "real ShootTimed2 registers the HE source consumed by production damage attribution");
            grenade->pev->flags|=FL_KILLME;
            Check(CSOL_ScenarioProjectileSource(*grenade)==0&&CSOL_DamageWeaponId(grenade,player)==0,
                "pending HE removal cannot retain a valid damage source");
            grenade->pev->flags&=~FL_KILLME;
            auto* grenadeEdict=grenade->edict();const EHANDLE retiredGrenade(grenade);
            Engine::Remove(grenadeEdict);
            Require(!retiredGrenade.Get<CBaseEntity>(),"actual engine free retires HE identity by serial");
            auto* reused=e.Reserve(Engine::Index(grenadeEdict),"grenade");
            auto* unregistered=GetClassPtr<CCSGrenade>(reinterpret_cast<CGrenade*>(&reused->v));
            Check(CSOL_ScenarioProjectileSource(*unregistered)==0&&CSOL_DamageWeaponId(unregistered,player)==0,
                "same classname and actual CGrenade type in a recycled edict do not inherit old HE registration");
            Engine::Remove(reused);
            ClearPointCases(e,*player);
        }
        else
        {
            Check(CSOL_RulesRejected()&&!CSOL_ScenarioSelecting()&&
                std::strstr(CSOL_RulesRejectionReason(),"CT spawn group is empty"),
                "invalid current spawn group still rejects activation after real marker collection");
            Check(probe->activates==0&&e.messages.size()==initialMessages,
                "rejected scenario never activates entities or links later client messages");
            for(int i=originalCount;i<e.nextEntity;++i)CheckFrozen(e.edicts[i]);
            auto* late=e.Reserve(Engine::Capacity-1,"late_probe");late->v.movetype=MOVETYPE_PUSH;late->v.velocity=Vector(9,8,7);late->v.nextthink=99;
            e.api.pfnStartFrame();CheckFrozen(*late);
        }
        e.onAllocate={};
        e.api.pfnServerDeactivate();e.api.pfnServerDeactivate();
        Check(deactivates==initialDeactivate+1&&!g_bServerActive,"repeat deactivate invokes real rules hook once");
        for(auto& ed:e.edicts)if(auto* monster=dynamic_cast<CCSOLScenarioMonster*>(static_cast<CBaseEntity*>(ed.pvPrivateData)))
            Check(monster->Pool()&&!monster->Pool()->EntityAccessAllowed(),
                "shutdown revokes the retained pool before actual engine private-data release");
        e.Clear();
    }
    e.files=originalFiles;e.globals.mapname=originalMap;e.Prepare("0");auto* world=e.Reserve(0,"worldspawn");worldspawn(&world->v);
    Check(e.api.pfnSpawn(world)==0&&!CSOL_RulesRejected()&&!CSOL_UsesScenarioRules(),"new real worldspawn clears old failure and selects native rules");
    auto* native=e.Reserve(e.nextEntity++,"fixture_probe");auto* nativeProbe=GetClassPtr<CCSEntity>(reinterpret_cast<Probe*>(&native->v));
    e.api.pfnServerActivate(e.edicts.data(),e.nextEntity,e.globals.maxClients);
    Check(nativeProbe->activates==1&&!CSOL_RulesRejected(),"native ServerActivate retains entity activation");e.Clear();
}

// These cases use the actual InstallGameRules export, the production registry
// queries and complete ReAPI wrappers. No replacement rules or registry exist
// in this fixture. Owner lookup observes registration, not a destructor count.
struct RetirementCase
{
    CHalfLifeMultiplay* rules{};
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    bool retireBefore{}, retireInDestroy{};
    int beforeHooks{}, afterHooks{}, freePosts{}, destroys{};
};
RetirementCase* retirement{};

std::shared_ptr<const CSOLScenarioEntityAccess> CurrentScenarioAccess(CGameRules* rules)
{
    CSOLScenarioSdkScope scope(rules);
    Require(scope.Scenario()&&scope.Current(),"capture actual current scenario registration");
    auto access=scope.EntityAccess();
    Require(access&&access->Allowed(),"actual scenario owns a live entity-access token");
    return access;
}

RetirementCase NewRetirementCase(Engine& e)
{
    e.Clear();e.Prepare("2");Install(e);
    Require(CSOL_UsesScenarioRules()&&!CSOL_RulesRejected(),"retirement uses installed production scenario rules");
    auto* rules=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
    Require(rules!=nullptr,"actual scenario retains multiplayer ABI");
    return {rules,CurrentScenarioAccess(rules)};
}

void CheckRetained(RetirementCase& state)
{
    Check(!g_pGameRules&&!state.access->Allowed(),"retirement unpublishes rules and revokes old entity access immediately");
    Require(CSOL_ScenarioOwnerRules(state.access.get())==state.rules,
        "retired actual rules remain registered while the native call is in flight");
    CSOL_CollectRetiredScenarioRules();
    Require(CSOL_ScenarioOwnerRules(state.access.get())==state.rules,
        "collector cannot delete rules inside a complete SDK or ReAPI scope");
    Check(state.rules->m_bFreezePeriod==TRUE&&!state.rules->m_bGameStarted,
        "real retired multiplayer fields remain readable until enclosing callback returns");
}

void RetirementFreeHook(IReGameHook_FreeGameRules* chain,CGameRules** slot)
{
    auto* prior=slot?*slot:nullptr;
    chain->callNext(slot);
    if(retirement&&prior==retirement->rules)
    {
        ++retirement->freePosts;
        Check(slot&&!*slot,"FreeGameRules original clears caller slot before its post hook");
        CheckRetained(*retirement);
    }
}

void RetirementRulesHook(IHookChain<void>* chain)
{
    Require(retirement!=nullptr,"actual rules hook has an active observation context");
    auto& state=*retirement;
    ++state.beforeHooks;
    if(state.retireBefore)FreeGameRules(&g_pGameRules);
    chain->callNext();
    if(!state.retireBefore)FreeGameRules(&g_pGameRules);
    ++state.afterHooks;
    CheckRetained(state);
}

struct RetirementHooks
{
    explicit RetirementHooks(RetirementCase& state)
    {
        Require(!retirement,"retirement hook cases do not overlap");retirement=&state;
        g_ReGameHookchains.m_FreeGameRules.registerHook(RetirementFreeHook,192);
        g_ReGameHookchains.m_CSGameRules_Think.registerHook(RetirementRulesHook,192);
        g_ReGameHookchains.m_CSGameRules_RestartRound.registerHook(RetirementRulesHook,192);
    }
    ~RetirementHooks()
    {
        g_ReGameHookchains.m_CSGameRules_RestartRound.unregisterHook(RetirementRulesHook);
        g_ReGameHookchains.m_CSGameRules_Think.unregisterHook(RetirementRulesHook);
        g_ReGameHookchains.m_FreeGameRules.unregisterHook(RetirementFreeHook);
        retirement=nullptr;
    }
};

void CollectRetirement(RetirementCase& state)
{
    Require(CSOL_ScenarioOwnerRules(state.access.get())==state.rules,
        "scope exit only releases its pin and leaves collection pending");
    const int before=frees;
    CSOL_CollectRetiredScenarioRules();
    Check(!CSOL_ScenarioOwnerRules(state.access.get()),"actual collector removes completed retirement registration");
    CSOL_CollectRetiredScenarioRules();
    Check(!CSOL_ScenarioOwnerRules(state.access.get())&&frees==before,
        "repeated collection neither republishes rules nor reenters FreeGameRules hooks");
}

void RetirementHooksMatrix(Engine& e)
{
    for(bool restart:{false,true})for(bool before:{true,false})
    {
        auto state=NewRetirementCase(e);state.retireBefore=before;
        RetirementHooks hooks(state);
        e.Record(std::string("retirement:")+(restart?"restart:":"think:")+(before?"pre":"post"));
        if(restart)state.rules->RestartRound();else state.rules->Think();
        Check(state.beforeHooks==1&&state.afterHooks==1&&state.freePosts==1,
            "one real rules call traverses pre original and post retirement hook stages");
        CollectRetirement(state);
    }
}

void RetirementAliases(Engine& e)
{
    auto state=NewRetirementCase(e);RetirementHooks hooks(state);
    CGameRules* first=state.rules;
    CGameRules* second=state.rules;
    const int before=frees;
    FreeGameRules(&first);
    Check(!first&&!g_pGameRules,"local alias release immediately clears alias and matching global slot");
    Require(second==state.rules,"second raw alias is held only until actual collection");
    FreeGameRules(&second);
    Check(!second&&state.freePosts==2,"second local alias is nulled without creating a second owner");
    FreeGameRules(&first);FreeGameRules(nullptr);
    Check(frees==before+4,"duplicate empty slots still traverse the actual FreeGameRules hook contract");
    CollectRetirement(state);
}

class RetirementProbe final : public CBaseEntity
{
public:
    RetirementCase* state{}; // Observation context outlives this engine block.
    void OnDestroy() override
    {
        // An assertion can throw before Engine::FreePrivate releases storage.
        // Failed-case cleanup must not revisit an expired stack observation.
        if(!state){CBaseEntity::OnDestroy();return;}
        ++state->destroys;
        if(state->retireInDestroy)FreeGameRules(&g_pGameRules);
        CheckRetained(*state);
        CBaseEntity::OnDestroy();
    }
};

void RetirementFreeTail(Engine& e,bool closeInside)
{
    auto state=NewRetirementCase(e);state.retireInDestroy=closeInside;
    RetirementHooks hooks(state);
    auto* ed=e.Reserve(e.nextEntity++,"retirement_probe");
    auto* probe=GetClassPtr<CCSEntity>(reinterpret_cast<RetirementProbe*>(&ed->v));
    probe->state=&state;
    struct ProbeReset
    {
        edict_t* entity;RetirementProbe* object;
        ~ProbeReset(){if(entity->pvPrivateData==object)object->state=nullptr;}
    } probeReset{ed,probe};
    ed->v.globalname=Engine::String(closeInside?"retirement_inside":"retirement_before");
    const auto globalname=ed->v.globalname;
    gGlobalState.EntityAdd(globalname,e.globals.mapname,GLOBAL_ON);
    AddEntityHashValue(&ed->v,"retirement_probe",CLASSNAME);
    Require(UTIL_FindEntityByClassname(nullptr,"retirement_probe")==probe,
        "real SDK classname hash contains the private object before free");
    Require(probe->m_pEntity!=nullptr,"free probe owns real SDK API extension");
    Require(TheCSBots()==nullptr,"bounded free-tail case does not claim Bot-manager callback coverage");
    if(!closeInside)FreeGameRules(&g_pGameRules);
    int tailObservations{};
    struct EdgeReset { Engine& engine; ~EdgeReset(){engine.afterFreeCallback={};} } edgeReset{e};
    e.afterFreeCallback=[&](edict_t* current)
    {
        Require(current==ed,"free-tail observation belongs to the expected entity");
        ++tailObservations;
        Check(state.destroys==1,"real SDK dispatch invoked one virtual OnDestroy");
        Check(gGlobalState.EntityGetState(globalname)==GLOBAL_DEAD,
            "actual UpdateOnRemove tail updates global state after OnDestroy");
        Check(!UTIL_FindEntityByClassname(nullptr,"retirement_probe"),
            "actual SDK free tail removes classname hash before engine release");
        Check(current->pvPrivateData==probe&&probe->m_pEntity==nullptr,
            "API extension is deleted while engine still owns private memory");
        Check(CSOL_ScenarioOwnerRules(state.access.get())==state.rules,
            "OnFree scope exit has not drained rules before physical engine release");
    };
    const int released=e.releases;
    Engine::FreePrivate(ed);
    e.afterFreeCallback={};
    Check(tailObservations==1&&e.releases==released+1&&!ed->pvPrivateData,
        "fixture engine releases the block only after the complete actual SDK free tail");
    Check(state.freePosts==1,"free-tail retirement enters actual FreeGameRules once");
    CollectRetirement(state);
    gGlobalState.ClearStates();
}

void CombatWorldResources(Engine& e)
{
    using difficulty_test::Encrypt;
    e.files.clear();
    for(const auto& entry:session_test::Resources())
    {
        auto path=entry.first;auto bytes=entry.second;
        const auto position=path.find("map_session_test");
        if(position!=std::string::npos)path.replace(position,std::strlen("map_session_test"),"zs_deadend");
        if(path=="resource/maplist.csv")
        {
            const auto name=bytes.find("map_session_test"),row=bytes.find("\r\n1,fixture,");
            Require(name!=std::string::npos&&row!=std::string::npos,"world fixture has an explicit authored map identity");
            bytes.replace(name,std::strlen("map_session_test"),"zs_deadend");bytes.replace(row+2,1,"98");
        }
        e.files.emplace(std::move(path),std::move(bytes));
    }
    e.globals.mapname=Engine::String("zs_deadend");
    e.Set("csol_scenario_difficulty","1");
    e.files["maps/zbs_zs_deadend.cso"]=Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
        "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
        "0,1,0,0,0,0,0,0,0,0,0,1\r\n1,0,0,0,0,0,0,0,0,0,0,1\r\n");
    e.files["maps/ZBS_ZT_zs_deadend.cso"]=Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,0,255 255 255,0,1,0,0,0,1\r\n");
    e.files["maps/ZBS_DropItem.cso"]=Encrypt("id,diff,effect,p,owner,count,remove,reward\r\n");
    std::string difficulty="header\r\n";
    for(int i=0;i<64;++i){if(i)difficulty+=',';difficulty+='1';}
    e.files["maps/ZBS_Difficulty_Default.cso"]=Encrypt(difficulty+"\r\n");
    std::string levels="level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for(int i=0;i<=100;++i)levels+=std::to_string(i)+",100,0,1,1,1,1,1\r\n";
    e.files["resource/ZBSVER2/ZBS_neededexp.cso"]=Encrypt(levels);
    e.files["resource/ZBSVER2/ZBS_class.cso"]=Encrypt("id,hp,damage,speed,armor,ammo,order\r\n");
    e.files["resource/buff/BuffWeapon.cso"]=Encrypt("header\r\n");
    e.files["resource/FloatingDamage/FloatingDamageInfo.jsoo"]=Encrypt(R"({"FLOATING_DMG_STYLE":{"MODE":[15]}})");
    e.files["resource/AnnounceRibbon/announceribbon.cso"]=Encrypt("id,name,unused,unused,priority,category\r\n1,fixture,0,0,1,8\r\n");
    e.files["resource/AnnounceRibbon/AnnounceCategory.cso"]=Encrypt("header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n");
    e.files["maps/ZBS.json"]="{}";
    e.files["resource/item.csv"]=";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n"
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n";
    e.files["resource/defaultitems.csv"]="Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
    csol::scenario::NavigationFile nav;
    nav.areas={nav_loader_test::Area(1,0,0,0,4096)};
    e.files["maps/zs_deadend.nav"]=nav_loader_test::Encode(nav);
    // Authored inputs exercise real services and engine load boundaries; no
    // actual monster attack, map completion or asset fidelity is claimed.
}

void RetirementWorld(Engine& e)
{
    const auto originalMap=e.globals.mapname;
    struct RestoreMap { globalvars_t& globals; string_t map; ~RestoreMap(){globals.mapname=map;} } restoreMap{e.globals,originalMap};
    auto old=NewRetirementCase(e);
    CombatWorldResources(e);
    auto* world=e.Reserve(0,"worldspawn");worldspawn(&world->v);
    const auto firstEvent=e.events.size();
    int executes{};
    struct EdgeReset { Engine& engine; ~EdgeReset(){engine.onExecute={};} } edgeReset{e};
    e.onExecute=[&]
    {
        ++executes;
        CheckRetained(old);
    };
    const int spawned=e.api.pfnSpawn(world);
    e.onExecute={};
    Require(spawned==0&&!CSOL_RulesRejected()&&CSOL_UsesScenarioRules(),
        "real world DispatchSpawn installs a new scenario without inheriting old cancellation");
    Require(g_pGameRules&&g_pGameRules!=old.rules&&executes==1,
        "actual world Free Install Precache sequence publishes one new rules instance");
    const auto current=CurrentScenarioAccess(g_pGameRules);
    Check(current!=old.access&&current->Allowed()&&!old.access->Allowed(),
        "world installation binds the fresh post-BeginMap token and never reopens the old one");
    const auto begin=e.events.begin()+static_cast<std::ptrdiff_t>(firstEvent);
    Check(std::find(begin,e.events.end(),"lightstyle:63")!=e.events.end()&&
        std::find(begin,e.events.end(),"load:maps/zs_deadend.txt")!=e.events.end()&&
        std::find(begin,e.events.end(),"load:maps/zs_deadend.nav")!=e.events.end(),
        "real world Precache and Spawn continue through their final engine boundaries");
    CollectRetirement(old);
    Check(CSOL_ScenarioOwnerRules(current.get())==g_pGameRules&&current->Allowed(),
        "collecting old world ownership preserves the installed new rules and access");
    e.Clear();
    Check(!CSOL_ScenarioOwnerRules(current.get()),"full fixture teardown frees new-world private blocks before collection");
}

void Retirement(Engine& e)
{
    RetirementHooksMatrix(e);
    RetirementAliases(e);
    RetirementFreeTail(e,false);
    RetirementFreeTail(e,true);
    RetirementWorld(e);
    // Actual production shutdown helper; FileSystem_Shutdown and DLL unload
    // are outside this controlled-engine executable's acceptance boundary.
    auto final=NewRetirementCase(e);
    CSOL_ShutdownScenarioRules();
    Check(!g_pGameRules&&!final.access->Allowed()&&!CSOL_ScenarioOwnerRules(final.access.get()),
        "production terminal helper retires current rules without relying on another frame");
}
}
int main(int argc,char** argv)
{
    std::unique_ptr<Engine> e;
    try
    {
        Require(argc==2,"choose exactly one bounded SDK group");e=std::make_unique<Engine>();Hooks(true);
        if(std::strcmp(argv[1],"--rules")==0)Rules(*e);
        else if(std::strcmp(argv[1],"--callbacks")==0)Callbacks(*e);
        else if(std::strcmp(argv[1],"--world")==0)World(*e);
        else if(std::strcmp(argv[1],"--drops")==0)ScenarioDropCases(*e);
        else if(std::strcmp(argv[1],"--entity-access")==0)ScenarioEntityAccessCases(*e);
        else if(std::strcmp(argv[1],"--protection")==0)ScenarioProtectionCases(*e);
        else if(std::strcmp(argv[1],"--death-runtime")==0)ScenarioDeathRuntimeCases(*e);
        else if(std::strcmp(argv[1],"--death-services")==0)ScenarioDeathServicesCases(*e);
        else if(std::strcmp(argv[1],"--monster-services")==0)ScenarioMonsterServicesCases(*e);
        else if(std::strcmp(argv[1],"--wave-services")==0)ScenarioWaveServicesCases(*e);
        else if(std::strcmp(argv[1],"--damage-statistics")==0)ScenarioDamageStatisticsCases(*e);
        else if(std::strcmp(argv[1],"--damage-buffs")==0)ScenarioDamageBuffCases(*e);
        else if(std::strcmp(argv[1],"--damage-effects")==0)ScenarioDamageEffectCases(*e);
        else if(std::strcmp(argv[1],"--retirement")==0)Retirement(*e);
        else throw std::runtime_error("unknown SDK group");
        e->Clear();Hooks(false);
    }
    catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    if(e)for(std::size_t i=0;i<e->events.size();++i)std::cout<<"EDGE "<<i<<' '<<e->events[i]<<'\n';
    e.reset();std::cout<<"sdk-rules-session "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
