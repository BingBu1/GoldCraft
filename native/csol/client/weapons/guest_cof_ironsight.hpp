// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

struct ref_params_s;

namespace csol::client
{
// All calls belong to the client owner thread. Failure leaves the adapter inert.
bool InitializeGuestCofIronsight();
bool GuestCofIronsightReady() noexcept;
void ShutdownGuestCofIronsight() noexcept;
void ResetGuestCofIronsight() noexcept;
// Supply only the current authenticated CoF presentation state; CurWeapon alone
// is not authority. Release/reset immediately on ownership or session loss.
void SetGuestCofIronsight(bool authenticatedGuest, bool aimed) noexcept;

// Enclose only the downstream V_CalcRefdef invocation. The scope preserves
// nesting and excludes remote/spectator, paused and intermission views.
class GuestCofIronsightViewScope
{
public:
    explicit GuestCofIronsightViewScope(const ref_params_s&) noexcept;
    ~GuestCofIronsightViewScope();
    GuestCofIronsightViewScope(const GuestCofIronsightViewScope&) = delete;
    GuestCofIronsightViewScope& operator=(const GuestCofIronsightViewScope&) = delete;
private:
    bool previous_{};
    bool entered_{};
};
}
