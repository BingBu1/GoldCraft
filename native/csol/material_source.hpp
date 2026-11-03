// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace csol
{
// Prepared per-model material rows replace the global table for this model.
// Keep the original model name and animation files; only texture paths move.
inline std::string ModelMaterialSource(const std::string& model)
{
    // The native client rejects download names containing more than one dot.
    return model.substr(0, model.size() - 4) + "_mat.json";
}

template<class Exists>
std::string ResolveModelMaterialSource(const std::string& model, const Exists& exists)
{
    auto path = ModelMaterialSource(model);
    if (exists(path)) return path;
    auto legacy = model.substr(0, model.size() - 4) + ".mat.json";
    return exists(legacy) ? legacy : path;
}
}
