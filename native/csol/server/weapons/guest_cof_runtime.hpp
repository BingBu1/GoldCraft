// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_owner_runtime.hpp"
#include "guest_cof_resources.hpp"
#include "guest_cof_p345_owner.hpp"
#include "guest_cof_revolver_owner.hpp"
#include "guest_cof_vp70_owner.hpp"
#include "guest_cof_tmp_owner.hpp"
#include "guest_cof_mp5_owner.hpp"
#include "guest_cof_m16_owner.hpp"
#include "native/guest/cof/server/sound_dispatch.hpp"
#include <memory>
#include <optional>

namespace csol::server::cof
{
// Explicit host resource budgets, not inferred original gameplay limits.
struct GlockRuntimeLimits { unsigned retainedMaps, ownerBindingsPerMap; };

struct P345RuntimeHost
{
    bool (*prepare)(void*,const P345Resources&,guest::cof::server::P345Prepared&) noexcept {};
    bool (*effect)(void*,const P345Resources&,const OwnerBinding&,
        CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::P345Event&) noexcept {};
    std::uint32_t effects{};
};
struct RevolverRuntimeHost
{
    bool (*prepare)(void*,const RevolverResources&,guest::cof::server::RevolverPrepared&) noexcept {};
    bool (*effect)(void*,const RevolverResources&,const OwnerBinding&,
        CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::RevolverEvent&) noexcept {};
    std::uint32_t effects{};
};
struct VP70RuntimeHost
{
    bool (*prepare)(void*,const VP70Resources&,guest::cof::server::VP70Prepared&) noexcept {};
    bool (*effect)(void*,const VP70Resources&,const OwnerBinding&,
        CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::VP70Event&) noexcept {};
    std::uint32_t effects{};
};

template<class Resources> struct AutomaticRuntimeHost
{
    bool (*prepare)(void*,const Resources&,guest::cof::server::TMPPrepared&) noexcept {};
    bool (*effect)(void*,const Resources&,const OwnerBinding&,
        CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::TMPEvent&) noexcept {};
    std::uint32_t effects{};
};
using TMPRuntimeHost = AutomaticRuntimeHost<TMPResources>;
using MP5RuntimeHost = AutomaticRuntimeHost<MP5Resources>;

struct M16RuntimeHost
{
    bool (*prepare)(void*, const M16Resources&, guest::cof::server::M16Prepared&) noexcept {};
    bool (*effect)(void*, const M16Resources&, const OwnerBinding&,
        CBasePlayer&, CBasePlayerWeapon&, const guest::cof::server::M16Event&) noexcept {};
    // Actual map stage/cycle owner, independent of this runtime's model views.
    // Must preserve same-life tasks, and run BeginCommand/PostThink at their
    // verified stages. Runtime owns common native sounds and the original
    // cooperative fire-sound transport; remaining effects need real consumers.
    bool (*actions)(void*, CBasePlayer&, std::uint64_t map, std::uint64_t life,
        guest::cof::server::PlayerActionBinding&) noexcept {};
    std::uint32_t effects{};
    guest::cof::PlaySfxRules soundRules{guest::cof::PlaySfxRules::Unsupported};
};

// Only the genuinely external consumers remain here. The runtime itself owns
// native sounds, resources, factory, player generations and owner frame stages.
// Storage and callbacks must survive EngineEntitiesReleased for their map.
struct GlockRuntimeHost
{
    void* context{};
    bool (*current)(void*, std::uint64_t map) noexcept {};
    bool (*ownerCurrent)(void*, CBasePlayer&, const NativeOwnerMode&) noexcept {};
    // Fill external message/melee/particle/presentation admission only. Actual
    // catalog resources below are overwritten from the runtime's cold result.
    bool (*prepare)(void*, const GlockResources&, guest::cof::server::GlockPrepared&) noexcept {};
    bool (*effect)(void*, const GlockResources&, const OwnerBinding&,
        CBasePlayer&, CBasePlayerWeapon&, const guest::cof::server::GlockEvent&) noexcept {};
    std::uint32_t effects{};
    // Original pickup suppression is independent from quietAudio. An explicit
    // host policy must supply it; it is never inferred from an unrelated field.
    bool pickupSuppressed{};
    P345RuntimeHost p345{};
    bool (*weaponCurrent)(void*,CBasePlayer&,int hostId) noexcept {};
    RevolverRuntimeHost revolver{};
    VP70RuntimeHost vp70{};
    TMPRuntimeHost tmp{};
    MP5RuntimeHost mp5{};
    M16RuntimeHost m16{};
};

struct GlockPlayerPreparation
{
    NativeOwnerMode mode;
    NativeOwnerRules rules;
    NativeOwnerPersistentState persistent;
    guest::cof::server::PistolAnimationRig rig;
    std::span<const std::byte> validatedModel;
    guest::cof::PlayerActionLabelCompare compare;
    guest::cof::PlayerModelExtent extent{guest::cof::PlayerModelExtent::CompleteFile};
    std::optional<NativeOwnerRules> p345Rules;
    struct RevolverPreparation { NativeOwnerRules rules; RevolverOwnerState state; };
    std::optional<RevolverPreparation> revolver;
    struct VP70Preparation { NativeOwnerRules rules; bool firstDrawn; };
    std::optional<VP70Preparation> vp70;
    struct TMPPreparation { NativeOwnerRules rules; bool firstDrawn; };
    std::optional<TMPPreparation> tmp;
    std::optional<NativeOwnerRules> mp5Rules;
    std::optional<NativeOwnerRules> m16Rules;
};

class GlockRuntime final
{
public:
    explicit GlockRuntime(GlockRuntimeLimits);
    // The owner must outlive all native Glock entities. Destruction without
    // EngineEntitiesReleased is a host lifecycle error, not an implicit release.
    ~GlockRuntime();
    GlockRuntime(const GlockRuntime&) = delete;
    GlockRuntime& operator=(const GlockRuntime&) = delete;
    GlockRuntime(GlockRuntime&&) = delete;
    GlockRuntime& operator=(GlockRuntime&&) = delete;

    // Call during native map precache, before W_Precache creates the weapon.
    // Epoch is runtime-owned, strictly increasing and never reused.
    ResourceResult BeginMap(const GlockRuntimeHost&) noexcept;
    void EndMap() noexcept;
    // Only after actual old-map private entities and callbacks were released.
    // ServerDeactivate alone does NOT meet this precondition.
    bool EngineEntitiesReleased(std::uint64_t epoch) noexcept;
    std::uint64_t MapEpoch() const noexcept;
    // Also identifies a failed BeginMap lease so its actual release can be
    // acknowledged; zero means no map generation has ever been issued.
    std::uint64_t LastIssuedMapEpoch() const noexcept;
    const GlockResources* Resources() const noexcept;
    const P345Resources* PreparedP345Resources() const noexcept;
    const RevolverResources* PreparedRevolverResources() const noexcept;
    const VP70Resources* PreparedVP70Resources() const noexcept;
    const TMPResources* PreparedTMPResources() const noexcept;
    const MP5Resources* PreparedMP5Resources() const noexcept;
    const M16Resources* PreparedM16Resources() const noexcept;

    // Spawn means successful real native initialization, including final model.
    // Model change preserves explicit owner state but revokes old weapon leases.
    bool PlayerSpawned(CBasePlayer&, const GlockPlayerPreparation&, std::string&);
    bool PlayerModelChanged(CBasePlayer&, const GlockPlayerPreparation&, std::string&);
    void PlayerModelChanging(CBasePlayer&) noexcept;
    void PlayerDied(CBasePlayer&) noexcept;
    void PlayerDisconnected(CBasePlayer&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::GlockBackends&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::P345Backends&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::RevolverBackends&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::VP70Backends&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::TMPBackends&) noexcept;
    bool MP5Backend(CBasePlayer&, guest::cof::server::TMPBackends&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::M16Backends&) noexcept;
    bool FirstDraw(const OwnerBinding&, bool) noexcept;
    bool Light(const OwnerBinding&, bool) noexcept;
    bool PersistentState(CBasePlayer&, NativeOwnerPersistentState&) noexcept;
    bool PersistentState(CBasePlayer&, RevolverOwnerState&) noexcept;
    bool PersistentVP70FirstDraw(CBasePlayer&, bool&) noexcept;
    bool PersistentTMPFirstDraw(CBasePlayer&, bool&) noexcept;
    bool PreserveRevolverReload(CBasePlayer&,int playerAnimation,int& desiredSequence) noexcept;
    bool BeginPreThink(CBasePlayer&) noexcept;
    bool BeginInput(CBasePlayer&) noexcept;
    bool AcceptedJump(CBasePlayer&) noexcept;
    bool AfterMovement(CBasePlayer&) noexcept;
    bool EndPostThink(CBasePlayer&) noexcept;
    bool CancelFrame(CBasePlayer&) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
