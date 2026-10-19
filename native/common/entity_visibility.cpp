#include "goldcraft/entity_visibility.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace goldcraft::visibility {
namespace {
// A fingerprint only selects a slot; both complete bit arrays must match.
std::uint64_t row_fingerprint(const View& row) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto word : row.leaves) hash = (hash ^ word) * 1099511628211ull;
    for (auto word : row.components) hash = (hash ^ word) * 1099511628211ull;
    return hash;
}
struct RowReuse {
    std::uint64_t hash = 0;
    std::size_t key = 0, operations = 0;
    bool valid = false;
};
} // namespace
struct EntityCache::PreparedRows {
    Prepared topology_;
    std::vector<View> rows_, audible_rows_;
    std::size_t row_bytes_ = 0, operations_ = 0, cached_words_ = 0;
    std::size_t reused_visual_rows_ = 0, reused_audible_rows_ = 0;
    PreparedRows(std::shared_ptr<const Source> source, std::span<const carving::Box> cuts,
                 Limits limits, const PreparedRows* previous);
};
EntityCache::PreparedRows::PreparedRows(std::shared_ptr<const Source> source,
                                        std::span<const carving::Box> cuts, Limits limits,
                                        const PreparedRows* previous)
    : topology_(source, cuts, limits, previous ? &previous->topology_ : nullptr,
                16 * 1024 * 1024) {
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
    rows_.reserve(count);
    operations_ = topology_.operations();
    // Reuse the same temporary direct-mapped index for visual and audible
    // preparation. At most 64 KiB; no copied keys or old revision owners.
    const auto reuse_count = std::min<std::size_t>(count, 64 * 1024 / sizeof(RowReuse));
    const auto reuse = std::make_unique<RowReuse[]>(reuse_count);
    // Match Prepared::view's ordinary-leaf seed: original PVS, self included,
    // tail padding removed. Key zero and virtual cavities use different seeds.
    const auto seed_word = [&](std::size_t key, std::size_t word) {
        auto bits = source->pvs[(key - 1) * words + word];
        if ((key - 1) / 64 == word) bits |= std::uint64_t{1} << ((key - 1) % 64);
        if (word + 1 == words && leaves % 64) bits &= (std::uint64_t{1} << (leaves % 64)) - 1;
        return bits;
    };
    const auto same_seed = [&](std::size_t a, std::size_t b) {
        for (std::size_t word = 0; word < words; ++word)
            if (seed_word(a, word) != seed_word(b, word)) return false;
        return true;
    };
    for (std::uint32_t key = 0; key < count; ++key) {
        RowReuse* slot = nullptr;
        std::uint64_t hash = 14695981039346656037ull;
        if (key && key <= leaves) {
            for (std::size_t word = 0; word < words; ++word)
                hash = (hash ^ seed_word(key, word)) * 1099511628211ull;
            slot = &reuse[hash % reuse_count];
        }
        View row;
        if (slot && slot->valid && slot->hash == hash && same_seed(slot->key, key)) {
            // Identical seeds in this topology follow the same queue order
            // and retain exactly the same operation charge and complete view.
            row = rows_[slot->key];
            ++reused_visual_rows_;
        } else row = topology_.view(key);
        if (row.operations > limits.operations - operations_)
            throw std::length_error("Entity PVS cache preparation budget");
        operations_ += row.operations;
        rows_.push_back(std::move(row));
        if (slot) *slot = {hash, key, 0, true};
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
    // Visual seeds and final masks are different keys. Clear the index before
    // reusing its storage; a collision merely replaces one completed row.
    std::fill_n(reuse.get(), reuse_count, RowReuse{});
    for (const auto &visual : rows_) {
        const auto hash = row_fingerprint(visual);
        auto& slot = reuse[hash % reuse_count];
        if (slot.valid && slot.hash == hash && rows_[slot.key].leaves == visual.leaves &&
            rows_[slot.key].components == visual.components) {
            // Keep the original expansion charge and independent output row.
            // Equal masks can have different visual preparation work counts.
            consume(slot.operations);
            auto heard = audible_rows_[slot.key];
            heard.operations = visual.operations;
            audible_rows_.push_back(std::move(heard));
            ++reused_audible_rows_;
            continue;
        }
        const auto before = operations_;
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
        const auto key = audible_rows_.size();
        audible_rows_.push_back(std::move(heard));
        slot = {hash, key, operations_ - before, true};
    }
}
EntityCache::EntityCache(std::shared_ptr<const Source> source, std::span<const carving::Box> cuts,
                         Limits limits, const EntityCache* previous)
    : prepared_(previous && previous->prepared_ && previous->prepared_->topology_.matches_input(source, cuts, limits)
        ? previous->prepared_ : std::make_shared<PreparedRows>(std::move(source), cuts, limits,
            previous ? previous->prepared_.get() : nullptr)) {
    // Never share FatPVS/FatPAS accumulators with the previous transaction.
    // Their storage is included in PreparedRows' original per-cache budget.
    const auto& row = prepared_->rows_.front();
    current_ = {Bits(row.leaves.size()), Bits(row.components.size())};
    current_audible_ = {Bits(row.leaves.size()), Bits(row.components.size())};
}
std::uint32_t EntityCache::key(std::uint32_t leaf, carving::Point point) const noexcept {
    return prepared_->topology_.key(leaf, point);
}
std::size_t EntityCache::leaf_count() const noexcept { return prepared_->topology_.leaf_count(); }
std::size_t EntityCache::operations() const noexcept { return prepared_->operations_; }
std::size_t EntityCache::cached_words() const noexcept { return prepared_->cached_words_; }
std::size_t EntityCache::reused_contacts() const noexcept { return prepared_->topology_.reused_contacts(); }
std::size_t EntityCache::contact_cache_bytes() const noexcept { return prepared_->topology_.contact_cache_bytes(); }
std::size_t EntityCache::reused_visual_rows() const noexcept { return prepared_->reused_visual_rows_; }
std::size_t EntityCache::reused_audible_rows() const noexcept { return prepared_->reused_audible_rows_; }
void EntityCache::begin(bool audible) noexcept {
    auto &current = audible ? current_audible_ : current_;
    std::fill(current.leaves.begin(), current.leaves.end(), 0);
    std::fill(current.components.begin(), current.components.end(), 0);
}
bool EntityCache::valid_width(std::size_t bytes) const noexcept {
    // ReHLDS uses four-byte rows with gPVS, and (numleafs + 31) >> 3
    // without it. That legacy padding can extend beyond our final 64-bit word.
    return bytes >= prepared_->row_bytes_ && bytes - prepared_->row_bytes_ <= 3;
}
bool EntityCache::merge(std::uint32_t key, std::span<std::uint8_t> mask, bool audible) noexcept {
    // Key zero is the Renderer no-vis convention. A server eye in untouched
    // solid must retain the engine result, not disclose every entity.
    if (!key || key >= prepared_->rows_.size() || !valid_width(mask.size()))
        return false;
    const auto &row = (audible ? prepared_->audible_rows_ : prepared_->rows_)[key];
    auto &current = audible ? current_audible_ : current_;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(row.leaves.data());
    for (std::size_t i = 0; i < prepared_->row_bytes_; ++i)
        mask[i] |= bytes[i];
    for (std::size_t i = 0; i < current.leaves.size(); ++i)
        current.leaves[i] |= row.leaves[i];
    for (std::size_t i = 0; i < current.components.size(); ++i)
        current.components[i] |= row.components[i];
    return true;
}
bool EntityCache::sees_bounds(const carving::Box &bounds, bool audible) const {
    return prepared_->topology_.sees_bounds(audible ? current_audible_ : current_, bounds);
}
bool EntityCache::matches(std::span<const std::uint8_t> mask, bool audible) const noexcept {
    const auto &current = audible ? current_audible_ : current_;
    return valid_width(mask.size()) &&
           (!prepared_->row_bytes_ || std::memcmp(mask.data(), current.leaves.data(), prepared_->row_bytes_) == 0) &&
           std::all_of(mask.begin() + prepared_->row_bytes_, mask.end(), [](auto byte) { return byte == 0; });
}
bool EntityCache::reaches(std::uint32_t from, std::uint32_t to, bool audible) const noexcept {
    if (!from || !to || from >= prepared_->rows_.size() || to >= prepared_->rows_.size())
        return false;
    const auto &row = (audible ? prepared_->audible_rows_ : prepared_->rows_)[from];
    const bool leaf = to < prepared_->topology_.leaf_count();
    const auto bit = leaf ? to - 1 : to - prepared_->topology_.leaf_count();
    const auto &bits = leaf ? row.leaves : row.components;
    return (bits[bit / 64] & (std::uint64_t{1} << (bit % 64))) != 0;
}
} // namespace goldcraft::visibility
