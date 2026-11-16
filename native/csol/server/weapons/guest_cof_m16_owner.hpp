// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/m16_weapon.hpp"
#include "native/guest/cof/server/player_action_runtime.hpp"

namespace csol::server::cof
{
class M16OwnerView;
struct M16OwnerBinding
{
    const M16OwnerView* view{};
    std::uint64_t generation{};
};
struct M16OwnerBackendView
{
    M16OwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
    guest::cof::server::PlayerThinkSlot* playerThink{};
};

// Canonical input follows the explicit native-CS migration profile. Stamina
// and delayed pushback come from a separately authenticated matching life.
class M16OwnerView final
{
public:
    M16OwnerView() = default;
    M16OwnerView(const M16OwnerView&) = delete;
    M16OwnerView& operator=(const M16OwnerView&) = delete;
    bool Prepare(CBasePlayer&, const OwnerBackendView&, NativeOwnerRules,
        guest::cof::server::PlayerActionBinding,
        guest::cof::server::PistolAnimationRig, std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare, M16OwnerBackendView&, std::string&,
        guest::cof::PlayerModelExtent = guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(M16OwnerBinding) const noexcept;
    bool Read(M16OwnerBinding, guest::cof::server::M16OwnerFrame&) const noexcept;
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*, guest::cof::server::M16OwnerFrame&) noexcept;

private:
    bool Local(M16OwnerBinding) const noexcept;
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
