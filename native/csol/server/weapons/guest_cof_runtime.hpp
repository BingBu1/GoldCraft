// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_owner_runtime.hpp"
#include "guest_cof_resources.hpp"
#include "guest_cof_p345_owner.hpp"
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

    // Spawn means successful real native initialization, including final model.
    // Model change preserves explicit owner state but revokes old weapon leases.
    bool PlayerSpawned(CBasePlayer&, const GlockPlayerPreparation&, std::string&);
    bool PlayerModelChanged(CBasePlayer&, const GlockPlayerPreparation&, std::string&);
    void PlayerDied(CBasePlayer&) noexcept;
    void PlayerDisconnected(CBasePlayer&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::GlockBackends&) noexcept;
    bool Backend(CBasePlayer&, guest::cof::server::P345Backends&) noexcept;
    bool FirstDraw(const OwnerBinding&, bool) noexcept;
    bool Light(const OwnerBinding&, bool) noexcept;
    bool PersistentState(CBasePlayer&, NativeOwnerPersistentState&) noexcept;
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
