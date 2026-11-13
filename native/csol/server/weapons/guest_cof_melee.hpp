// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_resources.hpp"
#include "native/guest/cof/server/weapon_melee.hpp"

namespace csol::server::cof
{
// Host resource placement only. Original logical selection, channels, timing,
// volume and RNG remain in the evidence-verified melee consumer.
inline constexpr guest::cof::PistolMeleeSamples P345MeleeSamples{
    P345Sounds[static_cast<unsigned>(P345Sound::Swing1)].host,
    P345Sounds[static_cast<unsigned>(P345Sound::Swing2)].host,
    P345Sounds[static_cast<unsigned>(P345Sound::Swing3)].host,
    P345Sounds[static_cast<unsigned>(P345Sound::Swing4)].host,
    P345Sounds[static_cast<unsigned>(P345Sound::Swing5)].host,
    P345Sounds[static_cast<unsigned>(P345Sound::Swing6)].host,
    P345Sounds[static_cast<unsigned>(P345Sound::PunchSwing)].host,
    P345Sounds[static_cast<unsigned>(P345Sound::PunchSmack)].host
};
}
