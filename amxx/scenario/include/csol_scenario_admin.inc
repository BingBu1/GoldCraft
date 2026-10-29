// SPDX-License-Identifier: GPL-3.0-or-later
#if defined _csol_scenario_admin_included
    #endinput
#endif
#define _csol_scenario_admin_included

native csol_scenario_admin_api_version();
// Current map snapshot. requested=0 means an invalid pending request.
// Unavailable returns false without changing output references.
native bool:csol_scenario_difficulty_info(&active, &requested, &maximum, &mapId);
// Trusted server plugin API. Check administrator permissions at submission.
// Updates the pending CVAR only; applies on a complete map/session reload.
native bool:csol_scenario_difficulty_request(mapId, difficulty);
