#include "precompiled.h"
#include "map_mining.h"
#include "bridge.h"
#include "interface.h"
#include "goldcraft/host_map_api.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <utility>

namespace {
using namespace goldcraft;
cvar_t mining_cvar = {"mc_map_mining", "1", FCVAR_SERVER, 1, nullptr};
cvar_t persist_cvar = {"mc_map_mining_persist", "0", FCVAR_SERVER, 0, nullptr};
cvar_t *registered_cvar = nullptr;
cvar_t *registered_persist = nullptr;
mining::Policy policy;
edits::Ledger map_edits;
IHostMapPhysics *map_physics = nullptr;
struct MinedEntity {
    int serial;
    CBaseEntity *object;
    string_t model, classname;
    bool matches(edict_t *entity) const {
        return entity && !entity->free && !(entity->v.flags & FL_KILLME) &&
               entity->serialnumber == serial && entity->pvPrivateData == object &&
               entity->v.model == model && entity->v.classname == classname;
    }
};
std::map<int, MinedEntity> mined_entities;
bool round_ended = false, cleanup_persist = false;
unsigned cleanup_depth = 0;

bool Persist() { return registered_persist && registered_persist->value == 1.0f; }
void ClearMapPhysics() {
    if (map_physics)
        map_physics->Reset(map_edits.state().epoch, map_edits.state().revision);
}
#ifdef GOLDCRAFT_HEADLESS_FIXTURE
template <class... Args> void CarvePrint(const char *format, Args... args) {
    char text[2048];
    std::snprintf(text, sizeof(text), format, args...);
    g_engfuncs.pfnServerPrint(text);
}
// The production permission remains unavailable until matching client/Renderer
// and Minecraft collision can commit the same edit. Only this fixture can call
// the engine transaction while those integrations are pending.
void CarveFixture() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS"))
        return;
    try {
        if (!map_physics)
            throw std::runtime_error("Map physics API unavailable");
        if (CMD_ARGC() != 8)
            return;
        char *end = nullptr;
        const auto slot = std::strtoul(CMD_ARGV(1), &end, 10);
        if (!*CMD_ARGV(1) || *end || slot >= static_cast<unsigned>(gpGlobals->maxEntities))
            return;
        auto *entity = INDEXENT(slot);
        edits::Target target{};
        if (slot) {
            if (entity->free || STRING(entity->v.model)[0] != '*')
                return;
            target = {static_cast<unsigned>(slot), static_cast<unsigned>(entity->serialnumber),
                      static_cast<unsigned>(std::strtoul(STRING(entity->v.model) + 1, &end, 10))};
            if (*end)
                return;
        }
        edits::Box box;
        for (int i = 0; i < 6; ++i) {
            const auto value = std::strtof(CMD_ARGV(2 + i), &end);
            if (!*CMD_ARGV(2 + i) || *end)
                return;
            (i < 3 ? box.min : box.max)[i % 3] = value;
        }
        auto candidate = map_edits;
        if (!candidate.add(target, box)) {
            CarvePrint("[GC carve] unchanged=%llu\n", map_edits.state().revision);
            return;
        }
        std::vector<HostMapCut> cuts;
        for (const auto &cut : candidate.state().cuts) {
            HostMapCut copy{cut.target.slot, cut.target.serial, cut.target.model, {}, {}};
            for (int i = 0; i < 3; ++i) {
                copy.min[i] = cut.box.min[i];
                copy.max[i] = cut.box.max[i];
            }
            cuts.push_back(copy);
        }
        if (!map_physics->Replace(candidate.state().epoch, candidate.state().revision, cuts.data(),
                                  static_cast<unsigned>(cuts.size())))
            throw std::runtime_error(map_physics->LastError());
        map_edits.swap(candidate);
        CarvePrint("[GC carve] committed=%llu cuts=%u\n", map_edits.state().revision,
                   static_cast<unsigned>(cuts.size()));
    } catch (const std::exception &failure) {
        CarvePrint("[GC carve] rejected=%s\n", failure.what());
    }
}
void CarveStatus() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS"))
        return;
    if (!map_physics) {
        CarvePrint("[GC carve] API unavailable\n");
        return;
    }
    const auto stats = map_physics->Stats();
    CarvePrint(
        "[GC carve] revision=%llu targets=%u traces=%llu points=%llu failures=%llu error=%s\n",
        stats.revision, stats.targets, stats.traces, stats.points, stats.failures,
        map_physics->LastError());
}
void CarveTrace() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || CMD_ARGC() != 9)
        return;
    const int slot = std::atoi(CMD_ARGV(1)), hull = std::atoi(CMD_ARGV(2));
    if (slot < -1 || slot >= gpGlobals->maxEntities || hull < 0 || hull > 3)
        return;
    Vector start{}, end{};
    for (int i = 0; i < 3; ++i) {
        start[i] = std::strtof(CMD_ARGV(3 + i), nullptr);
        end[i] = std::strtof(CMD_ARGV(6 + i), nullptr);
    }
    if (!std::isfinite(start.Length()) || !std::isfinite(end.Length()))
        return;
    TraceResult trace{};
    if (slot >= 0)
        g_engfuncs.pfnTraceModel(start, end, hull, INDEXENT(slot), &trace);
    else if (hull == 0)
        g_engfuncs.pfnTraceLine(start, end, ignore_monsters, nullptr, &trace);
    else
        g_engfuncs.pfnTraceHull(start, end, ignore_monsters, hull, nullptr, &trace);
    CarvePrint("[GC carve] fraction=%.9f start=%d all=%d open=%d water=%d hit=%d "
               "end=%.6f,%.6f,%.6f normal=%.6f,%.6f,%.6f contents=%d\n",
               trace.flFraction, trace.fStartSolid, trace.fAllSolid, trace.fInOpen, trace.fInWater,
               trace.pHit ? ENTINDEX(trace.pHit) : -1, trace.vecEndPos.x, trace.vecEndPos.y,
               trace.vecEndPos.z, trace.vecPlaneNormal.x, trace.vecPlaneNormal.y,
               trace.vecPlaneNormal.z, g_engfuncs.pfnPointContents(end));
}
void CarveMove() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || CMD_ARGC() != 9)
        return;
    const int slot = std::atoi(CMD_ARGV(1));
    if (slot < 1 || slot > gpGlobals->maxClients)
        return;
    auto *entity = INDEXENT(slot);
    if (!(entity->v.flags & FL_FAKECLIENT) || entity->free)
        return;
    Vector origin{}, angles{0, std::strtof(CMD_ARGV(5), nullptr), 0};
    for (int i = 0; i < 3; ++i)
        origin[i] = std::strtof(CMD_ARGV(2 + i), nullptr);
    const auto speed = std::strtof(CMD_ARGV(6), nullptr);
    const auto frames = std::atoi(CMD_ARGV(7));
    const bool duck = std::atoi(CMD_ARGV(8)) == 1;
    if (frames < 1 || frames > 100 || !std::isfinite(origin.Length()) ||
        !std::isfinite(angles.Length()) || !std::isfinite(speed) || std::abs(speed) > 1000)
        return;
    SET_ORIGIN(entity, origin);
    // Isolate an actual PM sweep at this height, with a known incoming velocity.
    // The runner disables gravity. Air acceleration from rest is capped at 30
    // and would otherwise never reach the test wall within this command.
    Vector forward;
    AngleVectors(angles, forward, nullptr, nullptr);
    entity->v.velocity = forward * speed;
    entity->v.basevelocity = g_vecZero;
    entity->v.flags &= ~(FL_ONGROUND | FL_FROZEN);
    entity->v.movetype = MOVETYPE_WALK;
    entity->v.flags = duck ? (entity->v.flags | FL_DUCKING) : (entity->v.flags & ~FL_DUCKING);
    SET_SIZE(entity, Vector(-16, -16, duck ? -18 : -36), Vector(16, 16, duck ? 18 : 36));
    entity->v.maxspeed = 400;
    for (int i = 0; i < frames; ++i)
        g_engfuncs.pfnRunPlayerMove(entity, angles, speed, 0, 0, duck ? IN_DUCK : 0, 0, 20);
    CarvePrint("[GC carve] move=%.6f,%.6f,%.6f flags=%u\n", entity->v.origin.x, entity->v.origin.y,
               entity->v.origin.z, entity->v.flags);
}
// Only the isolated fixture DLL can populate the pending edit ledger. This
// deliberately changes no collision/rendering and is not a mining success path.
void EditFixture() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || CMD_ARGC() != 2)
        return;
    try {
        char *end = nullptr;
        const auto count = std::strtoul(CMD_ARGV(1), &end, 10);
        if (!*CMD_ARGV(1) || *end || count > 1024)
            return;
        for (unsigned i = 0; i < count; ++i) {
            const float x = static_cast<float>(i * 2);
            map_edits.add({}, {{x, 0, 0}, {x + 1, 1, 1}});
        }
    } catch (const std::exception &error) {
        ALERT(at_console, "Map edit fixture: %s\n", error.what());
    }
}
#endif
void InvalidateRequests() {
    if (!policy.epoch)
        return;
    if (policy.revision == std::numeric_limits<std::uint64_t>::max())
        throw ProtocolError("Mining policy revision exhausted");
    ++policy.revision;
}
void RestoreMinedEntities() {
    map_edits.restore();
    ClearMapPhysics();
    // Drop records before invoking virtual callbacks: plugins can remove/reuse
    // edicts or reenter the round rules. Never copy private entity memory back.
    auto previous = std::exchange(mined_entities, {});
    for (const auto &[slot, identity] : previous) {
        if (slot >= gpGlobals->maxEntities)
            continue;
        auto *entity = INDEXENT(slot);
        if (identity.matches(entity))
            identity.object->Restart();
    }
}

mining::Target TargetKind(edict_t *entity) {
    if (!entity || entity->free ||
        (entity->v.flags & (FL_KILLME | FL_CLIENT | FL_FAKECLIENT | FL_MONSTER)))
        return mining::Target::none;
    if (ENTINDEX(entity) == 0)
        return mining::Target::geometry;
    const char *model = STRING(entity->v.model);
    if (!model || model[0] != '*' || entity->v.solid == SOLID_NOT ||
        entity->v.solid == SOLID_TRIGGER)
        return mining::Target::none;
    auto *object = GET_PRIVATE<CBaseEntity>(entity);
    if (!object || object->IsPlayer())
        return mining::Target::none;
    if (entity->v.takedamage != DAMAGE_NO && entity->v.health > 0 &&
        std::isfinite(entity->v.health))
        return mining::Target::damageable_entity;
    return mining::Target::geometry;
}
} // namespace

void GoldCraft_MapMiningInit() {
    // Register before game_init.cfg/server.cfg and AMXX plugin_init, including servers
    // without a configured bridge. The cvar persists over changelevel.
    if (!registered_cvar) {
        CVAR_REGISTER(&mining_cvar);
        registered_cvar = CVAR_GET_POINTER("mc_map_mining");
    }
    if (!registered_persist) {
        CVAR_REGISTER(&persist_cvar);
        registered_persist = CVAR_GET_POINTER("mc_map_mining_persist");
#ifdef GOLDCRAFT_HEADLESS_FIXTURE
        ADD_SERVER_COMMAND("gc_edits_fixture", EditFixture);
        ADD_SERVER_COMMAND("gc_carve_fixture", CarveFixture);
        ADD_SERVER_COMMAND("gc_carve_status", CarveStatus);
        ADD_SERVER_COMMAND("gc_carve_trace", CarveTrace);
        ADD_SERVER_COMMAND("gc_carve_move", CarveMove);
#endif
    }
}

void GoldCraft_MapMiningReset(std::uint64_t epoch) {
    GoldCraft_MapMiningInit();
    mined_entities.clear();
    round_ended = cleanup_persist = false;
    cleanup_depth = 0;
    map_edits.reset(epoch);
    policy = {epoch, epoch ? 1u : 0u,
              mining::cvar_mode(registered_cvar ? registered_cvar->value : 0),
              mining::entity_damage};
    const auto factory = Sys_GetFactory("swds.dll");
    map_physics =
        factory ? static_cast<IHostMapPhysics *>(factory(host_map_api_version, nullptr)) : nullptr;
    ClearMapPhysics();
}

void GoldCraft_MapMiningFrame() {
    if (!policy.epoch || !g_pGameRules || !g_pGameRules->IsMultiplayer())
        return;
    // ReAPI rg_round_end can set these fields directly using its own inline
    // TerminateRound. A post hook can also run after cancellation. Observe the
    // committed rules state instead of interpreting a hook notification as an end.
    const bool ended = CSGameRules()->m_bRoundTerminating;
    if (ended && !round_ended) {
        round_ended = true;
        InvalidateRequests();
        if (!Persist())
            RestoreMinedEntities();
    } else if (!ended)
        round_ended = false;
}

GoldCraft_MapMiningRoundCleanup::GoldCraft_MapMiningRoundCleanup() {
    if (cleanup_depth++ != 0)
        return;
    cleanup_persist = Persist();
    // A plugin may end the new round immediately, before another server frame
    // observes m_bRoundTerminating=false. Rearm the boundary at real cleanup.
    round_ended = false;
    // Handles direct rg_restart_round / sv_restart and no observed end frame.
    // Native cleanup performs the reset once when preservation is disabled.
    InvalidateRequests();
    if (!cleanup_persist) {
        mined_entities.clear();
        map_edits.restore();
        ClearMapPhysics();
    }
}
GoldCraft_MapMiningRoundCleanup::~GoldCraft_MapMiningRoundCleanup() {
    if (--cleanup_depth == 0)
        cleanup_persist = false;
}
bool GoldCraft_MapMiningKeepEntity(edict_t *entity) {
    if (!cleanup_depth || !cleanup_persist || !entity)
        return false;
    const auto found = mined_entities.find(ENTINDEX(entity));
    if (found == mined_entities.end())
        return false;
    if (found->second.matches(entity))
        return true;
    mined_entities.erase(found); // A reused slot/model must follow its own lifecycle.
    return false;
}

goldcraft::mining::Policy GoldCraft_MapMiningPolicy() {
    const auto current = mining::cvar_mode(registered_cvar ? registered_cvar->value : 0);
    if (policy.epoch && current != policy.mode) {
        InvalidateRequests();
        policy.mode = current;
    }
    return policy;
}
const goldcraft::edits::Ledger &GoldCraft_MapEdits() { return map_edits; }

goldcraft::mining::Result GoldCraft_MapMiningApply(const goldcraft::mining::Request &q,
                                                   edict_t *player) {
    using namespace goldcraft::mining;
    const auto current = GoldCraft_MapMiningPolicy();
    Result result{current.epoch, q.event, current.revision, q.slot,
                  q.serial,      q.life,  q.target,         q.target_serial};
    if (q.epoch != current.epoch || q.revision != current.revision) {
        result.status = Status::stale_policy;
        return result;
    }
    if (current.mode == Mode::disabled) {
        result.status = Status::disabled;
        return result;
    }
    auto *actor = player && !player->free ? GET_PRIVATE<CBasePlayer>(player) : nullptr;
    if (!actor || !actor->IsAlive() || (player->v.flags & FL_SPECTATOR) ||
        (g_pGameRules && g_pGameRules->IsFreezePeriod()))
        return result;
    if (g_pGameRules && g_pGameRules->IsMultiplayer() && CSGameRules()->m_bRoundTerminating)
        return result;
    result.status = Status::invalid_target;
    if (q.target >= static_cast<unsigned>(gpGlobals->maxEntities))
        return result;
    auto *target = INDEXENT(q.target);
    const auto kind = TargetKind(target);
    if (!permits(current.mode, kind) ||
        (q.target && target->serialnumber != static_cast<int>(q.target_serial)))
        return result;
    if (q.target) {
        char *end = nullptr;
        const auto model = std::strtoul(STRING(target->v.model) + 1, &end, 10);
        if (!end || *end || model != q.model)
            return result;
    }
    const Vector source = player->v.origin + player->v.view_ofs;
    const Vector point(q.point.x, q.point.y, q.point.z);
    const Vector delta = point - source;
    const float distance = delta.Length();
    result.status = Status::obstructed;
    if (!std::isfinite(distance) || distance < 0.01f || distance > q.reach + 1.0f)
        return result;
    const Vector direction = delta / distance;
    Vector forward;
    AngleVectors(player->v.v_angle, forward, nullptr, nullptr);
    if (DotProduct(direction, forward) < 0.95f)
        return result;
    TraceResult trace{};
    // Include players/monsters and MC proxy edicts as occluders. Do not trace
    // straight through an intervening object merely because the requested BSP matches.
    UTIL_TraceLine(source, point + direction, dont_ignore_monsters, player, &trace);
    if (trace.fStartSolid || trace.fAllSolid || trace.flFraction >= 1 || trace.pHit != target)
        return result;
    const auto commit_policy = GoldCraft_MapMiningPolicy();
    result.revision = commit_policy.revision;
    if (commit_policy != current) {
        result.status = Status::stale_policy;
        return result;
    }
    if (TargetKind(target) != kind ||
        (q.target && target->serialnumber != static_cast<int>(q.target_serial))) {
        result.status = Status::invalid_target;
        return result;
    }
    if (kind == Target::geometry) {
        result.status = Status::geometry_unavailable;
        return result; // Do not acknowledge a hole until rendering AND collision can commit it.
    }
    auto *object = GET_PRIVATE<CBaseEntity>(target);
    const auto serial = target->serialnumber;
    result.before = result.after = target->v.health;
    const auto solid = target->v.solid;
    const MinedEntity identity{serial, object, target->v.model, target->v.classname};
    const bool accepted = object->TakeDamage(&player->v, &player->v, q.damage, DMG_CLUB) != FALSE;
    // Breakable death returns FALSE. Ham callbacks may remove/reuse the edict;
    // compare its incarnation before inspecting the surviving object.
    const bool removed = target->free || target->serialnumber != serial ||
                         target->pvPrivateData != object || (target->v.flags & FL_KILLME);
    if (removed)
        result.after = 0;
    else if (std::isfinite(target->v.health))
        result.after = target->v.health;
    result.status = accepted || removed || result.after < result.before || target->v.solid != solid
                        ? Status::applied
                        : Status::no_effect;
    if (result.status == Status::applied && identity.matches(target) &&
        (result.after < result.before || target->v.solid != solid))
        mined_entities.insert_or_assign(q.target, identity);
    return result;
}
