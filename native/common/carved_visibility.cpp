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
int compare_box(const Box& a, const Box& b) noexcept {
    for (int axis = 0; axis < 3; ++axis) {
        const auto amin = std::bit_cast<std::uint64_t>(a.min[axis]);
        const auto bmin = std::bit_cast<std::uint64_t>(b.min[axis]);
        if (amin != bmin) return amin < bmin ? -1 : 1;
        const auto amax = std::bit_cast<std::uint64_t>(a.max[axis]);
        const auto bmax = std::bit_cast<std::uint64_t>(b.max[axis]);
        if (amax != bmax) return amax < bmax ? -1 : 1;
    }
    return 0;
}
} // namespace

bool View::sees_leaf(std::uint32_t leaf) const noexcept { return leaf && test(leaves, leaf - 1); }

Prepared::Prepared(std::shared_ptr<const Source> source, std::span<const Box> cuts, Limits limits,
                   const Prepared* previous, std::size_t contact_cache_bytes)
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
    // Cache storage is optional and separately bounded. Default callers retain
    // no classifications; the server opts in to retain them between revisions.
    const auto cache_limit = std::min<std::size_t>(contact_cache_bytes, 16 * 1024 * 1024);
    if (cache_limit && cuts.size() <= cache_limit / (sizeof(contacts_[0]) + sizeof(contact_order_[0]))) {
        contacts_.resize(cuts.size());
        contact_order_.reserve(cuts.size());
        contact_cache_bytes_ = contacts_.capacity() * sizeof(contacts_[0]) +
                               contact_order_.capacity() * sizeof(contact_order_[0]);
        if (contact_cache_bytes_ > cache_limit) {
            decltype(contacts_){}.swap(contacts_);
            decltype(contact_order_){}.swap(contact_order_);
            contact_cache_bytes_ = 0;
        }
    }
    if (previous && source_ != previous->source_) previous = nullptr;
    // Each cut is classified against the original BSP planes, never a leaf's
    // rounded file bounding box. Inclusive boundaries conservatively preserve
    // doors opened exactly up to the original air/solid plane.
    for (std::size_t i = 0; i < cuts.size(); ++i) {
        const auto component = cut_components_[i];
        const auto append = [&](std::uint32_t leaf) {
            if (contacts >= limits.words) throw std::length_error("Carved PVS contact storage");
            ++contacts;
            leaf_components_[leaf].push_back(component);
        };
        std::shared_ptr<const Contacts> cached;
        if (previous) {
            const auto& order = previous->contact_order_;
            const auto found = std::lower_bound(order.begin(), order.end(), cuts[i], [&](auto index, const Box& box) {
                return compare_box(previous->cuts_[index], box) < 0;
            });
            if (found != order.end() && compare_box(previous->cuts_[*found], cuts[i]) == 0)
                cached = previous->contacts_[*found];
        }
        const auto retain = [&](const std::shared_ptr<const Contacts>& value) {
            if (contacts_.empty() || !value) return;
            const auto bytes = sizeof(Contacts) + 64 + value->leaves.capacity() * sizeof(std::uint32_t);
            if (bytes <= cache_limit - contact_cache_bytes_) {
                contacts_[i] = value;
                contact_order_.push_back(static_cast<std::uint32_t>(i));
                contact_cache_bytes_ += bytes;
            }
        };
        if (cached) {
            consume(operations_, limits.operations, cached->operations);
            for (const auto leaf : cached->leaves) append(leaf);
            retain(cached);
            ++reused_contacts_;
            continue;
        }
        constexpr std::size_t entry_bytes = sizeof(Contacts) + 64;
        const auto available = cache_limit - contact_cache_bytes_;
        auto captured = !contacts_.empty() && available >= entry_bytes ? std::make_shared<Contacts>() : nullptr;
        const auto capture_leaf = [&](std::uint32_t leaf) {
            if (!captured) return;
            auto& leaves = captured->leaves;
            const auto max_leaves = (available - entry_bytes) / sizeof(std::uint32_t);
            if (leaves.size() == max_leaves) { captured.reset(); return; }
            if (leaves.size() == leaves.capacity()) {
                leaves.reserve(std::min(max_leaves, std::max<std::size_t>(4, leaves.capacity() * 2)));
                if (leaves.capacity() > max_leaves) { captured.reset(); return; }
            }
            leaves.push_back(leaf);
        };
        const auto before = operations_;
        const auto collect = [&](auto &&self, std::int32_t at) -> void {
            consume(operations_, limits.operations);
            if (at < 0) {
                const auto leaf = leaf_index(at);
                if (!leaf || source_->contents[leaf] == -2) return;
                append(static_cast<std::uint32_t>(leaf));
                capture_leaf(static_cast<std::uint32_t>(leaf));
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
        if (captured) {
            captured->operations = operations_ - before;
            retain(captured);
        }
    }
    std::sort(contact_order_.begin(), contact_order_.end(), [&](auto a, auto b) {
        const auto order = compare_box(cuts_[a], cuts_[b]);
        return order ? order < 0 : a < b;
    });
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

std::uint64_t Prepared::retained_bytes() const noexcept {
    std::uint64_t bytes = sizeof(Prepared) + 64;
    if (source_) {
        bytes += sizeof(Source) + 64 + std::uint64_t(source_->planes.capacity()) * sizeof(carving::Plane) +
                 std::uint64_t(source_->nodes.capacity()) * sizeof(carving::HullNode) +
                 std::uint64_t(source_->contents.capacity()) * sizeof(std::int32_t) +
                 std::uint64_t(source_->pvs.capacity()) * sizeof(std::uint64_t);
    }
    bytes += std::uint64_t(cuts_.capacity()) * sizeof(Box) + std::uint64_t(tree_.capacity()) * sizeof(Node) +
             std::uint64_t(order_.capacity()) * sizeof(std::uint32_t) +
             std::uint64_t(cut_components_.capacity()) * sizeof(std::uint32_t) +
             std::uint64_t(components_.capacity()) * sizeof(Component) +
             std::uint64_t(leaf_components_.capacity()) * sizeof(leaf_components_[0]);
    for (const auto& component : components_) bytes += std::uint64_t(component.pvs.capacity()) * sizeof(std::uint64_t);
    for (const auto& adjacent : leaf_components_) bytes += std::uint64_t(adjacent.capacity()) * sizeof(std::uint32_t);
    bytes += std::uint64_t(contacts_.capacity()) * sizeof(contacts_[0]) +
             std::uint64_t(contact_order_.capacity()) * sizeof(contact_order_[0]);
    for (const auto& contact : contacts_) if (contact)
        bytes += sizeof(Contacts) + 64 + std::uint64_t(contact->leaves.capacity()) * sizeof(std::uint32_t);
    return bytes;
}

bool Prepared::matches_input(const std::shared_ptr<const Source>& source,
                             std::span<const Box> cuts, Limits limits) const noexcept {
    if (source_ != source || cuts_.size() != cuts.size() || limits_.boxes != limits.boxes ||
        limits_.words != limits.words || limits_.operations != limits.operations) return false;
    for (std::size_t i = 0; i < cuts.size(); ++i)
        for (int axis = 0; axis < 3; ++axis)
            if (std::bit_cast<std::uint64_t>(cuts_[i].min[axis]) != std::bit_cast<std::uint64_t>(cuts[i].min[axis]) ||
                std::bit_cast<std::uint64_t>(cuts_[i].max[axis]) != std::bit_cast<std::uint64_t>(cuts[i].max[axis])) return false;
    return true;
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
