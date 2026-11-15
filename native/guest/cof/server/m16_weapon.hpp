// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "weapons.h"
#include "../magazine_actions.hpp"
#include "../magazine_reload.hpp"
#include "../m16_control.hpp"
#include "../m16_shot.hpp"
#include "../spread.hpp"
#include "ballistics.hpp"
#include "player_animation.hpp"
#include "weapon_melee.hpp"
#include "owner_frame_weapon.hpp"
#include "native/include/goldcraft/scenario_guest_weapon.hpp"
#include "native/include/goldcraft/inventory_restore.hpp"
#include <optional>
#include "native/csol/server/weapons/weapons.h"

namespace guest::cof::server
{
class PlayerThinkSlot;
inline constexpr int M16HostId = 902;
inline constexpr int M16HudSlot = 0;
inline constexpr int M16HudPosition = 2;
enum class M16Status : unsigned char
{
    Ready, Applied, Ineligible, MissingBackend, InvalidInput, UnsupportedState, Cancelled
};

// Typed original effects that do not exist in CS. The parent authenticates and
// transports these; acknowledging a missing renderer/audio consumer is invalid.
enum class M16Effect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    Shell, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState, ShotLatch,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, Count
};
struct M16Event
{
    M16Effect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    // Gunsmoke: value is the original weapon-edict owner, enabled is local,
    // other is attachment, and modelString is the prepared view-model string.
    // The script is the fixed original aurora/gunsmoke.aur resource.
    int modelString{};
};
inline constexpr std::uint32_t M16RequiredEffects =
    (1u << static_cast<unsigned>(M16Effect::Count)) - 1u;

struct M16OwnerFrame
{
    // Exact original-only state, read from the authenticated server session.
    // No inference from CS fields and no client claims/default-success values.
    float activityBlock{}, pendingGateTime{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{};
    int eventOwner2814{};
    float stamina{}; // Original authoritative owner+21F0; not inferred from CS ammo/health.
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    int bulletDamage{};
    // Exact 101202F0 rule result: original metadata200 or rules-v20 clip*3=60.
    // Zero is deliberately invalid; the host must authenticate this rule.
    int reserveLimit{};
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct M16Prepared
{
    MagazineSequenceTimes sequences;
    BallisticsEffects ballistics{};
    bool pushbackReady{}; // Prepared original long-gun pushback consumer, not pistol-whip.
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
class M16Weapon;
struct M16Backends;
// Per-entity cold provider, installed by the host factory before Spawn/Precache.
// current covers the map/resource generation and must work without a player.
// InventoryPickup must consume the original item metadata/capacity rules; it is
// not interchangeable with CS's CanHavePlayerItem or a forced-success callback.
struct M16ColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, M16Weapon&, M16Prepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, M16Backends&) noexcept {};
    bool (*inventoryPickup)(void*, CBasePlayer&, M16Weapon&, int itemString) noexcept {};
};
struct M16Backends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, M16OwnerFrame&) noexcept {};
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const M16Event&) noexcept {};
    std::uint32_t preparedEffects{};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
    std::uint64_t ownerLife{};
    PlayerThinkSlot* playerThink{};
};

// Actual ReGameDLL weapon class. Registry/ammo/transport hookup is deliberately
// outside this translation unit. Original IDs never index CS's weapon tables.
class M16Weapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle,
                         public OwnerFrameWeapon, public goldcraft::scenario::GuestWeapon,
                         public goldcraft::inventory::RestorableWeapon
{
public:
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept override;
    bool SaveInventorySnapshot(goldcraft::inventory::Snapshot&) const noexcept override;
    bool RestoreInventoryBinding(CBasePlayer&) noexcept override;
    bool RestoreInventorySnapshot(const goldcraft::inventory::Snapshot&) noexcept override;
    void ConfigureColdHost(M16ColdHost host) noexcept;
    M16Status Prepare(const M16Prepared&) noexcept;
    bool ResourcesReady() const noexcept;
    // Cold operation after real model/resource/backend preparation. No fallback
    // for absent aim, recoil, particles, sequences or player animation labels.
    M16Status Bind(CBasePlayer&, const M16Prepared&, const M16Backends&) noexcept;
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
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    BOOL CanDeploy() override;
    BOOL Deploy() override;
    void Holster(int skiplocal = 0) override;
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void ItemPostFrame() override;
    void WeaponIdle() override;
    BOOL UseDecrement() override { return FALSE; }
    BOOL IsPistol() override { return FALSE; }

    // Parent calls once from the original-equivalent player frame, before the
    // weapon input path, even while ownerAttack locks ItemPostFrame. Per-player
    // spread transitions are an explicit host multiplayer isolation adaptation.
    void AdvanceOwnerFrame() noexcept override;
    void Shoot();
    void ToggleFireMode();
    bool BurstMode() const noexcept { return burst_; }
    bool BurstPending() const noexcept { return burstPending_; }
    float BurstDeadline() const noexcept { return burstDeadline_; }
    void EXPORT DispatchThink();
    void ScheduledBurst();
    // Must be called on death, map/life change and ownership removal. Host
    // generation must also change; invalidation alone is not an identity token.
    void Invalidate() noexcept override;
    void OwnerModelChanging() noexcept override;
    M16Status Status() const noexcept { return status_; }
    const MagazineActionState& ActionState() const noexcept { return action_; }
    const SpreadState& Spread() const noexcept { return spread_; }

private:
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const M16Weapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    struct ActionBridge { M16Weapon* item; Lease lease; M16ControlState* control{}; bool callbackFailed{}; };
    static bool Current(void*) noexcept;
    struct WorldLease
    {
        edict_t* entity{};
        const M16Weapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool kill) noexcept;
    static bool ControlEvent(void*, const guest::cof::M16ControlEvent&) noexcept;
    static void ReloadCommitted(void*, const MagazineReload&) noexcept;
    bool Check(M16OwnerFrame&, bool active = true) noexcept;
    void ScheduleThink() noexcept;
    bool Emit(M16Event) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool) noexcept;
    bool PublishOwnerState() noexcept;
    bool FireEvent(const M16OwnerFrame&) noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool selected, bool ownerGate = true) noexcept;
    static M16ControlHost ControlHost(ActionBridge&) noexcept;
    void SetResult(M16ControlResult) noexcept;
    M16Prepared prepared_{};
    M16ColdHost cold_{};
    M16Backends backends_{};
    MagazineActionState action_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, emptyDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    float wearDeadline_{};
    bool preparedReady_{}, bound_{}, selected_{}, worldUse_{};
    bool burstPending_{}, burstDispatch_{}, burst_{true};
    float burstDeadline_{}, spreadIncrement_{.75F};
    struct InventoryState;
    struct ModelRebind
    {
        edict_t* owner{};
        void* map{};
        std::uint64_t life{};
        int ownerSerial{},weaponSerial{},spawn{};
        float burstDeadline{};
        bool selected{},burstPending{};
    };
    std::optional<ModelRebind> modelRebind_;
    M16Status status_{M16Status::MissingBackend};
};
}
