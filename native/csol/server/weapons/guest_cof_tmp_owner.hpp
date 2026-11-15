// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/tmp_weapon.hpp"

namespace csol::server::cof
{
class TMPOwnerView;
struct TMPOwnerBinding
{
    const TMPOwnerView* view{};
    std::uint64_t generation{};
};
struct TMPOwnerBackendView
{
    TMPOwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
};

// Borrows canonical native movement/input; TMP rules and original first-draw
// array entry remain independent. The retained map owns this stable storage.
class TMPOwnerView final
{
public:
    TMPOwnerView()=default;
    TMPOwnerView(const TMPOwnerView&)=delete;
    TMPOwnerView& operator=(const TMPOwnerView&)=delete;
    bool Prepare(CBasePlayer&,const OwnerBackendView&,NativeOwnerRules,bool firstDrawn,
        guest::cof::server::PistolAnimationRig,std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare,TMPOwnerBackendView&,std::string&,
        guest::cof::PlayerModelExtent=guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(TMPOwnerBinding) const noexcept;
    bool Read(TMPOwnerBinding,guest::cof::server::TMPOwnerFrame&) const noexcept;
    bool FirstDraw(TMPOwnerBinding) noexcept;
    bool PersistentFirstDraw(TMPOwnerBinding,bool&) const noexcept;
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*,guest::cof::server::TMPOwnerFrame&) noexcept;
private:
    bool Local(TMPOwnerBinding) const noexcept;
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
