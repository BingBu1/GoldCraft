#pragma once
#include "goldcraft/carved_visibility.hpp"

namespace goldcraft::visibility {
// Immutable server rows with independent per-revision query accumulators,
// prepared before the collision transaction commits.
// Fat-PVS traversal merges only the eye's original BSP leaves, never the PVS
// of every visible leaf (which would incorrectly close the original PVS).
class EntityCache {
  public:
    EntityCache(std::shared_ptr<const Source> source, std::span<const carving::Box> cuts,
                Limits limits = {}, const EntityCache* previous = nullptr);
    void begin(bool audible = false) noexcept;
    bool merge(std::uint32_t key, std::span<std::uint8_t> mask, bool audible = false) noexcept;
    std::uint32_t cavity_key(carving::Point eye) const noexcept { return key(0, eye); }
    std::uint32_t key(std::uint32_t leaf, carving::Point point) const noexcept;
    bool sees_bounds(const carving::Box &bounds, bool audible = false) const;
    bool matches(std::span<const std::uint8_t> mask, bool audible = false) const noexcept;
    // Immutable point-to-point queries do not disturb a pending FatPVS/FatPAS.
    bool reaches(std::uint32_t from, std::uint32_t to, bool audible = false) const noexcept;
    std::size_t leaf_count() const noexcept;
    std::size_t operations() const noexcept;
    std::size_t cached_words() const noexcept;
    bool shares_preparation(const EntityCache& other) const noexcept { return prepared_ == other.prepared_; }

  private:
    bool valid_width(std::size_t bytes) const noexcept;
    struct PreparedRows;
    std::shared_ptr<const PreparedRows> prepared_;
    View current_, current_audible_;
};
} // namespace goldcraft::visibility
