// SPDX-License-Identifier: GPL-3.0-or-later
// Offline download closure from the same tables and path rules as the DLL.
// Binary stdin avoids Windows command-line encoding of the common directory.
#include "config_codec.hpp"
#include "scenario_round_hud.hpp"
#include "scenario/dialogue_resources.hpp"
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <set>
#include <vector>

namespace
{
bool ReadBytes(std::vector<std::byte>& output)
{
    unsigned char sizeBytes[4]{};
    if (!std::cin.read(reinterpret_cast<char*>(sizeBytes), sizeof sizeBytes)) return false;
    const std::uint32_t size = std::uint32_t(sizeBytes[0]) | (std::uint32_t(sizeBytes[1]) << 8) |
        (std::uint32_t(sizeBytes[2]) << 16) | (std::uint32_t(sizeBytes[3]) << 24);
    if (!size || size > 8 * 1024 * 1024) return false;
    output.resize(size);
    return bool(std::cin.read(reinterpret_cast<char*>(output.data()), size));
}
}

int main()
{
    _setmode(_fileno(stdin), _O_BINARY);
    std::vector<std::byte> waveBytes, messageBytes;
    if (!ReadBytes(waveBytes) || !ReadBytes(messageBytes) || std::cin.peek() != std::char_traits<char>::eof())
    { std::cerr << "Expected length-prefixed wave and message resources\n"; return 1; }
    if (!csol::DecodeConfiguration(waveBytes))
    { std::cerr << "Invalid encrypted scenario wave table\n"; return 1; }
    while (!waveBytes.empty() && waveBytes.back() == std::byte{}) waveBytes.pop_back();
    csol::scenario::ConfigError error;
    csol::scenario::WaveTable waves;
    csol::scenario::MessageTable messages;
    csol::scenario::DialogueResources dialogue;
    if (!csol::scenario::ReadWaves(
            {reinterpret_cast<const char*>(waveBytes.data()), waveBytes.size()}, waves, error) ||
        !csol::scenario::ReadMessageTableResource(messageBytes, messages, error) ||
        !csol::scenario::CollectDialogueResources(waves, messages, dialogue, error))
    { std::cerr << error.message << '\n'; return 1; }
    std::set<std::string> resources{
        csol::scenario_hud::MessagePath, csol::scenario_hud::LocalizationPath, csol::scenario_hud::FontPath};
    for (const auto& resource : csol::scenario_hud::Resources) resources.emplace(resource.path);
    resources.insert(dialogue.images.begin(), dialogue.images.end());
    for (const auto& sound : dialogue.sounds) resources.emplace("sound/" + sound);
    for (const auto& resource : resources) std::cout << resource << '\n';
    return std::cout ? 0 : 1;
}
