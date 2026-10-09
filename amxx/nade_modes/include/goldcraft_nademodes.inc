// GoldCraft's ReAPI adapter for Nomexous & OT's Nade Modes 11.2.
// SPDX-License-Identifier: GPL-3.0-or-later
// Matched to ReAPI 5.29.0.358 / ReGameDLL 4a50c42e. No private offsets.

native gc_get_form(id); // Optional: a plain ReAPI server does not need GoldCraft.

enum _:NMRecord {
    NMSerial,
    NMRacePlusOne,
    NMOwnerUserId,
    NMAttachedSerial,
    NMTargetToken
}
new Array:gNmEntities;
new bool:gNmHasGoldCraft = true;
new bool:gNmAttack2[MAX_PLAYERS + 1];
new gNmThrows[NadeRace][NadeType], gNmTriggers[NadeRace][NadeType];
new gNmBotModeThrows[NadeRace][NadeType], gNmBotModeTriggers[NadeRace][NadeType];
new gNmBotThrows;
new Float:gNmTripInterval = 0.02;
const NM_TASK_TRIP = 100000;

public plugin_natives()
{
    register_library("nademodes");
    register_native("nm_get_grenade_race", "nm_native_race");
    register_native("nm_get_player_mode", "nm_native_get_mode");
    register_native("nm_set_player_mode", "nm_native_set_mode");
    set_native_filter("nm_native_filter");
}

public nm_native_filter(const name[], index, trap)
{
    if (!trap && equal(name, "gc_get_form")) {
        gNmHasGoldCraft = false;
        return PLUGIN_HANDLED;
    }
    return PLUGIN_CONTINUE;
}

bool:nm_cs_form(id)
{
    return !gNmHasGoldCraft || gc_get_form(id) == 0;
}

nm_init_entities()
{

    gNmEntities = ArrayCreate(NMRecord);
    ArrayResize(gNmEntities, global_get(glb_maxEntities));
    new empty[NMRecord];
    for (new ent = 0; ent < ArraySize(gNmEntities); ent++) ArraySetArray(gNmEntities, ent, empty);
}

bool:nm_record(ent, data[NMRecord])
{
    if (gNmEntities == Invalid_Array || ent <= MaxClients || ent >= ArraySize(gNmEntities) || !pev_valid(ent))
        return false;
    ArrayGetArray(gNmEntities, ent, data);
    return data[NMRacePlusOne] >= 1 && data[NMRacePlusOne] <= 3 && data[NMSerial] == pev_serial(ent);
}

bool:nm_owner_valid(ent)
{
    new data[NMRecord], owner = get_entvar(ent, var_owner);
    return nm_record(ent, data) && 1 <= owner <= MaxClients &&
        is_user_connected(owner) && get_user_userid(owner) == data[NMOwnerUserId];
}

public nm_native_race(plugin, params)
{
    return get_grenade_race(get_param(1));
}

public nm_native_get_mode(plugin, params)
{
    new id = get_param(1), race = get_param(2);
    if (!(1 <= id <= MaxClients) || !is_user_connected(id) || !(0 <= race < 3)) return -1;
    return _:mode[id][NadeRace:race];
}

public nm_native_set_mode(plugin, params)
{
    new id = get_param(1), race = get_param(2), selected = get_param(3);
    if (!(1 <= id <= MaxClients) || !is_user_alive(id) || !nm_cs_form(id) ||
        !(0 <= race < 3) || !(0 <= selected <= _:NADE_HOMING)) return 0;
    if (!is_nademodes_enabled() || !grenade_can_be_used(NadeRace:race) ||
        !is_mode_enabled(id, NadeType:selected, NadeRace:race)) return 0;
    mode[id][NadeRace:race] = NadeType:selected;
    return 1;
}

public nm_he_thrown(id, Float:start[3], Float:velocity[3], Float:time, team, event)
{
    nm_track_throw(id, GetHookChainReturn(ATYPE_INTEGER), GRENADE_EXPLOSIVE);
}
public nm_flash_thrown(id, Float:start[3], Float:velocity[3], Float:time)
{
    nm_track_throw(id, GetHookChainReturn(ATYPE_INTEGER), GRENADE_FLASHBANG);
}
public nm_smoke_thrown(id, Float:start[3], Float:velocity[3], Float:time, event)
{
    nm_track_throw(id, GetHookChainReturn(ATYPE_INTEGER), GRENADE_SMOKEGREN);
}

nm_track_throw(id, ent, NadeRace:race)
{
    if (!(1 <= id <= MaxClients) || !is_user_connected(id) ||
        ent <= MaxClients || ent >= ArraySize(gNmEntities) || !is_entity(ent) ||
        get_member(ent, m_Grenade_bIsC4)) return;
    new data[NMRecord];
    data[NMSerial] = pev_serial(ent);
    data[NMRacePlusOne] = _:race + 1;
    data[NMOwnerUserId] = get_user_userid(id);
    ArraySetArray(gNmEntities, ent, data);
    // ReGameDLL has finished initializing CGrenade and the ZP SetModel hooks.
    // Its identity is independent of replacement models or weapon pdata.
    set_entvar(ent, var_iuser1, _:NADE_NORMAL);
    cl_team[id] = TeamName:get_member(id, m_iTeam);
    if ((bs_forward_collection & FWD_SETMODEL) && nm_cs_form(id))
        grenade_process(id, ent, race);
    if (!is_grenade(ent)) return;
    new selected = _:get_grenade_type(ent);
    if (0 <= selected <= _:NADE_HOMING) gNmThrows[race][NadeType:selected]++;
    if (is_user_bot(id)) {
        gNmBotThrows++;
        if (0 <= selected <= _:NADE_HOMING) gNmBotModeThrows[race][NadeType:selected]++;
    }
}

nm_remove(ent)
{
    if (!is_grenade(ent)) return;
    nm_release_counter(ent);
    remove_task(NM_TASK_TRIP + ent);
    clear_line(ent);
    set_entvar(ent, var_flags, get_entvar(ent, var_flags) | FL_KILLME);
    new empty[NMRecord];
    ArraySetArray(gNmEntities, ent, empty);
}

nm_release_counter(ent)
{
    if (!is_grenade(ent)) return;
    new type = _:get_grenade_type(ent), owner = get_entvar(ent, var_owner);
    if (type & NADE_DONT_COUNT) return;
    if (!(0 <= type <= _:NADE_HOMING) || (UNCOUNTABLE_NADE_MODES & (1 << (type + 1)))) return;
    if (nm_owner_valid(ent)) {
        new NadeRace:race = NadeRace:get_grenade_race(ent);
        cl_counter[owner][race][NadeType:type] = max(0, cl_counter[owner][race][NadeType:type] - 1);
        refresh_can_use_nade(owner, GRENADE_EXPLOSIVE);
        refresh_can_use_nade(owner, GRENADE_FLASHBANG);
        refresh_can_use_nade(owner, GRENADE_SMOKEGREN);
    }
    set_entvar(ent, var_iuser1, type | NADE_DONT_COUNT);
}

public nm_entity_freed(ent)
{
    if (!is_grenade(ent)) return HC_CONTINUE;
    nm_release_counter(ent);
    remove_task(NM_TASK_TRIP + ent);
    new empty[NMRecord];
    ArraySetArray(gNmEntities, ent, empty);
    return HC_CONTINUE;
}

nm_init_reapi_options()
{
    RegisterHookChain(RH_ED_Free, "nm_entity_freed");
    bind_pcvar_float(create_cvar("nademodes_trip_scan_interval", "0.02", FCVAR_NONE,
        "Seconds between continuous trip-laser scans", true, 0.01, true, 0.1), gNmTripInterval);
}

nm_remove_owner(id, bool:include_normal = false)
{
    new ent = -1;
    while ((ent = rg_find_ent_by_class(ent, "grenade"))) {
        if (is_grenade(ent) && get_entvar(ent, var_owner) == id &&
            (include_normal || get_grenade_type(ent) != NADE_NORMAL)) nm_remove(ent);
    }
}

nm_detonate(ent)
{
    if (!is_grenade(ent)) return;
    set_entvar(ent, var_dmgtime, 0.0);
    if (get_grenade_race(ent) == _:GRENADE_SMOKEGREN && get_grenade_type(ent) != NADE_NORMAL) {
        // Both native smoke and ZP 5's flare wait for a stopped, grounded nade.
        // An explicitly triggered wall/remote trap must enter that same path.
        set_entvar(ent, var_flags, get_entvar(ent, var_flags) | FL_ONGROUND);
        set_entvar(ent, var_velocity, Float:{0.0, 0.0, 0.0});
    }
}

nm_attach(ent, surface)
{
    new data[NMRecord];
    if (!nm_record(ent, data)) return;
    // Fakemeta's FNullEnt test represents world pHit as -1.
    // The caller already checked fraction < 1 and the surface normal. Store
    // both world representations as 0; only positive edicts need serials.
    if (surface == -1) surface = 0;
    data[NMAttachedSerial] = surface > 0 && pev_valid(surface) ? pev_serial(surface) : 0;
    ArraySetArray(gNmEntities, ent, data);
    set_entvar(ent, var_iuser4, surface);
}

bool:nm_attachment_alive(ent)
{
    new data[NMRecord], surface = get_trip_grenade_attached_to(ent);
    return nm_record(ent, data) && (surface == 0 || (pev_valid(surface) &&
        pev_serial(surface) == data[NMAttachedSerial])) && is_attachable_surface(surface);
}

nm_target(ent, target)
{
    new data[NMRecord];
    if (!nm_record(ent, data)) return;
    data[NMTargetToken] = target <= MaxClients ? get_user_userid(target) : pev_serial(target);
    ArraySetArray(gNmEntities, ent, data);
    set_entvar(ent, var_iuser2, target);
}

bool:nm_target_valid(ent, target)
{
    new data[NMRecord];
    if (!target || !pev_valid(target) || !nm_record(ent, data)) return false;
    if (target <= MaxClients) {
        new owner = get_entvar(ent, var_owner);
        return is_user_alive(target) && get_user_userid(target) == data[NMTargetToken] &&
            (!get_option(OPTION_TEAM_PLAY) || get_member(target, m_iTeam) != get_member(owner, m_iTeam));
    }
    return pev_serial(target) == data[NMTargetToken] && is_ent_monster(target);
}

nm_arm_sound(ent)
{
    new data[2];
    data[0] = ent;
    data[1] = pev_serial(ent);
    set_task_ex(0.1, "nm_trip_charge", NM_TASK_TRIP + ent, data, sizeof data);
}

public nm_trip_charge(data[], task)
{
    new ent = data[0];
    if (is_grenade(ent) && pev_serial(ent) == data[1] &&
        !(get_entvar(ent, var_flags) & FL_KILLME) && get_grenade_type(ent) == NADE_TRIP)
        play_sound(ent, CHARGE);
}

public nm_player_traceattack(ent, attacker, Float:damage, Float:direction[3], trace, bits)
{
    fw_global_traceattack(ent, attacker, damage, direction, trace, bits);
    return HC_CONTINUE;
}

public zp_fw_gamemodes_start(game_mode)
{
    g_zombie_mod = ZM_DO_ALL;
}

public zp_fw_gamemodes_end(game_mode)
{
    g_zombie_mod = ZM_ZM_ACTIVE;
}

public goldcraft_form_changed(id, previous, current)
{
    gNmAttack2[id] = false;
    cl_nextusetime[id] = 0.0;
}

public nm_cvar_changed(pcvar, const previous[], const current[])
{
    // AMXX 1.9 notifies only actual changes, rather than polling every 2 s.
    if (gNmCvarsReady) {
        update_forward_registration();
        for (new id = 1; id <= MaxClients; id++) {
            if (!is_user_connected(id)) continue;
            refresh_can_use_nade(id, GRENADE_EXPLOSIVE);
            refresh_can_use_nade(id, GRENADE_FLASHBANG);
            refresh_can_use_nade(id, GRENADE_SMOKEGREN);
        }
    }
}

nm_menu_navigation(menu)
{
    menu_setprop(menu, MPROP_BACKNAME, "上一页");
    menu_setprop(menu, MPROP_NEXTNAME, "下一页");
    menu_setprop(menu, MPROP_EXITNAME, "返回 / 退出");
}

Float:nm_option_float(Option:option)
{
    // HP and percentages are integer cvars upstream; reinterpreting their bits
    // as float turns e.g. 100 HP into an almost-zero denormal.
    return option_type[option] == TOPTION_FLOAT ? (Float:ccvars[option]) : float(ccvars[option]);
}

public nm_status()
{
    new active, ent = -1;
    while ((ent = rg_find_ent_by_class(ent, "grenade")))
        if (is_grenade(ent) && !(get_entvar(ent, var_flags) & FL_KILLME)) active++;
    server_print("[NadeModes] ReAPI=5.29 active=%d botsThrown=%d goldcraft=%d slots=%d zp=%d", active, gNmBotThrows, gNmHasGoldCraft, ArraySize(gNmEntities), _:g_zombie_mod);
    new alive, he, flash, smoke;
    for (new id = 1; id <= MaxClients; id++) {
        if (!is_user_bot(id) || !is_user_alive(id)) continue;
        new weapons = get_entvar(id, var_weapons);
        alive++;
        if (weapons & (1 << _:WEAPON_HEGRENADE)) he++;
        if (weapons & (1 << _:WEAPON_FLASHBANG)) flash++;
        if (weapons & (1 << _:WEAPON_SMOKEGRENADE)) smoke++;
    }
    server_print("[NadeModes] aliveBots=%d holdingHE=%d holdingFlash=%d holdingSmoke=%d", alive, he, flash, smoke);
    for (new NadeType:type = NADE_NORMAL; type <= NADE_HOMING; type++)
    {
        server_print("[NadeModes] mode=%d thrown=%d,%d,%d triggered=%d,%d,%d", _:type,
            gNmThrows[GRENADE_EXPLOSIVE][type], gNmThrows[GRENADE_FLASHBANG][type], gNmThrows[GRENADE_SMOKEGREN][type],
            gNmTriggers[GRENADE_EXPLOSIVE][type], gNmTriggers[GRENADE_FLASHBANG][type], gNmTriggers[GRENADE_SMOKEGREN][type]);
        server_print("[NadeModes] botMode=%d thrown=%d,%d,%d triggered=%d,%d,%d", _:type,
            gNmBotModeThrows[GRENADE_EXPLOSIVE][type], gNmBotModeThrows[GRENADE_FLASHBANG][type], gNmBotModeThrows[GRENADE_SMOKEGREN][type],
            gNmBotModeTriggers[GRENADE_EXPLOSIVE][type], gNmBotModeTriggers[GRENADE_FLASHBANG][type], gNmBotModeTriggers[GRENADE_SMOKEGREN][type]);
    }
    return PLUGIN_HANDLED;
}
