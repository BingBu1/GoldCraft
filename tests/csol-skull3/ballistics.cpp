// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the real SDK/pooled-monster fixture and separate ReHLDS hull bridge.
// Its existing main is not used; the cases below call the new production TU.
#define main Skull3UnusedShieldMain
#include "../csol-monster-shield/server.cpp"
#undef main
#include "../../native/csol/server/skull3_ballistics.cpp"
#include <bit>

namespace skull3_ballistics_test
{
using sdk_test::Engine;
using sdk_test::Require;
unsigned checks{}, failures{};
std::function<void()> afterDamage;
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "ASSERT " << label << '\n'; }
}
float Float(unsigned value) { return std::bit_cast<float>(value); }
bool Equal(float value, unsigned bits) { return std::bit_cast<unsigned>(value) == bits; }

struct Target final : CBaseEntity
{
    std::vector<int> traced, applied;
    entvars_t* attacker{};
    void TraceAttack(entvars_t* owner, float damage, Vector direction, TraceResult* trace, int type) override
    {
        Check(owner == attacker && type == (DMG_BULLET | DMG_NEVERGIB), "real TraceAttack attacker/type");
        Check(direction == Vector(1, 0, 0), "real trace direction");
        traced.push_back(int(damage));
        CBaseEntity::TraceAttack(owner, damage, direction, trace, type);
    }
    BOOL TakeDamage(entvars_t* inflictor, entvars_t* owner, float damage, int type) override
    {
        Check(inflictor == attacker && owner == attacker, "ApplyMultiDamage retains player attribution");
        applied.push_back(int(damage));
        const auto result = CBaseEntity::TakeDamage(inflictor, owner, damage, type);
        if (afterDamage) afterDamage();
        return result;
    }
};

CBasePlayer& Player(Engine& e, int index)
{
    auto* ed = e.Reserve(index, "player");
    auto* player = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
    ed->v.flags = FL_CLIENT | FL_ONGROUND;
    ed->v.health = 1000;
    ed->v.deadflag = DEAD_NO;
    ed->v.takedamage = DAMAGE_AIM;
    player->m_iTeam = index == 1 ? CT : TERRORIST;
    player->m_iJoiningState = JOINED;
    player->m_iNumSpawns = 1;
    player->m_bJustConnected = false;
    player->has_disconnected = false;
    return *player;
}

bool DenyPenetration(IReGameHook_IsPenetrableEntity*, Vector&, Vector&, entvars_t*, edict_t*) { return false; }

void Oracle(const char* path)
{
    auto e = std::make_unique<Engine>();
    e->Prepare("0");
    g_pGameRules = InstallGameRules();
    Require(g_pGameRules && !CSOL_RulesRejected(), "real native game rules installed");
    LinkUserMessages();
    gpGlobals->v_right = Vector(0, 1, 0);
    gpGlobals->v_up = Vector(0, 0, 1);
    auto& player = Player(*e, 1);
    auto* ed = e->Reserve(e->nextEntity++, "bullet_target");
    auto* target = GetClassPtr<CCSEntity>(reinterpret_cast<Target*>(&ed->v));
    target->pev->takedamage = DAMAGE_AIM;
    target->attacker = player.pev;
    e->files["sound/materials.txt"] = "M matM\nC matC\nG matG\nV matV\nT matT\nP matP\nW matW\nF matF\nZ matZ\n";
    TEXTURETYPE_Init();
    e->onTrace = [](edict_t*, TraceResult&) {};
    static char texture[] = "matF";
    g_engfuncs.pfnTraceTexture = [](edict_t*, const float*, const float*) -> const char* { return texture; };
    g_engfuncs.pfnPointContents = [](const float*) { return CONTENTS_EMPTY; };

    std::ifstream input(path);
    Require(bool(input), "original MP oracle opens");
    unsigned count{}, seed{}, xb{}, yb{};
    input >> count;
    for (unsigned i = 0; i < count; ++i)
    {
        input >> seed >> xb >> yb;
        for (float spread : {0.f, .025f, .5625f, 1.2f, -1.2f, 2.f, -2.f})
        {
            const float accepted = std::abs(spread) > 1.2f ? 1.2f : spread;
            const auto result = CSOL_Fire45ACPAZ(player, Vector(10, 11, 22), Vector(1, 0, 0), spread, 8192, 1, 74, .93f, seed);
            Check(result == Vector(Float(xb) * accepted, Float(yb) * accepted, 0), "actual SDK shared spread equals original instructions");
        }
    }

    struct Hit { unsigned fraction{}, solid{}, group{}, material{}, source{}, end{}, position{}; int damage{}; };
    input >> count;
    for (unsigned caseIndex = 0; caseIndex < count; ++caseIndex)
    {
        unsigned penetration{}, range{}, hitCount{}, miss{}, lastSource{}, lastEnd{};
        int damage{};
        input >> penetration >> damage >> range >> hitCount >> miss >> lastSource >> lastEnd;
        std::vector<Hit> hits(hitCount);
        for (auto& hit : hits) input >> hit.fraction >> hit.solid >> hit.group >> hit.material >> hit.source >> hit.end >> hit.position >> hit.damage;
        Require(bool(input), "complete original ballistic oracle record");
        unsigned calls{};
        target->traced.clear(); target->applied.clear(); target->pev->health = 100000;
        g_engfuncs.pfnTraceLine = nullptr;
        // Engine callback dispatch uses one owned function so lambdas never
        // replace the production trace/damage implementation under test.
        static std::function<void(const float*, const float*, int, edict_t*, TraceResult*)> trace;
        trace = [&](const float* from, const float* to, int flags, edict_t* skip, TraceResult* out)
        {
            Check(flags == 0 && skip == player.edict() && gpGlobals->trace_flags == FTRACE_BULLET, "native engine trace contract");
            *out = {};
            const unsigned index = calls++;
            if (index >= hits.size())
            {
                Check(index == hits.size() && miss, "only expected final miss is traced");
                Check(Equal(from[0], lastSource) && Equal(to[0], lastEnd), "last trace preserves original source and range");
                out->flFraction = 1.f; out->vecEndPos = Vector(to); return;
            }
            const auto& hit = hits[index];
            Check(Equal(from[0], hit.source) && Equal(to[0], hit.end), "original material advance and remaining range");
            Check(from[1] == 11 && from[2] == 22 && to[1] == 11 && to[2] == 22, "off-axis trace origin retained");
            target->pev->solid = int(hit.solid);
            texture[3] = char(hit.material);
            out->flFraction = Float(hit.fraction); out->iHitgroup = int(hit.group);
            out->vecEndPos = Vector(Float(hit.position), 11, 22); out->pHit = target->edict();
        };
        g_engfuncs.pfnTraceLine = [](const float* a, const float* b, int flags, edict_t* skip, TraceResult* out) { trace(a, b, flags, skip, out); };
        const auto eventStart = e->events.size();
        CSOL_Fire45ACPAZ(player, Vector(10, 11, 22), Vector(1, 0, 0), 0, 8192, int(penetration), damage, Float(range), 13);
        Check(calls == hits.size() + miss, "original penetration count controls real loop");
        Check(target->traced.size() == hits.size() && target->applied.size() == hits.size(), "one TraceAttack and ApplyMultiDamage per hit");
        int total{};
        for (unsigned i = 0; i < hits.size(); ++i)
        {
            Check(i < target->traced.size() && target->traced[i] == hits[i].damage, "original integer hit damage");
            Check(i < target->applied.size() && target->applied[i] == hits[i].damage, "actual ApplyMultiDamage amount");
            total += hits[i].damage;
        }
        Check(target->pev->health == 100000 - total, "native TakeDamage changes health by original total");
        Check(e->events.size() == eventStart, "ballistic loop does not invent decals or sparks");
        trace = {};
    }
    // An external host blocker may deny continuation, but never the current hit.
    unsigned calls{};
    e->onTrace = [&](edict_t*, TraceResult& out) { ++calls; out.flFraction = .01f; out.vecEndPos = Vector(91.92f, 11, 22); out.pHit = target->edict(); };
    g_engfuncs.pfnTraceLine = [](const float*, const float*, int, edict_t* skip, TraceResult* out) { *out = {}; Engine::self->onTrace(skip, *out); };
    target->traced.clear(); target->applied.clear(); target->pev->solid = SOLID_BSP; target->pev->health = 100000; texture[3] = 'F';
    g_ReGameHookchains.m_IsPenetrableEntity.registerHook(DenyPenetration, 192);
    CSOL_Fire45ACPAZ(player, Vector(10, 11, 22), Vector(1, 0, 0), 0, 8192, 5, 74, .93f, 13);
    g_ReGameHookchains.m_IsPenetrableEntity.unregisterHook(DenyPenetration);
    Check(calls == 1 && target->traced.size() == 1 && target->applied.size() == 1 && target->pev->health < 100000, "host penetration denial preserves current authoritative damage");
    texture[3] = 'Z'; calls = 0; target->traced.clear(); target->applied.clear();
    CSOL_Fire45ACPAZ(player, Vector(10, 11, 22), Vector(1, 0, 0), 0, 8192, 5, 74, .93f, 13);
    Check(calls == 1 && target->traced.empty() && target->applied.empty(), "original Z material terminates before damage");
    auto& shield = Player(*e, 2);
    shield.m_bOwnsShield = true;
    texture[3] = 'F'; calls = 0;
    e->onTrace = [&](edict_t*, TraceResult& out)
    {
        ++calls; out.flFraction = .01f; out.iHitgroup = HITGROUP_SHIELD;
        out.vecEndPos = Vector(91.92f, 11, 22); out.pHit = shield.edict();
    };
    const auto eventStart = e->events.size();
    CSOL_Fire45ACPAZ(player, Vector(10, 11, 22), Vector(1, 0, 0), 0, 8192, 5, 74, .93f, 13);
    bool ricochet{};
    for (std::size_t i = eventStart; i < e->events.size(); ++i)
        ricochet |= e->events[i].find("weapons/ric_metal-") != std::string::npos;
    Check(calls == 1 && shield.pev->health == 1000 && ricochet, "native player shield still blocks with its own authoritative consumer");
    ClearMultiDamage();
    e.reset();
}

void Monsters(const std::filesystem::path& data)
{
    Fixture f(data);
    auto& player = f.Player(1, true);
    player.m_iJoiningState = JOINED;
    g_engfuncs.pfnCmd_Argc = [] { return 2; };
    g_engfuncs.pfnCmd_Argv = [](int i) -> const char* { return i == 1 ? "1" : "csol_character_select"; };
    g_engfuncs.pfnInfoKeyValue = [](char*, const char* key) -> char* { return const_cast<char*>(std::strcmp(key, "csol_protocol") == 0 ? "1" : ""); };
    CSOL_PrecacheCharacters();
    Require(CSOL_CharacterCommand(player.edict(), "csol_character_select"), "actual scenario character selected");
    Require(f.runtime->players->Prepare(player), "actual player damage and upgrades prepared");
    // Keep the shooter outside the factory's real spawn occupancy check.
    // Shot source below is a controlled trace input, independent of spawn.
    player.pev->origin = Vector(-1024, -1024, 0);
    player.pev->absmin = Vector(-1040, -1040, -36);
    player.pev->absmax = Vector(-1008, -1008, 36);
    // SKULL-3 weapon wrappers are being integrated separately. A known native
    // weapon source lets this case isolate the new bullet and target consumers.
    auto* ed = Engine::Create(Engine::String("weapon_skull1"));
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));
    Require(weapon != nullptr, "real existing weapon attribution fixture");
    weapon->m_iId = 130; weapon->m_pPlayer = &player; player.m_pActiveItem = weapon;
    gpGlobals->v_right = Vector(0, 1, 0); gpGlobals->v_up = Vector(0, 0, 1);
    for (int type = 1; type <= int(OrdinaryNames.size()); ++type)
    {
        auto& monster = f.Spawn(type);
        for (float height : {40.f, 64.f})
        {
            f.packets.clear(); f.traces = 0;
            g_engfuncs.pfnTraceLine = [](const float* begin, const float* end, int, edict_t*, TraceResult* out)
            {
                auto& current = *Fixture::current;
                auto& model = current.Find(STRING(current.target->pev->model));
                ShieldTraceResult result{};
                Require(current.traceFunction(model.raw.data(), current.target->pev->gamestate, 0, 0, begin, end, &result) == 1, "actual ReHLDS hull computes bullet28 impact");
                ++current.traces; current.last = result; *out = {};
                out->flFraction = result.fraction; out->iHitgroup = result.group;
                out->fStartSolid = result.startsolid; out->fAllSolid = result.allsolid;
                out->vecEndPos = Vector(result.end); out->vecPlaneNormal = Vector(result.normal);
                out->pHit = result.hit ? current.target->edict() : Engine::At(0);
            };
            const auto before = monster.pev->health;
            CSOL_Fire45ACPAZ(player, Vector(-100, 0, height), Vector(1, 0, 0), 0, 200, 1, 10, 1, 13);
            Check(f.traces == 1 && f.last.group >= 1 && f.last.group <= 7, "bullet28 reaches real flesh hull");
            Check(monster.pev->health < before, "actual ordinary monster loses health from bullet28");
            Check(f.Blood() == 1 && f.Sparks() == 0 && monster.Behavior()->Failure().empty(), "actual monster emits blood without shield spark or damage-source rejection");
            std::cout << "MONSTER " << OrdinaryNames[type - 1] << " height=" << height << " hp=" << before << "->" << monster.pev->health
                << " takedamage=" << monster.pev->takedamage << " group=" << f.last.group << " blood=" << f.Blood()
                << " sparks=" << f.Sparks() << " error=" << monster.Behavior()->Failure() << " rejected=" << CSOL_RulesRejected() << '\n';
        }
    }
    ClearMultiDamage();
}
void Lifecycle(bool release)
{
    auto e = std::make_unique<Engine>(); e->Prepare("0");
    g_pGameRules = InstallGameRules(); LinkUserMessages();
    auto* rules = g_pGameRules;
    auto& player = Player(*e, 1);
    const EHANDLE playerHandle(&player);
    auto* ed = e->Reserve(e->nextEntity++, "bullet_lifecycle_target");
    auto* target = GetClassPtr<CCSEntity>(reinterpret_cast<Target*>(&ed->v));
    target->pev->takedamage = DAMAGE_AIM; target->pev->health = 100000; target->attacker = player.pev;
    gpGlobals->v_right = Vector(0, 1, 0); gpGlobals->v_up = Vector(0, 0, 1);
    g_engfuncs.pfnTraceTexture = [](edict_t*, const float*, const float*) -> const char* { return nullptr; };
    unsigned traces{}, callbacks{};
    e->onTrace = [&](edict_t*, TraceResult& out) {
        ++traces; out.flFraction = .1f; out.vecEndPos = Vector(10, 11, 22); out.pHit = target->edict();
    };
    afterDamage = [&] {
        if (callbacks++ != 0) return;
        if (release) Engine::Remove(playerHandle.Get());
        else g_pGameRules = nullptr;
    };
    CSOL_Fire45ACPAZ(player, Vector(0, 11, 22), Vector(1, 0, 0), 0, 8192, 3, 74, 1, 13);
    afterDamage = {};
    Check(traces == 1 && target->applied.size() == 1, "actual damage callback retires the old ballistic operation before a second trace");
    if (release) Check(!playerHandle.Get(), "actual attacker private data and edict lifetime were released");
    g_pGameRules = rules; ClearMultiDamage(); e.reset();
}
}

int main(int argc, char** argv)
{
    using namespace skull3_ballistics_test;
    try
    {
        Require(argc == 3, "usage: group oracle-or-models");
        if (std::strcmp(argv[1], "oracle") == 0) Oracle(argv[2]);
        else if (std::strcmp(argv[1], "monsters") == 0) Monsters(argv[2]);
        else if (std::strcmp(argv[1], "lifecycle-reset") == 0) Lifecycle(false);
        else if (std::strcmp(argv[1], "lifecycle-release") == 0) Lifecycle(true);
        else throw std::runtime_error("unknown test group");
    }
    catch (const std::exception& error) { ++skull3_ballistics_test::failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "skull3_ballistics checks=" << skull3_ballistics_test::checks << " failures=" << skull3_ballistics_test::failures << " SDK=" << sdk_test::checks << " SDKfail=" << sdk_test::failures << '\n';
    return skull3_ballistics_test::failures || sdk_test::failures ? 1 : 0;
}
