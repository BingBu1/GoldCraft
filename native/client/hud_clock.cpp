#include <metahook.h>
#include "hud_clock.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ostream>

namespace goldcraft::hud_clock {
namespace detail {
static_assert(offsetof(State,delta)==12 && sizeof(State)==20);
void advance(State& state,float time) {
    if(!std::isfinite(time))return;
    state.previous=state.current;
    state.current=time;
    state.delta=std::max(0.0,double(time)-double(state.previous));
}
}
namespace {detail::State* state{};std::uint64_t updates{};}
bool install(metahook_api_s* api) {
    void* target{};std::uint64_t crc{};
    const auto module=api->GetClientBase();
    if(!module || api->GetModuleCRC64(module,&crc)!=MH_GAMESYMBOL_OK || crc!=0x124d6034a6b28b40ULL)return false;
    if(api->ResolveGameSymbol(module,"GoldCraftHudClock_State",MH_GAMESYMBOL_KIND_GLOBAL,&target)!=MH_GAMESYMBOL_OK || !target)return false;
    state=static_cast<detail::State*>(target);return true;
}
void advance(float time){if(state){detail::advance(*state,time);++updates;}}
void shutdown(){state=nullptr;}
void diagnostics(std::ostream& out){out<<"{\"installed\":"<<(state?"true":"false")<<",\"updates\":"<<updates<<",\"time\":"<<(state?state->current:0)<<'}';}
}
