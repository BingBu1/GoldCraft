// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace guest::cof::arsenal
{
struct ShotSound
{
    const char *localKey, *remoteKey, *localFile, *nativeAsset;
};
// Original PlaySFX uses the high-resolution local sample for the shooter and
// the native positional sample for the other players.
inline constexpr std::array Gunshots{
    ShotSound{"weapons_new/browning/browning_fire.wav", "weapons/browning/browning_fire.wav",
        "sound/guest/cof/weapons_new/browning/browning_fire.wav", "guest/cof/weapons/browning/browning_fire.wav"},
    ShotSound{"weapons_new/rifle/rifle_fire.wav", "weapons/rifle/rifle_fire.wav",
        "sound/guest/cof/weapons_new/rifle/rifle_fire.wav", "guest/cof/weapons/rifle/rifle_fire.wav"}
};
}
