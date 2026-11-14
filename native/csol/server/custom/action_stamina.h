// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/custom/stamina.hpp"
#include <memory>

class CBasePlayer;
class CCSOLScenarioRuntime;
class CSOLScenarioEntityAccess;
struct playermove_s;
namespace csol::scenario { struct ConfigError; }
enum class CSOLKickStaminaDebit { Applied, Duplicate, Declined, Retired };
struct CSOLActionStaminaJump
{
    std::uint64_t owner{},request{};
    unsigned slot{};
};
// Trusted native PM only: capture before callbacks, commit at the original
// successful jump epilogue. Failed/swimming/client-predicted calls never commit.
CSOLActionStaminaJump CSOL_PrepareActionStaminaJump(const playermove_s& movement);
bool CSOL_CommitActionStaminaJump(CSOLActionStaminaJump jump);
bool CSOL_PrecacheActionStamina(const std::shared_ptr<const CSOLScenarioEntityAccess>& access,
    csol::scenario::ConfigError& error);

// Actual PostThink admits/updates one owner. No weapon/sprint/Jump event is
// synthesized here. now is the authoritative engine float clock.
bool CSOL_TickActionStamina(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, float now, csol::scenario::ConfigError& error, bool forceHud=false);
bool CSOL_ActionStaminaCanKick(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player);
std::optional<csol::scenario::custom::StaminaView> CSOL_ActionStaminaView(
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CBasePlayer& player);
// Called exactly after the real action is accepted. Tokens are strictly
// increasing within a player life; repeating the last token never debits twice.
CSOLKickStaminaDebit CSOL_TryDebitKickStamina(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, std::uint64_t actionToken);
void CSOL_RetireActionStamina(CBasePlayer& player);
void CSOL_ResetActionStamina();
// Only an actual authoritative phase owner may call available=true. Raw JSON
// never manufactures elapsed time or capability. Missing/disabled time policy
// uses normal finite Night profile. A foreign/retired owner cannot publish.
bool CSOL_SetChallengeStaminaPhase(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    csol::scenario::custom::Phase phase, bool available);
