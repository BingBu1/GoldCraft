// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace csol {
std::string StudioTextureModel(const std::string& model);
// Assemble a rendering copy only. Original skeleton/geometry/animation offsets
// stay intact; all texture and skin references come from the decoded T.mdl.
std::vector<std::byte> AppendStudioTextures(std::span<const std::byte> model,
    std::span<const std::byte> textures);
}
