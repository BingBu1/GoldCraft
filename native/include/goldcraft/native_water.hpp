#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace goldcraft::native_water {
constexpr const char* message_name="GCWater";
constexpr const char* client_capability="_gc_water";
constexpr unsigned enabled=1,dolphins=2,slow_falling=4,sprinting=8,swimming=16,water_above=32;
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0,flags=0;
    float drag=.8f,speed=.1f,efficiency=0,swim=1,gravity=.08f;
};
inline State decode(std::span<const std::uint8_t> bytes){
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();s.flags=r.u32();
    s.drag=r.f32();s.speed=r.f32();s.efficiency=r.f32();s.swim=r.f32();s.gravity=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life||(s.flags&~63u)
        ||((s.flags&water_above)&&!(s.flags&swimming))
        ||!std::isfinite(s.drag)||s.drag<=0||s.drag>=1||!std::isfinite(s.speed)||s.speed<0||s.speed>1024
        ||!std::isfinite(s.efficiency)||s.efficiency<0||s.efficiency>1||!std::isfinite(s.swim)||s.swim<0||s.swim>1024
        ||!std::isfinite(s.gravity)||s.gravity<0||s.gravity>1)throw ProtocolError("Invalid native water state");
    return s;
}
class Receiver {
    State state_{};double received_=0;
public:
    void clear(){state_={};received_=0;}
    bool accept(const State& s,double now){
        if(s.epoch==state_.epoch&&s.serial==state_.serial&&s.revision<=state_.revision)return false;
        state_=s;received_=now;return true;
    }
    const State* active(std::uint64_t world,unsigned slot,unsigned serial,unsigned life,double now)const{
        return world&&state_.epoch==world&&state_.slot==slot&&state_.serial==serial&&state_.life==life
            &&now>=received_&&now-received_<.35?&state_:nullptr;
    }
};
struct Coefficients {float drag,acceleration,gravity;};
inline Coefficients coefficients(const State& s,bool grounded,bool falling){
    const float efficiency=s.efficiency*(grounded?1.f:.5f);
    float drag=(s.flags&sprinting)?.9f:s.drag;
    float acceleration=.02f;
    if(efficiency>0){drag+=(.54600006f-drag)*efficiency;acceleration+=(s.speed-acceleration)*efficiency;}
    if(s.flags&dolphins)drag=.96f;
    return {drag,acceleration*s.swim,(s.flags&slow_falling)&&falling?std::min(s.gravity,.01f):s.gravity};
}
// Fractional command durations compose the vanilla 20 Hz affine velocity law.
// At 50 ms the pre-move increment is exactly acceleration, followed by drag.
inline double acceleration_steps(float drag,float ticks){return double(drag)*std::expm1(-std::log(double(drag))*ticks)/(1-double(drag));}
inline float gravity_steps(float ticks){return -std::expm1(std::log(.8f)*ticks)/.2f;}

template<class PM> class MovementScope {
    using Vars=std::remove_pointer_t<decltype(PM::movevars)>;
    PM& pm_;const State* state_;MovementScope* previous_;
    inline static thread_local MovementScope* current_=nullptr;
public:
    MovementScope(PM& pm,const State* state):pm_(pm),state_(state),previous_(current_){
        for(auto* p=previous_;p;p=p->previous_)if(&p->pm_==&pm)state_=nullptr;
        current_=this;
    }
    ~MovementScope(){current_=previous_;}
    MovementScope(const MovementScope&)=delete;
    static bool water(void(*original)()){
        auto* scope=current_;if(!scope||!scope->state_||!(scope->state_->flags&enabled))return false;
        auto& p=scope->pm_;const auto& s=*scope->state_;
        if(p.dead||p.deadflag||p.spectator||p.iuser1||p.movetype!=3||(p.flags&((1<<24)|(1<<12)))
            ||p.waterlevel<2||p.watertype!=-3||p.waterjumptime||!p.movevars||p.maxspeed<=0
            ||p.frametime<=0||p.frametime>.255f)return false;
        constexpr float scale=units_per_block*20;
        const float ticks=p.frametime*20;
        if((s.flags&swimming)&&(p.forward[2]<=0||(p.cmd.buttons&2)||(s.flags&water_above))){
            const float blend=p.forward[2]<-.2f?.085f:.06f;
            const float target=p.forward[2]*scale;
            p.velocity[2]=target+(p.velocity[2]-target)*std::pow(1-blend,ticks);
        }
        const bool falling=p.velocity[2]<=0;
        const auto c=coefficients(s,p.onground!=-1,falling);
        float wish[3];float length2=0;
        for(int i=0;i<3;++i){wish[i]=(p.forward[i]*p.cmd.forwardmove+p.right[i]*p.cmd.sidemove+(i==2?p.cmd.upmove:0))/p.maxspeed;length2+=wish[i]*wish[i];}
        const float norm=length2>1?1/std::sqrt(length2):1;
        for(int i=0;i<3;++i){
            const float drag=i==2?.8f:c.drag;
            const double velocity=p.velocity[i]+double(wish[i])*norm*c.acceleration*scale*acceleration_steps(drag,ticks);
            p.velocity[i]=static_cast<float>(std::clamp(velocity,-double(p.movevars->maxvelocity),double(p.movevars->maxvelocity)));
        }
        // Let the original water mover do its stair/hull/sliding traces. Disable
        // only its force calculation; an idle command still takes the move path.
        const auto command=p.cmd;Vars* saved=p.movevars;Vars vars=*saved;
        vars.friction=vars.accelerate=0;p.movevars=&vars;
        p.cmd.forwardmove=p.cmd.sidemove=p.cmd.upmove=0;
        original();p.movevars=saved;p.cmd=command;
        // Native base velocity is removed by the caller; do not damp conveyors.
        for(int i=0;i<3;++i)p.velocity[i]=(p.velocity[i]-p.basevelocity[i])*std::pow(i==2?.8f:c.drag,ticks)+p.basevelocity[i];
        if(c.gravity!=0&&!(s.flags&sprinting)){
            const float vz=(p.velocity[2]-p.basevelocity[2])/scale;
            if(falling&&std::abs(vz-.005f)>=.003f&&std::abs(vz-c.gravity/16)<.003f)
                p.velocity[2]=p.basevelocity[2]-.003f*scale;
            else p.velocity[2]-=c.gravity/16*scale*gravity_steps(ticks);
        }
        return true;
    }
};
}
