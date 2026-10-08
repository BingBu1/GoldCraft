#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cstddef>
#include <cstring>

namespace goldcraft { namespace vitals {
constexpr char capability[] = "_gcvitals";
constexpr char version[] = "1";

// GoldSrc gameplay retains float health/armor. Only the HUD wire value is an
// integer; avoid out-of-range float-to-int conversion and preserve living HP.
inline std::int32_t integer(float value, bool health) {
    if (!(value > 0)) return 0;
    if (double(value) >= double((std::numeric_limits<std::int32_t>::max)()))
        return (std::numeric_limits<std::int32_t>::max)();
    return (std::max<std::int32_t>)(health ? 1 : 0, static_cast<std::int32_t>(value));
}

// The separate legacy clientdata health delta is a float encoded through a
// signed-int conversion (normally a 10-bit field). Avoid conversion overflow
// at 2^31, which otherwise makes a living player look dead. Keep the actual
// server health and the full LONG HUD value untouched.
inline float prediction_health(float value) {
    if (!(value > 0)) return 0;
    return (std::min)(value, 2147483520.0f); // Largest float below 2^31.
}

inline bool valid_payload(const std::uint8_t* bytes, std::size_t size, unsigned legacy_size, bool player) {
    const auto extended_size = player ? 5u : 4u;
    if (!bytes || (size != legacy_size && size != extended_size)) return false;
    if (size == extended_size) {
        std::int32_t value;
        std::memcpy(&value, bytes, sizeof(value));
        if (value < 0) return false;
    }
    return !player || (bytes[size - 1] >= 1 && bytes[size - 1] <= 32);
}
}}
