// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "weapons.h"
#include "../magazine_actions.hpp"
#include "../shotgun_aim.hpp"
#include "../shotgun_frame.hpp"
#include "../shotgun_shot.hpp"
#include "../spread.hpp"
#include "ballistics.hpp"
#include "owner_frame_weapon.hpp"
#include "player_animation.hpp"
#include "weapon_melee.hpp"
#include "native/include/goldcraft/inventory_restore.hpp"
#include "native/include/goldcraft/scenario_guest_weapon.hpp"
#include "native/csol/server/weapons/weapons.h"
#include <optional>

namespace guest::cof::server
{
class PlayerThinkSlot;
inline constexpr int ShotgunHostId = 904;
inline constexpr int ShotgunHudSlot = 0;
inline constexpr int ShotgunHudPosition = 4;
enum class ShotgunStatus : unsigned char
{
    Ready, Applied, Ineligible, MissingBackend, InvalidInput, UnsupportedState, Cancelled
};
enum class ShotgunEffect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    Shell, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, CameraZoom,
    ReloadStage, AmmoWarning, Count
};
struct ShotgunEvent
{
    ShotgunEffect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    int modelString{};
    // ReloadStage: 0=start, 1=insert, 2=empty finish, 3=loaded finish,
    // 4=interrupted finish; a is the original start/insert animation deadline.
};
inline constexpr std::uint32_t ShotgunRequiredEffects =
    (1u << static_cast<unsigned>(ShotgunEffect::Count)) - 1u;
struct ShotgunOwnerFrame
{
    float activityBlock{}, pendingGateTime{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{};
    int eventOwner2814{};
    std::optional<float> stamina;
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    int bulletDamage{};
    int reserveLimit{}; // Original metadata50 or cooperative rules-v20 clip*3=15.
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct ShotgunPrepared
{
    MagazineSequenceTimes sequences;
    BallisticsEffects ballistics{};
    bool pushbackReady{};
    Vector aimOffset{};
    int aimZoom{};
    int viewModelString{}, playerModelString{}, shellModel{};
    int worldModelString{}, inventoryItemString{};
    int hostSlot{}, hostPosition{};
    bool modelsReady{}, soundsReady{}, particleReady{}, aimMetadataReady{};
    bool inventoryReady{}, pickupSoundReady{}, deployPreludeReady{};
    MeleeWorld world{MeleeWorld::UnsupportedSingleplayer};
};
class ShotgunWeapon;
struct ShotgunBackends;
struct ShotgunColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, ShotgunWeapon&, ShotgunPrepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, ShotgunBackends&) noexcept {};
    bool (*inventoryPickup)(void*, CBasePlayer&, ShotgunWeapon&, int) noexcept {};
};
struct ShotgunBackends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, ShotgunOwnerFrame&) noexcept {};
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const ShotgunEvent&) noexcept {};
    std::uint32_t preparedEffects{};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
    std::uint64_t ownerLife{};
    PlayerThinkSlot* playerThink{};
};

// Ordinary cooperative 870. Registration and paired transport admission remain
// the host's cold transaction; this class does not grant purchase capability.
class ShotgunWeapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle,
    public OwnerFrameWeapon, public goldcraft::scenario::GuestWeapon,
    public goldcraft::inventory::RestorableWeapon
{
public:
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept override;
    bool SaveInventorySnapshot(goldcraft::inventory::Snapshot&) const noexcept override;
    bool RestoreInventoryBinding(CBasePlayer&) noexcept override;
    bool RestoreInventorySnapshot(const goldcraft::inventory::Snapshot&) noexcept override;
    void ConfigureColdHost(ShotgunColdHost) noexcept;
    ShotgunStatus Prepare(const ShotgunPrepared&) noexcept;
    bool ResourcesReady() const noexcept;
    ShotgunStatus Bind(CBasePlayer&, const ShotgunPrepared&, const ShotgunBackends&) noexcept;
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo*) override;
    int AddToPlayer(CBasePlayer*) override;
    int AddWeapon() override;
    int AddDuplicate(CBasePlayerItem*) override;
    int ExtractAmmo(CBasePlayerWeapon*) override;
    int ExtractClipAmmo(CBasePlayerWeapon*) override;
    void AttachToPlayer(CBasePlayer*) override;
    void Drop() override;
    void Kill() override;
    void BeforeWeaponDrop() override { Invalidate(); }
    int ObjectCaps() override;
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    void EXPORT WorldFallThink();
    void EXPORT WorldTouch(CBaseEntity*);
    void EXPORT WorldUse(CBaseEntity*, CBaseEntity*, USE_TYPE, float);
    void EXPORT DispatchThink();
    BOOL CanDeploy() override;
    BOOL Deploy() override;
    void Holster(int = 0) override;
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void ItemPostFrame() override;
    void WeaponIdle() override;
    void WeaponReloadCompleted(int) override;
    BOOL UseDecrement() override { return FALSE; }
    BOOL IsPistol() override { return FALSE; }
    void AdvanceOwnerFrame() noexcept override;
    void Invalidate() noexcept override;
    void OwnerModelChanging() noexcept override;
    ShotgunStatus Status() const noexcept { return status_; }
    const ShotgunActionState& ActionState() const noexcept { return shot_.action; }
    const SpreadState& Spread() const noexcept { return spread_; }

private:
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const ShotgunWeapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    static bool Current(void*) noexcept;
    struct WorldLease
    {
        edict_t* entity{};
        const ShotgunWeapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool) noexcept;
    struct ActionBridge { ShotgunWeapon* item; Lease lease; };
    static bool ControlEvent(void*, const ShotgunControlEvent&) noexcept;
    static ShotgunControlHost ControlHost(ActionBridge&) noexcept;
    ShotgunAimHost AimHost(ActionBridge&) noexcept;
    bool Check(ShotgunOwnerFrame&, bool active = true) noexcept;
    bool Emit(ShotgunEvent) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool, bool input = false) noexcept;
    bool PublishOwnerState() noexcept;
    bool CurrentDuration(float&) noexcept;
    bool CommonFrame() noexcept;
    bool Fidget() noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool selected, bool ownerGate = true) noexcept;
    void SetResult(ShotgunControlResult) noexcept;
    void ScheduleThink() noexcept;
    void GuardExpandedReload() noexcept;
    ShotgunShotFrame ShotFrame(const ShotgunOwnerFrame&) const noexcept;
    ShotgunShotHost ShotHost(ActionBridge&) noexcept;
    struct InventoryState;
    enum class WeaponTask : unsigned char { None, Insert, Pump };
    struct ModelRebind
    {
        edict_t* owner{};
        void* map{};
        std::uint64_t life{};
        int ownerSerial{}, weaponSerial{}, spawn{};
        WeaponTask task{};
        float deadline{};
        bool selected{};
    };
    std::optional<ModelRebind> modelRebind_;
    ShotgunPrepared prepared_{};
    ShotgunColdHost cold_{};
    ShotgunBackends backends_{};
    ShotgunShotState shot_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, wearDeadline_{}, taskDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    WeaponTask task_{};
    bool preparedReady_{}, bound_{}, selected_{}, worldUse_{};
    ShotgunStatus status_{ShotgunStatus::MissingBackend};
};
}
