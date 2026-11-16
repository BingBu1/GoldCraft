// SPDX-License-Identifier: GPL-3.0-or-later
// Actual GameDLL frame/snapshot and native client/server PM. The public engine
// trace callbacks describe planes; they are not a BSP or network transport.
#define CSOL_STANDALONE_FRAME_LIBRARY
#include "../csol-standalone-frame/sdk.cpp"
#include "pm_defs.h"
#include "pm_movevars.h"
#include "../../native/csol/scenario/custom/jump_prediction.hpp"
#include "native_sample.hpp"

namespace standalone_native_test
{
using namespace standalone_frame_test;
playermove_t* moving{};
bool wall{};
unsigned nativeMoves{}, serverMoves{}, departures{}, wallContacts{};

void Print(char*, ...) {}
void NumberPrint(int, char*, ...) {}
int Contents(float* point, int* actual)
{
    const int value = point[2] < 0 ? CONTENTS_SOLID : CONTENTS_EMPTY;
    if (actual) *actual = value;
    return value;
}
pmtrace_t Trace(float* start, float* end, int, int)
{
    pmtrace_t result{};
    result.fraction = 1; result.ent = -1;
    result.endpos = Vector(end);
    const auto plane = [&](unsigned axis, float boundary, float normal)
    {
        const float a = (start[axis] - boundary) * normal;
        const float b = (end[axis] - boundary) * normal;
        if (a < 0)
        {
            result.startsolid = TRUE;
            if (b < 0)
            {
                result.allsolid = TRUE; result.fraction = 0; result.ent = 0;
                result.endpos = Vector(start);
            }
            return;
        }
        if (b >= 0) return;
        const float fraction = a / (a - b);
        if (fraction > result.fraction) return;
        result.fraction = fraction; result.ent = 0;
        for (unsigned i = 0; i != 3; ++i)
        {
            result.endpos[i] = start[i] + fraction * (end[i] - start[i]);
            result.plane.normal[i] = i == axis ? normal : 0;
        }
        result.plane.dist = boundary * normal;
        if (axis == 0) ++wallContacts;
    };
    plane(2, -moving->player_mins[moving->usehull][2], 1);
    if (wall) plane(0, 256 - moving->player_maxs[moving->usehull][0], -1);
    return result;
}

std::unique_ptr<playermove_t> Movement(movevars_t& variables)
{
    auto value = std::make_unique<playermove_t>();
    auto& move = *value;
    move.multiplayer = TRUE; move.movetype = MOVETYPE_WALK;
    move.onground = 0; move.origin[2] = 36;
    move.friction = move.gravity = 1; move.movevars = &variables;
    move.numphysent = 1; move.physents[0].solid = SOLID_BSP;
    for (unsigned hull = 0; hull != 2; ++hull)
    {
        move.player_mins[hull][0] = move.player_mins[hull][1] = -16;
        move.player_maxs[hull][0] = move.player_maxs[hull][1] = 16;
        move.player_mins[hull][2] = hull ? -18.f : -36.f;
        move.player_maxs[hull][2] = -move.player_mins[hull][2];
    }
    move.PM_Info_ValueForKey = [](const char*, const char*) -> const char* { return "0"; };
    move.PM_Particle = [](float*, int, float, int, int) {};
    move.PM_TestPlayerPosition = [](float*, pmtrace_t*) { return -1; };
    move.Con_Printf = move.Con_DPrintf = Print; move.Con_NPrintf = NumberPrint;
    move.Sys_FloatTime = [] { return 1.; };
    move.PM_StuckTouch = [](int, pmtrace_t*) {};
    move.PM_PointContents = Contents;
    move.PM_TruePointContents = [](float* point) { return Contents(point, nullptr); };
    move.PM_PlayerTrace = Trace;
    move.PM_TraceLine = [](float* a, float* b, int flags, int, int ignore) -> pmtrace_t*
    { static pmtrace_t trace; trace = Trace(a, b, flags, ignore); return &trace; };
    move.RandomLong = [](int a, int) { return a; };
    move.RandomFloat = [](float a, float) { return a; };
    move.PM_GetModelType = [](model_s*) { return 0; };
    move.COM_FileSize = [](char*) { return 0; };
    move.COM_LoadFile = [](char*, int, int*) -> byte* { return nullptr; };
    move.COM_FreeFile = [](void*) {};
    move.PM_PlaySound = [](int, const char*, float, float, int, int) {};
    move.PM_TraceTexture = [](int, float*, float*) -> const char* { return "CONCRETE"; };
    move.PM_PlaybackEventFull = [](int, int, unsigned short, float, float*, float*, float, float, int, int, int, int) {};
    move.cmd.msec = 10;
    return value;
}

void Run(std::string_view group)
{
    Require(group == "walk" || group == "run" || group == "crouch" || group == "jump" ||
        group == "wall" || group == "freeze", "known actual native-PM group");
    Settings(); Case c(MusicResources);
    auto* rules = static_cast<CHalfLifeMultiplay*>(g_pGameRules);
    rules->m_bFreezePeriod = FALSE;
    const float weaponSpeed = c.player.m_pActiveItem->GetMaxSpeed();
    Admit(c, 100); Require(Frame(c, 100), "actual current-life movement owner is ready");
    const auto life = std::to_string(Grant(c).life);
    if (group != "walk") Command(c, "sprintON", life);
    g_engfuncs.pfnGetPhysicsInfoString = [](const edict_t*) -> const char* { return "\\native_test\\retained"; };

    Require(SetDllDirectoryA(native_sample::dependencies), "sandbox native dependencies are selected");
    const auto module = LoadLibraryExA(native_sample::path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Require(module != nullptr, "identity-checked unmodified native client loads");
    struct Release { HMODULE module; ~Release() { FreeLibrary(module); SetDllDirectoryA(nullptr); } } release{module};
    const auto initialize = reinterpret_cast<void (*)(playermove_t*)>(GetProcAddress(module, "HUD_PlayerMoveInit"));
    const auto predict = reinterpret_cast<void (*)(playermove_t*, int)>(GetProcAddress(module, "HUD_PlayerMove"));
    Require(initialize && predict && c.f.engine.api.pfnPM_Init && c.f.engine.api.pfnPM_Move,
        "actual client and GameDLL expose complete public PM entry points");
    movevars_t variables{};
    variables.gravity = 800; variables.maxspeed = 320; variables.stopspeed = 75;
    variables.accelerate = 5; variables.airaccelerate = 10; variables.friction = 4;
    variables.wateraccelerate = 10; variables.waterfriction = 1; variables.edgefriction = 2;
    variables.stepsize = 18; variables.maxvelocity = 2000; variables.entgravity = 1;
    auto client = Movement(variables), server = Movement(variables);
    server->player_index = client->player_index = c.player.entindex() - 1;
    moving = client.get(); initialize(moving);
    moving = server.get(); c.f.engine.api.pfnPM_Init(moving);
    wall = group == "wall";
    float maxPositionDifference{}, maxVelocityDifference{};
    unsigned clamped{}, frozen{}, denied{};
    for (unsigned step = 0; step != 160; ++step)
    {
        const float now = 100.01f + step * .01f;
        c.player.pev->origin = Vector(server->origin);
        c.player.pev->flags = server->flags & ~FL_FROZEN;
        if (group == "crouch")
        {
            c.player.pev->flags |= FL_DUCKING;
            for (auto* move : {client.get(), server.get()})
            { move->usehull = 1; if (step == 0) move->origin[2] = 18; }
        }
        rules->m_bFreezePeriod = group == "freeze" && step < 100;
        if (rules->m_bFreezePeriod) c.player.pev->flags |= FL_FROZEN;
        Require(Frame(c, now), "real frame advances the authoritative movement owner");
        clientdata_t snapshot{};
        c.f.engine.api.pfnUpdateClientData(c.player.edict(), 0, &snapshot);
        const float expected = rules->m_bFreezePeriod ? 1.f :
            weaponSpeed * (group == "walk" ? .5f : group == "crouch" ? .8f : 1.f);
        Expect(snapshot.maxspeed == expected, "actual native snapshot carries one authoritative speed transform");
        const bool jumpDenied = CSOL_ActionStaminaJumpBlocked(c.player);
        const bool departure = client->onground != -1 && !(client->oldbuttons & IN_JUMP) && !jumpDenied;
        const auto amount = Amount(c);
        for (auto* move : {client.get(), server.get()})
        {
            moving = move;
            move->time = now; move->clientmaxspeed = snapshot.maxspeed;
            move->maxspeed = variables.maxspeed; move->flags = snapshot.flags;
            move->cmd = {}; move->cmd.msec = 10; move->cmd.forwardmove = 400;
            if (group == "crouch") move->cmd.buttons |= IN_DUCK;
            if (group == "jump" && step % 4 == 0) move->cmd.buttons |= IN_JUMP;
            if (move == client.get())
            {
                if (custom::ApplyJumpPredictionGate(*move, jumpDenied)) ++denied;
                predict(move, 0); ++nativeMoves;
                Expect(Amount(c) == amount, "native prediction does not debit server stamina");
            }
            else { c.f.engine.api.pfnPM_Move(move, 1); ++serverMoves; }
            Require(!CSOL_RulesRejected(), "actual complete PM keeps the scenario usable");
            Require(std::isfinite(move->origin[0]) && std::isfinite(move->origin[2]) &&
                std::isfinite(move->velocity[0]) && std::isfinite(move->velocity[2]), "complete PM remains finite");
            Expect(move->maxspeed == expected, "complete native PM consumes the snapshot speed cap");
            // Thaw preserves native acceleration from rest. Give this explicit
            // test stream0.5 seconds after release before checking steady speed.
            const bool settled = group == "freeze" ? step >= 150 : step > 130;
            if (!wall && !rules->m_bFreezePeriod && settled && group != "jump" && group != "crouch")
            {
                Expect(std::abs(std::hypot(move->velocity[0], move->velocity[1]) - expected) < .001f,
                    "complete PM reaches the authoritative movement speed");
                ++clamped;
            }
            if (rules->m_bFreezePeriod)
            {
                Expect(move->origin[0] == 0, "actual frozen PM prevents horizontal displacement");
                ++frozen;
            }
            if (group == "freeze" && step == 100)
                Expect(move->velocity[0] > 0 && move->velocity[0] < expected,
                    "thaw resumes native acceleration instead of instantly assigning velocity");
            if (wall) Expect(move->origin[0] <= 240.001f, "actual native hull trace prevents crossing the wall");
        }
        for (unsigned axis = 0; axis != 3; ++axis)
        {
            maxPositionDifference = std::max(maxPositionDifference, std::abs(client->origin[axis] - server->origin[axis]));
            maxVelocityDifference = std::max(maxVelocityDifference, std::abs(client->velocity[axis] - server->velocity[axis]));
        }
        if (step == 0 || step == 130 || step == 159)
            std::cout << "step=" << step << " expected=" << expected << " speed=" << client->velocity.Length2D()
                << " x=" << client->origin[0] << " z=" << client->origin[2] << " flags=" << client->flags
                << " ground=" << client->onground << " amount=" << Amount(c) << " cmd=" << client->cmd.forwardmove
                << " frametime=" << client->frametime << '\n';
        if (group == "jump" && step % 4 == 0 && departure)
        {
            Expect(client->onground == -1 && server->onground == -1 &&
                client->velocity[2] > 0 && server->velocity[2] > 0,
                "client and server depart in the same eligible command");
            ++departures;
        }
    }
    Expect(maxPositionDifference < .01f && maxVelocityDifference < .01f,
        "unmodified native client and actual server complete PM agree across the command stream");
    if (group == "jump") Expect(departures >= 3, "several native jumps are executed");
    if (wall) Expect(wallContacts > 0 && server->origin[0] >= 239.99f,
        "both complete PM paths actually reach the controlled wall");
    std::cout << "nativeMoves=" << nativeMoves << " serverMoves=" << serverMoves << " departures=" << departures
        << " wallContacts=" << wallContacts << " clamped=" << clamped << " frozen=" << frozen << " denied=" << denied
        << " maxPositionDifference=" << maxPositionDifference << " maxVelocityDifference=" << maxVelocityDifference << '\n';
}
}
int main(int argc, char** argv)
{
    try
    {
        sdk_test::Require(argc == 2, "one native movement group");
        standalone_native_test::Run(argv[1]);
    }
    catch (const std::exception& error)
    { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
