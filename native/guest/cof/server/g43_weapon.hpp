// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "weapons.h"
#include "../magazine_actions.hpp"
#include "../magazine_reload.hpp"
#include "../g43_control.hpp"
#include "../g43_aim.hpp"
#include "../g43_frame.hpp"
#include "../g43_shot.hpp"
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
inline constexpr int G43HostId = 903;
inline constexpr int G43HudSlot = 0;
inline constexpr int G43HudPosition = 3;
enum class G43Status : unsigned char
{
    Ready, Applied, Ineligible, MissingBackend, InvalidInput, UnsupportedState, Cancelled
};

// Typed original effects that do not exist in CS. The parent authenticates and
// transports these; acknowledging a missing renderer/audio consumer is invalid.
enum class G43Effect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    Shell, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState, ShotLatch,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, CameraZoom, Count
};
struct G43Event
{
    G43Effect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    // Gunsmoke: value is the original weapon-edict owner, enabled is local,
    // other is attachment1, and modelString is the prepared view-model string.
    // The script is the fixed original aurora/gunsmoke.aur resource.
    int modelString{};
};
inline constexpr std::uint32_t G43RequiredEffects =
    (1u << static_cast<unsigned>(G43Effect::Count)) - 1u;

struct G43OwnerFrame
{
    // Exact original-only state, read from the authenticated server session.
    // No inference from CS fields and no client claims/default-success values.
    float activityBlock{}, pendingGateTime{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{};
    int eventOwner2814{};
    // Original authoritative owner+21F0. Shared-host service unavailability is
    // explicit: it disables this action, not unrelated gunfire or reloading.
    std::optional<float> stamina;
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    int bulletDamage{};
    // Exact 101202F0 rule result: original metadata40 or rules-v20 clip*3=30.
    // Zero is deliberately invalid; the host must authenticate this rule.
    int reserveLimit{};
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct G43Prepared
{
    MagazineSequenceTimes sequences;
    BallisticsEffects ballistics{};
    int csPenetration{}; // Cold host rule; zero retains original isolated simulation.
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
class G43Weapon;
struct G43Backends;
// Per-entity cold provider, installed by the host factory before Spawn/Precache.
// current covers the map/resource generation and must work without a player.
// InventoryPickup must consume the original item metadata/capacity rules; it is
// not interchangeable with CS's CanHavePlayerItem or a forced-success callback.
struct G43ColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, G43Weapon&, G43Prepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, G43Backends&) noexcept {};
    bool (*inventoryPickup)(void*, CBasePlayer&, G43Weapon&, int itemString) noexcept {};
};
struct G43Backends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, G43OwnerFrame&) noexcept {};
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const G43Event&) noexcept {};
    std::uint32_t preparedEffects{};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
    std::uint64_t ownerLife{};
    PlayerThinkSlot* playerThink{};
};

// Actual ReGameDLL weapon class. Registry/ammo/transport hookup is deliberately
// outside this translation unit. Original IDs never index CS's weapon tables.
class G43Weapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle,
                         public OwnerFrameWeapon, public goldcraft::scenario::GuestWeapon,
                         public goldcraft::inventory::RestorableWeapon
{
public:
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept override;
    bool SaveInventorySnapshot(goldcraft::inventory::Snapshot&) const noexcept override;
    bool RestoreInventoryBinding(CBasePlayer&) noexcept override;
    bool RestoreInventorySnapshot(const goldcraft::inventory::Snapshot&) noexcept override;
    void ConfigureColdHost(G43ColdHost host) noexcept;
    G43Status Prepare(const G43Prepared&) noexcept;
    bool ResourcesReady() const noexcept;
    // Cold operation after real model/resource/backend preparation. No fallback
    // for absent aim, recoil, particles, sequences or player animation labels.
    G43Status Bind(CBasePlayer&, const G43Prepared&, const G43Backends&) noexcept;
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
        // Must be called on death, map/life change and ownership removal. Host
    // generation must also change; invalidation alone is not an identity token.
    void Invalidate() noexcept override;
    void OwnerModelChanging() noexcept override;
    G43Status Status() const noexcept { return status_; }
    const MagazineActionState& ActionState() const noexcept { return action_; }
    const SpreadState& Spread() const noexcept { return spread_; }

private:
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const G43Weapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    struct ActionBridge { G43Weapon* item; Lease lease; bool callbackFailed{}; };
    static bool Current(void*) noexcept;
    struct WorldLease
    {
        edict_t* entity{};
        const G43Weapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool kill) noexcept;
    static bool ControlEvent(void*, const guest::cof::G43ControlEvent&) noexcept;
    static void ReloadCommitted(void*, const MagazineReload&) noexcept;
    bool Check(G43OwnerFrame&, bool active = true) noexcept;
    bool Emit(G43Event) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool, bool input = false) noexcept;
    bool PublishOwnerState() noexcept;
    bool CurrentDuration(float&) noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool selected, bool ownerGate = true) noexcept;
    static G43ControlHost ControlHost(ActionBridge&) noexcept;
    void SetResult(G43ControlResult) noexcept;
    G43Prepared prepared_{};
    G43ColdHost cold_{};
    G43Backends backends_{};
    MagazineActionState action_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, emptyDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    float wearDeadline_{};
    bool preparedReady_{}, bound_{}, selected_{}, worldUse_{};
    float spreadIncrement_{2.4F};
    struct InventoryState;
    struct ModelRebind
    {
        edict_t* owner{};
        void* map{};
        std::uint64_t life{};
        int ownerSerial{},weaponSerial{},spawn{};
        bool selected{};
    };
    std::optional<ModelRebind> modelRebind_;
    G43Status status_{G43Status::MissingBackend};
};
}
