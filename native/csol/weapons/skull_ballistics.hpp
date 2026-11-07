// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::skull_ballistics
{
inline constexpr int Original556AZ = 19;
inline constexpr int PenetrationPower = 45;
inline constexpr float PenetrationDistance = 8000;

// MP39f962f3 1141F960 converts the current hit damage to float. The store
// at1141F976 writes that float to original ESP+44 while arguments occupy
// 0x24 bytes. After TraceAttack returns,1141F9AC reads the float there.
// Keep the float multiply/truncation rather than an integer percentage.
inline int EntityContinuationDamage(int hitDamage)
{
    return static_cast<int>(static_cast<float>(hitDamage) * .9f);
}
}
