#include <metahook.h>
#include <VGUI/IBaseUI.h>
#include <VGUI/IGameUI.h>
#include <VGUI/IInput.h>
#include <VGUI/IPanel.h>
#include <VGUI/ISurface.h>
#include "host_ui.hpp"
#include "goldcraft/mouse_capture.hpp"

namespace goldcraft::host_ui {
namespace {
metahook_api_t* api=nullptr;
IBaseUI* base=nullptr;
IGameUI* game=nullptr;
vgui::IInput* input=nullptr;
vgui::IPanel* panel=nullptr;
vgui::ISurface* surface=nullptr;
hook_t* hook=nullptr;
KeyHandler handler=nullptr;
int(__fastcall* original_key)(void*,int,int,int,const char*)=nullptr;
std::uint64_t events=0,consumed=0;
MouseCapture mouse;

int __fastcall key_event(void* self,int,int down,int key,const char* binding){
    ++events;
    // CBaseUI consumes Escape before HUD_Key_Event. An existing Minecraft
    // screen owns that key; CS menus keep their normal priority and behavior.
    if(game&&!game->IsGameUIActive()&&handler&&handler(down,key,binding)){
        ++consumed;return 1;
    }
    return original_key(self,0,down,key,binding);
}

CreateInterfaceFn factory(const char* module){
    const auto loaded=GetModuleHandleA(module);
    return loaded?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(loaded,"CreateInterface")):nullptr;
}
}

void install(metahook_api_t* value,KeyHandler callback){
    if(hook)return;
    api=value;handler=callback;
    const auto engine=api->GetEngineFactory();
    const auto game_factory=factory("GameUI.dll"),vgui_factory=factory("vgui2.dll");
    base=engine?static_cast<IBaseUI*>(engine(BASEUI_INTERFACE_VERSION,nullptr)):nullptr;
    surface=engine?static_cast<vgui::ISurface*>(engine(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
    game=game_factory?static_cast<IGameUI*>(game_factory(GAMEUI_INTERFACE_VERSION,nullptr)):nullptr;
    input=vgui_factory?static_cast<vgui::IInput*>(vgui_factory(VGUI_INPUT_INTERFACE_VERSION,nullptr)):nullptr;
    panel=vgui_factory?static_cast<vgui::IPanel*>(vgui_factory(VGUI_PANEL_INTERFACE_VERSION,nullptr)):nullptr;
    if(const auto sdl=GetModuleHandleA("SDL2.dll")){
        mouse.get_relative_mode=reinterpret_cast<decltype(mouse.get_relative_mode)>(GetProcAddress(sdl,"SDL_GetRelativeMouseMode"));
        mouse.set_relative_mode=reinterpret_cast<decltype(mouse.set_relative_mode)>(GetProcAddress(sdl,"SDL_SetRelativeMouseMode"));
        mouse.show_cursor=reinterpret_cast<decltype(mouse.show_cursor)>(GetProcAddress(sdl,"SDL_ShowCursor"));
    }
    if(base&&game)hook=api->VFTHook(base,0,4,reinterpret_cast<void*>(key_event),reinterpret_cast<void**>(&original_key));
}

void shutdown(){
    if(hook&&api)api->UnHook(hook);
    hook=nullptr;base=nullptr;game=nullptr;input=nullptr;panel=nullptr;surface=nullptr;handler=nullptr;
    mouse={};
}

void resume_game(){if(base){base->HideConsole();base->HideGameUI();}}

void update_mouse_capture(bool active_window,bool raw_input){
    if(surface)mouse.update(active_window,surface->IsCursorVisible()||(game&&game->IsGameUIActive()),raw_input);
}

State state(){
    State result;result.hooked=hook!=nullptr;result.events=events;result.consumed=consumed;
    result.game_menu=game&&game->IsGameUIActive();result.keyboard=surface&&surface->NeedKBInput();
    result.cursor_visible=surface&&surface->IsCursorVisible();result.mouse_api=mouse.available();
    result.relative_mouse=mouse.get_relative_mode?mouse.get_relative_mode():-1;
    result.mouse_repairs=mouse.repairs;result.mouse_errors=mouse.errors;
    if(input&&panel)if(const auto focus=input->GetFocus())if(const char* name=panel->GetName(focus)){
        // Panel identifiers only; never export text fields or their contents.
        for(unsigned i=0;name[i]&&i<64;++i){const unsigned char c=name[i];result.focus+=c>=32&&c<127&&c!='"'&&c!='\\'?c:'?';}
    }
    return result;
}
}
