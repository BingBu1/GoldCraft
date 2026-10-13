#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>

namespace goldcraft {
struct StatusHudEntry {std::string id;std::uint32_t amplifier=0;std::int32_t duration=0;};
struct StatusHud {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0;
    std::vector<StatusHudEntry> entries;
};
inline StatusHud read_status_hud(std::span<const std::uint8_t> bytes){
    if(bytes.size()>4096)throw ProtocolError("Status HUD packet size");
    Reader r(bytes);StatusHud s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();const auto count=r.u32();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||count>32)throw ProtocolError("Status HUD identity/count");
    for(unsigned i=0;i<count;++i){
        StatusHudEntry e;e.id=r.string(96);e.amplifier=r.u32();e.duration=static_cast<std::int32_t>(r.u32());
        const auto colon=e.id.find(':');
        if(colon==0||colon==std::string::npos||colon+1==e.id.size()||e.id.find(':',colon+1)!=std::string::npos
            ||e.amplifier>255||e.duration< -1)throw ProtocolError("Status HUD entry");
        for(std::size_t j=0;j<e.id.size();++j){const auto c=e.id[j];
            if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'||(c==':'&&j==colon)||(c=='/'&&j>colon)))
                throw ProtocolError("Status HUD registry ID");
        }
        if(std::any_of(s.entries.begin(),s.entries.end(),[&](const auto& old){return old.id==e.id;}))throw ProtocolError("Duplicate status HUD effect");
        s.entries.push_back(std::move(e));
    }
    r.finish();return s;
}
inline std::string status_hud_text(const StatusHud& s){
    std::string text;
    for(const auto& e:s.entries){
        std::string line=e.id+" "+std::to_string(e.amplifier+1)+" "+(e.duration<0?"inf":std::to_string((static_cast<std::int64_t>(e.duration)+19)/20)+"s")+"\n";
        if(text.size()+line.size()>480){text+="...\n";break;}
        text+=line;
    }
    return text;
}
}
