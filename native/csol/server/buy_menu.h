// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct edict_s;
// Call during scenario precache; sends capability in the sign-on stream only
// after the original presentation resources are present and precached.
bool CSOL_PrecacheBuyMenu();
bool CSOL_BuyMenuCommand(edict_s* entity,const char* command);
void CSOL_ResetBuyMenu();
