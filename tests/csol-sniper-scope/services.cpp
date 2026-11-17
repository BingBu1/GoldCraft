// SPDX-License-Identifier: GPL-3.0-or-later
// Non-scope service boundaries; no kick, environment or VGUI scenario is active.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/scenario/core/human_kick.hpp"
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
namespace csol::client {
void ResetZeCounterHud(){} void ResetMountWeapon(){}
void ResetHumanKick(){} void ResetChallengePhase(){} void ResetChallengeEnvironment(){}
void ResetCofMovementInput(){} void ResetStandaloneMovementInput(){}
void ResetStaminaAudio(){} void ResetStaminaHud(){} void ResetRenderSettings(){}
bool HumanKickWeaponPresentationAllowed(int,int){return true;}
void HumanKickPredictionFinished(int,int,bool){}
bool HumanKickActive() noexcept{return false;}
void ReconcileWeaponAnimation(int,int,int sequence,int body,bool)
{
    if(engine.pfnWeaponAnim)engine.pfnWeaponAnim(sequence,body);
}
}
