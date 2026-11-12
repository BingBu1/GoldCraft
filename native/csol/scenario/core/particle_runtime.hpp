// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::particles
{
using Vec3 = std::array<float, 3>;
inline constexpr std::size_t PreparedCapacity = 10000, OrdinaryCapacity = 1200;

// Original client 01DE42D0. Equal-key order belongs to the CRT, not to this
// comparator. In particular, unordered values take the final branch.
inline int CompareDistance(float left, float right)
{
    if (left > right) return -1;
    if (left == right) return 0;
    return 1;
}

// One owner for the supported ordinary-renderer payloads. The original has a
// separate 128-record emitter owner; that limit must not become this pool's
// particle limit. Enhanced rendering and auxiliary timed lists are not here.
//
// Host representation: typed, contiguous, cold-prepared storage replaces the
// original separately allocated headers/payloads. All access is on the client
// thread. A callback may Clear; the current operation then aborts. Recursive
// allocation/rendering is refused rather than invalidating a live traversal.
template<class Payload> class OrdinaryPool
{
    using Index = std::uint16_t;
    static constexpr Index None = 0xffff;
    struct Node
    {
        Payload payload{};
        Index next{None}, previous{None};
    };
    struct DrawEntry
    {
        Index index;
        float distance;
    };
    std::array<Node, PreparedCapacity> nodes_{};
    std::array<DrawEntry, OrdinaryCapacity> draw_{};
    Index activeHead_{None}, activeTail_{None}, freeHead_{None};
    std::size_t activeCount_{}, drawCount_{};
    float previousTime_{}, gravity_{};
    Vec3 camera_{};
    std::uint64_t generation_{};
    bool prepared_{}, busy_{};

    struct Operation
    {
        bool& busy;
        explicit Operation(bool& value) : busy(value) { busy = true; }
        ~Operation() { busy = false; }
    };
    void RebuildFree()
    {
        activeHead_ = activeTail_ = freeHead_ = None;
        activeCount_ = 0;
        // 01DE3BA0/01DE4F10 prepend nodes in vector order. Do not overwrite
        // payloads or reset the independent previous-time/camera globals.
        for (Index i = 0; i < nodes_.size(); ++i)
        {
            nodes_[i].next = freeHead_;
            nodes_[i].previous = None;
            if (freeHead_ != None) nodes_[freeHead_].previous = i;
            freeHead_ = i;
        }
    }
    void Release(Index index)
    {
        auto& node = nodes_[index];
        if (node.previous == None) activeHead_ = node.next;
        else nodes_[node.previous].next = node.next;
        if (node.next == None) activeTail_ = node.previous;
        else nodes_[node.next].previous = node.previous;
        node.previous = None;
        node.next = freeHead_;
        if (freeHead_ != None) nodes_[freeHead_].previous = index;
        freeHead_ = index;
        --activeCount_;
    }
    static int Sort(const void* a, const void* b)
    {
        return CompareDistance(static_cast<const DrawEntry*>(a)->distance,
                               static_cast<const DrawEntry*>(b)->distance);
    }

public:
    OrdinaryPool() = default;
    OrdinaryPool(const OrdinaryPool&) = delete;
    OrdinaryPool& operator=(const OrdinaryPool&) = delete;

    // Called during cold resource/owner preparation, before message/frame work.
    void Prepare()
    {
        if (!prepared_)
        {
            RebuildFree();
            prepared_ = true;
        }
    }
    void Clear()
    {
        ++generation_;
        RebuildFree();
        prepared_ = true;
    }
    Payload* Allocate(bool enabled)
    {
        // 01DE4D40 tests the caller's gate before acquiring the manager, and
        // ordinary allocation rejects active>=1200. With 10000 prepared nodes
        // the active-tail eviction branch is unreachable in this subset.
        if (!enabled || !prepared_ || busy_ || activeCount_ >= OrdinaryCapacity) return nullptr;
        const Index index = freeHead_;
        if (index == None) return nullptr;
        auto& node = nodes_[index];
        freeHead_ = node.next;
        if (freeHead_ != None) nodes_[freeHead_].previous = None;
        node.previous = None;
        node.next = activeHead_;
        if (activeHead_ == None) activeTail_ = index;
        else nodes_[activeHead_].previous = index;
        activeHead_ = index;
        ++activeCount_;
        return &node.payload;
    }

    // 01DE5050 always stores gravity; only a clock unequal to previousTime
    // updates this camera cache. SortOrigin below is a separate live getter.
    template<class Clock> bool SetEnvironment(float gravity, const Vec3& camera, Clock& clock)
    {
        if (busy_) return false;
        Operation operation(busy_);
        const auto generation = generation_;
        gravity_ = gravity;
        const float now = clock();
        if (generation != generation_) return false;
        if (now != previousTime_) camera_ = camera;
        return true;
    }

    // Calls: Time, Visible, SortOrigin, Position, Update, Die, Retire, Draw.
    // Callback payloads are borrowed until they return. No frame allocations
    // or resource lookup are owned by this component; the CRT owns qsort.
    template<class Callbacks> bool Frame(Callbacks& callbacks)
    {
        if (!prepared_ || busy_) return false;
        Operation operation(busy_);
        const auto generation = generation_;
        const float snapshot = callbacks.Time();
        if (generation != generation_) return false;
        drawCount_ = 0;
        for (Index index = activeHead_; index != None;)
        {
            auto& node = nodes_[index];
            auto& payload = node.payload;
            const bool visible = callbacks.Visible(payload);
            if (generation != generation_) return false;
            if (visible)
            {
                const Vec3 camera = callbacks.SortOrigin();
                if (generation != generation_) return false;
                const Vec3 position = callbacks.Position(payload);
                if (generation != generation_) return false;
                const float x = camera[0] - position[0];
                const float y = camera[1] - position[1];
                const float z = camera[2] - position[2];
                float distance = x * x + 0.f;
                distance = y * y + distance;
                distance = z * z + distance;
                draw_[drawCount_++] = {index, distance};
            }
            // Resample per particle, but pass the INITIAL snapshot to Update.
            // Equal time skips Update; a backwards clock still updates.
            const float sample = callbacks.Time();
            if (generation != generation_) return false;
            if (sample != previousTime_)
            {
                callbacks.Update(payload, snapshot);
                if (generation != generation_) return false;
            }
            const float die = callbacks.Die(payload);
            if (generation != generation_) return false;
            const Index next = node.next;
            if (die != 0.f && snapshot >= die)
            {
                callbacks.Retire(payload);
                if (generation != generation_) return false;
                Release(index);
            }
            index = next;
        }
        std::qsort(draw_.data(), drawCount_, sizeof(DrawEntry), Sort);
        for (std::size_t i = 0; i != drawCount_; ++i)
        {
            // Even a retired visible payload was already queued. Its own Draw
            // callback decides whether it actually submits geometry this pass.
            callbacks.Draw(nodes_[draw_[i].index].payload);
            if (generation != generation_) return false;
        }
        previousTime_ = snapshot;
        return true;
    }
    template<class Visitor> void VisitActive(Visitor& visitor) const
    {
        // Diagnostics only; the visitor must not reenter or mutate the owner.
        const auto generation = generation_;
        for (Index index = activeHead_; index != None;)
        {
            const auto& node = nodes_[index];
            visitor(node.payload);
            if (generation != generation_) return;
            index = node.next;
        }
    }
    std::size_t ActiveCount() const { return activeCount_; }
    std::size_t DrawCount() const { return drawCount_; }
    float PreviousTime() const { return previousTime_; }
    float Gravity() const { return gravity_; }
    const Vec3& Camera() const { return camera_; }
    std::uint64_t Generation() const { return generation_; }
};
} // namespace csol::particles
