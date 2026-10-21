// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>

namespace csol
{
// Explicit little-endian fields: no native struct layout on the wire.
inline uint32_t PresentationLE(std::span<const uint8_t> data, size_t at, size_t bytes)
{
    uint32_t value{};for(size_t i=0;i<bytes;++i)value|=uint32_t(data[at+i])<<(8*i);return value;
}
struct PartPresentation
{
    uint32_t Model{};uint16_t Sequence{},Skin{},Body{},Ammo{},Maximum{},Flags{};uint8_t Slot{};
};
inline std::optional<PartPresentation> ParsePartPresentation(std::span<const uint8_t> data)
{
    if(data.empty())return {};
    const size_t modelBytes=data[0]==1?2:data[0]==2?4:0;
    if(!modelBytes || data.size()!=14+modelBytes || data[1]>=2)return {};
    const size_t at=2+modelBytes;
    PartPresentation result{PresentationLE(data,2,modelBytes),
        uint16_t(PresentationLE(data,at,2)),uint16_t(PresentationLE(data,at+2,2)),
        uint16_t(PresentationLE(data,at+4,2)),uint16_t(PresentationLE(data,at+6,2)),
        uint16_t(PresentationLE(data,at+8,2)),uint16_t(PresentationLE(data,at+10,2)),data[1]};
    if(!result.Model || result.Model>0x7fffffff || !result.Maximum || result.Ammo>result.Maximum || (result.Flags&~1u))return {};
    return result;
}
struct ViewPresentation {uint32_t Model{},Body{};uint16_t Skin{};};
inline std::optional<ViewPresentation> ParseViewPresentation(std::span<const uint8_t> data)
{
    if(data.size()!=11 || data[0]!=1)return {};
    ViewPresentation result{PresentationLE(data,1,4),PresentationLE(data,7,4),uint16_t(PresentationLE(data,5,2))};
    if(!result.Model || result.Model>0x7fffffff || result.Body>0x7fffffff)return {};
    return result;
}
// The entity update may arrive after the reliable message. Retain only one
// pending value per slot, then bind the accepted state to the current model.
template<class T> struct PendingPresentation
{
    std::optional<T> Value;double Time{};
    void Set(T value,double time){Value=value;Time=time;}
    std::optional<T> Take(uint32_t model,double now)
    {
        if(Value && (now<Time || now-Time>2.0))Value.reset();
        if(!Value || Value->Model!=model)return {};
        auto result=Value;Value.reset();return result;
    }
};
}
