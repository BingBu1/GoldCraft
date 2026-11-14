// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "weapons.h"
#include "../magazine_actions.hpp"
#include "../magazine_reload.hpp"
#include "../spread.hpp"
#include "ballistics.hpp"
#include "player_animation.hpp"
#include "weapon_melee.hpp"
#include "owner_frame_weapon.hpp"
#include "native/include/goldcraft/scenario_guest_weapon.hpp"
#include "native/csol/server/weapons/weapons.h"

namespace guest::cof::server
{
inline constexpr int VP70HudSlot = 1;
inline constexpr int VP70HudPosition = 3;
enum class VP70Status : unsigned char
{
    Ready, Applied, Ineligible, MissingBackend, InvalidInput, UnsupportedState, Cancelled
};

// Typed original effects that do not exist in CS. The parent authenticates and
// transports these; acknowledging a missing renderer/audio consumer is invalid.
enum class VP70Effect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    Shell, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState, ShotLatch,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, FirstDraw, Count
};
struct VP70Event
{
    VP70Effect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    // Gunsmoke: value is the original weapon-edict owner, enabled is local,
    // other is attachment, and modelString is the prepared view-model string.
    // The script is the fixed original aurora/gunsmoke.aur resource.
    int modelString{};
};
inline constexpr std::uint32_t VP70RequiredEffects =
    (1u << static_cast<unsigned>(VP70Effect::Count)) - 1u;

struct VP70OwnerFrame
{
    // Exact original-only state, read from the authenticated server session.
    // No inference from CS fields and no client claims/default-success values.
    float activityBlock{}, pendingGateTime{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{};
    int eventOwner2814{};
    bool firstDrawn{};
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    int bulletDamage{}, meleeDamage{};
    // Exact 101202F0 rule result: original metadata180 or rules-v20 clip*3=54.
    // Zero is deliberately invalid; the host must authenticate this rule.
    int reserveLimit{};
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct VP70Prepared
{
    MagazineSequenceTimes sequences;
    BallisticsEffects ballistics{};
    PistolMeleeEffects melee{};
    // Read from the original inventory item metadata, not guessed from the MDL.
    Vector aimOffset{};
    int aimZoom{};
    int viewModelString{}, playerModelString{}, shellModel{};
    int worldModelString{}, inventoryItemString{};
    int hostSlot{}, hostPosition{};
    bool modelsReady{}, soundsReady{}, particleReady{}, aimMetadataReady{};
    bool inventoryReady{}, pickupSoundReady{}, deployPreludeReady{};
    MeleeWorld world{MeleeWorld::UnsupportedSingleplayer};
};
class VP70Weapon;
struct VP70Backends;
// Per-entity cold provider, installed by the host factory before Spawn/Precache.
// current covers the map/resource generation and must work without a player.
// InventoryPickup must consume the original item metadata/capacity rules; it is
// not interchangeable with CS's CanHavePlayerItem or a forced-success callback.
struct VP70ColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, VP70Weapon&, VP70Prepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, VP70Backends&) noexcept {};
    bool (*inventoryPickup)(void*, CBasePlayer&, VP70Weapon&, int itemString) noexcept {};
};
struct VP70Backends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, VP70OwnerFrame&) noexcept {};
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const VP70Event&) noexcept {};
    std::uint32_t preparedEffects{};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
};

// Actual ReGameDLL weapon class. Registry/ammo/transport hookup is deliberately
// outside this translation unit. Original IDs never index CS's weapon tables.
class VP70Weapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle,
                         public OwnerFrameWeapon, public goldcraft::scenario::GuestWeapon
{
public:
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept override;
    void ConfigureColdHost(VP70ColdHost host) noexcept;
    VP70Status Prepare(const VP70Prepared&) noexcept;
    bool ResourcesReady() const noexcept;
    // Cold operation after real model/resource/backend preparation. No fallback
    // for absent aim, recoil, particles, sequences or player animation labels.
    VP70Status Bind(CBasePlayer&, const VP70Prepared&, const VP70Backends&) noexcept;
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo*) override;
    int AddToPlayer(CBasePlayer*) override;
    int AddDuplicate(CBasePlayerItem*) override;
    int ExtractAmmo(CBasePlayerWeapon*) override;
    int ExtractClipAmmo(CBasePlayerWeapon*) override;
    int AddWeapon() override;
    void AttachToPlayer(CBasePlayer*) override;
    void Drop() override;
    void Kill() override;
    void BeforeWeaponDrop() override { Invalidate(); }
    // This adapter commits through the original absolute-time reload path.
    // CS's countdown reload-completed callback must never finalize it twice.
    void WeaponReloadCompleted(int) override;
    int ObjectCaps() override;
    void EXPORT WorldFallThink();
    void EXPORT WorldTouch(CBaseEntity*);
    void EXPORT WorldUse(CBaseEntity*, CBaseEntity*, USE_TYPE, float);
    int iItemSlot() override { return PISTOL_SLOT; }
    BOOL CanDeploy() override;
    BOOL Deploy() override;
    void Holster(int skiplocal = 0) override;
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void ItemPostFrame() override;
    void WeaponIdle() override;
    BOOL UseDecrement() override { return FALSE; }
    BOOL IsPistol() override { return TRUE; }

    // Parent calls once from the original-equivalent player frame, before the
    // weapon input path, even while ownerAttack locks ItemPostFrame. Per-player
    // spread transitions are an explicit host multiplayer isolation adaptation.
    void AdvanceOwnerFrame() noexcept override;
    void EXPORT DelayedMelee();
    void Shoot();
    void EXPORT DispatchThink();
    void ScheduledBurst();
    // Must be called on death, map/life change and ownership removal. Host
    // generation must also change; invalidation alone is not an identity token.
    void Invalidate() noexcept override;
    VP70Status Status() const noexcept { return status_; }
    const MagazineActionState& ActionState() const noexcept { return action_; }
    const SpreadState& Spread() const noexcept { return spread_; }

private:
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const VP70Weapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    struct ActionBridge { VP70Weapon* item; Lease lease; bool callbackFailed{}; };
    static bool Current(void*) noexcept;
    struct WorldLease
    {
        edict_t* entity{};
        const VP70Weapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool kill) noexcept;
    static bool MagazineEvent(void*, const guest::cof::MagazineEvent&) noexcept;
    static void ReloadCommitted(void*, const MagazineReload&) noexcept;
    bool Check(VP70OwnerFrame&, bool active = true) noexcept;
    void ScheduleThink() noexcept;
    bool Emit(VP70Event) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool) noexcept;
    bool FireEvent(const VP70OwnerFrame&) noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool selected, bool ownerGate = true) noexcept;
    static MagazineActionHost ActionHost(ActionBridge&) noexcept;
    void SetResult(MagazineActionResult) noexcept;
    VP70Prepared prepared_{};
    VP70ColdHost cold_{};
    VP70Backends backends_{};
    MagazineActionState action_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, emptyDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    float wearDeadline_{};
    bool preparedReady_{}, bound_{}, selected_{}, meleePending_{}, worldUse_{};
    bool burstPending_{}, burstDispatch_{};
    float meleeDeadline_{}, burstDeadline_{};
    VP70Status status_{VP70Status::MissingBackend};
};
}
