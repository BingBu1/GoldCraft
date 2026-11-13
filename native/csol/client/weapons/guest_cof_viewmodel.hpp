// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct cl_entity_s;
struct cvar_s;
namespace csol::client {
bool InitializeGuestCofViewmodel() noexcept;
bool GuestCofViewmodelReady() noexcept;
void ShutdownGuestCofViewmodel() noexcept;
class GuestCofViewmodelScope final {
    ::cvar_s* handedness_{};
    float savedHand_{};
    unsigned char *knife_{},*elite_{};
    unsigned char savedKnife_{},savedElite_{};
public:
    GuestCofViewmodelScope(const cl_entity_s*,int flags) noexcept;
    ~GuestCofViewmodelScope();
    GuestCofViewmodelScope(const GuestCofViewmodelScope&)=delete;
    GuestCofViewmodelScope& operator=(const GuestCofViewmodelScope&)=delete;
};
}
