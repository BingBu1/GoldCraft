// SPDX-License-Identifier: GPL-3.0-or-later
// Actual kick controller + actual CoF Glock over the existing real SDK world.
// Engine studio/effect transport are controlled boundaries, not game assets.
#define main ExistingKickSdkMain
#include "../csol-human-kick-server/sdk.cpp"
#undef main
#include "../../native/guest/cof/server/glock_weapon.hpp"
#include "../../native/guest/cof/catalog.hpp"

namespace cof_kick_test
{
using namespace kick_test;
using namespace guest::cof;
using namespace guest::cof::server;

struct PlayerStudio
{
    studiohdr_t header{};
    mstudiobone_t bone{};
    std::array<mstudioseqdesc_t, 10> sequences{};
    PlayerStudio()
    {
        header.id = 0x54534449; header.version = 10; // Verified GoldSrc IDST/v10.
        header.length = sizeof(*this); header.numbones = 1;
        header.boneindex = int(reinterpret_cast<char*>(&bone) - reinterpret_cast<char*>(this));
        bone.parent = -1;
        header.numseq = int(sequences.size());
        header.seqindex = int(reinterpret_cast<char*>(sequences.data()) - reinterpret_cast<char*>(this));
        constexpr const char* names[]{"idle", "zombiekick", "ref_aim_onehanded", "crouch_aim_onehanded",
            "ref_shoot_onehanded", "crouch_shoot_onehanded", "ref_reload_onehanded", "crouch_reload_onehanded",
            "ref_shoot_knife", "crouch_shoot_knife"};
        for (std::size_t i = 0; i < sequences.size(); ++i)
        {
            std::snprintf(sequences[i].label, sizeof(sequences[i].label), "%s", names[i]);
            sequences[i].fps = 30; sequences[i].numframes = 43; sequences[i].flags = STUDIO_LOOPING;
        }
        sequences[1].flags = 0;
    }
};

struct Case
{
    inline static Case* active{};
    kick_test::Case host;
    PlayerStudio studio;
    PlayerAnimationAdapter animations;
    GlockWeapon* glock{};
    GlockPrepared prepared;
    GlockOwnerFrame frame;
    std::vector<GlockEvent> effects;
    unsigned views{};
    bool current{true};

    Case()
    {
        Require(!active, "one real CoF kick fixture"); active = this;
        g_engfuncs.pfnGetModelPtr = [](edict_t* edict) -> void*
        {
            return (edict->v.flags & FL_CLIENT) ? static_cast<void*>(&active->studio) :
                static_cast<void*>(&active->host.monsterStudio);
        };
        std::string error;
        const auto plan = RecommendedPistolAnimationPlan(Pistol::Glock, PistolAnimationRig::Stock);
        Require(plan && animations.Prepare(host.player,
            {reinterpret_cast<const std::byte*>(&studio), sizeof(studio)}, {1, 1}, Pistol::Glock, *plan,
            _stricmp, error), error.c_str());
        auto* edict = Engine::Create(Engine::String("weapon_cof_glock"));
        Require(edict, "engine creates a real weapon edict");
        glock = GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<GlockWeapon*>(&edict->v));
        Require(glock && dynamic_cast<CCSPlayerWeapon*>(glock->m_pEntity), "actual SDK constructs Glock and weapon API wrapper");
        glock->m_iPrimaryAmmoType = 1; glock->m_iSecondaryAmmoType = -1;
        glock->m_iClip = 15; host.player.m_rgAmmo[1] = 40;
        glock->pev->owner = host.player.edict();
        prepared.sequences.present = (std::uint64_t{1} << 43) - 1;
        prepared.sequences.duration.fill(1);
        prepared.modelsReady = prepared.inventoryReady = prepared.soundsReady = prepared.particleReady = true;
        prepared.pickupSoundReady = prepared.deployPreludeReady = prepared.aimMetadataReady = prepared.taclightSoundsReady = true;
        prepared.viewModelString = Engine::String("models/guest/cof/glock/v_glock.mdl");
        prepared.playerModelString = Engine::String("models/guest/cof/glock/p_glock.mdl");
        prepared.worldModelString = Engine::String("models/guest/cof/glock/w_glock.mdl");
        prepared.inventoryItemString = Engine::String("inventoryitems/weapons/weapon_glock.txt");
        prepared.hostSlot = GlockHudSlot; prepared.hostPosition = GlockHudPosition;
        prepared.shellModel = 31; prepared.ballistics = {77, 31}; prepared.aimOffset = {1, 2, 3}; prepared.aimZoom = 55;
        prepared.world = MeleeWorld::Multiplayer;
        frame.firstDrawn = true; frame.reserveLimit = 150; frame.sourceMode = PistolSource::NormalMuzzle;
        frame.bulletDamage = 0; frame.meleeDamage = 0; frame.randomSeed = 17;
        const GlockBackends backend{this, Current, OwnerFrame, Effect, GlockRequiredEffects, &animations, {1, 1}};
        Require(glock->Bind(host.player, prepared, backend) == GlockStatus::Ready, "actual Glock binds typed resources and authenticated owner frame");
        host.player.m_pActiveItem = glock;
        Require(glock->Deploy() != FALSE, "actual CoF Deploy mirrors its absolute deadlines");
        Require(glock->UseDecrement() == FALSE && glock->m_flNextPrimaryAttack > host.f.engine.globals.time,
            "actual class uses absolute weapon clocks after Deploy");
        host.packets.clear();
    }
    static bool Current(void* value) noexcept
    { return active == value && active->current && active->host.f.runtime && !active->host.f.runtime->Closing(); }
    static bool OwnerFrame(void* value, GlockOwnerFrame& output) noexcept
    { if (!Current(value)) return false; output = static_cast<Case*>(value)->frame; return true; }
    static bool Effect(void* value, CBasePlayer& player, CBasePlayerWeapon&, const GlockEvent& event) noexcept
    {
        if (!Current(value)) return false;
        auto& c = *static_cast<Case*>(value); c.effects.push_back(event);
        if (event.kind == GlockEffect::ViewAnimation) { player.pev->weaponanim = event.value; ++c.views; }
        if (event.kind == GlockEffect::Aim) player.m_iFOV = event.enabled ? event.value : 90;
        return true;
    }
    void Advance(float dt) { host.f.engine.globals.time += dt; }
    std::optional<kick::Snapshot> Snapshot()
    {
        host.error = {};
        Require(CSOL_UpdateHumanKick(host.f.runtime, host.player, host.error, true), host.error.message.c_str());
        return host.Snapshot();
    }
    bool Ready() { const auto value = Snapshot(); return value && (value->flags & kick::Ready); }
    bool Active() { const auto value = Snapshot(); return value && (value->flags & kick::Active); }
    void Command()
    { Require(CSOL_ScenarioCharacterSkillCommand(host.player, "csol_kick"), "actual F transport's server command is consumed"); }
    ~Case()
    {
        CSOL_ResetHumanKick(host.player, false);
        if (glock) glock->Invalidate();
        host.player.m_pActiveItem = host.weapon; active = nullptr;
    }
};

void ReadyAfterDraw()
{
    Case c;
    Expect(!c.Ready(), "real CoF draw deadline blocks kick");
    c.Advance(1.25f);
    Require(c.glock->m_flNextPrimaryAttack > 0 && c.glock->m_flNextPrimaryAttack < gpGlobals->time,
        "elapsed absolute CoF timer remains nonzero");
    Expect(c.Ready(), "elapsed actual Glock absolute timer enables kick Ready");
    c.Command();
    Expect(c.Active() && std::string_view(STRING(c.host.player.pev->viewmodel)) == kick::FootModel,
        "actual server command while holding CoF starts foot action");
}
void Completion()
{
    Case c; c.Advance(1.25f); Require(c.Ready(), "Glock kick readiness before completion scenario");
    c.Command(); Require(c.Active(), "actual foot action begins with CoF Glock");
    const auto views = c.views;
    const auto nextAttack = c.host.player.m_flNextAttack;
    c.glock->AdvanceOwnerFrame();
    c.host.player.ItemPostFrame();
    Expect(c.views == views && c.host.player.m_flNextAttack == nextAttack &&
        std::string_view(STRING(c.host.player.pev->viewmodel)) == kick::FootModel,
        "native player lock and real CoF spread advance preserve active foot ownership");
    c.host.AdvanceAnimation(.8f);
    Require(c.host.player.m_fSequenceFinished, "actual player sequence completion is produced by StudioFrameAdvance");
    Require(c.host.Update(), "actual kick completion invokes selected real CoF Deploy");
    Expect(!c.Active() && c.views == views + 1 &&
        int(c.host.player.pev->viewmodel) == c.prepared.viewModelString &&
        int(c.host.player.pev->weaponmodel) == c.prepared.playerModelString &&
        c.glock->m_flNextPrimaryAttack == gpGlobals->time + 1 && c.host.player.m_flNextAttack == 0,
        "real CoF Deploy restores models and absolute draw deadlines without native countdown substitution");
    const auto restoredViews = c.views;
    Require(c.host.Update(), "repeat completed update");
    Expect(c.views == restoredViews, "completed update cannot repeat CoF Deploy");
}
void Aim()
{
    Case c; c.Advance(1.25f); Require(c.Ready(), "Glock ready before actual aim action");
    c.frame.pressed = 0x40; c.frame.toggleAim = true; c.host.player.pev->button = 0x40;
    c.glock->ItemPostFrame();
    Require(c.glock->ActionState().aiming && c.host.player.m_iFOV == 55, "actual Glock aim action sets authored zoom");
    Expect(!c.Ready(), "real zoomed CoF aim retains kick rejection");
    c.Command(); Expect(!c.Active(), "aimed weapon cannot start foot action");
}
void Attack()
{
    Case c; c.Advance(1.25f); Require(c.Ready(), "Glock ready before actual primary action");
    c.glock->m_iClip = 0; c.glock->m_fFireOnEmpty = TRUE; c.frame.pressed = 1;
    c.glock->PrimaryAttack();
    Require(c.glock->m_flNextPrimaryAttack > gpGlobals->time, "actual Glock empty primary attack mirrors a future absolute timer");
    Expect(!c.Ready(), "actual CoF primary attack timer retains kick rejection");
    c.Command(); Expect(!c.Active(), "primary attack command cannot bypass readiness timer");
}
void ReloadAndAim()
{
    Case c; c.Advance(1.25f); Require(c.Ready(), "Glock is ready before real reload");
    c.glock->m_iClip = 5;
    c.glock->Reload();
    Expect(c.glock->m_fInReload && !c.Ready(), "actual CoF reload retains kick rejection");
    c.Command(); Expect(!c.Active(), "reload command does not begin foot action");
}
void AkCountdown() { kick_test::Completion(); kick_test::Blocked(); }
}

#ifndef COF_KICK_FIXTURE_ONLY
int main(int argc, char** argv)
{
    try
    {
        const std::string_view group = argc > 1 ? argv[1] : "ready";
        if (group == "ready") cof_kick_test::ReadyAfterDraw();
        else if (group == "completion") cof_kick_test::Completion();
        else if (group == "reload") cof_kick_test::ReloadAndAim();
        else if (group == "aim") cof_kick_test::Aim();
        else if (group == "attack") cof_kick_test::Attack();
        else if (group == "ak") cof_kick_test::AkCountdown();
        else throw std::runtime_error("unknown CoF kick group");
    }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
#endif
