#pragma once
#include <cstdint>
#include <iosfwd>
struct metahook_api_s;
namespace goldcraft::hud_clock {
bool install(metahook_api_s* api);
void advance(float time);
void shutdown();
void diagnostics(std::ostream& out);
namespace detail {
// Matching CS client's CHud fields, starting at m_flTime (offset 0x24).
#pragma pack(push,4)
struct State {float current,previous;std::uint32_t reserved;double delta;};
#pragma pack(pop)
void advance(State& state,float time);
}
}
