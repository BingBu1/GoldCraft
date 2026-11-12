// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/forward_displacement.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
using namespace guest::cof::client;
std::uint32_t MemoryWord(const void* pointer) noexcept
{
    const auto* bytes=static_cast<const volatile unsigned char*>(pointer);
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|
        (std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
int main()
{
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    std::array<std::uint32_t,8> row;
    while(std::fread(row.data(),sizeof(row),1,stdin)==1) {
        ForwardDisplacementState state;
        std::memcpy(&state.value,row.data()+1,4);
        std::array<float,3> origin,forward;
        // Original fields arrive as memory words. Returning an sNaN float
        // through an x87 helper would quiet it before the production function.
        std::memcpy(origin.data(),row.data()+2,12);std::memcpy(forward.data(),row.data()+5,12);
        if(row[0])ResetForwardDisplacement(state);
        LimitForwardDisplacement(state);ApplyForwardDisplacement(state,std::span<float,3>{origin},forward);
        std::array<std::uint32_t,5> out{MemoryWord(&state.value)};
        for(unsigned i=0;i<3;++i)out[i+1]=std::bit_cast<std::uint32_t>(origin[i]);
        ResetForwardDisplacement(state);out[4]=std::bit_cast<std::uint32_t>(state.value);
        if(std::fwrite(out.data(),sizeof(out),1,stdout)!=1)return 2;
    }
}
