// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <iosfwd>

namespace csol::client
{
void InitializeCamera();
void ResetCamera();
void CameraThink();
int CameraIsThirdPerson();
void CameraOffsets(float* offsets);
void CameraRefdef(ref_params_t* params);
void CameraDiagnostics(std::ostream& out);
}
