// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace texture_fixture {
using Bytes=std::vector<std::byte>;
inline int Word(std::span<const std::byte> bytes,size_t at) {
    if(at>bytes.size() || bytes.size()-at<4)throw std::runtime_error("fixture word outside file");
    int value{};memcpy(&value,bytes.data()+at,4);return value;
}
inline void Put(Bytes& bytes,size_t at,int value) {
    if(at>bytes.size() || bytes.size()-at<4)throw std::runtime_error("fixture write outside file");
    memcpy(bytes.data()+at,&value,4);
}
struct SplitModel {Bytes model,textures;};
// These actual stock samples place all texture records, skins and images after
// geometry. Cut that tail out of the main file, as a split-model compiler does.
inline SplitModel Split(const Bytes& source) {
    const int start=Word(source,184),count=Word(source,180);
    if(start<244 || start>=source.size() || count<1 || Word(source,200)<start || Word(source,188)<start)
        throw std::runtime_error("fixture expects a trailing texture section");
    SplitModel out{{source.begin(),source.begin()+start},Bytes(244)};
    Put(out.textures,0,0x54534449);Put(out.textures,4,10);
    out.textures.insert(out.textures.end(),source.begin()+start,source.end());
    for(int field:{180,192,196})Put(out.textures,field,Word(source,field));
    for(int field:{184,188,200})Put(out.textures,field,Word(source,field)-start+244);
    for(int i=0;i<count;++i)Put(out.textures,244+i*80+76,Word(source,start+i*80+76)-start+244);
    Put(out.textures,72,int(out.textures.size()));
    for(int field=180;field<=200;field+=4)Put(out.model,field,0);
    Put(out.model,72,int(out.model.size()));return out;
}
}
