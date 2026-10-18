#pragma once
#include "goldcraft/wire.hpp"
#include <cmath>

namespace goldcraft::view_distortion {
// Presentation from the paired Minecraft client; never grants gameplay state.
struct Frame {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t serial=0,life=0;
    float intensity=0,angle=0;
};
inline Frame read(Reader& r) {
    Frame f{r.u64(),r.u64(),r.u32(),r.u32(),r.f32(),r.f32()};r.finish();
    if(!f.epoch||!f.revision||!f.serial||!f.life||!std::isfinite(f.intensity)||f.intensity<0||f.intensity>1
        ||!std::isfinite(f.angle)||std::abs(f.angle)>3.141593f)throw ProtocolError("Invalid view distortion");
    return f;
}
class Receiver {
    Frame frame_{};double time_=0;
public:
    void clear(){frame_={};time_=0;}
    bool accept(const Frame& f,std::uint64_t epoch,unsigned serial,unsigned life,double now){
        if(!epoch||f.epoch!=epoch||f.serial!=serial||f.life!=life||!std::isfinite(now))return false;
        if(frame_.epoch==f.epoch&&frame_.serial==f.serial&&frame_.life==f.life&&f.revision<=frame_.revision)return false;
        frame_=f;time_=now;return true;
    }
    const Frame* active(std::uint64_t epoch,unsigned serial,unsigned life,double now)const{
        return epoch&&frame_.epoch==epoch&&frame_.serial==serial&&frame_.life==life&&now>=time_&&now-time_<.35?&frame_:nullptr;
    }
};
// Minecraft 1.21.1 GameRenderer: P * R(a) * scale(1/stretch,1,1) * R(-a).
// R(a)'s first column is (cos(a), sin(a)/sqrt(2), -sin(a)/sqrt(2)).
// The rank-one form avoids intermediate matrices and preserves column-major P.
inline void apply(float* projection,float intensity,float angle) noexcept {
    if(intensity<=0)return;
    float stretch=5.f/(intensity*intensity+5.f)-intensity*.04f;
    stretch*=stretch;
    const float gain=1.f/stretch-1.f;
    const float v[3]={std::cos(angle),std::sin(angle)*.7071067811865475f,-std::sin(angle)*.7071067811865475f};
    for(unsigned row=0;row<4;++row){
        const float along=projection[row]*v[0]+projection[4+row]*v[1]+projection[8+row]*v[2];
        for(unsigned col=0;col<3;++col)projection[col*4+row]+=gain*v[col]*along;
    }
}
}
