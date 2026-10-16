#include "goldcraft/form_menu.hpp"
#include "goldcraft/host_keys.hpp"
#include "goldcraft/host_key_events.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace goldcraft;
namespace {
constexpr int K_ESCAPE=27,K_F6=140;
constexpr int K_ALT=132,K_CTRL=133,K_SHIFT=134;
constexpr int K_MOUSE1=241,K_MOUSE3=243,K_MWHEELDOWN=239,K_MWHEELUP=240;
FormMenu form_menu;
std::uint64_t world=1,form_menu_selections=0;
bool fresh=false,ui_active=false,minecraft_menu=false,console=false,native_menu=false;
unsigned hud_flags=0,native_closes=0,commands=0;
unsigned key_events=0,ui_modifiers=0,native_keys=0,ui_events=0,inventory_closes=0,host_releases=0;
int last_key=0,last_key_down=0;
bool control=false;
std::array<bool,256> menu_pressed{};
std::string command;
double Seconds(){return 10;}
bool HudFresh(){return fresh;}
namespace host_ui {
struct State {bool game_menu=false,keyboard=false,hooked=true;} current;
const State& state(){return current;}
}
namespace host_text {
bool composing=false;
unsigned mods=0;
bool block_key(int){return composing;}
unsigned modifiers(unsigned){return mods;}
}
struct Engine {
    int (*Con_IsVisible)()=+[](){return console?1:0;};
    void (*pfnServerCmd)(const char*)=+[](const char* text){command=text;++commands;};
    void (*pfnClientCmd)(char*)=+[](char*){};
} gEngfuncs;
struct Exports {
    int (*HUD_Key_Event)(int,int,const char*)=+[](int,int,const char*){++native_keys;return 1;};
} gExportfuncs;
void CloseMinecraftMenu(){++inventory_closes;}
void ReleaseHostUse(){++host_releases;}
void ReleaseMinecraftKeys(){}
bool HasControl(){return control;}
void SendUi(unsigned,int,int=0,float=0){++ui_events;}
void SendControl(unsigned){}
bool ForwardKey(int,int){return false;}
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
    form_menu.clear();fresh=false;
    for(bool mc:{false,true})for(const char* binding:{"","save quick","echo Quicksaving...; wait; save quick","goldcraft_menu"}){
        control=mc;
        const auto before=commands,closes=inventory_closes,releases=host_releases;
        assert(KeyEvent(1,K_F6,binding)==0);
        assert(commands==before+1&&command=="goldcraft_menu\n");
        assert(inventory_closes==closes+1&&host_releases==releases+1);
        assert(KeyEvent(1,K_F6,binding)==0&&commands==before+1); // Held key never reopens.
        assert(KeyEvent(0,K_F6,binding)==0&&commands==before+1&&!menu_pressed[K_F6]);
        show("GoldCraft forms");
        assert(KeyEvent(1,K_F6,binding)==0&&commands==before+2&&command=="menuselect 10\n");
        assert(!form_menu.active(10)&&!native_menu);
        assert(KeyEvent(0,K_F6,binding)==0&&commands==before+2);
    }
    control=false;const auto before=commands;
    for(const char* binding:{"buy","save quick; say custom","goldcraft_menu; say custom","toggleconsole"}){
        const auto native_before=native_keys;
        assert(KeyEvent(1,K_F6,binding)==1&&KeyEvent(0,K_F6,binding)==1);
        assert(native_keys==native_before+2&&commands==before);
    }
    world=0; // No GoldCraft session: retain ordinary multiplayer/single-player keys.
    for(const char* binding:{"","save quick","echo Quicksaving...; wait; save quick"}){
        assert(KeyEvent(1,K_F6,binding)==1&&KeyEvent(0,K_F6,binding)==1&&commands==before);
    }
    world=1;console=true;
    assert(KeyEvent(1,K_F6,"save quick")==1&&KeyEvent(0,K_F6,"save quick")==1&&commands==before);
    console=false;ui_active=true;fresh=true;
    assert(KeyEvent(1,'x',"goldcraft_menu")==0&&ui_events==1&&commands==before);
    assert(KeyEvent(0,'x',"goldcraft_menu")==0&&ui_events==2);
    assert(KeyEvent(1,K_F6,"save quick")==0&&ui_events==3&&commands==before);
    assert(KeyEvent(0,K_F6,"save quick")==0&&ui_events==4);
    // Key callbacks no longer synthesize text: Shift, layouts and keypad text
    // come from SDL_TEXTINPUT exactly once, independent of frame key resets.
    ui_modifiers=0;host_text::mods=1;
    assert(KeyEvent(1,'2',"")==0&&ui_events==5&&ui_modifiers==1);
    assert(KeyEvent(1,170,"")==0&&ui_events==6);
    host_text::composing=true;
    for(int key:{13,27,8,int('2')})assert(KeyEvent(1,key,"")==0&&ui_events==6);
    std::cout<<"PASS production F6 press/release, stock bindings, native/MC menu open/close, custom bindings, ordinary servers, console and MC text input\n";
}
