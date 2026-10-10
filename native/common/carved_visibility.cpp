#include "goldcraft/carved_visibility.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace goldcraft::visibility {
namespace {
using carving::Box;
using carving::Point;
void consume(std::size_t &used, std::size_t limit, std::size_t count = 1) {
    if (used > limit || count > limit - used) throw std::length_error("Carved PVS work budget");
    used += count;
}
bool test(const Bits &bits, std::size_t index) noexcept {
    return index / 64 < bits.size() && (bits[index / 64] & (std::uint64_t{1} << (index % 64)));
}
void set(Bits &bits, std::size_t index) { bits[index / 64] |= std::uint64_t{1} << (index % 64); }
void trim(Bits &bits, std::size_t count) {
    if (count % 64) bits.back() &= (std::uint64_t{1} << (count % 64)) - 1;
}
bool overlaps(const Box &a, const Box &b) noexcept {
    for (int i = 0; i < 3; ++i)
        if (a.max[i] < b.min[i] || b.max[i] < a.min[i]) return false;
    return true;
}
bool contains(const Box &box, const Point &point) noexcept {
    for (int i = 0; i < 3; ++i)
        if (point[i] < box.min[i] || point[i] > box.max[i]) return false;
    return true;
}
void validate(const Box &box, bool flat = false) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(box.min[i]) || !std::isfinite(box.max[i]) ||
            std::abs(box.min[i]) > 1'000'000 || std::abs(box.max[i]) > 1'000'000 ||
            (flat ? box.min[i] > box.max[i] : box.min[i] >= box.max[i]))
            throw std::invalid_argument("Carved PVS bounds");
}
std::size_t leaf_index(std::int32_t child) { return std::size_t(-1 - std::int64_t(child)); }
} // namespace

bool View::sees_leaf(std::uint32_t leaf) const noexcept { return leaf && test(leaves, leaf - 1); }

Prepared::Prepared(std::shared_ptr<const Source> source, std::span<const Box> cuts, Limits limits)
    : source_(std::move(source)), limits_(limits) {
    if (!source_ || !limits.words || !limits.operations || source_->contents.empty() ||
        source_->contents[0] != -2 || source_->contents.size() > 65'537)
        throw std::invalid_argument("Carved PVS source or limits");
    const auto leaves = source_->contents.size() - 1;
    words_ = (leaves + 63) / 64;
    if (cuts.size() > limits.boxes || cuts.size() > 65'536 ||
        (words_ && leaves > limits.words / words_)) throw std::length_error("Carved PVS capacity");
    if (source_->pvs.size() != words_ * leaves) throw std::invalid_argument("Carved PVS row dimensions");
    consume(operations_, limits.operations, source_->pvs.size() + source_->contents.size() + cuts.size());
    for (auto contents : source_->contents)
        if (contents >= 0 || contents < -15) throw std::invalid_argument("Carved PVS leaf contents");
    // Validate topology with the existing bounded collision validator, while
    // retaining unique render-leaf identities for all visibility queries.
    const auto to_contents = [&](std::int32_t child) {
        if (child >= 0) return child;
        const auto leaf = leaf_index(child);
        if (leaf >= source_->contents.size()) throw std::invalid_argument("Carved PVS leaf index");
        return source_->contents[leaf];
    };
    consume(operations_, limits.operations, source_->nodes.size());
    auto nodes = source_->nodes;
    for (auto &node : nodes) for (auto &child : node.children) child = to_contents(child);
    const auto validated = carving::validate_hull({source_->planes, nodes, to_contents(source_->root)},
        {limits.operations, limits.operations - operations_});
    consume(operations_, limits.operations, validated);
    for (const auto &cut : cuts) validate(cut);
    cuts_.assign(cuts.begin(), cuts.end());
    order_.resize(cuts.size());
    std::iota(order_.begin(), order_.end(), 0u);
    tree_.reserve(cuts.size() * 2);
    const auto build = [&](auto &&self, std::size_t first, std::size_t count) -> std::uint32_t {
        const auto index = static_cast<std::uint32_t>(tree_.size());
        Node node{cuts_[order_[first]], static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count)};
        consume(operations_, limits.operations, count);
        for (std::size_t i = first + 1; i < first + count; ++i) for (int axis = 0; axis < 3; ++axis) {
            node.bounds.min[axis] = std::min(node.bounds.min[axis], cuts_[order_[i]].min[axis]);
            node.bounds.max[axis] = std::max(node.bounds.max[axis], cuts_[order_[i]].max[axis]);
        }
        tree_.push_back(node);
        if (count > 8) {
            int axis = 0;
            for (int i = 1; i < 3; ++i)
                if (node.bounds.max[i] - node.bounds.min[i] > node.bounds.max[axis] - node.bounds.min[axis]) axis = i;
            const auto middle = first + count / 2;
            std::nth_element(order_.begin() + first, order_.begin() + middle, order_.begin() + first + count,
                [&](auto a, auto b) {
                    consume(operations_, limits.operations);
                    const double ca = cuts_[a].min[axis] + cuts_[a].max[axis], cb = cuts_[b].min[axis] + cuts_[b].max[axis];
                    return ca == cb ? a < b : ca < cb;
                });
            tree_[index].left = self(self, first, middle - first);
            tree_[index].right = self(self, middle, first + count - middle);
            tree_[index].count = 0;
        }
        return index;
    };
    if (!cuts.empty()) build(build, 0, cuts.size());
    std::vector<std::uint32_t> parent(cuts.size()), sizes(cuts.size(), 1);
    std::iota(parent.begin(), parent.end(), 0u);
    const auto root = [&](std::uint32_t id) {
        while (parent[id] != id) { parent[id] = parent[parent[id]]; id = parent[id]; }
        return id;
    };
    for (std::uint32_t i = 0; i < cuts.size(); ++i) {
        const auto link = [&](auto &&self, std::uint32_t index) -> void {
            consume(operations_, limits.operations);
            const auto &node = tree_[index];
            if (!overlaps(node.bounds, cuts[i])) return;
            if (!node.count) { self(self, node.left); self(self, node.right); return; }
            consume(operations_, limits.operations, node.count);
            for (std::size_t j = node.first; j < std::size_t(node.first) + node.count; ++j) {
                const auto other = order_[j];
                if (other <= i || !overlaps(cuts[i], cuts[other])) continue;
                auto a = root(i), b = root(other);
                if (a == b) continue;
                if (sizes[a] < sizes[b]) std::swap(a, b);
                parent[b] = a; sizes[a] += sizes[b];
            }
        };
        link(link, 0);
    }
    constexpr auto missing = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> ids(cuts.size(), missing);
    cut_components_.resize(cuts.size());
    for (std::uint32_t i = 0; i < cuts.size(); ++i) {
        auto &id = ids[root(i)];
        if (id == missing) {
            if (words_ && components_.size() + 1 > (limits.words - source_->pvs.size()) / words_)
                throw std::length_error("Carved PVS component storage");
            id = static_cast<std::uint32_t>(components_.size());
            components_.push_back({Bits(words_)});
        }
        cut_components_[i] = id;
    }
    leaf_components_.resize(source_->contents.size());
    std::size_t contacts = 0;
    // Each cut is classified against the original BSP planes, never a leaf's
    // rounded file bounding box. Inclusive boundaries conservatively preserve
    // doors opened exactly up to the original air/solid plane.
    for (std::size_t i = 0; i < cuts.size(); ++i) {
        const auto component = cut_components_[i];
        const auto collect = [&](auto &&self, std::int32_t at) -> void {
            consume(operations_, limits.operations);
            if (at < 0) {
                const auto leaf = leaf_index(at);
                if (!leaf || source_->contents[leaf] == -2) return;
                if (contacts >= limits.words) throw std::length_error("Carved PVS contact storage");
                ++contacts;
                leaf_components_[leaf].push_back(component);
                return;
            }
            const auto &node = source_->nodes[at];
            const auto &plane = source_->planes[node.plane];
            double low = -plane.distance, high = low, magnitude = std::abs(plane.distance);
            for (int axis = 0; axis < 3; ++axis) {
                const auto a = plane.normal[axis] * cuts[i].min[axis], b = plane.normal[axis] * cuts[i].max[axis];
                low += std::min(a, b); high += std::max(a, b); magnitude += std::max(std::abs(a), std::abs(b));
            }
            const double error = 16 * std::numeric_limits<double>::epsilon() * std::max(1.0, magnitude);
            if (high >= -error) self(self, node.children[0]);
            if (low <= error) self(self, node.children[1]);
        };
        collect(collect, source_->root);
    }
    for (std::size_t leaf = 1; leaf < source_->contents.size(); ++leaf) {
        auto &adjacent = leaf_components_[leaf];
        consume(operations_, limits.operations, adjacent.size() * std::max<std::size_t>(1, std::bit_width(adjacent.size())));
        std::sort(adjacent.begin(), adjacent.end());
        adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
        for (auto component : adjacent) {
            consume(operations_, limits.operations, words_);
            auto &pvs = components_[component].pvs;
            for (std::size_t word = 0; word < words_; ++word) pvs[word] |= source_->pvs[(leaf - 1) * words_ + word];
            set(pvs, leaf - 1);
        }
    }
    for (auto &component : components_) trim(component.pvs, leaves);
}

View Prepared::view(std::uint32_t key) const {
    if (key >= source_->contents.size() + components_.size()) throw std::out_of_range("Carved PVS view key");
    View result{Bits(words_), Bits((components_.size() + 63) / 64)};
    const auto leaves = source_->contents.size() - 1;
    if (!key) {
        std::fill(result.leaves.begin(), result.leaves.end(), ~std::uint64_t{}); trim(result.leaves, leaves);
        std::fill(result.components.begin(), result.components.end(), ~std::uint64_t{}); trim(result.components, components_.size());
        return result;
    }
    std::vector<std::uint32_t> pending;
    pending.reserve(components_.size());
    const auto enqueue = [&](std::uint32_t component) {
        if (!test(result.components, component)) { set(result.components, component); pending.push_back(component); }
    };
    const auto visit_bits = [&](std::size_t word, std::uint64_t bits) {
        while (bits) {
            const auto bit = std::countr_zero(bits);
            bits &= bits - 1;
            const auto leaf = word * 64 + bit + 1;
            if (leaf >= source_->contents.size()) continue;
            consume(result.operations, limits_.operations, leaf_components_[leaf].size() + 1);
            for (auto component : leaf_components_[leaf]) enqueue(component);
        }
    };
    if (key < source_->contents.size()) {
        consume(result.operations, limits_.operations, words_);
        std::copy_n(source_->pvs.begin() + (key - 1) * words_, words_, result.leaves.begin());
        set(result.leaves, key - 1); trim(result.leaves, leaves);
        for (std::size_t word = 0; word < words_; ++word) visit_bits(word, result.leaves[word]);
    } else enqueue(key - static_cast<std::uint32_t>(source_->contents.size()));
    for (std::size_t at = 0; at < pending.size(); ++at) {
        const auto &pvs = components_[pending[at]].pvs;
        consume(result.operations, limits_.operations, words_);
        for (std::size_t word = 0; word < words_; ++word) {
            const auto added = pvs[word] & ~result.leaves[word];
            result.leaves[word] |= pvs[word];
            visit_bits(word, added);
        }
    }
    return result;
}

std::uint32_t Prepared::key(std::uint32_t original_leaf, Point eye) const noexcept {
    if (original_leaf && original_leaf < source_->contents.size()) return original_leaf;
    if (tree_.empty()) return 0;
    for (auto coordinate : eye) if (!std::isfinite(coordinate)) return 0;
    const auto locate = [&](auto &&self, std::uint32_t index) -> std::uint32_t {
        const auto &node = tree_[index];
        if (!contains(node.bounds, eye)) return 0;
        if (!node.count) { const auto found = self(self, node.left); return found ? found : self(self, node.right); }
        for (std::size_t i = node.first; i < std::size_t(node.first) + node.count; ++i)
            if (contains(cuts_[order_[i]], eye))
                return static_cast<std::uint32_t>(source_->contents.size()) + cut_components_[order_[i]];
        return 0;
    };
    return locate(locate, 0);
}

bool Prepared::sees_bounds(const View &view, const Box &bounds) const {
    validate(bounds, true);
    if (view.leaves.size() != words_ || view.components.size() != (components_.size() + 63) / 64)
        throw std::invalid_argument("Carved PVS view dimensions");
    if (tree_.empty()) return false;
    const auto visible = [&](auto &&self, std::uint32_t index) -> bool {
        const auto &node = tree_[index];
        if (!overlaps(node.bounds, bounds)) return false;
        if (!node.count) return self(self, node.left) || self(self, node.right);
        for (std::size_t i = node.first; i < std::size_t(node.first) + node.count; ++i)
            if (test(view.components, cut_components_[order_[i]]) && overlaps(cuts_[order_[i]], bounds)) return true;
        return false;
    };
    return visible(visible, 0);
}
} // namespace goldcraft::visibility
