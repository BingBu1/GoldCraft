#include "goldcraft/form_menu.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace goldcraft;
namespace {
constexpr int K_ESCAPE=27,K_F6=140;
FormMenu form_menu;
std::uint64_t world=1,form_menu_selections=0;
bool fresh=false,ui_active=false,minecraft_menu=false,console=false,native_menu=false;
unsigned hud_flags=0,native_closes=0,commands=0;
std::string command;
double Seconds(){return 10;}
bool HudFresh(){return fresh;}
namespace host_ui {
struct State {bool game_menu=false,keyboard=false;} current;
const State& state(){return current;}
}
struct Engine {
    int (*Con_IsVisible)()=+[](){return console?1:0;};
    void (*pfnServerCmd)(char*)=+[](char* text){command=text;++commands;};
} gEngfuncs;
auto previous_show_menu=+[](const char* name,int length,void* data){
    assert(std::strcmp(name,"ShowMenu")==0&&length==5);
    for(int i=0;i<length;++i)assert(static_cast<std::uint8_t*>(data)[i]==0);
    native_menu=false;++native_closes;return 1;
};
#include "form_menu_routing.inc"
void show(const char* title){
    Writer w;w.u16(514);w.u8(255);w.u8(0);
    w.data.insert(w.data.end(),title,title+std::strlen(title));w.u8(0);
    form_menu.message(w.data,Seconds());native_menu=true;
}
}
int main(){
    show("GoldCraft forms\n2. Minecraft\n0. Exit");
    assert(!fresh&&ServerMenuInput()); // Native CS must clear both copies.
    assert(ServerMenuKey('2',"slot2")&&form_menu.selection('2',"slot2",10)==2);
    SelectFormMenu(2);
    assert(commands==1&&command=="menuselect 2\n"&&native_closes==1&&!native_menu);
    fresh=true;assert(!ServerMenuInput()&&!form_menu.active(10));
    show("GoldCraft forms");console=true;assert(!ServerMenuInput());console=false;
    host_ui::current.keyboard=true;assert(!ServerMenuInput());host_ui::current.keyboard=false;
    ui_active=true;assert(!ServerMenuInput());ui_active=false;
    minecraft_menu=true;assert(!ServerMenuInput());minecraft_menu=false;
    assert(ServerMenuInput());SelectFormMenu(10);assert(command=="menuselect 10\n");
    show("AMXX inventory");fresh=false;assert(!ServerMenuInput());
    fresh=true;assert(ServerMenuInput());assert(!ServerMenuKey(K_ESCAPE,""));
    SelectFormMenu(2);assert(commands==3&&native_closes==3);
    assert(DefaultFormMenuKey(K_F6,nullptr)&&DefaultFormMenuKey(K_F6,""));
    assert(DefaultFormMenuKey('x',"goldcraft_menu"));
    assert(!DefaultFormMenuKey(K_F6,"buy")&&!DefaultFormMenuKey(K_F6,"goldcraft_menu; say test"));
    ui_active=true; // An inventory/JEI/chat search must receive its typed key.
    assert(!DefaultFormMenuKey('x',"goldcraft_menu")&&!DefaultFormMenuKey(K_F6,nullptr));
    ui_active=false;assert(DefaultFormMenuKey('x',"goldcraft_menu"));
    std::cout<<"PASS production menu selection across native/MC, both copies cleared, one server command, unrelated bindings/UI preserved\n";
}
