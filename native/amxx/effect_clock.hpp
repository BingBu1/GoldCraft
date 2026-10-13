#pragma once

#include <cstdint>
#include <limits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace goldcraft::effect_clock {

struct RawSample {
    std::int64_t counter = 0;
    std::int64_t frequency = 0;
    bool operator==(const RawSample&) const = default;
};
using Reader = bool (*)(RawSample&) noexcept;

enum class Error : std::uint32_t {
    none, read_failed, invalid_counter, invalid_frequency, frequency_changed,
    counter_regressed, conversion_overflow
};
struct Sample {
    std::uint64_t milliseconds = 0;
    std::uint64_t frequency = 0;
    std::uint32_t marginMs = 0;
    RawSample raw;
    bool operator==(const Sample&) const = default;
};

// floor(remainder * 1000 / divisor), where remainder < divisor <= INT64_MAX.
// Normal Windows frequencies take the single-division fast path. The fallback
// is exact on x86 without __int128 or a floating-point rounding dependency.
constexpr std::uint32_t fraction_ms(std::uint64_t remainder, std::uint64_t divisor) noexcept {
    if (remainder <= std::numeric_limits<std::uint64_t>::max() / 1000)
        return static_cast<std::uint32_t>(remainder * 1000 / divisor);
    std::uint64_t residue = 0;
    std::uint32_t quotient = 0;
    for (unsigned bit = 10; bit-- > 0;) {
        residue *= 2; // Less than 2 * divisor, so uint64 cannot overflow.
        quotient *= 2;
        if (residue >= divisor) { residue -= divisor; ++quotient; }
        if ((1000u >> bit) & 1u) {
            residue += remainder; // Again less than 2 * divisor.
            if (residue >= divisor) { residue -= divisor; ++quotient; }
        }
    }
    return quotient;
}

constexpr Error convert(RawSample raw, Sample& output) noexcept {
    if (raw.frequency <= 0) return Error::invalid_frequency;
    if (raw.counter < 0) return Error::invalid_counter;
    const auto frequency = static_cast<std::uint64_t>(raw.frequency);
    const auto counter = static_cast<std::uint64_t>(raw.counter);
    const auto whole = counter / frequency;
    if (whole > std::numeric_limits<std::uint64_t>::max() / 1000)
        return Error::conversion_overflow;
    const auto fraction = fraction_ms(counter % frequency, frequency);
    const auto base = whole * 1000;
    if (fraction > std::numeric_limits<std::uint64_t>::max() - base)
        return Error::conversion_overflow;
    // One ms covers flooring; two QPC periods cover the endpoint quantization
    // under the documented same-rate clock model. This is not a bound on a
    // remote clock, process suspension, transport timing or later revocation.
    const auto periods = 2000 / frequency + (2000 % frequency != 0);
    output = {base + fraction, frequency, static_cast<std::uint32_t>(1 + periods), raw};
    return Error::none;
}

#if defined(_WIN32)
inline bool read_windows_qpc(RawSample& output) noexcept {
    LARGE_INTEGER counter{}, frequency{};
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)) return false;
    output = {counter.QuadPart, frequency.QuadPart};
    return true;
}
#endif

// Both DLLs convert the same absolute QPC counter, never a DLL-local origin.
// Failure is latched for the clock object's lifetime: a map/plugin reload must
// not erase a real regression. Tests can inject a reader into a new object.
class Clock {
public:
    explicit Clock(Reader reader) noexcept : reader_(reader) {}
#if defined(_WIN32)
    Clock() noexcept : Clock(read_windows_qpc) {}
#endif
    Error read(Sample& output) noexcept {
        if (error_ != Error::none) return error_;
        RawSample raw{};
        if (!reader_ || !reader_(raw)) return error_ = Error::read_failed;
        return observe(raw, output);
    }
    // The host also observes adapter samples before accepting their derived
    // milliseconds, so raw regressions between DLL calls cannot hide in a ms.
    Error observe(RawSample raw, Sample& output) noexcept {
        if (error_ != Error::none) return error_;
        if (sampled_ && raw.frequency != last_.frequency) return error_ = Error::frequency_changed;
        if (sampled_ && raw.counter < last_.counter) return error_ = Error::counter_regressed;
        Sample converted{};
        const auto result = convert(raw, converted);
        if (result != Error::none) return error_ = result;
        last_ = raw;
        sampled_ = true;
        output = converted;
        return Error::none;
    }
    bool failed() const noexcept { return error_ != Error::none; }
    Error error() const noexcept { return error_; }
private:
    Reader reader_;
    RawSample last_{};
    Error error_ = Error::none;
    bool sampled_ = false;
};

constexpr std::uint32_t safe_remaining(std::uint32_t rawRemainingMs, const Sample& sample) noexcept {
    return sample.marginMs && rawRemainingMs > sample.marginMs ? rawRemainingMs - sample.marginMs : 0;
}

} // namespace goldcraft::effect_clock
