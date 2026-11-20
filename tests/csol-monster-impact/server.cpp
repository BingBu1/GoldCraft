// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production SDK objects; controlled engine allocation/message edges.
// Damage aggregation, registered rules, Session, Pool and TraceAttack are real.
#include "../csol-scenario-monster-skills/trap_fixture.hpp"
#include "../../native/csol/scenario/monsters/monster_impact.hpp"
#include "../../native/csol/scenario/custom/body_stains.hpp"
#include "../../native/csol/server/custom/body_stains.h"
#include <cstddef>
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
        // The fixture supplies trace contact points rather than studio geometry.
        g_engfuncs.pfnGetModelPtr = [](edict_t*) -> void* { return nullptr; };
        g_pGameRules=InstallGameRules();Require(g_pGameRules&&!CSOL_RulesRejected(),"actual registered rules");
        {CSOLScenarioSdkScope scope;runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());Require(scope.Current()&&runtime,"actual runtime registry");}
        session=runtime->session;
        const bool loaded=session->Precache(csol::scenario::RulesProfile::StandardVer2, "map_session_test",1,CSOLScenarioResourceScope::Selection);
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
void BodyStains(Engine& e)
{
    namespace custom = csol::scenario::custom;
    Fixture f(e);
    auto* playerEdict = e.Reserve(1, "player");
    auto* observer = e.Reserve(2, "player");
    auto& player = *GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&playerEdict->v));
    auto& receiver = *GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&observer->v));
    for (auto* owner : {&player, &receiver})
    {
        owner->m_iTeam = CT;
        owner->pev->flags = FL_CLIENT | FL_ONGROUND;
        owner->pev->health = 100;
        owner->pev->deadflag = DEAD_NO;
        owner->pev->model = Engine::String("models/player/fixture/fixture.mdl");
        owner->pev->modelindex = Engine::Model(STRING(owner->pev->model));
    }
    g_engfuncs.pfnCanSkipPlayer = [](const edict_t*) { return 0; };
    // Match the native initialization required by the weapon factories.
    WeaponInfoReset();
    gmsgCurWeapon = 0;
    LinkUserMessages();
    UTIL_PrecacheOtherWeapon("weapon_ak47");
    UTIL_PrecacheOtherWeapon("weapon_usp");
    const auto equip = [&](const char* name)
    {
        auto* entity = Engine::Create(Engine::String(name));
        entity->v.spawnflags |= SF_NORESPAWN;
        Require(e.api.pfnSpawn(entity) == 0, "native weapon spawn");
        auto* weapon = dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(entity));
        Require(weapon && player.AddPlayerItem(weapon), "actual native inventory accepts the deployed weapon");
        weapon->AttachToPlayer(&player);
        player.SelectItem(name);
        Require(player.m_pActiveItem == weapon && player.pev->viewmodel && player.pev->weaponmodel,
            "actual native active weapon and view/held models");
        return weapon;
    };
    auto* rifle = equip("weapon_ak47");
    f.monster->pev->modelindex = Engine::Model(STRING(f.monster->pev->model));
    e.globals.v_forward = Vector(1, 0, 0);
    e.globals.v_right = Vector(0, 1, 0);
    e.globals.v_up = Vector(0, 0, 1);
    g_engfuncs.pfnCheckVisibility = [](edict_t*, unsigned char*) { return 1; };
    g_engfuncs.pfnTraceTexture = [](edict_t*, const float*, const float*) -> const char* { return "CONCRETE"; };
    g_engfuncs.pfnDeltaFindField = [](delta_s*, const char* field)
    { return std::strcmp(field, custom::BodyStainDeltaField) ? -1 : 20; };
    alignas(void*) std::byte testDelta{};
    auto* schema = reinterpret_cast<delta_s*>(&testDelta);
    CSOL_RegisterBodyStainDelta(true, schema);
    CSOL_SetBodyStainPolicy(true, 128);
    Expect(!CSOL_BodyStainsActive(), "player delta alone cannot activate stain snapshots");
    CSOL_RegisterBodyStainDelta(false, schema);
    CSOL_SetBodyStainPolicy(true, 128);
    Require(CSOL_BodyStainsActive(), "both stain deltas activate the effect");

    const auto wire = [&](CBaseEntity& entity, bool isPlayer, edict_t* host)
    {
        entity_state_t state{};
        Require(e.api.pfnAddToFullPack(&state, entity.entindex(), entity.edict(), host, 0, isPlayer, nullptr) != 0,
            "native AddToFullPack produced the model snapshot");
        return static_cast<std::uint32_t>(state.iuser3);
    };
    const auto local = [&]{ return custom::UnpackBodyStain(wire(player, true, playerEdict)); };
    const auto zombie = [&]{ return custom::UnpackBodyStain(wire(*f.monster, false, playerEdict)); };
    const auto hit = [&](float distance, bool lands = true)
    {
        e.onTrace = [&](edict_t* shooter, TraceResult& trace)
        {
            if (!lands || shooter != playerEdict) return;
            trace.flFraction = distance / 512;
            trace.vecEndPos = Vector(distance, 0, 0);
            trace.pHit = f.monster->edict();
            trace.iHitgroup = HITGROUP_CHEST;
        };
        Vector origin{}, direction(1, 0, 0);
        player.FireBullets3(origin, direction, 0, 512, 1, BULLET_PLAYER_762MM, 20, 1, player.pev, false, 1);
        e.onTrace = {};
    };

    Expect(local().enabled && !local().body && !local().weapon, "local policy publishes clean arms and gun");
    hit(64, false);
    Expect(!local().body && !local().weapon && !zombie().body, "a missed shot cannot stain anything");
    hit(129);
    Expect(!local().body && !local().weapon && zombie().body && !zombie().weapon,
        "a distant hit stains only the zombie");
    const auto originalZombie = zombie().body;

    ClearMultiDamage();
    TraceResult thrown{};
    thrown.pHit = f.monster->edict();
    thrown.iHitgroup = HITGROUP_CHEST;
    thrown.vecEndPos = Vector(24, 0, 0);
    f.monster->TraceAttack(player.pev, 10, Vector(1, 0, 0), &thrown, DMG_BULLET);
    ApplyMultiDamage(player.pev, player.pev);
    Expect(!local().body && !local().weapon, "an unrelated close TraceAttack cannot stain the shooter");
    hit(128);
    const auto stained = local();
    Require(stained.enabled && stained.body && stained.weapon, "a confirmed boundary hit stains arms and the weapon");
    Expect(zombie().body == originalZombie, "the hit retains the prior zombie variation");
    const auto held = custom::UnpackBodyStain(wire(player, true, observer));
    Expect(held.enabled && !held.body && held.weapon == stained.weapon,
        "another observer receives the authoritative gun variation");
    hit(16);
    Expect(local().body == stained.body && local().weapon == stained.weapon, "successive shots keep established stains");

    auto* pistol = equip("weapon_usp");
    Expect(local().body == stained.body && !local().weapon, "a clean pistol inherits arms without inheriting rifle blood");
    player.SelectItem("weapon_ak47");
    Require(player.m_pActiveItem == rifle, "real inventory switches back to the same rifle");
    Expect(local().body == stained.body && local().weapon == stained.weapon, "switching back recovers the original gun variation");

    e.globals.time = 3600;
    CSOL_SetBodyStainPolicy(true, 128);
    Expect(local().body == stained.body && local().weapon == stained.weapon && zombie().body == originalZombie,
        "elapsed time neither clears nor replaces stains");
    f.monster->pev->deadflag = DEAD_DYING;
    f.monster->pev->health = 0;
    Expect(zombie().body == originalZombie, "visible dying zombie retains its stain");
    player.pev->deadflag = DEAD_DEAD;
    player.pev->health = 0;
    Expect(local().body == stained.body && !local().weapon, "dead player retains their arms without showing a held gun");
    player.pev->deadflag = DEAD_NO;
    player.pev->health = 100;
    ++player.m_iNumSpawns;
    Expect(local().body == stained.body && local().weapon == stained.weapon,
        "new life can display the same arms and weapon while their entities survive");

    Vector origin{}, angles{}, velocity{};
    auto* box = CreateWeaponBox(rifle, &player, "models/w_ak47.mdl", origin, angles, velocity, 300, false);
    Require(box && rifle->pev->owner == box->edict(), "actual drop packs this exact native weapon");
    const auto dropped = custom::UnpackBodyStain(wire(*box, false, playerEdict));
    Expect(!dropped.body && dropped.weapon == stained.weapon, "weaponbox publishes the same gun stain");
    Expect(local().body == stained.body && !local().weapon, "dropping the rifle retains arms only");
    box->pev->flags |= FL_ONGROUND;
    box->Touch(&receiver);
    Require(rifle->m_pPlayer == &receiver && rifle->pev->owner == observer, "actual weaponbox pickup keeps rifle entity");
    receiver.SelectItem("weapon_ak47");
    Require(receiver.m_pActiveItem == rifle, "receiver wields the picked-up rifle");
    const auto received = [&]{ return custom::UnpackBodyStain(wire(receiver, true, observer)); };
    Expect(!received().body && received().weapon == stained.weapon, "pickup transfers the weapon blood without transferring arms");

    CSOL_SetBodyStainPolicy(false, 128);
    Expect(!local().enabled && !received().enabled && !zombie().enabled, "off hides all stains");
    CSOL_SetBodyStainPolicy(true, 0);
    Expect(local().body == stained.body && received().weapon == stained.weapon && zombie().body == originalZombie,
        "turning the feature back on shows established stains even at zero splash radius");
    player.SelectItem("weapon_usp");
    Require(player.m_pActiveItem == pistol, "actual alternate survives the rifle drop");
    f.monster->pev->deadflag = DEAD_NO;
    f.monster->pev->health = 100;
    hit(64);
    Expect(local().body == stained.body && !local().weapon, "zero radius prevents new splashes");

    // Same-map round generation retires old pool occupants, while the
    // existing player and weapon edicts remain owned by this loaded map.
    CSOL_ClearBodyStainSplash();
    Require(f.session->Monsters().RestartRound(), "actual same-map monster pool restart");
    Require(f.session->Map().BeginRound(0, e.globals.time, 0, f.waves), "actual same-map round generation");
    Expect(local().body == stained.body && received().weapon == stained.weapon, "same-map round preserves surviving stains");
    Expect(!CSOL_PackBodyStain(*f.monster->edict(), f.monster->entindex(), false, false),
        "parked pool slot cannot publish a previous monster's stain");
    bool reused = false;
    for (std::size_t i = 0; i < CCSOLMonsterPool::Capacity; ++i)
    {
        auto* next = f.session->Monsters().Acquire(f.type, 7, f.session->Map().SpawnPoints().Points());
        Require(next, "real pool acquire exercises same slot");
        if (next == f.monster)
        {
            reused = true;
            Expect(!CSOL_PackBodyStain(*next->edict(), next->entindex(), false, false),
                "a new monster at the same edict cannot inherit former stains");
        }
        Require(f.session->Monsters().Free(*next), "release lease during FIFO traversal");
        if (reused) break;
    }
    Require(reused, "fixture reaches the same monster edict");

    const auto rifleIndex = rifle->entindex();
    Require(receiver.RemovePlayerItem(rifle), "native inventory detaches removed weapon");
    Engine::Remove(rifle->edict());
    auto* replacement = e.Reserve(rifleIndex, "weapon_ak47");
    auto* clean = GetClassPtr<CCSAK47>(reinterpret_cast<CAK47*>(&replacement->v));
    receiver.m_pActiveItem = clean;
    Expect(!received().weapon, "a new weapon at the same edict cannot inherit former stains");

    FreeGameRules(&g_pGameRules);
    Expect(!CSOL_BodyStainsActive(), "map retirement resets stain policy");
    CSOL_SetBodyStainPolicy(true, 128);
    const auto freshMap = custom::UnpackBodyStain(CSOL_PackBodyStain(*playerEdict, 1, true, true));
    Expect(freshMap.enabled && !freshMap.body && !freshMap.weapon,
        "a fresh map cannot publish stains from the retired map");
    CSOL_SetBodyStainPolicy(false, 0);
    e.onTrace = {};
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
int main(int argc, char** argv)
{
    try
    {
        auto engine = std::make_unique<Engine>();
        const std::string_view group = argc > 1 ? argv[1] : "all";
        if (group == "all" || group == "positive") Positive(*engine);
        if (group == "all" || group == "excluded") Exclusions(*engine);
        if (group == "all" || group == "reentry") Reentry(*engine);
        if (group == "all" || group == "stains") BodyStains(*engine);
    }
    catch (const std::exception& error)
    {
        ++failed;
        std::cerr << "EXCEPTION " << error.what() << '\n';
    }
    const int failures = failed + sdk_test::failures;
    std::cout << "behaviorChecks=" << assertions << " checks=" << sdk_test::checks
        << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
