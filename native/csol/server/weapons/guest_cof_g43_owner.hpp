// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/g43_weapon.hpp"
#include "native/guest/cof/server/player_action_runtime.hpp"

namespace csol::server::cof
{
class G43OwnerView;
struct G43OwnerBinding
{
    const G43OwnerView* view{};
    std::uint64_t generation{};
};
struct G43OwnerBackendView
{
    G43OwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
    guest::cof::server::PlayerThinkSlot* playerThink{};
};

// Canonical input follows the explicit native-CS migration profile. Stamina
// and delayed pushback come from a separately authenticated matching life.
class G43OwnerView final
{
public:
    G43OwnerView() = default;
    G43OwnerView(const G43OwnerView&) = delete;
    G43OwnerView& operator=(const G43OwnerView&) = delete;
    bool Prepare(CBasePlayer&, const OwnerBackendView&, NativeOwnerRules,
        guest::cof::server::PlayerActionBinding,
        guest::cof::server::PistolAnimationRig, std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare, G43OwnerBackendView&, std::string&,
        guest::cof::PlayerModelExtent = guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(G43OwnerBinding) const noexcept;
    bool Read(G43OwnerBinding, guest::cof::server::G43OwnerFrame&) const noexcept;
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*, guest::cof::server::G43OwnerFrame&) noexcept;

private:
    bool Local(G43OwnerBinding) const noexcept;
    OwnerBinding canonical_{};
    NativeOwnerRules rules_{};
    guest::cof::server::PlayerActionBinding actions_{};
    guest::cof::server::PlayerAnimationAdapter animation_;
    CBasePlayer* player_{};
    edict_t* entity_{};
    std::uint64_t generation_{1};
    int serial_{}, modelIndex_{}, spawn_{};
    bool ready_{}, preparing_{};
};
}
