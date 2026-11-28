// Perspective-correct, two-sided visibility buffer for offline asset baking.
#include <algorithm>
#include <cmath>
#include <cstdint>

extern "C" __declspec(dllexport) void rasterize(
    const float* points, const int32_t* faces, int count, int width, int height,
    int32_t* primitive, float* weights, float* depth)
{
    for (int face = 0; face < count; ++face) {
        auto a = points + 3 * faces[3 * face];
        auto b = points + 3 * faces[3 * face + 1];
        auto c = points + 3 * faces[3 * face + 2];
        if (std::min({a[2], b[2], c[2]}) <= 0.001f) continue;
        const float bx = b[0] - a[0], by = b[1] - a[1];
        const float cx = c[0] - a[0], cy = c[1] - a[1];
        const float det = bx * cy - by * cx;
        if (std::abs(det) < 1e-8f) continue;
        const int x0 = std::max(0, int(std::ceil(std::min({a[0], b[0], c[0]}))));
        const int x1 = std::min(width-1, int(std::floor(std::max({a[0], b[0], c[0]}))));
        const int y0 = std::max(0, int(std::ceil(std::min({a[1], b[1], c[1]}))));
        const int y1 = std::min(height-1, int(std::floor(std::max({a[1], b[1], c[1]}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float u = ((x-a[0])*cy - (y-a[1])*cx)/det;
                const float v = ((y-a[1])*bx - (x-a[0])*by)/det;
                if (u < -1e-5f || v < -1e-5f || u+v > 1.00001f) continue;
                const float wa = (1-u-v)/a[2], wb = u/b[2], wc = v/c[2];
                const float z = 1.f/(wa+wb+wc);
                const int i = y*width+x;
                if (z >= depth[i]) continue;
                primitive[i] = face;
                depth[i] = z;
                weights[3*i] = wa*z;
                weights[3*i+1] = wb*z;
                weights[3*i+2] = wc*z;
            }
        }
    }
}
