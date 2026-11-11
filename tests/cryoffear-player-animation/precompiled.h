// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Controlled engine edge for running the actual adapter TU. The runner also
// compiles that TU separately with the real current ReGameDLL precompiled.h.
#include <cstdint>
using vec3_t = float[3];
using byte = unsigned char;
using int32 = std::int32_t;
using qboolean = int;
#define BIT(number) (1U << (number))
#include "external/ReGameDLL_CS/regamedll/engine/studio.h"
#include "external/ReGameDLL_CS/regamedll/dlls/activity.h"
#include "external/ReGameDLL_CS/regamedll/common/const.h"

struct entvars_t
{
    int modelindex{}, sequence{}, effects{}, flags{}, deadflag{}, gaitsequence{};
    float health{}, frame{}, animtime{}, framerate{};
};
struct edict_s { int serialnumber{}; };
struct Globals { float time{}; };
extern Globals* gpGlobals;
extern const void* currentModel;
extern edict_t* currentEdict;
inline void* GET_MODEL_PTR(edict_t*) { return const_cast<void*>(currentModel); }
inline edict_t* ENT(entvars_t*) { return currentEdict; }
struct CBasePlayer
{
    entvars_t* pev{};
    edict_t* ed{};
    float m_flFlinchTime{}, m_flLastFired{}, m_flFrameRate{}, m_flGroundSpeed{}, m_flLastEventCheck{};
    int m_fSequenceFinished{}, m_fSequenceLoops{}, resetCalls{};
    Activity m_Activity{ACT_IDLE}, m_IdealActivity{ACT_IDLE};
    edict_t* edict() const { return ed; }
    void ResetSequenceInfo();
    int GetSequenceFlags();
};
enum GameEventType { EVENT_WEAPON_FIRED, EVENT_WEAPON_RELOADED };
struct BotEdge
{
    int fired{}, reloaded{};
    void OnEvent(GameEventType event, CBasePlayer*)
    { if (event == EVENT_WEAPON_FIRED) ++fired; else if (event == EVENT_WEAPON_RELOADED) ++reloaded; }
};
extern BotEdge* TheBots;
