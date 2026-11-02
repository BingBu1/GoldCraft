// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

namespace csol {

constexpr bool StudioSoundPathEqual(std::string_view left, std::string_view right)
{
    if (left.size() != right.size()) return false;
    const auto normalize = [](char c) {
        if (c == '\\') return '/';
        return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
    };
    for (std::size_t i = 0; i < left.size(); ++i)
        if (normalize(left[i]) != normalize(right[i])) return false;
    return true;
}

// The original model references this absent sample. The user explicitly
// chose to omit it after checking the current and backup NAR/PAK indexes.
// Keep download collection and event playback consistent, without changing
// the original model or replacing this cue with its separate hit sound.
constexpr bool SkipStudioSound(std::string_view model, int event, std::string_view sample)
{
    return event == 5004 && StudioSoundPathEqual(model, "models/v_dragonsword.mdl") &&
        StudioSoundPathEqual(sample, "weapons/dragonsword_stab.wav");
}

} // namespace csol
