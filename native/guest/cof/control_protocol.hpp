// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace guest::cof
{
inline constexpr char ControlMessage[] = "CoFControl";
inline constexpr std::uint8_t ControlProtocol = 1;
enum class ControlOperation : std::uint8_t { WeaponDeploy = 1, WeaponBody, ShotState };
struct ControlData { ControlOperation operation; std::uint8_t body{}; };
using ControlPacket = std::array<std::byte, 4>;
bool EncodeControl(ControlData, ControlPacket&) noexcept;
bool DecodeControl(std::span<const std::byte>, ControlData&) noexcept;
}
