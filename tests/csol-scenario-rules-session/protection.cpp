// SPDX-License-Identifier: GPL-3.0-or-later
// Complete SDK components with controlled engine edges. Selection only hosts
// the real player authority; these cases do not install combat or a manager.
#include "sdk_engine.hpp"
#include "../../native/csol/server/scenario_player_runtime.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../../native/csol/server/scenario_breakable_guard.h"
#include "../../native/csol/server/scenario_breakable.h"
#include "../../native/csol/server/scenario_entity_lookup.h"
#include "../csol-scenario-difficulty/support.hpp"
#include <bit>
#include <limits>

extern "C" void worldspawn(entvars_t*);

namespace
{
using namespace sdk_test;

void Resources(Engine& e)
{
    using difficulty_test::Encrypt;
    e.files=session_test::Resources();
    e.files["maps/zbs_map_session_test.cso"]=Encrypt(
        "prefix0\r\nprefix1\r\nprefix2,100\r\nprefix3,1\r\nprefix4,1\r\nprefix5\r\n"
        "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
        "0,1,0,0,0,0,0,0,0,0,0,1\r\n1,0,0,0,0,0,0,0,0,0,0,1\r\n");
    std::string difficulty="header\r\n";
    for(int i=0;i<64;++i){if(i)difficulty+=',';difficulty+='1';}
    e.files["maps/ZBS_Difficulty_Default.cso"]=Encrypt(difficulty+"\r\n");
    std::string levels="level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for(int i=0;i<=100;++i)levels+=std::to_string(i)+",100,0,1,1,1,1,1\r\n";
    e.files["resource/ZBSVER2/ZBS_neededexp.cso"]=Encrypt(levels);
    e.files["resource/ZBSVER2/ZBS_class.cso"]=Encrypt("id,hp,damage,speed,armor,ammo,order\r\n");
    e.files["resource/buff/BuffWeapon.cso"]=Encrypt("header\r\n");
    e.files["maps/ZBS.json"]="{}";
    e.files["resource/item.csv"]=";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n"
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n";
    e.files["resource/defaultitems.csv"]="Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
}

struct Fixture
{
    Engine& engine;
    CHalfLifeMultiplay* rules{};
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    explicit Fixture(Engine& e,const std::function<void()>& beforeActivate={}):engine(e)
    {
        e.Clear();Resources(e);e.globals.time=10;e.Set("csol_scenario_difficulty","1");e.Prepare("2");
        auto* world=e.Reserve(0,"worldspawn");worldspawn(&world->v);
        Require(e.api.pfnSpawn(world)==0&&!CSOL_RulesRejected(),"protection fixture uses real Selection world Precache");
        auto* spawn=Engine::Create(Engine::String("playerspawn"));
        auto* point=dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(spawn));
        Require(point!=nullptr,"real player-spawn export constructs marker");point->m_iId=1;point->m_iTeam=2;
        Require(e.api.pfnSpawn(spawn)==0,"real player-spawn marker dispatches");
        auto* monster=Engine::Create(Engine::String("zombiespawn"));
        auto* monsterPoint=dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(monster));
        Require(monsterPoint!=nullptr,"real zombie-spawn export constructs marker");monsterPoint->m_iId=1;monsterPoint->m_iTeam=1;
        Require(e.api.pfnSpawn(monster)==0,"real zombie-spawn marker dispatches");
        for(int id:{1,2,30})
        {
            auto* ed=Engine::Create(Engine::String("zbstrigger"));
            auto* trigger=dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed));
            Require(trigger!=nullptr,"real zbstrigger export constructs marker");trigger->m_iId=id;ed->v.model=Engine::String("*1");
            Require(e.api.pfnSpawn(ed)==0,"real trigger marker dispatches");
        }
        if(beforeActivate)beforeActivate();
        e.api.pfnServerActivate(e.edicts.data(),e.nextEntity,e.globals.maxClients);
        Require(CSOL_ScenarioSelecting()&&!CSOL_RulesRejected(),"protection fixture remains Selection without combat services");
        CSOLScenarioSdkScope scope;access=scope.EntityAccess();players=CSOL_ScenarioPlayerAuthority(access.get());
        rules=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
        Require(scope.Current()&&players&&rules,"production access token resolves retained actual player authority");
        Check(!rules->IsGameStarted()&&rules->IsFreezePeriod(),"authority access does not unlock combat");
    }
    CBasePlayer* Player(int slot,TeamName team=CT)
    {
        auto* ed=engine.Reserve(slot,"player");auto* player=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags=FL_CLIENT;ed->v.health=100;ed->v.armorvalue=50;ed->v.deadflag=DEAD_NO;ed->v.takedamage=DAMAGE_AIM;
        player->m_iTeam=team;player->m_iJoiningState=JOINED;player->m_bJustConnected=false;player->m_iNumSpawns=1;
        Require(players->SetProfile(*player,{}),"real authority registers the verified default player profile");
        return player;
    }
    ~Fixture()
    {
        engine.onOffset={};engine.onIndexLookup={};engine.onFind={};engine.onOrigin={};engine.onModel={};
        engine.Clear();
    }
};

struct Packet
{
    int destination{},message{},recipient{};
    std::vector<std::uint8_t> bytes;
    bool ended{};
};
struct PacketCapture
{
    inline static PacketCapture* current{};
    enginefuncs_t saved{g_engfuncs};
    std::vector<Packet> packets;
    std::function<void()> onBegin;
    int status{};
    explicit PacketCapture(Engine& e):status(e.messages.at("MPToCL2"))
    {
        Require(!current,"single bounded status packet capture");current=this;
        g_engfuncs.pfnMessageBegin=[](int destination,int id,const float*,edict_t* recipient)
        {
            auto& c=*current;c.packets.push_back({destination,id,Engine::Index(recipient),{},false});
            const auto callback=c.onBegin;if(id==c.status&&callback)callback();
        };
        g_engfuncs.pfnWriteByte=[](int value){current->Write(value,1);};
        g_engfuncs.pfnWriteLong=[](int value){current->Write(value,4);};
        g_engfuncs.pfnMessageEnd=[]{Require(!current->packets.empty(),"packet end has a beginning");current->packets.back().ended=true;};
    }
    void Write(int value,int count)
    {
        Require(!packets.empty()&&!packets.back().ended,"message write belongs to one open packet");
        for(int i=0;i<count;++i)packets.back().bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(value)>>(i*8)));
    }
    const Packet& Last() const {Require(!packets.empty(),"one status packet captured");return packets.back();}
    ~PacketCapture(){g_engfuncs=saved;current=nullptr;}
};
void AppendLong(std::vector<std::uint8_t>& out,std::int32_t value)
{for(int i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(value)>>(i*8)));}
std::vector<std::uint8_t> AddPacket(std::int32_t id)
{
    std::vector<std::uint8_t> out{0x40,0};AppendLong(out,id);out.push_back(0);AppendLong(out,7);
    AppendLong(out,std::bit_cast<std::int32_t>(-1.0f));return out;
}
std::vector<std::uint8_t> RemovePacket(std::int32_t id)
{std::vector<std::uint8_t> out{0x40,1};AppendLong(out,id);return out;}

void GrantAndPublish(Engine& e)
{
    Fixture f(e);auto* alive=f.Player(1);auto* dead=f.Player(2);dead->pev->deadflag=DEAD_DEAD;
    auto* terrorist=f.Player(3,TERRORIST);auto* dormant=f.Player(4);dormant->pev->flags=std::bit_cast<std::int32_t>(0x80000000u);
    PacketCapture wire(e);
    Require(f.players->SetPlayerClearPointProtection(true),"actual player portion of completion switch grants protection");
    const auto state=f.players->Find(EHANDLE(alive));Require(state&&state->buffs.size()==1&&state->labels.size()==1,"one actual buff owns one label");
    const auto id=state->labels.front().id;const auto& buff=state->buffs.front();
    Check(buff.group==2&&buff.source==3&&buff.subsource==0&&buff.mask==8&&buff.expires==0&&buff.renderHeld&&
        buff.labels==std::vector<std::int32_t>{id},"clearpoint installs original group source mask permanent-expiry and label ownership");
    Check(state->labels.front().type==7&&state->labels.front().group==2&&!state->labels.front().style&&state->labelsDirty,
        "clearpoint creates a deferred type-seven grouped status label");
    Check(alive->pev->rendermode==kRenderNormal&&alive->pev->renderfx==kRenderFxGlowShell&&alive->pev->rendercolor==Vector(201,201,201),
        "actual grant sets the verified white protection shell");
    Check(f.players->ProtectionBlocksDamage(EHANDLE(alive),e.globals.time)&&f.players->ProtectionBlocksDamage(EHANDLE(dead),e.globals.time)&&
        !f.players->ProtectionBlocksDamage(EHANDLE(terrorist),e.globals.time)&&!f.players->ProtectionBlocksDamage(EHANDLE(dormant),e.globals.time),
        "grant includes dead CT and excludes T and exact dormant flags without guessing alive-only eligibility");
    Check(f.players->SetPlayerClearPointProtection(true)&&state->buffs.size()==1&&state->labels.front().id==id&&wire.packets.empty(),
        "same switch value neither duplicates buffs nor publishes labels immediately");
    // Stop the unrelated native PreThink tail at its genuine game-over gate;
    // this still executes actual dispatch, ReAPI, buff tick and PlayerThink.
    f.rules->m_bGameOver=true;e.api.pfnPlayerPreThink(alive->edict());
    Check(wire.Last().message==wire.status&&wire.Last().destination==MSG_ONE&&wire.Last().recipient==1&&
        wire.Last().ended&&wire.Last().bytes==AddPacket(id)&&state->publishedLabels==std::vector<std::int32_t>{id},
        "actual PlayerPreThink publishes the original MPToCL2 status packet and permanent float sentinel");
    const auto sent=wire.packets.size();e.api.pfnPlayerPreThink(alive->edict());
    Check(wire.packets.size()==sent,"unchanged frame does not republish an already sent label");
    alive->pev->renderfx=kRenderFxPulseSlow;alive->pev->rendercolor=Vector(10,20,30);
    Require(f.players->SetPlayerClearPointProtection(false),"actual completion switch removes source three");
    Check(state->buffs.empty()&&state->labels.empty()&&!CSOL_ScenarioProtectionBlocks(*alive)&&
        alive->pev->renderfx==kRenderFxPulseSlow&&alive->pev->rendercolor==Vector(10,20,30),
        "protection removal preserves a newer unrelated render effect");
    e.api.pfnPlayerPreThink(alive->edict());
    Check(wire.Last().ended&&wire.Last().bytes==RemovePacket(id)&&state->publishedLabels.empty(),
        "next actual player frame removes precisely the previously published owned label");
    dormant->pev->flags=FL_DORMANT|FL_CLIENT;
    Require(f.players->SetPlayerClearPointProtection(true),"fresh enable revisits current CT list");
    Check(f.players->ProtectionBlocksDamage(EHANDLE(dormant),e.globals.time),"combined dormant flags differ from exact original exclusion word");
    const auto dormantState=f.players->Find(EHANDLE(dormant));const auto beforeDormant=wire.packets.size();
    Require(f.players->UpdatePlayerBuffs(*dormant,e.globals.time),"dormant player's state tick remains valid");
    Check(wire.packets.size()==beforeDormant&&dormantState->labelsDirty,"dormant recipient defers delivery without losing pending work");
    dormant->pev->flags=FL_CLIENT;Require(f.players->UpdatePlayerBuffs(*dormant,e.globals.time),"resumed recipient publishes pending label");
    Check(wire.packets.size()==beforeDormant+1&&!dormantState->labelsDirty,"delivery resumes when recipient is no longer dormant");
}

enum class DamageAction { CountOnly, RemoveVictim, RetireRules, GrantThenNext };
struct DamageContext
{
    DamageAction action{};int before{},after{};
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    CGameRules* rules{};
};
DamageContext* damageContext{};
BOOL DamageHook(IReGameHook_CBasePlayer_TakeDamage* chain,CBasePlayer* player,
    entvars_t* inflictor,entvars_t* attacker,float& damage,int type)
{
    auto& state=*damageContext;++state.before;
    if(state.action==DamageAction::CountOnly){++state.after;return FALSE;}
    if(state.action==DamageAction::RemoveVictim)Engine::Remove(player->edict());
    if(state.action==DamageAction::RetireRules)FreeGameRules(&g_pGameRules);
    if(state.action==DamageAction::GrantThenNext)Require(state.players->SetPlayerClearPointProtection(true),"pre hook grants through actual player authority");
    const auto result=chain->callNext(player,inflictor,attacker,damage,type);++state.after;
    if(state.action==DamageAction::RetireRules)
    {
        CSOL_CollectRetiredScenarioRules();
        Check(!state.access->Allowed()&&CSOL_ScenarioOwnerRules(state.access.get())==state.rules,
            "damage post hook retains actual retired rules until complete SDK wrapper exits");
    }
    return result;
}
struct DamageHooks
{
    explicit DamageHooks(DamageContext& context)
    {Require(!damageContext,"damage hook cases are serial");damageContext=&context;g_ReGameHookchains.m_CBasePlayer_TakeDamage.registerHook(DamageHook,192);}
    ~DamageHooks(){g_ReGameHookchains.m_CBasePlayer_TakeDamage.unregisterHook(DamageHook);damageContext=nullptr;}
};
void ActualDamageEntries(Engine& e)
{
    {
        Fixture f(e);auto* player=f.Player(1);auto* attacker=f.Player(2,TERRORIST);
        auto* grenade=e.Reserve(e.nextEntity++,"grenade");
        DamageContext context{DamageAction::CountOnly,0,0,f.players,f.access,f.rules};DamageHooks hooks(context);
        Require(f.players->SetPlayerClearPointProtection(true),"damage matrix grants through actual production service");
        for(int type:{DMG_GENERIC,DMG_BULLET,DMG_EXPLOSION,DMG_BLAST,DMG_FALL})
        {
            const float health=player->pev->health,armor=player->pev->armorvalue;float amount=25;
            Check(!player->TakeDamage(&grenade->v,attacker->pev,amount,type)&&context.before==0,
                "actual damage wrapper blocks protected generic bullet HE blast and fall before ReAPI pre hooks");
            Check(!player->TakeDamage_OrigFunc(&grenade->v,attacker->pev,amount,type)&&
                player->pev->health==health&&player->pev->armorvalue==armor&&amount==25,
                "direct original damage entry also rejects protection before armor health or amount changes");
        }
        Check(!f.rules->FPlayerCanTakeDamage(player,attacker)&&!f.rules->FPlayerCanTakeDamage_OrigFunc(player,nullptr),
            "real rules hook and direct original permission query reject protected victim");
        Require(f.players->SetPlayerClearPointProtection(false),"damage matrix removes protection");
        Check(f.rules->FPlayerCanTakeDamage(player,attacker),"unprotected enemy passes ordinary rule permission");
        player->TakeDamage(&grenade->v,attacker->pev,25,DMG_EXPLOSION);
        Check(context.before==1&&context.after==1,"unprotected wrapper reaches real ReAPI chain; test hook intentionally stops native damage");
        context.action=DamageAction::GrantThenNext;
        Check(!player->TakeDamage(&grenade->v,attacker->pev,25,DMG_EXPLOSION)&&context.before==2&&context.after==2&&player->pev->health==100,
            "protection granted during ReAPI pre is rechecked by real original damage entry");
    }
    for(auto action:{DamageAction::RemoveVictim,DamageAction::RetireRules})
    {
        Fixture f(e);auto* player=f.Player(1);auto* ed=player->edict();const EHANDLE original(player);
        DamageContext context{action,0,0,f.players,f.access,f.rules};DamageHooks hooks(context);
        Check(!player->TakeDamage(&e.edicts[0].v,&e.edicts[0].v,10,DMG_GENERIC)&&context.before==1&&context.after==1,
            "real damage original rejects deleted victim or retired map after ReAPI pre callback");
        if(action==DamageAction::RemoveVictim)Check(ed->free&&!original.Get(),"damage pre hook releases actual private block and serial identity");
        else
        {
            CSOL_CollectRetiredScenarioRules();
            Check(!CSOL_ScenarioOwnerRules(f.access.get()),"damage wrapper exit permits later explicit collection of retired rules");
        }
    }
}

void CallbackAndLifecycle(Engine& e)
{
    {
        Fixture f(e);auto* player=f.Player(1);PacketCapture wire(e);
        Require(f.players->SetPlayerClearPointProtection(true),"publication cancellation begins with actual protection");
        const auto state=f.players->Find(EHANDLE(player));const auto id=state->labels.front().id;
        wire.onBegin=[&]{wire.onBegin={};Require(f.players->SetPlayerClearPointProtection(false),"message callback disables source protection");};
        Require(f.players->UpdatePlayerBuffs(*player,e.globals.time),"message replacement cancels stale desired tail without invalidating player");
        Check(wire.Last().ended&&wire.Last().bytes==AddPacket(id)&&state->labelsDirty&&state->buffs.empty(),
            "already begun packet completes from values while revision change retains reconciliation work");
        Require(f.players->UpdatePlayerBuffs(*player,e.globals.time),"following frame reconciles actual sent prefix");
        Check(wire.Last().bytes==RemovePacket(id)&&state->publishedLabels.empty()&&!state->labelsDirty,
            "reconciliation removes the label actually sent during reentrant disable");
        Require(f.players->SetPlayerClearPointProtection(true),"fresh grant prepares OFFSET cancellation");
        e.onOffset=[&](const edict_t* ed){if(ed==player->edict()){e.onOffset={};Require(f.players->SetPlayerClearPointProtection(false),"OFFSET disables protection");}};
        const auto before=wire.packets.size();Require(f.players->UpdatePlayerBuffs(*player,e.globals.time),"OFFSET revision change safely defers reconciliation");
        Check(wire.packets.size()==before&&state->labelsDirty,"OFFSET change prevents any stale label packet");
        e.onOffset={};
    }
    {
        Fixture f(e);auto* player=f.Player(1);auto* ed=player->edict();const EHANDLE handle(player);PacketCapture wire(e);
        Require(f.players->SetPlayerClearPointProtection(true),"PreThink cancellation begins with actual protection");
        wire.onBegin=[&]{wire.onBegin={};Engine::Remove(ed);};
        e.api.pfnPlayerPreThink(ed);
        Check(ed->free&&!handle.Get()&&wire.Last().ended,"actual PlayerPreThink tick completes begun packet then exits after player deletion");
    }
    {
        Fixture f(e);auto* player=f.Player(1);auto* ed=player->edict();const EHANDLE old(player);
        Require(f.players->SetPlayerClearPointProtection(true),"disconnect fixture receives original source buff");
        const auto retired=f.players->Find(old);const auto generation=retired->generation;
        player->has_disconnected=true;CSOL_ScenarioPlayerDisconnected(*player);
        Check(!retired->generation&&retired->buffs.empty()&&ed->v.renderfx==kRenderFxNone,
            "production disconnect route removes owned protection even after has_disconnected is marked");
        Engine::Remove(ed);auto* replacement=f.Player(1);const auto next=f.players->Find(EHANDLE(replacement));
        Check(next->generation!=generation&&!old.Get()&&!CSOL_ScenarioProtectionBlocks(*replacement),
            "recycled player slot does not inherit disconnected player's protection or generation");
        Require(f.players->SetPlayerClearPointProtection(true),"repeated true remains accepted after player replacement");
        Check(next->buffs.empty(),"same global switch value does not retroactively grant a new slot occupant");
        Require(f.players->SetPlayerClearPointProtection(false)&&f.players->SetPlayerClearPointProtection(true),"new switch transition grants replacement");
        Check(CSOL_ScenarioProtectionBlocks(*replacement),"new CT identity gets only a new explicit grant");
        f.rules->RestartRound();
        Check(CSOL_ScenarioSelecting()&&CSOL_ScenarioProtectionBlocks(*replacement),"Selection restart cannot masquerade as a real combat restart or clear its state");
        e.api.pfnServerDeactivate();
        Check(!f.access->Allowed()&&replacement->pev->renderfx==kRenderFxNone&&next->buffs.empty()&&
            !CSOL_ScenarioPlayerAuthority(f.access.get()),"actual shutdown releases owned shell before access revocation and closes service lookup");
        e.api.pfnServerDeactivate();Check(replacement->pev->renderfx==kRenderFxNone,"repeated shutdown is harmless");
    }
}

void NativeRulesUnchanged(Engine& e)
{
    for(const char* mode:{"0","1"})
    {
        e.Clear();e.Prepare(mode);g_pGameRules=InstallGameRules();
        auto* rules=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);Require(rules&&!CSOL_UsesScenarioRules(),"ordinary mode installs actual native rules");
        auto make=[&](int slot,TeamName team)
        {auto* ed=e.Reserve(slot,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));p->m_iTeam=team;ed->v.flags=FL_CLIENT;return p;};
        auto* player=make(1,CT);auto* ally=make(2,CT);auto* enemy=make(3,TERRORIST);
        const float savedFriendlyFire=friendlyfire.value;friendlyfire.value=0;
        Check(!CSOL_ScenarioProtectionBlocks(*player)&&CSOL_UpdateScenarioPlayerBuffs(*player),"scenario protection helpers are inactive in ordinary modes");
        Check(!rules->FPlayerCanTakeDamage(player,ally)&&rules->FPlayerCanTakeDamage(player,enemy)&&
            rules->FPlayerCanTakeDamage(player,player)&&rules->FPlayerCanTakeDamage(player,nullptr),
            "ordinary friendlyfire enemy self and world permissions retain native semantics");
        friendlyfire.value=1;Check(rules->FPlayerCanTakeDamage(player,ally),"ordinary enabled friendlyfire remains allowed");friendlyfire.value=savedFriendlyFire;
        DamageContext context;DamageHooks hooks(context);
        player->TakeDamage(&e.edicts[0].v,enemy->pev,10,DMG_GENERIC);
        Check(context.before==1&&context.after==1,"ordinary damage wrapper still reaches actual ReAPI chain");
    }
    e.Clear();
}

void TargetKey(CCSOLScenarioTarget& target,const char* key,const char* value)
{
    KeyValueData data{const_cast<char*>("zbstarget"),const_cast<char*>(key),const_cast<char*>(value),FALSE};
    target.KeyValue(&data);Require(data.fHandled,"real target handles verified resource key");
}
CCSOLScenarioTarget* Target(Engine& e,int flags=0,const char* human="0",const char* monster="1")
{
    auto* ed=Engine::Create(Engine::String("zbstarget"));
    auto* target=dynamic_cast<CCSOLScenarioTarget*>(CBaseEntity::Instance(ed));
    Require(target!=nullptr,"actual zbstarget export constructs its own derived class");
    ed->v.model=Engine::String("*1");ed->v.health=31;ed->v.max_health=120;ed->v.spawnflags=flags;
    TargetKey(*target,"hmdmgratio",human);TargetKey(*target,"zbdmgratio",monster);
    Require(e.api.pfnSpawn(ed)==0&&target->ValidConfiguration(),"real DispatchSpawn accepts valid target");
    return target;
}

void TargetStateAndDamage(Engine& e)
{
    for(int flags:{0,SF_BREAK_TRIGGER_ONLY})
    {
        CCSOLScenarioTarget* target{};Fixture f(e,[&]{target=Target(e,flags);});
        Require(target,"collected target remains present");
        Check(target->Active()==!flags&&target->pev->health==120&&target->pev->team==2&&
            target->pev->nextthink==e.globals.time,"target Spawn uses active byte max_health monster-only team and current think time");
        Check(target->ObjectCaps()&FCAP_MUST_RESET,"target participates in real restart collection");
        for(int active:{0,1})
        {
            if(target->Active()!=bool(active))target->Use(nullptr,nullptr,USE_TOGGLE,0);
            const auto health=target->pev->health;
            target->SetClearPointProtection(true);
            Check(target->Active()==bool(active)&&target->pev->takedamage==DAMAGE_NO,
                "target protection changes permission without changing active");
            Check(!target->TakeDamage(&e.edicts[0].v,&e.edicts[0].v,10,DMG_CLUB)&&target->pev->health==health,
                "protected target rejects direct virtual melee damage even when active");
            target->SetClearPointProtection(false);
            Check(target->Active()==bool(active)&&target->pev->takedamage==((flags&&!active)?DAMAGE_NO:DAMAGE_YES),
                "protection removal follows the original flag by active matrix");
            if(!active)
                Check(!target->TakeDamage(&e.edicts[0].v,&e.edicts[0].v,10,DMG_BULLET)&&target->pev->health==health,
                    "inactive target rejects damage even when permission was restored to yes");
            else
                Check(target->TakeDamage(&e.edicts[0].v,&e.edicts[0].v,1,DMG_BULLET)&&target->pev->health<health,
                    "collected target reaches actual bound wall rules and native nonlethal damage");
        }
        for(USE_TYPE type:{USE_OFF,USE_ON,USE_SET,USE_TOGGLE})
        {
            const bool previous=target->Active();const auto health=target->pev->health;
            target->SetClearPointProtection(true);target->Use(nullptr,nullptr,type,0);
            Check(target->Active()!=previous&&target->pev->takedamage==(previous?DAMAGE_NO:DAMAGE_YES)&&target->pev->health==health,
                "every USE type toggles active once without breaking target; local Use can override protection");
        }
        target->pev->health=4;target->pev->max_health=333;target->pev->team=7;target->pev->nextthink=44;
        target->Restart();
        Check(target->Active()==!flags&&target->pev->health==333&&target->pev->team==7&&target->pev->nextthink==44,
            "real target Restart resets active and current max health without repeating Spawn team or timer writes");
        target->SetClearPointProtection(true);const auto currentHealth=target->pev->health;
        e.api.pfnServerDeactivate();target->Use(nullptr,nullptr,USE_TOGGLE,0);target->Restart();
        Check(!target->TakeDamage(&e.edicts[0].v,&e.edicts[0].v,10,DMG_BULLET)&&target->pev->health==currentHealth,
            "bound target entrypoints reject retired rules without touching previous map state");
    }
    {
        std::array<CCSOLScenarioTarget*,3> targets{};
        Fixture f(e,[&]{targets[0]=Target(e,0,"1","0");targets[1]=Target(e,0,"1","1");targets[2]=Target(e,0,"0","0");});
        Check(targets[0]->pev->team==1&&targets[1]->pev->team==0&&targets[2]->pev->team==0,
            "Spawn assigns only the verified one-sided ratio team values");
        TargetKey(*targets[0],"iconSuffix","257 1 2 3");
        Check(targets[0]->ValidConfiguration(),"icon prefix and low-byte value are accepted");
        TargetKey(*targets[1],"icon","2 1 nan 3");
        Check(!targets[1]->ValidConfiguration(),"malformed target coordinates fail configuration instead of inventing positions");
    }
}

void TargetCollectionCancellation(Engine& e)
{
    {
        Fixture f(e);Target(e);Target(e,SF_BREAK_TRIGGER_ONLY);
        auto* ed=Engine::Create(Engine::String("zbsbreak"));ed->v.model=Engine::String("*1");ed->v.health=20;
        Require(e.api.pfnSpawn(ed)==0,"collection fixture dispatches actual ordinary scenario wall");
        CCSOLScenarioWalls walls;walls.BindEntityAccess(f.access);csol::scenario::ConfigError error;
        Check(walls.Collect(error)&&walls.Count()==3,"real collection includes zbsbreak and both active and inactive zbstargets");
    }
    for(bool offset:{false,true})
    {
        CCSOLScenarioTarget* first{};CCSOLScenarioTarget* later{};
        Fixture f(e,[&]{first=Target(e);later=Target(e);});
        auto* ed=first->edict();const EHANDLE old(first),remaining(later);
        CCSOLScenarioWalls walls;walls.BindEntityAccess(f.access);csol::scenario::ConfigError error;
        if(offset)e.onOffset=[&](const edict_t* at){if(at==ed){e.onOffset={};Engine::Remove(ed);}};
        else e.onFind=[&](edict_t* found,const char*,const char* name)
        {if(found==ed&&std::strcmp(name,"zbstarget")==0){e.onFind={};Engine::Remove(ed);}};
        Check(!walls.Collect(error)&&walls.Count()==0&&!old.Get()&&remaining.Get<CBaseEntity>()==later,
            "collection must reject an interrupted target lookup instead of committing a truncated target list");
        e.onOffset={};e.onFind={};
    }
}

void ClearPointLookupCancellation(Engine& e)
{
    for(bool offset:{false,true})
    {
        Fixture f(e);
        const auto missing=CSOL_LookupScenarioEntity(nullptr,"trigger_clearpoint",f.access);
        Check(missing.valid&&!missing.entity,"real lookup distinguishes an exhausted world sentinel");
        auto* ed=Engine::Create(Engine::String("trigger_clearpoint"));ed->v.model=Engine::String("*1");
        Require(e.api.pfnSpawn(ed)==0,"actual clearpoint export spawns before lookup interruption");
        auto* point=dynamic_cast<CCSOLScenarioClearPoint*>(CBaseEntity::Instance(ed));
        Require(point!=nullptr,"actual clearpoint retains its distinct class");
        const auto revision=point->ControlRevision();point->Spawn();
        Check(point->ControlRevision()!=revision,"explicit clearpoint Spawn invalidates captured control revision");
        if(offset)e.onOffset=[&](const edict_t* at){if(at==ed){e.onOffset={};Engine::Remove(ed);}};
        else e.onFind=[&](edict_t* found,const char*,const char* name)
        {if(found==ed&&std::strcmp(name,"trigger_clearpoint")==0){e.onFind={};Engine::Remove(ed);}};
        const auto interrupted=CSOL_LookupScenarioEntity(nullptr,"trigger_clearpoint",f.access);
        Check(!interrupted.valid&&!interrupted.entity&&ed->free,
            "callback-deleted clearpoint reports cancellation and cannot masquerade as no-trigger victory branch");
        e.onFind={};e.onOffset={};
    }
}

// This is the real SDK CSave/CRestore wire buffer, not a replacement serializer.
// Function fields are deliberately absent in normal round trips; dedicated
// cancellation cases below exercise the engine function-name boundary.
struct TargetSaveBuffer
{
    std::array<char,65536> bytes{};
    std::array<char*,2048> tokens{};
    std::array<ENTITYTABLE,Engine::Capacity> entities{};
    SAVERESTOREDATA data{};
    int used{};
    explicit TargetSaveBuffer(Engine& e)
    {
        data.pBaseData=bytes.data();data.pCurrentData=bytes.data();data.bufferSize=static_cast<int>(bytes.size());
        data.tokenCount=static_cast<int>(tokens.size());data.pTokens=tokens.data();
        data.tableCount=e.nextEntity;data.pTable=entities.data();data.time=e.globals.time;
        for(int i=0;i<e.nextEntity;++i){entities[i].id=i;entities[i].pent=&e.edicts[i];}
    }
    int Save(CCSOLScenarioTarget& target)
    {CSave save(&data);const auto result=target.Save(save);used=data.size;return result;}
    int Restore(CCSOLScenarioTarget& target)
    {
        data.pCurrentData=bytes.data();data.size=0;data.bufferSize=used;
        CRestore restore(&data);return target.Restore(restore);
    }
    std::vector<std::pair<std::string,int>> Fields() const
    {
        std::vector<std::pair<std::string,int>> result;
        for(int at=0;at<used;)
        {
            Require(used-at>=4,"saved SDK record has full size and token header");
            std::uint16_t size{},token{};std::memcpy(&size,bytes.data()+at,2);std::memcpy(&token,bytes.data()+at+2,2);at+=4;
            Require(token<tokens.size()&&tokens[token]&&size<=used-at,"saved SDK record references valid token and payload");
            result.emplace_back(tokens[token],size);at+=size;
        }
        return result;
    }
};

enum class SerializationEdge { AllocateString, PrecacheModel, SetModel, NameForFunction, FunctionFromName };
struct SerializationEdges
{
    inline static SerializationEdges* current{};
    enginefuncs_t saved{g_engfuncs};
    SerializationEdge edge;
    std::function<void()> callback;
    int fired{};
    SerializationEdges(SerializationEdge selected,std::function<void()> fn):edge(selected),callback(std::move(fn))
    {
        Require(!current,"one bounded serializer engine callback fixture");current=this;
        g_engfuncs.pfnAllocString=[](const char* text){auto& c=*current;const auto value=c.saved.pfnAllocString(text);c.Fire(SerializationEdge::AllocateString);return value;};
        g_engfuncs.pfnPrecacheModel=[](const char* name){auto& c=*current;const auto value=c.saved.pfnPrecacheModel(name);c.Fire(SerializationEdge::PrecacheModel);return value;};
        g_engfuncs.pfnSetModel=[](edict_t* ed,const char* name){auto& c=*current;c.saved.pfnSetModel(ed,name);c.Fire(SerializationEdge::SetModel);};
        g_engfuncs.pfnNameForFunction=[](uint32){auto& c=*current;c.Fire(SerializationEdge::NameForFunction);return "prepared_target_function";};
        g_engfuncs.pfnFunctionFromName=[](const char*) -> uint32
        {auto& c=*current;c.Fire(SerializationEdge::FunctionFromName);return 0;};
    }
    void Fire(SerializationEdge at)
    {if(at!=edge||!callback)return;const auto fn=std::move(callback);callback={};++fired;fn();}
    ~SerializationEdges(){g_engfuncs=saved;current=nullptr;}
};

void TargetSerialization(Engine& e)
{
    for(int active:{0,1})
    {
        CCSOLScenarioTarget* target{};Fixture f(e,[&]{target=Target(e);});
        if(target->Active()!=bool(active))target->Use(nullptr,nullptr,USE_TOGGLE,0);
        target->SetTouch(nullptr);target->pev->health=97;TargetSaveBuffer saved(e);
        Require(saved.Save(*target)!=0,"real target Save writes the complete inherited SDK field chain");
        const auto fields=saved.Fields();
        const auto activeField=std::find_if(fields.begin(),fields.end(),[](const auto& field){return field.first=="m_active";});
        Check(active?activeField!=fields.end()&&activeField->second==1:activeField==fields.end(),
            "SDK serializes enabled active as one byte and omits its zero value");
        const auto base=std::find_if(fields.begin(),fields.end(),[](const auto& field){return field.first=="CBreakable";});
        const auto wall=std::find_if(fields.begin(),fields.end(),[](const auto& field){return field.first=="CCSOLScenarioBreakable";});
        const auto own=std::find_if(fields.begin(),fields.end(),[](const auto& field){return field.first=="CCSOLScenarioTarget";});
        Check(base!=fields.end()&&wall!=fields.end()&&own!=fields.end()&&base<wall&&wall<own,
            "actual serialized class sections retain base then ratios then target order");
        target->Use(nullptr,nullptr,USE_TOGGLE,0);target->pev->health=3;TargetKey(*target,"zbdmgratio","0");
        Require(saved.Restore(*target)!=0,"real target Restore reads all inherited fields");
        Check(target->Active()==bool(active)&&target->pev->health==97&&target->Classify()==16,
            "actual Restore recovers active health and saved ratio instead of inferred spawn state");
        Engine::CheckGuards(target->edict());
    }
    for(auto edge:{SerializationEdge::AllocateString,SerializationEdge::PrecacheModel,SerializationEdge::SetModel})
        for(int action:{0,1,2})
        {
            CCSOLScenarioTarget* target{};Fixture f(e,[&]{target=Target(e);});
            target->SetTouch(nullptr);TargetSaveBuffer saved(e);Require(saved.Save(*target)!=0,"save source before destructive restore edge");
            auto* ed=target->edict();const EHANDLE old(target);
            SerializationEdges callback(edge,[&]
            {
                if(action==0)Engine::Remove(ed);
                if(action==1)
                {
                    ed->v.spawnflags=SF_BREAK_TRIGGER_ONLY;ed->v.max_health=771;target->Spawn();
                    ed->v.mins=Vector(-7,-8,-9);ed->v.maxs=Vector(7,8,9);
                }
                if(action==2)FreeGameRules(&g_pGameRules);
            });
            Check(!saved.Restore(*target)&&callback.fired==1,"actual Restore cancels after deletion re-Spawn or rules retirement at each engine edge");
            if(action==0)Check(ed->free&&!old.Get(),"serializer deletion releases real SDK private storage and identity");
            if(action==1)Check(old.Get<CBaseEntity>()==target&&!target->Active()&&ed->v.health==771&&
                ed->v.mins==Vector(-7,-8,-9)&&ed->v.maxs==Vector(7,8,9),"cancelled outer Restore cannot overwrite freshly spawned target state or bounds");
            if(action==2)Check(!f.access->Allowed()&&!g_pGameRules,"serializer retirement revokes the real map token");
        }
    {
        CCSOLScenarioTarget* target{};Fixture f(e,[&]{target=Target(e,0,"0","0");});
        auto* ed=target->edict();TargetSaveBuffer saved(e);
        SerializationEdges callback(SerializationEdge::NameForFunction,[&]{Engine::Remove(ed);});
        Check(!saved.Save(*target)&&callback.fired==1&&ed->free,
            "actual Save stops after function-name callback deletes its source object");
    }
    {
        CCSOLScenarioTarget* target{};Fixture f(e,[&]{target=Target(e,0,"0","0");});
        auto* ed=target->edict();TargetSaveBuffer saved(e);
        SerializationEdges callback(SerializationEdge::FunctionFromName,[&]{Engine::Remove(ed);});
        Require(saved.Save(*target)!=0,"real Save retains callback symbol before reverse lookup cancellation");
        Check(!saved.Restore(*target)&&callback.fired==1&&ed->free,
            "real Restore cannot write resolved function pointer into a deleted private object");
    }
}

struct ProgressEvents final : csol::scenario::TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    int changed{},goals{};
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {++changed;}
    void GoalReached() override {++goals;}
};
void MapDeathProgress(Engine& e)
{
    Fixture f(e);CCSOLScenarioMap map;ProgressEvents events;bool reached=true;
    Check(!map.AddMonsterClearPoint(1,reached)&&!reached&&!map.ConsumeMonsterClearPoint(0),"empty real Map rejects progress and consume");
    // The Selection owner's collection has consumed original spawn markers.
    // Construct fresh actual markers for this independent production Map.
    for(const char* classname:{"playerspawn","zombiespawn"})
    {
        auto* ed=Engine::Create(Engine::String(classname));
        if(auto* point=dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))){point->m_iId=1;point->m_iTeam=2;}
        if(auto* point=dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))){point->m_iId=1;point->m_iTeam=1;}
        Require(e.api.pfnSpawn(ed)==0,"progress fixture dispatches real spawn marker");
    }
    for(int id:{1,2,30})
    {
        auto* ed=Engine::Create(Engine::String("zbstrigger"));
        auto* trigger=dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed));
        Require(trigger!=nullptr,"progress fixture constructs actual trigger marker");
        trigger->m_iId=id;ed->v.model=Engine::String("*1");
        Require(e.api.pfnSpawn(ed)==0,"progress fixture dispatches real trigger marker");
    }
    Require(map.BeginMap("map_session_test")&&map.CollectEntities()&&map.BeginRound(0,e.globals.time,0,events),
        "independent production Map loads actual parser resources and starts its own round without changing GameRules Ready");
    const auto round=map.RoundGeneration();
    Check(map.ClearPoint()==-1&&map.AddMonsterClearPoint(100,reached)&&!reached&&map.ClearPoint()==99,
        "death progress adds to disabled minus-one rather than implicitly enabling it");
    Check(map.AddMonsterClearPoint(1,reached)&&reached&&map.ClearPoint()==100&&map.AddMonsterClearPoint(9,reached)&&reached&&map.ClearPoint()==100,
        "death progress reaches threshold and clamps only the upper end");
    Check(!map.ConsumeMonsterClearPoint(round+1)&&map.ClearPoint()==100,
        "old or unrelated round identity cannot consume current progress");
    Check(map.ConsumeMonsterClearPoint(round)&&map.ClearPoint()==-1&&map.AddMonsterClearPoint(-2,reached)&&!reached&&map.ClearPoint()==-3,
        "trigger consume resets to minus-one and negative additions are not lower-clamped");
    const auto previous=map.ClearPoint();
    Check(!map.AddMonsterClearPoint(std::numeric_limits<float>::quiet_NaN(),reached)&&!reached&&map.ClearPoint()==previous,
        "nonfinite type value cannot mutate production progress");
    Require(map.BeginRound(0,e.globals.time,0,events),"real second round changes generation");
    Check(!map.ConsumeMonsterClearPoint(round)&&map.ClearPoint()==-1,
        "stale trigger completion cannot consume newly restarted round");
    Require(map.EnableMonsterClearPoint(1),"real ordinary-spawn progress enable prepares the enabled state");
    Check(map.ClearPoint()==0&&map.AddMonsterClearPoint(100,reached)&&reached&&map.ClearPoint()==100,
        "explicitly enabled zero progress plus one hundred reaches completion");
    map.RevokeEntityAccess();Check(!map.AddMonsterClearPoint(100,reached)&&!map.ConsumeMonsterClearPoint(map.RoundGeneration()),
        "revoked map refuses both progress operations");
    Check(CSOL_ScenarioSelecting()&&!f.rules->IsGameStarted()&&events.goals==0,
        "isolated Map tests neither fabricate death provider nor unlock the real gameplay gate");
    Require(map.EndMap(),"independent production Map releases its snapshots");
}
}

void ScenarioProtectionCases(sdk_test::Engine& engine)
{
    GrantAndPublish(engine);
    ActualDamageEntries(engine);
    CallbackAndLifecycle(engine);
    NativeRulesUnchanged(engine);
    TargetStateAndDamage(engine);
    TargetCollectionCancellation(engine);
    ClearPointLookupCancellation(engine);
    TargetSerialization(engine);
    MapDeathProgress(engine);
}
