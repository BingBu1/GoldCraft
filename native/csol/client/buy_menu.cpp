// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "buy_menu.hpp"
#include "weapon_inventory.hpp"
#include "../buy_menu.hpp"
#include "../image_codec.hpp"
#include <VGUI/ISurface.h>
#include <VGUI/IInput.h>
#include <VGUI/IClientVGUI.h>
#include <IVGUI2Extension.h>
#include <usercmd.h>
#include <in_buttons.h>
#include <algorithm>
#include <cstring>
#include <ostream>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace csol::client {
namespace {
using namespace csol::buy;
vgui::ISurface* surface{};
vgui::IInput* input{};
IClientVGUI* clientUI{};
IVGUI2Extension* extension{};
vgui::HFont font{},titleFont{};
HANDLE privateFont{};
Presentation presentation;
bool capability{},loaded{},visible{},waiting{},catalogReady{},mouseHeld{},swallowMouse{},restoreMouse{};
std::uint16_t request{};
int category=-1,page{},hover=-1,balance{},fontHeight{},left{},top{};
float scale=1,angles[3]{};
double requestedAt{};
std::wstring status;
std::unordered_map<int,Packet> offers;
std::unordered_map<std::string,int> textures;
std::vector<const Item*> visibleRows;
int rowsCategory=-2;
xcommand_t oldBuy{},oldCancel{};
pfnUserMsgHook oldVGUI{},oldShowMenu{};
unsigned rejected{},purchases{},draws{},restores{};
constexpr const wchar_t* categories[]{L"CSOL 武器",L"手枪",L"霰弹枪",L"冲锋枪",L"步枪",L"机枪",L"特殊装备",L"装备",L"主武器弹药",L"副武器弹药"};
std::wstring Wide(const std::string& text){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(n<=0)return{};std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),s.data(),n);return s;}
bool Focused(){DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
void RestoreMouse()
{
    if(!restoreMouse||!Focused()||(engine.Con_IsVisible&&engine.Con_IsVisible()))return;
    if(input){input->SetMouseCapture(0);input->SetCursorOveride(0);}
    if(surface){surface->SetCursor(1);surface->LockCursor();}
    if(downstream.IN_ClearStates)downstream.IN_ClearStates();
    if(downstream.IN_ActivateMouse)downstream.IN_ActivateMouse();
    restoreMouse=false;++restores;
}
void Close(bool gameplay=true)
{
    if(!visible)return;
    visible=false;waiting=false;catalogReady=false;hover=-1;swallowMouse=true;
    // A stock VGUI Buy panel can own SDL/VGUI input after its children close.
    // Hide that owner before handing the cursor back to native game input.
    if(clientUI)clientUI->HideAllVGUIMenu();
    if(input){input->SetMouseCapture(0);input->SetCursorOveride(0);}
    restoreMouse=gameplay;if(gameplay)RestoreMouse();
}
bool Load()
{
    if(loaded)return true;
    try{const auto bytes=ReadResource("models/csol/buymenu.json");presentation=ParsePresentation({reinterpret_cast<const char*>(bytes.data()),bytes.size()});loaded=true;return true;}
    catch(const std::exception& e){engine.Con_Printf("CSOL 购买菜单资源：%s\n",e.what());return false;}
}
void Refresh()
{
    if(++request==0)++request;offers.clear();catalogReady=false;waiting=false;
    requestedAt=engine.GetClientTime();std::string command="csol_buy_menu "+std::to_string(request)+"\n";engine.pfnServerCmd(command.data());
}
void OpenMenu(bool fallback)
{
    if(visible){Close();return;}
    if(!capability||!surface||!input||!extension||!Load()){if(fallback){if(oldBuy)oldBuy();else engine.pfnServerCmd("buy\n");}return;}
    if(clientUI)clientUI->HideAllVGUIMenu();
    ClearNativeWeaponSelection();
    if(oldShowMenu){unsigned char clear[5]{};oldShowMenu("ShowMenu",sizeof(clear),clear);}
    visible=true;restoreMouse=false;category=-1;page=0;hover=-1;status=L"正在获取商品与余额…";
    engine.GetViewAngles(angles);mouseHeld=input->IsMouseDown(vgui::MOUSE_LEFT);
    if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();surface->UnlockCursor();surface->SetCursor(2);Refresh();
}
void Open(){OpenMenu(true);}
void Cancel(){if(visible)Close();else if(oldCancel)oldCancel();}
const std::vector<const Item*>& Rows()
{
    if(rowsCategory!=category){visibleRows.clear();visibleRows.reserve(Catalog().size());rowsCategory=category;
        for(const auto& item:Catalog())if(category==0?(item.id==144||item.id==220||item.id==725||item.id==726):item.category==category)visibleRows.push_back(&item);}
    return visibleRows;
}
void Purchase(const Item& item)
{
    if(waiting||!catalogReady)return;
    auto it=offers.find(item.id);if(it==offers.end()||it->second.result!=Result::Available){status=L"此商品当前不可购买";return;}
    if(balance<it->second.price){status=L"金钱不足";return;}
    waiting=true;requestedAt=engine.GetClientTime();status=L"正在购买…";
    std::string command="csol_buy "+std::to_string(request)+" "+std::to_string(item.id)+"\n";engine.pfnServerCmd(command.data());
}
void Select(int row)
{
    if(waiting||row<0||row>=10)return;
    if(category<0){if(row>=8){const auto* item=Find(row==8?2001:2002);if(item)Purchase(*item);}else{category=row;page=0;hover=-1;}return;}
    const auto& rows=Rows();const int index=page*10+row;if(index<int(rows.size()))Purchase(*rows[index]);
}
int Receive(const char*,int size,void* data)
{
    SynchronizeSession();Packet p;
    if(!data||size<0||!Decode({static_cast<const std::uint8_t*>(data),std::size_t(size)},p)){++rejected;return 0;}
    if(p.kind==Kind::Capability){capability=true;return 1;}
    if(!visible||p.request!=request)return 1;
    balance=p.balance;
    switch(p.kind){
    case Kind::Begin:offers.clear();catalogReady=false;break;
    case Kind::Item:offers[p.id]=p;break;
    case Kind::End:catalogReady=true;status=L"选择分类或武器购买";break;
    case Kind::Result:
        waiting=false;
        if(p.result==Result::Success){++purchases;Close();}
        else if(p.result==Result::Unavailable){status=L"当前状态无法购买；按 B / Esc 返回";}
        else{status=p.result==Result::Insufficient?L"金钱不足":L"购买未完成，请检查当前装备";}
        break;
    default:break;
    }
    return 1;
}
int VGUI(const char* name,int size,void* data)
{
    if(capability&&data&&size>0){const auto id=*static_cast<const unsigned char*>(data);if(id>=28&&id<=34){if(!visible)OpenMenu(false);return visible?1:(oldVGUI?oldVGUI(name,size,data):1);}if(visible)Close(false);}
    return oldVGUI?oldVGUI(name,size,data):1;
}
int ShowMenu(const char* name,int size,void* data)
{
    const auto* b=static_cast<const unsigned char*>(data);
    if(capability&&b&&size==9&&b[3]==0&&!std::memcmp(b+4,"#Buy\0",5)){if(!visible)OpenMenu(false);if(visible)return 1;}
    return oldShowMenu?oldShowMenu(name,size,data):1;
}
int X(int x){return left+int(x*scale);}int Y(int y){return top+int(y*scale);}
void Image(const MenuTexture& t,const MenuRect& r,int alpha=255)
{
    auto[it,newly]=textures.try_emplace(t.path,0);
    if(newly)try{const auto image=DecodeTGA(ReadResource(t.path.c_str()));if(image.Width!=t.width||image.Height!=t.height)throw std::runtime_error("buy image dimensions changed");it->second=surface->CreateNewTextureID(true);surface->DrawSetTextureRGBA(it->second,image.RGBA.data(),image.Width,image.Height,1,true);}catch(const std::exception& e){engine.Con_Printf("CSOL buy image: %s\n",e.what());}
    if(it->second){surface->DrawSetColor(255,255,255,alpha);surface->DrawSetTexture(it->second);surface->DrawTexturedRect(X(r.x),Y(r.y),X(r.x+r.w),Y(r.y+r.h));}
}
void Text(int x,int y,const std::wstring& s,const char* color="basetext",bool title=false)
{
    const auto& c=presentation.colors.at(color);surface->DrawSetTextFont(title?titleFont:font);surface->DrawSetTextColor(c[0],c[1],c[2],c[3]);surface->DrawSetTextPos(X(x),Y(y));surface->DrawPrintText(s.c_str(),int(s.size()));
}
void Button(const MenuRect& r,const std::wstring& label,int hit,bool enabled=true)
{
    const int state=enabled&&hover==hit?(input&&input->IsMouseDown(vgui::MOUSE_LEFT)?2:1):0;const auto& b=presentation.buttons[state];
    Image(b[0],{r.x,r.y,b[0].width,r.h},enabled?255:100);Image(b[1],{r.x+b[0].width,r.y,r.w-b[0].width-b[2].width,r.h},enabled?255:100);Image(b[2],{r.x+r.w-b[2].width,r.y,b[2].width,r.h},enabled?255:100);
    Text(r.x+7,r.y+int((r.h-fontHeight/scale)/2),label,enabled&&hover==hit?"brightcontroltext":enabled?"basetext":"labeldimtext");
}
void Layout()
{
    int w,h;surface->GetScreenSize(w,h);scale=std::max(.1f,std::min(w/640.f,h/480.f));left=(w-int(presentation.width*scale))/2;top=(h-int(presentation.height*scale))/2;
    const int height=std::max(9,int(9*scale));if(font&&height==fontHeight)return;
    if(!font){
        HMODULE module{};if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&InitializeBuyMenu),&module))if(auto r=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10)))if(auto d=LoadResource(module,r)){DWORD n{};privateFont=AddFontMemResourceEx(LockResource(d),SizeofResource(module,r),nullptr,&n);}
        font=surface->CreateFont();titleFont=surface->CreateFont();
    }
    const char* name=privateFont?presentation.font.c_str():"Microsoft YaHei";fontHeight=height;
    surface->AddGlyphSetToFont(font,name,height,400,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);surface->AddGlyphSetToFont(titleFont,name,std::max(11,int(11*scale)),800,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);
}
int Hit(int x,int y)
{
    const float px=(x-left)/scale,py=(y-top)/scale;
    for(int i=0;i<10;++i)if(presentation.slots[i].Contains(px,py))return i;
    if(presentation.previous.Contains(px,py))return 10;if(presentation.next.Contains(px,py))return 11;
    if(presentation.quit.Contains(px,py)||MenuRect{presentation.width-25,5,20,20}.Contains(px,py))return 12;
    if(MenuRect{12,25,194,20}.Contains(px,py))return 13;return -1;
}
void Click(int hit)
{
    if(hit>=0&&hit<10)Select(hit);
    else if(hit==12)Close();
    else if(hit==13){category=-1;page=0;}
    else if(category>=0&&(hit==10||hit==11)){const auto& rows=Rows();page=std::clamp(page+(hit==10?-1:1),0,std::max(0,(int(rows.size())-1)/10));}
}
void Paint()
{
    if(!visible||!loaded||!surface)return;Layout();const auto root=surface->GetEmbeddedPanel();if(!root)return;surface->PushMakeCurrent(root,false);
    const auto& f=presentation.frame;const int xs[]{0,f[0].width,presentation.width-f[2].width,presentation.width},ys[]{0,f[0].height,presentation.height-f[6].height,presentation.height};
    for(int i=0;i<9;++i){int x=i%3,y=i/3;Image(f[i],{xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y]});}
    Text(16,6,Wide(presentation.title),"basetext",true);Button({12,25,194,20},category<0?L"武器分类":L"返回武器分类",13);
    const auto& rows=Rows();
    for(int i=0;i<10;++i){
        if(category<0)Button(presentation.slots[i],std::to_wstring((i+1)%10)+L"  "+categories[i],i,catalogReady&&!waiting);
        else if(page*10+i<int(rows.size())){const auto* item=rows[page*10+i];auto it=offers.find(item->id);const bool enabled=it!=offers.end()&&it->second.result==Result::Available&&balance>=it->second.price;
            Button(presentation.slots[i],std::to_wstring((i+1)%10)+L"  "+Wide(item->name),i,enabled&&!waiting);}
    }
    if(category>=0){Button(presentation.previous,L"上一页",10,page>0);Button(presentation.next,L"下一页",11,(page+1)*10<int(rows.size()));Text(97,308,std::to_wstring(page+1));}
    const Item* selected=nullptr;if(category>=0&&hover>=0&&hover<10&&page*10+hover<int(rows.size()))selected=rows[page*10+hover];
    else if(category>=0&&!rows.empty())selected=rows[page*10];
    if(selected){
        const auto it=presentation.images.find(selected->id);if(it!=presentation.images.end()){const auto& image=it->second;const auto& r=presentation.info;const float factor=std::min(float(r.w)/image.width,float(r.h-35)/image.height);const int w=int(image.width*factor),h=int(image.height*factor);Image(image,{r.x+(r.w-w)/2,r.y+(r.h-35-h)/2,w,h});}
        Text(presentation.info.x,presentation.info.y+presentation.info.h-25,Wide(selected->name),"brightcontroltext");
        auto offer=offers.find(selected->id);if(offer!=offers.end())Text(presentation.info.x,presentation.info.y+presentation.info.h-7,L"价格：$"+std::to_wstring(offer->second.price));
    }else Text(presentation.info.x,presentation.info.y+20,L"选择分类查看武器");
    Text(390,50,L"当前金钱","labeldimtext");Text(390,72,L"$ "+std::to_wstring(balance),"brightcontroltext",true);
    Text(390,112,L"购买后返回游戏","labeldimtext");Text(390,132,L"按 B / Esc 关闭","labeldimtext");
    Button(presentation.quit,L"返回游戏",12);Image(presentation.close[hover==12?1:0],{presentation.width-24,7,15,15});
    Text(16,presentation.height-38,status,"labeldimtext");surface->DrawFlushText();surface->PopMakeCurrent(root);++draws;
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks {
public:
    int GetAltitude()const override{return 31;}
    void Initialize(CreateInterfaceFn*,int)override{}void Start(cl_enginefuncs_s*,int)override{}void Shutdown()override{Close(false);}
    void Key_Event(int& down,int& key,const char*& binding,VGUI2Extension_CallbackContext* context)override{
        if(!visible||context->IsPost)return;
        if(binding&&(!std::strcmp(binding,"toggleconsole")||!std::strcmp(binding,"snapshot")||!std::strcmp(binding,"screenshot"))){if(down){if(!std::strcmp(binding,"toggleconsole"))Close(false);std::string command=std::string(binding)+"\n";engine.pfnClientCmd(command.data());}}
        else if(down){if(key==27||(binding&&!std::strcmp(binding,"buy")))Close();else if(key>='0'&&key<='9')Select(key=='0'?9:key-'1');else if(key==241){int x,y;input->GetCursorPos(x,y);mouseHeld=true;Click(Hit(x,y));}else if(key==149||key==150)Click(key==149?11:10);else if(key==8){category=-1;page=0;}}
        if(context->pPluginReturnValue)*static_cast<int*>(context->pPluginReturnValue)=1;context->Result=VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS;
    }
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int& message,unsigned int& wparam,long&,VGUI2Extension_CallbackContext* context)override{if(visible&&!context->IsPost&&(message==WM_KILLFOCUS||(message==WM_ACTIVATEAPP&&!wparam)))Close(false);}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)csol::client::Paint();}
    void HideGameUI(VGUI2Extension_CallbackContext*)override{}void HideConsole(VGUI2Extension_CallbackContext*)override{}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)Close(false);}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)Close(false);}
} overlay;
}
void InitializeBuyMenu()
{
    engine.pfnHookUserMsg(csol::buy::Message,Receive);oldBuy=api->HookCmd("buy",Open);oldCancel=api->HookCmd("cancelselect",Cancel);
    if(!oldBuy)engine.pfnAddCommand("buy",Open);
    engine.pfnAddCommand("csol_buymenu",Open);oldVGUI=api->HookUserMsg("VGUIMenu",VGUI);oldShowMenu=api->HookUserMsg("ShowMenu",ShowMenu);
    const auto factory=api->GetEngineFactory();surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
    const auto clientFactory=api->GetClientFactory();clientUI=clientFactory?static_cast<IClientVGUI*>(clientFactory(CLIENTVGUI_INTERFACE_VERSION,nullptr)):nullptr;
    auto module=GetModuleHandleA("vgui2.dll");const auto inputFactory=module?Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(module)):nullptr;input=inputFactory?static_cast<vgui::IInput*>(inputFactory(VGUI_INPUT_INTERFACE_VERSION,nullptr)):nullptr;
    const auto plugin=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");const auto pluginFactory=plugin?Sys_GetFactory(plugin):nullptr;extension=pluginFactory?static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    if(extension&&surface&&input)extension->RegisterBaseUICallbacks(&overlay);
}
void ResetBuyMenu()
{
    Close(false);capability=false;loaded=false;offers.clear();visibleRows.clear();rowsCategory=-2;restoreMouse=false;swallowMouse=false;request=0;presentation={};
    if(surface)for(const auto& [path,texture]:textures)if(texture)surface->DeleteTextureByID(texture);textures.clear();
}
void ShutdownBuyMenu(){ResetBuyMenu();if(extension)extension->UnregisterBaseUICallbacks(&overlay);if(privateFont)RemoveFontMemResourceEx(privateFont);privateFont=nullptr;extension=nullptr;input=nullptr;surface=nullptr;clientUI=nullptr;}
void BuyMenuFrame()
{
    if(!visible)return;if(!Focused()){Close(false);return;}Layout();
    if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();surface->UnlockCursor();surface->SetCursor(2);
    int x,y;input->GetCursorPos(x,y);hover=Hit(x,y);const bool pressed=input->IsMouseDown(vgui::MOUSE_LEFT);if(pressed&&!mouseHeld)Click(hover);mouseHeld=pressed;
    if((waiting||!catalogReady)&&engine.GetClientTime()-requestedAt>5){waiting=false;status=L"购买请求超时；请关闭后重试";}
}
void BuyMenuCreateMove(usercmd_t* cmd,int active)
{
    if(!cmd)return;
    if(!visible){if(active)RestoreMouse();if(swallowMouse){cmd->buttons&=~(IN_ATTACK|IN_ATTACK2);if(!input||(!input->IsMouseDown(vgui::MOUSE_LEFT)&&!input->IsMouseDown(vgui::MOUSE_RIGHT)))swallowMouse=false;}return;}
    cmd->buttons=0;cmd->forwardmove=cmd->sidemove=cmd->upmove=0;std::copy_n(angles,3,cmd->viewangles);engine.SetViewAngles(angles);
}
bool BuyMenuVisible(){return visible;}
void BuyMenuDiagnostics(std::ostream& out){out<<",\"buyMenu\":{\"capability\":"<<capability<<",\"visible\":"<<visible<<",\"loaded\":"<<loaded<<",\"waiting\":"<<waiting<<",\"offers\":"<<offers.size()<<",\"purchases\":"<<purchases<<",\"rejected\":"<<rejected<<",\"draws\":"<<draws<<",\"mouseRestores\":"<<restores<<'}';}
}
