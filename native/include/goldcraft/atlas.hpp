#pragma once
#include "wire.hpp"

namespace goldcraft {
struct AtlasPatch {
    unsigned x,y,width,height;
    std::span<const std::uint8_t> rgba;
};

// Validate the whole message before any GPU write, including x86 size overflow.
inline std::vector<AtlasPatch> read_atlas_patches(Reader& r,unsigned width,unsigned height) {
    const auto count=r.u32();
    if(count>1024)throw ProtocolError("animated atlas patch count exceeds budget");
    std::vector<AtlasPatch> patches;patches.reserve(count);
    std::uint64_t bytes=0;
    for(unsigned i=0;i<count;++i) {
        const auto x=r.u32(),y=r.u32(),w=r.u32(),h=r.u32();
        if(!w||!h||w>width||h>height||x>width-w||y>height-h)
            throw ProtocolError("animated atlas rectangle out of bounds");
        const std::uint64_t size=std::uint64_t(w)*h*4;
        bytes+=size;
        if(bytes>8*1024*1024)throw ProtocolError("animated atlas byte budget exceeded");
        patches.push_back({x,y,w,h,r.bytes(static_cast<std::size_t>(size))});
    }
    r.finish();return patches;
}
}
