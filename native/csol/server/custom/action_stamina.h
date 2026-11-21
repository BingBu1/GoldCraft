// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/custom/stamina.hpp"
#include "../../scenario/custom/standalone_input.hpp"
#include <memory>
#include <string_view>

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
    bool denied{};
};
// Trusted native PM only: capture before callbacks, commit at the original
// successful jump epilogue. Failed/swimming/client-predicted calls never commit.
CSOLActionStaminaJump CSOL_PrepareActionStaminaJump(const playermove_s& movement);
bool CSOL_ActionStaminaJumpAllowed(CSOLActionStaminaJump jump);
bool CSOL_CommitActionStaminaJump(CSOLActionStaminaJump jump);
bool CSOL_ActionStaminaJumpBlocked(CBasePlayer& player);
bool CSOL_PrecacheActionStamina(const std::shared_ptr<const CSOLScenarioEntityAccess>& access,
    csol::scenario::ConfigError& error);

// Actual PostThink admits/updates one owner. No weapon/sprint/Jump event is
// synthesized here. now is the authoritative engine float clock.
bool CSOL_TickActionStamina(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, float now, csol::scenario::ConfigError& error, bool forceHud=false);
// Called only from the rules frame, before the gameplay manager. PostThink
// and accepted kick queries must not consume another displacement/recovery.
bool CSOL_TickStandaloneMovementFrame(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    csol::scenario::ConfigError& error);
bool CSOL_ActionStaminaRunCommand(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, bool requested, std::string_view life);
// Host transport proof only: no original mode or movement state is inferred
// from the broad weapon protocol or a client-controlled persisted userinfo key.
bool CSOL_ActionStaminaInputReady(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, std::string_view life, std::string_view revision);
std::optional<float> CSOL_ActionStaminaRunSpeed(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, float weaponSpeed);
bool CSOL_ActionStaminaCanKick(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player);
std::optional<csol::scenario::custom::StaminaView> CSOL_ActionStaminaView(
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CBasePlayer& player);
// Called exactly after the real action is accepted. Tokens are strictly
// increasing within a player life; repeating the last token never debits twice.
CSOLKickStaminaDebit CSOL_TryDebitKickStamina(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, std::uint64_t actionToken);
// CoF pickaxe checks eight at wind-up and spends it at contact.
bool CSOL_GuestCofPickaxeSwing(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, bool debit);
// Original CoF positive gate/debit15, explicitly on the existing CSOL ledger.
// requestedAmount must equal its current amount minus15. This never grants
// stamina, advances recovery, seeds a life, or publishes a second HUD.
bool CSOL_DebitGuestCofPushbackStamina(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, float requestedAmount);
// Noon exposes available capacity to the original positive-amount gate while
// preserving the stored native balance. This is a read, never a refill.
bool CSOL_ReadGuestCofPushbackStamina(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, float& amount, bool& infinite);
void CSOL_RetireActionStamina(CBasePlayer& player);
void CSOL_ResetActionStamina();
// Only an actual authoritative phase owner may call available=true. Raw JSON
// never manufactures elapsed time or capability. Missing/disabled time policy
// uses normal finite Night profile. A foreign/retired owner cannot publish.
bool CSOL_SetChallengeStaminaPhase(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    csol::scenario::custom::Phase phase, bool available);
