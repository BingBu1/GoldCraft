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
// 1 changed; 0 unchanged; -1 invalid, -2 unpaired, -3 unavailable, -4 exhausted.
// This runs on the authoritative game server and revokes old movement updates.
native gc_set_form(id, form);
forward goldcraft_form_changed(id, previous, current);
