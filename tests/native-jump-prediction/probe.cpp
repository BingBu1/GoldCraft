// SPDX-License-Identifier: GPL-3.0-or-later
// Executes the unmodified, identity-checked native client in a separate x86
// console process. Engine callbacks provide a flat floor, not a game server.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
using byte = unsigned char;
using qboolean = int;
using vec3_t = float[3];
struct trace_s;
using trace_t = trace_s;
#include <pm_defs.h>
#include <pm_movevars.h>
#include <in_buttons.h>
#include "../../native/csol/scenario/custom/jump_prediction.hpp"

namespace
{
playermove_t* current{};
unsigned checks{}, moves{}, sounds{};
void Require(bool condition, const char* reason)
{ ++checks; if (!condition) throw std::runtime_error(reason); }
void Print(char*, ...) {}
void NumberPrint(int, char*, ...) {}
void Sound(int, const char*, float, float, int, int) { ++sounds; }
int Contents(float* p, int* actual)
{ const int result = p[2] < 0 ? -2 : -1; if (actual) *actual = result; return result; }
pmtrace_t Trace(float* start, float* end, int, int)
{
    pmtrace_t result{};
    result.fraction = 1;
    result.ent = -1;
    std::copy_n(end, 3, result.endpos);
    const float floor = -current->player_mins[current->usehull][2];
    if (end[2] < floor && start[2] >= floor)
    {
        result.fraction = (start[2] - floor) / (start[2] - end[2]);
        result.ent = 0;
        for (int i = 0; i < 3; ++i) result.endpos[i] = start[i] + result.fraction * (end[i] - start[i]);
        result.plane.normal[2] = 1;
    }
    return result;
}
std::unique_ptr<playermove_t> Movement(movevars_t& variables)
{
    auto value = std::make_unique<playermove_t>();
    auto& m = *value;
    m.multiplayer = 1; m.movetype = 3; m.onground = 0; m.origin[2] = 36;
    m.maxspeed = m.clientmaxspeed = 250; m.friction = m.gravity = 1;
    m.movevars = &variables; m.numphysent = 1; m.physents[0].solid = 4;
    m.player_mins[0][0] = m.player_mins[0][1] = -16; m.player_mins[0][2] = -36;
    m.player_maxs[0][0] = m.player_maxs[0][1] = 16; m.player_maxs[0][2] = 36;
    m.player_mins[1][0] = m.player_mins[1][1] = -16; m.player_mins[1][2] = -18;
    m.player_maxs[1][0] = m.player_maxs[1][1] = 16; m.player_maxs[1][2] = 18;
    m.PM_Info_ValueForKey = [](const char*, const char*) -> const char* { return "0"; };
    m.PM_Particle = [](float*, int, float, int, int) {};
    m.PM_TestPlayerPosition = [](float*, pmtrace_t*) { return -1; };
    m.Con_Printf = m.Con_DPrintf = Print; m.Con_NPrintf = NumberPrint;
    m.Sys_FloatTime = [] { return 1.; };
    m.PM_StuckTouch = [](int, pmtrace_t*) {};
    m.PM_PointContents = Contents;
    m.PM_TruePointContents = [](float* p) { return Contents(p, nullptr); };
    m.PM_PlayerTrace = Trace;
    m.PM_TraceLine = [](float* a, float* b, int flags, int, int ignore) -> pmtrace_t*
    { static pmtrace_t t; t = Trace(a, b, flags, ignore); return &t; };
    m.RandomLong = [](long a, long) { return a; };
    m.RandomFloat = [](float a, float) { return a; };
    m.PM_GetModelType = [](model_s*) { return 0; };
    m.COM_FileSize = [](char*) { return 0; };
    m.COM_LoadFile = [](char*, int, int*) -> byte* { return nullptr; };
    m.COM_FreeFile = [](void*) {};
    m.PM_PlaySound = Sound;
    m.PM_TraceTexture = [](int, float*, float*) -> const char* { return "CONCRETE"; };
    m.PM_PlaybackEventFull = [](int, int, unsigned short, float, float*, float*, float, float, int, int, int, int) {};
    m.cmd.msec = 10;
    return value;
}
void Layout()
{
    std::printf("{\"size\":%zu,\"time\":%zu,\"origin\":%zu,\"velocity\":%zu,\"oldbuttons\":%zu,"
        "\"onground\":%zu,\"cmd\":%zu,\"physinfo\":%zu,\"frametime\":%zu,\"fuser2\":%zu,"
        "\"cmdSize\":%zu,\"cmdButtons\":%zu,\"cmdMsec\":%zu,\"flags\":%zu,\"dead\":%zu,\"movetype\":%zu}\n",
        sizeof(playermove_t), offsetof(playermove_t,time), offsetof(playermove_t,origin),
        offsetof(playermove_t,velocity), offsetof(playermove_t,oldbuttons), offsetof(playermove_t,onground),
        offsetof(playermove_t,cmd), offsetof(playermove_t,physinfo), offsetof(playermove_t,frametime), offsetof(playermove_t,fuser2),
        sizeof(usercmd_t), offsetof(usercmd_t,buttons), offsetof(usercmd_t,msec), offsetof(playermove_t,flags),
        offsetof(playermove_t,dead), offsetof(playermove_t,movetype));
}
}
int main(int argc, char** argv)
{
    Layout();
    if (argc == 1) return 0;
    try
    {
        Require(argc == 3, "Supply native client sample and sandbox dependency directory");
        Require(SetDllDirectoryA(argv[2]), "Set sandbox dependency directory");
        const auto module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) { std::printf("LoadLibrary error %lu\n", GetLastError()); return 2; }
        const auto initialize = reinterpret_cast<void (*)(playermove_t*)>(GetProcAddress(module,"HUD_PlayerMoveInit"));
        const auto native = reinterpret_cast<void (*)(playermove_t*,int)>(GetProcAddress(module,"HUD_PlayerMove"));
        Require(initialize && native, "Actual native PM exports exist");
        static_assert(offsetof(playermove_t,onground) == 0xe0);
        static_assert(offsetof(playermove_t,oldbuttons) == 0xc8);
        static_assert(offsetof(playermove_t,cmd) + offsetof(usercmd_t,msec) == 0x4545a);
        movevars_t variables{};
        variables.gravity=800; variables.maxspeed=320; variables.stopspeed=75;
        variables.accelerate=5; variables.airaccelerate=10; variables.friction=4;
        variables.wateraccelerate=10; variables.waterfriction=1; variables.edgefriction=2;
        variables.stepsize=18; variables.maxvelocity=2000; variables.entgravity=1;
        auto move = Movement(variables); current = move.get(); initialize(current);
        for (int step=0; step<700; ++step)
        {
            current->time = step * .01f;
            current->cmd.buttons = (step%4==0) ? IN_JUMP : 0;
            const bool departure = current->onground != -1 && (current->cmd.buttons & IN_JUMP) && !(current->oldbuttons & IN_JUMP);
            const float before = current->origin[2];
            const bool gated = csol::scenario::custom::ApplyJumpPredictionGate(*current, false);
            Require(!gated, "Sufficient-balance gate leaves actual native command untouched");
            native(current,0); ++moves;
            if (departure)
            {
                Require(current->onground == -1 && current->velocity[2] > 0, "Native departure occurs in the same command");
                Require(current->origin[2] > before, "Native jump moves during its first command");
                std::printf("departure step=%d z=%g vz=%g\n",step,current->origin[2],current->velocity[2]);
            }
        }
        std::printf("checks=%u moves=%u sounds=%u\n",checks,moves,sounds);
        FreeLibrary(module);
    }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
