// SPDX-License-Identifier: GPL-3.0-or-later
#include "beamgun_state.hpp"
#include <rapidjson/document.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace csol;
using JSON = rapidjson::Value;
namespace
{
int checks{}, samples{};
std::string context;
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok) throw std::runtime_error(context + ": " + what);
}
void Near(float actual, float expected, const char* field)
{
    ++checks;
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 1e-6f)
        throw std::runtime_error(context + ": " + field + " expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
}
int Int(const JSON& o, const char* key, int fallback = 0) { return o.HasMember(key) ? o[key].GetInt() : fallback; }
float Float(const JSON& o, const char* key, float fallback = 0) { return o.HasMember(key) ? o[key].GetFloat() : fallback; }
rapidjson::Document Read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read original oracle capture");
    const std::string data((std::istreambuf_iterator<char>(in)), {});
    rapidjson::Document doc;
    doc.Parse(data.data(), data.size());
    if (doc.HasParseError() || !doc.IsObject()) throw std::runtime_error("bad oracle JSON");
    return doc;
}

void Scenarios()
{
    context = "right-button combo into alpha and charged shot";
    BeamWeaponState s;
    s.clip = 80;
    auto a = s.Secondary();
    Check(a.animation == BeamShootB1 && s.pending == BeamPending::First, "first animation");
    s.Decrement(0.199f);
    Check(s.DelayedAttack().attack == BeamAttack::None, "no premature damage");
    s.Decrement(0.0011f);
    Check(s.DelayedAttack().attack == BeamAttack::Secondary1, "first damage once");
    Check(s.DelayedAttack().attack == BeamAttack::None, "no repeated damage");
    s.Decrement(0.8f);
    Check(s.Secondary().animation == BeamShootB2, "second animation in combo window");
    s.Decrement(0.3f);
    Check(s.DelayedAttack().attack == BeamAttack::Secondary2, "second damage at zero");
    Check(s.charge == BeamCharge::Alpha, "enter alpha");
    s.Decrement(0.5f);
    a = s.Primary(true);
    Check(a.animation == BeamChargeShoot && s.clip == 80, "charged shot does not consume magazine");
    s.Decrement(1.4f);
    a = s.Primary(true);
    Check(a.animation == BeamChargeShootExit && s.charge == BeamCharge::None, "charged exit shot");

    context = "beta gate, consumption and lifecycle";
    s = {}; s.energy = 100;
    Check(s.FullCharge(1).attack == BeamAttack::None && s.energy == 100, "one button retains energy");
    s.reloading = true;
    Check(s.FullCharge(0x801).attack == BeamAttack::None, "reload gate");
    s.reloading = false;
    Check(s.FullCharge(0x801).attack == BeamAttack::FullCharge && s.energy == 0, "consume once");
    Check(s.FullCharge(0x801).attack == BeamAttack::None, "held request cannot consume twice");
    s.Holster();
    Check(s.Charged() && s.chargeTime == 7, "holster preserves beta");
    s.Decrement(1);
    Check(s.BeginDeploy() == BeamChargeDraw && s.chargeTime == 6, "redeploy charged with elapsed time");
    s.Removed();
    Check(!s.Charged() && s.chargeTime == 0, "inventory removal cancels charge");
    Check(s.BeginDeploy() == BeamDraw, "redeploy ordinary after removal");
    s.pending = BeamPending::First; s.dive = BeamDive::Start;
    s.FinishDeploy(false);
    Check(s.pending == BeamPending::First && s.dive == BeamDive::Start, "failed deploy keeps action");
    s.FinishDeploy(true);
    Check(s.pending == BeamPending::None && s.dive == BeamDive::None, "successful deploy resets action");

    context = "dive expiration and landing";
    s = {}; s.charge = BeamCharge::Beta; s.chargeTime = 0;
    Check(s.Secondary().clearGround, "zero remaining accepts dive");
    s.Decrement(0.5f);
    Check(s.ExpireCharge().animation == -1 && s.Charged(), "expiration does not interrupt dive");
    a = s.UpdateDive(false, 3);
    Check(a.descend && !a.stopVelocity && s.dive == BeamDive::Start, "walk in air descends");
    a = s.UpdateDive(true, 3);
    Check(a.stopVelocity && s.dive == BeamDive::Landed && a.attack == BeamAttack::None, "landing only changes phase");
    a = s.UpdateDive(true, 3);
    Check(a.animation == BeamDiveEndExit && a.attack == BeamAttack::DiveEnd, "following call finishes");
    Check(s.UpdateDive(true, 3).attack == BeamAttack::None, "no repeated landing");
    Check(s.clip == 0, "dive is independent of magazine");

    context = "countdown ownership and invalid time";
    s = {}; s.nextPrimary = 1; s.nextSecondary = 2; s.idleTime = 3;
    s.chargeTime = s.comboTime = s.delayedTime = 0.1f;
    s.Decrement(std::numeric_limits<float>::infinity());
    s.Decrement(std::numeric_limits<float>::quiet_NaN());
    s.Decrement(-1);
    Near(s.chargeTime, 0.1f, "invalid delta ignored");
    s.Decrement(1);
    Near(s.chargeTime, -0.001f, "floor");
    Check(s.nextPrimary == 1 && s.nextSecondary == 2 && s.idleTime == 3, "ordinary timers left to engine");
}

void OriginalStates(const std::filesystem::path& directory, bool lifecycle)
{
    auto document = Read(directory / (lifecycle ? "lifecycle-vectors.json" : "state-vectors.json"));
    for (const auto& row : document["samples"].GetArray())
    {
        const std::string entry = row["entry"].GetString();
        const auto& c = row["case"];
        context = std::string(lifecycle ? "lifecycle/" : "state/") + row["binary"].GetString() + "/" + entry + "/" + std::to_string(samples);
        BeamWeaponState s;
        s.clip = 80; s.energy = 100;
        bool grounded = true; int moveType = 3;
        std::vector<int> animations;
        std::vector<int> playerAnimations;
        std::vector<BeamAction> events;
        auto collect = [&](BeamAction action)
        {
            if (action.animation >= 0) animations.push_back(action.animation);
            if (action.playerAnimation >= 0) playerAnimations.push_back(action.playerAnimation);
            if (action.eventMode >= 0) events.push_back(action);
            if (action.clearGround) grounded = false;
            return action;
        };
        BeamAction motion;
        if (entry == "primary")
        {
            s.charge = BeamCharge(Int(c,"mode"));
            s.clip = Int(c,"clip");
            s.chargeTime = Float(c,"chargeRemaining",Float(c,"remaining"));
            collect(s.Primary(!lifecycle));
        }
        else if (entry == "secondary")
        {
            s.charge = BeamCharge(Int(c,"mode"));
            s.chargeTime = lifecycle ? Float(c,"remaining") : 2.5f;
            s.comboTime = Float(c,"comboRemaining");
            collect(s.Secondary());
        }
        else if (entry == "charged")
        {
            s.charge = BeamCharge(Int(c,"initialMode"));
            s.reloading = Int(c,"reloading") != 0;
            s.energy = Int(c,"charge",100);
            collect(s.FullCharge(Int(c,"pressed",0x801)));
        }
        else if (entry == "delayed")
        {
            s.pending = BeamPending(Int(c,"delayedAction"));
            s.delayedTime = Float(c,"delayRemaining");
            collect(s.DelayedAttack());
        }
        else if (entry == "idle")
        {
            s.charge = BeamCharge(Int(c,"mode"));
            s.idleTime = Float(c,"remaining");
            collect(s.Idle());
        }
        else if (entry == "expire")
        {
            s.charge = BeamCharge(Int(c,"mode",1));
            s.chargeTime = Float(c,"remaining",-.01f);
            if (c.HasMember("divePhase"))
            {
                s.dive = BeamDive(Int(c,"divePhase"));
                s.comboTime = .4f; s.delayedTime = .2f; grounded = false;
            }
            collect(s.ExpireCharge());
            collect(s.FullCharge(0));
            motion = collect(s.UpdateDive(grounded,moveType));
        }
        else if (entry == "reload")
        {
            s.charge = BeamCharge(Int(c,"mode"));
            if (Int(c,"reserve"))
            {
                bool found = false;
                for (const auto& call : row["callbacks"].GetArray())
                    if (call.IsObject() && std::string(call["callback"].GetString()) == "defaultReloadSelectionOnly")
                    { Check(s.ReloadAnimation() == call["sequence"].GetInt(), "reload sequence"); found = true; }
                Check(found,"original reload callback");
                collect(s.ReloadStarted(Int(c,"baseResult") != 0));
            }
        }
        else if (entry == "deploy")
        {
            s.charge = BeamCharge(Int(c,"mode")); s.chargeTime = Float(c,"remaining");
            s.dive = BeamDive::Loop; s.pending = BeamPending::Second;
            s.comboTime = .4f; s.delayedTime = .2f;
            const int sequence = s.BeginDeploy();
            for (const auto& call : row["callbacks"].GetArray())
                if (call.IsObject() && std::string(call["callback"].GetString()) == "defaultDeploySelectionOnly")
                    Check(sequence == call["sequence"].GetInt(), "deploy sequence");
            s.FinishDeploy(Int(c,"baseResult") != 0);
        }
        else if (entry == "attach" || entry == "holster")
        {
            s.charge = BeamCharge(Int(c,"mode")); s.chargeTime = 2.5f;
            // Historical capture used the label holster for virtual0x274.
            // Its callers prove inventory removal, not weapon switching.
            if (entry == "attach") s.Attached(Int(c,"baseResult") != 0); else s.Removed();
        }
        else if (entry == "reset")
        {
            s.energy = Int(c,"ammo"); s.dive = BeamDive::Loop; s.pending = BeamPending::Second;
            s.comboTime = s.delayedTime = 1; s.ResetAction();
        }
        else if (entry == "dive")
        {
            if (c.HasMember("chargeRemaining"))
            {
                s.charge = BeamCharge(Int(c,"mode")); s.dive = BeamDive::Landed;
                s.chargeTime = Float(c,"chargeRemaining");
            }
            else if (c.HasMember("delay"))
            {
                s.charge = BeamCharge::Alpha; s.dive = BeamDive(Int(c,"phase"));
                s.chargeTime = 2.5f; s.comboTime = .4f; s.delayedTime = Float(c,"delay");
                grounded = Int(c,"ground") != 0;
                // The old capture stub was misnamed; its true result is fly.
                moveType = Int(c,"bottom") ? 5 : 3;
            }
            else
            {
                s.dive = BeamDive(Int(c,"phase")); s.comboTime = Float(c,"diveRemaining");
                s.delayedTime = 1; grounded = false;
            }
            motion = collect(s.UpdateDive(grounded,moveType));
        }
        else throw std::runtime_error("unhandled original state entry");

        const auto& ints = row["integers"];
        Check(int(s.pending) == Int(ints,"0x64"),"pending");
        Check(s.clip == Int(ints,"0x160"),"clip");
        Check(s.energy == Int(ints,"0x168"),"energy");
        Check(int(s.reloading) == Int(ints,"0x174"),"reloading");
        Check(int(s.dive) == Int(ints,"0x178"),"dive phase");
        Check(int(s.charge) == Int(ints,"0x1c0"),"charge mode");
        const auto& timers = row["timers"];
        Near(s.nextPrimary,Float(timers,"0x134"),"next primary");
        Near(s.nextSecondary,Float(timers,"0x13c"),"next secondary");
        Near(s.idleTime,Float(timers,"0x144"),"idle time");
        Near(s.chargeTime,Float(timers,"0x1cc"),"charge time");
        Near(s.comboTime,Float(timers,"0x1d4"),"combo time");
        Near(s.delayedTime,Float(timers,"0x1dc"),"delayed time");
        // Frozen captures called entvars+0x120 body; the original field table
        // identifies it as skin. Keep the evidence, correct its interpretation.
        Check(int(s.energyFull) == row["body"].GetInt(),"energy skin");
        Check(grounded == ((row["playerFlags"].GetUint() & 512) != 0),"ground flag");
        Check(animations.size() == row["animations"].Size(),"animation count");
        for (unsigned i=0;i<animations.size();++i) Check(animations[i] == row["animations"][i].GetInt(),"animation");
        Check(events.size() == row["events"].Size(),"event count");
        std::vector<int> originalPlayerAnimations;
        for (const auto& call : row["callbacks"].GetArray())
            if (call.IsObject() && std::string(call["callback"].GetString()) == "playerAnimation")
                originalPlayerAnimations.push_back(call["value"].GetInt());
        Check(playerAnimations == originalPlayerAnimations,"player animation callbacks");
        for (unsigned i=0;i<events.size();++i)
        {
            Check(events[i].eventMode == row["events"][i]["bparam2"].GetInt(),"event mode");
            Check(events[i].eventPhase == row["events"][i]["bparam1"].GetInt(),"event phase");
        }
        Near(motion.descend && !motion.stopVelocity ? -500.f : 0.f,row["velocity"][2].GetFloat(),"vertical velocity");
        ++samples;
    }
}

void OriginalTimersAndLanding(const std::filesystem::path& directory)
{
    auto timers = Read(directory / "timer-vectors.json");
    for (const auto& row : timers["samples"].GetArray())
    {
        context = "original countdown/" + std::to_string(samples);
        BeamWeaponState s;
        s.chargeTime = s.comboTime = s.delayedTime = row["remaining"].GetFloat();
        s.Decrement(float(row["commandMsec"].GetInt()) / 1000.f);
        Near(s.chargeTime,row["timers"][0].GetFloat(),"charge");
        Near(s.comboTime,row["timers"][1].GetFloat(),"combo");
        Near(s.delayedTime,row["timers"][2].GetFloat(),"delay");
        ++samples;
    }
    auto landing = Read(directory / "landing-gate-vectors.json");
    for (const auto& row : landing["samples"].GetArray())
    {
        context = "original movetype landing/" + std::to_string(samples);
        BeamWeaponState s;
        s.charge = BeamCharge::Alpha; s.chargeTime = 2.5f;
        s.dive = BeamDive(row["phase"].GetInt()); s.comboTime = .4f; s.delayedTime = row["delay"].GetFloat();
        const auto a = s.UpdateDive(row["ground"].GetInt() != 0,row["movetype"].GetInt());
        Check(a.stopVelocity == row["ended"].GetBool(),"stop velocity");
        Check(s.dive == (row["ended"].GetBool() ? BeamDive::Landed : BeamDive(row["phase"].GetInt())),"landing phase");
        ++samples;
    }
}
}

int main(int argc, char** argv)
{
    try
    {
        Scenarios();
        if (argc == 2)
        {
            OriginalStates(argv[1],false);
            OriginalStates(argv[1],true);
            OriginalTimersAndLanding(argv[1]);
        }
        std::cout << "PASS: " << checks << " checks, " << samples << " original-code samples, " << sizeof(void*)*8 << "-bit process\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
