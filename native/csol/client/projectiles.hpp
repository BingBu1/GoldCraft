// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <iosfwd>

namespace csol::client
{
void InitializeProjectiles();
void ResetProjectiles();
int ProjectileAddEntity(int type, cl_entity_t* entity, const char* modelName);
void ProjectileDiagnostics(std::ostream& output);
}
