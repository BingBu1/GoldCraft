#include "../../native/client/hud_clock.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
    using namespace goldcraft::hud_clock::detail;
    State state{12,11,0x12345678,1};
    advance(state,12.25f);assert(state.current==12.25f && state.previous==12 && state.delta==.25);
    // A hidden native HUD must continue advancing the TAB timer.
    const float deadline=state.current+1;
    for(int i=1;i<=5;++i)advance(state,12.25f+i*.25f);
    assert(state.current>deadline && state.delta==.25 && state.reserved==0x12345678);
    advance(state,0);assert(state.current==0 && state.previous==13.5f && state.delta==0);
    advance(state,std::numeric_limits<float>::quiet_NaN());assert(state.current==0);
    std::puts("Native HUD clock: hidden-frame progression, deadline, restart delta and reserved field preserved");
}
