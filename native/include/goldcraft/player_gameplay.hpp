#pragma once
#include "goldcraft/wire.hpp"

namespace goldcraft {
constexpr unsigned player_connected=1,player_creative=2,player_armored=4,player_skill_guard=8;
struct ActivePlayerAttack {
    int victim=0,attacker=0,kind=0;
    unsigned depth=0;std::uint64_t world=0,generation=0;
    unsigned victim_serial=0,victim_spawn=0,victim_life=0,attacker_serial=0,attacker_spawn=0,attacker_life=0;
    bool current(unsigned vs,unsigned vb,unsigned vl,unsigned as,unsigned ab,unsigned al)const{
        return victim_serial==vs&&victim_spawn==vb&&victim_life==vl&&attacker_serial==as&&attacker_spawn==ab&&attacker_life==al;
    }
    int match(int target,int source,unsigned call_depth,std::uint64_t epoch,std::uint64_t authority)const{
        return victim==target&&attacker==source&&depth==call_depth&&world==epoch&&generation==authority?kind:0;
    }
};
struct PlayerGameplay {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    Key uuid{};
    std::uint32_t flags=0;
};
inline PlayerGameplay read_player_gameplay(std::span<const std::uint8_t> bytes,bool skill_guard=false){
    Reader r(bytes);PlayerGameplay s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();s.uuid=r.key();s.flags=r.u32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life||s.uuid==Key{}
        ||(s.flags&~(skill_guard?15u:7u))||(!(s.flags&player_connected)&&s.flags))throw ProtocolError("Invalid player gameplay state");
    return s;
}
class PlayerGameplayLease {
    PlayerGameplay state_;
    float expires_=0;
    float updated_=0;
public:
    void revoke_privileges(){state_.flags&=player_connected;}
    bool update(const PlayerGameplay& next,float now){
        if(next.epoch==state_.epoch&&next.serial==state_.serial&&next.uuid==state_.uuid&&next.revision<=state_.revision)return false;
        state_=next;updated_=now;expires_=now+1.0f;return true;
    }
    int flags(std::uint64_t world,unsigned serial,unsigned spawn,unsigned life,const Key& uuid,float now,bool connected,bool alive=true)const{
        if(state_.epoch!=world||state_.serial!=serial||state_.uuid!=uuid)return 0;
        if(!connected||now<updated_||now>=expires_)return 0;
        // Presence survives form/respawn. Equipment and creative privileges do not.
        const auto flags=now<updated_+.15f?state_.flags:state_.flags&~player_skill_guard;
        return alive&&state_.spawn==spawn&&state_.life==life?static_cast<int>(flags):static_cast<int>(flags&player_connected);
    }
};
}
