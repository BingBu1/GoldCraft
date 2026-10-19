#pragma once
#include <array>
#include <cstring>
#include <string_view>

namespace goldcraft::userinfo {
// These are storage sizes, including the trailing NUL. Keep the engine's
// original 256-byte members and the ReAPI client_t offsets unchanged.
inline constexpr std::size_t capacity = 1024;
inline constexpr std::size_t legacy_capacity = 256;
inline constexpr std::size_t identity_reserve = 32;
inline constexpr std::string_view capability = "_gcu";

inline bool token(std::string_view text) {
    if (text.find("..") != text.npos) return false;
    for (unsigned char c : text) if (c < 32 || c == 127 || c == '\\' || c == '"') return false;
    return true;
}

template<class F> bool fields(std::string_view info, F visit) {
    while (!info.empty()) {
        if (info.front() != '\\') return false;
        info.remove_prefix(1);
        const auto end = info.find('\\');
        if (end == info.npos || end == 0) return false;
        const auto key = info.substr(0, end);
        info.remove_prefix(end + 1);
        const auto next = info.find('\\');
        const auto value = info.substr(0, next);
        if (value.empty() || key.size() >= 127 || value.size() >= 127 || !token(key) || !token(value)) return false;
        visit(key, value);
        if (next == info.npos) return true;
        info.remove_prefix(next);
    }
    return true;
}

inline std::string_view value(std::string_view info, std::string_view key) {
    std::string_view result;
    fields(info, [&](auto k, auto v) { if (k == key) result = v; });
    return result;
}

inline bool append(char* dest, std::size_t limit, std::string_view key, std::string_view val) {
    const auto used = std::strlen(dest);
    if (used + key.size() + val.size() + 2 >= limit) return false;
    auto* out = dest + used;
    *out++ = '\\';
    std::memcpy(out, key.data(), key.size()); out += key.size();
    *out++ = '\\';
    std::memcpy(out, val.data(), val.size()); out += val.size();
    *out = 0;
    return true;
}

// Build complete pairs, never a truncated tail. Authentication/identity and
// prerequisites for resource decoding precede optional gameplay fields.
inline void project(char* dest, std::size_t limit, std::string_view info) {
    dest[0] = 0;
    constexpr std::string_view first[] = {
        "name", "password", "_pw", "*sid", "*hltv", "gc_precache", "_gcpe",
        "gc_brushes", "_gcf", "_gcu", "model", "rate", "cl_updaterate",
        "cl_lw", "cl_lc", "topcolor", "bottomcolor", "_vgui_menus"
    };
    for (auto key : first) {
        auto val = value(info, key);
        if (!val.empty()) append(dest, limit, key, val);
    }
    fields(info, [&](auto key, auto val) {
        for (auto priority : first) if (priority == key) return;
        append(dest, limit, key, val);
    });
}

inline bool fits(std::string_view info, std::string_view key,
                 std::string_view val, std::size_t limit = capacity) {
    std::size_t used = 0;
    const bool valid = fields(info, [&](auto k, auto v) {
        if (k != key) used += k.size() + v.size() + 2;
    });
    return valid && used + (val.empty() ? 0 : key.size() + val.size() + 2) < limit;
}

struct Storage {
    std::array<char, capacity> full{};
    std::array<char, legacy_capacity> projection{};

    bool assign(std::string_view info) {
        if (info.size() >= capacity || !fields(info, [](auto, auto) {})) return false;
        std::memmove(full.data(), info.data(), info.size());
        full[info.size()] = 0;
        project(projection.data(), projection.size(), full.data());
        return true;
    }
    void publish(char* legacy, std::size_t limit = legacy_capacity) {
        project(projection.data(), limit, full.data());
        std::memcpy(legacy, projection.data(), projection.size());
    }
    // Plugins which edit the legacy member directly retain their ABI. Apply
    // their changes/deletions to projected keys without losing overflow keys.
    void reconcile(const char* legacy) {
        if (!std::memchr(legacy, 0, legacy_capacity) || std::strcmp(legacy, projection.data()) == 0) return;
        std::array<char, capacity> merged{};
        if (!fields(legacy, [](auto, auto) {})) return;
        fields(full.data(), [&](auto k, auto v) {
            if (value(projection.data(), k).empty() && value(legacy, k).empty())
                append(merged.data(), merged.size(), k, v);
        });
        bool accepted = true;
        fields(legacy, [&](auto k, auto v) { accepted &= append(merged.data(), merged.size(), k, v); });
        if (accepted) assign(merged.data());
    }
};
} // namespace goldcraft::userinfo
