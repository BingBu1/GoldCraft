// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared by ordinary facing, movement feelers and melee. The table uses CSO's
// 256 entries with denominator255; this is not a continuous sin/cos query.
bool CSOL_MonsterHeading(float yaw, float& cosine, float& sine);
bool CSOL_MonsterFacingTarget(float yaw, float deltaX, float deltaY, bool& facing);
