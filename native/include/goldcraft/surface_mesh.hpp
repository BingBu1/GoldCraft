#pragma once
#include "goldcraft/world_carving.hpp"
#include <cstdint>
#include <memory>

namespace goldcraft::surface {
// These are the existing Renderer brush buffer layouts, not a wire format.
struct Vertex {
    float pos[3]{}, texcoord[2]{}, lightmaptexcoord[2]{};
    bool operator==(const Vertex &) const = default;
};
struct Frame {
    float normal[3]{}, s_tangent[3]{}, t_tangent[3]{}, smoothnormal[3]{};
    bool operator==(const Frame &) const = default;
};
struct Instance {
    std::uint16_t packed_matId[2]{};
    std::uint8_t styles[4]{};
    float diffusescale = 0;
    bool operator==(const Instance &) const = default;
};
static_assert(sizeof(Vertex) == 28 && sizeof(Frame) == 48 && sizeof(Instance) == 12);
struct Range {
    std::uint32_t first = 0, count = 0;
    bool operator==(const Range &) const = default;
};
struct Face {
    Range forward, reverse, instances;
    std::uint32_t polygons = 0;
    bool editable = true;
    bool operator==(const Face &) const = default;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Frame> frames;
    std::vector<Instance> instances;
    std::vector<std::uint32_t> indices;
    std::vector<Face> faces;
    bool operator==(const Mesh &) const = default;
};
struct Selection {
    std::uint32_t first_face, face_count;
    carving::Box box; // Local to the selected model's faces.
};
struct Limits {
    std::size_t vertices = 2'097'152, indices = 8'388'608;
    std::size_t fragments = 1'048'576, operations = 67'108'864;
};
struct Prepared {
    std::shared_ptr<const Mesh> mesh;
    std::size_t changed_faces = 0, fragments = 0, operations = 0;
};
// Always derive from the immutable original, never repeatedly clip a prior
// edit. Preserve untouched vertices/attributes and every face's instance range.
// New vertices interpolate UV, lightmap and all TBN fields with the same
// weights. No live resource is changed on invalid input, work exhaustion or
// allocation failure. Empty/nonintersecting edits return the original shared
// resource. This prepares remaining surfaces only; cavity walls/PVS/shadows
// need their own coordinated preparation before gameplay may commit the edit
// revision.
Prepared prepare(std::shared_ptr<const Mesh> original, std::span<const Selection> cuts,
                 Limits limits = {});
} // namespace goldcraft::surface
