// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

struct client_data_s;
struct cl_entity_s;
struct GoldCraftConnectionAPI;

namespace csol::client
{
void InitializeGuestCof();
void InitializeGuestCof(const GoldCraftConnectionAPI* connection);
bool GuestCofConnectionReady() noexcept;
void ShutdownGuestCof();
void ResetGuestCof();
void GuestCofVideoInit();
// Actual HUD_UpdateClientData wrapper; preserves downstream result and applies
// the authenticated CoF state after the native client has filled its output.
int GuestCofUpdateClientData(client_data_s* data, float time);
// Runs before native Studio bone setup; authority and HUD identity must agree.
bool GuestCofPrepareStudioModel(cl_entity_s*, int hostWeaponId) noexcept;
}
