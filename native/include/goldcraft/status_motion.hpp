#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace goldcraft::status_motion {
constexpr const char* message_name="GCMotion";
constexpr const char* client_capability="_gc_motion";
constexpr unsigned slow_falling=1, levitation=2, dolphins_grace=4;
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0,flags=0;
    float jump=0,levitation_target=0;
};
inline State decode(std::span<const std::uint8_t> bytes){
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();
    s.flags=r.u32();s.jump=r.f32();s.levitation_target=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life||(s.flags&~7u)
        ||!std::isfinite(s.jump)||s.jump<0||s.jump>25.6f||!std::isfinite(s.levitation_target)
        ||s.levitation_target<0||s.levitation_target>12.8f
        ||bool(s.flags&levitation)!=(s.levitation_target>0))throw ProtocolError("Invalid native motion state");
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
    const State* active(std::uint64_t world,unsigned slot,unsigned serial,unsigned life,double now) const {
        return world&&state_.epoch==world&&state_.slot==slot&&state_.serial==serial&&state_.life==life
            &&now>=received_&&now-received_<.35?&state_:nullptr;
    }
};
// Fractional command durations retain the vanilla 20Hz affine velocity law.
// GoldSrc still performs both half-gravity steps and every hull collision.
inline float vertical_target(float velocity,float gravity,float dt,const State& s){
    constexpr float speed_scale=units_per_block*20;
    const float ticks=20*dt;
    if(s.flags&levitation){
        const float equilibrium=(.196f/.216f)*s.levitation_target*speed_scale;
        return equilibrium+(velocity-equilibrium)*std::pow(.784f,ticks);
    }
    const float g=std::min(std::max(0.f,gravity),.01f*units_per_block*400);
    const float equilibrium=-.98f*(g/20)/.02f;
    return equilibrium+(velocity-equilibrium)*std::pow(.98f,ticks);
}
template<class PM> class MovementScope {
    using Vars=std::remove_pointer_t<decltype(PM::movevars)>;
    PM& pm_;const State* state_;Vars* saved_;Vars vars_{};float gravity_;
    MovementScope* previous_;bool restore_=false,prepared_=false,vertical_=false,rising_=false;
    inline static thread_local MovementScope* current_=nullptr;
    bool walking()const{return state_&&!pm_.dead&&!pm_.deadflag&&!pm_.spectator&&!pm_.iuser1
        &&pm_.movetype==3&&!(pm_.flags&((1<<24)|(1<<12)))&&pm_.frametime>0&&pm_.frametime<=.255f;}
    void categorized(float before_z){
        if(!walking()||pm_.waterlevel>=2||pm_.waterjumptime)return;
        if(!prepared_){
            prepared_=true;
            vertical_=(state_->flags&levitation)||((state_->flags&slow_falling)&&pm_.velocity[2]<=0);
            if(vertical_){
                const float gravity=(gravity_==0?1:gravity_)*saved_->gravity;
                const float target=vertical_target(pm_.velocity[2],gravity,pm_.frametime,*state_);
                vars_.gravity=(pm_.velocity[2]-target)/pm_.frametime;pm_.gravity=1;
                rising_=(state_->flags&levitation)&&target>0;
            }
        }
        // The native two-unit downward support snap must not pin a rising
        // levitating player to the floor. Actual movement traces remain intact.
        if(rising_&&pm_.velocity[2]>=0&&pm_.onground!=-1){pm_.origin[2]=before_z;pm_.onground=-1;}
        if(vertical_)pm_.flFallVelocity=0;
    }
public:
    MovementScope(PM& pm,const State* state):pm_(pm),state_(state),saved_(pm.movevars),gravity_(pm.gravity),previous_(current_){
        // Reentrant movement must use its own state, never the outer command's
        // temporary movevars. No permanent player/usercmd field is reserved.
        const Vars* base=saved_;float base_gravity=gravity_;bool reentrant=false;
        for(auto* p=previous_;p;p=p->previous_)if(&p->pm_==&pm){
            state_=nullptr;reentrant=true;base=p->saved_;base_gravity=p->gravity_;
        }
        if((state_||reentrant)&&base){
            vars_=*base;pm_.movevars=&vars_;restore_=true;
            if(reentrant)pm_.gravity=base_gravity;
        }else state_=nullptr;
        current_=this;
    }
    ~MovementScope(){if(restore_){pm_.movevars=saved_;pm_.gravity=gravity_;}current_=previous_;}
    MovementScope(const MovementScope&)=delete;
    static void categorize(void(*original)()){
        auto* s=current_;const float z=s?s->pm_.origin[2]:0;original();if(s)s->categorized(z);
    }
    static void jump(void(*original)()){
        auto* s=current_;const bool eligible=s&&s->walking()&&s->pm_.waterlevel<2&&!s->pm_.waterjumptime&&s->pm_.onground!=-1;
        if(eligible&&s->vertical_&&!(s->state_->flags&levitation)){
            s->vars_.gravity=s->saved_->gravity;s->pm_.gravity=s->gravity_;s->vertical_=false;
        }
        original();
        // ReAPI may cancel a jump. Only augment a real ground-to-air launch.
        if(eligible&&s->pm_.onground==-1&&s->pm_.velocity[2]>0)
            s->pm_.velocity[2]=std::min(s->pm_.movevars->maxvelocity,s->pm_.velocity[2]+s->state_->jump*units_per_block*20);
    }
    static void water(void(*original)()){
        auto* s=current_;
        if(!s||!s->walking()||!(s->state_->flags&dolphins_grace)||s->pm_.watertype!=-3||s->pm_.waterjumptime){original();return;}
        auto& p=s->pm_;const auto saved_vars=*p.movevars;const auto cmd=p.cmd;const float maxspeed=p.maxspeed;
        // Native acceleration is wish-speed based. Increase its water ceiling
        // by the vanilla drag ratio, retaining the original input acceleration.
        constexpr float ratio=5;const float dt=p.frametime;
        p.movevars->friction=p.friction>0?(1-std::pow(.96f,20*dt))/(dt*p.friction):0;
        p.movevars->accelerate/=ratio;p.maxspeed*=ratio;
        p.cmd.forwardmove*=ratio;p.cmd.sidemove*=ratio;p.cmd.upmove*=ratio;
        original();
        *p.movevars=saved_vars;p.cmd=cmd;p.maxspeed=maxspeed;
    }
};
}
