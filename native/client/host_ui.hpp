#pragma once
#include <cstdint>
#include <string>

struct metahook_api_s;
namespace goldcraft::host_ui {
using KeyHandler=bool(*)(int down,int key,const char* binding);
struct State {
    bool hooked=false,game_menu=false,keyboard=false;
    std::uint64_t events=0,consumed=0;
    std::string focus;
};
void install(metahook_api_s* api,KeyHandler handler);
void shutdown();
void resume_game();
State state();
}
