// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "particle_runtime.hpp"
#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::particles
{
using Matrix = std::array<float, 16>;
using Plane = std::array<float, 4>;
using Frustum = std::array<Plane, 6>;

// Original client01DE1790. Both inputs are column-major API matrices.
inline Frustum GenerateFrustum(const Matrix& projection, const Matrix& model)
{
    Matrix clip;
    for (unsigned column = 0; column != 4; ++column)
        for (unsigned row = 0; row != 4; ++row)
        {
            float value = projection[row] * model[column * 4];
            value = value + model[column * 4 + 1] * projection[4 + row];
            value = value + model[column * 4 + 2] * projection[8 + row];
            value = value + model[column * 4 + 3] * projection[12 + row];
            clip[column * 4 + row] = value;
        }
    Frustum result;
    for (unsigned plane = 0; plane != 6; ++plane)
    {
        const unsigned row = plane / 2;
        const bool add = plane == 1 || plane == 2 || plane == 5;
        auto& out = result[plane];
        for (unsigned column = 0; column != 4; ++column)
            out[column] = add ? clip[column * 4 + 3] + clip[column * 4 + row] :
                                clip[column * 4 + 3] - clip[column * 4 + row];
        const float square = (out[0] * out[0] + out[1] * out[1]) + out[2] * out[2];
        const float length = static_cast<float>(std::sqrt(static_cast<double>(square)));
        // Preserve the original divisions, including a degenerate matrix.
        for (float& value : out) value = value / length;
    }
    return result;
}

// Only the actual supported drawFlags1C AABB/PVS path of01DE2790. Extent is
// size/5 sampled before its clock/PVS callbacks; equality is outside a plane.
inline bool FixedBoundsVisible(const Vec3& origin, float extent, const Frustum& planes, bool inPvs)
{
    const float x = origin[0] - extent, y = origin[1] - extent, z = origin[2] - extent;
    for (const auto& p : planes)
    {
        const float xm = x * p[0], ym = y * p[1], zm = z * p[2];
        const float low = xm + ym;
        if ((zm + low) + p[3] > 0.f) continue;
        const float xp = (origin[0] + extent) * p[0];
        const float right = xp + ym;
        if ((right + zm) + p[3] > 0.f) continue;
        const float yp = (origin[1] + extent) * p[1];
        const float upper = xm + yp;
        if ((upper + zm) + p[3] > 0.f) continue;
        const float upperRight = xp + yp;
        if ((upperRight + zm) + p[3] > 0.f) continue;
        const float zp = (origin[2] + extent) * p[2];
        if ((zp + low) + p[3] > 0.f) continue;
        if ((zp + right) + p[3] > 0.f) continue;
        if ((zp + upper) + p[3] > 0.f) continue;
        if (!((zp + upperRight) + p[3] > 0.f)) return false;
    }
    return inPvs;
}
}
