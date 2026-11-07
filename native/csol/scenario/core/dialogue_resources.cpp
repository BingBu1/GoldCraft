// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogue_resources.hpp"
#include "wave_events.hpp"
#include <algorithm>

namespace csol::scenario
{
namespace
{
std::optional<std::string> RelativePath(std::string_view name)
{
    if (name.empty() || name.size() > 240) return {};
    std::string path(name);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.front() == '/' || path.back() == '/') return {};
    std::size_t first{};
    while (first < path.size())
    {
        const auto end = path.find('/', first);
        const auto part = std::string_view(path).substr(first, end == path.npos ? end : end - first);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            return {};
        for (unsigned char c : part)
            if (c < 32 || c == 127 || c == ':' || c == '"' || c == '<' || c == '>' ||
                c == '|' || c == '?' || c == '*') return {};
        if (end == path.npos) break;
        first = end + 1;
    }
    return path;
}
}
std::optional<std::string> DialogueImagePath(std::string_view name)
{
    auto path = RelativePath(name);
    if (!path || !path->starts_with("resource/")) return {};
    const auto slash = path->rfind('/');
    const auto dot = path->rfind('.');
    if (dot == path->npos || dot < slash) *path += ".tga";
    else if (path->substr(dot) != ".tga" && path->substr(dot) != ".TGA") return {};
    return path;
}
std::optional<std::string> DialogueSoundPath(std::string_view name)
{
    auto path = RelativePath(name);
    if (!path || path->starts_with("sound/") ||
        (!path->ends_with(".wav") && !path->ends_with(".WAV"))) return {};
    return path;
}
bool CollectDialogueResources(const WaveTable& waves, const MessageTable& messages,
    DialogueResources& output, ConfigError& error)
{
    error = {};
    DialogueResources candidate;
    struct Event : WaveEventActions
    {
        std::uint16_t message{};
        bool HasSolidMonster(int) override { return true; }
        void BroadcastEvent(std::span<const std::uint8_t, 4> bytes) override
        { message = std::uint16_t(bytes[2] | (unsigned(bytes[3]) << 8)); }
    } event;
    for (const auto& row : waves.rows)
    {
        if (!row.IsEvent()) continue;
        if (DispatchWaveEvent(row, event, error) != WaveEventResult::Sent) return false;
        const auto* message = messages.Find(event.message);
        // The original client ignores a missing message-table ID.
        if (!message) continue;
        if (!message->image.empty() && message->image != " ")
        {
            auto path = DialogueImagePath(message->image);
            if (!path) { error = {row.recordIndex, "invalid scenario dialogue image path"}; return false; }
            candidate.images.insert(std::move(*path));
        }
        if (!message->sound.empty())
        {
            auto path = DialogueSoundPath(message->sound);
            if (!path) { error = {row.recordIndex, "invalid scenario dialogue sound path"}; return false; }
            candidate.sounds.insert(std::move(*path));
        }
    }
    output = std::move(candidate);
    return true;
}
}
