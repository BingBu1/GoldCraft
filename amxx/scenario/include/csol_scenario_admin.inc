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
// Updates the pending CVAR only; applies on a new map or an explicit replay.
native bool:csol_scenario_difficulty_request(mapId, difficulty);
native csol_scenario_replay_api_version();
// Queues a new challenge using pending difficulty without reloading the map.
// True means queued; duplicate requests and stale/unavailable maps are rejected.
native bool:csol_scenario_replay(mapId);
