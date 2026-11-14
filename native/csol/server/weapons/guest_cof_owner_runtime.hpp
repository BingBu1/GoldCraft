// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_owner.hpp"
#include <array>
#include <span>
#include <string>

namespace csol::server::cof
{
class GlockOwnerRuntime;

// Store this VALUE in the actual weapon/effect context. A naked Slot pointer
// would silently acquire a new owner's authority when the fixed slot is reused.
struct OwnerBinding
{
    GlockOwnerRuntime* runtime{};
    std::uint64_t generation{}, map{}, life{}, model{};
    unsigned slot{};
    int serial{};
};

struct OwnerBackendView
{
    OwnerBinding binding{};
    const guest::cof::server::PlayerAnimationAdapter* animation{};
    guest::cof::PlayerModelGeneration animationGeneration{};
};

struct OwnerRuntimeAuthority
{
    void* context{};
    // Authenticate actual map/life/model-content and every named mode invariant.
    // Called on the authoritative game thread. Revocation/reentry is supported.
    bool (*current)(void*, CBasePlayer&, std::uint64_t map,
        std::uint64_t life, std::uint64_t model, const NativeOwnerMode&) noexcept {};
};

struct OwnerPreparation
{
    unsigned slot;
    std::uint64_t map, life, modelGeneration;
    NativeOwnerMode mode;
    NativeOwnerRules rules;
    NativeOwnerPersistentState persistent;
    OwnerRuntimeAuthority authority;
    guest::cof::server::PistolAnimationRig rig;
    // Fully validated actual GET_MODEL_PTR memory, only during this cold call.
    std::span<const std::byte> validatedModel;
    guest::cof::PlayerActionLabelCompare compare;
    guest::cof::PlayerModelExtent extent{guest::cof::PlayerModelExtent::CompleteFile};
};

class GlockOwnerRuntime final
{
public:
    static constexpr unsigned SlotCount = 33;
    GlockOwnerRuntime() noexcept;
    GlockOwnerRuntime(const GlockOwnerRuntime&) = delete;
    GlockOwnerRuntime& operator=(const GlockOwnerRuntime&) = delete;
    GlockOwnerRuntime(GlockOwnerRuntime&&) = delete;
    GlockOwnerRuntime& operator=(GlockOwnerRuntime&&) = delete;

    bool BeginMap(std::uint64_t epoch) noexcept;
    void EndMap() noexcept;
    // Cold model/life boundary. Failure invalidates the old slot. Persistent
    // state must come from the actual caller; no cross-life inference here.
    bool Prepare(CBasePlayer&, const OwnerPreparation&, OwnerBackendView&, std::string& error);
    bool Backend(unsigned slot, CBasePlayer&, OwnerBackendView&) noexcept;
    bool Current(const OwnerBinding&) const noexcept;
    bool Read(const OwnerBinding&, guest::cof::server::GlockOwnerFrame&) const noexcept;
    bool FirstDraw(const OwnerBinding&, bool) noexcept;
    bool Light(const OwnerBinding&, bool) noexcept;
    bool PersistentState(const OwnerBinding&, NativeOwnerPersistentState&) const noexcept;

    // Real native callbacks call these; they never prepare/parse a model.
    bool BeginPreThink(unsigned slot, CBasePlayer&) noexcept;
    bool BeginInput(unsigned slot, CBasePlayer&) noexcept;
    bool AcceptedJump(unsigned slot, CBasePlayer&) noexcept;
    bool AfterMovement(unsigned slot, CBasePlayer&) noexcept;
    bool EndPostThink(unsigned slot, CBasePlayer&) noexcept;
    bool CancelFrame(unsigned slot, CBasePlayer&) noexcept;
    // Death, disconnect, role/model/content change. Also clears actual carried
    // Glock delayed callbacks, without invoking native drop or resetting CS guns.
    void Invalidate(unsigned slot, const CBasePlayer* expected = nullptr) noexcept;
    void ModelChanging(unsigned slot, CBasePlayer&) noexcept;

    // Convenience only when opaque points to an immutable OwnerBinding value.
    static bool BackendCurrent(void* opaque) noexcept;
    static bool BackendFrame(void* opaque, guest::cof::server::GlockOwnerFrame&) noexcept;

private:
    struct Slot
    {
        GlockOwnerRuntime* runtime{};
        CBasePlayer* player{};
        edict_t* entity{};
        unsigned index{};
        int serial{}, modelIndex{}, modelString{};
        std::uint64_t generation{}, map{}, life{}, model{};
        NativeOwnerMode mode{};
        OwnerRuntimeAuthority authority{};
        GlockOwner owner;
        guest::cof::server::PlayerAnimationAdapter animations;
        bool ready{};
    };
    bool Matches(const OwnerBinding&) const noexcept;
    bool Identity(const Slot&) const noexcept;
    bool AuthorityCurrent(const Slot&) const noexcept;
    static bool OwnerCurrent(void*, CBasePlayer&, std::uint64_t, std::uint64_t) noexcept;
    OwnerBinding Binding(const Slot&) const noexcept;
    Slot* Ready(unsigned, CBasePlayer&) noexcept;
    void Clear(Slot&, bool retireInventory = true) noexcept;
    bool AdvanceInventory(Slot&) noexcept;
    std::array<Slot, SlotCount> slots_{};
    std::uint64_t map_{};
};
}
