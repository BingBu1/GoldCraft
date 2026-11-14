// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/vp70_weapon.hpp"

namespace csol::server::cof
{
class VP70OwnerView;
struct VP70OwnerBinding
{
    const VP70OwnerView* view{};
    std::uint64_t generation{};
};
struct VP70OwnerBackendView
{
    VP70OwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
};

// Borrows canonical native movement/input; VP70 rules and original first-draw
// array entry remain independent. The retained map owns this stable storage.
class VP70OwnerView final
{
public:
    VP70OwnerView()=default;
    VP70OwnerView(const VP70OwnerView&)=delete;
    VP70OwnerView& operator=(const VP70OwnerView&)=delete;
    bool Prepare(CBasePlayer&,const OwnerBackendView&,NativeOwnerRules,bool firstDrawn,
        guest::cof::server::PistolAnimationRig,std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare,VP70OwnerBackendView&,std::string&,
        guest::cof::PlayerModelExtent=guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(VP70OwnerBinding) const noexcept;
    bool Read(VP70OwnerBinding,guest::cof::server::VP70OwnerFrame&) const noexcept;
    bool FirstDraw(VP70OwnerBinding) noexcept;
    bool PersistentFirstDraw(VP70OwnerBinding,bool&) const noexcept;
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*,guest::cof::server::VP70OwnerFrame&) noexcept;
private:
    bool Local(VP70OwnerBinding) const noexcept;
    OwnerBinding canonical_{};
    NativeOwnerRules rules_{};
    guest::cof::server::PlayerAnimationAdapter animation_;
    CBasePlayer* player_{};
    edict_t* entity_{};
    std::uint64_t generation_{1};
    int serial_{},modelIndex_{},spawn_{};
    bool ready_{},firstDrawn_{};
};
}
