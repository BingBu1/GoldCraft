// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "native/guest/cof/server/glock_weapon.hpp"
#include "native/guest/cof/server/owner_state.hpp"
#include <cstdint>

namespace csol::server::cof
{
enum class OwnerMovement : unsigned char { NativeCounterStrike, CompleteCryOfFear };

// These are enforced host capabilities, not discovered CoF defaults. The
// authority must revoke this binding before any capability ceases to hold.
struct NativeOwnerMode
{
    OwnerMovement movement;
    bool freeActivity;
    bool phoneUnavailable;
    bool substanceFree;
    bool finiteAmmo;
    bool firstPerson;
};

struct NativeOwnerRules
{
    bool toggleAim;
    int bulletDamage;
    int meleeDamage;
    int reserveLimit; // Original MP45; original non-MP/metadata150.
};

struct NativeOwnerPersistentState
{
    bool firstDrawn;
    bool lightOn;
};

struct NativeOwnerAuthority
{
    void* context{};
    // Must authenticate the session, map and native life AND the named mode
    // invariants above. This is called before reading the owner, and again
    // after callbacks. No client-supplied mode or generation is authoritative.
    bool (*current)(void*, CBasePlayer&, std::uint64_t map,
        std::uint64_t life) noexcept {};
    std::uint64_t map{};
    std::uint64_t life{};
};

// One instance per authoritative player life. Storage belongs to the server
// owner, not the selected weapon, so weapon switching cannot reset jump/edges.
// This adapter deliberately retains native CS movement. It does not implement
// the original CoF stamina/stairs/sprint graph or its audio/FOV side effects.
class GlockOwner final
{
public:
    bool Bind(CBasePlayer&, NativeOwnerAuthority, NativeOwnerMode,
        NativeOwnerRules, NativeOwnerPersistentState) noexcept;
    void Invalidate() noexcept;
    bool Current() const noexcept;

    // Before guest AdvanceOwnerFrame/spread, before current-frame edges/Jump.
    bool BeginPreThink() noexcept;
    // After spread, before native Jump. Computes original edges from actual
    // server buttons. Native input remapping, if any, must precede this call.
    bool BeginInput() noexcept;
    // Call INSIDE actual native Jump, only after its real acceptance gates.
    // A pre/post hook that only observes held IN_JUMP is insufficient.
    bool AcceptedNativeJump() noexcept;
    // After actual PM_Move, before the native player's ItemPostFrame.
    bool AfterMovement() noexcept;
    // At PostThink tail, after all weapon input, alongside native button-last.
    bool EndPostThink() noexcept;
    // Native callback cancellation only: leave jump/persistent state intact.
    // No original gameplay step is synthesized for an aborted host frame.
    bool CancelFrame() noexcept;

    bool Read(guest::cof::server::GlockOwnerFrame&) const noexcept;
    bool SetFirstDrawn(bool) noexcept;
    bool SetLight(bool) noexcept;
    bool PersistentState(NativeOwnerPersistentState&) const noexcept;

private:
    enum class Phase : unsigned char { Outside, Spread, Input, Weapons };
    bool LocalCurrent() const noexcept;
    bool ModeSupported() const noexcept;
    std::uint32_t OriginalFlags() const noexcept;
    CBasePlayer* player_{};
    edict_t* entity_{};
    int serial_{};
    NativeOwnerAuthority authority_{};
    NativeOwnerMode mode_{};
    NativeOwnerRules rules_{};
    NativeOwnerPersistentState persistent_{};
    guest::cof::OwnerJumpState jump_{};
    guest::cof::OwnerButtonEdges edges_{};
    std::uint32_t lastButtons_{};
    std::uint64_t binding_{};
    Phase phase_{Phase::Outside};
};
}
