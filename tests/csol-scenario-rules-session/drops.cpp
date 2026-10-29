// SPDX-License-Identifier: GPL-3.0-or-later
// Component integration in the complete SDK executable. Engine edges are
// controlled; no fake UTIL_Remove, player class, Session or entity dispatch.
#include "sdk_engine.hpp"
#include "../../native/csol/server/scenario_drops.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../../native/csol/server/scenario_entity_lookup.h"
#include "../../native/csol/server/scenario_monster_factory.h"
#include "../../native/csol/server/scenario_weapon_state.h"
#include "../csol-scenario-navigation-loader/support.hpp"
#include <set>

namespace
{
using namespace sdk_test;
using csol::scenario::ConfigError;
using difficulty_test::Encrypt;

std::map<std::string,std::string> DropResources()
{
    auto files=session_test::Resources();
    // Authored inputs exercise real loaders. They are not the original map or
    // an oracle for its probabilities, reward IDs, navigation or combat.
    files["maps/zbs_map_session_test.cso"]=Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
        "round,waveTypes\r\n1,1\r\n");
    files["maps/ZBS_ZT_map_session_test.cso"]=Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,0,255 255 255,0,0,0,0,7,1\r\n");
    files["maps/ZBS_DropItem.cso"]=Encrypt(
        "id,diff,effect,p,owner,count,remove,reward\r\n7,1,0,100,0,3,5,3000\r\n");
    std::string difficulty="header\r\n";
    for(int i=0;i<64;++i){if(i)difficulty+=',';difficulty+='1';}
    files["maps/ZBS_Difficulty_Default.cso"]=Encrypt(difficulty+"\r\n");
    std::string levels="level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for(int i=0;i<=100;++i)levels+=std::to_string(i)+",100,0,1,1,1,1,1\r\n";
    files["resource/ZBSVER2/ZBS_neededexp.cso"]=Encrypt(levels);
    files["resource/ZBSVER2/ZBS_class.cso"]=Encrypt("id,hp,damage,speed,armor,ammo,order\r\n");
    files["resource/buff/BuffWeapon.cso"]=Encrypt("header\r\n");
    files["maps/ZBS.json"]="{}";
    files["resource/item.csv"]=";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n"
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n";
    files["resource/defaultitems.csv"]="Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
    files["resource/AnnounceRibbon/announceribbon.cso"]=Encrypt(
        "id,name,unused,unused,priority,category\r\n1,fixture,0,0,1,8\r\n");
    files["resource/AnnounceRibbon/AnnounceCategory.cso"]=Encrypt(
        "header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n");
    files[csol::scenario::DamageDisplayModesPath]=Encrypt(
        R"({"FLOATING_DMG_STYLE":{"MODE":[15]}})");
    csol::scenario::NavigationFile nav;
    nav.areas={nav_loader_test::Area(1,0,0,0,4096)};
    files["maps/map_session_test.nav"]=nav_loader_test::Encode(nav);
    return files;
}

// Keep this on the complete SDK path. The old Layer A Session fixture does
// not supply the modern resource dependencies and cannot reach this gate.
struct PrecacheFixture
{
    Engine& engine;
    CCSOLScenarioSession session;
    explicit PrecacheFixture(Engine& value):engine(value)
    {
        engine.files=DropResources();engine.globals.time=10;engine.Prepare("0");
        g_pGameRules=InstallGameRules();
        Require(g_pGameRules&&!CSOL_RulesRejected(),"native SDK rules host Session resource test");
    }
    ~PrecacheFixture()
    {
        Check(session.Deactivate(),"resource test closes real Session before engine teardown");
        engine.Clear();
    }
};

std::size_t DamageModeLoads(const Engine& engine,std::size_t firstEvent)
{
    return std::count(engine.events.begin()+firstEvent,engine.events.end(),
        std::string("load:")+csol::scenario::DamageDisplayModesPath);
}

void SessionDamageDisplayModes(Engine& engine)
{
    for(bool malformed:{false,true})
    {
        PrecacheFixture f(engine);
        if(malformed)engine.files[csol::scenario::DamageDisplayModesPath]=Encrypt(
            R"({"FLOATING_DMG_STYLE":{"MODE":"15"}})");
        else engine.files.erase(csol::scenario::DamageDisplayModesPath);
        const auto firstEvent=engine.events.size();
        Check(!f.session.Precache("map_session_test",1,CSOLScenarioResourceScope::Combat),
            "Combat rejects missing or malformed floating damage modes");
        Check(f.session.State()==CSOLScenarioSessionState::Failed&&
            f.session.Error().path==csol::scenario::DamageDisplayModesPath&&
            !f.session.Error().detail.message.empty(),"Combat reports the failing modes resource");
        Check(!f.session.DamageDisplayModes()&&engine.buffers.empty(),
            "failed Combat publishes no modes snapshot and releases loaded buffers");
        Check(DamageModeLoads(engine,firstEvent)==1,"Combat reaches modes loader exactly once");
        Require(f.session.Deactivate(),"failed Combat resource state can be cleared");
        Check(f.session.State()==CSOLScenarioSessionState::Empty&&!f.session.DamageDisplayModes(),
            "failed Combat cleanup restores an empty Session");
    }
    for(bool malformed:{false,true})
    {
        PrecacheFixture f(engine);
        if(malformed)engine.files[csol::scenario::DamageDisplayModesPath]="invalid";
        else engine.files.erase(csol::scenario::DamageDisplayModesPath);
        const auto firstEvent=engine.events.size();
        const bool loaded=f.session.Precache("map_session_test",1,CSOLScenarioResourceScope::Selection);
        Require(loaded,f.session.Error().detail.message.c_str());
        Check(f.session.State()==CSOLScenarioSessionState::Precached&&
            f.session.ResourceScope()==CSOLScenarioResourceScope::Selection&&
            !f.session.DamageDisplayModes(),"Selection succeeds without a modes snapshot");
        Check(DamageModeLoads(engine,firstEvent)==0&&engine.buffers.empty(),
            "Selection never loads missing or malformed Combat-only modes");
    }
    {
        PrecacheFixture f(engine);const auto firstEvent=engine.events.size();
        const bool loaded=f.session.Precache("map_session_test",1,CSOLScenarioResourceScope::Combat);
        Require(loaded,f.session.Error().detail.message.c_str());
        auto retained=f.session.DamageDisplayModes();
        Require(bool(retained),"successful Combat publishes a modes snapshot");
        Check(retained->Contains(15)&&!retained->Contains(48)&&
            DamageModeLoads(engine,firstEvent)==1&&engine.buffers.empty(),
            "Combat snapshot owns decoded modes after releasing the file buffer");
        engine.files[csol::scenario::DamageDisplayModesPath]=Encrypt(
            R"({"FLOATING_DMG_STYLE":{"MODE":[48]}})");
        Check(retained->Contains(15)&&!retained->Contains(48),
            "changing file contents cannot mutate the published snapshot");
        Require(f.session.Deactivate(),"successful Combat Session deactivates");
        Check(f.session.State()==CSOLScenarioSessionState::Empty&&!f.session.DamageDisplayModes()&&
            retained->Contains(15),"Deactivate drops Session ownership while a retained snapshot stays valid");
        // Finish actual engine/private cleanup before reusing the same Session.
        engine.Clear();engine.Prepare("0");g_pGameRules=InstallGameRules();
        Require(g_pGameRules&&!CSOL_RulesRejected(),"native SDK rules host reloaded Session");
        const bool reloaded=f.session.Precache("map_session_test",1,CSOLScenarioResourceScope::Combat);
        Require(reloaded,f.session.Error().detail.message.c_str());
        auto replacement=f.session.DamageDisplayModes();
        Require(bool(replacement),"reloaded Combat publishes replacement modes");
        Check(replacement!=retained&&replacement->Contains(48)&&!replacement->Contains(15)&&
            retained->Contains(15)&&!retained->Contains(48)&&DamageModeLoads(engine,firstEvent)==2,
            "new map resource snapshot cannot reuse the old mode allowance");
        std::weak_ptr<const csol::scenario::DamageDisplayModes> released=replacement;
        replacement.reset();
        Require(f.session.Deactivate(),"reloaded Combat Session deactivates");
        Check(!f.session.DamageDisplayModes()&&released.expired()&&engine.buffers.empty(),
            "Deactivate releases the final unretained modes owner");
    }
}

struct WaveObserver final : csol::scenario::TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    int summaries{};
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {++summaries;}
    void GoalReached() override {}
};

struct UnusedDeathExecutor final : CCSOLMonsterDeathExecutor
{
    bool ExecuteDeath(CCSOLScenarioMonster&,const CSOLMonsterDeathArguments&) override
    {throw std::runtime_error("drop component test must not claim full death execution");}
};

bool IsBox(const edict_t* ed)
{return ed&&!ed->free&&std::strcmp(STRING(ed->v.classname),"zbsitem")==0;}

struct Fixture
{
    Engine& engine;
    std::shared_ptr<CCSOLScenarioSession> session=std::make_shared<CCSOLScenarioSession>();
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<CCSOLScenarioDrops> drops;
    UnusedDeathExecutor unusedExecutor;
    CCSOLMonsterDeathRequests requests{unusedExecutor};
    WaveObserver waves;
    CBasePlayer* player{};
    CCSOLScenarioMonster* monster{};
    std::uint64_t playerGeneration{};
    explicit Fixture(Engine& value):engine(value)
    {
        engine.files=DropResources();engine.globals.time=10;engine.Prepare("0");
        g_pGameRules=InstallGameRules();Require(g_pGameRules&&!CSOL_RulesRejected(),"native SDK rules host component test");
        // A finite floor is an explicit engine test boundary, not BSP physics.
        engine.onTrace=[](edict_t*,TraceResult& trace){trace.flFraction=0.5f;trace.vecEndPos=Vector(0,0,0);};
        RequireSession(session->Precache("map_session_test",1,CSOLScenarioResourceScope::Combat));
        Marker("playerspawn",1);Marker("zombiespawn",1);
        for(int id:{1,2,30})Marker("zbstrigger",id);
        RequireSession(session->Activate());
        players=std::make_shared<CCSOLScenarioPlayerRuntime>(session);
        Require(players->Activate(),"actual player runtime binds actual equipment and upgrade services");
        player=CreatePlayer(1);
        playerGeneration=players->Find(EHANDLE(player))->generation;
        RequireSession(session->BeginRound(0,engine.globals.time,0,waves));
        Require(waves.summaries==1,"real round published initial wave summary");
        const auto* type=session->Map().Tables()->monsterTypes.Find(1);
        Require(type!=nullptr,"real type parser loaded drop source");
        monster=session->Monsters().Acquire(*type,1,session->Map().SpawnPoints().Points());
        Require(monster!=nullptr,"actual initialized pool lends drop source");
        drops=std::make_shared<CCSOLScenarioDrops>(session,players);
        Require(drops->Activate(),"actual drop owner activates");
    }
    void RequireSession(bool success)
    {Require(success,session->Error().detail.message.c_str());}
    edict_t* Marker(const char* name,int id)
    {
        auto* ed=CREATE_NAMED_ENTITY(Engine::String(name));
        if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))){p->m_iId=id;p->m_iTeam=2;}
        if(auto* m=dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))){m->m_iId=id;m->m_iTeam=1;m->m_iMaxSpawn=5;}
        if(auto* t=dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))){t->m_iId=id;ed->v.model=Engine::String("*1");}
        Require(engine.api.pfnSpawn(ed)==0,"real marker DispatchSpawn");
        ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
        return ed;
    }
    CBasePlayer* CreatePlayer(int slot)
    {
        auto* ed=engine.Reserve(slot,"player");
        auto* result=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags=FL_CLIENT;ed->v.health=100;ed->v.deadflag=DEAD_NO;
        result->m_iTeam=CT;result->m_iNumSpawns=1;result->has_disconnected=false;
        // This registers actual identity/generation only. It does not claim
        // character selection, full Prepare(), inventory or XP delivery.
        Require(players->SetProfile(*result,{}),"actual authority registers player profile");
        Require(players->Find(EHANDLE(result))!=nullptr,"actual authority resolves current player");
        return result;
    }
    bool Spawn()
    {
        CSOLMonsterDeathArguments arguments;arguments.attacker=player;
        CSOLMonsterDeathCall call(session->Map(),requests,*monster,arguments);
        Require(call.Current()==monster,"real death transaction captures current lease");
        ConfigError error;const bool result=drops->MonsterKilled(call,1,1,error);
        Require(error.message.empty(),error.message.c_str());return result;
    }
    std::vector<edict_t*> Boxes() const
    {
        std::vector<edict_t*> out;
        for(auto& ed:engine.edicts)if(IsBox(&ed))out.push_back(&ed);
        return out;
    }
    void ThreeBoxes()
    {
        Require(Spawn(),"MonsterKilled creates actual zbsitem exports");
        Require(Boxes().size()==3&&drops->PendingRewardCount()==3,"three issued tickets are pending");
        for(auto* ed:Boxes())Check(ed->v.movetype==MOVETYPE_TOSS&&ed->v.solid==SOLID_TRIGGER&&
            ed->v.nextthink==10&&ed->v.mins==Vector(-32,-32,0)&&
            std::strcmp(STRING(ed->v.model),"models/dropitem.mdl")==0,"actual Spawn and Configure publish box state");
    }
    void Rewards(std::size_t count)
    {
        Check(drops->Rewards().size()==count,"expected queued reward count");
        std::set<std::uint64_t> tickets;
        for(const auto& reward:drops->Rewards())
        {
            Check(tickets.insert(reward.sequence).second,"one submission per issued ticket");
            Check(reward.playerGeneration==playerGeneration&&reward.rewardId==3000&&
                reward.status==CSOLScenarioDropReward::Status::AwaitingHandler,"request retains original identity and stays undelivered");
        }
    }
    void ResetEdges()
    {engine.onOffset={};engine.onIndexLookup={};engine.onFind={};engine.onSize={};engine.onOrigin={};engine.onTrace={};engine.onSound={};}
    ~Fixture()
    {
        ResetEdges();
        if(drops)drops->Deactivate();
        if(players)players->Invalidate();
        Check(requests.Reset(),"drop test retires death transaction generation");
        // Revocation grants no late EHANDLE cleanup. Mirror the engine's real
        // raw-private release while the Session owner is still retained.
        if(!session->Monsters().EntityAccessAllowed())
            for(auto& ed:engine.edicts)if(ed.pvPrivateData)Engine::FreePrivate(&ed);
        Check(session->Deactivate(),"drop test closes real Session");
        engine.Clear();
    }
};

void SelectedWeaponOwnership(Engine& engine)
{
    Fixture f(engine);
    const auto weapons=f.session->Weapons();
    Require(weapons&&weapons->Current(),"real Session owns the loaded weapon catalog and default selection");
    auto selected=[&](int source,int expected)
    {
        std::int32_t item=-1;ConfigError error;
        Check(weapons->Item(*f.player,source,item,error)&&item==expected,
            "real PlayerRuntime installs original default knife and HE items without carried inventory");
    };
    Check(f.player->m_rgpPlayerItems[KNIFE_SLOT]==nullptr&&f.player->m_rgpPlayerItems[GRENADE_SLOT]==nullptr,
        "selection ownership test has no knife or grenade inventory instance");
    selected(WEAPON_KNIFE,161);selected(WEAPON_HEGRENADE,31);
    CSOLScenarioPlayerProfile invalid;
    invalid.selectedWeapons=std::array<std::int32_t,2>{161,161};
    Check(!f.players->SetProfile(*f.player,invalid),"actual profile authority rejects unsupported selected HE");
    selected(WEAPON_HEGRENADE,31);

    // CreatePlayer intentionally registers a valid profile. Allocate a second
    // actual SDK player directly to observe a rejected first registration.
    auto* unregisteredEdict=engine.Reserve(2,"player");
    auto* unregistered=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&unregisteredEdict->v));
    unregisteredEdict->v.flags=FL_CLIENT;unregisteredEdict->v.health=100;unregisteredEdict->v.deadflag=DEAD_NO;
    unregistered->m_iTeam=CT;unregistered->m_iNumSpawns=1;unregistered->has_disconnected=false;
    const EHANDLE unregisteredHandle(unregistered);
    Require(!f.players->Find(unregisteredHandle),"fresh SDK player has no authority profile before first request");
    Check(!f.players->SetProfile(*unregistered,invalid),"invalid first profile is rejected before default selection registration");
    Check(!f.players->Find(unregisteredHandle),"failed first profile leaves no PlayerRuntime state");
    for(int source:{WEAPON_KNIFE,WEAPON_HEGRENADE})
    {
        std::int32_t rejectedItem=-1;ConfigError rejectedError;
        Check(!weapons->Item(*unregistered,source,rejectedItem,rejectedError)&&rejectedItem==0&&
            !rejectedError.message.empty(),"failed first profile leaves no knife or HE item record");
    }
    Require(f.players->SetProfile(*unregistered,{}),"valid retry registers player after rejected first profile");
    Require(f.players->Find(unregisteredHandle)!=nullptr,"valid retry publishes the player authority state");
    for(int source:{WEAPON_KNIFE,WEAPON_HEGRENADE})
    {
        std::int32_t acceptedItem=-1;ConfigError acceptedError;
        Check(weapons->Item(*unregistered,source,acceptedItem,acceptedError)&&
            acceptedItem==(source==WEAPON_KNIFE?161:31)&&acceptedError.message.empty(),
            "valid retry installs both actual default selections");
    }
    f.players->Remove(unregisteredHandle);Engine::Remove(unregisteredEdict);

    const EHANDLE priorPlayer(f.player);const auto oldGeneration=f.playerGeneration;
    f.players->Remove(priorPlayer);
    std::int32_t item=-1;ConfigError error;
    Check(!weapons->Item(*f.player,WEAPON_HEGRENADE,item,error)&&item==0,
        "PlayerRuntime removal clears the exact selection generation before the real edict is freed");
    Engine::Remove(f.player->edict());f.player=f.CreatePlayer(1);
    f.playerGeneration=f.players->Find(EHANDLE(f.player))->generation;
    Require(f.playerGeneration!=oldGeneration&&!priorPlayer.Get<CBaseEntity>(),
        "real SDK player slot reuse has fresh entity and authority generations");
    weapons->RemovePlayer(priorPlayer,oldGeneration);
    selected(WEAPON_KNIFE,161);selected(WEAPON_HEGRENADE,31);

    // Full SDK linkage supplies the actual CGrenade RTTI/vtable and allocation
    // wrappers. Factory-to-gate registration is independently prepared in World.
    auto* ed=engine.Reserve(engine.nextEntity++,"grenade");
    auto* grenade=GetClassPtr<CCSGrenade>(reinterpret_cast<CGrenade*>(&ed->v));
    Check(weapons->ProjectileSource(*grenade)==0,"fresh native grenade is not implicitly registered by classname");
    Require(weapons->RecordHeGrenade(*grenade)&&weapons->RecordHeGrenade(*grenade),
        "actual CGrenade can be registered repeatedly without changing its identity");
    Check(weapons->ProjectileSource(*grenade)==WEAPON_HEGRENADE,"registered actual grenade exposes HE source4");
    grenade->pev->flags|=FL_KILLME;
    Check(weapons->ProjectileSource(*grenade)==0&&!weapons->RecordHeGrenade(*grenade),
        "removed actual grenade cannot source or refresh HE attribution");
    grenade->pev->flags&=~FL_KILLME;
    const EHANDLE oldGrenade(grenade);const int slot=Engine::Index(ed);
    Engine::Remove(ed);ed=engine.Reserve(slot,"grenade");
    grenade=GetClassPtr<CCSGrenade>(reinterpret_cast<CGrenade*>(&ed->v));
    Check(!oldGrenade.Get<CBaseEntity>()&&weapons->ProjectileSource(*grenade)==0,
        "real private free and serial reuse do not inherit projectile registration");
    Require(weapons->RecordHeGrenade(*grenade),"new native grenade generation receives its own registration");
    f.session->RevokeEntityAccess();
    Check(!weapons->Current()&&weapons->ProjectileSource(*grenade)==0&&!weapons->RecordHeGrenade(*grenade),
        "retained weapon owner denies actual grenade access after Session revocation");
}

void PickupAndClose(Engine& engine)
{
    {
        Fixture f(engine);f.ThreeBoxes();auto boxes=f.Boxes();
        auto* stranger=f.CreatePlayer(2);
        engine.api.pfnTouch(boxes[0],stranger->edict());
        Check(f.drops->PendingRewardCount()==3&&f.drops->Rewards().empty(),"wrong player cannot claim assigned box");
        engine.api.pfnTouch(boxes[0],f.player->edict());
        f.Rewards(1);Check(boxes[0]->v.flags&FL_KILLME,"actual UTIL_Remove marks claimed box");
        engine.api.pfnTouch(boxes[0],f.player->edict());
        f.drops->Deactivate();f.drops->Deactivate();f.Rewards(3);
        Check(!f.drops->Active()&&f.drops->PendingRewardCount()==0,"close flushes every ticket once");
        for(auto* ed:boxes){Check(ed->v.flags&FL_KILLME,"close removes every registered box");Engine::Remove(ed);}
        f.Rewards(3);
    }
    {
        Fixture f(engine);f.ThreeBoxes();
        // Actual sound engine callback closes while ItemTouch is on-stack.
        engine.onSound=[&](edict_t*,const char*){engine.onSound={};f.players->Invalidate();f.drops->Deactivate();};
        engine.api.pfnTouch(f.Boxes()[0],f.player->edict());
        f.Rewards(3);Check(f.drops->PendingRewardCount()==0,"nested close does not stop at first invalid recipient");
    }
    {
        Fixture f(engine);f.ThreeBoxes();engine.api.pfnTouch(f.Boxes()[0],f.player->edict());
        const auto request=f.drops->Rewards().front();using Status=CSOLScenarioDropReward::Status;
        Check(!f.drops->Acknowledge(request.sequence,request.playerGeneration+1,Status::Delivered),"wrong generation cannot acknowledge queued reward");
        Check(!f.drops->Acknowledge(request.sequence+999,request.playerGeneration,Status::Delivered),"unknown ticket cannot acknowledge reward");
        Check(!f.drops->Acknowledge(request.sequence,request.playerGeneration,Status::AwaitingHandler),"pending status is not a terminal acknowledgment");
        Check(f.drops->Acknowledge(request.sequence,request.playerGeneration,Status::Delivered),"matching ticket accepts one terminal status");
        Check(!f.drops->Acknowledge(request.sequence,request.playerGeneration,Status::Rejected)&&
            f.drops->Rewards().front().status==Status::Delivered,"repeated acknowledgment cannot change terminal status");
        // Only the acknowledgment API is tested; no account grant occurs.
    }
}

void RecipientAndTimeout(Engine& engine)
{
    {
        Fixture f(engine);f.ThreeBoxes();auto boxes=f.Boxes();const EHANDLE old(f.player);
        f.players->Remove(old);Engine::Remove(f.player->edict());f.player=f.CreatePlayer(1);
        const auto replacement=f.players->Find(EHANDLE(f.player));
        Require(replacement&&replacement->generation!=f.playerGeneration&&!old.Get(),"same player slot has new serial and generation");
        engine.api.pfnTouch(boxes[0],f.player->edict());
        Check(f.drops->Rewards().empty()&&f.drops->PendingRewardCount()==3,"replacement cannot claim predecessor tickets");
        engine.globals.time=15;
        for(auto* ed:boxes)engine.api.pfnThink(ed);
        Check(f.drops->Rewards().empty()&&f.drops->PendingRewardCount()==0,"missing original recipient discards all expired tickets");
        for(auto* ed:boxes){Check(ed->v.flags&FL_KILLME,"timeout removes orphan box");Engine::Remove(ed);}
        Check(f.drops->Rewards().empty(),"raw-free cannot resurrect discarded ticket");
    }
    for(int flags:{FL_SPECTATOR,FL_DORMANT})
    {
        Fixture f(engine);f.ThreeBoxes();f.player->pev->flags|=flags;engine.globals.time=15;
        for(auto* ed:f.Boxes())engine.api.pfnThink(ed);
        Check(f.drops->Rewards().empty()&&f.drops->PendingRewardCount()==0,"timeout rejects spectator or dormant recipient");
    }
    {
        Fixture f(engine);f.ThreeBoxes();f.player->pev->health=0;f.player->pev->deadflag=DEAD_DEAD;
        engine.globals.time=14;for(auto* ed:f.Boxes())engine.api.pfnThink(ed);
        Check(f.drops->PendingRewardCount()==3,"Think before deadline retains ticket");
        engine.globals.time=15;for(auto* ed:f.Boxes())engine.api.pfnThink(ed);
        f.Rewards(3);Check(f.drops->PendingRewardCount()==0,"original receipt gate is spectator flag, not IsAlive");
    }
}

void CancelCreation(Engine& engine)
{
    enum class Boundary { Configure, Trace, Origin };
    for(auto boundary:{Boundary::Configure,Boundary::Trace,Boundary::Origin})
    {
        Fixture f(engine);int callbacks{};
        const auto cancel=[&](edict_t* ed)
        {if(IsBox(ed)){++callbacks;Require(f.requests.Reset(),"same-map callback cancels captured death generation");}};
        if(boundary==Boundary::Configure)engine.onSize=cancel;
        if(boundary==Boundary::Trace)engine.onTrace=[&](edict_t* ed,TraceResult&){cancel(ed);};
        if(boundary==Boundary::Origin)engine.onOrigin=cancel;
        Check(!f.Spawn()&&callbacks==1,"creation cancels at selected real engine boundary");
        const auto boxes=f.Boxes();Require(boxes.size()==1,"cancellation stops remaining recipients");
        Check(boxes[0]->v.flags&FL_KILLME,"cancelled candidate is removed through actual UTIL_Remove");
        f.Rewards(1);Check(f.drops->PendingRewardCount()==0,"registered cancellation ticket submits exactly once");
        f.ResetEdges();Engine::Remove(boxes[0]);f.Rewards(1);
    }
}

// Guard every page occupied by the edict while cancelled SpawnItem returns. This turns
// an accidental direct EHANDLE/edict read into an observable process failure,
// unlike an engine-call counter which cannot observe inline field accesses.
struct EdictReadGuard
{
    void* page{};SIZE_T length{};DWORD previous{};
    void Arm(const edict_t* ed)
    {
        SYSTEM_INFO info{};GetSystemInfo(&info);const auto pageSize=info.dwPageSize;
        const auto address=reinterpret_cast<std::uintptr_t>(ed);
        const auto begin=address-address%pageSize;
        const auto end=(address+sizeof(edict_t)+pageSize-1)/pageSize*pageSize;
        page=reinterpret_cast<void*>(begin);length=end-begin;
        Require(VirtualProtect(page,length,PAGE_NOACCESS,&previous)!=0,"old edict page protected after map generation change");
    }
    void Restore()
    {if(page){DWORD discarded{};Require(VirtualProtect(page,length,previous,&discarded)!=0,"restore old edict page for fixture teardown");page=nullptr;}}
    ~EdictReadGuard(){Restore();}
};

void OffsetCancellation(Engine& engine)
{
    {
        Fixture f(engine);EdictReadGuard guard;int callbacks{};
        engine.onOffset=[&](const edict_t* ed)
        {
            if(!IsBox(ed))return;
            ++callbacks;engine.onOffset={};
            Require(f.requests.Reset()&&f.session->Map().EndMap(),"OFFSET cancels original map before old memory is inaccessible");
            guard.Arm(ed);
        };
        const bool spawned=f.Spawn();guard.Restore();
        Check(!spawned&&callbacks==1&&f.drops->PendingRewardCount()==0&&f.drops->Rewards().empty(),
            "OFFSET map cancellation returns without old-edict access or reward");
    }
    {
        Fixture f(engine);EHANDLE replacement;int callbacks{};
        engine.onOffset=[&](const edict_t* ed)
        {
            if(!IsBox(ed))return;
            ++callbacks;engine.onOffset={};const int slot=Engine::Index(ed);
            Engine::Remove(const_cast<edict_t*>(ed));auto* reused=engine.Reserve(slot,"replacement_probe");
            auto* probe=GetClassPtr<CCSEntity>(reinterpret_cast<Probe*>(&reused->v));
            reused->v.health=713;replacement=probe;
        };
        Check(f.Spawn()&&callbacks==1,"same-map recycled candidate consumes only its cancelled allocation");
        auto* probe=replacement.Get<Probe>();Require(probe!=nullptr,"replacement survives stale candidate cleanup");
        Check(probe->spawns==0&&probe->pev->health==713&&!(probe->pev->flags&FL_KILLME),
            "old handle neither spawns nor removes replacement private object");
        // Remaining allocations may proceed; only the replaced one never
        // reaches Configure and therefore has no issued ticket.
        Check(f.drops->PendingRewardCount()==2&&f.Boxes().size()==2,"replacement never acquires predecessor ticket");
    }
}

void RawFreeOwnership(Engine& engine)
{
    Fixture f(engine);f.ThreeBoxes();auto boxes=f.Boxes();
    // Deliberately bypass UTIL_Remove to exercise the real engine raw-private
    // release callback, virtual OnDestroy and API wrapper teardown.
    Engine::FreePrivate(boxes[0]);f.Rewards(1);
    Check(!boxes[0]->pvPrivateData&&f.drops->PendingRewardCount()==2,"raw-free submits ticket and releases private object");
    Engine::FreePrivate(boxes[0]);f.Rewards(1);
    f.drops->Deactivate();f.Rewards(3);
    std::weak_ptr<CCSOLScenarioDrops> weak=f.drops;f.drops.reset();
    Check(!weak.expired(),"remaining private entities retain drop owner until OnDestroy");
    Engine::FreePrivate(boxes[1]);Engine::FreePrivate(boxes[2]);
    Check(weak.expired(),"all raw frees release shared owner without C++ entity destructor");
}

void BusyMapRevocation(Engine& engine)
{
    Fixture f(engine);CCSOLScenarioMap map;
    Require(map.BeginMap("map_session_test"),"independent actual map loads authored tables");
    const auto oldAccess=map.EntityAccess();const auto* tables=map.Tables();
    Require(tables&&tables->monsterTypes.Find(1),"busy map retains a real parsed type table");
    auto* marker=f.Marker("playerspawn",11);EdictReadGuard guard;int callbacks{};
    engine.onFind=[&](edict_t* result,const char*,const char*)
    {
        if(result!=marker)return;
        ++callbacks;engine.onFind={};
        Check(!map.EndMap(),"EndMap reports busy while CollectEntities is on-stack");
        Check(!oldAccess->Allowed()&&!map.EntityAccessAllowed(),"busy EndMap immediately revokes access");
        Check(map.Tables()==tables&&tables->monsterTypes.Find(1),"busy EndMap retains borrowed tables");
        guard.Arm(result);
    };
    const bool collected=map.CollectEntities();guard.Restore();
    Check(!collected&&callbacks==1&&map.Tables()==tables,"cancelled collection neither reads old storage nor clears its table");
    Require(map.EndMap()&&map.Tables()==nullptr,"unwound map can finally clear its tables");
    Require(map.BeginMap("map_session_test"),"next map creates new access authority");
    const auto newAccess=map.EntityAccess();
    Check(newAccess!=oldAccess&&newAccess->Allowed()&&!oldAccess->Allowed(),"new map cannot reopen retained old token");
    int forbiddenLookups{};engine.onFind=[&](edict_t*,const char*,const char*){++forbiddenLookups;};
    guard.Arm(marker);
    const auto* oldResult=CSOL_FindScenarioEntity(nullptr,"playerspawn",oldAccess);
    guard.Restore();engine.onFind={};
    Check(!oldResult&&!forbiddenLookups,"old token rejects a fresh lookup before entering engine");
    Check(map.EndMap()&&!newAccess->Allowed()&&!oldAccess->Allowed(),"both retired tokens stay revoked");
}

void CollectionLookupRevocation(Engine& engine)
{
    for(bool trigger:{false,true})for(bool offset:{false,true})
    {
        Fixture f(engine);CCSOLSpawnPointManager points;CCSOLTriggerManager triggers;
        auto oldAccess=std::make_shared<CSOLScenarioEntityAccess>();
        auto replacement=std::make_shared<CSOLScenarioEntityAccess>();
        points.BindEntityAccess(oldAccess);triggers.BindEntityAccess(oldAccess);
        f.Marker("playerspawn",11);
        auto* marker=f.Marker(trigger?"zbstrigger":"zombiespawn",trigger?71:11);
        EdictReadGuard guard;int callbacks{};
        const auto revoke=[&](const edict_t* result)
        {
            if(result!=marker)return;
            ++callbacks;engine.onFind={};engine.onOffset={};oldAccess->Revoke();
            if(trigger)triggers.BindEntityAccess(replacement);else points.BindEntityAccess(replacement);
            guard.Arm(result);
        };
        if(offset)engine.onOffset=revoke;
        else engine.onFind=[&](edict_t* result,const char*,const char*){revoke(result);};
        const bool cancelled=trigger?triggers.CollectMap()==CSOLTriggerCollection::NotCollected:
            points.CollectMap()==CSOLPointCollection::NotCollected;
        guard.Restore();
        Check(cancelled&&callbacks==1,"real point/trigger lookup cancels after Find or OFFSET revocation");
        Check(!oldAccess->Allowed()&&replacement->Allowed(),"replacement authority never revives the old collection stack");
        if(trigger)
        {
            Check(triggers.Volumes().Volumes().empty(),"cancelled lookup publishes no old trigger volume");
            Check(triggers.CollectMap()==CSOLTriggerCollection::Ready&&triggers.Volumes().Volumes().size()==1,
                "fresh trigger collection uses replacement authority");
        }
        else
        {
            Check(points.PlayerPointCount(CT,11)==0,"cancelled lookup publishes no old player snapshot");
            Check(points.CollectMap()==CSOLPointCollection::Ready&&points.PlayerPointCount(CT,11)==1,
                "fresh point collection uses replacement authority");
        }
    }
}

struct ScanActions final : csol::scenario::TriggerActions
{
    // Explicit observation state; Current performs no engine/plugin callback.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Current() const override { return observerValid && observerError.message.empty(); }
    int waves{},refreshes{},goals{};std::function<void()> onWave;
    void ActivateWave(std::uint16_t,float) override {++waves;const auto callback=onWave;if(callback)callback();}
    bool EndWave(std::uint16_t,bool) override {return false;}
    void RefreshWaves() override {++refreshes;}
    void GoalReached() override {++goals;}
};

void InstallScanProgram(csol::scenario::TriggerProgram& program)
{
    csol::scenario::TriggerRuleTable table;csol::scenario::TriggerRule rule;
    rule.group=1;rule.trigger=100;rule.command='w';rule.cooldown=-2;rule.waves={42};
    table.rows.push_back(rule);table.groups[1]={0};csol::scenario::TriggerVolumes volumes;
    volumes.Register({{-10,-20,-30},{10,20,30},100,0});ConfigError error;
    Require(program.Install(table,volumes,1,error),"real trigger program installs authored wave action");
}

void PlayerScanRevocation(Engine& engine)
{
    {
        Fixture f(engine);CCSOLTriggerPlayerScan scan;csol::scenario::TriggerProgram program;ScanActions actions;
        InstallScanProgram(program);auto oldAccess=std::make_shared<CSOLScenarioEntityAccess>();
        auto replacement=std::make_shared<CSOLScenarioEntityAccess>();scan.BindEntityAccess(oldAccess);
        EdictReadGuard guard;int callbacks{};auto* playerEdict=f.player->edict();
        engine.onIndexLookup=[&](int slot,edict_t* result)
        {
            Require(slot==1&&result==playerEdict,"scanner resolves actual first SDK player");
            ++callbacks;engine.onIndexLookup={};oldAccess->Revoke();scan.BindEntityAccess(replacement);guard.Arm(result);
        };
        const auto result=scan.Update(true,program,0,1,actions);guard.Restore();
        Check(!result.valid&&callbacks==1&&scan.NextPlayerSlot()==1&&!actions.waves,
            "INDEXENT revocation does not read returned player or advance old scan");
        Check(!oldAccess->Allowed()&&replacement->Allowed(),"new scan token cannot resume cancelled lookup");
        Check(scan.Update(true,program,0,1,actions).valid&&actions.waves==1&&scan.NextPlayerSlot()==2,
            "next explicit scan uses replacement token and real player");
    }
    {
        Fixture f(engine);CCSOLTriggerPlayerScan scan;csol::scenario::TriggerProgram program;ScanActions actions;
        InstallScanProgram(program);auto oldAccess=std::make_shared<CSOLScenarioEntityAccess>();
        auto replacement=std::make_shared<CSOLScenarioEntityAccess>();scan.BindEntityAccess(oldAccess);
        actions.onWave=[&]{oldAccess->Revoke();scan.BindEntityAccess(replacement);};
        const auto result=scan.Update(true,program,0,1,actions);
        Check(!result.valid&&actions.waves==1&&scan.NextPlayerSlot()==1&&!scan.IsScanning(),
            "trigger callback revocation preserves performed action but cancels scan cursor commit");
    }
}

void RevokedPoolDestroy(Engine& engine)
{
    Fixture f(engine);auto& pool=f.session->Monsters();std::array<edict_t*,CCSOLMonsterPool::Capacity> edicts{};
    for(std::size_t i=0;i<edicts.size();++i){auto* entity=pool.At(i);Require(entity!=nullptr,"capture all actual owned pool exports");edicts[i]=entity->edict();}
    CSOLMonsterDeathArguments arguments;arguments.attacker=f.player;
    CSOLMonsterDeathCall death(f.session->Map(),f.requests,*f.monster,arguments);
    CSOLMonsterSpawnCall spawn(f.session->Map(),pool,f.requests,*f.monster);
    Require(death.Current()==f.monster&&spawn.Current()==f.monster,"actual call guards begin on current lease");
    const auto slot=f.monster->Slot();auto* victim=f.monster;auto* victimEdict=victim->edict();
    f.session->Map().RevokeEntityAccess();EdictReadGuard guard;guard.Arm(victimEdict);
    const bool rejected=!death.Current()&&!death.Attacker()&&!spawn.Current()&&!spawn.SameMap()&&!pool.At(slot);
    spawn.ReturnIncomplete();
    // The engine dispatcher must read edict storage before and after virtual
    // OnDestroy. Protect only the real virtual phase here, then separately
    // execute the complete actual OnFreeEntPrivateData path below.
    static_cast<CBaseEntity*>(victim)->OnDestroy();
    guard.Restore();
    Check(rejected&&!victim->Pool(),"revoked call guards and actual virtual OnDestroy never resolve old edict storage");
    Check(!pool.ForgetMap(),"one destroyed entity cannot release other registered owners");
    for(auto* ed:edicts)Engine::FreePrivate(ed);
    Check(pool.ForgetMap(),"actual raw-free callbacks unregister all remaining pool identities");
    Check(!pool.EntityAccessAllowed()&&!pool.At(slot),"forgetting pool never reopens old access");
}

void RevokedDropTicket(Engine& engine)
{
    Fixture f(engine);f.ThreeBoxes();const auto boxes=f.Boxes();EdictReadGuard guard;int callbacks{};
    engine.onSound=[&](edict_t*,const char*)
    {
        ++callbacks;engine.onSound={};f.session->Map().RevokeEntityAccess();f.players->Invalidate();
        // Actual raw-private destruction submits the issued ticket while its
        // ItemTouch caller is still on-stack. No account/recipient is resolved.
        Engine::FreePrivate(boxes[0]);guard.Arm(boxes[0]);
    };
    engine.api.pfnTouch(boxes[0],f.player->edict());guard.Restore();
    Check(callbacks==1&&!boxes[0]->pvPrivateData,"pickup callback frees actual private object before returning");
    f.Rewards(1);Check(f.drops->PendingRewardCount()==2,"on-stack raw-free submits only its issued ticket");
    f.drops->Deactivate();f.Rewards(1);
    Check(!(boxes[1]->v.flags&FL_KILLME)&&!(boxes[2]->v.flags&FL_KILLME),
        "revoked drop close leaves physical removal to engine");
    for(auto* ed:boxes)Engine::FreePrivate(ed);
    f.Rewards(3);Check(f.drops->PendingRewardCount()==0,"all engine releases queue each issued ticket once after revocation");
}
}

void ScenarioDropCases(sdk_test::Engine& engine)
{
    SessionDamageDisplayModes(engine);
    PickupAndClose(engine);RecipientAndTimeout(engine);CancelCreation(engine);
    OffsetCancellation(engine);RawFreeOwnership(engine);
    SelectedWeaponOwnership(engine);
}

void ScenarioEntityAccessCases(sdk_test::Engine& engine)
{
    BusyMapRevocation(engine);CollectionLookupRevocation(engine);PlayerScanRevocation(engine);
    RevokedPoolDestroy(engine);RevokedDropTicket(engine);
}
