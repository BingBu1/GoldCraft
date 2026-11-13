// SPDX-License-Identifier: GPL-3.0-or-later
// Actual Glock and full SDK world; only overlay acquisition is controlled here.
#define COF_KICK_FIXTURE_ONLY
#include "sdk.cpp"
#undef COF_KICK_FIXTURE_ONLY

namespace emergency_test
{
using namespace cof_kick_test;
using Case = cof_kick_test::Case;
using csol::scenario::custom::ActionResumeResult;
using csol::scenario::custom::ActionResumeToken;
constexpr ActionResumeToken Token{0x11223344};
constexpr float OverlayLock = 1000000;
unsigned Count(const Case& c, GlockEffect kind)
{
    return unsigned(std::count_if(c.effects.begin(), c.effects.end(),
        [=](const auto& e) { return e.kind == kind; }));
}
struct Clocks
{
    float primary, secondary, idle, owner, extra;
    explicit Clocks(const Case& c) : primary(c.glock->ActionState().primary),
        secondary(c.glock->ActionState().secondary), idle(c.glock->ActionState().idle),
        owner(c.glock->ActionState().ownerAttack), extra(c.glock->ActionState().extra) {}
    bool Same(const Case& c) const
    {
        const auto& a = c.glock->ActionState();
        return primary == a.primary && secondary == a.secondary && idle == a.idle &&
            owner == a.ownerAttack && extra == a.extra && c.glock->m_flNextPrimaryAttack == primary &&
            c.glock->m_flNextSecondaryAttack == secondary && c.glock->m_flTimeWeaponIdle == idle;
    }
};
void Suspend(Case& c)
{
    Require(c.glock->SuspendWeaponAction(Token) == ActionResumeResult::Applied, "real Glock suspends current action");
    c.host.player.pev->viewmodel = Engine::String(kick::FootModel);
    c.host.player.pev->weaponmodel = 0; c.host.player.pev->weaponanim = 99;
    c.host.player.pev->fov = 90; c.host.player.m_iFOV = 90;
    c.host.player.m_flNextAttack = OverlayLock;
}
void Resume(Case& c)
{
    const auto deploys = Count(c, GlockEffect::DeployPrelude);
    Require(c.glock->ResumeWeaponAction(Token) == ActionResumeResult::Applied, "real Glock resumes live action");
    Expect(c.host.player.m_flNextAttack == OverlayLock, "resume leaves overlay player lock to generic owner");
    Expect(int(c.host.player.pev->viewmodel) == c.prepared.viewModelString &&
        int(c.host.player.pev->weaponmodel) == c.prepared.playerModelString &&
        Count(c, GlockEffect::DeployPrelude) == deploys, "current gun models restored without Deploy effects");
    c.host.player.m_flNextAttack = 0; // Actual overlay owner releases its lock.
}
void ReloadPending()
{
    Case c; c.Advance(1.25f); c.glock->m_iClip = 5; c.glock->Reload();
    Require(c.glock->ActionState().loading && c.glock->m_iClip == 0, "real original reload discards old magazine");
    const Clocks deadlines(c); const auto notices = Count(c, GlockEffect::InventoryChanged);
    Suspend(c); c.glock->m_iClip = 2; c.host.player.m_rgAmmo[1] = 11; c.Advance(.25f);
    c.glock->ItemPostFrame(); c.glock->PrimaryAttack(); c.glock->Reload();
    Expect(c.host.player.pev->weaponanim == 99 && c.glock->m_iClip == 2 &&
        c.host.player.m_rgAmmo[1] == 11, "overlay suppresses new real input and old-clip rollback");
    Resume(c);
    Expect(c.glock->m_iClip == 2 && c.host.player.m_rgAmmo[1] == 11 && c.glock->m_fInReload &&
        c.glock->ActionState().loading && c.glock->ActionState().action == 2 && deadlines.Same(c),
        "unfinished real reload retains current ammo/loading and all original deadlines");
    Expect(Count(c, GlockEffect::InventoryChanged) == notices, "unfinished resume cannot notify a premature ammo transfer");
    c.Advance(4); c.host.player.pev->button = 0; c.glock->ItemPostFrame();
    Expect(c.glock->m_iClip == 13 && c.host.player.m_rgAmmo[1] == 0 && !c.glock->m_fInReload,
        "later actual weapon frame commits missing/current-reserve once");
    const auto committed = Count(c, GlockEffect::InventoryChanged);
    c.glock->ItemPostFrame();
    Expect(c.glock->m_iClip == 13 && Count(c, GlockEffect::InventoryChanged) == committed,
        "second real frame cannot duplicate reload completion");
}
void ReloadElapsed()
{
    Case c; c.Advance(1.25f); c.glock->m_iClip = 5; c.glock->Reload();
    const Clocks deadlines(c); const auto notices = Count(c, GlockEffect::InventoryChanged);
    Suspend(c); c.glock->m_iClip = 3; c.host.player.m_rgAmmo[1] = 7; c.Advance(4);
    Resume(c);
    Expect(c.glock->m_iClip == 10 && c.host.player.m_rgAmmo[1] == 0 && !c.glock->m_fInReload &&
        !c.glock->ActionState().loading && c.glock->ActionState().action == 0,
        "elapsed resume commits current 3+7 ammo and consumes real loading state");
    Expect(deadlines.Same(c) && c.host.player.pev->weaponanim == 0,
        "elapsed reload restores current idle without any new cooldown");
    Expect(Count(c, GlockEffect::InventoryChanged) == notices + 1, "elapsed completion has one real inventory observer");
    const auto effects = c.effects.size();
    Expect(c.glock->ResumeWeaponAction(Token) == ActionResumeResult::Rejected && c.effects.size() == effects,
        "consumed generation cannot replay presentation or reload");
    c.host.player.pev->button = 0; c.glock->ItemPostFrame();
    Expect(c.glock->m_iClip == 10 && c.host.player.m_rgAmmo[1] == 0 &&
        Count(c, GlockEffect::InventoryChanged) == notices + 1, "next ordinary frame cannot commit elapsed reload twice");
}
void AimAndAttack()
{
    Case c; c.Advance(1.25f);
    c.frame.pressed = 0x40; c.frame.toggleAim = true; c.host.player.pev->button = 0x40;
    c.glock->ItemPostFrame(); Require(c.glock->ActionState().aiming, "actual aim action starts");
    const Clocks aimed(c); Suspend(c); c.Advance(.1f); Resume(c);
    Expect(c.glock->ActionState().aiming && c.host.player.m_iFOV == 55 && c.host.player.pev->fov == 55 && aimed.Same(c),
        "real aimed state and zoom return without transition cooldown reset");
    c.Advance(2); c.glock->m_iClip = 0; c.glock->m_fFireOnEmpty = TRUE; c.frame.pressed = 1;
    c.glock->PrimaryAttack(); const Clocks shot(c);
    Require(shot.primary > gpGlobals->time, "actual empty attack establishes future cooldown");
    Suspend(c); c.Advance(.1f); c.glock->m_iClip = 4; c.host.player.m_rgAmmo[1] = 9; Resume(c);
    Expect(shot.Same(c) && c.glock->m_iClip == 4 && c.host.player.m_rgAmmo[1] == 9 &&
        c.host.player.pev->weaponanim == 35, "interrupted actual aimed attack preserves current ammo/sequence/cooldown");
}
void AttachmentCallbacks()
{
    Case c; c.Advance(1.25f);
    Require(c.glock->RequestTaclightAttachment() == GlockStatus::Applied, "real authenticated attachment request");
    c.host.player.pev->button = 0; c.glock->ItemPostFrame();
    Require(c.glock->m_pfnThink && c.glock->pev->nextthink > gpGlobals->time, "real frame schedules original mid-attach callback");
    Suspend(c); const auto views = c.views; const auto bodies = Count(c, GlockEffect::Body);
    c.Advance(1.25f); c.glock->Think();
    Expect(c.glock->m_pfnThink && c.glock->pev->nextthink > gpGlobals->time &&
        c.host.player.m_flNextAttack == OverlayLock, "real intermediate callback schedules original successor without unlocking overlay");
    c.Advance(1.25f); c.glock->Think();
    Expect(c.glock->TaclightInstalled() && !c.glock->m_pfnThink && c.glock->pev->nextthink == 0,
        "real final attachment callback consumes its own schedule once");
    Expect(c.views == views && Count(c, GlockEffect::Body) == bodies && c.host.player.pev->weaponanim == 99 &&
        std::string_view(STRING(c.host.player.pev->viewmodel)) == kick::FootModel && c.host.player.m_flNextAttack == OverlayLock,
        "late real callbacks defer gun view/body and preserve foot ownership");
    const Clocks progressed(c); const auto sounds = Count(c, GlockEffect::AttachmentSound); c.glock->Think();
    Expect(Count(c, GlockEffect::AttachmentSound) == sounds, "consumed real Think cannot replay attachment sound");
    Resume(c);
    Expect(progressed.Same(c) && c.host.player.pev->weaponanim == 6 && Count(c, GlockEffect::Body) == bodies + 1,
        "resume publishes latest attachment result and progressed original deadlines");
}
void Cancellation()
{
    Case c; c.Advance(1.25f);
    Require(c.glock->RequestTaclightAttachment() == GlockStatus::Applied, "attachment before cancelled overlay");
    c.host.player.pev->button = 0; c.glock->ItemPostFrame(); Suspend(c);
    Expect(c.glock->SuspendWeaponAction({Token.generation + 1}) == ActionResumeResult::Rejected,
        "second concurrent generation cannot acquire real weapon overlay");
    const auto effects = c.effects.size(); c.host.player.m_pActiveItem = c.host.weapon;
    Expect(c.glock->ResumeWeaponAction(Token) == ActionResumeResult::Cancelled && !c.glock->m_pfnThink && c.glock->pev->nextthink == 0,
        "switch rejects old selected weapon and cancels pending real callback");
    c.Advance(10); c.glock->Think();
    Expect(!c.glock->TaclightInstalled() && c.effects.size() == effects, "cancelled callback cannot mutate switched weapon or presentation");
    c.host.player.m_pActiveItem = c.glock;
    Expect(c.glock->ResumeWeaponAction(Token) == ActionResumeResult::Rejected,
        "switching back cannot resurrect retired generation");
}
void LifeAndUndo()
{
    Case c; c.Advance(1.25f); const Clocks original(c);
    Require(c.glock->SuspendWeaponAction(Token) == ActionResumeResult::Applied, "reserve before failed foot acquisition");
    Require(c.glock->ResumeWeaponAction(Token) == ActionResumeResult::Applied, "resume undoes failed overlay acquisition");
    Expect(original.Same(c) && c.glock->m_iClip == 15 && c.host.player.m_rgAmmo[1] == 40,
        "failed foot acquisition preserves ordinary gun action/ammo");
    Suspend(c); ++c.host.player.m_iNumSpawns;
    const auto effects = c.effects.size();
    Expect(c.glock->ResumeWeaponAction(Token) == ActionResumeResult::Cancelled && c.effects.size() == effects,
        "changed actual native spawn cannot resume prior life presentation");
}
struct ObservedCase : Case
{
    bool reenter{}, failInventory{};
    unsigned observers{};
    ActionResumeResult nested{ActionResumeResult::Applied};
    ObservedCase()
    {
        const GlockBackends backend{this, Current, OwnerFrame, Observe, GlockRequiredEffects, &animations, {1, 1}};
        Require(glock->Bind(host.player, prepared, backend) == GlockStatus::Ready, "observed real Glock binding");
        Require(glock->Deploy() != FALSE, "observed real Glock ordinary deploy");
        observers = 0; effects.clear(); Advance(1.25f);
    }
    static bool Observe(void* value, CBasePlayer& player, CBasePlayerWeapon& weapon, const GlockEvent& event) noexcept
    {
        auto& c = *static_cast<ObservedCase*>(value);
        const bool delivered = Case::Effect(value, player, weapon, event);
        if (event.kind == GlockEffect::InventoryChanged)
        {
            ++c.observers;
            if (c.reenter)
            {
                c.nested = c.glock->ResumeWeaponAction(Token);
                c.glock->ItemPostFrame();
            }
            if (c.failInventory) return false;
        }
        return delivered;
    }
};
void ObserverRetry()
{
    ObservedCase c; c.glock->m_iClip = 5; c.glock->Reload();
    Suspend(c); c.glock->m_iClip = 3; c.host.player.m_rgAmmo[1] = 7; c.Advance(4);
    c.reenter = c.failInventory = true;
    Expect(c.glock->ResumeWeaponAction(Token) == ActionResumeResult::Rejected &&
        c.nested == ActionResumeResult::Rejected && c.observers == 1,
        "real reload observer reentry cannot resume or commit twice and delivery failure stays rejected");
    Expect(c.glock->m_iClip == 10 && c.host.player.m_rgAmmo[1] == 0 && !c.glock->m_fInReload &&
        !c.glock->ActionState().loading && c.glock->ActionState().action == 0 &&
        c.host.player.m_flNextAttack == OverlayLock,
        "failed observer retains already consumed live reload without releasing foot lock");
    c.reenter = c.failInventory = false; Resume(c);
    Expect(c.observers == 1 && c.glock->m_iClip == 10 && c.host.player.pev->weaponanim == 0,
        "presentation retry restores latest idle and cannot repeat ammo observer");
}
void LoadedAttack()
{
    Case c; c.Advance(1.25f); c.frame.bulletDamage = 15; c.frame.pressed = 1;
    // Explicit dry geometry for the real bullet's water/bubble consumer.
    // Unsupported engine edges intentionally throw in the common fixture.
    g_engfuncs.pfnPointContents = [](const float*) { return CONTENTS_EMPTY; };
    c.glock->PrimaryAttack();
    Require(c.glock->m_iClip == 14 && c.glock->Status() == GlockStatus::Applied &&
        Count(c, GlockEffect::FireEvent) == 1, "real loaded Glock attack fires and consumes one native cartridge");
    const Clocks shot(c); const auto scalar = c.glock->Spread().scalar;
    Suspend(c); c.Advance(.05f); Resume(c);
    Expect(c.glock->m_iClip == 14 && c.glock->Spread().scalar == scalar && shot.Same(c) &&
        c.host.player.pev->weaponanim == 2 && Count(c, GlockEffect::FireEvent) == 1,
        "loaded attack resumes current firing sequence with no repeated shot/ammo/spread/cooldown reset");
}
}
int main(int argc, char** argv)
{
    try
    {
        const std::string_view group = argc > 1 ? argv[1] : "reload-pending";
        if (group == "reload-pending") emergency_test::ReloadPending();
        else if (group == "reload-elapsed") emergency_test::ReloadElapsed();
        else if (group == "aim-attack") emergency_test::AimAndAttack();
        else if (group == "callbacks") emergency_test::AttachmentCallbacks();
        else if (group == "cancel") emergency_test::Cancellation();
        else if (group == "life-undo") emergency_test::LifeAndUndo();
        else if (group == "observer-retry") emergency_test::ObserverRetry();
        else if (group == "loaded-attack") emergency_test::LoadedAttack();
        else throw std::runtime_error("unknown emergency SDK group");
    }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
