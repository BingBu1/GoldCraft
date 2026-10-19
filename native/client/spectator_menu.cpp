#include <metahook.h>
#include <VGUI/IPanel.h>
#include "spectator_menu.hpp"
#include <array>
#include <ostream>

namespace goldcraft::spectator_menu {
namespace detail {
static_assert(sizeof(void*)==4 && sizeof(Node)==12);
static_assert(offsetof(Menu,head)==0xa0 && offsetof(Menu,input_mode)==0xd8);
int find(const Menu& menu,const void* item) {
    if(!item || !menu.nodes || menu.capacity<0 || menu.capacity>4096)return -1;
    int previous=-1;
    for(int i=menu.head,visited=0;i!=-1 && visited<menu.capacity;++visited) {
        if(i<0 || i>=menu.capacity || menu.nodes[i].previous!=previous)return -1;
        if(menu.nodes[i].item==item)return i;
        previous=i;i=menu.nodes[i].next;
    }
    return -1;
}
bool dispatch(Menu* menu,std::uint32_t panel,bool enter,const Calls& calls) {
    if(!menu)return false;
    if(menu->input_mode!=0)return true;
    auto* item=calls.resolve(menu,panel);
    if(find(*menu,item)<0)return false;
    if(!enter){calls.disarm(item);return true;}
    calls.arm(item);
    if(find(*menu,item)<0)return false;
    calls.open(item);
    // Opening a cascade can rebuild the parent. Re-resolve membership after
    // native callbacks; never select a stale index saved before reentrancy.
    const int selected=find(*menu,item);
    if(selected<0)return false;
    calls.select(menu,selected);
    return true;
}
}
namespace {
metahook_api_t* api{};
vgui::IPanel* panels{};
std::array<hook_t*,2> hooks{};
std::array<void*,2> originals{};
void (__thiscall* open_cascade)(void*){};
void (__thiscall* select_item)(void*,int){};
std::uint64_t accepted{},rejected{};
bool enabled{};
template<class R,class... A> R invoke(void* object,unsigned slot,A... args) {
    return reinterpret_cast<R(__thiscall*)(void*,A...)>((*static_cast<void***>(object))[slot])(object,args...);
}
void* resolve(detail::Menu* menu,std::uint32_t handle) {
    if(!handle)return nullptr;
    return panels->GetPanel(handle,invoke<const char*>(menu,26));
}
void arm(void* item){invoke<void>(item,200);}
void disarm(void* item){invoke<void>(item,201);}
void open(void* item){open_cascade(item);}
void select(detail::Menu* menu,int index){select_item(menu,index);}
const detail::Calls calls{resolve,arm,open,select,disarm};
template<bool Enter> void __fastcall cursor(detail::Menu* menu,void*,std::uint32_t panel) {
    ++(detail::dispatch(menu,panel,Enter,calls)?accepted:rejected);
}
}
bool install(metahook_api_t* value) {
    if(enabled)return true;
    api=value;
    auto* module=api->GetClientBase();std::uint64_t crc{};
    if(!module || api->GetModuleCRC64(module,&crc)!=MH_GAMESYMBOL_OK || crc!=0x124d6034a6b28b40ULL)return false;
    const auto loaded=GetModuleHandleA("vgui2.dll");
    const auto factory=loaded?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(loaded,"CreateInterface")):nullptr;
    panels=factory?static_cast<vgui::IPanel*>(factory(VGUI_PANEL_INTERFACE_VERSION,nullptr)):nullptr;
    if(!panels)return false;
    const char* names[]{"GoldCraftMenu_CursorEntered","GoldCraftMenu_CursorExited","GoldCraftMenu_OpenCascade","GoldCraftMenu_SelectIndex"};
    std::array<void*,4> targets{};
    for(unsigned i=0;i<targets.size();++i)
        if(api->ResolveGameSymbol(module,names[i],MH_GAMESYMBOL_KIND_FUNCTION,&targets[i])!=MH_GAMESYMBOL_OK || !targets[i])return false;
    open_cascade=reinterpret_cast<decltype(open_cascade)>(targets[2]);
    select_item=reinterpret_cast<decltype(select_item)>(targets[3]);
    hooks[0]=api->InlineHook(targets[0],reinterpret_cast<void*>(cursor<true>),&originals[0]);
    hooks[1]=api->InlineHook(targets[1],reinterpret_cast<void*>(cursor<false>),&originals[1]);
    if(!hooks[0] || !hooks[1]){shutdown();return false;}
    enabled=true;return true;
}
void shutdown() {
    for(auto& hook:hooks){if(hook&&api)api->UnHook(hook);hook=nullptr;}
    originals={};
    enabled=false;panels=nullptr;open_cascade=nullptr;select_item=nullptr;
}
void diagnostics(std::ostream& out) {
    out<<"{\"installed\":"<<(enabled?"true":"false")<<",\"accepted\":"<<accepted<<",\"stale\":"<<rejected<<'}';
}
}
