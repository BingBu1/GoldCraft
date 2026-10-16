// Compile the production adapter. Only the SDL queue, foreground check and
// destination callback are replaced; never open a window or inject game input.
#include <metahook.h>
#include <cassert>
#include <deque>
#include <iostream>
#include <vector>
namespace fixture {
bool focused=true;
HWND foreground(){return reinterpret_cast<HWND>(1);}
DWORD process(){return 17;}
DWORD window_process(HWND,LPDWORD value){*value=focused?17:18;return 1;}
}
#define GetForegroundWindow fixture::foreground
#define GetCurrentProcessId fixture::process
#define GetWindowThreadProcessId fixture::window_process
#include "../../native/client/host_text.cpp"
#undef GetForegroundWindow
#undef GetCurrentProcessId
#undef GetWindowThreadProcessId

int main(){
    using namespace goldcraft::host_text;
    static Context current{11,21};static Uint64 now=100;
    static bool enabled=true;static unsigned starts=0,stops=0;
    static SDL_Keymod mod=SDL_KMOD_NONE;
    static std::deque<SDL_Event> queue;
    static std::vector<std::pair<std::uint32_t,unsigned>> delivered;
    query=+[](){return current;};commit=+[](std::uint32_t cp,unsigned mods){delivered.emplace_back(cp,mods);};
    keyboard_focus=+[](){return reinterpret_cast<SDL_Window*>(1);};window_id=+[](SDL_Window*)->Uint32{return 31;};
    window_from_id=+[](SDL_WindowID){return reinterpret_cast<SDL_Window*>(1);};
    ticks=+[](){return now;};mod_state=+[](){return mod;};
    text_active=+[](SDL_Window*){return enabled;};
    start_text=+[](SDL_Window*){enabled=true;++starts;return true;};stop_text=+[](SDL_Window*){enabled=false;++stops;return true;};
    poll_hook=reinterpret_cast<hook_t*>(1);
    original_poll=+[](SDL_Event* e,Sint32){if(queue.empty())return false;if(e){*e=queue.front();queue.pop_front();}return true;};
    auto text=[&](const char* value,Uint64 stamp=100,Uint32 window=31){SDL_Event e{};e.text.type=SDL_EVENT_TEXT_INPUT;e.text.timestamp=stamp;e.text.windowID=window;e.text.text=value;queue.push_back(e);};
    auto edit=[&](const char* value){SDL_Event e{};e.edit.type=SDL_EVENT_TEXT_EDITING;e.edit.timestamp=now;e.edit.windowID=31;e.edit.text=value;queue.push_back(e);};
    auto key=[&](){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;queue.push_back(e);};
    SDL_Event event{};
    auto poll_one=[&](){return poll(&event,0);};
    text("中文");assert(!poll_one()&&delivered.size()==2&&delivered[0].first==0x4e2d&&starts==1);
    mod=SDL_KMOD_SHIFT;text("@");assert(!poll_one()&&delivered.back()==std::make_pair(std::uint32_t('@'),1u));
    mod=SDL_KMOD_NUM;text("0123456789+./");assert(!poll_one()&&delivered.back().first=='/');
    auto prior=delivered.size();text("中文输入法一次提交超过三十二字节也必须完整");assert(!poll_one()&&delivered.size()==prior+21);
    prior=delivered.size();text("old",99);assert(!poll_one()&&delivered.size()==prior);
    text("foreign",100,32);assert(poll_one()&&event.text.windowID==32&&delivered.size()==prior);
    edit("zhong");key();assert(poll_one()&&block_key(257)&&preedit==L"zhong");
    const char* words[]{"中","种"};SDL_Event ce{};
    ce.edit_candidates={SDL_EVENT_TEXT_EDITING_CANDIDATES,0,100,31,words,2,0,false,{}};queue.push_back(ce);key();
    assert(poll_one()&&candidate_count==2&&candidates[0]==L"中");
    edit("");text("中");key();assert(poll_one()&&block_key(257)&&candidate_count==0);
    assert(!poll_one()&&!block_key(257));
    // Menu generations reject delayed commits and cancel old preedit.
    edit("ni");key();assert(poll_one()&&block_key(259));
    current.menu=22;now=200;text("old",150);text("好",200);
    const auto before_change=delivered.size();assert(!poll_one()&&delivered.size()==before_change+1&&delivered.back().first==0x597d);
    fixture::focused=false;text("native",201);assert(poll_one()&&enabled&&!session.target);
    fixture::focused=true;enabled=false;now=300;key();assert(poll_one()&&enabled);
    current={};key();assert(poll_one()&&!enabled&&!session.target);
    assert(starts>=3&&stops>=3);
    SDL_Event shortcut{};shortcut.key.type=SDL_EVENT_KEY_DOWN;shortcut.key.mod=SDL_KMOD_CTRL;
    mod=SDL_KMOD_NONE;queue.push_back(shortcut);assert(poll_one()&&modifiers(0)==2);
    poll_hook=nullptr;
    std::cout<<"PASS production SDL adapter: long Chinese commit, Shift symbol, NumLock text, native/foreign/stale isolation, candidates, IME Enter and state restoration\n";
}
