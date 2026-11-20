// SPDX-License-Identifier: GPL-3.0-or-later
// Local fixture: inspect real inventory and exercise the actual purchase command.
#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>

public plugin_init()
{
    register_plugin("CoF local purchase checks", "0.1.0", "GoldCraft contributors");
    register_srvcmd("cof_live_status", "Status");
    register_srvcmd("cof_live_purchase", "Purchase");
    register_srvcmd("cof_live_famas_rays", "ArmFamasRays");
    register_srvcmd("cof_live_glock", "GlockActionState");
    register_srvcmd("cof_live_clear_batch", "ClearBatch");
    register_srvcmd("cof_live_round_protect", "RoundProtect");
    register_srvcmd("cof_live_test_funds", "TestFunds");
    register_srvcmd("cof_live_burn_setup", "BurnSetup");
    register_srvcmd("cof_live_flare_scene", "FlareScene");
    register_srvcmd("cof_live_burn_state", "BurnState");
    register_srvcmd("cof_live_join", "Join");
    register_srvcmd("cof_live_burn_wave", "BurnWave");
    register_srvcmd("cof_live_burn_teammate", "BurnTeammate");
    register_srvcmd("cof_live_flashlight_scene", "FlashlightScene");
    RegisterHookChain(RG_CBasePlayer_Spawn, "TeammateSpawned", true);
}

// Enable this observer only on request. Keep the first actual engine trace of
// each shot, before material penetration traces start at a different surface.
new rayPlayer, rayUserid, raysLeft, raySequence, tracingShot;
new Float:raySource[3];
new bool:rayHooksInstalled;

public ArmFamasRays()
{
    new slot[12], user[12], address[32];
    read_argv(1, slot, charsmax(slot));
    read_argv(2, user, charsmax(user));
    new id = str_to_num(slot), userid = str_to_num(user);
    if (read_argc() != 3 || !Human(id, userid) || !is_user_alive(id)) return PLUGIN_HANDLED;
    get_user_ip(id, address, charsmax(address), 1);
    if (!equal(address, "127.0.0.1")) return PLUGIN_HANDLED;
    new weapon = get_member(id, m_pActiveItem);
    if (is_nullent(weapon) || get_member(weapon, m_iId) != 905) return PLUGIN_HANDLED;
    if (!rayHooksInstalled)
    {
        RegisterHookChain(RG_CBaseEntity_FireBullets3, "FamasShotBegin");
        RegisterHookChain(RG_CBaseEntity_FireBullets3, "FamasShotEnd", true);
        register_forward(FM_TraceLine, "FamasFirstTrace", 1);
        rayHooksInstalled = true;
    }
    rayPlayer = id;
    rayUserid = userid;
    raysLeft = 64;
    server_print("[cof-rays] armed player=%d userid=%d", id, userid);
    return PLUGIN_HANDLED;
}

public FamasShotBegin(id, Float:source[3], Float:dir[3], Float:spread, Float:distance,
    penetration, bulletType, damage, Float:modifier, attacker, bool:pistol, seed)
{
    if (!raysLeft || id != rayPlayer || attacker != id || !Human(id, rayUserid)) return HC_CONTINUE;
    new weapon = get_member(id, m_pActiveItem);
    if (is_nullent(weapon) || get_member(weapon, m_iId) != 905) return HC_CONTINUE;
    --raysLeft;
    tracingShot = ++raySequence;
    raySource = source;
    new Float:fov = get_entvar(id, var_fov);
    server_print("[cof-rays] shot=%d t=%.4f seed=%d clip=%d fov=%.1f damage=%d src=%.4f,%.4f,%.4f dir=%.7f,%.7f,%.7f",
        tracingShot, get_gametime(), seed, get_member(weapon, m_Weapon_iClip), fov, damage,
        source[0], source[1], source[2], dir[0], dir[1], dir[2]);
    return HC_CONTINUE;
}

public FamasFirstTrace(const Float:start[3], const Float:end[3], flags, skip, trace)
{
    if (!tracingShot || skip != rayPlayer || get_distance_f(start, raySource) > 0.01) return FMRES_IGNORED;
    new Float:point[3], Float:fraction;
    get_tr2(trace, TR_vecEndPos, point);
    get_tr2(trace, TR_flFraction, fraction);
    server_print("[cof-rays] trace=%d hit=%d fraction=%.6f end=%.4f,%.4f,%.4f",
        tracingShot, get_tr2(trace, TR_pHit), fraction, point[0], point[1], point[2]);
    tracingShot = 0;
    return FMRES_IGNORED;
}

public FamasShotEnd()
{
    tracingShot = 0;
    return HC_CONTINUE;
}

// The observer only runs in the isolated fixture with a real Glock owner.
new glockOwner, glockUserid, glockItem, glockItemSerial, glockTarget, glockTargetSerial;
new glockShots, glockSounds, glockHits, glockButtons;
new bool:glockObserved;
new HamHook:glockDamageHook;

bool:ObservedGlock(id)
{
    return id == glockOwner && Human(id, glockUserid) && is_user_alive(id) &&
        get_member(id, m_pActiveItem) == glockItem && pev_valid(glockItem) == 2 &&
        pev_serial(glockItem) == glockItemSerial;
}

public GlockInput(id, cmd, seed)
{
    if (!ObservedGlock(id)) return FMRES_IGNORED;
    new buttons = get_uc(cmd, UC_Buttons) & (IN_ATTACK | IN_ATTACK2 | 0x40);
    if (buttons != glockButtons)
    {
        glockButtons = buttons;
        server_print("[cof-glock] input t=%.3f bits=%d", get_gametime(), buttons);
    }
    return FMRES_IGNORED;
}

public GlockPlayback(flags, id, event, Float:delay, const Float:origin[3], const Float:angles[3],
    Float:f1, Float:f2, originalWeapon, item, b1, b2)
{
    if (originalWeapon == 3 && item == glockItem && ObservedGlock(id))
        server_print("[cof-glock] muzzle t=%.3f shot=%d owner=%d item=%d event=%d flags=%d",
            get_gametime(), ++glockShots, id, item, event, flags);
    return FMRES_IGNORED;
}

public GlockLocalSound(id)
{
    // read_datanum includes the AMXX destination at slot zero.
    if (!ObservedGlock(id) || read_datanum() != 10 || read_data(1) != 1 ||
        read_data(4) || read_data(8) != id) return;
    new sample[96];
    read_data(9, sample, charsmax(sample));
    if (!equal(sample, "weapons_new/glock/glock_fire.wav")) return;
    server_print("[cof-glock] local-sound t=%.3f packet=%d owner=%d sample=%s",
        get_gametime(), ++glockSounds, id, sample);
}

public GlockNativeSound(id, channel, const sample[], Float:volume, Float:attenuation, flags, pitch)
{
    if (ObservedGlock(id) && contain(sample, "guest/cof/") == 0)
        server_print("[cof-glock] native-sound t=%.3f channel=%d sample=%s",
            get_gametime(), channel, sample);
    return FMRES_IGNORED;
}

public GlockTargetDamaged(victim, inflictor, attacker, Float:amount, bits)
{
    if (victim == glockTarget && pev_valid(victim) == 2 && pev_serial(victim) == glockTargetSerial &&
        ObservedGlock(attacker))
        server_print("[cof-glock] hit t=%.3f count=%d victim=%d serial=%d attacker=%d inflictor=%d amount=%.3f bits=%d hp=%.3f",
            get_gametime(), ++glockHits, victim, glockTargetSerial, attacker, inflictor, amount, bits,
            Float:get_entvar(victim, var_health));
    return HAM_IGNORED;
}

public GlockActionState()
{
    if (read_argc() != 4 || !IsolatedCoFMap()) return PLUGIN_HANDLED;
    new id = read_argv_int(1), userid = read_argv_int(2), action[12], address[32];
    if (!Human(id, userid) || !is_user_alive(id)) return PLUGIN_HANDLED;
    get_user_ip(id, address, charsmax(address), 1);
    if (!equal(address, "127.0.0.1")) return PLUGIN_HANDLED;
    new item = get_member(id, m_pActiveItem);
    if (is_nullent(item) || get_member(item, m_iId) != 896) return PLUGIN_HANDLED;
    read_argv(3, action, charsmax(action));
    if (equal(action, "arm"))
    {
        if (!glockObserved)
        {
            if (!get_user_msgid("CoFSFX")) return PLUGIN_HANDLED;
            register_forward(FM_CmdStart, "GlockInput");
            register_forward(FM_PlaybackEvent, "GlockPlayback", 1);
            register_forward(FM_EmitSound, "GlockNativeSound", 1);
            register_event_ex("CoFSFX", "GlockLocalSound", RegisterEvent_Single);
            glockObserved = true;
        }
        glockOwner = id; glockUserid = userid; glockItem = item; glockItemSerial = pev_serial(item);
        glockShots = glockSounds = glockHits = glockButtons = 0;
        glockTarget = glockTargetSerial = 0;
    }
    else if (!ObservedGlock(id)) return PLUGIN_HANDLED;
    else if (equal(action, "target"))
    {
        new entity;
        while ((entity = rg_find_ent_by_class(entity, "monster_entity")) > 0)
            if (get_entvar(entity, var_deadflag) == DEAD_NO && Float:get_entvar(entity, var_health) > 0.0 &&
                Float:get_entvar(entity, var_takedamage) != DAMAGE_NO && !(get_entvar(entity, var_effects) & EF_NODRAW)) break;
        if (entity <= 0 || glockTarget || !PlaceStandingPlayer(id, Float:{-2000.0, 1256.0, 36.03125})) return PLUGIN_HANDLED;
        new Float:eye[3], Float:offset[3], Float:lo[3], Float:hi[3], Float:at[3];
        get_entvar(id, var_origin, eye); get_entvar(id, var_view_ofs, offset);
        for (new i; i < 3; ++i) eye[i] += offset[i];
        // Place the near face of the real monster inside the 32-unit trace.
        get_entvar(entity, var_mins, lo); get_entvar(entity, var_maxs, hi);
        at[0] = eye[0] + 24.0 - lo[0];
        at[1] = eye[1] - (lo[1] + hi[1]) * 0.5;
        at[2] = eye[2] - (lo[2] + hi[2]) * 0.5;
        set_entvar(entity, var_nextthink, 0.0);
        set_entvar(entity, var_movetype, MOVETYPE_NONE);
        set_entvar(entity, var_velocity, Float:{0.0, 0.0, 0.0});
        engfunc(EngFunc_SetOrigin, entity, at);
        glockTarget = entity; glockTargetSerial = pev_serial(entity);
        if (!glockDamageHook) glockDamageHook = RegisterHamFromEntity(Ham_TakeDamage, entity, "GlockTargetDamaged", 1);
    }
    else if (!equal(action, "state")) return PLUGIN_HANDLED;
    new Float:health = -1.0;
    if (glockTarget && pev_valid(glockTarget) == 2 && pev_serial(glockTarget) == glockTargetSerial)
        health = get_entvar(glockTarget, var_health);
    server_print("[cof-glock] t=%.3f owner=%d uid=%d item=%d clip=%d fov=%.1f anim=%d shots=%d sounds=%d hits=%d target=%d serial=%d hp=%.3f",
        get_gametime(), id, userid, item, get_member(item, m_Weapon_iClip), Float:get_entvar(id, var_fov),
        get_entvar(id, var_weaponanim), glockShots, glockSounds, glockHits, glockTarget, glockTargetSerial, health);
    return PLUGIN_HANDLED;
}

// The flare and monsters come from normal play. The map BSP blocks target 1.
// Credit mode adds two exposed targets that die on different damage pulses.
new const Float:burnCenters[][3] = {
    {-2000.0, 1248.0, 36.0}, {-2000.0, 1368.0, 36.0}, {-1860.0, 1288.0, 36.0},
    {-2040.0, 1256.0, 36.0}, {-1960.0, 1256.0, 36.0}
};
new const Float:burnHealth[] = {1000.0, 1000.0, 1000.0, 5.0, 65.0};
new burnFlare, burnSerial, burnCount, burnTargets[sizeof burnCenters], burnTargetSerial[sizeof burnCenters];
new burnHits[sizeof burnCenters], bool:burnDead[sizeof burnCenters];
new HamHook:burnHook;
new const Float:burnOrigin[3] = {-2000.0, 1288.0, 0.03125};
enum { Lamp, Walker, SceneActors };
new actorSlot[SceneActors], actorUserid[SceneActors], actorSerial[SceneActors];
const TeammateMoveTask = 31871;
new bool:walkerMoving;
new Float:walkerDestination;

bool:OwnsActor(which)
{
    new id = actorSlot[which];
    return id > 0 && is_user_connected(id) && actorUserid[which] > 0 &&
        get_user_userid(id) == actorUserid[which] && pev_valid(id) && pev_serial(id) == actorSerial[which];
}

public client_disconnected(id)
{
    for (new which; which < SceneActors; which++)
    {
        if (actorSlot[which] != id) continue;
        remove_task(TeammateMoveTask + which);
        if (which == Walker) walkerMoving = false;
        actorSlot[which] = actorUserid[which] = actorSerial[which] = 0;
    }
}

public plugin_end()
{
    for (new which; which < SceneActors; which++)
        if (OwnsActor(which)) rh_drop_client(actorSlot[which], "CoF fixture finished");
}

public TeammateSpawned(id)
{
    if (pev_valid(id) != 2 || !IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn)) return HC_CONTINUE;
    for (new which; which < SceneActors; which++)
    {
        if (id != actorSlot[which] || !OwnsActor(which)) continue;
        set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT | FL_CLIENT);
        if (get_member(id, m_iTeam) == TEAM_CT && is_user_alive(id))
            set_entvar(id, var_takedamage, DAMAGE_NO);
    }
    return HC_CONTINUE;
}

public MoveTeammate(task)
{
    new which = task - TeammateMoveTask;
    new id = actorSlot[which];
    if (!OwnsActor(which) || pev_valid(id) != 2 || !is_user_bot(id)) return;
    new Float:angles[3];
    get_entvar(id, var_v_angle, angles);
    new Float:speed;
    if (which == Walker && walkerMoving)
    {
        new Float:position[3];
        get_entvar(id, var_origin, position);
        new Float:remaining = walkerDestination - position[0];
        if (floatabs(remaining) < 1.0)
        {
            walkerMoving = false;
            set_entvar(id, var_velocity, Float:{0.0, 0.0, 0.0});
        }
        else
            speed = floatclamp(remaining * 4.0, -140.0, 140.0);
        angles[0] = angles[1] = angles[2] = 0.0;
    }
    engfunc(EngFunc_RunPlayerMove, id, angles, speed, 0.0, 0.0, 0, 0, 100);
}

bool:IsolatedCoFMap()
{
    new map[32], host[64], ip[32];
    get_mapname(map, charsmax(map));
    get_cvar_string("hostname", host, charsmax(host));
    get_cvar_string("ip", ip, charsmax(ip));
    return equal(map, "zs_deadend") && get_cvar_num("csol_game_mode") == 2 &&
        equal(ip, "127.0.0.1") && equal(host, "GoldCraft isolated CoF cold");
}

AddFixtureActor(which, const name[])
{
    if (actorSlot[which]) return;
    new id = engfunc(EngFunc_CreateFakeClient, name);
    if (id < 1 || id > MaxClients || !is_user_connected(id) || !is_user_bot(id)) return;
    if (pev_valid(id) == 2) engfunc(EngFunc_FreeEntPrivateData, id);
    actorSlot[which] = id;
    actorUserid[which] = get_user_userid(id);
    actorSerial[which] = pev_serial(id);
    set_pev(id, pev_flags, pev(id, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
    dllfunc(DLLFunc_ClientPutInServer, id);
    if (!OwnsActor(which) || pev_valid(id) != 2) return;
    set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    set_member(id, m_iMenu, Menu_ChooseTeam);
    set_member(id, m_iJoiningState, PICKINGTEAM);
    engclient_cmd(id, "jointeam", "2");
    if (!OwnsActor(which) || get_member(id, m_iTeam) != TEAM_CT ||
        get_member(id, m_iMenu) != Menu_ChooseAppearance) return;
    engclient_cmd(id, "joinclass", "1");
    if (OwnsActor(which)) set_task(0.1, "MoveTeammate", TeammateMoveTask + which, _, _, "b");
}

public BurnTeammate()
{
    if (read_argc() == 1 && IsolatedCoFMap()) AddFixtureActor(Lamp, "CoF_Flare_Teammate");
    return PLUGIN_HANDLED;
}

// Fixed side view. The holder uses the same select command and impulse as a human.
// The second CT can occupy the beam while its body stays clear of the sampled wall.
bool:PlaceStandingPlayer(id, const Float:position[3])
{
    new trace = create_tr2();
    engfunc(EngFunc_TraceHull, position, position, DONT_IGNORE_MONSTERS, HULL_HUMAN, id, trace);
    new blocked = get_tr2(trace, TR_StartSolid) || get_tr2(trace, TR_AllSolid);
    free_tr2(trace);
    if (blocked) return false;
    set_entvar(id, var_velocity, Float:{0.0, 0.0, 0.0});
    engfunc(EngFunc_SetOrigin, id, position);
    return true;
}

// Observe the actual player delta at the end of AddToFullPack, on request.
new wireForward, ownerForward, wireActor, wireUserid, wireFrames, wireWeapon, wireBits;
new ownerFrames, ownerView, ownerBits;
public ObserveLampOwner(receiver, sendweapons, packet)
{
    if (receiver == wireActor && get_user_userid(receiver) == wireUserid)
    {
        ownerView = get_cd(packet, CD_ViewModel);
        ownerBits = get_cd(packet, CD_iUser4);
        ++ownerFrames;
    }
    return FMRES_IGNORED;
}
public ObserveLampPack(packet, number, entity, receiver, flags, player, pvs)
{
    if (player && entity == wireActor && receiver == wireActor && number == entity &&
        get_orig_retval() && get_user_userid(entity) == wireUserid)
    {
        wireWeapon = get_es(packet, ES_WeaponModel);
        wireBits = get_es(packet, ES_iUser4);
        ++wireFrames;
    }
    return FMRES_IGNORED;
}

new lampBuyUserid, Float:lampBuyOrigin[3];
public FlashlightScene()
{
    if (!IsolatedCoFMap()) return PLUGIN_HANDLED;
    new action[12];
    read_argv(1, action, charsmax(action));
    if (read_argc() == 2 && equal(action, "actors"))
    {
        AddFixtureActor(Lamp, "CoF_Flare_Teammate");
        AddFixtureActor(Walker, "CoF_Light_Walker");
        return PLUGIN_HANDLED;
    }
    if (read_argc() != 4) return PLUGIN_HANDLED;
    new slot[12], user[12], address[32];
    read_argv(2, slot, charsmax(slot)); read_argv(3, user, charsmax(user));
    new id = str_to_num(slot);
    if (!Human(id, str_to_num(user)) || !is_user_alive(id)) return PLUGIN_HANDLED;
    get_user_ip(id, address, charsmax(address), 1);
    if (!equal(address, "127.0.0.1")) return PLUGIN_HANDLED;
    for (new which; which < SceneActors; which++)
    {
        if (!OwnsActor(which) || !is_user_alive(actorSlot[which]) ||
            get_member(actorSlot[which], m_iTeam) != TEAM_CT)
        {
            server_print("[lamp-scene] actor %d unavailable slot=%d", which, actorSlot[which]);
            return PLUGIN_HANDLED;
        }
    }
    if (equal(action, "wire"))
    {
        if (!wireForward)
            wireForward = register_forward(FM_AddToFullPack, "ObserveLampPack", 1);
        if (!ownerForward)
            ownerForward = register_forward(FM_UpdateClientData, "ObserveLampOwner", 1);
        if (wireActor != id || wireUserid != get_user_userid(id))
        {
            wireActor = id;
            wireUserid = get_user_userid(id);
            wireFrames = wireWeapon = wireBits = 0;
            ownerFrames = ownerView = ownerBits = 0;
        }
        server_print("[lamp-wire] actor=%d receiver=%d userid=%d frames=%d weapon=%d bits=%d",
            id, id, wireUserid, wireFrames, wireWeapon, wireBits);
        server_print("[lamp-owner] frames=%d view=%d bits=%d", ownerFrames, ownerView, ownerBits);
        return PLUGIN_HANDLED;
    }
    if (equal(action, "savebuy"))
    {
        lampBuyUserid = get_user_userid(id);
        get_entvar(id, var_origin, lampBuyOrigin);
    }
    else if (equal(action, "returnbuy"))
    {
        if (get_user_userid(id) != lampBuyUserid || !PlaceStandingPlayer(id, lampBuyOrigin))
            return PLUGIN_HANDLED;
    }
    new lamp = actorSlot[Lamp], walker = actorSlot[Walker];
    new const Float:aim[3] = {0.0, -90.0, 0.0};
    if (equal(action, "pose"))
    {
        if (!PlaceStandingPlayer(id, Float:{-1824.0, 1256.0, 36.03125}) ||
            !PlaceStandingPlayer(lamp, Float:{-2020.0, 1300.0, 36.03125}) ||
            !PlaceStandingPlayer(walker, Float:{-2144.0, 1200.0, 36.03125}))
        {
            server_print("[lamp-scene] occupied pose");
            return PLUGIN_HANDLED;
        }
        walkerMoving = false;
        // Real clients acknowledge the spawn angle. Server-only actors never receive that message.
        // ReHLDS copies usercmd angles into both v_angle and replicated angles only with fixangle clear.
        set_entvar(lamp, var_fixangle, 0);
        set_entvar(walker, var_fixangle, 0);
        set_entvar(lamp, var_v_angle, aim);
        engfunc(EngFunc_RunPlayerMove, lamp, aim, 0.0, 0.0, 0.0, 0, 0, 100);
    }
    else if (equal(action, "near"))
    {
        if (!PlaceStandingPlayer(id, Float:{-1924.0, 1228.0, 36.03125}))
        {
            server_print("[lamp-scene] occupied near camera");
            return PLUGIN_HANDLED;
        }
    }
    else if (equal(action, "lightleft") || equal(action, "lightright"))
    {
        // Keep the receiver and camera fixed. Aim both lamp positions at the
        // same point so the wall can be compared against its own clear frame.
        new Float:source[3] = {-2060.0, 1300.0, 36.03125};
        if (equal(action, "lightright")) source[0] = -1980.0;
        if (!PlaceStandingPlayer(lamp, source)) return PLUGIN_HANDLED;
        new Float:direction[3], Float:angles[3];
        direction[0] = -2030.0 - source[0];
        direction[1] = -100.0;
        engfunc(EngFunc_VecToAngles, direction, angles);
        set_entvar(lamp, var_fixangle, 0);
        set_entvar(lamp, var_v_angle, angles);
        engfunc(EngFunc_RunPlayerMove, lamp, angles, 0.0, 0.0, 0.0, 0, 0, 100);
    }
    else if (equal(action, "select")) engclient_cmd(lamp, "goldcraft_night_flashlight");
    else if (equal(action, "toggle"))
        engfunc(EngFunc_RunPlayerMove, lamp, aim, 0.0, 0.0, 0.0, 0, 100, 100);
    else if (equal(action, "cross") || equal(action, "clear"))
    {
        walkerDestination = equal(action, "cross") ? -2030.0 : -2144.0;
        walkerMoving = true;
    }
    else if (!equal(action, "status") && !equal(action, "savebuy") && !equal(action, "returnbuy")) return PLUGIN_HANDLED;
    server_print("[lamp-scene] observer slot=%d userid=%d flashlight=%d", id, get_user_userid(id),
        !!(get_entvar(id, var_effects) & EF_DIMLIGHT));
    new Float:pos[3], Float:view[3], model[64];
    for (new which; which < SceneActors; which++)
    {
        new actor = actorSlot[which];
        get_entvar(actor, var_origin, pos); get_entvar(actor, var_angles, view);
        get_entvar(actor, var_weaponmodel, model, charsmax(model));
        server_print("[lamp-scene] role=%d slot=%d userid=%d xyz=%.2f,%.2f,%.2f yaw=%.2f lit=%d weapon=%s moving=%d",
            which, actor, actorUserid[which], pos[0], pos[1], pos[2], view[1],
            !!(get_entvar(actor, var_effects) & EF_DIMLIGHT), model, which == Walker && walkerMoving);
    }
    return PLUGIN_HANDLED;
}
// Fixed camera and resting position for comparing rendered frames across builds.
// The player must buy, ignite and throw a real flare before it can be aligned.
new photoFlare, photoSerial;
public FlareScene()
{
    new slot[12], user[12], action[12], map[32], host[64], address[32];
    if (read_argc() != 4) return PLUGIN_HANDLED;
    read_argv(1, slot, charsmax(slot)); read_argv(2, user, charsmax(user));
    read_argv(3, action, charsmax(action));
    new id = str_to_num(slot);
    if (!Human(id, str_to_num(user)) || !is_user_alive(id)) return PLUGIN_HANDLED;
    get_user_ip(id, address, charsmax(address), 1);
    get_mapname(map, charsmax(map)); get_cvar_string("hostname", host, charsmax(host));
    if (!equal(address,"127.0.0.1") || !equal(map,"zs_deadend") ||
        !equal(host,"GoldCraft isolated CoF cold")) return PLUGIN_HANDLED;
    if (equal(action,"camera"))
    {
        new Float:camera[3] = {-1808.0, 1248.0, 36.03125};
        new trace = create_tr2();
        engfunc(EngFunc_TraceHull,camera,camera,IGNORE_MONSTERS,HULL_HUMAN,id,trace);
        new solid = get_tr2(trace,TR_StartSolid) || get_tr2(trace,TR_AllSolid);
        free_tr2(trace);
        if (solid) { server_print("[flare-photo] camera obstructed"); return PLUGIN_HANDLED; }
        set_entvar(id,var_velocity,Float:{0.0,0.0,0.0});
        engfunc(EngFunc_SetOrigin,id,camera);
        server_print("[flare-photo] camera placed");
        return PLUGIN_HANDLED;
    }
    if (equal(action,"align"))
    {
        new entity, count, landed;
        while ((entity = rg_find_ent_by_class(entity,"weapon_cof_flare")) > 0)
            if (get_entvar(entity,var_body)==1 && get_entvar(entity,var_solid)==SOLID_TRIGGER &&
                (get_entvar(entity,var_flags)&FL_ONGROUND) && is_nullent(get_entvar(entity,var_owner)))
            { landed=entity; ++count; }
        if (count != 1) { server_print("[flare-photo] landed=%d",count); return PLUGIN_HANDLED; }
        photoFlare=landed; photoSerial=pev_serial(landed);
        engfunc(EngFunc_SetOrigin,landed,burnOrigin);
    }
    else if (!equal(action,"status")) return PLUGIN_HANDLED;
    if (!photoFlare || !pev_valid(photoFlare) || pev_serial(photoFlare)!=photoSerial) return PLUGIN_HANDLED;
    new Float:at[3]; get_entvar(photoFlare,var_origin,at);
    server_print("[flare-photo] t=%.3f entity=%d body=%d solid=%d at=%.3f,%.3f,%.3f",
        get_gametime(),photoFlare,get_entvar(photoFlare,var_body),get_entvar(photoFlare,var_solid),at[0],at[1],at[2]);
    return PLUGIN_HANDLED;
}
public BurnSetup()
{
    new slot[12], user[12], address[32], map[32];
    read_argv(1,slot,charsmax(slot)); read_argv(2,user,charsmax(user));
    new id=str_to_num(slot), userid=str_to_num(user);
    if (!Human(id,userid) || !is_user_alive(id) || get_cvar_num("csol_game_mode")!=2)
        return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1); get_mapname(map,charsmax(map));
    if (!equal(address,"127.0.0.1") || !equal(map,"zs_deadend")) return PLUGIN_HANDLED;
    new required = 3;
    if (read_argc() == 4)
    {
        new mode[12];
        read_argv(3,mode,charsmax(mode));
        if (!equal(mode,"credit") || !OwnsActor(Lamp) || !is_user_alive(actorSlot[Lamp]) ||
            get_member(actorSlot[Lamp],m_iTeam)!=TEAM_CT) return PLUGIN_HANDLED;
        required = sizeof burnCenters;
    }
    else if (read_argc() != 3) return PLUGIN_HANDLED;
    new entity, flare, candidates[sizeof burnCenters], count;
    while ((entity=rg_find_ent_by_class(entity,"weapon_cof_flare"))>0)
        if (get_entvar(entity,var_body)==1 && get_entvar(entity,var_solid)==SOLID_TRIGGER &&
            (get_entvar(entity,var_flags)&FL_ONGROUND) && is_nullent(get_entvar(entity,var_owner)))
            flare=entity;
    entity=0;
    while (count<required && (entity=rg_find_ent_by_class(entity,"monster_entity"))>0)
        if (get_entvar(entity,var_deadflag)==DEAD_NO && Float:get_entvar(entity,var_health)>0.0 &&
            Float:get_entvar(entity,var_takedamage)!=DAMAGE_NO && !(get_entvar(entity,var_effects)&EF_NODRAW))
            candidates[count++]=entity;
    server_print("[cof-burn] setup flare=%d candidates=%d",flare,count);
    if (!flare || count!=required) return PLUGIN_HANDLED;
    engfunc(EngFunc_SetOrigin,flare,burnOrigin);
    burnFlare=flare; burnSerial=pev_serial(flare); burnCount=count;
    for (new i=0;i<burnCount;i++)
    {
        entity=candidates[i];
        new Float:mins[3], Float:maxs[3], Float:point[3];
        get_entvar(entity,var_mins,mins); get_entvar(entity,var_maxs,maxs);
        for (new axis=0;axis<3;axis++)
            point[axis]=burnCenters[i][axis]-(mins[axis]+maxs[axis])*0.5;
        // Pin each center for repeatable distance and wall tests.
        set_entvar(entity,var_nextthink,0.0); set_entvar(entity,var_movetype,MOVETYPE_NONE);
        set_entvar(entity,var_velocity,Float:{0.0,0.0,0.0});
        set_entvar(entity,var_health,burnHealth[i]); set_entvar(entity,var_armorvalue,50.0);
        engfunc(EngFunc_SetOrigin,entity,point);
        burnTargets[i]=entity; burnTargetSerial[i]=pev_serial(entity); burnHits[i]=0; burnDead[i]=false;
    }
    if (!burnHook) burnHook=RegisterHamFromEntity(Ham_TakeDamage,candidates[0],"BurnHit",1);
    return BurnState();
}
public BurnHit(entity,inflictor,attacker,Float:amount,bits)
{
    if (inflictor!=burnFlare || !pev_valid(inflictor) || pev_serial(inflictor)!=burnSerial) return HAM_IGNORED;
    for (new i=0;i<burnCount;i++)
        if (!burnDead[i] && entity==burnTargets[i] && pev_serial(entity)==burnTargetSerial[i])
        {
            ++burnHits[i];
            new dead=get_entvar(entity,var_deadflag);
            burnDead[i] = dead==DEAD_DEAD;
            server_print("[cof-burn] hit t=%.3f target=%d index=%d source=%d actor=%d amount=%.1f bits=%d hp=%.1f deadflag=%d",
                get_gametime(),i,entity,inflictor,attacker,amount,bits,Float:get_entvar(entity,var_health),dead);
        }
    return HAM_IGNORED;
}
public BurnState()
{
    if (!burnFlare || !pev_valid(burnFlare) || pev_serial(burnFlare)!=burnSerial)
    {
        server_print("[cof-burn] no tracked world flare"); return PLUGIN_HANDLED;
    }
    new Float:origin[3], Float:start[3], tr=create_tr2();
    get_entvar(burnFlare,var_origin,origin);
    for(new axis=0;axis<3;axis++) start[axis]=origin[axis];
    start[2]+=1.0;
    server_print("[cof-burn] t=%.3f flare=%d body=%d next=%.3f",get_gametime(),burnFlare,
        get_entvar(burnFlare,var_body),Float:get_entvar(burnFlare,var_nextthink));
    for (new i=0;i<burnCount;i++)
    {
        new entity=burnTargets[i];
        if (burnDead[i])
        {
            server_print("[cof-burn] target=%d index=%d died=1 hits=%d",i,entity,burnHits[i]);
            continue;
        }
        if (!pev_valid(entity) || pev_serial(entity)!=burnTargetSerial[i]) continue;
        new Float:lo[3], Float:hi[3], Float:center[3], Float:fraction;
        get_entvar(entity,var_absmin,lo); get_entvar(entity,var_absmax,hi);
        for(new axis=0;axis<3;axis++) center[axis]=(lo[axis]+hi[axis])*0.5;
        engfunc(EngFunc_TraceLine,start,center,IGNORE_MONSTERS,burnFlare,tr);
        get_tr2(tr,TR_flFraction,fraction);
        server_print("[cof-burn] target=%d index=%d hp=%.2f armor=%.2f range=%.2f trace=%.3f hits=%d",i,
            entity,Float:get_entvar(entity,var_health),Float:get_entvar(entity,var_armorvalue),
            get_distance_f(origin,center),fraction,burnHits[i]);
    }
    free_tr2(tr);
    return PLUGIN_HANDLED;
}
bool:Human(id, userid)
{
    return id >= 1 && id <= MaxClients && is_user_connected(id) &&
        !is_user_bot(id) && !is_user_hltv(id) && get_user_userid(id) == userid;
}
// Confirm the sandbox client's chosen appearance without focusing its window.
public Join()
{
    new slot[12], user[12], chosen[12], address[32], map[32];
    if (read_argc()!=4) return PLUGIN_HANDLED;
    read_argv(1,slot,charsmax(slot)); read_argv(2,user,charsmax(user));
    read_argv(3,chosen,charsmax(chosen));
    new id=str_to_num(slot);
    if (!Human(id,str_to_num(user)) || is_user_alive(id) || get_member(id,m_iTeam)!=TEAM_CT ||
        get_cvar_num("csol_game_mode")!=2 || str_to_num(chosen)<=0) return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1); get_mapname(map,charsmax(map));
    if (equal(map,"zs_deadend") && equal(address,"127.0.0.1"))
        engclient_cmd(id,"csol_character_select",chosen);
    return PLUGIN_HANDLED;
}
public BurnWave()
{
    new slot[12], user[12], address[32], map[32];
    read_argv(1,slot,charsmax(slot)); read_argv(2,user,charsmax(user));
    new id=str_to_num(slot);
    if (!Human(id,str_to_num(user)) || !is_user_alive(id) || get_cvar_num("csol_game_mode")!=2)
        return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1); get_mapname(map,charsmax(map));
    if (equal(map,"zs_deadend") && equal(address,"127.0.0.1"))
    {
        // Center of BSP trigger 1 at standing height. The normal trigger manager
        // selects and spawns the wave. Keep the lone test CT alive until expiry.
        set_entvar(id,var_takedamage,DAMAGE_NO);
        engfunc(EngFunc_SetOrigin,id,Float:{1088.0,-1728.0,36.03125});
        server_print("[cof-burn] CT placed inside native wave trigger 1");
    }
    return PLUGIN_HANDLED;
}
// Keep the test CT at the second stage's normal shop for long inventory checks.
public RoundProtect()
{
    if (read_argc() != 3 || get_cvar_num("csol_game_mode") != 2 ||
        get_member_game(m_iNumCTWins) != 1 || get_member_game(m_bFreezePeriod) ||
        get_member_game(m_bRoundTerminating)) return PLUGIN_HANDLED;
    new slot[12], user[12], map[32], host[64], ip[32], address[32];
    read_argv(1, slot, charsmax(slot));
    read_argv(2, user, charsmax(user));
    new id = str_to_num(slot);
    if (!Human(id, str_to_num(user)) || !is_user_alive(id) || get_member(id, m_iTeam) != TEAM_CT)
        return PLUGIN_HANDLED;
    get_mapname(map, charsmax(map));
    get_cvar_string("hostname", host, charsmax(host));
    get_cvar_string("ip", ip, charsmax(ip));
    get_user_ip(id, address, charsmax(address), 1);
    if (!equal(map, "zs_deadend") || !equal(host, "GoldCraft isolated CoF cold") ||
        !equal(ip, "127.0.0.1") || !equal(address, "127.0.0.1")) return PLUGIN_HANDLED;
    set_entvar(id, var_takedamage, DAMAGE_NO);
    server_print("[CoF live] protected test CT slot=%d userid=%d", id, get_user_userid(id));
    return PLUGIN_HANDLED;
}

Inspect(id)
{
    server_print("[CoF live] player=%d userid=%d alive=%d health=%.1f money=%d team=%d frags=%d",
        id, get_user_userid(id), is_user_alive(id), Float:get_entvar(id,var_health),
        get_member(id,m_iAccount), get_member(id,m_iTeam), get_user_frags(id));
    new active=get_member(id,m_pActiveItem);
    for(new slot;slot<6;slot++)
    {
        new entity=get_member(id,m_rgpPlayerItems,slot), count;
        while(!is_nullent(entity) && count++<64)
        {
            new classname[64];get_entvar(entity,var_classname,classname,charsmax(classname));
            new ammo=get_member(entity,m_Weapon_iPrimaryAmmoType);
            new reserve=ammo>=0&&ammo<32?get_member(id,m_rgAmmo,ammo):-1;
            server_print("[CoF live] slot=%d entity=%d id=%d clip=%d reserve=%d active=%d classname=%s serial=%d",
                slot,entity,get_member(entity,m_iId),get_member(entity,m_Weapon_iClip),reserve,entity==active,classname,pev_serial(entity));
            entity=get_member(entity,m_pNext);
        }
    }
}
public Status()
{
    new map[32];
    get_mapname(map, charsmax(map));
    server_print("[CoF live] map=%s time=%.2f ct_wins=%d started=%d freeze=%d terminating=%d",
        map, get_gametime(), get_member_game(m_iNumCTWins), get_member_game(m_bGameStarted),
        get_member_game(m_bFreezePeriod), get_member_game(m_bRoundTerminating));
    for(new id=1;id<=MaxClients;id++)
        if(is_user_connected(id)&&!is_user_bot(id)&&!is_user_hltv(id))Inspect(id);
    if (OwnsActor(Lamp) && pev_valid(actorSlot[Lamp])==2)
        server_print("[cof-burn] teammate=%d userid=%d serial=%d bot=%d alive=%d team=%d frags=%d",
            actorSlot[Lamp],actorUserid[Lamp],actorSerial[Lamp],is_user_bot(actorSlot[Lamp]),is_user_alive(actorSlot[Lamp]),
            get_member(actorSlot[Lamp],m_iTeam),get_user_frags(actorSlot[Lamp]));
    return PLUGIN_HANDLED;
}

public ClearBatch()
{
    if (read_argc() != 3 || !IsHamValid(Ham_TakeDamage)) return PLUGIN_HANDLED;
    new slot[12], user[12], map[32], address[32];
    read_argv(1, slot, charsmax(slot));
    read_argv(2, user, charsmax(user));
    new id = str_to_num(slot), userid = str_to_num(user);
    if (!Human(id, userid) || !is_user_alive(id) || !get_member_game(m_bGameStarted) ||
        get_member_game(m_bFreezePeriod) || get_member_game(m_bRoundTerminating) ||
        get_member_game(m_iNumCTWins) != 0 || get_cvar_num("csol_game_mode") != 2)
        return PLUGIN_HANDLED;
    get_mapname(map, charsmax(map));
    get_user_ip(id, address, charsmax(address), 1);
    if (!equal(map, "zs_deadend") || !equal(address, "127.0.0.1")) return PLUGIN_HANDLED;

    new entity, examined, damaged;
    while (examined++ < 256 && (entity = rg_find_ent_by_class(entity, "monster_entity")))
    {
        if (get_entvar(entity, var_solid) == SOLID_NOT || get_entvar(entity, var_deadflag) != DEAD_NO ||
            (get_entvar(entity, var_effects) & EF_NODRAW) || Float:get_entvar(entity, var_health) <= 0.0)
            continue;
        ExecuteHamB(Ham_TakeDamage, entity, id, id, 2000.0, DMG_BULLET);
        ++damaged;
        if (!Human(id, userid) || !is_user_alive(id) || get_member_game(m_iNumCTWins) != 0 ||
            get_member_game(m_bRoundTerminating)) break;
    }
    server_print("[CoF live] synthetic_monster_damage=1 targets=%d", damaged);
    return PLUGIN_HANDLED;
}
// Refill only the named local test actor. Purchase still pays the real price.
public TestFunds()
{
    if(read_argc()!=3)return PLUGIN_HANDLED;
    new slot[12], user[12], address[32], map[32];
    read_argv(1,slot,charsmax(slot));read_argv(2,user,charsmax(user));
    new id=str_to_num(slot), userid=str_to_num(user);
    if(!Human(id,userid)||!is_user_alive(id)||get_cvar_num("csol_game_mode")!=2)return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1);
    get_mapname(map,charsmax(map));
    if(!equal(address,"127.0.0.1")||!equal(map,"zs_deadend"))return PLUGIN_HANDLED;
    new before=get_member(id,m_iAccount);
    rg_add_account(id,32000,AS_SET,false);
    server_print("[CoF live] fixture_balance before=%d after=%d",before,get_member(id,m_iAccount));
    return PLUGIN_HANDLED;
}
public Purchase()
{
    if(read_argc()!=4 && read_argc()!=5)return PLUGIN_HANDLED;
    new slot[12],user[12],request[12];read_argv(1,slot,charsmax(slot));read_argv(2,user,charsmax(user));read_argv(3,request,charsmax(request));
    new id=str_to_num(slot),userid=str_to_num(user),token=str_to_num(request);
    if(!Human(id,userid)||!is_user_alive(id)||token<1||token>65535)return PLUGIN_HANDLED;
    new item[12]="896", address[32];
    if(read_argc()==5)read_argv(4,item,charsmax(item));
    new itemid=str_to_num(item);
    if((itemid<896 || itemid>905) && itemid!=976 && itemid!=977)return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1);
    if(!equal(address,"127.0.0.1"))return PLUGIN_HANDLED;
    new before=get_member(id,m_iAccount);
    engclient_cmd(id,"csol_buy_menu",request);
    if(!Human(id,userid)||!is_user_alive(id))return PLUGIN_HANDLED;
    engclient_cmd(id,"csol_buy",request,item);
    if(!Human(id,userid))return PLUGIN_HANDLED;
    server_print("[CoF live] actual_server_purchase item=%d before=%d after=%d debit=%d synthetic_command=1 physical_ui=0",
        itemid,before,get_member(id,m_iAccount),before-get_member(id,m_iAccount));
    Inspect(id);
    return PLUGIN_HANDLED;
}
