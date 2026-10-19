#pragma once
#include "world_carving.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace goldcraft::carving {
// Exact union simplification for collision and visibility. Equal cross sections
// can merge only when they touch or overlap; no epsilon fills a gap. The
// authority's individual records, revisions and render material owners remain
// unchanged. Bounded passes may leave redundant boxes, never alter the union.
template<class Consume>
inline std::vector<Box> compact_cuts(std::span<const Box> input, Consume consume) {
    if (input.size() > 65536) throw std::length_error("Cut union capacity");
    consume(input.size());
    std::vector<Box> boxes(input.begin(), input.end());
    for (const auto& b : boxes)
        for (int a = 0; a < 3; ++a)
            if (!std::isfinite(b.min[a]) || !std::isfinite(b.max[a]) ||
                b.min[a] >= b.max[a]) throw std::invalid_argument("Cut union bounds");
    for (int pass = 0; pass < 8; ++pass) {
        const auto previous = boxes.size();
        for (int a = 0; a < 3; ++a) {
            consume(boxes.size() * (std::bit_width(boxes.size()) + 1));
            const auto u = (a + 1) % 3, v = (a + 2) % 3;
            std::ranges::sort(boxes, [=](const Box& x, const Box& y) {
                return std::tie(x.min[u], x.max[u], x.min[v], x.max[v], x.min[a], x.max[a]) <
                    std::tie(y.min[u], y.max[u], y.min[v], y.max[v], y.min[a], y.max[a]);
            });
            std::size_t count = 0;
            for (const auto b : boxes) {
                if (count) {
                    auto& last = boxes[count - 1];
                    if (last.min[u] == b.min[u] && last.max[u] == b.max[u] &&
                        last.min[v] == b.min[v] && last.max[v] == b.max[v] &&
                        b.min[a] <= last.max[a]) {
                        last.max[a] = std::max(last.max[a], b.max[a]);
                        continue;
                    }
                }
                boxes[count++] = b;
            }
            boxes.resize(count);
        }
        if (boxes.size() == previous) break;
    }
    return boxes;
}
inline std::vector<Box> compact_cuts(std::span<const Box> input) {
    return compact_cuts(input, [](std::size_t) {});
}
} // namespace goldcraft::carving
