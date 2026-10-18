#pragma once
#include "goldcraft/avatar.hpp"
#include <cmath>

namespace goldcraft::status_glow {
constexpr const char* message_name="GCOutline";
constexpr const char* client_capability="_gc_outline";
constexpr unsigned enabled=1, wire_bytes=60;
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0,flags=0,color=0;
};
inline void validate(const State& s){
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life
        ||(s.flags&~enabled)||(s.color&0xff000000u)||(!s.flags&&s.color))
        throw ProtocolError("Invalid native outline state");
}
inline State read(Reader& r){
    State s;s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();
    s.spawn=r.u32();s.life=r.u32();s.flags=r.u32();s.color=r.u32();validate(s);return s;
}
inline State decode(std::span<const std::uint8_t> bytes){Reader r(bytes);auto s=read(r);r.finish();return s;}
inline void write(Writer& w,const State& s){
    validate(s);w.u64(s.epoch);w.u64(s.revision);w.u32(s.slot);w.u32(s.serial);
    w.u32(s.spawn);w.u32(s.life);w.u32(s.flags);w.u32(s.color);
}
// This reliable CS message uses a host-owned delivery sequence, independent of
// the MC producer sequence. Expiry/removal must supersede the last active frame.
struct Presentation {
    State state;
    std::uint32_t userid=0;
    Key uuid{};
    PlayerIdentity identity()const{return {state.slot,state.serial,userid,uuid};}
};
inline Presentation decode_presentation(std::span<const std::uint8_t> bytes){
    Reader r(bytes);Presentation p;p.state=read(r);p.userid=r.u32();p.uuid=r.key();r.finish();
    if(!p.userid)throw ProtocolError("Invalid native outline userid");return p;
}
inline Bytes encode(const Presentation& p){
    if(!p.userid)throw ProtocolError("Invalid native outline userid");
    Writer w;write(w,p.state);w.u32(p.userid);w.bytes(p.uuid);return w.data;
}
class Receiver {
    struct Entry {Presentation presentation;double received=0;};
    std::array<Entry,65> entries_{};
public:
    void clear(){entries_={};}
    bool accept(const Presentation& p,std::uint64_t world,double now){
        validate(p.state);
        if(!p.userid||!std::isfinite(now)||p.state.epoch!=world)return false;
        auto& entry=entries_[p.state.slot];const auto& old=entry.presentation.state;
        if(old.epoch==world&&p.state.revision<=old.revision)return false;
        entry={p,now};return true;
    }
    const Presentation* active(const PlayerPresentation& roster,std::uint64_t world,double now)const{
        const auto slot=roster.identity.slot;if(!slot||slot>=entries_.size())return nullptr;
        const auto& entry=entries_[slot];const auto& p=entry.presentation;
        return world&&p.state.epoch==world&&p.state.flags==enabled&&p.identity()==roster.identity
            &&(roster.flags&5u)==1u&&now>=entry.received&&now-entry.received<.35?&p:nullptr;
    }
};
}
