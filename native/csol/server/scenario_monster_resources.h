// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Every pooled monster starts with this model before receiving its type's
// appearance. The map resource gate must check it even if no type uses it.
inline constexpr char CSOLScenarioInitialMonsterModel[] =
    "models/player/zombi_origin/zombi_origin.mdl";
