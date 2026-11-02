// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wire {
enum { DT_FLOAT=1, DT_INTEGER=2, DT_SIGNED=4 };
struct Field { std::size_t offset; int type, bits; double scale; };
// Generated from the pinned ReGameDLL weapon_data_t delta block with the
// project's existing weapon ID/clip widening applied; commas are normalized.
// Fields absent from that block remain zero after a server snapshot.
#define DEFINE_DELTA(name,type,bits,scale) {offsetof(weapon_data_t,name),type,bits,scale}
inline const Field fields[]{
#include "delta_fields.inc"
};
#undef DEFINE_DELTA
inline weapon_data_t RoundTrip(const weapon_data_t& source)
{
    weapon_data_t result{};
    for (const auto& field : fields) {
        const auto* from=reinterpret_cast<const unsigned char*>(&source)+field.offset;
        auto* to=reinterpret_cast<unsigned char*>(&result)+field.offset;
        double value{};
        if(field.type&DT_FLOAT) { float f;std::memcpy(&f,from,sizeof(f));value=f; }
        else { std::int32_t i;std::memcpy(&i,from,sizeof(i));value=i; }
        // Same truncation/predivide as ReHLDS DELTA_WriteMarkedFields/ParseDelta;
        // these tests keep values in range and explicitly catch truncation.
        auto raw=std::int64_t(value*field.scale);
        const auto mask=(std::int64_t(1)<<(field.bits-((field.type&DT_SIGNED)?1:0)))-1;
        raw=raw<0?-((-raw)&mask):(raw&mask);
        value=double(raw)/field.scale;
        if(field.type&DT_FLOAT) { const float f=float(value);std::memcpy(to,&f,sizeof(f)); }
        else { const auto i=std::int32_t(value);std::memcpy(to,&i,sizeof(i)); }
    }
    return result;
}
}
