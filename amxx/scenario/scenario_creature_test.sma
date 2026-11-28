// SPDX-License-Identifier: GPL-3.0-or-later
#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
#include <xs>
#include <csol_game_mode>
#include <scenario_test_studio>

#define CREATURE_PLUGIN "Scenario Creature Placement"
new const GHOST[] = "scenario_creature_ghost";
new const ACTOR[] = "scenario_creature_target";
enum { Siren, Reptile, KindCount };
enum { Rest, Hurt, Dying, TrackCount };
new const Names[KindCount][] = { "警笛头", "SCP-682" };
new const Models[KindCount][] = { "models/siren_head/siren_head.mdl", "models/scp682/scp682.mdl" };
new const SoundDirs[KindCount][] = { "siren_head/", "scp682/" };
new const Labels[KindCount][TrackCount][] = {
    { "walk", "flinch", "death" },
    { "SCP_862_Idle", "", "SCP_862_Death" }
};
const Float:TestHealth = 200.0;
const Float:RecoilTime = 0.25;
const Float:CorpseHold = 6.0;
const AttackKeys = IN_ATTACK | IN_ATTACK2;

new g_Tracks[KindCount][TrackCount][StudioTrack], g_Cues[MAX_CREATURE_CUES][StudioCue], g_CueCount;
new Float:g_Mins[KindCount][3], Float:g_Maxs[KindCount][3];
new Float:g_ClipRadius[KindCount], Float:g_ClipBottom[KindCount], Float:g_ClipTop[KindCount];
// The idle pelvis, abdomen and chest occupy the central part of the reptile.
new const Float:ReptileMoveMin[3] = { -38.0, -18.0, 0.0 };
new const Float:ReptileMoveMax[3] = { 34.0, 18.0, 56.0 };
new g_Ghost[33], g_Userid[33], g_LastButtons[33], g_Menu[33] = { -1, ... };
new bool:g_Suppress[33], Float:g_NextTrace[33];
new g_Trace, g_MaxEdicts;

public plugin_precache()
{
    if (csol_game_mode_current() != _:CSOL_MODE_SCENARIO) return;
    for (new kind; kind < KindCount; kind++)
    {
        new file = fopen(Models[kind], "rb");
        if (!file) { set_fail_state("Missing creature test model"); return; }
        new size = file_size(Models[kind]), table = ReadStudioInt(file, 168), count = ReadStudioInt(file, 164);
        new bool:ok = ReadStudioInt(file, 0) == 0x54534449 && ReadStudioInt(file, 4) == 10 &&
            count > 0 && count <= 256 && table >= 244 && table <= size && count <= (size-table)/176;
        for (new track; ok && track < TrackCount; track++)
        {
            if (Labels[kind][track][0])
                ok = ReadCreatureTrack(file, size, table, count, Labels[kind][track],
                    SoundDirs[kind], g_Tracks[kind][track], g_Cues, g_CueCount);
        }
        if (ok)
        {
            for (new track; track < TrackCount; track++)
            {
                if (!Labels[kind][track][0]) continue;
                new base = table + 176 * g_Tracks[kind][track][ClipIndex];
                new Float:radiusVec[3], Float:lo, Float:hi;
                for (new axis; axis < 3; axis++)
                {
                    lo = Float:ReadStudioInt(file, base + 96 + axis * 4);
                    hi = Float:ReadStudioInt(file, base + 108 + axis * 4);
                    if (axis < 2) radiusVec[axis] = floatmax(floatabs(lo), floatabs(hi));
                }
                g_ClipRadius[kind] = floatmax(g_ClipRadius[kind], xs_vec_len(radiusVec) + 2.0);
                g_ClipBottom[kind] = floatmin(g_ClipBottom[kind], lo - 2.0);
                g_ClipTop[kind] = floatmax(g_ClipTop[kind], hi + 2.0);
            }
            if (kind == Reptile)
            {
                xs_vec_copy(ReptileMoveMin, g_Mins[kind]);
                xs_vec_copy(ReptileMoveMax, g_Maxs[kind]);
            }
            else for (new axis; axis < 3; axis++)
            {
                g_Mins[kind][axis] = Float:ReadStudioInt(file, 88 + axis * 4);
                g_Maxs[kind][axis] = Float:ReadStudioInt(file, 100 + axis * 4);
            }
        }
        fclose(file);
        if (!ok) { set_fail_state("Invalid creature model sequence or sound"); return; }
        precache_model(Models[kind]);
    }
}

public plugin_init()
{
    register_plugin(CREATURE_PLUGIN, "1.1", "GoldCraft contributors");
    if (csol_game_mode_current() != _:CSOL_MODE_SCENARIO) return;
    register_clcmd("amx_creature_test", "OpenMenu", ADMIN_CFG);
    register_forward(FM_CmdStart, "PlacementInput");
    RegisterHam(Ham_SetObjectCollisionBox, "info_target", "TargetAbsBox", true);
    RegisterHam(Ham_TakeDamage, "info_target", "TargetDamage");
    RegisterHam(Ham_Killed, "player", "PlayerKilled", true);
    register_event("HLTV", "RoundReset", "a", "1=0", "2=0");
    g_Trace = create_tr2();
    g_MaxEdicts = global_get(glb_maxEntities);
}

bool:Allowed(id)
{
    return id > 0 && id <= MaxClients && is_user_alive(id) && (get_user_flags(id) & ADMIN_CFG) &&
        get_member(id, m_iJoiningState) == JOINED && csol_game_mode_current() == _:CSOL_MODE_SCENARIO;
}

CloseMenu(id)
{
    new menu = g_Menu[id];
    g_Menu[id] = -1;
    if (menu >= 0) menu_destroy(menu);
}

public OpenMenu(id)
{
    if (!Allowed(id))
    {
        if (id && is_user_connected(id)) client_print(id, print_chat, "[模型测试] 请以存活的管理员身份放置模型。");
        return PLUGIN_HANDLED;
    }
    CancelGhost(id);
    CloseMenu(id);
    new menu = menu_create("模型生成测试^n选择模型后，用准星定位", "SelectModel");
    g_Menu[id] = menu;
    g_Userid[id] = get_user_userid(id);
    for (new i; i < KindCount; i++) menu_additem(menu, Names[i]);
    menu_additem(menu, "清除我放置的测试模型");
    menu_setprop(menu, MPROP_EXITNAME, "关闭");
    menu_display(id, menu);
    return PLUGIN_HANDLED;
}

public SelectModel(id, menu, item)
{
    if (g_Menu[id] != menu) return PLUGIN_HANDLED;
    CloseMenu(id);
    if (item < 0 || !Allowed(id) || g_Userid[id] != get_user_userid(id)) return PLUGIN_HANDLED;
    if (item == KindCount) { RemoveTargets(g_Userid[id]); return PLUGIN_HANDLED; }
    if (item >= KindCount) return PLUGIN_HANDLED;
    if (engfunc(EngFunc_NumberOfEntities) >= g_MaxEdicts-8)
    {
        client_print(id, print_chat, "[模型测试] 实体空间不足，请先清除测试模型。");
        return PLUGIN_HANDLED;
    }
    new ent = rg_create_entity("info_target");
    if (is_nullent(ent)) return PLUGIN_HANDLED;
    set_entvar(ent, var_classname, GHOST);
    set_entvar(ent, var_owner, id);
    set_entvar(ent, var_effects, EF_OWNER_VISIBILITY);
    set_entvar(ent, var_movetype, MOVETYPE_NONE);
    set_entvar(ent, var_solid, SOLID_NOT);
    set_entvar(ent, var_takedamage, DAMAGE_NO);
    set_entvar(ent, var_rendermode, kRenderTransAlpha);
    set_entvar(ent, var_renderamt, 105.0);
    set_entvar(ent, var_iuser1, item);
    set_entvar(ent, var_iuser3, get_user_userid(id));
    engfunc(EngFunc_SetModel, ent, Models[item]);
    set_entvar(ent, var_sequence, g_Tracks[item][Rest][ClipIndex]);
    set_entvar(ent, var_frame, 0.0);
    set_entvar(ent, var_framerate, 0.0);
    g_Ghost[id] = ent;
    g_LastButtons[id] = AttackKeys;
    g_NextTrace[id] = 0.0;
    new Float:view[3];
    get_entvar(id, var_v_angle, view);
    PositionGhost(id, ent, view);
    client_print(id, print_center, "%s  |  左键放置  右键取消", Names[item]);
    client_print(id, print_chat, "[模型测试] 移动准星选择地面。左键确认，右键取消。");
    return PLUGIN_HANDLED;
}

bool:OwnGhost(id, ent)
{
    return !is_nullent(ent) && FClassnameIs(ent, GHOST) && get_entvar(ent, var_owner) == id &&
        get_entvar(ent, var_iuser3) == g_Userid[id];
}

CancelGhost(id)
{
    if (g_Ghost[id] && OwnGhost(id, g_Ghost[id])) engfunc(EngFunc_RemoveEntity, g_Ghost[id]);
    g_Ghost[id] = 0;
}

RemoveTargets(userid)
{
    new ent = -1;
    while ((ent = engfunc(EngFunc_FindEntityByString, ent, "classname", ACTOR)) > 0)
        if (!userid || get_entvar(ent, var_iuser3) == userid)
            engfunc(EngFunc_RemoveEntity, ent);
}

public PlayerKilled(id) { CancelGhost(id); }
public client_disconnected(id)
{
    if (g_Userid[id]) RemoveTargets(g_Userid[id]);
    CancelGhost(id);
    CloseMenu(id);
    g_Userid[id] = 0;
    g_Suppress[id] = false;
}

public RoundReset()
{
    for (new id=1; id<=MaxClients; id++) CancelGhost(id);
    RemoveTargets(0);
}

public plugin_end()
{
    if (!g_Trace) return;
    RoundReset();
    for (new id=1; id<=MaxClients; id++) CloseMenu(id);
    free_tr2(g_Trace);
}

// ReHLDS links absmin/absmax into the area tree before testing individual Studio
// hitboxes. PMove reads mins/maxs separately for the actual blocking hull.
public TargetAbsBox(ent)
{
    if (!FClassnameIs(ent, ACTOR) && !FClassnameIs(ent, GHOST)) return HAM_IGNORED;
    new kind = get_entvar(ent, var_iuser1);
    new Float:p[3], Float:lo[3], Float:hi[3], Float:r = g_ClipRadius[kind];
    get_entvar(ent, var_origin, p);
    for (new axis; axis < 2; axis++)
    {
        lo[axis] = p[axis] - r;
        hi[axis] = p[axis] + r;
    }
    lo[2] = p[2] + g_ClipBottom[kind];
    hi[2] = p[2] + g_ClipTop[kind];
    set_entvar(ent, var_absmin, lo);
    set_entvar(ent, var_absmax, hi);
    return HAM_IGNORED;
}

RotatedBounds(ent, kind, Float:yaw)
{
    new Float:c = floatcos(yaw, degrees), Float:s = floatsin(yaw, degrees);
    new Float:mins[3], Float:maxs[3];
    for (new a; a<2; a++)
    {
        new Float:u = a ? s : c, Float:v = a ? c : -s;
        mins[a] = floatmin(u*g_Mins[kind][0], u*g_Maxs[kind][0]) + floatmin(v*g_Mins[kind][1], v*g_Maxs[kind][1]);
        maxs[a] = floatmax(u*g_Mins[kind][0], u*g_Maxs[kind][0]) + floatmax(v*g_Mins[kind][1], v*g_Maxs[kind][1]);
    }
    maxs[2] = g_Maxs[kind][2];
    engfunc(EngFunc_SetSize, ent, mins, maxs);
}

bool:PositionGhost(id, ent, const Float:view[3])
{
    new Float:eye[3], Float:offset[3], Float:direction[3], Float:end[3], Float:point[3], Float:normal[3];
    get_entvar(id, var_origin, eye);
    get_entvar(id, var_view_ofs, offset);
    xs_vec_add(eye, offset, eye);
    angle_vector(view, ANGLEVECTOR_FORWARD, direction);
    xs_vec_mul_scalar(direction, 2048.0, end);
    xs_vec_add(eye, end, end);
    rg_trace_line(eye, end, DONT_IGNORE_MONSTERS, id, g_Trace);
    get_tr2(g_Trace, TR_vecEndPos, point);
    get_tr2(g_Trace, TR_vecPlaneNormal, normal);
    new Float:fraction;
    get_tr2(g_Trace, TR_flFraction, fraction);
    new bool:supported = fraction < 1.0 && normal[2] >= 0.70 && !get_tr2(g_Trace, TR_StartSolid);
    new hit = get_tr2(g_Trace, TR_pHit);
    if (hit > 0 && (hit <= MaxClients || get_entvar(hit, var_solid) != SOLID_BSP)) supported = false;
    new Float:angles[3];
    angles[1] = view[1]+180.0;
    point[2] += 1.0;
    set_entvar(ent, var_angles, angles);
    new kind = get_entvar(ent, var_iuser1);
    RotatedBounds(ent, kind, angles[1]);
    engfunc(EngFunc_SetOrigin, ent, point);
    if (supported)
    {
        // Skip only the non-solid preview. The footprint must also clear its placer.
        engfunc(EngFunc_TraceMonsterHull, ent, point, point, DONT_IGNORE_MONSTERS, 0, g_Trace);
        supported = !get_tr2(g_Trace, TR_StartSolid) && !get_tr2(g_Trace, TR_AllSolid);
    }
    // BSP player hulls have fixed dimensions. Check headroom explicitly for a tall target.
    if (supported && kind == Siren)
    {
        new Float:top[3];
        xs_vec_copy(point, top);
        top[2] += g_ClipTop[kind];
        rg_trace_line(point, top, IGNORE_MONSTERS, id, g_Trace);
        get_tr2(g_Trace, TR_flFraction, fraction);
        supported = fraction >= 1.0 && !get_tr2(g_Trace, TR_StartSolid);
    }
    new Float:alpha = supported ? 116.0 : 55.0;
    set_entvar(ent, var_renderamt, alpha);
    return supported;
}

public PlacementInput(id, cmd)
{
    if (!g_Ghost[id] && !g_Suppress[id]) return FMRES_IGNORED;
    new buttons = get_uc(cmd, UC_Buttons), down = buttons & AttackKeys;
    set_uc(cmd, UC_Buttons, buttons & ~AttackKeys);
    if (!g_Ghost[id])
    {
        if (!down) g_Suppress[id] = false;
        return FMRES_HANDLED;
    }
    new ent = g_Ghost[id];
    if (!Allowed(id) || get_user_userid(id) != g_Userid[id] || !OwnGhost(id, ent))
    {
        CancelGhost(id);
        return FMRES_HANDLED;
    }
    new pressed = down & ~g_LastButtons[id];
    g_LastButtons[id] = down;
    if (pressed & IN_ATTACK2)
    {
        g_Suppress[id] = true;
        CancelGhost(id);
        client_print(id, print_center, "已取消放置");
        return FMRES_HANDLED;
    }
    new Float:now = get_gametime();
    if (!(pressed & IN_ATTACK) && now < g_NextTrace[id]) return FMRES_HANDLED;
    g_NextTrace[id] = now+0.04;
    new Float:view[3];
    get_uc(cmd, UC_ViewAngles, view);
    new bool:valid = PositionGhost(id, ent, view);
    if (pressed & IN_ATTACK)
    {
        if (!valid) client_print(id, print_center, "当前位置无法放置，请瞄准空旷地面");
        else
        {
            g_Ghost[id] = 0;
            g_Suppress[id] = true;
            SpawnTarget(ent);
            client_print(id, print_center, "已放置 %s", Names[get_entvar(ent, var_iuser1)]);
        }
    }
    return FMRES_HANDLED;
}

// Actor fields: iuser1 model, iuser2 trackState, iuser3 placer userid, iuser4 next sound.
// fuser1 sequence start, fuser2 idle yaw, fuser3 signed hit recoil, fuser4 recoil start.
SpawnTarget(ent)
{
    set_entvar(ent, var_classname, ACTOR);
    set_entvar(ent, var_owner, 0);
    set_entvar(ent, var_effects, 0);
    set_entvar(ent, var_rendermode, kRenderNormal);
    set_entvar(ent, var_renderamt, 255.0);
    set_entvar(ent, var_health, TestHealth);
    set_entvar(ent, var_max_health, TestHealth);
    set_entvar(ent, var_takedamage, DAMAGE_YES);
    set_entvar(ent, var_solid, SOLID_SLIDEBOX);
    new Float:p[3], Float:angles[3];
    get_entvar(ent, var_origin, p);
    get_entvar(ent, var_angles, angles);
    set_entvar(ent, var_fuser2, angles[1]);
    engfunc(EngFunc_SetOrigin, ent, p);
    SetThink(ent, "ActorThink");
    StartTrack(ent, Rest);
}

StartTrack(ent, trackState)
{
    new kind = get_entvar(ent, var_iuser1);
    new Float:now = get_gametime();
    set_entvar(ent, var_iuser2, trackState);
    set_entvar(ent, var_iuser4, 0);
    set_entvar(ent, var_fuser1, now);
    set_entvar(ent, var_sequence, g_Tracks[kind][trackState][ClipIndex]);
    set_entvar(ent, var_frame, 0.0);
    set_entvar(ent, var_animtime, now);
    if (kind == Siren && trackState == Rest)
    {
        set_entvar(ent, var_framerate, 0.0);
        set_entvar(ent, var_nextthink, 0.0);
        return;
    }
    set_entvar(ent, var_framerate, 1.0);
    ActorThink(ent);
}

PlayDueCues(ent, kind, trackState, Float:elapsed)
{
    new first = g_Tracks[kind][trackState][ClipFirstCue], count = g_Tracks[kind][trackState][ClipCueCount];
    new cursor = get_entvar(ent, var_iuser4);
    while (cursor < count && g_Cues[first+cursor][CueTime] <= elapsed)
    {
        new cue = first+cursor++;
        emit_sound(ent, g_Cues[cue][CueChannel], g_Cues[cue][CueSample], 1.0, ATTN_NORM, 0, PITCH_NORM);
    }
    set_entvar(ent, var_iuser4, cursor);
}

public ActorThink(ent)
{
    new kind = get_entvar(ent, var_iuser1), trackState = get_entvar(ent, var_iuser2);
    new Float:now = get_gametime(), Float:start = get_entvar(ent, var_fuser1);
    new Float:elapsed = floatmax(0.0, now-start), Float:duration = g_Tracks[kind][trackState][ClipDuration];
    if (trackState == Rest && kind == Reptile && elapsed >= duration)
    {
        new cycles = floatround(elapsed/duration, floatround_floor);
        start += float(cycles)*duration;
        elapsed = floatmax(0.0, now-start);
        set_entvar(ent, var_fuser1, start);
        set_entvar(ent, var_iuser4, 0);
    }
    PlayDueCues(ent, kind, trackState, elapsed);
    if (kind == Reptile && trackState == Rest) ApplyRecoil(ent, now);
    if (elapsed >= duration && trackState == Hurt) { StartTrack(ent, Rest); return; }
    if (elapsed >= duration && trackState == Dying)
    {
        if (elapsed >= duration+CorpseHold) { engfunc(EngFunc_RemoveEntity, ent); return; }
        set_entvar(ent, var_frame, 255.0);
        set_entvar(ent, var_animtime, now);
        set_entvar(ent, var_framerate, 0.0);
        set_entvar(ent, var_nextthink, start+duration+CorpseHold+0.01);
        return;
    }
    set_entvar(ent, var_frame, floatmin(255.0, elapsed*256.0/duration));
    set_entvar(ent, var_animtime, now);
    set_entvar(ent, var_nextthink, now+0.05);
}

ApplyRecoil(ent, Float:now)
{
    new Float:strength = get_entvar(ent, var_fuser3);
    if (strength == 0.0) return;
    new Float:t = floatclamp((now-Float:get_entvar(ent, var_fuser4))/RecoilTime, 0.0, 1.0);
    new Float:angles[3];
    get_entvar(ent, var_angles, angles);
    angles[1] = Float:get_entvar(ent, var_fuser2) + strength*floatsin(t*3.14159265)*(1.0-t);
    set_entvar(ent, var_angles, angles);
    if (t >= 1.0) set_entvar(ent, var_fuser3, 0.0);
}

public TargetDamage(ent, inflictor, attacker, Float:damage, damageBits)
{
    if (!FClassnameIs(ent, ACTOR)) return HAM_IGNORED;
    if (get_entvar(ent, var_iuser2) == Dying || !(damage > 0.0)) return HAM_SUPERCEDE;
    new Float:remaining = floatmax(0.0, Float:get_entvar(ent, var_health)-damage);
    set_entvar(ent, var_health, remaining);
    if (remaining <= 0.0)
    {
        set_entvar(ent, var_takedamage, DAMAGE_NO);
        set_entvar(ent, var_solid, SOLID_NOT);
        set_entvar(ent, var_deadflag, DEAD_DEAD);
        new Float:pos[3];
        get_entvar(ent, var_origin, pos);
        engfunc(EngFunc_SetOrigin, ent, pos);
        StartTrack(ent, Dying);
    }
    else if (get_entvar(ent, var_iuser1) == Siren)
    {
        // Let sustained fire play through the flinch instead of pinning it to frame zero.
        if (get_entvar(ent, var_iuser2) != Hurt) StartTrack(ent, Hurt);
    }
    else
    {
        new Float:angles[3], Float:right[3], Float:p[3], Float:source[3], Float:direction[3];
        get_entvar(ent, var_angles, angles);
        angle_vector(angles, ANGLEVECTOR_RIGHT, right);
        get_entvar(ent, var_origin, p);
        if (!is_nullent(attacker)) get_entvar(attacker, var_origin, source);
        else xs_vec_copy(p, source);
        xs_vec_sub(source, p, direction);
        set_entvar(ent, var_fuser3, (xs_vec_dot(direction, right) >= 0.0) ? -12.0 : 12.0);
        set_entvar(ent, var_fuser4, get_gametime());
    }
    if (attacker > 0 && attacker <= MaxClients && is_user_connected(attacker))
        client_print(attacker, print_center, "%s   %.0f / %.0f HP", Names[get_entvar(ent, var_iuser1)], remaining, TestHealth);
    SetHamReturnInteger(1);
    return HAM_SUPERCEDE;
}
