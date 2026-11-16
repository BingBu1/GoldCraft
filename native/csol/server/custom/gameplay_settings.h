// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/custom/settings.hpp"
#include "../../scenario/custom/phase.hpp"

void CSOL_InitGameplaySettings();
void CSOL_PollGameplaySettings();
void CSOL_ShutdownGameplaySettings();
bool CSOL_CustomFeatureEnabled(csol::scenario::custom::Feature feature);
csol::scenario::custom::SnapshotPtr CSOL_CustomSettings();
bool CSOL_ReadTimeCycleControls(csol::scenario::custom::TimeCycleControls& output);
