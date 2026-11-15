// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace goldcraft::resource_model_cache {
// Matched HL10210 host ABI, not an on-disk SPR structure.
struct SpriteFrame { int width, height; float up, down, left, right; int texture; };
struct SpriteFrameDesc { int type; SpriteFrame* frame; };
struct SpriteGroup { int count; float* intervals; SpriteFrame* frames[1]; };
struct SpriteHeader {
    short type, format;
    int width, height, count, palette;
    float beam;
    void* model;
    SpriteFrameDesc frames[1];
};
static_assert(sizeof(void*) == 4 && sizeof(SpriteFrame) == 28 &&
    offsetof(SpriteHeader, frames) == 28 && offsetof(SpriteGroup, frames) == 8);

// Only used immediately after the matched native sprite loader returns.
// Temporary-entity globals retain model objects across low-hunk resets. Copy
// the entire late-loaded pointer graph; retain texture IDs and model identity.
class PermanentSprite {
    std::vector<std::unique_ptr<std::byte[]>> blocks_;
    SpriteHeader* header_{};
    std::size_t bytes_{};
    static std::size_t Payload(const void* source) {
        if (!source) throw std::runtime_error("null permanent sprite allocation");
        std::uint32_t sentinel{}, size{};
        const auto* bytes = static_cast<const std::byte*>(source);
        std::memcpy(&sentinel, bytes - 72, 4);
        std::memcpy(&size, bytes - 68, 4);
        // Hunk_AllocName 102382B0: 72-byte prefix and padded payload.
        if (sentinel != 502268397u || size < 72 || size > 64u * 1024 * 1024)
            throw std::runtime_error("invalid permanent sprite hunk allocation");
        return size - 72;
    }
    void* Copy(const void* source, std::size_t length) {
        if (!length || length > Payload(source) || bytes_ + length > 64u * 1024 * 1024)
            throw std::runtime_error("invalid permanent sprite allocation extent");
        auto owned = std::make_unique<std::byte[]>(length);
        std::memcpy(owned.get(), source, length);
        auto* result = owned.get();
        blocks_.push_back(std::move(owned));
        bytes_ += length;
        return result;
    }
    SpriteFrame* Frame(const SpriteFrame* source) {
        return static_cast<SpriteFrame*>(Copy(source, sizeof(SpriteFrame)));
    }
public:
    explicit PermanentSprite(const SpriteHeader* source) {
        const auto extent = Payload(source);
        if (extent < sizeof(SpriteHeader) || source->count < 1 ||
            std::size_t(source->count) > (extent - 28) / sizeof(SpriteFrameDesc) ||
            source->palette < 0 || std::size_t(source->palette) > extent)
            throw std::runtime_error("invalid permanent sprite header");
        header_ = static_cast<SpriteHeader*>(Copy(source, extent)); // Includes palette.
        for (int i = 0; i < source->count; ++i) {
            const auto& frame = source->frames[i];
            if (frame.type == 0) header_->frames[i].frame = Frame(frame.frame);
            else if (frame.type == 1) {
                const auto* group = reinterpret_cast<const SpriteGroup*>(frame.frame);
                const auto groupExtent = Payload(group);
                if (groupExtent < sizeof(SpriteGroup) || group->count < 1 ||
                    std::size_t(group->count) > (groupExtent - 8) / sizeof(SpriteFrame*))
                    throw std::runtime_error("invalid permanent sprite group");
                auto* owned = static_cast<SpriteGroup*>(Copy(group, 8 + sizeof(SpriteFrame*) * group->count));
                owned->intervals = static_cast<float*>(Copy(group->intervals, sizeof(float) * group->count));
                for (int n = 0; n < group->count; ++n) owned->frames[n] = Frame(group->frames[n]);
                header_->frames[i].frame = reinterpret_cast<SpriteFrame*>(owned);
            } else throw std::runtime_error("invalid permanent sprite frame type");
        }
    }
    SpriteHeader* Header() const { return header_; }
};
}
