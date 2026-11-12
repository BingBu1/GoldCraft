// SPDX-License-Identifier: GPL-3.0-or-later
#include "crosshair_protocol.hpp"
#include <cmath>
namespace guest::cof
{
namespace
{
bool ValidSnapshot(const CrosshairSnapshot& value) noexcept
{return Valid(value.key)&&value.presentationRevision&&value.sequence&&value.scalar<=10;}
void Put(CrosshairPacket& out,std::size_t at,std::uint32_t value) noexcept
{for(unsigned i=0;i<4;++i)out[at+i]=std::byte(value>>(i*8));}
std::uint32_t Get(std::span<const std::byte> in,std::size_t at) noexcept
{
    std::uint32_t value{};
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(in[at+i])<<(i*8);
    return value;
}
}
bool QuantizeCrosshair(float scalar,std::uint8_t& result) noexcept
{
    if(!std::isfinite(scalar)||scalar<0||scalar>10)return false;
    result=static_cast<std::uint8_t>(scalar);
    return true;
}
bool EncodeCrosshair(const CrosshairSnapshot& value,CrosshairPacket& out) noexcept
{
    if(!ValidSnapshot(value))return false;
    CrosshairPacket bytes{};bytes[0]=std::byte{1};bytes[1]=std::byte{value.scalar};
    Put(bytes,2,value.key.sessionEpoch);Put(bytes,6,value.key.lifeEpoch);Put(bytes,10,value.key.generation);
    Put(bytes,14,value.presentationRevision);Put(bytes,18,value.sequence);out=bytes;return true;
}
bool DecodeCrosshair(std::span<const std::byte> bytes,CrosshairSnapshot& out) noexcept
{
    if(bytes.size()!=CrosshairBytes||bytes[0]!=std::byte{1})return false;
    const CrosshairSnapshot value{{Get(bytes,2),Get(bytes,6),Get(bytes,10)},Get(bytes,14),Get(bytes,18),
        std::to_integer<std::uint8_t>(bytes[1])};
    if(!ValidSnapshot(value))return false;
    out=value;return true;
}
void CrosshairReceiver::Reset() noexcept
{key_={};revision_=0;snapshot_={};active_=received_=false;}
void CrosshairReceiver::Present(AuthorityKey key,std::uint32_t revision,bool active) noexcept
{
    if(!active||!Valid(key)||!revision){Reset();return;}
    if(!active_||!(key==key_)||revision_!=revision){snapshot_={};received_=false;}
    key_=key;revision_=revision;active_=true;
}
CrosshairAcceptance CrosshairReceiver::Receive(std::span<const std::byte> bytes) noexcept
{
    CrosshairSnapshot next{};
    if(!DecodeCrosshair(bytes,next))return CrosshairAcceptance::Invalid;
    if(!active_)return CrosshairAcceptance::Inactive;
    if(!(next.key==key_))return CrosshairAcceptance::WrongAuthority;
    if(next.presentationRevision!=revision_)return CrosshairAcceptance::WrongPresentation;
    if(received_&&next.sequence<=snapshot_.sequence)return CrosshairAcceptance::Replayed;
    snapshot_=next;received_=true;return CrosshairAcceptance::Accepted;
}
}
