// SPDX-License-Identifier: GPL-3.0-or-later
// Control the module boundary without replacing the production HUD consumer.
#include "../../native/csol/client/runtime.hpp"
namespace fixture { HMODULE WINAPI VGuiModule(const char*); }
#define GetModuleHandleA fixture::VGuiModule
#include "../../native/csol/client/wall_upgrade_hud.cpp"
#undef GetModuleHandleA
