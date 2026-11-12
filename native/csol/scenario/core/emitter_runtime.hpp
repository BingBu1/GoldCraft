// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace csol::particles
{
inline constexpr std::size_t TemporaryEmitterCapacity = 128;

// Typed host storage, not the original client's 0x168-byte engine ABI. Payload
// owns the proven producer fields; Model is an immutable host model reference.
template <class Payload, class Model = std::uintptr_t> struct TemporaryRecord
{
    std::uint32_t tag{};
    std::int32_t total{}, burst{}, remaining{}, active{};
    float interval{}, next{};
    Model model{};
    Payload payload{};
};

enum class EmitterPrepare { Emit, NextRecord, StopFrame };
enum class EmitterResult { Complete, Full, FactoryFailed, Stopped, Invalidated, Reentrant };

// One owner shared by supported TempEmit and MPToCL88 producers. It does not
// register a PEmitter receiver or infer unsupported entity/connection policy.
// Original client f4145136: Add02AA49B0, Think02AA6650, VidInit02AA7020.
template <class Payload, class Model = std::uintptr_t> class TemporaryPool
{
public:
    using Record = TemporaryRecord<Payload, Model>;
    static_assert(std::is_trivially_copyable_v<Record>);
    TemporaryPool() = default;
    TemporaryPool(const TemporaryPool &) = delete;
    TemporaryPool &operator=(const TemporaryPool &) = delete;

    // Init's temporary-record substep; registration/model records are separate.
    void Initialize() noexcept
    {
        ++epoch_;
        records_ = {};
    }

    // The emitter's own ResetHUD virtual slot is a record-preserving ret0.
    void ResetHud() const noexcept {}

    // Only the temporary-record substep of common VidInit. Its caller clears
    // the ordinary particle owner first; model records and resources follow
    // their separate original contract. Inactive bytes remain available.
    void ResetVideoRecords() noexcept
    {
        ++epoch_;
        for (auto &record : records_)
            if (record.active == 1)
            {
                record.tag = 0;
                record.total = record.remaining = record.active = 0;
            }
    }

    // Original02AA57F0: owner0 matches every owner in the requested group.
    // This clears only temporary records, never already allocated particles.
    // Owner is a side-effect-free typed accessor for the caller's payload.
    template<class Owner> EmitterResult StopTagged(std::uint32_t tag, std::uint16_t owner, Owner ownerOf)
    {
        if (adding_ || thinking_) return EmitterResult::Reentrant;
        Guard guard{thinking_};
        const auto epoch = ++epoch_;
        for (auto& record : records_)
            if (record.tag == tag && (!owner || ownerOf(record.payload) == owner))
            {
                if (epoch != epoch_) return EmitterResult::Invalidated;
                record.tag = 0;
                record.active = 0;
                record.remaining = 0;
                record.total = 0;
            }
        return EmitterResult::Complete;
    }

    // Host exposes LoadSprite(record), GetModel(handle), HudTime(), Overflow.
    // HudTime reads the Redraw cache AFTER resource callbacks, not Frame time.
    template <class Host> EmitterResult Add(const Record &input, Host &host)
    {
        if (adding_)
            return EmitterResult::Reentrant;
        Guard guard{adding_};
        const auto epoch = epoch_;
        for (auto &record : records_)
        {
            if (record.remaining != 0)
                continue;
            record = input;
            const auto sprite = host.LoadSprite(record);
            if (epoch != epoch_)
                return EmitterResult::Invalidated;
            const auto model = host.GetModel(sprite);
            if (epoch != epoch_)
                return EmitterResult::Invalidated;
            record.model = model;
            const float requested = input.next;
            const float cached = host.HudTime();
            if (epoch != epoch_)
                return EmitterResult::Invalidated;
            record.next = requested > cached ? requested : cached;
            record.remaining = record.total;
            record.active = 1;
            return EmitterResult::Complete;
        }
        host.Overflow(input);
        return epoch == epoch_ ? EmitterResult::Full : EmitterResult::Invalidated;
    }

    // Host exposes Time(), BadSprite(record), Prepare(record, sample), and
    // Create(record, sample). Prepare explicitly owns original owner checks;
    // no implicit entity validity or lifetime policy is supplied here.
    template <class Host> EmitterResult Frame(Host &host)
    {
        if (thinking_ || adding_)
            return EmitterResult::Reentrant;
        Guard guard{thinking_};
        const auto epoch = epoch_;
        const float now = host.Time();
        if (epoch != epoch_)
            return EmitterResult::Invalidated;
        for (auto &record : records_)
        {
            if (!(record.remaining > 0 || record.total == -1) ||
                !(now > record.next) || record.next == 0.f)
                continue;
            if (record.remaining > 0)
                --record.remaining;
            record.next = now + record.interval;
            if (!record.model)
            {
                host.BadSprite(record);
                if (epoch != epoch_)
                    return EmitterResult::Invalidated;
                continue;
            }
            const auto action = host.Prepare(record, now);
            if (epoch != epoch_)
                return EmitterResult::Invalidated;
            if (action == EmitterPrepare::StopFrame)
                return EmitterResult::Stopped;
            if (action == EmitterPrepare::NextRecord)
                continue;
            const auto burst = std::clamp(record.burst, 1, 100);
            for (std::int32_t i = 0; i != burst; ++i)
            {
                const bool created = host.Create(record, now);
                if (epoch != epoch_)
                    return EmitterResult::Invalidated;
                if (!created)
                    return EmitterResult::FactoryFailed;
            }
        }
        return EmitterResult::Complete;
    }

    // Main-thread, read-only diagnostics; visitors must not reenter/mutate.
    [[nodiscard]] std::span<const Record, TemporaryEmitterCapacity> Records() const noexcept
    {
        return records_;
    }

private:
    struct Guard
    {
        bool &flag;
        explicit Guard(bool &value) noexcept : flag(value) { flag = true; }
        ~Guard() { flag = false; }
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
    };
    std::array<Record, TemporaryEmitterCapacity> records_{};
    std::uint64_t epoch_{};
    bool adding_{}, thinking_{};
};
} // namespace csol::particles
