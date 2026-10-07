#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>
#include <deque>

namespace goldcraft {
struct CameraPose {Vec3 position{},angles{};float vertical_fov=70;std::uint32_t perspective=0;};
struct CameraFrame {std::uint64_t epoch,revision,produced;std::uint32_t life;CameraPose pose;};
inline CameraFrame read_camera(Reader& r){
    CameraFrame f;f.epoch=r.u64();f.revision=r.u64();f.produced=r.u64();f.life=r.u32();f.pose.perspective=r.u32();
    f.pose.position={r.f32(),r.f32(),r.f32()};f.pose.angles={r.f32(),r.f32(),r.f32()};f.pose.vertical_fov=r.f32();r.finish();
    if(!f.epoch||!f.revision||!f.produced||!f.life||f.pose.perspective>2||f.pose.vertical_fov<1||f.pose.vertical_fov>=179
       ||std::abs(f.pose.angles.x)>180||std::abs(f.pose.angles.y)>360||std::abs(f.pose.angles.z)>180)throw ProtocolError("Invalid Minecraft camera");
    return f;
}
// The producer already interpolates Minecraft ticks. Buffer just one 60 Hz
// presentation interval, hold on loss, and snap perspective/lifecycle changes.
class CameraInterpolator {
public:
    static constexpr double delay_seconds=1.0/60;
    void clear(){samples_.clear();offset_=0;}
    bool push(CameraPose pose,double produced,double received){
        if(!std::isfinite(produced)||!std::isfinite(received)||(!samples_.empty()&&produced<=samples_.back().time))return false;
        bool reset=samples_.empty();
        if(!reset){const auto& p=samples_.back();float x=pose.position.x-p.pose.position.x,y=pose.position.y-p.pose.position.y,z=pose.position.z-p.pose.position.z;
            reset=p.pose.perspective!=pose.perspective||produced-p.time>0.25||x*x+y*y+z*z>16;}
        if(reset){samples_.clear();offset_=received-produced;}else offset_=std::min(offset_,received-produced);
        samples_.push_back({produced,pose});while(samples_.size()>16)samples_.pop_front();return true;
    }
    CameraPose at(double now)const{
        if(samples_.empty())return {};
        const double target=now-offset_-delay_seconds;
        if(target<=samples_.front().time)return samples_.front().pose;
        for(std::size_t i=1;i<samples_.size();++i){
            const auto& a=samples_[i-1];const auto& b=samples_[i];if(target>b.time)continue;
            const auto t=static_cast<float>((target-a.time)/(b.time-a.time));
            auto lerp=[&](float x,float y){return std::lerp(x,y,t);};
            auto angle=[&](float x,float y){return x+std::remainder(y-x,360.0f)*t;};
            return {{lerp(a.pose.position.x,b.pose.position.x),lerp(a.pose.position.y,b.pose.position.y),lerp(a.pose.position.z,b.pose.position.z)},
                    {angle(a.pose.angles.x,b.pose.angles.x),angle(a.pose.angles.y,b.pose.angles.y),angle(a.pose.angles.z,b.pose.angles.z)},
                    lerp(a.pose.vertical_fov,b.pose.vertical_fov),b.pose.perspective};
        }
        return samples_.back().pose;
    }
private:
    struct Sample{double time;CameraPose pose;};
    std::deque<Sample> samples_;double offset_=0;
};
}
