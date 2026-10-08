#pragma once
#include <cstdint>
#include <string>

struct metahook_api_s;
namespace goldcraft::host_ui {
using KeyHandler=bool(*)(int down,int key,const char* binding);
struct State {
    bool hooked=false,game_menu=false,keyboard=false;
    bool cursor_visible=false,mouse_api=false;
    int relative_mouse=-1;
    std::uint64_t events=0,consumed=0;
    std::uint64_t mouse_repairs=0,mouse_errors=0;
    std::string focus;
};
void install(metahook_api_s* api,KeyHandler handler);
void shutdown();
void resume_game();
void update_mouse_capture(bool active_window,bool raw_input);
State state();
}
