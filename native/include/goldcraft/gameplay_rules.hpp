#pragma once
#include "goldcraft/wire.hpp"

namespace goldcraft {
// Cumulative clear revisions survive bridge reconnects; only a map change resets them.
class GameplayRules {
public:
    std::uint64_t world=0,revision=1,clear_revision=0;
    bool clear_blocks=false,drop_items=true;
    void reset(std::uint64_t epoch) { *this=GameplayRules{};world=epoch; }
    void configure(bool clear,bool drops) {
        if(clear_blocks==clear&&drop_items==drops)return;
        clear_blocks=clear;drop_items=drops;++revision;
    }
    // Called only by the outermost real CleanUpMap, during RestartRound.
    // A win/termination announcement must leave the completed round intact.
    void cleanup_round() {
        if(world&&clear_blocks){++clear_revision;++revision;}
    }
    Bytes encode() const {
        Writer w;w.u64(world);w.u64(revision);w.u64(clear_revision);
        w.u32(clear_blocks?1:0);w.u32(drop_items?1:0);return std::move(w.data);
    }
};
}
