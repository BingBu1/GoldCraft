// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit local test setup; never enabled or deployed as a gameplay plugin.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <hamsandwich>

public plugin_init()
{
    register_plugin("Oberon sound live probe", "0.1.0", "GoldCraft contributors");
    register_srvcmd("csol_sound_probe", "Command");
}

bool:Human(id, userid)
{
    return id >= 1 && id <= MaxClients && is_user_connected(id) &&
        !is_user_bot(id) && !is_user_hltv(id) && get_user_userid(id) == userid;
}

bool:Place(id, const Float:origin[3])
{
    new trace = create_tr2();
    engfunc(EngFunc_TraceHull, origin, origin, 0, HULL_HUMAN, id, trace);
    new blocked = get_tr2(trace, TR_StartSolid) || get_tr2(trace, TR_AllSolid);
    free_tr2(trace);
    if (blocked) return false;
    new Float:zero[3];
    set_entvar(id, var_velocity, zero);
    engfunc(EngFunc_SetOrigin, id, origin);
    return true;
}

public Command()
{
    new map[32]; get_mapname(map, charsmax(map));
    if (!equal(map, "zs_deadend") || get_cvar_num("csol_game_mode") != 2 || read_argc() != 4)
        return PLUGIN_HANDLED;
    new action[16], value[16]; read_argv(1, action, charsmax(action));
    read_argv(2, value, charsmax(value)); new id = str_to_num(value);
    read_argv(3, value, charsmax(value)); new userid = str_to_num(value);
    if (!Human(id, userid)) return PLUGIN_HANDLED;
    if (equal(action, "status"))
        server_print("[SoundProbe] player=%d team=%d joining=%d alive=%d dead=%d health=%.1f", id,
            get_member(id, m_iTeam), get_member(id, m_iJoiningState), is_user_alive(id),
            get_entvar(id, var_deadflag), Float:get_entvar(id, var_health));
    // Explicit test setup only; it does not validate normal late-join respawn.
    if (equal(action, "spawn") && get_member(id, m_iTeam) == TEAM_CT && !is_user_alive(id))
        rg_round_respawn(id);
    if (!is_user_alive(id)) return PLUGIN_HANDLED;
    if (equal(action, "goal"))
    {
        // Original BSP goal100 (*138) volume, consumed by the normal scanner.
        new Float:origin[3] = {3264.0, -120.0, 36.0};
        server_print("[SoundProbe] goal player=%d moved=%d", id, Place(id, origin));
        return PLUGIN_HANDLED;
    }
    new entity, model[128], Float:origin[3], Float:player[3];
    while ((entity = engfunc(EngFunc_FindEntityByString, entity, "classname", "monster_entity")) > 0)
    {
        if (pev_valid(entity) != 2) continue;
        get_entvar(entity, var_model, model, charsmax(model));
        if (!equal(model, "models/monster/zbs_bossL_big02.mdl")) continue;
        get_entvar(entity, var_origin, origin);
        server_print("[SoundProbe] boss=%d sequence=%d frame=%.3f animtime=%.3f hp=%.1f dead=%d pos=%.1f,%.1f,%.1f",
            entity, get_entvar(entity, var_sequence), Float:get_entvar(entity, var_frame),
            Float:get_entvar(entity, var_animtime), Float:get_entvar(entity, var_health),
            get_entvar(entity, var_deadflag), origin[0], origin[1], origin[2]);
        if (equal(action, "view"))
        {
            player = origin; player[0] -= 600.0; player[2] += 36.0;
            new moved = Place(id, player);
            if (moved)
            {
                new Float:angles[3];
                set_entvar(id, var_angles, angles); set_entvar(id, var_v_angle, angles);
                set_entvar(id, var_fixangle, 1);
            }
            server_print("[SoundProbe] view player=%d moved=%d", id, moved);
        }
        if (equal(action, "kill") && Float:get_entvar(entity, var_health) > 0.0 && IsHamValid(Ham_TakeDamage))
        {
            new Float:damage = Float:get_entvar(entity, var_health) + 100000.0;
            ExecuteHamB(Ham_TakeDamage, entity, id, id, damage, DMG_GENERIC);
            server_print("[SoundProbe] real_damage boss=%d hp=%.1f dead=%d", entity,
                Float:get_entvar(entity, var_health), get_entvar(entity, var_deadflag));
        }
    }
    return PLUGIN_HANDLED;
}
