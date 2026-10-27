// SPDX-License-Identifier: GPL-3.0-or-later
#if defined _csol_game_mode_included
    #endinput
#endif
#define _csol_game_mode_included

enum CSOLGameMode
{
    CSOL_MODE_NATIVE = 0,
    CSOL_MODE_ZOMBIE_PLAGUE = 1,
    CSOL_MODE_SCENARIO = 2
};

native csol_game_mode_api_version();
// Locked for this map. Changing csol_game_mode takes effect on the next map.
native csol_game_mode_current();
// Called by the first primary plugin after the complete early pause policy.
native bool:csol_game_mode_confirm(mode);
