// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "material_state.hpp"
#include <cstddef>
#include <functional>
#include <string_view>
namespace csol
{
using ResourceReader=std::function<std::vector<std::byte>(const std::string&)>;
using ResourceExists=std::function<bool(const std::string&)>;
struct ModelRedirect { std::string Requested, Model, TexturePrefix, Configuration; };
std::vector<ModelRedirect> ParseWeaponRedirects(std::string_view json,const std::string& configuration);
struct ModelSource
{
    std::string Requested, PhysicalName, File, TexturePrefix, Configuration;
    bool Redirected{};
};
std::string ModelSourceDescriptor(const std::string& model);
std::string SerializeModelSource(const ModelSource& source);
ModelSource ResolveModelSource(const std::string& model,const ResourceReader& read,const ResourceExists& exists,
    const std::vector<ModelRedirect>& configured={});
struct TextureSource
{
    std::string Name, Resource, Descriptor;
    std::vector<std::string> Probes;
    MaterialPolicy Policy;
    bool External{}, EmbeddedFallback{};
};
TextureSource ResolveTextureSource(const ModelSource& model,const std::string& original,
    const std::string& replacement,const ResourceExists& exists);
}
