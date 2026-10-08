#include "goldcraft/mouse_capture.hpp"
#include <iostream>
#include <stdexcept>

namespace {
struct SdlState {
    bool relative=false, shown=true, fail_relative=false, fail_cursor=false;
    int mode_writes=0, cursor_writes=0, queries=0;
} sdl;
int get_mode(){++sdl.queries;return sdl.relative?1:0;}
int set_mode(int value){++sdl.mode_writes;if(sdl.fail_relative)return -1;sdl.relative=value!=0;return 0;}
int show_cursor(int value){++sdl.queries;if(sdl.fail_cursor)return -1;if(value>=0){++sdl.cursor_writes;sdl.shown=value!=0;}return sdl.shown?1:0;}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
goldcraft::MouseCapture capture(){return {get_mode,set_mode,show_cursor};}

void engine_transition_regressions(){
    // Observed SDL outcomes from the matching engine's actual x86 cursor
    // function, replayed separately in an emulator. These are the three broken
    // transitions, not a second implementation of the reconciliation code.
    struct Case {bool actual_relative,actual_shown,cursor_visible,raw_input;};
    const Case cases[]{
        {false,true,false,true}, // enable raw input while cursor stays hidden
        {true,true,false,false}, // disable raw input during gameplay
        {true,true,true,false},  // disable raw input while opening a menu
    };
    for(const auto& test:cases){
        sdl={test.actual_relative,test.actual_shown};auto mouse=capture();
        mouse.update(true,test.cursor_visible,test.raw_input);
        const bool desired=test.raw_input&&!test.cursor_visible;
        require(sdl.relative==desired,"wrong relative mode after engine transition");
        require(desired||sdl.shown==test.cursor_visible,"wrong cursor after engine transition");
        require(mouse.repairs>0&&mouse.errors==0,"repair was not successful");
        const int writes=sdl.mode_writes+sdl.cursor_writes;
        for(int frame=0;frame<240;++frame)mouse.update(true,test.cursor_visible,test.raw_input);
        require(sdl.mode_writes+sdl.cursor_writes==writes,"steady gameplay repeatedly resets SDL capture");
    }
}

void menus_focus_and_failures(){
    sdl={true,true};auto mouse=capture();
    mouse.update(true,true,true);
    require(!sdl.relative&&sdl.shown,"native menu did not release relative mode");
    mouse.update(true,false,true);
    require(sdl.relative,"return to gameplay did not enable raw mode");
    const auto saved=sdl;
    mouse.update(false,true,false);
    require(sdl.queries==saved.queries&&sdl.mode_writes==saved.mode_writes&&sdl.cursor_writes==saved.cursor_writes,
        "inactive window touched SDL mouse state");
    sdl={false,true}; // native focus handling released the previous lease
    mouse.update(true,false,true);
    require(sdl.relative,"focused gameplay did not regain raw mode");
    sdl={false,true};sdl.fail_relative=true;mouse=capture();
    mouse.update(true,false,true);
    require(mouse.errors==1&&mouse.repairs==0&&!sdl.relative&&sdl.cursor_writes==0,
        "failed capture was reported as success or cursor was changed");
    sdl={false,false};sdl.fail_cursor=true;mouse=capture();
    mouse.update(true,true,false);
    require(mouse.errors==1&&mouse.repairs==0,"cursor query error was ignored");
    goldcraft::MouseCapture unsupported;
    unsupported.update(true,false,true);
    require(!unsupported.available()&&unsupported.repairs==0,"missing SDL API was treated as available");
}
}

int main(){
    try{engine_transition_regressions();menus_focus_and_failures();std::cout<<"Mouse capture transitions, stable frames, menus, focus and failure handling passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
