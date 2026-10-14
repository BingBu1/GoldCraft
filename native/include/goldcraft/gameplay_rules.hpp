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
    void observe_round(bool ended) {
        if(ended&&!ended_)finish_round();
        ended_=ended;
    }
    void cleanup_round() {
        if(!ended_)finish_round(); // Direct rg_restart_round / sv_restart.
        ended_=false;
    }
    Bytes encode() const {
        Writer w;w.u64(world);w.u64(revision);w.u64(clear_revision);
        w.u32(clear_blocks?1:0);w.u32(drop_items?1:0);return std::move(w.data);
    }
private:
    bool ended_=false;
    void finish_round() { if(world&&clear_blocks){++clear_revision;++revision;} }
};
}
