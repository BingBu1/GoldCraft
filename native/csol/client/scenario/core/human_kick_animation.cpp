// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../weapons/weapons.hpp"
#include "human_kick.hpp"
#include "human_kick_animation_state.hpp"
#include <cstring>

namespace csol::client
{
namespace
{
human_kick_detail::AnimationOverlay animation;
int* sequence{};
float* started{};
DWORD owner{};
int lastPresentedWeapon{}, lastPresentedModel{};
bool Owner() { return owner && owner == GetCurrentThreadId(); }
std::uintptr_t Model(int index)
{
    return index > 0 && engine.hudGetModelByIndex ?
        reinterpret_cast<std::uintptr_t>(engine.hudGetModelByIndex(index)) : 0;
}
bool Foot(std::uintptr_t address)
{
    const auto* model = reinterpret_cast<const model_t*>(address);
    return model && !std::strcmp(model->name, scenario::human_kick::FootModel);
}
}
void InitializeHumanKickAnimation()
{
    owner = GetCurrentThreadId();
    animation.Reset();
    lastPresentedWeapon = lastPresentedModel = 0;
    void* a{}; void* b{};
    if (api->ResolveGameSymbol(api->GetEngineBase(), "cl_weaponsequence", MH_GAMESYMBOL_KIND_GLOBAL, &a) != MH_GAMESYMBOL_OK ||
        api->ResolveGameSymbol(api->GetEngineBase(), "cl_weaponstarttime", MH_GAMESYMBOL_KIND_GLOBAL, &b) != MH_GAMESYMBOL_OK ||
        !a || !b || !engine.GetViewModel || !engine.hudGetModelByIndex || !engine.GetClientTime || !engine.pfnClientCmd)
    {
        sequence = nullptr; started = nullptr;
        if (engine.pfnClientCmd) engine.pfnClientCmd("setinfo csol_kick_phase 0\n");
        return;
    }
    sequence = static_cast<int*>(a); started = static_cast<float*>(b);
    engine.pfnClientCmd("setinfo csol_kick_phase 1\n");
}
void ResetHumanKickAnimation()
{
    if (Owner()) { animation.Reset(); lastPresentedWeapon = lastPresentedModel = 0; }
}
void HumanKickAnimationSnapshot(const scenario::human_kick::Snapshot& previous,
    const scenario::human_kick::Snapshot& current)
{
    if (!Owner() || !sequence || !started) return;
    namespace kick = scenario::human_kick;
    if (previous.life != current.life) animation.Reset();
    if (!(current.flags & kick::Enabled)) { animation.Reset(); return; }
    if (current.flags & kick::Active)
    {
        if (previous.life == current.life && (previous.flags & kick::Active)) return;
        const auto* viewmodel = engine.GetViewModel();
        auto model = reinterpret_cast<std::uintptr_t>(viewmodel ? viewmodel->model : nullptr);
        const int weapon = ActiveWeaponHudId();
        // Consecutive kicks can complete before the renderer has replaced the
        // previous Foot. Retain the already acknowledged weapon model identity.
        if (Foot(model))
        {
            if (animation.Pending() && animation.Owner().weapon == weapon) model = animation.Owner().model;
            else if (lastPresentedWeapon == weapon) model = Model(lastPresentedModel);
        }
        if (Foot(model)) { animation.Reset(); return; }
        animation.Begin(current.life, {weapon, model, *sequence, *started}, float(engine.GetClientTime()));
    }
    else if (previous.flags & kick::Active)
    {
        const auto restore = animation.End(current.life, (current.flags & kick::ResumePhase) != 0,
            ActiveWeaponHudId(), *sequence);
        if (restore) *started = *restore;
    }
}
bool HumanKickWeaponPresentationAllowed(int, int viewmodel)
{
    if (!Owner()) return true;
    if (HumanKickActive()) return false;
    return !animation.Pending() || !Foot(Model(viewmodel));
}
void HumanKickPredictionFinished(int weapon, int viewmodel, bool presented)
{
    if (!Owner() || !presented) return;
    lastPresentedWeapon = weapon; lastPresentedModel = viewmodel;
    if (animation.Pending()) animation.Predicted(weapon, Model(viewmodel));
}
void ReconcileWeaponAnimation(int weapon, int viewmodel, int nextSequence, int body, bool newAction)
{
    if (!newAction && Owner() && sequence && started && animation.Pending())
        if (const auto restore = animation.Reconcile(weapon, Model(viewmodel), nextSequence))
        { *sequence = nextSequence; *started = *restore; return; }
    if (engine.pfnWeaponAnim) engine.pfnWeaponAnim(nextSequence, body);
}
}
