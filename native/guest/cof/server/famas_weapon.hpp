// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "external/ReGameDLL_CS/regamedll/dlls/weapons.h"
#include "../famas_control.hpp"
#include "../famas_aim.hpp"
#include "../famas_frame.hpp"
#include "../famas_shot.hpp"
#include "../famas_reload.hpp"
#include "ballistics.hpp"
#include "player_animation.hpp"
#include "weapon_melee.hpp"
#include "owner_frame_weapon.hpp"
#include "native/include/goldcraft/scenario_guest_weapon.hpp"
#include "native/include/goldcraft/inventory_restore.hpp"
#include "native/csol/server/weapons/weapons.h"
#include <optional>

namespace guest::cof::server
{
class PlayerThinkSlot;
inline constexpr int FamasHostId = 905;
inline constexpr int FamasHudSlot = 0;
inline constexpr int FamasHudPosition = 5;
enum class FamasStatus : unsigned char
{
    Ready, Applied, AppliedWithoutPresentation, Ineligible, MissingBackend,
    InvalidInput, UnsupportedState, Cancelled
};
enum class FamasEffect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    Shell, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState, ShotLatch,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, CameraZoom,
    FirstDrawSound, FirstDrawSubtitle, HolsterPresentation
};
struct FamasEvent
{
    FamasEffect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    int modelString{};
};
struct FamasOwnerFrame
{
    float activityBlock{}, pendingGateTime{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{};
    int eventOwner2814{};
    std::optional<float> stamina;
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    int bulletDamage{};
    // Original101202F0: metadata150 or cooperative rule capacity30*3.
    int reserveLimit{};
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct FamasPrepared
{
    MagazineSequenceTimes sequences;
    BallisticsEffects ballistics{};
    int csPenetration{};
    Vector aimOffset{};
    int aimZoom{};
    int viewModelString{}, playerModelString{}, shellModel{};
    int worldModelString{}, inventoryItemString{};
    int hostSlot{}, hostPosition{};
    bool modelsReady{}, aimMetadataReady{}, inventoryReady{};
    MeleeWorld world{MeleeWorld::UnsupportedSingleplayer};
};
class FamasWeapon;
struct FamasBackends;
struct FamasColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, FamasWeapon&, FamasPrepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, FamasBackends&) noexcept {};
    bool (*inventoryPickup)(void*, CBasePlayer&, FamasWeapon&, int itemString) noexcept {};
};
struct FamasBackends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, FamasOwnerFrame&) noexcept {};
    // Optional presentation. Recoil is a separate required per-shot consumer.
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const FamasEvent&) noexcept {};
    bool (*recoil)(void*, CBasePlayer&, const Vector&) noexcept {};
    // Atomically consume original owner+3F4+ID23 on this connection. Missing
    // first-draw state suppresses that presentation, never ownership or firing.
    bool (*firstEquip)(void*, bool& first) noexcept {};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
    std::uint64_t ownerLife{};
    PlayerThinkSlot* playerThink{};
};

class FamasWeapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle,
                          public OwnerFrameWeapon, public goldcraft::scenario::GuestWeapon,
                          public goldcraft::inventory::RestorableWeapon
{
public:
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept override;
    bool SaveInventorySnapshot(goldcraft::inventory::Snapshot&) const noexcept override;
    bool RestoreInventoryBinding(CBasePlayer&) noexcept override;
    bool RestoreInventorySnapshot(const goldcraft::inventory::Snapshot&) noexcept override;
    void ConfigureColdHost(FamasColdHost) noexcept;
    FamasStatus Prepare(const FamasPrepared&) noexcept;
    bool ResourcesReady() const noexcept;
    FamasStatus Bind(CBasePlayer&, const FamasPrepared&, const FamasBackends&) noexcept;
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
    void Fidget() noexcept;
    BOOL UseDecrement() override { return FALSE; }
    BOOL IsPistol() override { return FALSE; }
    void AdvanceOwnerFrame() noexcept override;
    void Invalidate() noexcept override;
    void OwnerModelChanging() noexcept override;
    FamasStatus Status() const noexcept { return status_; }
    const MagazineActionState& ActionState() const noexcept { return action_; }
    const SpreadState& Spread() const noexcept { return spread_; }

private:
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const FamasWeapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    struct ActionBridge { FamasWeapon* item; Lease lease; };
    struct WorldLease
    {
        edict_t* entity{};
        const FamasWeapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool Current(void*) noexcept;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool kill) noexcept;
    static bool ControlEvent(void*, const guest::cof::FamasControlEvent&) noexcept;
    static FamasControlHost ControlHost(ActionBridge&) noexcept;
    void SetResult(FamasControlResult) noexcept;
    bool EnsureBinding() noexcept;
    bool Check(FamasOwnerFrame&, bool active = true) noexcept;
    bool Emit(FamasEvent) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool, bool input = false) noexcept;
    bool PublishOwnerState() noexcept;
    bool CurrentDuration(float&) noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool selected, bool ownerGate = true) noexcept;
    FamasPrepared prepared_{};
    FamasColdHost cold_{};
    FamasBackends backends_{};
    MagazineActionState action_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, emptyDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    float wearDeadline_{};
    bool preparedReady_{}, bound_{}, selected_{}, worldUse_{};
    float spreadIncrement_{.15F};
    struct InventoryState;
    struct ModelRebind
    {
        edict_t* owner{};
        void* map{};
        std::uint64_t life{};
        int ownerSerial{}, weaponSerial{}, spawn{};
        bool selected{};
    };
    std::optional<ModelRebind> modelRebind_;
    FamasStatus status_{FamasStatus::MissingBackend};
};
}
