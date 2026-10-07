#pragma once
#include "wire.hpp"
#include <algorithm>

namespace goldcraft {
// Every image is self-contained, so dropping queued older HUD frames is safe.
// RGBA rows run bottom to top, matching an OpenGL texture. A u16 token stores
// count - 1; its high bit indicates a repeated pixel instead of literal pixels.
inline Bytes read_hud_pixels(Reader& r, unsigned width, unsigned height) {
    if(!width||!height||width>4096||height>4096||std::uint64_t(width)*height>8388608)throw ProtocolError("HUD dimensions exceed budget");
    const std::size_t count=std::size_t(width)*height;
    Bytes result(count*4);
    std::size_t position=0;
    while(position<count) {
        const auto token=r.u16();const std::size_t run=(token&0x7fff)+1;
        if(run>count-position)throw ProtocolError("HUD pixel run exceeds image");
        if(token&0x8000) {
            const auto pixel=r.bytes(4);
            for(std::size_t i=0;i<run;i++)std::copy(pixel.begin(),pixel.end(),result.begin()+(position+i)*4);
        }else{
            const auto pixels=r.bytes(run*4);std::copy(pixels.begin(),pixels.end(),result.begin()+position*4);
        }
        position+=run;
    }
    r.finish();return result;
}
}
