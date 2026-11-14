// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "environment.hpp"
#include "settings.hpp"

namespace csol::scenario::custom
{
inline std::optional<EnvironmentConfig> ReadEnvironmentConfig(const Snapshot& settings) noexcept
{
    EnvironmentConfig result;
    const auto dusk = settings.Integer(Setting::DuskLightLevel);
    const auto night = settings.Integer(Setting::NightLightLevel);
    const auto step = settings.Number(Setting::LightStepSeconds);
    const auto density = settings.Number(Setting::NightFogDensity);
    const auto color = settings.Integer(Setting::NightFogColor);
    const auto start = settings.Number(Setting::NightFogStart);
    const auto end = settings.Number(Setting::NightFogEnd);
    const auto sky = settings.Integer(Setting::NightFogSkybox);
    if (!dusk || !night || !step || !density || !color || !start || !end || !sky) return {};
    result.dusk = static_cast<char>(*dusk); result.night = static_cast<char>(*night);
    result.stepSeconds = static_cast<float>(*step); result.fog.density = static_cast<float>(*density);
    const auto rgb = static_cast<std::uint32_t>(*color);
    for (unsigned i = 0; i != 3; ++i) result.fog.color[i] = std::uint8_t((rgb >> (16 - 8 * i)) & 255);
    result.fog.start = static_cast<float>(*start); result.fog.end = static_cast<float>(*end);
    result.fog.sky = *sky != 0;
    return Valid(result) ? std::optional{result} : std::nullopt;
}
}
