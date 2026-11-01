// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "message_table.hpp"
#include <optional>
#include <set>

namespace csol::scenario
{
// Image names in ZBS_message omit the VGUI image extension and commonly use
// backslashes. Sound names are relative to sound/, as required by the engine.
std::optional<std::string> DialogueImagePath(std::string_view name);
std::optional<std::string> DialogueSoundPath(std::string_view name);
struct DialogueResources
{
    std::set<std::string> images;
    std::set<std::string> sounds;
};
// Inspect every event row for this map, without executing its world predicate.
// Use the same message-number parser and last-row lookup as live dispatch.
bool CollectDialogueResources(const WaveTable& waves, const MessageTable& messages,
    DialogueResources& output, ConfigError& error);
}
