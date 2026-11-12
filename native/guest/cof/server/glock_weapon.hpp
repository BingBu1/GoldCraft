// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "weapons.h"
#include "../magazine_actions.hpp"
#include "../magazine_reload.hpp"
#include "../spread.hpp"
#include "ballistics.hpp"
#include "player_animation.hpp"
#include "weapon_melee.hpp"
#include "native/csol/server/weapons/weapons.h"

namespace guest::cof::server
{
enum class GlockStatus : unsigned char
{
    Ready, Applied, Ineligible, MissingBackend, InvalidInput, UnsupportedState, Cancelled
};

// Typed original effects that do not exist in CS. The parent authenticates and
// transports these; acknowledging a missing renderer/audio consumer is invalid.
enum class GlockEffect : unsigned char
{
    DeployPrelude, ReserveHud, InventoryChanged, ViewAnimation, EmptyClick,
    Shell, Gunsmoke, FireEvent, Recoil, Shake, Aim, FireSound, ShotState, ShotLatch,
    PickupSound, DeployReset, Sleeve, AmmoPickupSound, Body, FirstDraw, LightState, Tutorial, AttachmentSound, Count
};
struct GlockEvent
{
    GlockEffect kind{};
    Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
    bool enabled{};
    // Gunsmoke: value is the original weapon-edict owner, enabled is local,
    // other is attachment, and modelString is the prepared view-model string.
    // The script is the fixed original aurora/gunsmoke.aur resource.
    int modelString{};
};
// Complete effect catalog for providers; individual operations require only
// their reachable bits. This is deliberately not a Bind admission condition.
inline constexpr std::uint32_t GlockRequiredEffects =
    (1u << static_cast<unsigned>(GlockEffect::Count)) - 1u;

struct GlockOwnerFrame
{
    // Exact original-only state, read from the authenticated server session.
    // No inference from CS fields and no client claims/default-success values.
    float activityBlock{}, pendingGateTime{};
    bool condition3AA{}, pendingGate{}, toggleAim{}, infiniteAmmo{}, quietAudio{};
    int eventOwner2814{};
    bool firstDrawn{}, lightOn{};
    std::uint32_t originalFlags{}, pressed{}, released{}, randomSeed{};
    int bulletDamage{}, meleeDamage{};
    // Exact 101202F0 rule result: original metadata150 or rules-v20 clip*3=45.
    // Zero is deliberately invalid; the host must authenticate this rule.
    int reserveLimit{};
    PistolSource sourceMode{PistolSource::UnsupportedOriginalOverride};
};
struct GlockPrepared
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
    bool inventoryReady{}, pickupSoundReady{}, deployPreludeReady{}, taclightSoundsReady{};
    MeleeWorld world{MeleeWorld::UnsupportedSingleplayer};
};
class GlockWeapon;
struct GlockBackends;
// Per-entity cold provider, installed by the host factory before Spawn/Precache.
// current covers the map/resource generation and must work without a player.
// InventoryPickup must consume the original item metadata/capacity rules; it is
// not interchangeable with CS's CanHavePlayerItem or a forced-success callback.
struct GlockColdHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*prepare)(void*, GlockWeapon&, GlockPrepared&) noexcept {};
    bool (*ownerBackends)(void*, CBasePlayer&, GlockBackends&) noexcept {};
    bool (*inventoryPickup)(void*, CBasePlayer&, GlockWeapon&, int itemString) noexcept {};
};
struct GlockBackends
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*ownerFrame)(void*, GlockOwnerFrame&) noexcept {};
    bool (*effect)(void*, CBasePlayer&, CBasePlayerWeapon&, const GlockEvent&) noexcept {};
    std::uint32_t preparedEffects{};
    const PlayerAnimationAdapter* playerAnimation{};
    PlayerModelGeneration playerModelGeneration{};
};

// Actual ReGameDLL weapon class. Registry/ammo/transport hookup is deliberately
// outside this translation unit. Original IDs never index CS's weapon tables.
class GlockWeapon final : public CBasePlayerWeapon, public CSOLWeaponLifecycle
{
public:
    void ConfigureColdHost(GlockColdHost host) noexcept;
    GlockStatus Prepare(const GlockPrepared&) noexcept;
    // World/inventory resources only; this never promises combat/aim/light readiness.
    bool ResourcesReady() const noexcept;
    // Cold identity binding; each operation admits its reachable original effects
    // and resources before its first visible mutation. Missing consumers reject.
    GlockStatus Bind(CBasePlayer&, const GlockPrepared&, const GlockBackends&) noexcept;
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
    void AdvanceOwnerFrame() noexcept;
    void EXPORT DelayedMelee();
    // Called only after the authenticated inventory combine consumes a taclight.
    // Original100E3020 sets Glock+1F8 after the glockwithtaclight branch.
    GlockStatus RequestTaclightAttachment() noexcept;
    void WeaponFunction();
    void EXPORT MidAttachTaclight();
    void EXPORT AttachTaclight();
    void EXPORT ToggleSwitch();
    bool TaclightInstalled() const noexcept { return taclightInstalled_; }
    // Must be called on death, map/life change and ownership removal. Host
    // generation must also change; invalidation alone is not an identity token.
    void Invalidate() noexcept;
    GlockStatus Status() const noexcept { return status_; }
    const MagazineActionState& ActionState() const noexcept { return action_; }
    const SpreadState& Spread() const noexcept { return spread_; }

private:
    struct Lease
    {
        edict_t* owner{};
        edict_t* weapon{};
        CBasePlayer* player{};
        const GlockWeapon* item{};
        int ownerSerial{}, weaponSerial{}, modelIndex{};
        void* context{};
        bool (*current)(void*) noexcept {};
        bool active{};
    } lease_;
    struct ActionBridge { GlockWeapon* item; Lease lease; bool callbackFailed{}; };
    static bool Current(void*) noexcept;
    struct WorldLease
    {
        edict_t* entity{};
        const GlockWeapon* item{};
        int serial{};
        void* context{};
        bool (*current)(void*) noexcept {};
    } worldLease_;
    static bool WorldCurrent(const WorldLease&) noexcept;
    void WorldMaterialize() noexcept;
    void RetireWorld(bool kill) noexcept;
    static bool MagazineEvent(void*, const guest::cof::MagazineEvent&) noexcept;
    static void ReloadCommitted(void*, const MagazineReload&) noexcept;
    bool Check(GlockOwnerFrame&, bool active = true) noexcept;
    bool RequireEffects(std::uint32_t) noexcept;
    bool RequireSequence(int) noexcept;
    bool RequireAnimation(PlayerAction) noexcept;
    bool ReloadReady(bool resumePending = false) noexcept;
    bool PrimaryReady(const GlockOwnerFrame&, bool forceEmpty = false) noexcept;
    bool MeleeReady(const GlockOwnerFrame&) noexcept;
    bool AttachmentReady() noexcept;
    bool AimReady(bool) noexcept;
    bool Emit(GlockEvent) noexcept;
    bool Animate(PlayerAction) noexcept;
    bool View(int) noexcept;
    bool SetAim(bool) noexcept;
    bool UpdateBody(bool lightOn) noexcept;
    bool Fidget() noexcept;
    void ReadAmmo() noexcept;
    void WriteAmmo() noexcept;
    void MirrorTimers(bool selected) noexcept;
    static MagazineActionHost ActionHost(ActionBridge&) noexcept;
    void SetResult(MagazineActionResult) noexcept;
    GlockPrepared prepared_{};
    GlockColdHost cold_{};
    GlockBackends backends_{};
    MagazineActionState action_{};
    SpreadState spread_{};
    SpreadTransitions transitions_{};
    float actionDeadline_{}, emptyDeadline_{};
    int reserveHud_{}, wearMaximum_{}, wear_{};
    float wearDeadline_{};
    bool preparedReady_{}, bound_{}, selected_{}, meleePending_{}, worldUse_{};
    bool taclightAttachPending_{}, taclightInstalled_{};
    unsigned lightThink_{}; // 1 switch,2 mid attach,3 final attach
    GlockStatus status_{GlockStatus::MissingBackend};
};
}
