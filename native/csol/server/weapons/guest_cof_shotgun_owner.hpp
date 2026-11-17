// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/shotgun_weapon.hpp"
#include "native/guest/cof/server/player_action_runtime.hpp"

namespace csol::server::cof
{
struct ShotgunOwnerState { std::optional<float> reloadUntil; };
class ShotgunOwnerView;
struct ShotgunOwnerBinding
{
    const ShotgunOwnerView* view{};
    std::uint64_t generation{};
};
struct ShotgunOwnerBackendView
{
    ShotgunOwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
    guest::cof::server::PlayerThinkSlot* playerThink{};
};

// Canonical native input and the current CSOL action life remain shared across
// weapons. The optional reload clock must be explicitly preserved by the host
// at a same-life model change; fresh lives start from their own state.
class ShotgunOwnerView final
{
public:
    ShotgunOwnerView() = default;
    ShotgunOwnerView(const ShotgunOwnerView&) = delete;
    ShotgunOwnerView& operator=(const ShotgunOwnerView&) = delete;
    bool Prepare(CBasePlayer&, const OwnerBackendView&, NativeOwnerRules,
        guest::cof::server::PlayerActionBinding, ShotgunOwnerState,
        guest::cof::server::PistolAnimationRig, std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare, ShotgunOwnerBackendView&, std::string&,
        guest::cof::PlayerModelExtent = guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(ShotgunOwnerBinding) const noexcept;
    bool Read(ShotgunOwnerBinding, guest::cof::server::ShotgunOwnerFrame&) const noexcept;
    bool PersistentState(ShotgunOwnerBinding, ShotgunOwnerState&) const noexcept;
    bool ReloadStage(ShotgunOwnerBinding, const guest::cof::server::ShotgunEvent&) noexcept;
    std::optional<int> ReloadSequence(ShotgunOwnerBinding, int playerAnimation) const noexcept;
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*, guest::cof::server::ShotgunOwnerFrame&) noexcept;

private:
    bool Local(ShotgunOwnerBinding) const noexcept;
    OwnerBinding canonical_{};
    NativeOwnerRules rules_{};
    guest::cof::server::PlayerActionBinding actions_{};
    ShotgunOwnerState state_{};
    guest::cof::server::PlayerAnimationAdapter animation_;
    CBasePlayer* player_{};
    edict_t* entity_{};
    std::uint64_t generation_{1};
    int serial_{}, modelIndex_{}, spawn_{};
    bool ready_{}, preparing_{};
};
}
