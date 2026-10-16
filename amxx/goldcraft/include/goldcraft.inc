#if defined _goldcraft_included
    #endinput
#endif
#define _goldcraft_included
#pragma reqlib goldcraft
#pragma loadlib goldcraft

#define GC_FORM_CS 0
#define GC_FORM_MINECRAFT 1

// API version 1 requires the workspace ReHLDS/ReGameDLL and GoldCraft AMXX module.
native gc_api_version();
native gc_get_form(id); // -1 disconnected; otherwise GC_FORM_*
native gc_is_paired(id);
// 1 changed; 0 unchanged; -1 invalid, -2 unpaired, -3 unavailable, -4 exhausted, -5 vetoed.
// This runs on the authoritative game server and revokes old movement updates.
native gc_set_form(id, form);
forward goldcraft_form_changed(id, previous, current);

#define GC_PLAYER_CONNECTED 1
#define GC_PLAYER_CREATIVE 2
#define GC_PLAYER_ARMORED 4
native gc_gameplay_api_version();
// -2 unsupported, -1 unpaired; >=0 is the authenticated GC_PLAYER_* mask.
native gc_player_flags(id);
// Only during matching active player damage: 1 melee, 2 projectile, 4 skill; 0 otherwise.
native gc_player_attack(victim, attacker);
// PLUGIN_HANDLED vetoes MC entry, including commands/default/Pawn. CS exit is never vetoed.
forward goldcraft_form_request(id, requested);
// Actual native loss after all damage hooks; no forward for rejected/zero-loss hits.
forward goldcraft_player_attack_post(victim, attacker, kind, Float:health_loss, Float:armor_loss);

stock bool:gc_mc_infection_protected(id)
{
    if (gc_get_form(id) != GC_FORM_MINECRAFT) return false;
    new flags = gc_player_flags(id);
    return flags >= 0 && (flags & (GC_PLAYER_CONNECTED | GC_PLAYER_ARMORED)) == (GC_PLAYER_CONNECTED | GC_PLAYER_ARMORED);
}
