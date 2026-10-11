#pragma once
#include <array>
#include <cstring>

namespace goldcraft::host_keys {
enum class Route { native, observe, consume };

inline bool console_binding(const char* binding) {
    return binding && (std::strcmp(binding, "toggleconsole") == 0 ||
        std::strcmp(binding, "showconsole") == 0 || std::strcmp(binding, "hideconsole") == 0);
}

// BaseUI observes +command presses; GoldSrc can skip it on key-up. The public
// engine Key_Event observer drains releases separately. HUD is the press
// fallback when the BaseUI hook is unavailable.
inline Route gameplay_route(const char* binding, bool base_ui, bool hooked) {
    if (console_binding(binding)) return Route::native;
    if (binding && std::strcmp(binding, "+use") == 0) return Route::native;
    if (binding && binding[0] == '+')
        return base_ui || !hooked ? Route::observe : Route::native;
    return Route::consume;
}

inline void modifier(unsigned& modifiers, unsigned bit, bool down) {
    if (down) modifiers |= bit;
    else modifiers &= ~bit;
}

// -1 means no edge. Only keyboard keys support repeat (GLFW action 2).
inline int action(std::array<bool, 256>& held, int key, bool down, bool mouse) {
    if (key < 0 || key >= int(held.size())) return -1;
    if ((!down || mouse) && held[key] == down) return -1;
    const int result = down ? (held[key] ? 2 : 1) : 0;
    held[key] = down;
    return result;
}
}
