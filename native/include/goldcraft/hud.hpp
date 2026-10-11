#pragma once
#include "wire.hpp"
#include <algorithm>

namespace goldcraft {
// Every image is self-contained, so dropping queued older HUD frames is safe.
// RGBA rows run bottom to top, matching an OpenGL texture. A u16 token stores
// count - 1; its high bit indicates a repeated pixel instead of literal pixels.
struct HudImage {
    Bytes rgba;
    // Empty storage represents a fully validated image whose RGBA bytes are all zero.
    bool blank() const noexcept { return rgba.empty(); }
};

inline HudImage read_hud_image(Reader& r, unsigned width, unsigned height) {
    if(!width||!height||width>4096||height>4096||std::uint64_t(width)*height>8388608)throw ProtocolError("HUD dimensions exceed budget");
    const std::size_t count=std::size_t(width)*height;
    HudImage result;
    std::size_t position=0;
    while(position<count) {
        const auto token=r.u16();const std::size_t run=(token&0x7fff)+1;
        if(run>count-position)throw ProtocolError("HUD pixel run exceeds image");
        if(token&0x8000) {
            const auto pixel=r.bytes(4);
            if(result.blank()&&std::any_of(pixel.begin(),pixel.end(),[](auto b){return b!=0;}))result.rgba.resize(count*4);
            if(!result.blank())for(std::size_t i=0;i<run;i++)std::copy(pixel.begin(),pixel.end(),result.rgba.begin()+(position+i)*4);
        }else{
            const auto pixels=r.bytes(run*4);
            if(result.blank()&&std::any_of(pixels.begin(),pixels.end(),[](auto b){return b!=0;}))result.rgba.resize(count*4);
            if(!result.blank())std::copy(pixels.begin(),pixels.end(),result.rgba.begin()+position*4);
        }
        position+=run;
    }
    r.finish();return result;
}

inline Bytes read_hud_pixels(Reader& r, unsigned width, unsigned height) {
    auto result=read_hud_image(r,width,height);
    if(result.blank())result.rgba.resize(std::size_t(width)*height*4);
    return std::move(result.rgba);
}
}
