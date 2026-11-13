// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/p345_weapon.hpp"

namespace csol::server::cof
{
class P345OwnerView;
struct P345OwnerBinding
{
    const P345OwnerView* view{};
    std::uint64_t generation{};
};
struct P345OwnerBackendView
{
    P345OwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
};

// Borrows the one canonical native player lifecycle/input clock. Only P345's
// explicit weapon rules and cold animation binding are independent.
// Keep this object at a stable retained-map address until every binding/callback
// is retired; Invalidate revokes keys, it does not own external callback storage.
class P345OwnerView final
{
public:
    P345OwnerView()=default;
    P345OwnerView(const P345OwnerView&)=delete;
    P345OwnerView& operator=(const P345OwnerView&)=delete;
    bool Prepare(CBasePlayer&,const OwnerBackendView&,NativeOwnerRules,
        guest::cof::server::PistolAnimationRig,std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare,P345OwnerBackendView&,std::string&,
        guest::cof::PlayerModelExtent=guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(P345OwnerBinding) const noexcept;
    bool Read(P345OwnerBinding,guest::cof::server::P345OwnerFrame&) const noexcept;
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*,guest::cof::server::P345OwnerFrame&) noexcept;
private:
    bool Local(P345OwnerBinding) const noexcept;
    OwnerBinding canonical_{};
    NativeOwnerRules rules_{};
    guest::cof::server::PlayerAnimationAdapter animation_;
    CBasePlayer* player_{};
    edict_t* entity_{};
    std::uint64_t generation_{1};
    int serial_{},modelIndex_{},spawn_{};
    bool ready_{};
};
}
