// SPDX-License-Identifier: GPL-3.0-or-later
// Link the actual server objects: FireBullets3, material lookup, hookchains,
// TraceAttack and ApplyMultiDamage are not replaced in this fixture.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/guest/cof/server/ballistics.hpp"
#include "../../native/guest/cof/scenario_damage.hpp"
#include <fstream>
#include <iterator>

namespace
{
using namespace sdk_test;
using namespace guest::cof;
unsigned behaviorChecks{}, scenarios{}, traces{}, materialQueries{}, nativeCalls{};
int expectedBudget{};
entvars_t* expectedAttacker{};
char currentMaterial{'F'};
bool denyPenetration{};

void Expect(bool value, const char* message)
{ ++behaviorChecks; Check(value, message); }

struct Target final : CBaseEntity
{
    std::vector<int> traced, applied, levels;
    int BloodColor() override { return DONT_BLEED; }
    void TraceAttack(entvars_t* attacker, float damage, Vector direction, TraceResult* trace, int type) override
    {
        Expect(attacker == expectedAttacker && type == (DMG_BULLET | DMG_NEVERGIB), "native hit retains attacker and damage bits");
        traced.push_back(int(damage)); levels.push_back(GetDmgPenetrationLevel());
        CBaseEntity::TraceAttack(attacker, damage, direction, trace, type);
    }
    BOOL TakeDamage(entvars_t* inflictor, entvars_t* attacker, float damage, int type) override
    {
        Expect(inflictor == expectedAttacker && attacker == expectedAttacker, "native multi-damage retains shooter attribution");
        applied.push_back(int(damage));
        return CBaseEntity::TakeDamage(inflictor, attacker, damage, type);
    }
};

struct Surface
{
    Target* target;
    float distance;
    float thickness;
    char material;
    int group{HITGROUP_GENERIC};
};
std::vector<Surface> surfaces;

bool Current(void*) noexcept { return true; }
bool Penetrable(IReGameHook_IsPenetrableEntity* chain, Vector& source, Vector& end,
    entvars_t* attacker, edict_t* hit)
{
    Expect(attacker == expectedAttacker, "native penetration hook retains owner");
    return !denyPenetration && chain->callNext(source, end, attacker, hit);
}
Vector& Native(IReGameHook_CBaseEntity_FireBullets3* chain, CBaseEntity* shooter,
    Vector& source, Vector& direction, float spread, float distance, int count,
    int bullet, int damage, float range, entvars_t* attacker, bool pistol, int seed)
{
    ++nativeCalls;
    Expect(count == expectedBudget && count >= 3 && spread == 0 && range == 1 && distance == 8192 && pistol,
        "cold rifle policy reaches the actual native hook");
    return chain->callNext(shooter, source, direction, spread, distance, count, bullet,
        damage, range, attacker, pistol, seed);
}

void Trace(const float* from, const float* to, int flags, edict_t* skip, TraceResult* result)
{
    ++traces;
    Expect(flags == dont_ignore_monsters && skip->v.pContainingEntity->v.flags & FL_CLIENT,
        "actual FireBullets3 uses a player trace");
    Expect(gpGlobals->trace_flags == FTRACE_BULLET, "native bullet trace flag");
    *result = {}; result->flFraction = 1; result->vecEndPos = Vector(to);
    currentMaterial = 'F';
    const Surface* first{};
    float firstDistance = to[0];
    for (const auto& surface : surfaces)
    {
        if (from[0] > surface.distance + surface.thickness) continue;
        const float position = std::max(from[0], surface.distance);
        if (position < firstDistance) { first = &surface; firstDistance = position; }
    }
    if (!first) return;
    result->flFraction = (firstDistance - from[0]) / (to[0] - from[0]);
    result->vecEndPos = Vector(firstDistance, from[1] + (to[1] - from[1]) * result->flFraction,
        from[2] + (to[2] - from[2]) * result->flFraction);
    result->pHit = first->target->edict(); result->iHitgroup = first->group;
    result->vecPlaneNormal = Vector(-1, 0, 0);
    result->fStartSolid = from[0] > first->distance;
    currentMaterial = first->material;
}

std::string Read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    Require(bool(file), "actual scenario profile opens");
    return {std::istreambuf_iterator<char>(file), {}};
}

void Run(const char* profile)
{
    auto engine = std::make_unique<Engine>();
    engine->Prepare("0");
    g_pGameRules = InstallGameRules();
    Require(g_pGameRules && !CSOL_RulesRejected(), "actual native rules installed");
    LinkUserMessages();
    auto* playerEdict = engine->Reserve(1, "player");
    auto* player = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&playerEdict->v));
    player->pev->flags = FL_CLIENT | FL_ONGROUND; player->pev->health = 100;
    expectedAttacker = player->pev;
    std::array<Target*, 36> targets{};
    for (auto& target : targets)
    {
        auto* edict = engine->Reserve(engine->nextEntity++, "penetration_target");
        target = GetClassPtr<CCSEntity>(reinterpret_cast<Target*>(&edict->v));
        target->pev->takedamage = DAMAGE_AIM;
        target->traced.reserve(32); target->applied.reserve(32); target->levels.reserve(32);
    }
    engine->files["sound/materials.txt"] = "F materialF\nW materialW\nM materialM\nC materialC\n";
    TEXTURETYPE_Init();
    g_engfuncs.pfnTraceLine = Trace;
    g_engfuncs.pfnTraceTexture = [](edict_t*, const float*, const float*) -> const char*
    {
        ++materialQueries;
        switch (currentMaterial)
        { case 'W': return "materialW"; case 'M': return "materialM"; case 'C': return "materialC"; default: return "materialF"; }
    };
    g_engfuncs.pfnPointContents = [](const float*) { return CONTENTS_EMPTY; };
    gpGlobals->v_right = Vector(0, 1, 0); gpGlobals->v_up = Vector(0, 0, 1);
    g_ReGameHookchains.m_IsPenetrableEntity.registerHook(Penetrable, 192);
    g_ReGameHookchains.m_CBaseEntity_FireBullets3.registerHook(Native, 192);
    const auto reset = [&]
    {
        ++scenarios; surfaces.clear(); traces = materialQueries = nativeCalls = 0; denyPenetration = false;
        for (auto* target : targets)
        {
            target->pev->health = 100000; target->pev->deadflag = DEAD_NO;
            target->pev->solid = SOLID_SLIDEBOX; target->pev->takedamage = DAMAGE_AIM;
            target->traced.clear(); target->applied.clear(); target->levels.clear();
        }
    };
    const auto fire = [&](const ScenarioDamage& damage, CSRifleMaterial material)
    {
        expectedBudget = damage.penetration;
        PistolBallistics shot{{0, 11, 22}, {1, 0, 0}, {}, {0, 1, 0}, {0, 0, 1},
            137, int(damage.shot), false, PistolPlayerHits::OriginalCooperativeSkipPlayers, PistolSource::NormalMuzzle};
        const auto result = FireCSRifleBallistics(*player, shot, {77, 13}, {Current, nullptr}, damage.penetration, material);
        Expect(result.status == BallisticsStatus::Fired && result.spread == Vector(), "actual rifle/native call completes");
        Expect(nativeCalls == 1 && materialQueries == traces, "native material lookup runs for every trace");
    };
    const auto base = Read(profile);
    for (const auto identity : {Firearm::M16, Firearm::G43})
    {
        const auto material = identity == Firearm::M16 ? CSRifleMaterial::Rifle556 : CSRifleMaterial::Rifle762;
        const std::string marker = identity == Firearm::M16 ? "\"shot\": 90" : "\"shot\": 180";
        Require(base.find(marker) != base.npos, "cold rifle profile marker exists");
        for (int configured : {-1, 0, 1, 2, 3, 5, 32})
        {
            std::string json = base;
            if (configured >= 0) json.insert(json.find(marker) + marker.size(), ", \"penetration\": " + std::to_string(configured));
            ScenarioDamageConfig config; std::string error;
            Require(config.Parse(json, error), "cold actual scenario profile accepted");
            const auto* damage = config.Find(identity);
            Require(damage && damage->penetration == std::max(3, configured), "missing/low rifle setting has minimum three");
            reset();
            // A high budget retains native range limits; it does not promise
            // thirty-two impacts after the native remaining range contracts.
            const auto surfaceCount = damage->penetration == 32 ? 3u : unsigned(targets.size());
            for (unsigned i = 0; i < surfaceCount; ++i) surfaces.push_back({targets[i], 100.f + 70.f * i, 1, 'F'});
            fire(*damage, material);
            int expected = int(damage->shot);
            for (unsigned i = 0; i < targets.size(); ++i)
            {
                const bool hit = i < std::min(unsigned(damage->penetration), surfaceCount);
                Expect(targets[i]->traced == (hit ? std::vector<int>{expected} : std::vector<int>{}), "native penetration budget limits consecutive flesh hits");
                Expect(targets[i]->applied == targets[i]->traced && targets[i]->pev->health == 100000 - (hit ? expected : 0), "actual multi-damage subtracts expected health");
                Expect(targets[i]->levels == (hit ? std::vector<int>{int(i)} : std::vector<int>{}), "native target records exact penetration level");
                expected = int(expected * .75f);
            }
        }
        ScenarioDamageConfig config; std::string error;
        Require(config.Parse(base, error), "actual default profile accepted");
        const auto damage = *config.Find(identity);
        reset();
        targets[0]->pev->solid = targets[1]->pev->solid = SOLID_BSP;
        surfaces = {{targets[0], 100, 1, 'W'}, {targets[1], 200, 1, 'W'}, {targets[2], 300, 1, 'F'}};
        fire(damage, material);
        const int afterFirst = int(damage.shot * .6f), afterSecond = int(afterFirst * .6f);
        Expect(targets[0]->applied == std::vector<int>{int(damage.shot)} && targets[1]->applied == std::vector<int>{afterFirst} && targets[2]->applied == std::vector<int>{afterSecond}, "two thin wood walls preserve real material attenuation and hit target");

        reset(); targets[0]->pev->solid = SOLID_BSP;
        surfaces = {{targets[0], 100, 8, 'M'}, {targets[1], 300, 1, 'F'}};
        fire(damage, material);
        Expect(targets[1]->applied.empty(), "metal thickness respects native power instead of guaranteed wall bypass");

        reset(); denyPenetration = true;
        surfaces = {{targets[0], 100, 1, 'F'}, {targets[1], 200, 1, 'F'}};
        fire(damage, material);
        Expect(targets[0]->applied == std::vector<int>{int(damage.shot)} && targets[1]->applied.empty(), "actual deny hook prevents further damage");

        reset(); surfaces = {{targets[0], 100, 1, 'F', HITGROUP_SHIELD}, {targets[1], 200, 1, 'F'}};
        fire(damage, material);
        Expect(targets[0]->applied.empty() && targets[1]->applied.empty() && traces == 1, "actual shield branch stops the shot");

        reset(); const float limit = material == CSRifleMaterial::Rifle556 ? 4000.f : 5000.f;
        surfaces = {{targets[0], limit + 50, 1, 'F'}, {targets[1], limit + 150, 1, 'F'}};
        fire(damage, material);
        Expect(targets[0]->applied == std::vector<int>{int(damage.shot)} && targets[1]->applied.empty() && traces == 1, "native caliber penetration distance stops subsequent targets");
    }
    for (const int walls : {6, 7}) {
        reset();
        for (int i = 0; i < walls; ++i) {
            targets[i]->pev->solid = SOLID_BSP;
            surfaces.push_back({targets[i], 80.f + 50.f * i, 1.f, 'W'});
        }
        surfaces.push_back({targets[walls], 80.f + 50.f * walls, 1.f, 'F'});
        fire({1500, 3, 7}, CSRifleMaterial::Rifle762);
        Expect(targets[walls]->applied.empty() == (walls == 7), "hunting rifle six-wall limit");
        if (walls == 6) Expect(targets[walls]->levels == std::vector<int>{6} &&
            targets[walls]->applied.front() > 0, "target beyond sixth wall takes native material damage");
    }
    g_ReGameHookchains.m_CBaseEntity_FireBullets3.unregisterHook(Native);
    g_ReGameHookchains.m_IsPenetrableEntity.unregisterHook(Penetrable);
    engine.reset();
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    try { Run(argv[1]); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
    std::cout << "scenarios=" << scenarios << " behaviorChecks=" << behaviorChecks << " checks=" << sdk_test::checks << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 3 : 0;
}
