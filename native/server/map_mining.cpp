#include "precompiled.h"
#include "map_mining.h"
#include <cstdlib>
#include <limits>

namespace {
using namespace goldcraft;
cvar_t mining_cvar = {"mc_map_mining", "1", FCVAR_SERVER, 1, nullptr};
cvar_t* registered_cvar = nullptr;
mining::Policy policy;

mining::Target TargetKind(edict_t* entity) {
    if (!entity || entity->free || (entity->v.flags & (FL_KILLME | FL_CLIENT | FL_FAKECLIENT | FL_MONSTER)))
        return mining::Target::none;
    if (ENTINDEX(entity) == 0) return mining::Target::geometry;
    const char* model = STRING(entity->v.model);
    if (!model || model[0] != '*' || entity->v.solid == SOLID_NOT || entity->v.solid == SOLID_TRIGGER)
        return mining::Target::none;
    auto* object = GET_PRIVATE<CBaseEntity>(entity);
    if (!object || object->IsPlayer()) return mining::Target::none;
    if (entity->v.takedamage != DAMAGE_NO && entity->v.health > 0 && std::isfinite(entity->v.health))
        return mining::Target::damageable_entity;
    return mining::Target::geometry;
}
}

void GoldCraft_MapMiningInit() {
    // Register before game_init.cfg/server.cfg and AMXX plugin_init, including servers
    // without a configured bridge. The cvar persists over changelevel.
    if (!registered_cvar) {
        CVAR_REGISTER(&mining_cvar);
        registered_cvar = CVAR_GET_POINTER("mc_map_mining");
    }
}

void GoldCraft_MapMiningReset(std::uint64_t epoch) {
    GoldCraft_MapMiningInit();
    policy = {epoch, epoch ? 1u : 0u, mining::cvar_mode(registered_cvar ? registered_cvar->value : 0), mining::entity_damage};
}

goldcraft::mining::Policy GoldCraft_MapMiningPolicy() {
    const auto current = mining::cvar_mode(registered_cvar ? registered_cvar->value : 0);
    if (policy.epoch && current != policy.mode) {
        if (policy.revision == std::numeric_limits<std::uint64_t>::max()) throw ProtocolError("Mining policy revision exhausted");
        ++policy.revision; policy.mode = current;
    }
    return policy;
}

goldcraft::mining::Result GoldCraft_MapMiningApply(const goldcraft::mining::Request& q, edict_t* player) {
    using namespace goldcraft::mining;
    const auto current = GoldCraft_MapMiningPolicy();
    Result result{current.epoch, q.event, current.revision, q.slot, q.serial, q.life, q.target, q.target_serial};
    if (q.epoch != current.epoch || q.revision != current.revision) { result.status = Status::stale_policy; return result; }
    if (current.mode == Mode::disabled) { result.status = Status::disabled; return result; }
    auto* actor = player && !player->free ? GET_PRIVATE<CBasePlayer>(player) : nullptr;
    if (!actor || !actor->IsAlive() || (player->v.flags & FL_SPECTATOR) || (g_pGameRules && g_pGameRules->IsFreezePeriod())) return result;
    result.status = Status::invalid_target;
    if (q.target >= static_cast<unsigned>(gpGlobals->maxEntities)) return result;
    auto* target = INDEXENT(q.target);
    const auto kind = TargetKind(target);
    if (!permits(current.mode, kind) || (q.target && target->serialnumber != static_cast<int>(q.target_serial))) return result;
    if (q.target) {
        char* end = nullptr;
        const auto model = std::strtoul(STRING(target->v.model) + 1, &end, 10);
        if (!end || *end || model != q.model) return result;
    }
    const Vector source = player->v.origin + player->v.view_ofs;
    const Vector point(q.point.x, q.point.y, q.point.z);
    const Vector delta = point - source;
    const float distance = delta.Length();
    result.status = Status::obstructed;
    if (!std::isfinite(distance) || distance < 0.01f || distance > q.reach + 1.0f) return result;
    const Vector direction = delta / distance;
    Vector forward; AngleVectors(player->v.v_angle, forward, nullptr, nullptr);
    if (DotProduct(direction, forward) < 0.95f) return result;
    TraceResult trace{};
    // Include players/monsters and MC proxy edicts as occluders. Do not trace
    // straight through an intervening object merely because the requested BSP matches.
    UTIL_TraceLine(source, point + direction, dont_ignore_monsters, player, &trace);
    if (trace.fStartSolid || trace.fAllSolid || trace.flFraction >= 1 || trace.pHit != target) return result;
    const auto commit_policy = GoldCraft_MapMiningPolicy();
    result.revision = commit_policy.revision;
    if (commit_policy != current) { result.status = Status::stale_policy; return result; }
    if (TargetKind(target) != kind || (q.target && target->serialnumber != static_cast<int>(q.target_serial))) {
        result.status = Status::invalid_target; return result;
    }
    if (kind == Target::geometry) {
        result.status = Status::geometry_unavailable;
        return result; // Do not acknowledge a hole until rendering AND collision can commit it.
    }
    auto* object = GET_PRIVATE<CBaseEntity>(target);
    const auto serial = target->serialnumber;
    result.before = result.after = target->v.health;
    const auto solid = target->v.solid;
    const bool accepted = object->TakeDamage(&player->v, &player->v, q.damage, DMG_CLUB) != FALSE;
    // Breakable death returns FALSE. Ham callbacks may remove/reuse the edict;
    // compare its incarnation before inspecting the surviving object.
    const bool removed = target->free || target->serialnumber != serial || target->pvPrivateData != object || (target->v.flags & FL_KILLME);
    if (removed) result.after = 0;
    else if (std::isfinite(target->v.health)) result.after = target->v.health;
    result.status = accepted || removed || result.after < result.before || target->v.solid != solid ? Status::applied : Status::no_effect;
    return result;
}
