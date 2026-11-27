// SPDX-License-Identifier: GPL-3.0-or-later
#include "maneuver_gear.hpp"
#include "../../runtime.hpp"
#include "../../camera.hpp"
#include "../../resource_refresh.hpp"
#include "../../../scenario/core/maneuver_gear.hpp"
#include <event_api.h>
#include <event_args.h>
#include <r_efx.h>
#include <beamdef.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <ostream>

namespace csol::client::maneuver
{
namespace
{
namespace assets = csol::maneuver_gear;
constexpr float WireLife = 0.15f;
struct Wires
{
    std::array<BEAM*, 2> beams{};
    int anchor{}, shot{};
    float expires{};
};
std::array<Wires, 32> players;
std::uint32_t active{};
int sprite{};
unsigned events{}, launches{}, gasSounds{}, releases{}, beamStarts{}, lateFrames{}, missingModels{}, changedAnchors{};

// A GoldSrc beam is borrowed. The engine can release it before its scheduled
// expiry when its end entity leaves the visible set.
bool Owns(const Wires& wires, const BEAM* beam, float now)
{
    return beam && now <= wires.expires &&
        beam->die == wires.expires && beam->modelIndex == sprite &&
        (beam->flags & FBEAM_ENDENTITY) && beam->endEntity == wires.anchor;
}

void ClearBeams(unsigned slot, float now)
{
    auto& wires = players[slot];
    for (auto* beam : wires.beams)
        if (Owns(wires, beam, now))
        {
            beam->die = now;
            beam->endEntity = 0;
        }
    wires.beams = {};
    wires.expires = 0;
    active &= ~(std::uint32_t{1} << slot);
}

void Receive(event_args_t* args)
{
    if (!args || !ResourceReadsAllowed()) return;
    SynchronizeSession();
    const char* level = engine.pfnGetLevelName();
    if (!level || std::strcmp(level, "maps/at_attack.bsp") || args->entindex < 1 ||
        args->entindex > engine.GetMaxClients() || args->entindex > int(players.size())) return;
    // GoldSrc encodes a beam end as a 12-bit edict and 4-bit attachment.
    if (args->iparam1 < 0 || args->iparam1 > 0xfff || unsigned(args->iparam2) > 1) return;
    const float now = engine.GetClientTime();
    if (!std::isfinite(now)) return;
    const unsigned slot = unsigned(args->entindex - 1);
    auto& wires = players[slot];
    if (!std::isfinite(args->fparam1) || args->fparam1 < 0 || args->fparam1 > 4095) return;
    const int shot = static_cast<int>(args->fparam1);
    ++events;
    if (!args->iparam1)
    {
        if (shot && wires.shot && wires.shot != shot) return;
        ClearBeams(slot, now);
        wires = {};
        ++releases;
        return;
    }

    auto* ev = engine.pEventAPI;
    auto* fx = engine.pEfxAPI;
    if (!ev || !ev->EV_PlaySound || !ev->EV_FindModelIndex || !fx || !fx->R_BeamPoints) return;
    const bool first = wires.anchor != args->iparam1 || wires.shot != shot;
    if (first)
    {
        if (wires.anchor && wires.anchor != args->iparam1) ++changedAnchors;
        ClearBeams(slot, now);
    }
    if (!sprite) sprite = ev->EV_FindModelIndex(assets::WireSprite);
    if (sprite <= 0) return;

    float start[3]{}, end[3]{}, forward[3]{};
    float height[]{0.0f, 0.0f, args->ducking == 1 ? 12.0f : 17.0f};
    if (ev->EV_IsLocal && ev->EV_IsLocal(slot) && ev->EV_LocalPlayerViewheight)
        ev->EV_LocalPlayerViewheight(height);
    engine.pfnAngleVectors(args->angles, forward, nullptr, nullptr);
    for (int axis = 0; axis < 3; ++axis)
    {
        start[axis] = args->origin[axis] + height[axis];
        end[axis] = start[axis] + forward[axis] * 8192.0f;
    }

    wires.anchor = args->iparam1;
    wires.shot = shot;
    const float expiry = now + WireLife;
    for (unsigned side = 0; side < wires.beams.size(); ++side)
    {
        auto*& beam = wires.beams[side];
        if (!Owns(wires, beam, now))
        {
            beam = fx->R_BeamPoints(start, end, sprite, WireLife, 0.6f, 0, 0.1f, 0, 0, 10, 1, 1, 1);
            if (beam) { beam->flags = FBEAM_SOLID | FBEAM_ENDENTITY; ++beamStarts; }
        }
        if (beam)
        {
            beam->endEntity = wires.anchor;
            beam->die = expiry;
        }
    }
    wires.expires = expiry;
    active |= std::uint32_t{1} << slot;
    // 025A657F and 025A65C8 play from the event origin, once on the first side.
    if (first)
    {
        ev->EV_PlaySound(args->entindex, args->origin, CHAN_WEAPON, assets::WireSound, 1.0f, 0.8f, 0, 100);
        ++launches;
    }
    if (args->iparam2 == 1 && wires.beams[0])
    {
        ev->EV_PlaySound(args->entindex, args->origin, CHAN_AUTO, assets::GasSound, 0.2f, 0.8f, 0, 100);
        ++gasSounds;
    }
}
}

void Initialize()
{
    if (engine.pfnHookEvent) engine.pfnHookEvent(assets::Event, Receive);
}

void Reset()
{
    // The world or video system may already have released its beam pool.
    players = {};
    active = 0;
    sprite = 0;
}

void Frame()
{
    if (!active) return;
    const float now = engine.GetClientTime();
    const auto* local = engine.GetLocalPlayer();
    for (auto mask = active; mask; mask &= mask - 1)
    {
        const unsigned slot = std::countr_zero(mask);
        const int player = int(slot) + 1;
        const auto& wires = players[slot];
        if (now >= wires.expires) { ++lateFrames; ClearBeams(slot, now); continue; }
        const bool firstPerson = local && local->index == player && !CameraIsThirdPerson();
        const auto* model = firstPerson ? engine.GetViewModel() : engine.GetEntityByIndex(player);
        if (!model || !model->model) { ++missingModels; ClearBeams(slot, now); continue; }
        float right[3]{};
        if (!firstPerson)
        {
            float angles[]{float(double(model->angles[0]) * -3.0340726356988985), model->angles[1], model->angles[2]};
            engine.pfnAngleVectors(angles, nullptr, right, nullptr);
        }
        for (unsigned side = 0; side < wires.beams.size(); ++side)
        {
            auto* beam = wires.beams[side];
            if (!Owns(wires, beam, now)) continue;
            if (firstPerson)
                std::copy_n(model->attachment[side], 3, beam->source);
            else
                for (int axis = 0; axis < 3; ++axis)
                    beam->source[axis] = model->origin[axis] + right[axis] * (side ? 4.0f : -4.0f);
        }
    }
}

void WriteSnapshot(std::ostream& out)
{
    out << ",\"maneuver\":{\"events\":" << events << ",\"launches\":" << launches
        << ",\"gasSounds\":" << gasSounds << ",\"releases\":" << releases
        << ",\"beamsCreated\":" << beamStarts << ",\"expiredFrames\":" << lateFrames
        << ",\"missingModels\":" << missingModels << ",\"anchorChanges\":" << changedAnchors
        << ",\"players\":" << std::popcount(active) << '}';
}

void Diagnostics(std::ostream& out)
{
    out << " maneuverEvents=" << events << " wireSounds=" << launches << " gasSounds=" << gasSounds
        << " releases=" << releases << " wirePlayers=" << std::popcount(active);
}
}
