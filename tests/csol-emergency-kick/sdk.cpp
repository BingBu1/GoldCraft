// SPDX-License-Identifier: GPL-3.0-or-later
#define COF_KICK_FIXTURE_ONLY
#include "../csol-cof-kick/sdk.cpp"
#undef COF_KICK_FIXTURE_ONLY
#include "../../native/csol/server/custom/action_stamina.h"
#include "../../native/csol/scenario/custom/settings.hpp"
#include "../../native/csol/server/custom/gameplay_settings.h"
#include "goldcraft/csol_gameplay_settings_api.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace emergency_integration
{
using namespace kick_test;
using CoF = cof_kick_test::Case;
namespace custom = csol::scenario::custom;

void Settings(bool time)
{
    custom::Draft draft; custom::Error error; custom::SnapshotPtr snapshot;
    Require(draft.Set(custom::Feature::TimeCycle, time, error) && draft.Finish(snapshot, error),
        "typed configuration created before real engine initialization");
    const auto path = std::filesystem::current_path() / "cstrike/addons/goldcraft/config/gameplay.json";
    Require(!std::filesystem::exists(path), "independent fresh game directory");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary); output << custom::Serialize(*snapshot);
    Require(bool(output), "real settings input is persisted before actual GameMode startup");
}
void Complete(kick_test::Case& c)
{
    c.AdvanceAnimation(.8f);
    Require(c.player.m_fSequenceFinished, "actual studio animation completes");
    Require(c.Update(), "actual controller resumes interrupted weapon");
    const auto packet = c.Snapshot();
    Require(packet && !(packet->flags & kick::Active), "completed controller releases Active");
}
void StockBusy()
{
    kick_test::Case c;
    c.weapon->m_fInReload = TRUE; c.weapon->m_iClip = 5;
    c.weapon->m_flNextPrimaryAttack = .75f; c.weapon->m_flNextSecondaryAttack = .5f;
    c.player.m_flNextAttack = 2; c.player.m_iFOV = 55; c.player.pev->fov = 55;
    const auto view = c.player.pev->viewmodel;
    Require(c.Try(), "emergency request during stock reload/attack/zoom");
    auto packet = c.Snapshot();
    Expect(packet && (packet->flags & kick::Active), "weapon busy timers and zoom do not veto kick");
    const auto after = CSOL_ActionStaminaView(c.f.runtime, c.player);
    Expect(after && after->amount == 90, "actual accepted kick debits ten once");
    Require(c.Try(), "repeat active request is safely consumed");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->amount == 90, "held F cannot double debit");
    c.weapon->m_iClip = 3;
    Complete(c);
    Expect(c.deploys == 0 && c.weapon->m_iClip == 3 && c.weapon->m_fInReload,
        "stock recovery uses current ammo/loading and never Deploy");
    Expect(c.weapon->m_flNextPrimaryAttack == .75f && c.weapon->m_flNextSecondaryAttack == .5f &&
        c.player.m_flNextAttack > 1.1f && c.player.m_flNextAttack < 1.3f,
        "stock weapon timers are untouched and player delay loses only elapsed overlay time");
    Expect(c.player.pev->viewmodel == view && c.player.m_iFOV == 55 && c.player.pev->fov == 55,
        "native weapon presentation and prior zoom return");
    Require(c.Try(), "second kick immediately after real completion");
    packet = c.Snapshot();
    Expect(packet && (packet->flags & kick::Active), "no separate1.1 second weapon gate remains");
}
void GlockReload(bool elapsed)
{
    CoF c; c.Advance(1.25f); c.glock->m_iClip = 5; c.glock->Reload();
    Expect(dynamic_cast<custom::ResumableWeaponAction*>(c.host.player.m_pActiveItem) != nullptr,
        "coherent actual Glock RTTI exposes the new generic action capability");
    const auto deadlines = c.glock->ActionState();
    const auto deploys = std::count_if(c.effects.begin(), c.effects.end(), [](const auto& e)
        { return e.kind == guest::cof::server::GlockEffect::DeployPrelude; });
    c.Command();
    Expect(c.Active(), "real csol_kick command accepts real in-flight CoF reload");
    c.glock->m_iClip = 3; c.host.player.m_rgAmmo[1] = 7;
    if (elapsed) c.Advance(4);
    Complete(c.host);
    Expect(c.glock->ActionState().primary == deadlines.primary &&
        c.glock->ActionState().ownerAttack == deadlines.ownerAttack,
        "generic controller and real CoF resume preserve absolute action deadlines");
    Expect(std::count_if(c.effects.begin(), c.effects.end(), [](const auto& e)
        { return e.kind == guest::cof::server::GlockEffect::DeployPrelude; }) == deploys,
        "foot completion does not execute CoF Deploy");
    if (elapsed)
        Expect(c.glock->m_iClip == 10 && c.host.player.m_rgAmmo[1] == 0 && !c.glock->m_fInReload,
            "elapsed reload transfers current ammunition once");
    else
        Expect(c.glock->m_iClip == 3 && c.host.player.m_rgAmmo[1] == 7 && c.glock->m_fInReload,
            "unfinished reload is not completed or rewound by kick");
    const int clip = c.glock->m_iClip;
    Require(c.host.Update(), "repeat post-completion update");
    Expect(c.glock->m_iClip == clip && c.host.player.m_flNextAttack < 100000,
        "repeat update neither duplicates ammo nor strands overlay lock");
}
void GlockAimAttack()
{
    CoF c; c.Advance(1.25f); c.frame.toggleAim = true; c.frame.pressed = 0x40;
    c.host.player.pev->button = 0x40; c.glock->ItemPostFrame();
    Require(c.glock->ActionState().aiming, "real CoF aim state");
    c.Command(); Expect(c.Active(), "aiming does not block emergency kick");
    Complete(c.host);
    Expect(c.glock->ActionState().aiming && c.host.player.m_iFOV == 55,
        "current CoF aim resumes without Deploy reset");
    c.Advance(2); c.frame.pressed = 1; c.frame.bulletDamage = 15;
    g_engfuncs.pfnPointContents = [](const float*) { return CONTENTS_EMPTY; };
    c.glock->PrimaryAttack();
    Require(c.glock->m_iClip == 14, "real loaded shot spends one round");
    const float primary = c.glock->m_flNextPrimaryAttack;
    c.Command(); Expect(c.Active(), "just-fired weapon does not block emergency kick");
    Complete(c.host);
    Expect(c.glock->m_iClip == 14 && c.glock->m_flNextPrimaryAttack == primary,
        "shot ammunition and original attack deadline are preserved");
}
void NoonAndLife()
{
    kick_test::Case c;
    Require(CSOL_SetChallengeStaminaPhase(c.f.runtime, custom::Phase::Noon, true),
        "explicit authoritative phase boundary (world time rendering is outside this test)");
    Require(c.Update(), "apply noon to actual stamina owner");
    Require(c.Try(), "noon kick is accepted");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->infinite &&
        CSOL_ActionStaminaView(c.f.runtime, c.player)->amount == 100, "noon kick is free");
    Complete(c);
    c.player.pev->health = 0; c.player.pev->deadflag = DEAD_DEAD;
    Require(c.Update(), "death retires stamina without rejecting ordinary controller cleanup");
    Expect(!CSOL_ActionStaminaCanKick(c.f.runtime, c.player), "dead owner cannot consume or observe ready stamina");
}
void BudgetAndLife()
{
    kick_test::Case c; csol::scenario::ConfigError error;
    for (std::uint64_t token = 1; token <= 10; ++token)
    {
        Expect(CSOL_TryDebitKickStamina(c.f.runtime, c.player, token) == CSOLKickStaminaDebit::Applied,
            "accepted real owner token spends finite stamina");
        Expect(CSOL_TryDebitKickStamina(c.f.runtime, c.player, token) == CSOLKickStaminaDebit::Duplicate,
            "the same accepted token cannot spend twice");
    }
    Expect(!CSOL_ActionStaminaCanKick(c.f.runtime, c.player) &&
        CSOL_TryDebitKickStamina(c.f.runtime, c.player, 11) == CSOLKickStaminaDebit::Declined,
        "insufficient finite stamina blocks the next accepted action");
    Require(CSOL_TickActionStamina(c.f.runtime, c.player, gpGlobals->time, error),
        "same-frame observer does not invent a new recovery cycle");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->amount == 0,
        "checking readiness or switching a weapon cannot refill stamina");
    CSOL_RetireActionStamina(c.player);
    Expect(!CSOL_TickActionStamina(c.f.runtime, c.player, gpGlobals->time, error),
        "retired current-life owner cannot rebind to refill");
    c.f.players->PlayerSpawnStarted(EHANDLE(&c.player)); ++c.player.m_iNumSpawns;
    Require(CSOL_TickActionStamina(c.f.runtime, c.player, gpGlobals->time, error),
        "actual native spawn serial establishes a new stamina life");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->amount == 100,
        "new genuine player life gets its configured capacity");
}
void ConfigurationScope()
{
    kick_test::Case c; csol::scenario::ConfigError error;
    Require(CSOL_TryDebitKickStamina(c.f.runtime, c.player, 1) == CSOLKickStaminaDebit::Applied,
        "default finite kick cost applied before edit");
    auto* settings = static_cast<csol::IGameplaySettingsService*>(
        CreateInterface(csol::GameplaySettingsServiceVersion, nullptr));
    csol::GameplaySettingsStatus status;
    Require(settings && settings->Status(status), "actual settings service available");
    Require(settings->Edit(1, settings->UserId(1), status.revision,
        int(custom::FeatureCount) + int(custom::Setting::KickCost), 30) == csol::GameplayEditResult::Queued,
        "actual administrator service queues changed next-map stamina cost");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        CSOL_PollGameplaySettings(); Require(settings->Status(status), "actual writer status");
        if (!status.busy) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    Require(!status.busy && status.result == csol::GameplayEditResult::Saved,
        "new cost is really persisted before testing its scope");
    Require(CSOL_TickActionStamina(c.f.runtime, c.player, gpGlobals->time, error), "current map tick after save");
    Require(CSOL_TryDebitKickStamina(c.f.runtime, c.player, 2) == CSOLKickStaminaDebit::Applied,
        "current accepted kick after save");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->amount == 80,
        "next-map saved cost does not change current-map ten-point cost");
    c.f.players->PlayerSpawnStarted(EHANDLE(&c.player)); ++c.player.m_iNumSpawns;
    Require(CSOL_TickActionStamina(c.f.runtime, c.player, gpGlobals->time, error), "new life in same map");
    Require(CSOL_TryDebitKickStamina(c.f.runtime, c.player, 1) == CSOLKickStaminaDebit::Applied,
        "new-life token in same map");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->amount == 90,
        "respawn does not apply a policy promised for the next map");
    CSOL_ResetActionStamina(); // Explicit controlled map-policy retirement boundary.
    Require(CSOL_TickActionStamina(c.f.runtime, c.player, gpGlobals->time, error), "new policy epoch after cold reset");
    Require(CSOL_TryDebitKickStamina(c.f.runtime, c.player, 1) == CSOLKickStaminaDebit::Applied,
        "new policy action");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->amount == 70,
        "retired map policy now consumes actually saved thirty-point cost");
}
CoF* swapCase{};
bool SwapOnReload(void* context, CBasePlayer& player, CBasePlayerWeapon& item,
    const guest::cof::server::GlockEvent& event) noexcept
{
    const bool delivered = CoF::Effect(context, player, item, event);
    if (swapCase && event.kind == guest::cof::server::GlockEffect::InventoryChanged)
    {
        swapCase = nullptr;
        // A real player weapon selection synchronously calls both ordinary
        // Holster and stock AK Deploy inside the reload observer.
        player.SelectItem("weapon_ak47");
    }
    return delivered;
}
void SwapDuringResume()
{
    CoF c;
    const guest::cof::server::GlockBackends backend{
        &c, CoF::Current, CoF::OwnerFrame, SwapOnReload,
        guest::cof::server::GlockRequiredEffects, &c.animations, {1, 1}};
    Require(c.glock->Bind(c.host.player, c.prepared, backend) ==
        guest::cof::server::GlockStatus::Ready && c.glock->Deploy(),
        "real CoF backend records the synchronous inventory observer");
    c.Advance(1.25f); c.glock->m_iClip = 5; c.glock->Reload();
    c.Command(); Require(c.Active(), "actual emergency starts during reload");
    swapCase = &c; c.Advance(4); Complete(c.host);
    Expect(!swapCase && c.host.player.m_pActiveItem == c.host.weapon,
        "reload observer really selected the inventory AK");
    Expect(std::string_view(STRING(c.host.player.pev->viewmodel)) == "models/v_ak47.mdl" &&
        c.host.player.m_flNextAttack == .75f && c.host.deploys == 1,
        "outer recovery cannot overwrite callback-selected gun presentation or its draw delay");
    Expect(c.glock->m_iClip == 15 && c.host.player.m_rgAmmo[1] == 25,
        "committed current reload is retained when an observer changes weapons");
    Expect(!c.glock->ActionState().loading && !c.glock->m_fInReload,
        "observer retirement cannot leave the already committed reload pending");
}
}
int main(int argc, char** argv)
{
    try
    {
        const std::string_view group = argc > 1 ? argv[1] : "stock";
        emergency_integration::Settings(group == "noon-life");
        if (group == "stock") emergency_integration::StockBusy();
        else if (group == "reload-pending") emergency_integration::GlockReload(false);
        else if (group == "reload-elapsed") emergency_integration::GlockReload(true);
        else if (group == "aim-attack") emergency_integration::GlockAimAttack();
        else if (group == "noon-life") emergency_integration::NoonAndLife();
        else if (group == "budget-life") emergency_integration::BudgetAndLife();
        else if (group == "config-scope") emergency_integration::ConfigurationScope();
        else if (group == "swap-resume") emergency_integration::SwapDuringResume();
        else throw std::runtime_error("unknown emergency integration group");
    }
    catch (const std::exception& error)
    { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
