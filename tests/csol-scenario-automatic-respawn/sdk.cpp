// SPDX-License-Identifier: GPL-3.0-or-later
// Execute unchanged production rule methods with explicit SDK callback seams.
// This is not a full GameDLL, map or graphical-client execution.
#include "../../native/csol/scenario/core/automatic_respawn.hpp"
#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace csol::scenario { struct ConfigError { std::string message; }; }
constexpr int FALSE=0, TRUE=1, CT=2, TERRORIST=1, SPECTATOR=3, UNASSIGNED=0;
constexpr int DEAD_NO=0, DEAD_DYING=1, DEAD_DEAD=2, DEAD_RESPAWNABLE=3;
constexpr int JOINED=0, GETINTOGAME=5, PICKINGTEAM=4, Menu_ChooseAppearance=3;
constexpr int FL_DORMANT=1, FL_KILLME=2, FL_SPECTATOR=4, HUD_PRINTCENTER=4;
unsigned checks{}, failures{}, rejects{}, notices{}, spawns{}, counts{}, prepares{};
std::function<void()> onUserId, onIndex, onClass, onCount, onDifficulty, onSend, onPrepare, onPermit, onSpawn;
void Check(bool value, const char* reason)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %u: %s\n", checks, reason); }
}
struct CBaseEntity;
struct CBasePlayer;
struct CCSOLScenarioRules;
CCSOLScenarioRules* g_pGameRules{};
struct Vars { int flags{}, deadflag=DEAD_RESPAWNABLE, iuser1=1; float health{}; };
struct edict_t { CBaseEntity* entity{}; int serialnumber=1, userid=1; bool free{}; };
struct CBaseEntity
{
    Vars vars;
    Vars* pev=&vars;
    edict_t entity{this};
    virtual ~CBaseEntity()=default;
    edict_t* edict(){return &entity;}
};
struct EHANDLE
{
    edict_t* value{};
    int serial{};
    EHANDLE()=default;
    EHANDLE(CBaseEntity* entity){*this=entity;}
    EHANDLE& operator=(CBaseEntity* entity)
    {value=entity?entity->edict():nullptr;serial=value?value->serialnumber:0;return *this;}
    edict_t* Get()const{return value&&!value->free&&value->serialnumber==serial?value:nullptr;}
    template<class T>T* Get()const{return Get()?dynamic_cast<T*>(Get()->entity):nullptr;}
};
struct CBasePlayer: CBaseEntity
{
    int index=1, m_iNumSpawns{}, m_iTeam=CT, m_iJoiningState=JOINED, m_iMenu{};
    bool m_bJustConnected{}, has_disconnected{}, selected=true;
    float m_fDeadTime{};
    int entindex(){if(onIndex)onIndex();return index;}
    void RoundRespawn();
};
int UserId(edict_t* edict){if(onUserId)onUserId();return edict->userid;}
struct Engine {int(*pfnGetPlayerUserId)(edict_t*)=UserId;}g_engfuncs;
#define GETPLAYERUSERID(edict) g_engfuncs.pfnGetPlayerUserId(edict)
struct Globals {float time=600;int maxClients=32;}globals,*gpGlobals=&globals;
void ClientPrint(Vars*,int,const char*){}
void CSOL_RejectGameRules(const char*){++rejects;}
struct Access {bool allowed=true;bool Allowed()const{return allowed;}};
bool CSOL_CharacterScenarioClass(CBasePlayer* player,int& id,const std::shared_ptr<Access>&)
{if(onClass)onClass();id=1;return player->selected;}
struct Progress {int level=1;};
struct Upgrade {Progress state;const Progress* State()const{return &state;}};
struct Registry
{
    std::array<std::shared_ptr<Upgrade>,33> upgrades;
    csol::scenario::ConfigError error;
    Registry(){for(auto& value:upgrades)value=std::make_shared<Upgrade>();}
    auto Upgrades(const EHANDLE& handle)
    {auto* player=handle.Get<CBasePlayer>();return player?upgrades[player->index]:std::shared_ptr<Upgrade>{};}
    bool Prepare(CBasePlayer& player)
    {++prepares;if(onPrepare)onPrepare();if(!upgrades[player.index])upgrades[player.index]=std::make_shared<Upgrade>();return true;}
    const auto& Error()const{return error;}
};
struct Tables {int scenarioType=1;};
struct MapState
{
    std::uint64_t generation=1;
    ::Tables tables;
    std::shared_ptr<Access> access=std::make_shared<Access>();
    std::uint64_t RoundGeneration()const{return generation;}
    const ::Tables* Tables()const{return &tables;}
    auto EntityAccess()const{return access;}
};
struct DifficultyState
{
    float factor=1;
    bool valid=true;
    bool Parameter(int type,int column,float& value,csol::scenario::ConfigError&)
    {Check(type==15&&column==6,"actual rules use difficulty type15 column6");if(onDifficulty)onDifficulty();value=factor;return valid;}
};
struct MessageState
{
    bool result=true;
    int seconds=-1;
    bool SendRespawnCountdown(edict_t*,bool automatic,int remaining)
    {Check(automatic,"automatic countdown is distinct from paid item prompts");seconds=remaining;++notices;if(onSend)onSend();return result;}
};
struct Session
{
    MapState map;
    DifficultyState difficulty;
    MessageState messages;
    std::shared_ptr<Registry> players=std::make_shared<Registry>();
    auto& Map(){return map;}
    const auto& Map()const{return map;}
    auto& Difficulty(){return difficulty;}
    auto& Messages(){return messages;}
    auto Players()const{return players;}
};
struct Runtime
{
    std::shared_ptr<Session> session=std::make_shared<Session>();
    std::shared_ptr<Registry> players=session->players;
    bool closing{};
    struct {int alive{},total{};bool valid{};}ctCounts;
    bool Closing()const{return closing;}
};
enum class Phase {Loading,Selecting,Waiting,Starting,Playing,Failed,FreePlay};
struct CCSOLScenarioRules
{
    std::shared_ptr<Runtime> m_runtime=std::make_shared<Runtime>();
    Phase m_phase=Phase::Playing;
    bool ready=true,started=true,m_bRoundTerminating{},m_bFreezePeriod{},m_restarting{},m_completing{},m_applyingFinish{},m_respawning{};
    int m_aliveCt=1,actualAlive=1,actualDead=1;
    EHANDLE m_automaticSpawn;
    std::uint64_t m_automaticSpawnRound{},m_automaticSpawnLife{};
    bool SelectionReady()const{return ready&&OwnsRules();}
    bool Ready()const{return SelectionReady()&&m_phase!=Phase::Selecting;}
    bool OwnsRules()const{return g_pGameRules==this&&!m_runtime->Closing();}
    bool IsGameStarted()const{return started;}
    bool HasCharacterClass(int id)const{return id==1;}
    auto EntityAccess()const{return m_runtime->session->Map().EntityAccess();}
    void InitializePlayerCounts(int& aliveT,int& aliveCt,int& deadT,int& deadCt)
    {++counts;if(onCount)onCount();aliveT=deadT=0;aliveCt=actualAlive;deadCt=actualDead;}
    int FPlayerCanRespawn(CBasePlayer* player)
    {if(onPermit)onPermit();return permitResult&&CanRespawn(*player)?TRUE:FALSE;}
    bool permitResult=true;
#include "actual_automatic_rules.inc"
    std::array<RespawnSlot,33> m_respawns;
};
void CBasePlayer::RoundRespawn()
{
    ++spawns;
    g_pGameRules->SpawnStarted(*this);
    if(onSpawn)onSpawn();
    ++m_iNumSpawns;
    pev->deadflag=DEAD_NO;
    pev->flags&=~FL_SPECTATOR;
    pev->iuser1=0;
    pev->health=100;
}
void Reset()
{
    rejects=notices=spawns=counts=prepares=0;
    onUserId=onIndex=onClass=onCount=onDifficulty=onSend=onPrepare=onPermit=onSpawn={};
    globals={600,32};g_engfuncs.pfnGetPlayerUserId=UserId;
}
struct Fixture
{
    CCSOLScenarioRules rules;
    CBasePlayer player;
    Fixture(){Reset();g_pGameRules=&rules;}
    void Late(float when=600)
    {
        gpGlobals->time=when;player.m_iJoiningState=GETINTOGAME;player.m_bJustConnected=true;
        rules.CharacterSelected(player);player.m_iJoiningState=JOINED;player.m_bJustConnected=false;
        player.pev->deadflag=DEAD_RESPAWNABLE;player.pev->flags=FL_SPECTATOR;
    }
    bool Due(){gpGlobals->time=615;return rules.RespawnThink(player);}
};
void OriginsAndAuthorization()
{
    Fixture f;
    Check(!f.rules.CanRespawn(f.player),"ordinary active-round CanRespawn denies unsolicited spawn");
    f.rules.RespawnThink(f.player);
    Check(spawns==0&&notices==0,"observer polling alone never invents initial timestamp");
    f.Late();gpGlobals->time=603;f.rules.RespawnThink(f.player);
    Check(notices==1&&f.rules.m_runtime->session->Messages().seconds==12,"JOINED frame keeps selection timestamp600");
    Check(spawns==0&&!f.rules.CanRespawn(f.player),"countdown does not create persistent respawn permit");
    const auto initialCounts=counts;
    gpGlobals->time=604;f.rules.RespawnThink(f.player);
    Check(counts==initialCounts,"countdown frames do not enumerate all teammates");
    gpGlobals->time=std::nextafter(615.0f,0.0f);f.rules.RespawnThink(f.player);
    Check(spawns==0,"actual rules deny fraction before original deadline");
    Check(!f.Due(),"successful spawn stops processing the old observer PreThink frame");
    Check(spawns==1&&f.player.pev->deadflag==DEAD_NO&&f.player.m_iNumSpawns==1,"deadline uses native RoundRespawn seam");
    Check(!f.rules.m_respawns[1].clock.Armed()&&!f.rules.m_automaticSpawnLife,"actual SpawnStarted consumes clock and permit");
    Check(f.rules.RespawnThink(f.player),"next living frame resumes ordinary PreThink");
    Check(spawns==1&&!f.rules.CanRespawn(f.player),"later frame and CanRespawn do not repeat old life");

    Fixture dead;dead.player.pev->deadflag=DEAD_DYING;dead.player.m_fDeadTime=600;
    dead.rules.DeathClock(dead.player);
    Check(!dead.rules.m_respawns[1].clock.Armed(),"DYING is not death-clock start");
    dead.player.pev->deadflag=DEAD_DEAD;dead.player.m_fDeadTime=602;
    dead.rules.DeathClock(dead.player);gpGlobals->time=615;dead.rules.RespawnThink(dead.player);
    Check(spawns==0,"ordinary death uses animation-end time602");
    gpGlobals->time=617;dead.rules.RespawnThink(dead.player);
    Check(spawns==1,"ordinary death expires at617");
}
void Eligibility()
{
    for(int gate=0;gate<12;++gate)
    {
        Fixture f;f.Late();
        if(gate==0)f.rules.m_phase=Phase::Failed;
        if(gate==1)f.rules.m_bRoundTerminating=true;
        if(gate==2)f.rules.started=false;
        if(gate==3)f.player.m_iTeam=SPECTATOR;
        if(gate==4)f.player.m_iJoiningState=GETINTOGAME;
        if(gate==5)f.player.m_bJustConnected=true;
        if(gate==6)f.player.m_iMenu=Menu_ChooseAppearance;
        if(gate==7)f.player.pev->iuser1=0;
        if(gate==8)f.player.pev->deadflag=DEAD_DYING;
        if(gate==9)f.player.has_disconnected=true;
        if(gate==10)f.player.pev->flags|=FL_DORMANT;
        if(gate==11)f.rules.m_restarting=true;
        f.Due();Check(spawns==0,"current eligibility gate denies automatic spawn");
    }
    Fixture freeze;freeze.Late();freeze.rules.m_bFreezePeriod=true;freeze.Due();
    Check(spawns==1,"freeze does not pause an already armed clock");
    Fixture frozenStart;frozenStart.rules.m_bFreezePeriod=true;frozenStart.Late();
    Check(!frozenStart.rules.m_respawns[1].clock.Armed(),"freeze disallows ordinary late-join origin");
    Fixture stale;stale.Late();stale.rules.actualAlive=0;stale.Due();
    Check(spawns==0&&stale.rules.m_aliveCt==0,"cached1 actual0 cannot revive same-frame team wipe");
    Fixture callback;callback.Late();onPermit=[&]{callback.rules.actualAlive=0;};callback.Due();
    Check(spawns==0,"last teammate dying inside CanRespawn hook revokes due permit");
    Fixture noTeam;noTeam.rules.actualAlive=0;noTeam.Late();
    Check(!noTeam.rules.m_respawns[1].clock.Armed(),"selection does not seed using stale surviving-team cache");
}
void ReentryAndInvalidation()
{
    Fixture reenter;reenter.Late();onPermit=[&]{reenter.rules.RespawnThink(reenter.player);};reenter.Due();
    Check(spawns==1,"CanRespawn callback reentry cannot duplicate current spawn");
    Fixture veto;veto.Late();veto.rules.permitResult=false;veto.Due();
    Check(spawns==0&&veto.rules.m_respawns[1].clock.Armed()&&!veto.rules.m_automaticSpawnLife,"temporary plugin veto revokes permit without consuming clock");
    veto.rules.permitResult=true;veto.Due();Check(spawns==1,"later allowed frame can consume vetoed clock");
    Fixture nestedSpawn;nestedSpawn.Late();onPermit=[&]{nestedSpawn.player.RoundRespawn();};nestedSpawn.Due();
    Check(spawns==1&&!nestedSpawn.rules.m_automaticSpawnLife,"plugin Spawn cancels outer automatic spawn");
    for(int seam=0;seam<5;++seam)for(int mutation=0;mutation<8;++mutation)
    {
        Fixture f;f.Late();
        auto mutate=[&]{
            if(mutation==0)++f.rules.m_runtime->session->map.generation;
            if(mutation==1)++f.player.entity.serialnumber;
            if(mutation==2)f.player.has_disconnected=true;
            if(mutation==3)++f.player.m_iNumSpawns;
            if(mutation==4)f.rules.m_runtime=std::make_shared<Runtime>();
            if(mutation==5)f.rules.m_runtime->session=std::make_shared<Session>();
            if(mutation==6)f.rules.m_runtime->session->map.access->allowed=false;
            if(mutation==7)f.rules.m_runtime->session->map.access=std::make_shared<Access>();
        };
        if(seam==0)onClass=mutate;
        if(seam==1)onCount=mutate;
        if(seam==2)onPermit=mutate;
        if(seam==3)onDifficulty=mutate;
        if(seam==4)onUserId=mutate;
        f.Due();Check(spawns==0,"identity/round/life invalidation at SDK seam cancels due spawn");
        Check(!f.rules.m_automaticSpawnLife,"invalid callback cannot leak automatic permit");
    }
    Fixture countdown;countdown.Late();onSend=[&]{++countdown.rules.m_runtime->session->map.generation;};
    gpGlobals->time=603;countdown.rules.RespawnThink(countdown.player);onSend={};countdown.Due();
    Check(spawns==0,"countdown callback changing round invalidates old observer clock");
    Fixture prepared;prepared.Late();prepared.rules.m_runtime->players->upgrades[1].reset();prepared.Due();
    Check(prepares==1&&spawns==1,"initial observer is prepared without paid consumable/account dependencies");
}
void SlotLifetimes()
{
    Fixture f;
    std::array<CBasePlayer,32> players;
    for(int i=0;i<32;++i)
    {
        auto& player=players[i];player.index=i+1;player.entity.userid=101+i;
        player.m_iJoiningState=GETINTOGAME;gpGlobals->time=600+static_cast<float>(i);
        f.rules.CharacterSelected(player);player.m_iJoiningState=JOINED;
    }
    for(int i=0;i<32;++i)
    {
        gpGlobals->time=615+static_cast<float>(i);f.rules.RespawnThink(players[i]);
        Check(spawns==static_cast<unsigned>(i+1),"all32 edict/userid slots retain independent origins and permits");
    }
    auto& reused=players[31];++reused.entity.serialnumber;++reused.entity.userid;
    reused.m_iNumSpawns=0;reused.pev->deadflag=DEAD_RESPAWNABLE;reused.pev->iuser1=1;
    gpGlobals->time=1000;f.rules.RespawnThink(reused);
    Check(spawns==32,"reused slot32 does not inherit prior connection clock");
    reused.m_iJoiningState=GETINTOGAME;f.rules.CharacterSelected(reused);reused.m_iJoiningState=JOINED;
    gpGlobals->time=1015;f.rules.RespawnThink(reused);
    Check(spawns==33,"replacement slot32 starts only from its own accepted selection");
}
void SelectionCallbacks()
{
    for(int seam=0;seam<2;++seam)for(int mutation=0;mutation<12;++mutation)
    {
        Fixture f;
        auto mutate=[&]{
            if(mutation==0)++f.rules.m_runtime->session->map.generation;
            if(mutation==1)f.rules.m_runtime->session=std::make_shared<Session>();
            if(mutation==2)f.rules.m_phase=Phase::Failed;
            if(mutation==3)f.player.pev->deadflag=DEAD_NO;
            if(mutation==4)f.player.m_iTeam=SPECTATOR;
            if(mutation==5)f.player.m_iJoiningState=PICKINGTEAM;
            if(mutation==6)++f.player.m_iNumSpawns;
            if(mutation==7)f.rules.m_bRoundTerminating=true;
            if(mutation==8)f.rules.m_bFreezePeriod=true;
            if(mutation==9)f.rules.started=false;
            if(mutation==10)f.rules.m_runtime->session->map.access->allowed=false;
            if(mutation==11)f.rules.m_runtime->session->map.access=std::make_shared<Access>();
        };
        if(seam==0)onCount=mutate;else onUserId=mutate;
        f.Late();Check(!f.rules.m_respawns[1].clock.Armed(),"selection SDK callback cannot seed a replaced lifecycle or ineligible player");
    }
    Fixture staleZero;staleZero.rules.m_aliveCt=0;staleZero.rules.actualAlive=1;staleZero.Late();
    Check(staleZero.rules.m_respawns[1].clock.Armed(),"selection uses current live team rather than stale zero count");
}
int main()
{
    OriginsAndAuthorization();Eligibility();ReentryAndInvalidation();SlotLifetimes();SelectionCallbacks();
    std::printf("automatic respawn actual rules: %u checks, %u failures; controlled SDK boundaries, no full DLL or live game\n",checks,failures);
    return failures?1:0;
}
