#pragma once
#include "goldcraft/status_glow.hpp"
#include <IMetaRendererStudioOutline.h>

namespace goldcraft {
struct OutlineSubmission {
    bool minecraft=false;
    std::array<MetaRendererStudioOutlinePlayer,64> players{};
    std::uint32_t count=0;
};
inline OutlineSubmission native_outline_submission(const status_glow::Receiver& receiver,
    const std::array<PlayerPresentation,65>& roster,const std::array<double,65>& received,
    std::uint64_t world,double now){
    OutlineSubmission result;
    for(unsigned slot=1;slot<roster.size();++slot){
        if(roster[slot].identity.slot!=slot||!std::isfinite(received[slot])||
            now<received[slot]||now-received[slot]>=3)continue;
        if(const auto* state=receiver.active(roster[slot],world,now))
            result.players[result.count++]={slot,state->state.color};
    }
    return result;
}
}
