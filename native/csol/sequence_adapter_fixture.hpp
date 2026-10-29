// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Synthetic model bytes are input fixtures, not a second implementation of
// the production loader, registry, selection or sampling checks.
namespace adapter_test
{
using Bytes=std::vector<std::byte>;
inline unsigned checks{};
inline void Require(bool condition,const char* message)
{++checks;if(!condition)throw std::runtime_error(message);}
struct HostError:std::runtime_error{using std::runtime_error::runtime_error;};
[[noreturn]] inline void HostFailure(const char* format,va_list args)
{char text[1024]{};std::vsnprintf(text,sizeof(text),format,args);throw HostError(text);}
template<class Callback> void Rejected(Callback callback,const char* message)
{
    try{callback();}catch(const HostError&){++checks;return;}
    throw std::runtime_error(message);
}
template<class T> void Put(Bytes& bytes,std::size_t offset,T value)
{
    if(offset>bytes.size() || sizeof(value)>bytes.size()-offset)throw std::runtime_error("bad fixture write");
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
inline unsigned Word(const Bytes& bytes,std::size_t offset)
{unsigned value{};std::memcpy(&value,bytes.data()+offset,4);return value;}
inline Bytes Model(bool tagged=true,bool textured=false,unsigned frames=3)
{
    constexpr unsigned bones=2,count=2,blends=2,tables=bones*blends*12,trackBytes=tables+8;
    const unsigned texture=244+bones*112,animation=texture+(textured?80:0);
    const unsigned records=animation+count*trackBytes,footer=records+count*32,descriptors=footer+32;
    const unsigned retained=descriptors+count*176;
    Bytes bytes(retained+(textured?32:0));
    Put(bytes,0,0x54534449u);Put(bytes,4,10u);Put(bytes,72,unsigned(bytes.size()));
    Put(bytes,140,bones);Put(bytes,144,244u);Put(bytes,164,count);Put(bytes,168,descriptors);
    Put(bytes,180,unsigned(textured));Put(bytes,184,textured?texture:0);Put(bytes,188,textured?retained:0);
    for(unsigned bone=0;bone<bones;++bone){Put(bytes,244+bone*112+32,-1);for(unsigned axis=0;axis<6;++axis)Put(bytes,244+bone*112+88+axis*4,0.01f);}
    for(unsigned i=0;i<count;++i)
    {
        const unsigned at=animation+i*trackBytes,descriptor=descriptors+i*176;
        Put(bytes,at+6,std::uint16_t(tables));bytes[at+tables]=std::byte{1};bytes[at+tables+1]=std::byte(frames);
        Put(bytes,at+tables+2,std::int16_t{123});bytes[at+tables+4]=bytes[at+tables+5]=std::byte{1};Put(bytes,at+tables+6,std::int16_t{321});
        Put(bytes,descriptor+56,frames);Put(bytes,descriptor+120,blends);Put(bytes,descriptor+124,at);
        const unsigned row[8]{at,trackBytes,frames,blends,0,bones,1,0};
        std::memcpy(bytes.data()+records+i*32,row,sizeof(row));
    }
    if(tagged)
    {
        const unsigned data[8]{0x51455343,0x31415444,1,count,bones,records,32,0};
        std::memcpy(bytes.data()+footer,data,sizeof(data));
    }
    if(textured)Put(bytes,texture+76,retained);
    return bytes;
}
inline unsigned Retained(const Bytes& bytes){return Word(bytes,184)?Word(bytes,188):Word(bytes,72);}
inline Bytes UnsupportedTrack()
{
    auto bytes=Model();const auto descriptors=Word(bytes,168),records=Word(bytes,descriptors-12);
    const auto animation=Word(bytes,descriptors+176+124);
    bytes[animation+48]=std::byte{0}; // A real incompatible run, not just a forged status.
    Put(bytes,records+32+24,0u);return bytes;
}
template<class T> T* At(Bytes& bytes,unsigned offset){return reinterpret_cast<T*>(bytes.data()+offset);}
}
