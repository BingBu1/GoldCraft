#include "goldcraft/entity_visibility.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace goldcraft::visibility {
EntityCache::EntityCache(std::shared_ptr<const Source> source, std::span<const carving::Box> cuts,
                         Limits limits)
    : topology_(std::move(source), cuts, limits) {
    static_assert(std::endian::native == std::endian::little);
    const auto leaves = topology_.leaf_count() - 1;
    const auto words = (leaves + 63) / 64;
    const auto component_words = (topology_.component_count() + 63) / 64;
    const auto count = topology_.leaf_count() + topology_.component_count();
    row_bytes_ = (leaves + 7) / 8;
    // Bound the entire cache and cumulative preparation, not each row alone.
    const auto row_words = words + component_words;
    if (row_words && count + 1 > limits.words / (2 * row_words))
        throw std::length_error("Entity PVS cache storage budget");
    cached_words_ = 2 * (count + 1) * row_words;
    current_ = {Bits(words), Bits(component_words)};
    current_audible_ = {Bits(words), Bits(component_words)};
    rows_.reserve(count);
    operations_ = topology_.operations();
    for (std::uint32_t key = 0; key < count; ++key) {
        auto row = topology_.view(key);
        if (row.operations > limits.operations - operations_)
            throw std::length_error("Entity PVS cache preparation budget");
        operations_ += row.operations;
        rows_.push_back(std::move(row));
    }
    // Match CM_CalcPAS: union the PVS of each directly visible region ONCE.
    // Iterate the immutable visual row, not the growing audible result. Virtual
    // cavities are regions too, including a sealed cavity with no old leaf.
    audible_rows_.reserve(count);
    const auto consume = [&](std::size_t work) {
        if (work > limits.operations - operations_)
            throw std::length_error("Entity PAS cache preparation budget");
        operations_ += work;
    };
    for (const auto &visual : rows_) {
        consume(row_words);
        auto heard = visual;
        const auto expand = [&](const Bits &bits, std::size_t offset) {
            for (std::size_t word = 0; word < bits.size(); ++word) {
                consume(1);
                for (auto pending = bits[word]; pending; pending &= pending - 1) {
                    const auto key = offset + word * 64 + std::countr_zero(pending);
                    consume(row_words + 1);
                    const auto &next = rows_[key];
                    for (std::size_t i = 0; i < words; ++i)
                        heard.leaves[i] |= next.leaves[i];
                    for (std::size_t i = 0; i < component_words; ++i)
                        heard.components[i] |= next.components[i];
                }
            }
        };
        expand(visual.leaves, 1);
        expand(visual.components, topology_.leaf_count());
        audible_rows_.push_back(std::move(heard));
    }
}
void EntityCache::begin(bool audible) noexcept {
    auto &current = audible ? current_audible_ : current_;
    std::fill(current.leaves.begin(), current.leaves.end(), 0);
    std::fill(current.components.begin(), current.components.end(), 0);
}
bool EntityCache::valid_width(std::size_t bytes) const noexcept {
    // ReHLDS uses four-byte rows with gPVS, and (numleafs + 31) >> 3
    // without it. That legacy padding can extend beyond our final 64-bit word.
    return bytes >= row_bytes_ && bytes - row_bytes_ <= 3;
}
bool EntityCache::merge(std::uint32_t key, std::span<std::uint8_t> mask, bool audible) noexcept {
    // Key zero is the Renderer no-vis convention. A server eye in untouched
    // solid must retain the engine result, not disclose every entity.
    if (!key || key >= rows_.size() || !valid_width(mask.size()))
        return false;
    const auto &row = (audible ? audible_rows_ : rows_)[key];
    auto &current = audible ? current_audible_ : current_;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(row.leaves.data());
    for (std::size_t i = 0; i < row_bytes_; ++i)
        mask[i] |= bytes[i];
    for (std::size_t i = 0; i < current.leaves.size(); ++i)
        current.leaves[i] |= row.leaves[i];
    for (std::size_t i = 0; i < current.components.size(); ++i)
        current.components[i] |= row.components[i];
    return true;
}
bool EntityCache::sees_bounds(const carving::Box &bounds, bool audible) const {
    return topology_.sees_bounds(audible ? current_audible_ : current_, bounds);
}
bool EntityCache::matches(std::span<const std::uint8_t> mask, bool audible) const noexcept {
    const auto &current = audible ? current_audible_ : current_;
    return valid_width(mask.size()) &&
           (!row_bytes_ || std::memcmp(mask.data(), current.leaves.data(), row_bytes_) == 0) &&
           std::all_of(mask.begin() + row_bytes_, mask.end(), [](auto byte) { return byte == 0; });
}
bool EntityCache::reaches(std::uint32_t from, std::uint32_t to, bool audible) const noexcept {
    if (!from || !to || from >= rows_.size() || to >= rows_.size())
        return false;
    const auto &row = (audible ? audible_rows_ : rows_)[from];
    const bool leaf = to < topology_.leaf_count();
    const auto bit = leaf ? to - 1 : to - topology_.leaf_count();
    const auto &bits = leaf ? row.leaves : row.components;
    return (bits[bit / 64] & (std::uint64_t{1} << (bit % 64))) != 0;
}
} // namespace goldcraft::visibility
