#pragma once
#include "wire.hpp"
#include <array>
#include <cmath>

namespace goldcraft {
// A complete 5x5x5 section window has at most 512,000 block emitters.
// This is a transport validation bound, not a nearest-light rendering budget.
constexpr std::uint32_t max_scene_lights=524288;
struct SceneLight {
    std::array<float,3> origin;
    float radius;
    std::uint32_t color;
    bool operator==(const SceneLight&) const = default;
};
inline std::vector<SceneLight> read_scene_lights(Reader& r) {
    const auto count=r.u32();
    if(count>max_scene_lights||r.remaining()!=std::size_t(count)*20)
        throw ProtocolError("invalid light snapshot");
    std::vector<SceneLight> next;next.reserve(count);
    for(std::uint32_t i=0;i<count;++i){
        const Vec3 p{r.f32(),r.f32(),r.f32()};
        const auto position=to_goldsrc(p);
        const float radius=r.f32();const auto color=r.u32();
        if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)
            ||radius<=0||radius>768)throw ProtocolError("invalid scene light");
        next.push_back({{position.x,position.y,position.z},radius,color});
    }
    r.finish();return next;
}
}
