// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "authority.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace guest::cof
{
inline constexpr char CrosshairMessage[]="CoFCrosshair";
inline constexpr std::size_t CrosshairBytes=22;
struct CrosshairSnapshot
{
    AuthorityKey key{};
    std::uint32_t presentationRevision{}, sequence{};
    std::uint8_t scalar{}; // Original signed-float delta, pre/postmultiply1.
    bool operator==(const CrosshairSnapshot&) const noexcept=default;
};
using CrosshairPacket=std::array<std::byte,CrosshairBytes>;
// Reachable original Glock spread is finite0..10. Preserve original truncation
// before sending; native CS clientdata's ammunition fields remain untouched.
bool QuantizeCrosshair(float scalar,std::uint8_t& result) noexcept;
bool EncodeCrosshair(const CrosshairSnapshot&,CrosshairPacket&) noexcept;
bool DecodeCrosshair(std::span<const std::byte>,CrosshairSnapshot&) noexcept;
enum class CrosshairAcceptance { Accepted, Invalid, Inactive, WrongAuthority, WrongPresentation, Replayed };
class CrosshairReceiver
{
public:
    void Present(AuthorityKey,std::uint32_t revision,bool active) noexcept;
    void Reset() noexcept;
    CrosshairAcceptance Receive(std::span<const std::byte>) noexcept;
    const CrosshairSnapshot* Current() const noexcept {return received_?&snapshot_:nullptr;}
private:
    AuthorityKey key_{};
    std::uint32_t revision_{};
    CrosshairSnapshot snapshot_{};
    bool active_{},received_{};
};
}
