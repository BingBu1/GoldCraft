// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace csol
{
// Prepared per-model material rows replace the global table for this model.
// Keep the original model name and animation files; only texture paths move.
inline std::string ModelMaterialSource(const std::string& model)
{
    return model.substr(0, model.size() - 4) + ".mat.json";
}
}
