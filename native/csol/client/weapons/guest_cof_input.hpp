// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
struct usercmd_s;
namespace csol::client
{
// Namespaced host commands; installation never changes native key bindings.
bool InitializeGuestCofInput() noexcept;
void ResetGuestCofInput() noexcept;
void ShutdownGuestCofInput() noexcept;
bool GuestCofInputReady() noexcept;
void ApplyGuestCofAimDefault(std::uint64_t connection) noexcept;
void GuestCofCreateMove(usercmd_s*, int active, bool admitted) noexcept;
}
