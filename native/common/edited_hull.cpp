#include "goldcraft/edited_hull.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace goldcraft::carving {
namespace {
double dot(const Point &a, const Point &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Plane reverse(Plane p) {
    for (auto &n : p.normal)
        n = -n;
    p.distance = -p.distance;
    return p;
}
void valid(Point p) {
    for (auto v : p)
        if (!std::isfinite(v) || std::abs(v) > 1'000'000)
            throw std::invalid_argument("Edited hull coordinate");
}
int contents(HullView hull, Point point) {
    auto node = hull.root;
    while (node >= 0) {
        const auto &branch = hull.nodes[node];
        const auto &plane = hull.planes[branch.plane];
        node = branch.children[dot(point, plane.normal) >= plane.distance ? 0 : 1];
    }
    return node;
}
struct LeafSpan {
    CollisionSpan interval;
    int contents;
};
struct TraceScratch {
    bool busy = false;
    std::vector<LeafSpan> leaves;
    std::vector<CollisionSpan> native, original, remaining, removed, spans;
    std::vector<std::size_t> candidates;
    void trim() noexcept {
        // At most 256 KiB per cached depth (1 MiB per thread). A complex
        // one-off query is freed on return, including exceptional returns.
        constexpr std::size_t limit = 256 * 1024;
        std::size_t bytes = 0;
        const auto fits = [&](const auto &buffer) {
            if (buffer.capacity() > (limit - bytes) / sizeof(buffer[0]))
                return false;
            bytes += buffer.capacity() * sizeof(buffer[0]);
            return true;
        };
        if (fits(leaves) && fits(native) && fits(original) && fits(remaining) &&
            fits(removed) && fits(spans) && fits(candidates))
            return;
        std::vector<LeafSpan>().swap(leaves);
        for (auto *buffer : {&native, &original, &remaining, &removed, &spans})
            std::vector<CollisionSpan>().swap(*buffer);
        std::vector<std::size_t>().swap(candidates);
    }
};
class ScratchLease {
  public:
    ScratchLease() {
        // Never grow the pool itself: allocator callbacks can reenter before
        // an outer vector finishes growing. A slot is marked busy first.
        static thread_local std::array<TraceScratch, 4> pool;
        for (auto &slot : pool)
            if (!slot.busy) {
                scratch_ = &slot;
                break;
            }
        scratch_->busy = true;
    }
    ~ScratchLease() {
        scratch_->trim();
        scratch_->busy = false;
    }
    TraceScratch &get() { return *scratch_; }
    ScratchLease(const ScratchLease &) = delete;
    ScratchLease &operator=(const ScratchLease &) = delete;

  private:
    TraceScratch fallback_; // Arbitrarily deep nesting owns separate storage.
    TraceScratch *scratch_ = &fallback_;
};
void gather(HullView hull, std::int32_t node, const Point &start, const Point &delta,
            CollisionSpan interval, std::vector<LeafSpan> &output, std::size_t &budget) {
    if (!budget--)
        throw std::length_error("Edited hull trace budget");
    if (node < 0) {
        output.push_back({interval, node});
        return;
    }
    const auto &branch = hull.nodes[node];
    const auto &plane = hull.planes[branch.plane];
    const double distance = dot(start, plane.normal) - plane.distance,
                 speed = dot(delta, plane.normal);
    const double a = distance + speed * interval.begin, b = distance + speed * interval.end;
    if (a >= 0 && b >= 0) {
        gather(hull, branch.children[0], start, delta, interval, output, budget);
        return;
    }
    if (a < 0 && b < 0) {
        gather(hull, branch.children[1], start, delta, interval, output, budget);
        return;
    }
    const double t = std::clamp(-distance / speed, interval.begin, interval.end);
    const int near = speed > 0 ? 1 : 0;
    // The normal faces the starting side. The next span enters the far side.
    const Plane hit = near ? reverse(plane) : plane;
    if (t > interval.begin)
        gather(hull, branch.children[near], start, delta,
               {interval.begin, t, interval.enter, reverse(hit)}, output, budget);
    if (t < interval.end)
        gather(hull, branch.children[near ^ 1], start, delta,
               {t, interval.end, hit, interval.leave}, output, budget);
}
void subtract(std::span<const CollisionSpan> a, std::span<const CollisionSpan> b,
              std::vector<CollisionSpan> &output) {
    output.clear();
    std::size_t first = 0;
    for (auto part : a) {
        while (first < b.size() && b[first].end <= part.begin)
            ++first;
        for (auto i = first; i < b.size() && b[i].begin < part.end; ++i) {
            const auto &cut = b[i];
            if (cut.begin > part.begin)
                output.push_back({part.begin, cut.begin, part.enter, reverse(cut.enter)});
            if (cut.end >= part.end) {
                part.begin = part.end;
                break;
            }
            part.begin = cut.end;
            part.enter = reverse(cut.leave);
        }
        if (part.begin < part.end)
            output.push_back(part);
    }
}
} // namespace
EditedHull::EditedHull(HullView point_hull, HullView body_hull, Box body, std::span<const Box> cuts,
                       Limits limits)
    : planes_(body_hull.planes.begin(), body_hull.planes.end()),
      nodes_(body_hull.nodes.begin(), body_hull.nodes.end()), root_(body_hull.root),
      query_limit_(limits.operations) {
    operations_ = validate_hull(body_hull, limits);
    if (cuts.empty())
        throw std::invalid_argument("Edited hull needs cuts");
    Box affected = cuts.front();
    for (const auto &cut : cuts)
        for (int axis = 0; axis < 3; ++axis) {
            affected.min[axis] = std::min(affected.min[axis], cut.min[axis]);
            affected.max[axis] = std::max(affected.max[axis], cut.max[axis]);
        }
    Box region;
    for (int axis = 0; axis < 3; ++axis) {
        affected.min[axis] -= body.max[axis];
        affected.max[axis] -= body.min[axis];
        // All bodies that can touch a cut fit inside this support region. No
        // artificial regional boundary can remove a real contact outside it.
        region.min[axis] = affected.min[axis] + body.min[axis] - 1;
        region.max[axis] = affected.max[axis] + body.max[axis] + 1;
    }
    const auto budget = [&] { return Limits{limits.fragments, limits.operations - operations_}; };
    const auto old = carve_volume(point_hull, region, {}, budget());
    operations_ += old.operations;
    const auto next = carve_volume(point_hull, region, cuts, budget());
    operations_ += next.operations;
    original_ = expand_volume(old, body, budget());
    operations_ += original_.operations;
    remaining_ = expand_volume(next, body, budget());
    operations_ += remaining_.operations;
    original_index_ = index_volume(original_, budget());
    operations_ += original_index_.operations;
    remaining_index_ = index_volume(remaining_, budget());
    operations_ += remaining_index_.operations;
    if (cuts.size() > limits.fragments || cuts.size() > limits.operations - operations_)
        throw std::length_error("Edited hull index budget");
    operations_ += cuts.size();
    std::vector<Box> bounds;
    bounds.reserve(cuts.size());
    for (const auto &cut : cuts) {
        Box box;
        for (int axis = 0; axis < 3; ++axis) {
            box.min[axis] = cut.min[axis] - body.max[axis];
            box.max[axis] = cut.max[axis] - body.min[axis];
        }
        bounds.push_back(box);
    }
    affected_ = index_bounds(bounds, budget());
    operations_ += affected_.operations;
}
bool EditedHull::affects(Point start, Point end) const {
    return intersects(affected_, start, end);
}
int EditedHull::contents(Point point) const {
    valid(point);
    const int original = carving::contents(hull(), point);
    return original == -2 && affects(point, point) && contains(original_, original_index_, point) &&
                   !contains(remaining_, remaining_index_, point)
               ? -1
               : original;
}
HullTrace EditedHull::trace(Point start, Point end, double margin) const {
    valid(start);
    valid(end);
    Point delta{};
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = end[axis] - start[axis];
    ScratchLease lease;
    auto &scratch = lease.get();
    auto &leaves = scratch.leaves;
    leaves.clear();
    auto budget = query_limit_;
    gather(hull(), root_, start, delta, {0, 1, {}, {}}, leaves, budget);
    auto &native = scratch.native;
    native.clear();
    for (const auto &leaf : leaves)
        if (leaf.contents == -2)
            native.push_back(leaf.interval);
    auto &removed = scratch.removed;
    removed.clear();
    if (affects(start, end)) {
        volume_spans(original_, start, end, scratch.original, scratch.candidates, &original_index_);
        volume_spans(remaining_, start, end, scratch.remaining, scratch.candidates, &remaining_index_);
        subtract(scratch.original, scratch.remaining, removed);
    }
    auto &spans = scratch.spans;
    subtract(native, removed, spans);
    merge_spans_in_place(spans);
    HullTrace result;
    static_cast<VolumeTrace &>(result) =
        trace_spans(spans, start, end, contents(start) == -2, contents(end) == -2, margin);
    for (const auto &leaf : leaves) {
        if (leaf.interval.begin > result.fraction)
            break;
        if (leaf.contents == -1)
            result.in_open = true;
        else if (leaf.contents != -2 && leaf.contents != -15)
            result.in_water = true;
        else if (leaf.contents == -2) {
            for (const auto &hole : removed) {
                const double begin = std::max(leaf.interval.begin, hole.begin),
                             finish = std::min({leaf.interval.end, hole.end, result.fraction});
                if (begin < finish)
                    result.in_open = true;
            }
        }
    }
    return result;
}
} // namespace goldcraft::carving
