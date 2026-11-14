// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/revolver_weapon.hpp"
#include "native/guest/cof/revolver_lifecycle.hpp"
#include <optional>

namespace csol::server::cof
{
using RevolverOwnerState=guest::cof::RevolverPlayerState;
class RevolverOwnerView;
struct RevolverOwnerBinding
{
    const RevolverOwnerView* view{};
    std::uint64_t generation{};
};
struct RevolverOwnerBackendView
{
    RevolverOwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
};

// Borrows the one canonical native player lifecycle/input clock. Only Revolver's
// explicit weapon rules, cylinder state and cold animation binding are independent.
// Prepare requires an authenticated seed; it never invents a life/reset policy.
// Keep this object at a stable retained-map address until every binding/callback
// is retired; Invalidate revokes keys, it does not own external callback storage.
class RevolverOwnerView final
{
public:
    RevolverOwnerView()=default;
    RevolverOwnerView(const RevolverOwnerView&)=delete;
    RevolverOwnerView& operator=(const RevolverOwnerView&)=delete;
    bool Prepare(CBasePlayer&,const OwnerBackendView&,NativeOwnerRules,RevolverOwnerState,
        guest::cof::server::PistolAnimationRig,std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare,RevolverOwnerBackendView&,std::string&,
        guest::cof::PlayerModelExtent=guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(RevolverOwnerBinding) const noexcept;
    bool Read(RevolverOwnerBinding,guest::cof::server::RevolverOwnerFrame&) const noexcept;
    bool PersistentState(RevolverOwnerBinding,RevolverOwnerState&) const noexcept;
    bool FirstDraw(RevolverOwnerBinding,bool) noexcept;
    bool Cylinder(RevolverOwnerBinding,const guest::cof::server::RevolverEvent&) noexcept;
    bool ReloadStage(RevolverOwnerBinding,const guest::cof::server::RevolverEvent&) noexcept;
    std::optional<int> ReloadSequence(RevolverOwnerBinding,int playerAnimation) const noexcept;
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*,guest::cof::server::RevolverOwnerFrame&) noexcept;
private:
    bool Local(RevolverOwnerBinding) const noexcept;
    OwnerBinding canonical_{};
    NativeOwnerRules rules_{};
    RevolverOwnerState state_{};
    guest::cof::server::PlayerAnimationAdapter animation_;
    CBasePlayer* player_{};
    edict_t* entity_{};
    std::uint64_t generation_{1};
    int serial_{},modelIndex_{},spawn_{};
    bool ready_{};
};
}
