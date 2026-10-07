#pragma once
#include "wire.hpp"

namespace goldcraft {
struct PlayerIdentity {
    std::uint32_t slot=0,serial=0,userid=0;
    Key uuid{};
    bool operator==(const PlayerIdentity&) const = default;
};
struct PlayerPresentation {
    PlayerIdentity identity;
    std::uint32_t flags=0;
};
inline bool replaces_player(const PlayerIdentity& mesh,const PlayerPresentation& host) {
    // The native reliable roster, not a reused slot alone, authorizes replacement.
    return mesh.slot && mesh.serial && mesh.userid && mesh==host.identity
        && (host.flags&113u)==113u && (host.flags&12u)==0;
}
inline std::vector<PlayerIdentity> read_rendered_players(Reader& r) {
    const auto count=r.u32();
    if(count>64 || r.remaining()!=count*28u)throw ProtocolError("Invalid avatar identity count");
    std::array<bool,65> seen{};
    std::vector<PlayerIdentity> players;players.reserve(count);
    for(unsigned i=0;i<count;++i) {
        PlayerIdentity player{r.u32(),r.u32(),r.u32(),r.key()};
        if(!player.slot||player.slot>64||!player.serial||!player.userid||player.uuid==Key{}||seen[player.slot])throw ProtocolError("Invalid avatar identity");
        seen[player.slot]=true;players.push_back(player);
    }
    r.finish();return players;
}
}
