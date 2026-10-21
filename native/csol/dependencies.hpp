// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include "model_source.hpp"
namespace csol
{
// Returns actual relative files required by the supported weapon presentation.
// Missing required references fail before publishing an incomplete download list.
std::vector<std::string> WeaponDependencies(const std::string& model,const ResourceReader& read,const ResourceExists& exists,
    const std::vector<ModelRedirect>& configured={});
std::string ExternalTexture(const std::string& model,std::string texture,bool replacement,const ResourceExists& exists);
}
