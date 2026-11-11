// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_effects.hpp"

#include <metahook.h>
#include <com_model.h>
#include <event_api.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace guest::cof::client
{
namespace
{
template<std::size_t N>
bool Text(const char (&input)[N], std::string_view& output) noexcept
{
    const auto* end = static_cast<const char*>(std::memchr(input, 0, N));
    if (!end) return false;
    output = {input, static_cast<std::size_t>(end - input)};
    return true;
}

bool LocalPistol(Weapon weapon) noexcept
{
    return weapon == Weapon::Glock || weapon == Weapon::P345 ||
           weapon == Weapon::Vp70 || weapon == Weapon::Revolver;
}
}

EffectResult WeaponEffects::StudioEvent(const mstudioevent_s& event, const cl_entity_s& entity,
                                       Weapon currentWeapon) const noexcept
{
    if (event.event != 5004 && event.event != 5006 && event.event != 5080)
        return EffectResult::Unhandled;
    if (!entity.model) return EffectResult::InvalidEntity;
    std::string_view modelName, options;
    if (!Text(entity.model->name, modelName)) return EffectResult::InvalidEntity;
    if (!Text(event.options, options)) return EffectResult::InvalidOptions;

    if (event.event == 5004)
    {
        if (LocalPistol(currentWeapon) && options.find("weapons/") != options.npos)
        {
            if (!host_.localSound) return EffectResult::MissingBackend;
            const auto separator = options.find_first_of("/\\");
            const auto suffix = options.substr(separator + 1);
            constexpr std::string_view prefix = "weapons_new/";
            std::array<char, prefix.size() + sizeof(event.options)> sample{};
            std::memcpy(sample.data(), prefix.data(), prefix.size());
            std::memcpy(sample.data() + prefix.size(), suffix.data(), suffix.size());
            // Original local2D10080410 ignores bool and xyz. Pass explicit zero
            // instead of the original caller's raw models/cs bool float bits.
            const LocalSound request{{sample.data(), prefix.size() + suffix.size()}, 0, false, {}};
            return host_.localSound(host_.context, request) ? EffectResult::Dispatched : EffectResult::BackendFailed;
        }
        if (!host_.engine || !host_.engine->pfnPlaySoundByNameAtLocation)
            return EffectResult::MissingBackend;
        std::array<char, 3 + sizeof(event.options)> sample{};
        const bool cs = modelName.find("models/cs/") != modelName.npos;
        const auto prefixLength = cs ? 3U : 0U;
        if (cs) std::memcpy(sample.data(), "cs/", prefixLength);
        std::memcpy(sample.data() + prefixLength, options.data(), options.size());
        std::array<float, 3> origin;
        std::memcpy(origin.data(), entity.attachment[0], sizeof(origin));
        host_.engine->pfnPlaySoundByNameAtLocation(sample.data(), 1.0F, origin.data());
        return EffectResult::Dispatched;
    }
    if (event.event == 5006)
    {
        if (!host_.engine || !host_.engine->pEventAPI || !host_.engine->pEfxAPI ||
            !host_.engine->pEventAPI->EV_FindModelIndex || !host_.engine->pEfxAPI->R_TempModel)
            return EffectResult::MissingBackend;
        const int model = host_.engine->pEventAPI->EV_FindModelIndex("models/shell.mdl");
        std::array<float, 3> origin, velocity, angles{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            origin[i] = entity.attachment[1][i];
            velocity[i] = (entity.attachment[2][i] - origin[i]) * 10.0F;
        }
        return host_.engine->pEfxAPI->R_TempModel(origin.data(), velocity.data(), angles.data(),
                                                2.5F, model, 1)
            ? EffectResult::Dispatched : EffectResult::BackendFailed;
    }
    if (!host_.engine || !host_.engine->pfnServerCmd) return EffectResult::MissingBackend;
    std::array<char, 32> command{};
    std::snprintf(command.data(), command.size(), "zoominmdl %i", std::atoi(event.options));
    host_.engine->pfnServerCmd(command.data());
    return EffectResult::Dispatched;
}
}
