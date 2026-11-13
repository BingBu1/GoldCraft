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
native csol_scenario_map_vote_api_version();
// GoldCraft administrator request, independent of the post-clear policy.
// Check administrator permissions before calling. True means queued for a
// safe frame, not that the vote opened or that a map change has completed.
native bool:csol_scenario_map_vote(mapId);

// Typed persistent GoldCraft settings. Reads do not grant edit authority.
enum
{
    CSOL_CUSTOM_BOOLEAN = 0,
    CSOL_CUSTOM_INTEGER,
    CSOL_CUSTOM_REAL
};
enum
{
    CSOL_CUSTOM_FAILED = -1,
    CSOL_CUSTOM_STALE = -2,
    CSOL_CUSTOM_BUSY = -3,
    CSOL_CUSTOM_UNAVAILABLE = -4,
    CSOL_CUSTOM_QUEUED = 1,
    CSOL_CUSTOM_SAVED = 2
};
native csol_custom_api_version();
native bool:csol_custom_info(&revision, &count, &busy, &result, error[], length);
native bool:csol_custom_read(index, key[], keyLength, comment[], commentLength,
    &type, &Float:value, &Float:minimum, &Float:maximum, &nextMap, &available);
// The module rechecks current ADMIN_CFG and exact userid. Queued is not saved;
// poll info and the matching revision/value before reporting durable success.
native csol_custom_edit(id, expectedUserid, revision, index, Float:value);
