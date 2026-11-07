// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace csol::music
{
inline constexpr char MessageName[] = "CSOScnBgm";
inline constexpr std::size_t MaxPath = 160;
enum class Action : std::uint8_t { Stop, Fade, Play, Loop };
struct Track { Action action{}; std::string_view filename; };
enum class Cue { None, Ready, Round };
class Countdown
{
public:
    void Restart(float now) { m_start=now; m_ready=false; m_complete=false; }
    void Thaw(float now) { m_start=now; }
    Cue Tick(float now);
private:
    float m_start{};
    bool m_ready{},m_complete{};
};
// Original client switch has 39 entries. Entry25 reads a dynamic string and
// cannot be reconstructed from its numeric ID alone.
std::optional<Track> FindTrack(std::uint8_t id);
// The CSO engine resolves files below Bin/bgm. The host uses its downloaded
// game-relative bgm tree. No command fragments or escaping are accepted.
std::optional<std::string> ResourcePath(std::string_view filename);
struct Packet
{
    Action action{};
    std::uint32_t revision{};
    std::string path;
};
std::vector<std::uint8_t> Encode(const Packet& packet);
std::optional<Packet> Decode(std::span<const std::byte> bytes);
std::optional<std::string> Command(const Packet& packet);
}
