// SPDX-License-Identifier: GPL-3.0-or-later
#include "events.hpp"
#include <charconv>
#include <cmath>
#include <cctype>
#include <stdexcept>
namespace csol {
std::string SpriteEvent::ResourceName() const { return "sprites/muzzleflash" + std::to_string(Resource) + ".spr"; }
double SpriteEvent::Duration(int frames) const { return Animate && !Loop && Life >= 998 ? frames / FPS : Life; }
std::optional<SpriteEvent> ParseSpriteEvent(int id, std::string_view options)
{
    if (!options.starts_with('#')) return {};
    if (id < 5000 || id >= 5640) throw std::runtime_error("invalid effect event attachment");
    SpriteEvent event; event.Attachment = (id - 5000) / 10;
    size_t at = 1;
    while (at < options.size())
    {
        if (std::isspace((unsigned char)options[at])) { ++at; continue; }
        const char key = options[at++];
        if (key < 'A' || key > 'Z') throw std::runtime_error("invalid effect token");
        float n = 0;
        const size_t start = at;
        if (at < options.size() && (options[at] == '+' || options[at] == '-')) ++at;
        const size_t integer = at;
        while (at < options.size() && options[at] >= '0' && options[at] <= '9') ++at;
        bool digits = at != integer;
        if (at < options.size() && options[at] == '.')
        {
            ++at;
            const size_t fraction = at;
            while (at < options.size() && options[at] >= '0' && options[at] <= '9') ++at;
            digits |= at != fraction;
        }
        if (!digits) at = start;
        const auto* begin = options.data() + start + (digits && options[start] == '+' ? 1 : 0);
        const auto parsed = std::from_chars(begin, options.data() + at, n, std::chars_format::fixed);
        const bool hasNumber = digits && parsed.ec == std::errc{} && parsed.ptr == options.data() + at;
        if (key == 'U') { event.Overlay = true; continue; }
        if (!hasNumber || !std::isfinite(n) || std::abs(n) > 1000000) throw std::runtime_error("invalid effect value");
        switch (key)
        {
        case 'I': event.Resource = int(n); break;
        case 'E': event.Alternate = int(n); break;
        case 'S': event.Scale = n; break;
        case 'R': event.Rotation = int(n); break;
        case 'F': event.RandomFrame = int(n) != 0; break;
        case 'P': event.FPS = int(n); break;
        case 'T': event.Life = n; break;
        case 'A': event.Animate = int(n) != 0; break;
        case 'L': event.Loop = int(n) != 0; break;
        case 'X': event.Follow = int(n); break;
        case 'D': event.Alpha = int(n) != 0; break;
        case 'H': event.HandFlip = int(n) != 0; break;
        case 'Q': event.SequenceOnly = int(n) != 0; break;
        case 'O': break;
        default: event.Notices += key; break;
        }
    }
    if (event.Resource < 1 || event.Resource > 65535 || event.Alternate < 0 || event.Alternate > 65535 ||
        event.Follow < -1 || event.Follow > 63 || event.Scale <= 0 || event.Scale > 1024 ||
        event.FPS <= 0 || event.FPS > 10000 || event.Life < 0 || event.Life > 86400)
        throw std::runtime_error("effect parameters out of range");
    return event;
}
}
