#include "../../native/amxx/effect_clock.hpp"
#include <iostream>
#include <limits>

namespace {
using namespace goldcraft::effect_clock;
int checks = 0, failures = 0;
void expect(bool value, const char* expression, int line) {
    ++checks;
    if (!value) { ++failures; std::cerr << line << ": " << expression << '\n'; }
}
#define CHECK(expression) expect(static_cast<bool>(expression), #expression, __LINE__)
RawSample next{0, 10000000};
bool succeeds = true;
int reads = 0;
bool reader(RawSample& value) noexcept { ++reads; if (!succeeds) return false; value = next; return true; }

void conversion_and_independent_epoch() {
    Sample result{999, 99, 9};
    const auto sentinel = result;
    CHECK(convert({1, 0}, result) == Error::invalid_frequency && result == sentinel);
    CHECK(convert({1, -1}, result) == Error::invalid_frequency && result == sentinel);
    CHECK(convert({-1, 1000}, result) == Error::invalid_counter && result == sentinel);
    CHECK(convert({std::numeric_limits<std::int64_t>::max(), 1}, result) == Error::conversion_overflow && result == sentinel);
    CHECK(convert({0, 10000000}, result) == Error::none && result.milliseconds == 0 && result.marginMs == 2);
    CHECK(convert({123456789012345, 10000000}, result) == Error::none && result.milliseconds == 12345678901ULL);
    CHECK(convert({std::numeric_limits<std::int64_t>::max() - 1, std::numeric_limits<std::int64_t>::max()}, result) == Error::none);
    CHECK(result.milliseconds == 999 && result.marginMs == 2); // Overflow-safe fractional fallback.
    CHECK(convert({std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max()}, result) == Error::none && result.milliseconds == 1000);
    // Quotient is near UINT64_MAX/1000; adding the fractional term must be
    // checked independently of multiplying the whole-second quotient.
    constexpr auto maxMs = std::numeric_limits<std::uint64_t>::max();
    constexpr auto whole = maxMs / 1000;
    CHECK(convert({static_cast<std::int64_t>(whole * 499 + 498), 499}, result) == Error::conversion_overflow);
    CHECK(convert({static_cast<std::int64_t>(whole * 499), 499}, result) == Error::none && result.milliseconds == whole * 1000);
    next = {90000000, 10000000}; succeeds = true;
    Clock first(reader), second(reader);
    Sample a{}, b{};
    CHECK(first.read(a) == Error::none && a.milliseconds == 9000);
    next.counter += 25000;
    CHECK(second.read(b) == Error::none && b.milliseconds == 9002);
    CHECK(first.read(a) == Error::none && a == b); // No per-object first-sample origin.
}

void raw_regression_frequency_and_failure_latch() {
    next = {15009, 1000000}; succeeds = true;
    Clock clock(reader);
    Sample value{};
    CHECK(clock.read(value) == Error::none && value.milliseconds == 15);
    const auto before = value;
    --next.counter; // Same floored millisecond, but raw counter regresses.
    CHECK(clock.read(value) == Error::counter_regressed && value == before);
    next.counter += 100000;
    const auto calls = reads;
    CHECK(clock.read(value) == Error::counter_regressed && reads == calls && value == before);
    Clock frequency(reader);
    CHECK(frequency.read(value) == Error::none);
    ++next.frequency;
    CHECK(frequency.read(value) == Error::frequency_changed);
    Clock failing(reader);
    succeeds = false;
    CHECK(failing.read(value) == Error::read_failed && failing.failed());
    succeeds = true;
    CHECK(failing.read(value) == Error::read_failed);
    Clock missing(nullptr);
    CHECK(missing.read(value) == Error::read_failed);
}

void conservative_deadline_bound() {
    // Exact integer oracle in microseconds, across low and normal frequencies.
    // For two quantized endpoints, safe duration must not outlive the native
    // floored deadline measured on the same-rate continuous model. This cannot
    // prove an unrelated JVM clock, suspension or authenticated remote grant.
    for (const std::int64_t hz : {1LL, 60LL, 1000LL, 1000000LL, 10000000LL}) {
        for (std::int64_t startUs = 15000; startUs < 17000; startUs += 47) {
            Sample start{};
            CHECK(convert({startUs * hz / 1000000, hz}, start) == Error::none);
            for (std::int64_t deltaUs : {0LL, 250LL, 1600LL, 15999LL}) {
                const auto sampleUs = startUs + deltaUs;
                Sample sampled{};
                CHECK(convert({sampleUs * hz / 1000000, hz}, sampled) == Error::none);
                const auto deadlineMs = start.milliseconds + 40;
                const auto raw = deadlineMs > sampled.milliseconds ? static_cast<std::uint32_t>(deadlineMs - sampled.milliseconds) : 0;
                const auto safe = safe_remaining(raw, sampled);
                CHECK(!safe || static_cast<std::uint64_t>(sampleUs) + safe * 1000ULL <= deadlineMs * 1000ULL);
                // Independent worst endpoint errors of one QPC period each,
                // compared in integer counter*1000 units (no float oracle).
                const auto sampleCounter = static_cast<std::uint64_t>(sampleUs * hz / 1000000);
                CHECK(!safe || (sampleCounter + 1) * 1000 + safe * static_cast<std::uint64_t>(hz)
                    <= deadlineMs * static_cast<std::uint64_t>(hz) - 1000);
            }
        }
    }
    Sample value{};
    CHECK(convert({15250, 1000000}, value) == Error::none && value.milliseconds == 15);
    CHECK(safe_remaining(17, value) == 15); // Old coarse-tick example returned 32.
    CHECK(safe_remaining(0, value) == 0 && safe_remaining(1, value) == 0 && safe_remaining(2, value) == 0);
    CHECK(safe_remaining(60000, Sample{}) == 0); // Unqualified sample cannot grant.
}
} // namespace

int main() {
    conversion_and_independent_epoch();
    raw_regression_frequency_and_failure_latch();
    conservative_deadline_bound();
    std::cout << "clock arithmetic/model: " << checks << " assertions, " << failures
              << " failures; no physical execution or remote-clock proof\n";
    return failures ? 1 : 0;
}
