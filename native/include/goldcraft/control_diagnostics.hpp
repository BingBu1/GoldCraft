#pragma once
#include <array>
#include <cstdint>
#include <ostream>

namespace goldcraft {
// Fixed-size diagnostic history; neither this helper nor observations grant control.
struct ControlSample {
    std::uint64_t world=0,connection=0;
    std::uint32_t life=0,host_life=0,blocked=0,hud_flags=0,ui_flags=0,hud_blocked=0;
    double seconds=0,pose_ms=-1,hud_ms=-1,camera_ms=-1;
    bool same_state(const ControlSample& other) const noexcept {
        return world==other.world&&connection==other.connection&&life==other.life&&host_life==other.host_life
            &&blocked==other.blocked&&hud_flags==other.hud_flags&&ui_flags==other.ui_flags&&hud_blocked==other.hud_blocked;
    }
};
enum ControlBlock : std::uint32_t {
    control_native_form=1,control_life_mismatch=2,control_producer_not_ready=4,
    control_link_down=8,control_pose_stale=16
};
constexpr std::uint32_t control_blocked(bool form,bool same_life,bool producer,bool connected,bool fresh_pose) noexcept {
    return (form?0u:control_native_form)|(same_life?0u:control_life_mismatch)
        |(producer?0u:control_producer_not_ready)|(connected?0u:control_link_down)|(fresh_pose?0u:control_pose_stale);
}
class ControlTrace {
    struct Entry {ControlSample state;std::uint64_t sequence=0;std::uint32_t kind=0,error=0;};
    std::array<Entry,32> entries_{};
    ControlSample latest_{};
    std::uint64_t events_=0,losses_=0,hud_fallbacks_=0;
    bool observed_=false;
    void append(const ControlSample& sample,std::uint32_t kind,std::uint32_t error=0) noexcept {
        auto& entry=entries_[events_%entries_.size()];entry={sample,++events_,kind,error};
    }
public:
    void observe(const ControlSample& sample) noexcept {
        if(observed_&&latest_.same_state(sample))return;
        if(observed_&&latest_.blocked==0&&sample.blocked!=0)++losses_;
        latest_=sample;observed_=true;append(sample,0);
    }
    void hud_fallback(const ControlSample& sample,std::uint32_t error) noexcept {
        ++hud_fallbacks_;
        // Keep the first failure and changes, not an entry for every repeated frame.
        if(events_){const auto& last=entries_[(events_-1)%entries_.size()];
            if(last.kind==1&&last.error==error&&last.state.same_state(sample))return;}
        append(sample,1,error);
    }
    void write(std::ostream& out) const {
        out<<"\"controlTrace\":{\"events\":"<<events_<<",\"overwritten\":"<<(events_>entries_.size()?events_-entries_.size():0)
           <<",\"losses\":"<<losses_<<",\"hudFallbacks\":"<<hud_fallbacks_<<",\"records\":[";
        const auto begin=events_>entries_.size()?events_-entries_.size():0;
        for(auto i=begin;i<events_;++i){if(i!=begin)out<<',';const auto& e=entries_[i%entries_.size()];const auto& s=e.state;
            out<<"{\"sequence\":"<<e.sequence<<",\"kind\":"<<e.kind<<",\"error\":"<<e.error
               <<",\"seconds\":"<<s.seconds<<",\"world\":\""<<s.world<<"\",\"connection\":"<<s.connection
               <<",\"life\":"<<s.life<<",\"hostLife\":"<<s.host_life<<",\"blocked\":"<<s.blocked
               <<",\"hudFlags\":"<<s.hud_flags<<",\"uiFlags\":"<<s.ui_flags<<",\"hudBlocked\":"<<s.hud_blocked
               <<",\"poseMs\":"<<s.pose_ms<<",\"hudMs\":"<<s.hud_ms<<",\"cameraMs\":"<<s.camera_ms<<'}';
        }out<<"]}";
    }
};
enum class ControlCause : std::uint32_t {
    acquire=1,authority_not_ready,death,form_change,relocation,map_deactivate,
    disconnect,authority_generation,authority_link_down,pose_stale,spawn,
    form_amxx,form_command,form_default
};
struct ServerControlEvent {
    std::uint64_t world=0,connection=0;
    std::uint32_t slot=0,life=0;
    ControlCause cause=ControlCause::acquire;
    int previous_form=0,requested_form=0,result=0;
    bool active=false;
    double seconds=0,pose_ms=-1;
};
class ServerControlTrace {
    std::array<ServerControlEvent,32> entries_{};
    std::uint64_t count_=0;
public:
    void record(const ServerControlEvent& event) noexcept {entries_[count_++%entries_.size()]=event;}
    void write(std::ostream& out) const {
        out<<"\"serverControlTrace\":{\"events\":"<<count_<<",\"records\":[";
        const auto first=count_>entries_.size()?count_-entries_.size():0;
        for(auto i=first;i<count_;++i){if(i!=first)out<<',';const auto& e=entries_[i%entries_.size()];
            out<<"{\"sequence\":"<<(i+1)<<",\"world\":\""<<e.world<<"\",\"connection\":"<<e.connection
               <<",\"slot\":"<<e.slot<<",\"life\":"<<e.life<<",\"cause\":"<<static_cast<unsigned>(e.cause)
               <<",\"previousForm\":"<<e.previous_form<<",\"requestedForm\":"<<e.requested_form<<",\"result\":"<<e.result
               <<",\"active\":"<<(e.active?"true":"false")<<",\"seconds\":"<<e.seconds<<",\"poseMs\":"<<e.pose_ms<<'}';
        }out<<"]}";
    }
};
}
