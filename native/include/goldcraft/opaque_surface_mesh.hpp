// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <interface.h>
#include <cstdint>

struct model_s;
struct OpaqueSurfaceRange { std::uint32_t first, count; };
struct OpaqueSurfaceBuffers { std::uint32_t vertex, index, indexCount; };

// Owner-thread map query. The host owns both buffers until map retirement.
// Positions, diffuse UVs and lightmap UVs use seven floats per vertex. Each
// face range addresses the host's unsigned, 32-bit GL_TRIANGLES index array.
class IMetaRendererOpaqueMesh : public IBaseInterface
{
public:
    virtual bool Query(model_s* world, OpaqueSurfaceBuffers& buffers,
                       OpaqueSurfaceRange* ranges, std::uint32_t surfaceCount) const noexcept = 0;
};
inline constexpr char MetaRendererOpaqueMeshVersion[] = "MetaRendererOpaqueMesh_API_001";
