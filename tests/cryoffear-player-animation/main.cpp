// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/player_animation.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

Globals globals;
Globals* gpGlobals = &globals;
const void* currentModel{};
edict_t* currentEdict{};
BotEdge bots;
BotEdge* TheBots = &bots;
using BOOL = int;
using studiohdr_t = ::studiohdr_t;
using mstudioseqdesc_t = ::mstudioseqdesc_t;
constexpr int FALSE = 0;
float Q_sqrt(float value) { return std::sqrt(value); }
#include "sdk_sequence_info.inc"
#include "sdk_sequence_flags.inc"
int CBasePlayer::GetSequenceFlags() { return ::GetSequenceFlags(const_cast<void*>(currentModel), pev); }
void CBasePlayer::ResetSequenceInfo()
{
    ++resetCalls;
#include "sdk_reset_sequence.inc"
}

namespace
{
using namespace guest::cof;
using namespace guest::cof::server;
using Model = std::vector<std::byte>;
int checks{}, failures{};
void Check(bool ok, const char* reason)
{ ++checks; if (!ok) { ++failures; std::cerr << reason << '\n'; } }
Model Read(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Missing actual character model");
    std::vector<char> data{std::istreambuf_iterator<char>(input), {}};
    Model result(data.size()); std::memcpy(result.data(), data.data(), data.size()); return result;
}
template<class T> T Object(const Model& model, std::size_t offset)
{
    if (offset > model.size() || sizeof(T) > model.size() - offset) throw std::runtime_error("Bad real-model oracle offset");
    T result; std::memcpy(&result, model.data() + offset, sizeof(T)); return result;
}
int Compare(const char* left, const char* right) { return _stricmp(left, right); }
int Lookup(const Model& model, const char* label)
{
    const auto header = Object<studiohdr_t>(model, 0);
    for (int i = 0; i < header.numseq; ++i)
        if (!_stricmp(Object<mstudioseqdesc_t>(model, header.seqindex + i * sizeof(mstudioseqdesc_t)).label, label)) return i;
    return -1;
}
bool Bits(float left, float right)
{ return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right); }
constexpr PlayerModelGeneration Generation{19, 23};
PlayerAnimationPlan Plan(const char* standingMelee, const char* crouchingMelee)
{
    return {{{"ref_aim_onehanded", "crouch_aim_onehanded"},
             {"ref_aim_onehanded", "crouch_aim_onehanded"},
             {"ref_shoot_onehanded", "crouch_shoot_onehanded"},
             {"ref_reload_onehanded", "crouch_reload_onehanded"},
             {"ref_aim_onehanded", "crouch_aim_onehanded"},
             {standingMelee, crouchingMelee}}};
}
struct Fixture
{
    entvars_t vars{};
    edict_t ed{};
    CBasePlayer player;
    PlayerAnimationAdapter adapter;
    std::string error;
    explicit Fixture(const Model& model)
    {
        globals.time = 100; gpGlobals = &globals; currentModel = model.data(); TheBots = &bots; bots = {};
        vars.modelindex = 11; vars.health = 100; vars.gaitsequence = 5; vars.framerate = 1;
        ed.serialnumber = 3; player.pev = &vars; player.ed = &ed; currentEdict = &ed;
        player.m_Activity = player.m_IdealActivity = ACT_WALK; player.m_fSequenceFinished = true;
    }
    template<class Weapon> bool Prepare(const Model& model, Weapon weapon, const PlayerAnimationPlan& plan)
    { return adapter.Prepare(player, model, Generation, weapon, plan, Compare, error); }
    template<class Weapon> PlayerAnimationResult Apply(Weapon weapon, PlayerAction action)
    { return adapter.Apply(player, Generation, weapon, action, globals.time); }
};
void Metadata(const Fixture& f, const Model& model, const char* label)
{
    const auto index = Lookup(model, label);
    Check(index >= 0 && f.vars.sequence == index, "Applied index differs from actual role label");
    if (index < 0) return;
    const auto header = Object<studiohdr_t>(model, 0);
    const auto desc = Object<mstudioseqdesc_t>(model, header.seqindex + index * sizeof(mstudioseqdesc_t));
    const auto rate = desc.numframes <= 1 ? 256.0f : desc.fps * 256.0f / (desc.numframes - 1);
    Check(Bits(f.player.m_flFrameRate, rate), "Real SDK ResetSequenceInfo frame rate differs");
    Check(f.player.m_fSequenceLoops == ((desc.flags & STUDIO_LOOPING) != 0), "Real SDK loop flag differs");
    Check(f.vars.animtime == globals.time && f.vars.framerate == 1 && !f.player.m_fSequenceFinished &&
          f.player.m_flLastEventCheck == globals.time, "Real SDK reset lifecycle differs");
    Check(f.vars.gaitsequence == 5, "Upper-body adapter changed native gait");
}
template<class Weapon> void ActionsFor(const Model& model, const PlayerAnimationPlan& plan,
                                      unsigned begin, unsigned end)
{
    for (unsigned identity = begin; identity < end; ++identity)
        for (bool crouching : {false, true})
            for (unsigned slot = 0; slot < PlayerActionCount; ++slot)
            {
                Fixture f(model);
                const auto weapon = static_cast<Weapon>(identity);
                const auto action = static_cast<PlayerAction>(slot);
                if (crouching) f.vars.flags |= FL_DUCKING;
                Check(f.Prepare(model, weapon, plan), "Explicit full actual-model plan rejected");
                f.vars.frame = 170;
                Check(f.Apply(weapon, action) == PlayerAnimationResult::Applied, "Valid real action not applied");
                Metadata(f, model, (crouching ? plan[slot].crouching : plan[slot].standing).data());
                Check(f.vars.frame == 0 && f.player.resetCalls == 1, "Action reset count/frame differs");
                const bool fired = action == PlayerAction::Fire, melee = action == PlayerAction::WeaponMelee;
                const bool reload = action == PlayerAction::Reload;
                Check(bots.fired == int(fired) && bots.reloaded == int(reload), "Native bot event misclassified melee or pose");
                Check(f.player.m_Activity == (fired || melee ? ACT_RANGE_ATTACK1 : reload ? ACT_RELOAD : ACT_WALK),
                      "Native activity changed incorrectly");
                Check(f.player.m_flLastFired == (fired || melee ? globals.time : 0), "Last-fired native run guard differs");
                const auto selected = f.adapter.Select(Generation, weapon, action, crouching);
                Check(selected && Bits(selected->sdkDuration, float(selected->frames) / selected->fps), "Cached SDK duration changed");
                f.vars.frame = 80;
                const auto again = f.Apply(weapon, action);
                if (fired || melee || (reload && !f.player.m_fSequenceLoops))
                    Check(again == PlayerAnimationResult::Applied && f.vars.frame == 0 && f.player.resetCalls == 2,
                          "Explicit event did not restart action");
                else if (!reload)
                    Check(again == PlayerAnimationResult::Unchanged && f.vars.frame == 80 && f.player.resetCalls == 1,
                          "Pose reapplication restarted animation");
            }
}
void Actions(const Model& model, const PlayerAnimationPlan& plan)
{ ActionsFor<Pistol>(model, plan, 0, static_cast<unsigned>(Pistol::Count)); }
void LongGunActions(const Model& model, PistolAnimationRig rig)
{
    for (unsigned i = 0; i < static_cast<unsigned>(LongGun::Count); ++i)
    {
        const auto weapon = static_cast<LongGun>(i);
        const auto plan = RecommendedLongGunAnimationPlan(weapon, rig);
        Check(plan.has_value(), "Explicit longgun authoring plan missing");
        if (!plan) continue;
        ActionsFor<LongGun>(model, *plan, i, i + 1);
        const bool compact = weapon == LongGun::TMP || weapon == LongGun::MP5;
        const char* melee = rig == PistolAnimationRig::Stock ? "ref_shoot_knife" :
            compact ? "ref_shoot_nataknife" : "ref_shoot2_m16a1ep";
        Check((*plan)[5].standing == melee, "Longgun explicit melee family differs");
        Fixture f(model);
        Check(f.Prepare(model, weapon, *plan), "Longgun identity setup failed");
        Check(f.Apply(Pistol::Count, PlayerAction::Fire) == PlayerAnimationResult::Rejected &&
              f.Apply(Pistol::Glock, PlayerAction::Fire) == PlayerAnimationResult::Rejected,
              "Pistol identity reused longgun cache or invalid enum collided");
        Check(f.Apply(LongGun::Count, PlayerAction::Fire) == PlayerAnimationResult::Rejected,
              "Invalid longgun identity applied");
        Check(!f.Prepare(model, LongGun::Count, *plan) &&
              f.Apply(weapon, PlayerAction::Fire) == PlayerAnimationResult::Rejected,
              "Failed longgun bind retained prior weapon cache");
    }
    Check(!RecommendedLongGunAnimationPlan(LongGun::Count, rig) &&
          !RecommendedLongGunAnimationPlan(LongGun::TMP, static_cast<PistolAnimationRig>(255)),
          "Invalid longgun recommendation accepted");
}
void Guards(const Model& model)
{
    const auto plan = Plan("ref_shoot_knife", "crouch_shoot_knife");
    Fixture f(model);
    Check(f.Prepare(model, Pistol::Glock, plan), "Guard setup failed");
    const auto rejected = [&](PlayerModelGeneration generation, Pistol weapon, PlayerAction action, float now)
    {
        const auto old = f.vars; const auto resets = f.player.resetCalls;
        Check(f.adapter.Apply(f.player, generation, weapon, action, now) == PlayerAnimationResult::Rejected,
              "Invalid actor/action/model generation was accepted");
        Check(std::memcmp(&old, &f.vars, sizeof(old)) == 0 && f.player.resetCalls == resets, "Rejected action mutated player");
    };
    rejected({20,23}, Pistol::Glock, PlayerAction::Fire, globals.time);
    rejected({19,24}, Pistol::Glock, PlayerAction::Fire, globals.time);
    rejected({}, Pistol::Glock, PlayerAction::Fire, globals.time);
    rejected(Generation, Pistol::VP70, PlayerAction::Fire, globals.time);
    rejected(Generation, Pistol::Glock, PlayerAction::Count, globals.time);
    rejected(Generation, Pistol::Glock, PlayerAction::Fire, globals.time - 1);
    rejected(Generation, Pistol::Glock, PlayerAction::Fire, std::numeric_limits<float>::quiet_NaN());
    ++f.ed.serialnumber; rejected(Generation, Pistol::Glock, PlayerAction::Fire, globals.time); --f.ed.serialnumber;
    ++f.vars.modelindex; rejected(Generation, Pistol::Glock, PlayerAction::Fire, globals.time); --f.vars.modelindex;
    f.vars.deadflag = DEAD_DEAD; rejected(Generation, Pistol::Glock, PlayerAction::Fire, globals.time); f.vars.deadflag = DEAD_NO;
    for (float health : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()})
    { f.vars.health = health; rejected(Generation, Pistol::Glock, PlayerAction::Fire, globals.time); }
    f.vars.health = 100;
    CBasePlayer other = f.player;
    Check(f.adapter.Apply(other, Generation, Pistol::Glock, PlayerAction::Fire, globals.time) == PlayerAnimationResult::Rejected,
          "Other owner reused actor cache");
    for (Activity activity : {ACT_SWIM, ACT_DIESIMPLE, ACT_HOVER, ACT_HOP, ACT_LEAP})
    {
        f.player.m_Activity = activity; const auto old = f.vars;
        Check(f.Apply(Pistol::Glock, PlayerAction::Fire) == PlayerAnimationResult::PreservedNative &&
              std::memcmp(&old, &f.vars, sizeof(old)) == 0, "Native movement/death activity overwritten");
    }
    f.player.m_Activity = ACT_WALK; f.vars.flags |= FL_FROZEN;
    Check(f.Apply(Pistol::Glock, PlayerAction::Fire) == PlayerAnimationResult::PreservedNative, "Frozen actor fired animation");
    f.vars.flags &= ~FL_FROZEN; f.player.m_flFlinchTime = globals.time + 1;
    Check(f.Apply(Pistol::Glock, PlayerAction::Fire) == PlayerAnimationResult::PreservedNative, "Flinch lock overwritten");
    f.player.m_flFlinchTime = 0;
    for (Activity activity : {ACT_RANGE_ATTACK1, ACT_RANGE_ATTACK2, ACT_RELOAD, ACT_FLINCH, ACT_LARGE_FLINCH})
    {
        f.player.m_Activity = activity; f.player.m_fSequenceFinished = false;
        for (auto action : {PlayerAction::Hold, PlayerAction::Move, PlayerAction::IronSight})
            Check(f.Apply(Pistol::Glock, action) == PlayerAnimationResult::PreservedNative, "Pose interrupted unfinished action");
    }
    f.player.m_fSequenceFinished = true;
    Check(f.Apply(Pistol::Glock, PlayerAction::Hold) == PlayerAnimationResult::Applied, "Finished action did not return to hold");
    Check(f.player.m_Activity == ACT_WALK && f.player.m_IdealActivity == ACT_WALK &&
          f.Apply(Pistol::Glock, PlayerAction::Hold) == PlayerAnimationResult::Unchanged,
          "Returning pose retained finished attack activity and blocked itself");
    f.player.m_Activity = ACT_WALK;
    Check(f.Apply(Pistol::Glock, PlayerAction::Fire) == PlayerAnimationResult::Applied, "Fire guard setup failed");
    Check(f.Apply(Pistol::Glock, PlayerAction::WeaponMelee) == PlayerAnimationResult::Applied,
          "Melee event did not explicitly supersede fired action");
    TheBots = nullptr;
    Check(f.Apply(Pistol::Glock, PlayerAction::Fire) == PlayerAnimationResult::Applied, "No Bot manager blocked action");
    TheBots = &bots;
    f.adapter.Reset(); rejected(Generation, Pistol::Glock, PlayerAction::Fire, globals.time);
    auto bad = plan; bad[5] = {"ref_shoot2_onehanded", "crouch_shoot2_onehanded"};
    Check(f.Prepare(model, Pistol::Glock, plan) && !f.Prepare(model, Pistol::Glock, bad), "Missing shoot2 plan accepted");
    rejected(Generation, Pistol::Glock, PlayerAction::Fire, globals.time);
    currentModel = nullptr;
    Check(!f.Prepare(model, Pistol::Glock, plan), "Unrelated offline buffer accepted as actual engine model");
    currentModel = model.data();
    Check(!f.Prepare(model, Pistol::Count, plan), "Invalid pistol accepted");
}
void LoopingReload(const Model& source)
{
    Model model = source;
    const int index = Lookup(model, "ref_reload_onehanded");
    const auto header = Object<studiohdr_t>(model, 0);
    const auto at = header.seqindex + index * sizeof(mstudioseqdesc_t);
    auto descriptor = Object<mstudioseqdesc_t>(model, at);
    descriptor.flags |= STUDIO_LOOPING;
    std::memcpy(model.data() + at, &descriptor, sizeof(descriptor));
    Fixture f(model);
    Check(f.Prepare(model, Pistol::P345, Plan("ref_shoot_knife", "crouch_shoot_knife")), "Loop test setup rejected");
    f.vars.sequence = index; f.vars.frame = 173; f.player.m_fSequenceLoops = true;
    Check(f.Apply(Pistol::P345, PlayerAction::Reload) == PlayerAnimationResult::Unchanged &&
          f.vars.frame == 173 && f.player.resetCalls == 0 && !(f.vars.effects & EF_NOINTERP),
          "Already-current looping reload was restarted or forced no-interpolation");
    f.player.m_Activity = ACT_RUN; f.player.m_fSequenceFinished = true;
    Check(f.Apply(Pistol::P345, PlayerAction::Move) == PlayerAnimationResult::Applied &&
          f.player.m_Activity == ACT_RUN && f.player.m_IdealActivity == ACT_RUN && f.vars.gaitsequence == 5,
          "Explicit movement pose changed native run/gait");
}
}
int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    try
    {
        const auto urban = Read(argv[1]), male = Read(argv[2]), female = Read(argv[3]);
        const auto stock = Plan("ref_shoot_knife", "crouch_shoot_knife");
        for (const auto* model : {&urban, &male, &female}) { Actions(*model, stock); Guards(*model); LoopingReload(*model); }
        for (const auto* model : {&urban, &male, &female}) LongGunActions(*model, PistolAnimationRig::Stock);
        for (const auto* model : {&male, &female})
        {
            LongGunActions(*model, PistolAnimationRig::CSO);
            const auto recommended = RecommendedPistolAnimationPlan(Pistol::Glock, PistolAnimationRig::CSO);
            Check(recommended && (*recommended)[5].standing == "ref_shoot_nataknife" &&
                  (*recommended)[5].crouching == "crouch_shoot_nataknife", "Explicit CSO authoring recommendation differs");
            if (recommended) Actions(*model, *recommended);
            Actions(*model, Plan("ref_shoot2_skull8", "crouch_shoot2_skull8"));
            Actions(*model, Plan("ref_shoot2_m16a1ep", "crouch_shoot2_m16a1ep"));
        }
        Fixture f(urban);
        for (unsigned i = 0; i < static_cast<unsigned>(Pistol::Count); ++i)
        {
            const auto recommended = RecommendedPistolAnimationPlan(static_cast<Pistol>(i), PistolAnimationRig::Stock);
            Check(recommended && (*recommended)[5].standing == "ref_shoot_knife" &&
                  f.Prepare(urban, static_cast<Pistol>(i), *recommended), "Explicit stock four-pistol recommendation rejected");
        }
        Check(!RecommendedPistolAnimationPlan(Pistol::Count, PistolAnimationRig::Stock) &&
              !RecommendedPistolAnimationPlan(Pistol::Glock, static_cast<PistolAnimationRig>(255)), "Invalid recommendation identity accepted");
        const auto unsupported = RecommendedPistolAnimationPlan(Pistol::Glock, PistolAnimationRig::CSO);
        Check(unsupported && !f.Prepare(urban, Pistol::Glock, *unsupported), "CSO recommendation silently fell back on stock role");
        Check(!f.Prepare(urban, Pistol::Glock, Plan("ref_shoot2_skull8", "crouch_shoot2_skull8")),
              "Stock model silently used unsupported CSO extended track");
        for (unsigned i = 0; i < static_cast<unsigned>(LongGun::Count); ++i)
        {
            const auto weapon = static_cast<LongGun>(i);
            const auto plan = RecommendedLongGunAnimationPlan(weapon, PistolAnimationRig::CSO);
            Check(plan && !f.Prepare(urban, weapon, *plan), "CSO longgun plan silently fell back on stock model");
        }
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
