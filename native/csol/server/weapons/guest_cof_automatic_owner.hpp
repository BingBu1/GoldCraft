// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_owner_runtime.hpp"
#include "native/guest/cof/server/automatic_weapon.hpp"

namespace csol::server::cof
{
using guest::cof::server::LongGun;
template<LongGun Weapon> class AutomaticOwnerView;
template<LongGun Weapon> struct AutomaticOwnerBinding
{
    const AutomaticOwnerView<Weapon>* view{};
    std::uint64_t generation{};
};
template<LongGun Weapon> struct AutomaticOwnerBackendView
{
    AutomaticOwnerBinding<Weapon> binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
};

// Canonical input/life is shared; bindings, rules and animation types are not.
template<LongGun Weapon> class AutomaticOwnerView final
{
public:
    static_assert(Weapon == LongGun::TMP || Weapon == LongGun::MP5);
    using Binding = AutomaticOwnerBinding<Weapon>;
    using BackendView = AutomaticOwnerBackendView<Weapon>;
    AutomaticOwnerView() = default;
    AutomaticOwnerView(const AutomaticOwnerView&) = delete;
    AutomaticOwnerView& operator=(const AutomaticOwnerView&) = delete;
    bool Prepare(CBasePlayer&, const OwnerBackendView&, NativeOwnerRules, bool firstDrawn,
        guest::cof::server::PistolAnimationRig, std::span<const std::byte>,
        guest::cof::PlayerActionLabelCompare, BackendView&, std::string&,
        guest::cof::PlayerModelExtent = guest::cof::PlayerModelExtent::CompleteFile);
    void Invalidate() noexcept;
    bool Current(Binding) const noexcept;
    bool Read(Binding, guest::cof::server::TMPOwnerFrame&) const noexcept;
    bool FirstDraw(Binding) noexcept requires (Weapon == LongGun::TMP);
    bool PersistentFirstDraw(Binding, bool&) const noexcept requires (Weapon == LongGun::TMP);
    static bool BackendCurrent(void*) noexcept;
    static bool BackendFrame(void*, guest::cof::server::TMPOwnerFrame&) noexcept;
private:
    bool Local(Binding) const noexcept;
    OwnerBinding canonical_{};
    NativeOwnerRules rules_{};
    guest::cof::server::PlayerAnimationAdapter animation_;
    CBasePlayer* player_{};
    edict_t* entity_{};
    std::uint64_t generation_{1};
    int serial_{}, modelIndex_{}, spawn_{};
    bool ready_{}, firstDrawn_{};
};
using TMPOwnerView = AutomaticOwnerView<LongGun::TMP>;
using TMPOwnerBinding = AutomaticOwnerBinding<LongGun::TMP>;
using TMPOwnerBackendView = AutomaticOwnerBackendView<LongGun::TMP>;
using MP5OwnerView = AutomaticOwnerView<LongGun::MP5>;
using MP5OwnerBinding = AutomaticOwnerBinding<LongGun::MP5>;
using MP5OwnerBackendView = AutomaticOwnerBackendView<LongGun::MP5>;
}
