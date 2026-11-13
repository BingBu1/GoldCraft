// SPDX-License-Identifier: GPL-3.0-or-later
#include "presentation_protocol.hpp"
#include "catalog.hpp"

#include <bit>

namespace guest::cof
{
namespace
{
bool ValidState(const PresentationState& value) noexcept
{
    return Valid(value.key) && value.revision &&
        (!value.originalWeapon || OriginalPistolId(value.originalWeapon)) &&
        (value.originalWeapon || !value.aimEnabled) &&
        (value.revolverAngle.has_value() == (value.originalWeapon == 21));
}

void Store(std::span<std::byte> output, std::size_t offset, std::uint32_t word, std::size_t bytes) noexcept
{
    for (std::size_t i = 0; i < bytes; ++i)
        output[offset + i] = static_cast<std::byte>((word >> (8 * i)) & 255U);
}

std::uint32_t Load(std::span<const std::byte> input, std::size_t offset, std::size_t bytes) noexcept
{
    std::uint32_t word{};
    for (std::size_t i = 0; i < bytes; ++i)
        word |= std::to_integer<std::uint32_t>(input[offset + i]) << (8 * i);
    return word;
}
void EncodeCommon(const PresentationState& state, std::span<std::byte> encoded) noexcept
{
    encoded[1] = static_cast<std::byte>(state.originalWeapon);
    encoded[2] = static_cast<std::byte>(state.aimEnabled ? 1 : 0);
    Store(encoded, 4, state.key.sessionEpoch, 4);
    Store(encoded, 8, state.key.lifeEpoch, 4);
    Store(encoded, 12, state.key.generation, 4);
    Store(encoded, 16, state.revision, 4);
    Store(encoded, 20, state.fovMode, 2);
    Store(encoded, 22, std::bit_cast<std::uint16_t>(state.zoomSpeedEighths), 2);
}
}

bool EncodeState(const PresentationState& state, StatePacket& output) noexcept
{
    if (!ValidState(state) || state.revolverAngle) return false;
    StatePacket encoded{};
    encoded[0] = static_cast<std::byte>(StateProtocol);
    EncodeCommon(state, encoded);
    output = encoded;
    return true;
}

bool EncodeState(const PresentationState& state, RevolverStatePacket& output) noexcept
{
    if (!ValidState(state) || !state.revolverAngle) return false;
    RevolverStatePacket encoded{};
    encoded[0] = static_cast<std::byte>(RevolverStateProtocol);
    EncodeCommon(state, encoded);
    Store(encoded, StateBytes, std::bit_cast<std::uint32_t>(*state.revolverAngle), 4);
    output = encoded;
    return true;
}

bool DecodeState(std::span<const std::byte> bytes, PresentationState& output) noexcept
{
    const bool revolver = bytes.size() == RevolverStateBytes;
    if ((!revolver && bytes.size() != StateBytes) ||
        bytes[0] != static_cast<std::byte>(revolver ? RevolverStateProtocol : StateProtocol) ||
        std::to_integer<unsigned>(bytes[2]) > 1 || bytes[3] != std::byte{} ||
        (bytes[1] == std::byte{21}) != revolver) return false;
    PresentationState decoded{
        {Load(bytes, 4, 4), Load(bytes, 8, 4), Load(bytes, 12, 4)}, Load(bytes, 16, 4),
        std::to_integer<std::uint8_t>(bytes[1]), bytes[2] == std::byte{1},
        static_cast<std::uint16_t>(Load(bytes, 20, 2)),
        std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(Load(bytes, 22, 2)))};
    if (revolver) decoded.revolverAngle = std::bit_cast<float>(Load(bytes, StateBytes, 4));
    if (!ValidState(decoded)) return false;
    output = decoded;
    return true;
}

void PresentationReceiver::Reset() noexcept
{
    current_ = {};
    initialized_ = false;
}

StateAcceptance PresentationReceiver::Receive(std::span<const std::byte> bytes,
                                               PresentationState& output) noexcept
{
    PresentationState next{};
    if (!DecodeState(bytes, next)) return StateAcceptance::Invalid;
    if (initialized_)
    {
        if (next.key.sessionEpoch < current_.key.sessionEpoch) return StateAcceptance::PreviousSession;
        if (next.key.sessionEpoch == current_.key.sessionEpoch)
        {
            if (next.key.lifeEpoch < current_.key.lifeEpoch) return StateAcceptance::PreviousLife;
            if (next.key.lifeEpoch == current_.key.lifeEpoch)
            {
                if (next.key.generation < current_.key.generation) return StateAcceptance::PreviousInstance;
                if (next.key.generation == current_.key.generation && next.revision <= current_.revision)
                    return StateAcceptance::Replayed;
            }
        }
    }
    current_ = next;
    initialized_ = true;
    output = next;
    return StateAcceptance::Accepted;
}
}
