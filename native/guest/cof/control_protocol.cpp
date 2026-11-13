// SPDX-License-Identifier: GPL-3.0-or-later
#include "control_protocol.hpp"
namespace guest::cof
{
namespace
{
bool Valid(ControlData value) noexcept
{
    return value.operation == ControlOperation::WeaponBody || value.operation == ControlOperation::ShotState ||
        (value.operation == ControlOperation::WeaponDeploy && !value.body);
}
}
bool EncodeControl(ControlData value, ControlPacket& output) noexcept
{
    if (!Valid(value)) return false;
    output = {std::byte{ControlProtocol}, std::byte(value.operation), std::byte(value.body), std::byte{}};
    return true;
}
bool DecodeControl(std::span<const std::byte> bytes, ControlData& output) noexcept
{
    if (bytes.size() != ControlPacket{}.size() || bytes[0] != std::byte{ControlProtocol} ||
        bytes[3] != std::byte{}) return false;
    const ControlData value{ControlOperation(std::to_integer<std::uint8_t>(bytes[1])),
        std::to_integer<std::uint8_t>(bytes[2])};
    if (!Valid(value)) return false;
    output = value;
    return true;
}
}
