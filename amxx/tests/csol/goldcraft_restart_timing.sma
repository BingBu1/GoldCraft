// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit local map-load stage observer; no changes to rules or timings.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#define TRACE_FILE "goldcraft_restart_timing.log"
#define RESOURCE_KINDS 3

new Float:g_ActivateStart;
new Float:g_GameDllEnd;
new bool:g_Started;
new bool:g_GameDllObserved;
new Float:g_PrecacheStart;
new Float:g_ResourceStart[RESOURCE_KINDS];
new Float:g_ResourceTotal[RESOURCE_KINDS];
new Float:g_ResourceMax[RESOURCE_KINDS];
new Float:g_ResourceFirst[RESOURCE_KINDS];
new Float:g_ResourceLast[RESOURCE_KINDS];
new g_ResourceCount[RESOURCE_KINDS];
new g_ResourceDepth[RESOURCE_KINDS];
new g_ResourceNested[RESOURCE_KINDS];
new g_ResourceUnmatched[RESOURCE_KINDS];
new g_ResourceMaxPath[RESOURCE_KINDS][128];
new g_ObserverForwards[7];

public plugin_precache()
{
    g_PrecacheStart = Float:engfunc(EngFunc_Time);
    log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=plugin_precache clock=%.6f version=2", g_PrecacheStart);
    RegisterHookChain(RH_SV_ActivateServer, "ActivateBegin", false);
    RegisterHookChain(RH_SV_ActivateServer, "ActivateEnd", true);
    RegisterHookChain(RH_Con_Printf, "ObserveDependencySummary", false);
    g_ObserverForwards[0] = register_forward(FM_Spawn, "ObserveSpawnEnd", true);
    g_ObserverForwards[1] = register_forward(FM_PrecacheModel, "ModelBegin", false);
    g_ObserverForwards[2] = register_forward(FM_PrecacheModel, "ModelEnd", true);
    g_ObserverForwards[3] = register_forward(FM_PrecacheSound, "SoundBegin", false);
    g_ObserverForwards[4] = register_forward(FM_PrecacheSound, "SoundEnd", true);
    g_ObserverForwards[5] = register_forward(FM_PrecacheGeneric, "GenericBegin", false);
    g_ObserverForwards[6] = register_forward(FM_PrecacheGeneric, "GenericEnd", true);
}

public plugin_init()
{
    register_plugin("CSOL restart timing", "1.1", "GoldCraft tests");
}

public ObserveDependencySummary(const message[])
{
    if (containi(message, "CSOL: dependencies ") == 0)
        log_to_file(TRACE_FILE, "%s", message);
    if (containi(message, "[CSOL] Characters ready:") == 0)
        log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=characters_ready clock=%.6f", Float:engfunc(EngFunc_Time));
    return HC_CONTINUE;
}

public ObserveSpawnEnd(const entity)
{
    if (entity == 0)
        log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=worldspawn_end clock=%.6f", Float:engfunc(EngFunc_Time));
    return FMRES_IGNORED;
}

ResourceBegin(const kind)
{
    ++g_ResourceCount[kind];
    if (g_ResourceDepth[kind]++ > 0)
    {
        ++g_ResourceNested[kind];
        return FMRES_IGNORED;
    }
    g_ResourceStart[kind] = Float:engfunc(EngFunc_Time);
    if (g_ResourceCount[kind] == 1) g_ResourceFirst[kind] = g_ResourceStart[kind];
    return FMRES_IGNORED;
}

ResourceEnd(const kind, const path[])
{
    if (g_ResourceDepth[kind] <= 0)
    {
        ++g_ResourceUnmatched[kind];
        return FMRES_IGNORED;
    }
    if (--g_ResourceDepth[kind] > 0) return FMRES_IGNORED;
    new Float:now = Float:engfunc(EngFunc_Time);
    new Float:elapsed = now - g_ResourceStart[kind];
    g_ResourceLast[kind] = now;
    g_ResourceTotal[kind] += elapsed;
    if (elapsed > g_ResourceMax[kind])
    {
        g_ResourceMax[kind] = elapsed;
        copy(g_ResourceMaxPath[kind], charsmax(g_ResourceMaxPath[]), path);
    }
    return FMRES_IGNORED;
}

public ModelBegin(const path[]) { return ResourceBegin(0); }
public ModelEnd(const path[]) { return ResourceEnd(0, path); }
public SoundBegin(const path[]) { return ResourceBegin(1); }
public SoundEnd(const path[]) { return ResourceEnd(1, path); }
public GenericBegin(const path[]) { return ResourceBegin(2); }
public GenericEnd(const path[]) { return ResourceEnd(2, path); }

public ActivateBegin(const runPhysics)
{
    g_ActivateStart = Float:engfunc(EngFunc_Time);
    g_Started = true;
    log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=begin clock=%.6f physics=%d", g_ActivateStart, runPhysics);
    return HC_CONTINUE;
}

// AMXX invokes plugin_cfg inside C_ServerActivate_Post, after the GameDLL.
// This marks an AMXX stage, not the exact GameDLL return boundary.
public plugin_cfg()
{
    g_GameDllEnd = Float:engfunc(EngFunc_Time);
    g_GameDllObserved = true;
    log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=plugin_cfg clock=%.6f", g_GameDllEnd);
}

public ActivateEnd(const runPhysics)
{
    new Float:now = Float:engfunc(EngFunc_Time);
    log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=end clock=%.6f activate=%.6f resources=%.6f valid=%d physics=%d", now, now - g_ActivateStart, now - g_GameDllEnd, g_Started && g_GameDllObserved, runPhysics);
    for (new kind; kind < RESOURCE_KINDS; ++kind)
        log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=resource_summary kind=%d count=%d seconds=%.6f max=%.6f first=%.6f last=%.6f depth=%d nested=%d unmatched=%d max_path=%s", kind, g_ResourceCount[kind], g_ResourceTotal[kind], g_ResourceMax[kind], g_ResourceFirst[kind], g_ResourceLast[kind], g_ResourceDepth[kind], g_ResourceNested[kind], g_ResourceUnmatched[kind], g_ResourceMaxPath[kind]);
    unregister_forward(FM_Spawn, g_ObserverForwards[0], true);
    unregister_forward(FM_PrecacheModel, g_ObserverForwards[1], false);
    unregister_forward(FM_PrecacheModel, g_ObserverForwards[2], true);
    unregister_forward(FM_PrecacheSound, g_ObserverForwards[3], false);
    unregister_forward(FM_PrecacheSound, g_ObserverForwards[4], true);
    unregister_forward(FM_PrecacheGeneric, g_ObserverForwards[5], false);
    unregister_forward(FM_PrecacheGeneric, g_ObserverForwards[6], true);
    return HC_CONTINUE;
}

public plugin_end()
{
    log_to_file(TRACE_FILE, "GC_CACHE_TIME phase=plugin_end clock=%.6f", Float:engfunc(EngFunc_Time));
}
