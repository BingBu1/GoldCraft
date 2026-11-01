// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "elite_visuals.hpp"
#include "../scenario_session.h"
#include "../../character_skill_protocol.hpp"
#include "../../elite_visual_resources.hpp"
#include <array>
#include <bit>
#include <cmath>

#pragma clang fp contract(off)

namespace csol::scenario::skills
{
namespace
{
struct Resources
{
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    int trail{}, emitter{}, world{}, explosion{}, frost{}, pull{};
};
std::shared_ptr<Resources> resources;
bool sending{}, preparing{};
struct Sending { Sending() { sending = true; } ~Sending() { sending = false; } };
struct Preparing { Preparing() { preparing = true; } ~Preparing() { preparing = false; } };
bool Fail(ConfigError& error, const char* message) { error.message = message; return false; }
bool Current(const std::shared_ptr<Resources>& state, const CCSOLScenarioSession& session)
{
    return state && resources == state && state->session.lock().get() == &session &&
        state->access && state->access->Allowed() && session.Map().EntityAccess() == state->access;
}
bool Transport()
{
    return g_engfuncs.pfnMessageBegin && g_engfuncs.pfnMessageEnd &&
        g_engfuncs.pfnWriteByte && g_engfuncs.pfnWriteShort && g_engfuncs.pfnWriteLong &&
        g_engfuncs.pfnWriteCoord && g_engfuncs.pfnWriteString;
}
void Float(float value) { WRITE_LONG(std::bit_cast<std::int32_t>(value)); }
bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
}

bool PrecacheEliteVisuals(const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    if (!session || !session->Map().EntityAccessAllowed() || sending || preparing) return false;
    const Preparing guard;
    resources.reset();
    if (!g_engfuncs.pfnRegUserMsg || !g_engfuncs.pfnGetFileSize ||
        !g_engfuncs.pfnPrecacheModel || !g_engfuncs.pfnPrecacheSound)
        return Fail(error, "elite visual precache API is unavailable");
    auto next = std::make_shared<Resources>();
    next->session = session; next->access = session->Map().EntityAccess();
    const auto current = [&] { return !resources && next->access->Allowed() &&
        session->Map().EntityAccess() == next->access; };
    for (const auto* path : elitefx::Models)
    {
        const int size = g_engfuncs.pfnGetFileSize(path);
        if (!current()) return false;
        if (size <= 0) { error.message = std::string("missing elite visual resource: ") + path; return false; }
    }
    for (const auto* path : elitefx::Sounds)
    {
        const int size = g_engfuncs.pfnGetFileSize(path);
        if (!current()) return false;
        if (size <= 0) { error.message = std::string("missing elite sound: ") + path; return false; }
        PRECACHE_SOUND(const_cast<char*>(path + 6));
        if (!current()) return false;
    }
    next->trail = REG_USER_MSG("ZombieBoss", -1); if (!current()) return false;
    next->emitter = REG_USER_MSG("TempEmit", -1); if (!current()) return false;
    next->world = REG_USER_MSG(skillfx::MessageName, -1); if (!current()) return false;
    for (int id : {next->trail, next->emitter, next->world})
        if (id < 64 || id > 255) return Fail(error, "elite visual message registration failed");
    for (std::size_t i = 0; i < elitefx::Models.size(); ++i)
    {
        const int model = PRECACHE_MODEL(const_cast<char*>(elitefx::Models[i]));
        if (!current()) return false;
        if (model < 1 || model > 0xffff) return Fail(error, "elite visual model precache failed");
        if (i == 3) next->frost = model;
        if (i == 4) next->pull = model;
        if (i == 5) next->explosion = model;
    }
    resources = std::move(next);
    return true;
}

void ResetEliteVisuals(const CCSOLScenarioSession& session)
{
    if (resources && resources->session.lock().get() == &session) resources.reset();
}

bool PublishEliteEntityVisual(CCSOLScenarioSession& session, const EHANDLE& handle,
    CSOLEliteEntityVisual kind, const Vector& offset, float duration, float radius, ConfigError& error)
{
    const auto state = resources;
    if (!Current(state, session) || sending) return false;
    auto* entity = handle.Get<CBaseEntity>();
    if (!entity || !entity->pev || (entity->pev->flags & FL_KILLME)) return false;
    const int index = entity->entindex();
    const auto current = [&] { return Current(state, session) && handle.Get<CBaseEntity>() == entity; };
    if (!current() || index < 1 || index > 0x7fff) return false;
    if (!Transport()) return Fail(error, "elite visual message transport is unavailable");
    if (!Finite(offset) || !std::isfinite(duration) || !std::isfinite(radius) || duration < 0 || radius < 0)
        return Fail(error, "elite visual parameters are invalid");
    if (kind == CSOLEliteEntityVisual::FireballTrail)
    {
        const Sending guard;
        // MP109680CE: ZombieBoss20/4, not the unrelated MPToCL20 channel.
        MESSAGE_BEGIN(MSG_BROADCAST, state->trail);
        WRITE_BYTE(20); WRITE_BYTE(4); WRITE_LONG(index);
        MESSAGE_END();
        return current();
    }
    if (kind == CSOLEliteEntityVisual::FireballExplosion)
    {
        if (!g_engfuncs.pfnEmitAmbientSound) return Fail(error, "elite explosion sound API is unavailable");
        Vector origin = entity->pev->origin;
        if (!Finite(origin)) return Fail(error, "elite explosion origin is invalid");
        const float scale = (radius * 0.025400051847100258f) * 8.0f;
        if (scale > 2147483520.0f) return Fail(error, "elite explosion scale is invalid");
        {
            const Sending guard;
            MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, origin);
            WRITE_BYTE(TE_EXPLOSION);
            WRITE_COORD(origin.x); WRITE_COORD(origin.y); WRITE_COORD(origin.z);
            WRITE_SHORT(state->explosion); WRITE_BYTE(static_cast<int>(scale));
            WRITE_BYTE(30); WRITE_BYTE(TE_EXPLFLAG_NOSOUND);
            // CSO appends a zero extension byte; stock TE_EXPLOSION has no
            // such field. Its ordinary sprite effect is the zero-extension case.
            MESSAGE_END();
        }
        if (!current()) return false;
        EMIT_AMBIENT_SOUND(entity->edict(), origin, elitefx::Sounds[0] + 6, 1, 0, 0, 100);
        return current();
    }
    const bool center = kind == CSOLEliteEntityVisual::FirewallSegment;
    if (!center && kind != CSOLEliteEntityVisual::FirewallPositiveSegment &&
        kind != CSOLEliteEntityVisual::FirewallNegativeSegment)
        return Fail(error, "unsupported elite visual kind");
    if (!g_engfuncs.pfnRandomFloat || !g_engfuncs.pfnRandomLong)
        return Fail(error, "elite particle random API is unavailable");
    const auto* sprite = center ? "sprites/burn7.spr" :
        kind == CSOLEliteEntityVisual::FirewallPositiveSegment ? "sprites/burn5.spr" : "sprites/burn6.spr";
    // Preserve the original RNG order, which differs from wire field order.
    const float growth = RANDOM_FLOAT(2, 5); if (!current()) return false;
    const int size = RANDOM_LONG(80, 100); if (!current()) return false;
    const int spread = RANDOM_LONG(70, 120); if (!current()) return false;
    const Sending guard;
    MESSAGE_BEGIN(MSG_BROADCAST, state->emitter);
    WRITE_STRING(sprite); WRITE_SHORT(index);
    WRITE_COORD(offset.x); WRITE_COORD(offset.y); WRITE_COORD(offset.z);
    WRITE_COORD(0); WRITE_COORD(0); WRITE_COORD(center ? 0 : 10);
    WRITE_SHORT(-1); Float(.04f); Float(duration); WRITE_LONG(0x830);
    WRITE_BYTE(0); WRITE_BYTE(0); WRITE_BYTE(15); WRITE_BYTE(5); WRITE_BYTE(150);
    WRITE_BYTE(255); WRITE_BYTE(255); WRITE_BYTE(255);
    Float(static_cast<float>(spread)); Float(static_cast<float>(size));
    Float(0); Float(5); Float(growth); Float(0); Float(0);
    MESSAGE_END();
    return current();
}

bool PublishEliteAreaVisual(CSOLMonsterSpawnCall& call, int ability, ConfigError& error)
{
    const auto state = resources;
    const auto session = state ? state->session.lock() : nullptr;
    if (!session || !Current(state, *session) || sending || !call.BelongsTo(session->Map())) return false;
    auto* entity = call.Current();
    if (!entity || !entity->pev) return false;
    if (ability != 4 && ability != 12) return Fail(error, "unsupported elite area visual");
    if (!Transport()) return Fail(error, "elite area visual transport is unavailable");
    skillfx::WorldEffect effect;
    effect.model.origin = {entity->pev->origin.x, entity->pev->origin.y, entity->pev->origin.z};
    effect.model.model = static_cast<std::uint16_t>(ability == 4 ? state->frost : state->pull);
    // Original115302F0 -> HW TE145: frost1.5/.5/fade1, pull1/.8/no fade.
    effect.model.life = ability == 4 ? 1.5f : 1.0f;
    effect.model.frameRate = ability == 4 ? .5f : .8f;
    effect.model.fadeSpeed = ability == 4 ? 1.0f : 0;
    effect.model.fadeIn = 0; effect.model.scale = 0; effect.model.alpha = 255;
    const auto bytes = skillfx::Encode(effect);
    if (bytes.empty()) return Fail(error, "elite area visual parameters are invalid");
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, state->world);
    for (auto byte : bytes) WRITE_BYTE(byte);
    MESSAGE_END();
    return Current(state, *session) && call.Current() == entity;
}
}
