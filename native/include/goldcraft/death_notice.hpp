#pragma once
#include "wire.hpp"
#include <algorithm>
#include <string>

namespace goldcraft {
// CS DeathMsg's original prefix is unchanged by optional ReGameDLL additions.
struct DeathNotice {
    unsigned killer=0,victim=0;
    bool headshot=false;
    std::string weapon;
};
inline DeathNotice read_death_notice(std::span<const std::uint8_t> bytes){
    if(bytes.size()<4||bytes.size()>512)throw ProtocolError("Invalid DeathMsg length");
    const auto end=std::find(bytes.begin()+3,bytes.end(),0);
    if(end==bytes.end()||end-(bytes.begin()+3)>63||bytes[0]>32||bytes[1]>32||!bytes[1])
        throw ProtocolError("Invalid DeathMsg prefix");
    DeathNotice result{bytes[0],bytes[1],bytes[2]!=0,{}};
    result.weapon.assign(reinterpret_cast<const char*>(bytes.data()+3),static_cast<std::size_t>(end-(bytes.begin()+3)));
    return result;
}
}
