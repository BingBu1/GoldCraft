// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace csol::scenario_respawn_ui
{
// Shared by the original-message consumer and the server download list.
struct Resource { const char* path; int width, height; };
inline constexpr std::array<Resource, 2> Outcomes{{
    {"resource/Hud/ZBS/roundclear.tga", 410, 73},
    {"resource/Hud/ZBS/roundfail.tga", 410, 73}
}};
inline constexpr char ItemSprite[] = "sprites/zbs_level.spr";
inline constexpr Resource CenterBackground{"resource/Hud/Hud_string_bg.tga", 673, 33};
inline constexpr char LocalizationPath[] = "resource/cso_chn.txt";
inline constexpr std::array<Resource, 11> RetryImages{{
    {"resource/Hud/ZBS/continue.tga", 512, 73},
    {"resource/Hud/ZBS/Number0.tga", 128, 128},
    {"resource/Hud/ZBS/Number1.tga", 128, 128},
    {"resource/Hud/ZBS/Number2.tga", 128, 128},
    {"resource/Hud/ZBS/Number3.tga", 128, 128},
    {"resource/Hud/ZBS/Number4.tga", 128, 128},
    {"resource/Hud/ZBS/Number5.tga", 128, 128},
    {"resource/Hud/ZBS/Number6.tga", 128, 128},
    {"resource/Hud/ZBS/Number7.tga", 128, 128},
    {"resource/Hud/ZBS/Number8.tga", 128, 128},
    {"resource/Hud/ZBS/Number9.tga", 128, 128}
}};
inline constexpr std::array<const char*, 11> RetrySounds{
    "sound/vox/One.wav", "sound/vox/Two.wav", "sound/vox/Three.wav",
    "sound/vox/Four.wav", "sound/vox/Five.wav", "sound/vox/Six.wav",
    "sound/vox/Seven.wav", "sound/vox/Eight.wav", "sound/vox/Nine.wav",
    "sound/vox/Ten.wav", "sound/zombi/TD_Heal.wav"
};
}
