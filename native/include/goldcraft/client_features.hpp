#pragma once
#include <cstdint>

namespace goldcraft::client_features {
// GoldSrc userinfo is only 256 bytes, including the server's Steam identity.
// Keep optional presentation/prediction capabilities in one compact field.
inline constexpr const char* key = "_gcf";
enum Feature : std::uint32_t {
    motion = 1, water = 2, nausea = 4, vision = 8, outline = 16, fluid = 32
};
template<class Read>
bool supports(Read read, Feature feature, const char* legacy) {
    const char* value = read(key);
    if (!value || !*value) {
        value = read(legacy);
        return value && value[0] == '1' && value[1] == '\0';
    }
    std::uint32_t bits = 0;
    for (unsigned digits = 0; *value; ++value, ++digits) {
        if (digits == 10 || *value < '0' || *value > '9') return false;
        const auto digit = static_cast<std::uint32_t>(*value - '0');
        if (bits > (UINT32_MAX - digit) / 10) return false;
        bits = bits * 10 + digit;
    }
    return (bits & feature) != 0;
}
}
