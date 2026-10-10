#pragma once
#include "goldcraft/carved_visibility.hpp"

namespace goldcraft::visibility {
// Per-revision server rows, prepared before the collision transaction commits.
// Fat-PVS traversal merges only the eye's original BSP leaves, never the PVS
// of every visible leaf (which would incorrectly close the original PVS).
class EntityCache {
  public:
    EntityCache(std::shared_ptr<const Source> source, std::span<const carving::Box> cuts,
                Limits limits = {});
    void begin(bool audible = false) noexcept;
    bool merge(std::uint32_t key, std::span<std::uint8_t> mask, bool audible = false) noexcept;
    std::uint32_t cavity_key(carving::Point eye) const noexcept { return topology_.key(0, eye); }
    std::uint32_t key(std::uint32_t leaf, carving::Point point) const noexcept {
        return topology_.key(leaf, point);
    }
    bool sees_bounds(const carving::Box &bounds, bool audible = false) const;
    bool matches(std::span<const std::uint8_t> mask, bool audible = false) const noexcept;
    // Immutable point-to-point queries do not disturb a pending FatPVS/FatPAS.
    bool reaches(std::uint32_t from, std::uint32_t to, bool audible = false) const noexcept;
    std::size_t leaf_count() const noexcept { return topology_.leaf_count(); }
    std::size_t operations() const noexcept { return operations_; }
    std::size_t cached_words() const noexcept { return cached_words_; }

  private:
    bool valid_width(std::size_t bytes) const noexcept;
    Prepared topology_;
    std::vector<View> rows_, audible_rows_;
    View current_, current_audible_;
    std::size_t row_bytes_ = 0, operations_ = 0, cached_words_ = 0;
};
} // namespace goldcraft::visibility
