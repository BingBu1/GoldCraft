// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::k3 {
inline constexpr int Id = 197, ItemId = 525, Clip = 150, Reserve = 200;
inline constexpr float Interval = .1f, ReloadTime = 3.9f, DeployTime = .75f;
inline constexpr float FireIdle = 1.6f, DeployIdle = 1.5f, IdleTime = 20.f;
inline constexpr float EmptyInterval = .2f, Distance = 8192.f;
inline constexpr const char* Classname = "weapon_k3";
inline constexpr const char* EventPath = "events/k3.sc";
inline constexpr const char* ViewModel = "models/v_k3.mdl";
inline constexpr const char* ShellModel = "models/rshell.mdl";
inline constexpr const char* FireSound = "weapons/K3-1.wav";
enum Animation { Idle = 0, Shoot1 = 1, Shoot2 = 2, Reload = 3, Draw = 4 };
}
