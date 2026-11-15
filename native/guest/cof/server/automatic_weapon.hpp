// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/include/goldcraft/scenario_guest_weapon.hpp"
#include "native/include/goldcraft/inventory_restore.hpp"

#include "weapons.h"
#include "../magazine_actions.hpp"
#include "../magazine_reload.hpp"
#include "../spread.hpp"
#include "ballistics.hpp"
#include "player_animation.hpp"
#include "weapon_melee.hpp"
#include "owner_frame_weapon.hpp"
#include "../automatic_sound.hpp"
#include <optional>
#include "native/csol/server/weapons/weapons.h"

namespace guest::cof::server
{
inline constexpr int TMPHostId=900, TMPHudSlot=0, TMPHudPosition=0;
inline constexpr int MP5HostId=901, MP5HudSlot=0, MP5HudPosition=1;
enum class TMPStatus : unsigned char
{
    Ready, Applied, Ineligible, MissingBackend, InvalidInput, UnsupportedState, Cancelled
};

// Typed original effects that do not exist in CS. The parent authenticates and
// transports these; acknowledging a missing renderer/audio consumer is invalid.
enum class TMPEffect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    Shell, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState, ShotLatch,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, FirstDraw, KillLocalParticles, AutomaticSound, Count
};
struct TMPEvent
{
    TMPEffect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    // Gunsmoke: value is the original weapon-edict owner, enabled is local,
    // other is attachment, and modelString is the prepared view-model string.
    // The script is the fixed original aurora/gunsmoke.aur resource.
    int modelString{};
    guest::cof::AutomaticSoundEvent sound{};
};
inline constexpr std::uint32_t TMPRequiredEffects =
    ((1u << static_cast<unsigned>(TMPEffect::Count)) - 1u) &
    ~(1u << static_cast<unsigned>(TMPEffect::FireSound)); // AutomaticSound owns native fire/tail/stop.
inline constexpr std::uint32_t MP5RequiredEffects = TMPRequiredEffects &
    ~(1U << static_cast<unsigned>(TMPEffect::FirstDraw));

struct TMPOwnerFrame
{
    // Exact original-only state, read from the authenticated server session.
    // No inference from CS fields and no client claims/default-success values.
    float activityBlock{}, pendingGateTime{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{}, firstDrawn{};
    int eventOwner2814{};
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    int bulletDamage{}, meleeDamage{};
    // Exact 101202F0 rule result: original metadata300 or rules-v20 clip*3=90.
    // Zero is deliberately invalid; the host must authenticate this rule.
    int reserveLimit{};
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct TMPPrepared
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
    bool automaticFireReady{}, automaticTailReady{};
    MeleeWorld world{MeleeWorld::UnsupportedSingleplayer};
};
template<LongGun Weapon> class AutomaticWeapon;
using TMPWeapon = AutomaticWeapon<LongGun::TMP>;
using MP5Weapon = AutomaticWeapon<LongGun::MP5>;
struct TMPBackends;
// Per-entity cold provider, installed by the host factory before Spawn/Precache.
// current covers the map/resource generation and must work without a player.
// InventoryPickup must consume the original item metadata/capacity rules; it is
// not interchangeable with CS's CanHavePlayerItem or a forced-success callback.
template<class Weapon> struct AutomaticColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, Weapon&, TMPPrepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, TMPBackends&) noexcept {};
    bool (*inventoryPickup)(void*, CBasePlayer&, Weapon&, int itemString) noexcept {};
};
using TMPColdHost = AutomaticColdHost<TMPWeapon>;
using MP5ColdHost = AutomaticColdHost<MP5Weapon>;
struct TMPBackends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, TMPOwnerFrame&) noexcept {};
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const TMPEvent&) noexcept {};
    std::uint32_t preparedEffects{};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
    std::uint64_t ownerLife{};
};

// Actual ReGameDLL weapon class. Registry/ammo/transport hookup is deliberately
// outside this translation unit. Original IDs never index CS's weapon tables. TMP remains unregistered until its complete host closure is installed.
template<LongGun Weapon>
class AutomaticWeapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle,
    public goldcraft::scenario::GuestWeapon, public goldcraft::inventory::RestorableWeapon,
    public OwnerFrameWeapon
{
public:
    static_assert(Weapon == LongGun::TMP || Weapon == LongGun::MP5);
    using ColdHost = AutomaticColdHost<AutomaticWeapon>;
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept override;
    bool SaveInventorySnapshot(goldcraft::inventory::Snapshot&) const noexcept override;
    bool RestoreInventoryBinding(CBasePlayer&) noexcept override;
    bool RestoreInventorySnapshot(const goldcraft::inventory::Snapshot&) noexcept override;
    void ConfigureColdHost(ColdHost host) noexcept;
    TMPStatus Prepare(const TMPPrepared&) noexcept;
    bool ResourcesReady() const noexcept;
    // Cold operation after real model/resource/backend preparation. No fallback
    // for absent aim, recoil, particles, sequences or player animation labels.
    TMPStatus Bind(CBasePlayer&, const TMPPrepared&, const TMPBackends&) noexcept;
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
    void EXPORT DelayedMelee();
    // Must be called on death, map/life change and ownership removal. Host
    // generation must also change; invalidation alone is not an identity token.
    void Invalidate() noexcept override;
    void OwnerModelChanging() noexcept override;
    TMPStatus Status() const noexcept { return status_; }
    const MagazineActionState& ActionState() const noexcept { return action_; }
    const SpreadState& Spread() const noexcept { return spread_; }

private:
    static constexpr int HostId = Weapon == LongGun::TMP ? TMPHostId : MP5HostId;
    static constexpr int HudSlot = Weapon == LongGun::TMP ? TMPHudSlot : MP5HudSlot;
    static constexpr int HudPosition = Weapon == LongGun::TMP ? TMPHudPosition : MP5HudPosition;
    static constexpr unsigned OriginalId = Weapon == LongGun::TMP ? 14 : 27;
    static constexpr unsigned SequenceCount = Weapon == LongGun::TMP ? 16 : 20;
    static constexpr int DefaultAmmo = Weapon == LongGun::TMP ? 30 : 300;
    static constexpr std::uint64_t InventoryKind = Weapon == LongGun::TMP ?
        0x434f46544d500000ULL : 0x434f464d50350000ULL;
    static constexpr std::uint32_t RequiredEffects = Weapon == LongGun::TMP ? TMPRequiredEffects : MP5RequiredEffects;
    struct ModelRebind
    {
        edict_t* owner{}; void* map{}; std::uint64_t life{};
        int ownerSerial{},weaponSerial{},spawn{};
        float nextThink{}; bool selected{},meleePending{};
    };
    std::optional<ModelRebind> modelRebind_;
    struct InventoryState;
    bool PublishOwnerAim() noexcept;
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const AutomaticWeapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    struct ActionBridge { AutomaticWeapon* item; Lease lease; bool callbackFailed{}; };
    static bool Current(void*) noexcept;
    struct WorldLease
    {
        edict_t* entity{};
        const AutomaticWeapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool kill) noexcept;
    static bool MagazineEvent(void*, const guest::cof::MagazineEvent&) noexcept;
    static void ReloadCommitted(void*, const MagazineReload&) noexcept;
    bool Check(TMPOwnerFrame&, bool active = true) noexcept;
    bool Emit(TMPEvent) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool) noexcept;
    bool AutomaticSound(bool start,const TMPOwnerFrame&) noexcept;
    static bool AutomaticSoundEvent(void*,const guest::cof::AutomaticSoundEvent&) noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool selected) noexcept;
    static MagazineActionHost ActionHost(ActionBridge&) noexcept;
    void SetResult(MagazineActionResult) noexcept;
    TMPPrepared prepared_{};
    ColdHost cold_{};
    TMPBackends backends_{};
    MagazineActionState action_{};
    guest::cof::AutomaticSoundState sound_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, emptyDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    float wearDeadline_{};
    bool preparedReady_{}, bound_{}, selected_{}, meleePending_{}, worldUse_{};
    TMPStatus status_{TMPStatus::MissingBackend};
};
}
