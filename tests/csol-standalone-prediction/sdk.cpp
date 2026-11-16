// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the real frame owner -> UpdateClientData -> PM_CheckParameters.
// Engine/world edges come from the existing explicit SDK fixture. This is
// not a graphical client, a network transport or a complete PM movement run.
#define CSOL_STANDALONE_FRAME_LIBRARY
#include "../csol-standalone-frame/sdk.cpp"
#include "pm_defs.h"
#include "pm_movevars.h"

extern playermove_t* pmove;
void PM_CheckParameters();

namespace standalone_prediction_test
{
using namespace standalone_frame_test;

void SnapshotAndClamp(Case& c, float expected, bool frozen = false)
{
    g_engfuncs.pfnGetPhysicsInfoString = [](const edict_t*) -> const char*
    { return "\\native_test\\retained"; };
    clientdata_t snapshot{};
    c.f.engine.api.pfnUpdateClientData(c.player.edict(), 0, &snapshot);
    Require(!CSOL_RulesRejected(), "actual ClientData publication keeps the map usable");
    Expect(snapshot.maxspeed == expected && snapshot.maxspeed == c.player.pev->maxspeed,
        "actual native snapshot carries the frame-owned speed without a second multiplier");
    Expect(std::string_view(snapshot.physinfo).starts_with("\\native_test\\retained"),
        "speed publication retains the native physics information");
    const auto amount = Amount(c);
    for (int server : {0, 1})
    {
        const auto storage = std::make_unique<playermove_t>();
        auto& move = *storage;
        movevars_t variables{};
        variables.rollangle = 2; variables.rollspeed = 200;
        move.movevars = &variables;
        move.server = server; move.multiplayer = TRUE;
        move.movetype = MOVETYPE_WALK;
        move.maxspeed = 900;
        move.clientmaxspeed = snapshot.maxspeed;
        move.flags = snapshot.flags;
        move.cmd.forwardmove = 400; move.cmd.sidemove = 300;
        move.frametime = .01f;
        auto* previous = pmove;
        pmove = &move;
        PM_CheckParameters();
        pmove = previous;
        Expect(move.maxspeed == expected && move.clientmaxspeed == snapshot.maxspeed,
            "actual shared PM consumes the same native speed in server and prediction modes");
        const float speed = std::hypot(move.cmd.forwardmove, move.cmd.sidemove);
        Expect(frozen ? speed == 0 : std::abs(speed - expected) <= .0001f,
            "actual shared PM clamps diagonal input and honors authoritative freeze flags");
        Expect(Amount(c) == amount, "prediction/cap queries do not advance recovery or charge displacement twice");
    }
}

void Run(std::string_view group)
{
    standalone_frame_test::Settings();
    Case c(MusicResources);
    auto* rules = static_cast<CHalfLifeMultiplay*>(g_pGameRules);
    rules->m_bFreezePeriod = FALSE;
    const float weapon = c.player.m_pActiveItem->GetMaxSpeed();
    Require(std::isfinite(weapon) && weapon > 0, "real current weapon has native movement speed");
    Require(Frame(c, 100), "real movement frame prepares walk and current-life input");
    const auto grant = Grant(c);
    const auto life = std::to_string(grant.life);
    SnapshotAndClamp(c, weapon * .5f);
    Command(c, "sprintON", life);
    SnapshotAndClamp(c, weapon * .5f); // A queued request alone does not run a new frame.
    Require(Frame(c, 100.02f), "accepted current-life sprint reaches the next real frame");
    SnapshotAndClamp(c, weapon);
    if (group == "crouch")
    {
        c.player.pev->flags |= FL_DUCKING;
        Require(Frame(c, 100.03f), "native crouch consumes the current frame");
        SnapshotAndClamp(c, weapon * .8f);
    }
    else if (group == "freeze")
    {
        rules->m_bFreezePeriod = TRUE;
        c.player.ResetMaxSpeed();
        SnapshotAndClamp(c, 1);
        c.player.pev->flags |= FL_FROZEN;
        SnapshotAndClamp(c, 1, true);
        c.player.pev->flags &= ~FL_FROZEN;
        rules->m_bFreezePeriod = FALSE;
        Require(Frame(c, 100.03f), "thaw preserves the current run owner");
        SnapshotAndClamp(c, weapon);
    }
    else if (group == "engine-limit")
    {
        c.f.engine.Set("sv_maxspeed", "100");
        c.player.ResetMaxSpeed();
        SnapshotAndClamp(c, 100);
    }
    else
    {
        Require(group == "flow", "known prediction integration group");
        c.player.pev->origin.x += 80;
        Require(Frame(c, 100.03f), "real frame charges accepted horizontal run displacement");
        Expect(Amount(c) == 92, "frame owns the original0.1 per-unit debit");
        SnapshotAndClamp(c, weapon);
        Command(c, "sprintOFF", life);
        Require(Frame(c, 100.04f), "release is consumed by the next actual frame");
        SnapshotAndClamp(c, weapon * .5f);
    }
}
}

int main(int argc, char** argv)
{
    try
    {
        sdk_test::Require(argc == 2, "one standalone prediction integration group");
        standalone_prediction_test::Run(argv[1]);
    }
    catch (const std::exception& error)
    { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
