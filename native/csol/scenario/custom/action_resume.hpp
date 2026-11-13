// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace csol::scenario::custom
{
// Server-local generation; never a cross-process pointer or inventory snapshot.
struct ActionResumeToken
{
    std::uint64_t generation{};
    friend constexpr bool operator==(ActionResumeToken, ActionResumeToken) = default;
};
enum class ActionResumeResult : std::uint8_t { Applied, Rejected, Cancelled };

// The overlay owns the player input lock and releases it after Resume returns.
// A successful Suspend followed by a failed overlay acquisition must be undone
// with Resume. Cancel retires the interrupted action and its pending callbacks.
class ResumableWeaponAction
{
public:
    virtual ActionResumeResult SuspendWeaponAction(ActionResumeToken) noexcept = 0;
    virtual ActionResumeResult ResumeWeaponAction(ActionResumeToken) noexcept = 0;
    virtual void CancelWeaponAction(ActionResumeToken) noexcept = 0;
protected:
    // Borrowed capability; engine private-data lifecycle owns destruction.
    ~ResumableWeaponAction() = default;
};
}
