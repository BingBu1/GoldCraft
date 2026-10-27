// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace csol {
struct HandBoneBinding {
    std::string donor, target;
    // Donor bone-local coordinates -> target bone-local coordinates.
    std::array<float,12> transform{1,0,0,0,0,1,0,0,0,0,1,0};
};
struct HandAdapterOptions {
    std::vector<std::string> textures;
    std::vector<HandBoneBinding> bones;
};
struct HandBodygroup {
    int base{};
    // Original submodel -> visible arms: 1 left, 2 right, 3 both.
    std::vector<uint8_t> visibility;
};
struct AdaptedHands {
    std::vector<std::byte> bytes;
    std::string reason;
    int removedMeshes{}, mappedBones{};
    std::vector<HandBodygroup> bodygroups;
};
// Geometry-only conversion. The original skeleton, sequences, events,
// attachments, weapon meshes, skin families and texture payloads are retained.
// A rejected conversion returns no bytes and leaves both inputs untouched.
AdaptedHands AdaptViewmodelHands(std::span<const std::byte> target,
    std::span<const std::byte> donor, const HandAdapterOptions& options={});
}
