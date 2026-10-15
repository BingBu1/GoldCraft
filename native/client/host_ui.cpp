#include <metahook.h>
#include <VGUI/IBaseUI.h>
#include <VGUI/IGameUI.h>
#include <VGUI/IInput.h>
#include <VGUI/IPanel.h>
#include <VGUI/ISurface.h>
#include "host_ui.hpp"
#include "work_history.hpp"
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
hook_t* release_hook=nullptr;
KeyHandler handler=nullptr;
KeyReleaseHandler release_handler=nullptr;
void(*original_engine_key)(int,int)=nullptr;
int(__fastcall* original_key)(void*,int,int,int,const char*)=nullptr;
std::uint64_t events=0,consumed=0;
std::uint64_t releases=0;
MouseCapture mouse;
enum UiWork : std::size_t {engine_event,native_engine,base_event,native_base,mc_handler,mouse_capture,resume_menu,work_count};
constexpr std::array<const char*,work_count> work_names{
    "engineKey","nativeEngineKey","baseUiKey","nativeBaseUiKey","minecraftKeyHandler","mouseCapture","resumeMenu"};
timing::History<work_count> work_history;

void engine_key_event(int key,int down){
    timing::Scope work(work_history,engine_event,key);
    // GoldSrc skips ClientDLL/BaseUI on key-up for unconsumed +commands.
    // Observe the public engine entry so a native -attack/-reload release
    // also drains the previously forwarded Minecraft key. Always continue
    // into the original engine; native bindings and command timing stay intact.
    if(!down&&release_handler){++releases;release_handler(key);}
    {timing::Scope native(work_history,native_engine,key);original_engine_key(key,down);}
}

int __fastcall key_event(void* self,int,int down,int key,const char* binding){
    timing::Scope work(work_history,base_event,key);
    ++events;
    // CBaseUI consumes Escape before HUD_Key_Event. An existing Minecraft
    // screen owns that key; CS menus keep their normal priority and behavior.
    if(game&&!game_menu_active()&&handler){
        timing::Scope handled(work_history,mc_handler,key);
        if(handler(down,key,binding)){++consumed;return 1;}
    }
    timing::Scope native(work_history,native_base,key);
    return original_key(self,0,down,key,binding);
}

CreateInterfaceFn factory(const char* module){
    const auto loaded=GetModuleHandleA(module);
    return loaded?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(loaded,"CreateInterface")):nullptr;
}
}

void install(metahook_api_t* value,KeyHandler callback,void(*engine_key)(int,int),KeyReleaseHandler release){
    if(hook)return;
    api=value;handler=callback;release_handler=release;
    if(engine_key&&release&&!release_hook)
        release_hook=api->InlineHook(reinterpret_cast<void*>(engine_key),reinterpret_cast<void*>(engine_key_event),reinterpret_cast<void**>(&original_engine_key));
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
    release_handler=nullptr;
    if(release_hook&&api)api->UnHook(release_hook);
    release_hook=nullptr;original_engine_key=nullptr;
    if(hook&&api)api->UnHook(hook);
    hook=nullptr;base=nullptr;game=nullptr;input=nullptr;panel=nullptr;surface=nullptr;handler=nullptr;
    mouse={};
}

void resume_game(){timing::Scope work(work_history,resume_menu);if(base){base->HideConsole();base->HideGameUI();}}

void update_mouse_capture(bool active_window,bool raw_input){
    timing::Scope work(work_history,mouse_capture);
    if(surface)mouse.update(active_window,surface->IsCursorVisible()||game_menu_active(),raw_input);
}
bool game_menu_active(){return game&&game->IsGameUIActive();}
void write_work_diagnostics(std::ostream& out){work_history.write(out,work_names);}

State state(){
    State result;result.hooked=hook!=nullptr;result.events=events;result.consumed=consumed;
    result.release_hooked=release_hook!=nullptr;result.releases=releases;
    result.game_menu=game_menu_active();result.keyboard=surface&&surface->NeedKBInput();
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
