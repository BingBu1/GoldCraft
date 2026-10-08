#pragma once
#include <cstdint>

namespace goldcraft {
// SDL2's public cursor/mode API. Deliberately excludes the relative-delta
// getter: reading it here would steal mouse movement from the game client.
struct MouseCapture {
    int (*get_relative_mode)() = nullptr;
    int (*set_relative_mode)(int) = nullptr;
    int (*show_cursor)(int) = nullptr;
    std::uint64_t repairs = 0, errors = 0;

    bool available() const { return get_relative_mode && set_relative_mode && show_cursor; }

    void update(bool active_window, bool cursor_visible, bool raw_input) {
        if (!active_window || !available()) return;
        const bool relative = raw_input && !cursor_visible;
        // GoldSrc can change m_rawinput while its cursor remains hidden without
        // issuing the corresponding SDL_SetRelativeMouseMode call.
        if ((get_relative_mode() != 0) != relative) {
            if (set_relative_mode(relative ? 1 : 0) < 0) { ++errors; return; }
            ++repairs;
        }
        if (!relative) {
            const int shown = show_cursor(-1); // SDL_QUERY; does not read deltas.
            if (shown < 0) { ++errors; return; }
            if ((shown != 0) != cursor_visible) {
                if (show_cursor(cursor_visible ? 1 : 0) < 0) ++errors;
                else ++repairs;
            }
        }
    }
};
}
