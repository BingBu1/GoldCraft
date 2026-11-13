// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the complete K3 predictor/event regression and add the real phase
// backend; random spread is a controlled engine boundary in this fixture.
#define main ExistingK3Main
#include "../csol-k3/client.cpp"
#undef main
#include "../../native/csol/client/scenario/core/human_kick.hpp"
#include "../../native/csol/client/weapons/weapon_module.cpp"

namespace csol::client
{
metahook_api_t fixtureApi{};
metahook_api_t* api = &fixtureApi;
engine_studio_api_t studio{};
int ActiveWeaponHudId() { return k3::Id; }
bool kicking{};
bool HumanKickActive() noexcept { return kicking; }
float SharedRandomFloat(unsigned, float low, float high) { return (low + high) * .5f; }
}
namespace
{
int globalSequence{};
float globalStart{}, clientTime = 10;
model_t gunModel{}, footModel{};
void PhasePrediction()
{
    using namespace csol::client;
    namespace kick = csol::scenario::human_kick;
    ResetK3();
    std::snprintf(gunModel.name, sizeof(gunModel.name), "models/v_k3.mdl");
    std::snprintf(footModel.name, sizeof(footModel.name), "%s", kick::FootModel);
    fixtureApi.GetEngineBase = []() -> void* { return reinterpret_cast<void*>(0x10000000); };
    fixtureApi.ResolveGameSymbol = [](void*, const char* name, mh_gamesymbol_kind_t, void** address)
    {
        *address = !std::strcmp(name,"cl_weaponsequence") ? static_cast<void*>(&globalSequence) :
            !std::strcmp(name,"cl_weaponstarttime") ? static_cast<void*>(&globalStart) : nullptr;
        return *address ? MH_GAMESYMBOL_OK : MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    };
    engine.GetClientTime = [] { return clientTime; };
    engine.hudGetModelByIndex = [](int index) -> model_t* { return index == 12 ? &gunModel : index == 13 ? &footModel : nullptr; };
    engine.pfnClientCmd = [](const char*) { return 1; };
    engine.pfnWeaponAnim = [](int seq, int body) { Animate(seq,body); globalSequence=seq; globalStart=0; };
    InitializeHumanKickAnimation();
    clientWeapons=1;model.model=&gunModel;
    auto gun = State();gun.client.weaponanim=k3::Shoot1;
    gun.weapondata[0].m_flNextPrimaryAttack=.5f;gun.weapondata[0].m_flTimeWeaponIdle=5;
    gun=Predict(gun,0);
    globalSequence=k3::Shoot1;globalStart=5;
    HumanKickAnimationSnapshot({1,kick::Enabled|kick::Ready,0},{1,kick::Enabled|kick::Active,0});
    globalSequence=1;globalStart=0;model.model=&footModel;kicking=true;
    auto foot=gun;foot.client.viewmodel=13;foot.client.weaponanim=1;foot.client.m_flNextAttack=1000000;
    const int before=animations;
    const bool allowed=HumanKickWeaponPresentationAllowed(k3::Id,13);
    // Run the actual module with the same no-presentation flag supplied by
    // the production dispatcher. Its state simulation must still complete.
    local_state_t silent{};usercmd_t command{};command.msec=10;
    Check(PredictK3(&foot,&silent,&command,allowed?1:0,10,123),"actual foot snapshot still simulates K3 state");
    Check(animations==before,"kick-owned prediction does not replace the foot animation");
    globalSequence=k3::Shoot1;globalStart=0;clientTime=11;
    HumanKickAnimationSnapshot({1,kick::Enabled|kick::Active,0},{1,kick::Enabled|kick::Ready|kick::ResumePhase,0});
    HumanKickAnimationSnapshot({1,kick::Enabled|kick::Ready|kick::ResumePhase,0},{1,kick::Enabled|kick::Ready,0});
    kicking=false;
    gun=Predict(gun,0);
    HumanKickPredictionFinished(k3::Id,12,true);
    Check(animations==before&&globalStart==5,"actual K3 model reconciliation preserves old attack time and does not replay");
    gun.weapondata[0].m_flNextPrimaryAttack=-.001f;
    gun=Predict(gun,IN_ATTACK);
    Check(animations==before+1&&globalStart==0&&gun.weapondata[0].m_iClip==149,
        "actual subsequent K3 attack still fires once and starts a new animation");
}
}
int main()
{
    // The inherited fixture's full RNG oracle requires the original mapped
    // client sampler. This suite uses controlled spread and tests only the
    // actual predictor's kick/restore/new-shot presentation transitions.
    try { Setup(); PhasePrediction(); }
    catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << error.what() << '\n'; }
    std::cout << "kick animation predictor: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
