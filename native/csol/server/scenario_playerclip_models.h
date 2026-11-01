// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct model_s;
// The implementation is compiled independently of ReGameDLL's older CSSDK
// ReHLDS headers. Call only while the owning map's entity access is current.
model_s* CSOL_ScenarioBrushModel(int modelIndex);
