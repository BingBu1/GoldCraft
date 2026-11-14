// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "native/include/goldcraft/scenario_guest_weapon.hpp"
#include "native/include/goldcraft/inventory_restore.hpp"
#include "owner_frame_weapon.hpp"
#include "weapons.h"
#include "../magazine_actions.hpp"
#include "../spread.hpp"
#include "ballistics.hpp"
#include "player_animation.hpp"
#include "weapon_melee.hpp"
#include "native/csol/server/weapons/weapons.h"
#include <optional>

namespace guest::cof::server
{
inline constexpr int RevolverHudSlot = 1;
inline constexpr int RevolverHudPosition = 2;
enum class RevolverStatus : unsigned char
{
    Ready, Applied, Ineligible, MissingBackend, InvalidInput, UnsupportedState, Cancelled
};
enum class RevolverEffect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    EjectEvent, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState, ShotLatch,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, Body, FirstDraw,
    ReloadStage, AmmoX, Cylinder, Count
};
struct RevolverEvent
{
    RevolverEffect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    int modelString{};
    // EjectEvent: a=absolute-event delay, value=old clip; flags0, zero angles,
    // origin=first, all remaining original event parameters zero.
    // FireEvent: flags0, value=original21, origin=first, zero other parameters.
    // ReloadStage: value=0/start,1/insert,2/end; a=original player sequence
    // deadline for start/insert. End keeps the actual selected sequence time.
    // Cylinder: a=actual owner angle, b=owner update gate, value=target angle;
    // enabled writes the update gate; false preserves the previous owner gate.
};
inline constexpr std::uint32_t RevolverRequiredEffects =
    (1u << static_cast<unsigned>(RevolverEffect::Count)) - 1u;

struct RevolverOwnerFrame
{
    float activityBlock{}, pendingGateTime{};
    // Original owner Post100EFB41..EFC5D mutable state, authenticated by host.
    float cylinderAngle{}, cylinderGate{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{};
    bool firstDrawn{};
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    // Producer includes original owner680 damage35 override when supported.
    int bulletDamage{}, meleeDamage{}, reserveLimit{};
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct RevolverPrepared
{
    MagazineSequenceTimes sequences;
    BallisticsEffects ballistics{};
    PistolMeleeEffects melee{};
    Vector aimOffset{};
    int aimZoom{};
    int viewModelString{}, playerModelString{}, shellModel{};
    int worldModelString{}, inventoryItemString{};
    int hostSlot{}, hostPosition{};
    bool modelsReady{}, soundsReady{}, particleReady{}, aimMetadataReady{};
    bool inventoryReady{}, pickupSoundReady{}, deployPreludeReady{};
    bool cylinderReady{}, ejectEventReady{}, reloadStagesReady{}, ammoXReady{};
    MeleeWorld world{MeleeWorld::UnsupportedSingleplayer};
};
class RevolverWeapon;
struct RevolverBackends;
struct RevolverColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, RevolverWeapon&, RevolverPrepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, RevolverBackends&) noexcept {};
    // Actual named inventory capacity/already-owned branch, not CS slot count.
    bool (*inventoryPickup)(void*, CBasePlayer&, RevolverWeapon&, int) noexcept {};
};
struct RevolverBackends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, RevolverOwnerFrame&) noexcept {};
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const RevolverEvent&) noexcept {};
    std::uint32_t preparedEffects{};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
    std::uint64_t ownerLife{};
};
struct RevolverCylinder
{
    bool loading{}, cancel{}, pending{};
    float commitDeadline{};
    int targetAngle{};
};

// Original hl.dll0036b91c, ID21; actual SDK host898. Cold resources and typed
// consumers are mandatory. Registration/transport belongs to the host factory.
class RevolverWeapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle,
    public goldcraft::scenario::GuestWeapon, public goldcraft::inventory::RestorableWeapon,
    public OwnerFrameWeapon
{
public:
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept override;
    bool SaveInventorySnapshot(goldcraft::inventory::Snapshot&) const noexcept override;
    bool RestoreInventoryBinding(CBasePlayer&) noexcept override;
    bool RestoreInventorySnapshot(const goldcraft::inventory::Snapshot&) noexcept override;
    void ConfigureColdHost(RevolverColdHost) noexcept;
    RevolverStatus Prepare(const RevolverPrepared&) noexcept;
    bool ResourcesReady() const noexcept;
    RevolverStatus Bind(CBasePlayer&, const RevolverPrepared&, const RevolverBackends&) noexcept;
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
    // Same original-equivalent owner frame, even while nextAttack locks input.
    void AdvanceOwnerFrame() noexcept override;
    void EXPORT DispatchThink();
    void DelayedMelee();
    void ResetCylinder();
    void ReloadInsert();
    void SetCylinder();
    void Invalidate() noexcept override;
    void OwnerModelChanging() noexcept override;
    RevolverStatus Status() const noexcept { return status_; }
    const MagazineActionState& ActionState() const noexcept { return action_; }
    const RevolverCylinder& CylinderState() const noexcept { return cylinder_; }
    const SpreadState& Spread() const noexcept { return spread_; }
private:
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const RevolverWeapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    static bool Current(void*) noexcept;
    struct WorldLease
    {
        edict_t* entity{};
        const RevolverWeapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool) noexcept;
    bool Check(RevolverOwnerFrame&, bool active = true) noexcept;
    bool Emit(RevolverEvent) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool) noexcept;
    bool PublishOwnerState() noexcept;
    struct InventoryState;
    bool Body(int) noexcept;
    bool ReloadAnimation(int stage) noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool) noexcept;
    void ScheduleThink() noexcept;
    // Cylinder and player melee were separate original think slots. A bounded
    // host dispatcher owns two independent clocks; no dynamic queue/allocation.
    enum class CylinderThink : unsigned char { None, Reset, Insert, Set };
    struct ModelRebind
    {
        edict_t* owner{};
        void* map{};
        std::uint64_t life{};
        int ownerSerial{}, weaponSerial{}, spawn{};
        CylinderThink cylinderThink{};
        float cylinderDeadline{}, meleeDeadline{};
        bool selected{}, meleePending{};
    };
    std::optional<ModelRebind> modelRebind_;
    void ScheduleCylinder(CylinderThink, float) noexcept;
    void RunCylinder();
    RevolverPrepared prepared_{};
    RevolverColdHost cold_{};
    RevolverBackends backends_{};
    MagazineActionState action_{};
    RevolverCylinder cylinder_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, emptyDeadline_{}, wearDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    CylinderThink cylinderThink_{};
    float cylinderDeadline_{}, meleeDeadline_{};
    bool preparedReady_{}, bound_{}, selected_{}, meleePending_{}, worldUse_{};
    RevolverStatus status_{RevolverStatus::MissingBackend};
};
}
