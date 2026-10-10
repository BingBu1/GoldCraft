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
    if (words + component_words && count + 1 > limits.words / (words + component_words))
        throw std::length_error("Entity PVS cache storage budget");
    cached_words_ = (count + 1) * (words + component_words);
    current_ = {Bits(words), Bits(component_words)};
    rows_.reserve(count);
    operations_ = topology_.operations();
    for (std::uint32_t key = 0; key < count; ++key) {
        auto row = topology_.view(key);
        if (row.operations > limits.operations - operations_)
            throw std::length_error("Entity PVS cache preparation budget");
        operations_ += row.operations;
        rows_.push_back(std::move(row));
    }
}
void EntityCache::begin() noexcept {
    std::fill(current_.leaves.begin(), current_.leaves.end(), 0);
    std::fill(current_.components.begin(), current_.components.end(), 0);
}
bool EntityCache::valid_width(std::size_t bytes) const noexcept {
    // ReHLDS uses four-byte rows with gPVS, and (numleafs + 31) >> 3
    // without it. That legacy padding can extend beyond our final 64-bit word.
    return bytes >= row_bytes_ && bytes - row_bytes_ <= 3;
}
bool EntityCache::merge(std::uint32_t key, std::span<std::uint8_t> mask) noexcept {
    // Key zero is the Renderer no-vis convention. A server eye in untouched
    // solid must retain the engine result, not disclose every entity.
    if (!key || key >= rows_.size() || !valid_width(mask.size()))
        return false;
    const auto &row = rows_[key];
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(row.leaves.data());
    for (std::size_t i = 0; i < row_bytes_; ++i)
        mask[i] |= bytes[i];
    for (std::size_t i = 0; i < current_.leaves.size(); ++i)
        current_.leaves[i] |= row.leaves[i];
    for (std::size_t i = 0; i < current_.components.size(); ++i)
        current_.components[i] |= row.components[i];
    return true;
}
bool EntityCache::sees_bounds(const carving::Box &bounds) const {
    return topology_.sees_bounds(current_, bounds);
}
bool EntityCache::matches(std::span<const std::uint8_t> mask) const noexcept {
    return valid_width(mask.size()) &&
           (!row_bytes_ || std::memcmp(mask.data(), current_.leaves.data(), row_bytes_) == 0) &&
           std::all_of(mask.begin() + row_bytes_, mask.end(), [](auto byte) { return byte == 0; });
}
} // namespace goldcraft::visibility
