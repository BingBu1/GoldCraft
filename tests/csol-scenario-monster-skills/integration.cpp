// SPDX-License-Identifier: GPL-3.0-or-later
// Complete SDK component replay: real GameRules installs the services and
// factory, and actual DispatchThink reaches ordinary RunTree and UseSkill.
#include "trap_fixture.hpp"
#include "../../native/csol/server/scenario_monster_services.h"
#include "../../native/csol/server/scenario_gameplay.h"
#include "hostage/hostage.h"

namespace
{
using namespace sdk_test;
struct Fixture
{
    inline static Fixture* active{};
    std::unique_ptr<Engine> engineStorage{std::make_unique<Engine>()};
    Engine& engine{*engineStorage};
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    trap_test::Waves waves;
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t, 35> sequence{}; } model;
    CCSOLScenarioMonster* monster{};
    int skill{};

    explicit Fixture(int value) : skill(value)
    {
        Require(!active, "one actual services integration fixture"); active = this;
        engine.files.clear();
        for (const auto& entry : trap_test::Resources(0))
        {
            auto path = entry.first, bytes = entry.second;
            if (const auto p = path.find("map_session_test"); p != std::string::npos)
                path.replace(p, std::strlen("map_session_test"), "zs_deadend");
            if (path == "resource/maplist.csv")
            {
                const auto p = bytes.find("map_session_test"), row = bytes.find("\r\n1,fixture,");
                Require(p != std::string::npos && row != std::string::npos, "authored map row exists");
                bytes.replace(p, std::strlen("map_session_test"), "zs_deadend"); bytes.replace(row + 2, 1, "98");
            }
            engine.files.emplace(std::move(path), std::move(bytes));
        }
        engine.files["maps/zbs_zs_deadend.cso"] = difficulty_test::Encrypt(
            "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nRewardRound,0\r\n"
            "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
            "0,1,0,0,0,0,0,0,0,0,0,1\r\n");
        engine.files["maps/ZBS_ZT_zs_deadend.cso"] = difficulty_test::Encrypt(
            "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
            "1,zombi_origin,1,1,1,100,0,100,1,0," + std::to_string(skill) +
            ",255 255 255,0,0,0,0,7,1\r\n");
        engine.globals.mapname = Engine::String("zs_deadend");
        engine.globals.time = 10; engine.globals.frametime = .03f;
        engine.Set("mp_freezetime", "0"); engine.Set("csol_scenario_difficulty", "1");
        engine.Set("csol_scenario_short_final_delay", "0"); engine.Prepare("2");
        model.header.numseq = static_cast<int>(model.sequence.size()); model.header.length = sizeof(model);
        model.header.seqindex = static_cast<int>(reinterpret_cast<char*>(model.sequence.data()) - reinterpret_cast<char*>(&model));
        constexpr std::array labels{"zbs_idle1","zbs_walk","zbs_run","zbs_jump","zbs_attack_walk","zbs_attack1_run","zbs_attack2_run",
            "zbs_aim_grenade_idle1","zbs_aim_grenade_walk","zbs_aim_grenade_run","zbs_shoot_grenade_idle1","zbs_shoot_grenade_walk","zbs_shoot_grenade_run",
            "head_flinch","gut_flinch","head","death1","death2","death3","gutshot","left","back","right","forward",
            "zbs_shoot_grenade_idle1","zbs_skill_dash","zbs_skill1_idle","zbs_skill1_walk","zbs_skill1_run","skill1","zbs_climb","turnL","turnR"};
        for (auto& sequence : model.sequence) { sequence.fps = 10; sequence.numframes = 11; sequence.flags = STUDIO_LOOPING; }
        for (std::size_t i = 0; i < labels.size(); ++i) std::strcpy(model.sequence[i + 2].label, labels[i]);
        g_engfuncs.pfnGetModelPtr = [](edict_t*) -> void* { return &active->model; };
        // A flat test world has ground at Z=0 and unobstructed horizontal rays.
        // Engine traces remain controlled; all NAV/skill/ordinary logic is real.
        g_engfuncs.pfnTraceLine = [](const float* begin, const float* end, int, edict_t*, TraceResult* trace)
        {
            *trace = {}; trace->flFraction = 1; trace->vecEndPos = Vector(end);
            if (begin[2] >= 0 && end[2] < 0)
            {
                trace->flFraction = begin[2] / (begin[2] - end[2]);
                trace->vecEndPos = Vector(begin) + (Vector(end) - Vector(begin)) * trace->flFraction;
                trace->vecPlaneNormal = Vector(0, 0, 1); trace->pHit = Engine::At(0);
            }
        };
        gmsgCurWeapon = 0; LinkUserMessages(); CSOL_PrecacheCharacters();
        g_pGameRules = InstallGameRules(); CSOL_PrecacheGameRules();
        Require(g_pGameRules && !CSOL_RulesRejected(), "real rules precache assembles actual damage, death, skills and gameplay");
        {
            CSOLScenarioSdkScope scope;
            runtime = CSOL_TestScenarioRuntimeForAccess(scope.EntityAccess().get());
            Require(scope.Current() && runtime && runtime->monsterServices && runtime->gameplay,
                "actual registered runtime owns concrete services and gameplay");
        }
        for (const char* name : {"playerspawn", "zombiespawn", "zbstrigger"})
            for (int id : {1, 2, 30})
            {
                if (std::strcmp(name, "zbstrigger") && id != 1) continue;
                auto* ed = Engine::Create(Engine::String(name));
                ed->v.origin = Vector(128, 128, 0);
                if (auto* p = dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = CT; }
                if (auto* p = dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = TERRORIST; p->m_iMaxSpawn = 5; }
                if (auto* p = dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))) { p->m_iId = id; ed->v.model = Engine::String("*1"); }
                Require(engine.api.pfnSpawn(ed) == 0, "integration markers enter actual DispatchSpawn");
                ed->v.mins = Vector(120, 120, 0); ed->v.maxs = Vector(136, 136, 64);
            }
        Require(CSOL_ActivateGameRules(), "actual rules activate navigation, players, factory and pool");
        Require(runtime->gameplay->BeginRound(0, 10, 0, waves), "actual gameplay installs round identity");
        auto* ed = engine.Reserve(1, "player");
        auto* player = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags = FL_CLIENT; ed->v.health = 100; ed->v.deadflag = DEAD_NO;
        ed->v.origin = Vector(512, 512, 0); ed->v.view_ofs = Vector(0, 0, 17);
        player->m_iTeam = CT; player->m_iNumSpawns = 1; player->m_bJustConnected = false;
        player->m_iJoiningState = JOINED; player->has_disconnected = false;
        CSOLScenarioPlayerProfile profile; profile.equipment.classModsComplete = true;
        Require(runtime->players->SetProfile(*player, std::move(profile)), "target has real current player profile");
        const auto spawned = runtime->gameplay->Factory().Spawn(TERRORIST, 1, 1, 1);
        if (spawned != CSOLMonsterSpawnResult::Spawned)
            std::cerr << "FACTORY " << runtime->gameplay->Factory().Error().message << '\n';
        Require(spawned == CSOLMonsterSpawnResult::Spawned, "actual factory creates skill-bearing ordinary monster");
        for (auto& candidate : engine.edicts)
            if (auto* entity = dynamic_cast<CCSOLScenarioMonster*>(CBaseEntity::Instance(&candidate));
                entity && entity->Pool() && entity->Pool()->Borrowed(*entity)) monster = entity;
        Require(monster && dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(monster->Behavior()), "actual concrete ordinary behavior installed");
        monster->pev->flags |= FL_ONGROUND; cv_hostage_stop.value = 0;
    }
    CCSOLOrdinaryMonsterBehavior& Behavior()
    { return *dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(monster->Behavior()); }
    void Tick(float now)
    {
        engine.globals.time = now;
        // GoldSrc clears a due deadline before dispatch. Without this, the
        // old early-return bug would look scheduled by its previous value.
        monster->pev->nextthink = 0;
        engine.api.pfnThink(monster->edict());
    }
    int Effects() const
    {
        int count{};
        for (const auto& ed : engine.edicts)
            if (!ed.free && !(ed.v.flags & FL_KILLME) && ed.v.classname &&
                !std::strcmp(STRING(ed.v.classname), skill == 3 ? "zombitrap" : "deimostail")) ++count;
        return count;
    }
    ~Fixture()
    { engine.Clear(); runtime.reset(); cv_hostage_stop.value = 0; active = nullptr; }
};

void Run(int skill, bool oldUnsupported)
{
    Fixture f(skill);
    for (int step = 1; step <= 100; ++step)
    {
        f.Tick(10 + step * .1f);
        if (!f.Behavior().Error().message.empty()) break;
    }
    if (oldUnsupported)
    {
        Check(f.Behavior().Error().message == "scenario ordinary skill activation is not implemented for skill " + std::to_string(skill),
            "old services UseSkill emits the expected unsupported skill error through actual RunTree");
        f.Tick(f.engine.globals.time + .1f);
        Check(f.monster->pev->nextthink == 0, "old poisoned ordinary Think leaves nextthink zero after next dispatch");
    }
    else
    {
        if (!f.Behavior().Error().message.empty()) std::cerr << "ORDINARY " << f.Behavior().Error().message << '\n';
        Check(f.Behavior().Error().message.empty(), "new actual services skill leaves ordinary behavior healthy");
        Check(f.Effects() > 0, "actual RunTree skill creates its real auxiliary entity");
        f.Tick(f.engine.globals.time + .1f);
        Check(f.monster->pev->nextthink > f.engine.globals.time,
            "new real ordinary Think continues scheduling after skill execution");
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        const int skill = argc > 1 ? std::atoi(argv[1]) : 3;
        Require(skill == 3 || skill == 8, "integration selects supported tested skill 3 or 8");
        Run(skill, argc > 2 && !std::strcmp(argv[2], "old-unsupported"));
    }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "monster-skill-integration " << sdk_test::checks << " checks " << sdk_test::failures << " failures\n";
    return sdk_test::failures ? 1 : 0;
}
