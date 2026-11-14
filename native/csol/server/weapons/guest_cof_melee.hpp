// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_resources.hpp"
#include "native/guest/cof/server/weapon_melee.hpp"

namespace csol::server::cof
{
inline constexpr guest::cof::PistolMeleeSamples VP70MeleeSamples{
    VP70Sounds[static_cast<unsigned>(VP70Sound::Swing1)].host,
    VP70Sounds[static_cast<unsigned>(VP70Sound::Swing2)].host,
    VP70Sounds[static_cast<unsigned>(VP70Sound::Swing3)].host,
    VP70Sounds[static_cast<unsigned>(VP70Sound::Swing4)].host,
    VP70Sounds[static_cast<unsigned>(VP70Sound::Swing5)].host,
    VP70Sounds[static_cast<unsigned>(VP70Sound::Swing6)].host,
    VP70Sounds[static_cast<unsigned>(VP70Sound::PunchSwing)].host,
    VP70Sounds[static_cast<unsigned>(VP70Sound::PunchSmack)].host
};
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
inline constexpr guest::cof::PistolMeleeSamples RevolverMeleeSamples{
    RevolverSounds[static_cast<unsigned>(RevolverSound::Swing1)].host,
    RevolverSounds[static_cast<unsigned>(RevolverSound::Swing2)].host,
    RevolverSounds[static_cast<unsigned>(RevolverSound::Swing3)].host,
    RevolverSounds[static_cast<unsigned>(RevolverSound::Swing4)].host,
    RevolverSounds[static_cast<unsigned>(RevolverSound::Swing5)].host,
    RevolverSounds[static_cast<unsigned>(RevolverSound::Swing6)].host,
    RevolverSounds[static_cast<unsigned>(RevolverSound::PunchSwing)].host,
    RevolverSounds[static_cast<unsigned>(RevolverSound::PunchSmack)].host
};
}
