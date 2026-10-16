#include <metahook.h>
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include "host_text.hpp"
#include "goldcraft/text_input.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <ostream>

namespace goldcraft::host_text {
namespace {
// The pinned SDL2 compatibility layer implements PollEvent through SDL3's
// WaitEventTimeout, then truncates committed UTF-8 to text[32]. Intercept the
// public SDL3 entry before that conversion so a Chinese phrase stays intact.
metahook_api_t* api=nullptr;hook_t* poll_hook=nullptr;
Query query=nullptr;Commit commit=nullptr;
decltype(&SDL_WaitEventTimeout) original_poll=nullptr;
decltype(&SDL_GetKeyboardFocus) keyboard_focus=nullptr;
decltype(&SDL_GetWindowID) window_id=nullptr;
decltype(&SDL_GetWindowFromID) window_from_id=nullptr;
decltype(&SDL_GetTicksNS) ticks=nullptr;
decltype(&SDL_GetModState) mod_state=nullptr;
decltype(&SDL_StartTextInput) start_text=nullptr;
decltype(&SDL_StopTextInput) stop_text=nullptr;
decltype(&SDL_TextInputActive) text_active=nullptr;
IVGUI2Extension* extension=nullptr;vgui::ISurface* surface=nullptr;
vgui::HFont font=0;
text_input::Session session;
bool restore_active=false;
bool keyboard_mod_valid=false;SDL_Keymod keyboard_mod=SDL_KMOD_NONE;
std::wstring preedit;
std::array<std::wstring,10> candidates;
unsigned candidate_count=0;int selected=-1;
std::array<std::wstring,11> lines;unsigned line_count=0;int overlay_width=220;
bool overlay_dirty=true;
std::uint64_t commits=0,characters=0,rejected=0,editing_events=0,candidate_events=0;

void clear_overlay(){preedit.clear();for(auto& text:candidates)text.clear();candidate_count=0;selected=-1;overlay_dirty=true;}
text_input::Target target(){
    if(!query||!keyboard_focus)return {};
    DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);
    if(process!=GetCurrentProcessId())return {};
    const auto c=query();const auto window=keyboard_focus();
    return c.world&&c.menu&&window?text_input::Target{c.world,c.menu,window_id(window)}:text_input::Target{};
}
void sync(){
    const auto next=target();
    if(next==session.target)return;
    const bool was_active=bool(session.target);
    if(was_active)if(const auto previous=window_from_id(session.target.window)){
        // Stop cancels the old composition; restore the native engine's text
        // input state when ownership leaves this displayed Minecraft screen.
        stop_text(previous);if(restore_active)start_text(previous);
    }
    session.set(next,ticks());clear_overlay();
    if(next){const auto window=keyboard_focus();restore_active=text_active(window);start_text(window);}
}
std::string_view bounded(const char* text,std::size_t limit){
    if(!text)return {};
    std::size_t n=0;while(n<limit&&text[n])++n;
    return n<limit?std::string_view(text,n):std::string_view{};
}
bool route(SDL_Event& e){
    const bool owned=bool(session.target);
    if(!owned)return false;
    if(e.type!=SDL_EVENT_TEXT_INPUT&&e.type!=SDL_EVENT_TEXT_EDITING&&e.type!=SDL_EVENT_TEXT_EDITING_CANDIDATES)return false;
    if(e.text.windowID!=session.target.window)return false;
    if(!session.accepts(e.text.windowID,e.common.timestamp)){++rejected;return true;}
    overlay_dirty=true;
    if(e.type==SDL_EVENT_TEXT_INPUT){
        session.committed();clear_overlay();
        const auto text=bounded(e.text.text,4096);
        if(text.empty()||!text_input::decode(text,[&](std::uint32_t cp){commit(cp,modifiers(0));++characters;}))++rejected;
        else ++commits;
    }else if(e.type==SDL_EVENT_TEXT_EDITING){
        const auto text=bounded(e.edit.text,4096);
        preedit=text_input::wide(text);session.editing(!text.empty());++editing_events;
        if(text.empty())clear_overlay();
    }else{
        const auto& event=e.edit_candidates;
        candidate_count=0;selected=-1;for(auto& text:candidates)text.clear();++candidate_events;
        if(event.num_candidates>0&&event.num_candidates<=1024&&event.candidates){
            const int first=event.selected_candidate>=0&&event.selected_candidate<event.num_candidates?(event.selected_candidate/10)*10:0;
            candidate_count=unsigned(std::min(10,event.num_candidates-first));selected=event.selected_candidate-first;
            for(unsigned i=0;i<candidate_count;++i)candidates[i]=text_input::wide(bounded(event.candidates[first+i],1024));
            session.editing(true);
        }
    }
    return true;
}
bool SDLCALL poll(SDL_Event* e,Sint32 timeout){
    keyboard_mod_valid=false;
    sync();
    for(;;){
        const bool result=original_poll(e,timeout);
        if(!result){session.end_batch();return result;}
        if(!e)return result;
        bool consumed=false;
        try{consumed=route(*e);}catch(...){++rejected;clear_overlay();consumed=true;}
        if(!consumed){
            if(e->type==SDL_EVENT_KEY_DOWN||e->type==SDL_EVENT_KEY_UP){keyboard_mod=e->key.mod;keyboard_mod_valid=true;}
            return result;
        }
        // SDL3 owns these strings. Copy before polling again, and never extend
        // a caller's timed wait after consuming an event intended for MC.
        timeout=0;
    }
}
void paint(int left,int top,int right,int bottom){
    sync();if(!session.target||!surface||(preedit.empty()&&!candidate_count))return;
    const auto root=surface->GetEmbeddedPanel();if(!root)return;
    if(!font){font=surface->CreateFont();if(!surface->AddGlyphSetToFont(font,"Microsoft YaHei",20,400,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff)){font=0;return;}}
    if(overlay_dirty){
        line_count=0;overlay_width=220;
        if(!preedit.empty())lines[line_count++]=preedit;
        for(unsigned i=0;i<candidate_count;++i)lines[line_count++]=std::to_wstring((i+1)%10)+L". "+candidates[i];
        for(unsigned i=0;i<line_count;++i){int w=0,h=0;surface->GetTextSize(font,lines[i].c_str(),w,h);overlay_width=std::max(overlay_width,w+20);}
        overlay_dirty=false;
    }
    const auto count=line_count;int width=overlay_width;
    const int x=left+16,y=std::max(top+8,bottom-70-int(count)*24);
    width=std::min(width,std::max(1,right-x-8));
    surface->PushMakeCurrent(root,false);
    surface->DrawSetColor(16,20,27,245);surface->DrawFilledRect(x,y,x+width,y+int(count)*24+12);
    surface->DrawSetTextFont(font);
    for(unsigned i=0;i<count;++i){
        const bool current=int(i)-(preedit.empty()?0:1)==selected&&selected>=0;
        surface->DrawSetTextColor(current?255:235,current?218:235,current?96:235,255);
        surface->DrawSetTextPos(x+8,y+6+int(i)*24);surface->DrawPrintText(lines[i].c_str(),int(lines[i].size()));
    }
    surface->DrawFlushText();surface->PopMakeCurrent(root);
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks {
public:
    int GetAltitude()const override{return 10;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int& x,int& y,int& right,int& bottom,VGUI2Extension_CallbackContext* context)override{
        if(context->IsPost)paint(x,y,right,bottom);
    }
    void HideGameUI(VGUI2Extension_CallbackContext*)override{}
    void ActivateGameUI(VGUI2Extension_CallbackContext*)override{}
    void HideConsole(VGUI2Extension_CallbackContext*)override{}
    void ShowConsole(VGUI2Extension_CallbackContext*)override{}
} overlay;
}
bool install(metahook_api_t* value,Query q,Commit c){
    if(poll_hook)return true;
    api=value;
    const auto sdl=GetModuleHandleA("SDL3.dll");if(!sdl)return false;
    #define GC_SDL(name,slot) slot=reinterpret_cast<decltype(slot)>(GetProcAddress(sdl,#name));if(!slot)return false
    GC_SDL(SDL_GetKeyboardFocus,keyboard_focus);GC_SDL(SDL_GetWindowID,window_id);GC_SDL(SDL_GetWindowFromID,window_from_id);GC_SDL(SDL_GetTicksNS,ticks);
    GC_SDL(SDL_GetModState,mod_state);GC_SDL(SDL_StartTextInput,start_text);GC_SDL(SDL_StopTextInput,stop_text);
    GC_SDL(SDL_TextInputActive,text_active);
    #undef GC_SDL
    const auto engine=api->GetEngineFactory();surface=engine?static_cast<vgui::ISurface*>(engine(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
    const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
    const auto factory=module?Sys_GetFactory(module):nullptr;
    extension=factory?static_cast<IVGUI2Extension*>(factory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    const auto entry=GetProcAddress(sdl,"SDL_WaitEventTimeout");
    if(!surface||!extension||!entry)return false;
    poll_hook=api->InlineHook(reinterpret_cast<void*>(entry),reinterpret_cast<void*>(poll),reinterpret_cast<void**>(&original_poll));
    if(!poll_hook)return false;
    query=q;commit=c;extension->RegisterBaseUICallbacks(&overlay);return true;
}
void shutdown(){
    query=nullptr;
    if(poll_hook){sync();extension->UnregisterBaseUICallbacks(&overlay);api->UnHook(poll_hook);}
    poll_hook=nullptr;original_poll=nullptr;extension=nullptr;surface=nullptr;commit=nullptr;font=0;
    session={};keyboard_mod_valid=false;clear_overlay();
}
bool block_key(int glfw){if(!poll_hook)return false;sync();return session.block_key(glfw);}
unsigned modifiers(unsigned fallback){
    if(!mod_state||!poll_hook)return fallback;
    const auto mod=keyboard_mod_valid?keyboard_mod:mod_state();
    return (mod&SDL_KMOD_SHIFT?1u:0u)|(mod&SDL_KMOD_CTRL?2u:0u)|(mod&SDL_KMOD_ALT?4u:0u);
}
void diagnostics(std::ostream& out){
    out<<"{\"hooked\":"<<(poll_hook?"true":"false")<<",\"active\":"<<(session.target?"true":"false")
       <<",\"composing\":"<<(session.composing?"true":"false")<<",\"commits\":"<<commits<<",\"characters\":"<<characters
       <<",\"editing\":"<<editing_events<<",\"candidates\":"<<candidate_events<<",\"rejected\":"<<rejected<<'}';
}
}
