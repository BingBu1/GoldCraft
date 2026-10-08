// Expand native spawn capacity using nearby, standing-hull-verified positions.
// ReAPI 5.29: rg_create_entity(..., true) also registers the classname hash.
#include <amxmodx>
#include <engine>
#include <fakemeta>
#include <reapi>

#define MAX_SPAWNS 64
new gTrace, gTarget, gCount, gAddedCount, gAddedT, gAddedCT;
new Float:gOrigins[MAX_SPAWNS][3], gAdded[MAX_SPAWNS];
new const gDx[8] = {1,-1,0,0,1,1,-1,-1};
new const gDy[8] = {0,0,1,-1,1,-1,1,-1};

public plugin_init()
{
    register_plugin("GoldCraft native spawn capacity", "0.1.0", "GoldCraft contributors");
    register_srvcmd("gc_spawn_capacity", "Status");
    new map[32]; get_mapname(map, charsmax(map));
    if (!equali(map, "cs_assault")) return;
    gTrace = create_tr2();
    gTarget = (get_maxplayers() + 1) / 2;
    RememberSpawns("info_player_start");
    RememberSpawns("info_player_deathmatch");
    gAddedCT = ExpandTeam("info_player_start");
    gAddedT = ExpandTeam("info_player_deathmatch");
    // Match actual entities instead of bypassing ReGameDLL's TeamFull check.
    set_member_game(m_iSpawnPointCount_CT, CountSpawns("info_player_start"));
    set_member_game(m_iSpawnPointCount_Terrorist, CountSpawns("info_player_deathmatch"));
    Status();
}

public plugin_end() { if (gTrace) free_tr2(gTrace); }

stock CountSpawns(const classname[])
{
    new entity, count;
    while ((entity = find_ent_by_class(entity, classname)) > 0) count++;
    return count;
}

stock RememberSpawns(const classname[])
{
    new entity;
    while ((entity = find_ent_by_class(entity, classname)) > 0 && gCount < MAX_SPAWNS)
        pev(entity, pev_origin, gOrigins[gCount++]);
}

stock bool:Clear(const Float:a[3], const Float:b[3])
{
    engfunc(EngFunc_TraceHull, a, b, IGNORE_MONSTERS, HULL_HUMAN, 0, gTrace);
    new Float:fraction; get_tr2(gTrace, TR_flFraction, fraction);
    return !get_tr2(gTrace, TR_StartSolid) && !get_tr2(gTrace, TR_AllSolid) && fraction >= 0.9999;
}

stock bool:FindFloor(const Float:point[3], Float:result[3])
{
    new Float:top[3], Float:bottom[3], Float:normal[3], Float:fraction;
    for (new axis = 0; axis < 3; axis++) { top[axis] = point[axis]; bottom[axis] = point[axis]; }
    top[2] += 18.0; bottom[2] -= 96.0;
    engfunc(EngFunc_TraceHull, top, bottom, IGNORE_MONSTERS, HULL_HUMAN, 0, gTrace);
    get_tr2(gTrace, TR_flFraction, fraction);
    get_tr2(gTrace, TR_vecPlaneNormal, normal);
    if (get_tr2(gTrace, TR_StartSolid) || get_tr2(gTrace, TR_AllSolid) || fraction >= 1.0 || normal[2] < 0.7)
        return false;
    get_tr2(gTrace, TR_vecEndPos, result); result[2] += 0.5;
    return floatabs(result[2] - point[2]) <= 24.0 && Clear(result, result);
}

stock bool:Separated(const Float:point[3])
{
    for (new i = 0; i < gCount; i++) {
        new Float:dx = point[0] - gOrigins[i][0], Float:dy = point[1] - gOrigins[i][1];
        if (floatabs(point[2] - gOrigins[i][2]) < 72.0 && dx * dx + dy * dy < 4096.0) return false;
    }
    return true;
}

stock ExpandTeam(const classname[])
{
    new seeds[32], seedCount, entity, added;
    while ((entity = find_ent_by_class(entity, classname)) > 0 && seedCount < sizeof seeds)
        seeds[seedCount++] = entity;
    new Float:origin[3], Float:angles[3], Float:candidate[3], Float:floor[3];
    for (new radius = 64; radius <= 192 && seedCount + added < gTarget; radius += 32)
        for (new seed = 0; seed < seedCount && seedCount + added < gTarget; seed++) {
            pev(seeds[seed], pev_origin, origin); pev(seeds[seed], pev_angles, angles);
            for (new direction = 0; direction < 8 && seedCount + added < gTarget; direction++) {
                if (gCount >= MAX_SPAWNS) return added;
                candidate[0] = origin[0] + float(gDx[direction] * radius);
                candidate[1] = origin[1] + float(gDy[direction] * radius);
                candidate[2] = origin[2];
                if (!FindFloor(candidate, floor) || !Separated(floor) || !Clear(origin, floor)) continue;
                entity = rg_create_entity(classname, true);
                if (!entity) continue;
                entity_set_origin(entity, floor); set_pev(entity, pev_angles, angles); DispatchSpawn(entity);
                for (new axis = 0; axis < 3; axis++) gOrigins[gCount][axis] = floor[axis];
                gCount++; gAdded[gAddedCount++] = entity; added++;
            }
        }
    return added;
}

public Status()
{
    new blocked, Float:origin[3];
    for (new i = 0; i < gAddedCount; i++) {
        if (!pev_valid(gAdded[i])) { blocked++; continue; }
        pev(gAdded[i], pev_origin, origin);
        if (!Clear(origin, origin)) blocked++;
    }
    server_print("[GoldCraft spawns] maxplayers=%d target=%d T=%d CT=%d addedT=%d addedCT=%d checked=%d blocked=%d",
        get_maxplayers(), gTarget, get_member_game(m_iSpawnPointCount_Terrorist),
        get_member_game(m_iSpawnPointCount_CT), gAddedT, gAddedCT, gAddedCount, blocked);
    return PLUGIN_HANDLED;
}
