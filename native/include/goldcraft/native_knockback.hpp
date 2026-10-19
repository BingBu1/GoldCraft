#pragma once
#include "goldcraft/native_impulse.hpp"
#include <array>

namespace goldcraft::native_knockback {
// One confirmed hurt can authorize one aggregate vanilla impulse. The epoch,
// connection and life fence also covers counter reuse after reconnect/map reset.
class Grants {
    struct Entry {
        std::uint64_t epoch=0,generation=0,event=0;
        std::uint32_t slot=0,serial=0,spawn=0,life=0,team=0;
        float time=0;
    };
    std::array<Entry,256> entries_{};
    std::size_t next_=0;
public:
    void grant(std::uint64_t epoch,std::uint64_t generation,std::uint64_t event,
        unsigned slot,unsigned serial,unsigned spawn,unsigned life,unsigned team,float now){
        entries_[next_++%entries_.size()]={epoch,generation,event,slot,serial,spawn,life,team,now};
    }
    bool consume(const native_impulse::Request& q,std::uint64_t damage_event,std::uint64_t generation,unsigned team,float now){
        for(auto& entry:entries_)if(entry.event==damage_event&&entry.epoch==q.epoch&&entry.generation==generation){
            const auto saved=entry;entry={};
            return saved.slot==q.slot&&saved.serial==q.serial&&saved.spawn==q.spawn&&saved.life==q.life&&saved.team==team
                &&std::isfinite(now)&&now>=saved.time&&now-saved.time<=1.0f;
        }
        return false;
    }
};
}
