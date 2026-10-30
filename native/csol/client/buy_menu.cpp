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
#include <vgui_controls/TextEntry.h>
#include <algorithm>
#include <cstring>
#include <ostream>
#include <cwctype>
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
bool rightHeld{},rebuying{};
int team=2;
cvar_t* favoriteCvars[2]{};
Favorites favorites{};
std::vector<int> lastPurchases,rebuyQueue;
std::wstring query;
vgui::DHANDLE<vgui::TextEntry> searchEntry;
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
constexpr const wchar_t* categories[]{L"手枪",L"霰弹枪",L"微型冲锋枪",L"自动步枪",L"机枪",L"主武器弹药",L"副武器弹药",L"装备",L"近身武器",L"结束武器选择"};
constexpr char favoriteKeys[]="sdfgzxcv";
void Layout();
void LoadFavorites(){favorites=ParseFavorites(favoriteCvars[team-1]?favoriteCvars[team-1]->string:"");}
void SaveFavorites(){const auto text=SerializeFavorites(favorites);engine.Cvar_Set(favoriteCvars[team-1]->name,text.c_str());}
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
    rebuying=false;rebuyQueue.clear();if(searchEntry){searchEntry->SetVisible(false);searchEntry->SetKeyBoardInputEnabled(false);}
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
    visible=true;restoreMouse=false;category=-1;page=0;hover=-1;rowsCategory=-2;query.clear();status=L"正在获取商品与余额…";LoadFavorites();
    if(!searchEntry&&g_pVGuiInput2&&g_pVGuiSurface2&&g_pVGui&&g_pVGuiPanel){
        searchEntry=new vgui::TextEntry(nullptr,"CSOLBuySearch");searchEntry->SetParent(surface->GetEmbeddedPanel());
        searchEntry->SetMaximumCharCount(48);searchEntry->SetCatchEnterKey(true);searchEntry->SetZPos(300);
    }
    if(searchEntry){searchEntry->SetText(L"");searchEntry->SetVisible(true);searchEntry->SetKeyBoardInputEnabled(false);}
    engine.GetViewAngles(angles);mouseHeld=input->IsMouseDown(vgui::MOUSE_LEFT);rightHeld=input->IsMouseDown(vgui::MOUSE_RIGHT);
    if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();surface->UnlockCursor();surface->SetCursor(2);Refresh();
}
void Open(){OpenMenu(true);}
void Cancel(){if(visible)Close();else if(oldCancel)oldCancel();}
const std::vector<const Item*>& Rows()
{
    if(rowsCategory!=category){visibleRows.clear();visibleRows.reserve(Catalog().size());rowsCategory=category;
        for(const auto& item:Catalog())if(InTeamList(item,team)&&item.id<2000&&(query.empty()?item.category==category:true)){
            auto text=Wide(item.name)+L" "+Wide(item.alias);std::transform(text.begin(),text.end(),text.begin(),std::towlower);
            auto needle=query;std::transform(needle.begin(),needle.end(),needle.begin(),std::towlower);
            if(needle.empty()||text.find(needle)!=text.npos)visibleRows.push_back(&item);
        }}
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
    if(row==9){if(category<0&&query.empty())Close();else{category=-1;query.clear();if(searchEntry)searchEntry->SetText(L"");page=0;rowsCategory=-2;}return;}
    if(category<0&&query.empty()){if(row==5||row==6){const auto* item=Find(row==5?2001:2002);if(item)Purchase(*item);}else{category=row+1;page=0;hover=-1;}return;}
    const auto& rows=Rows();const int index=page*9+row;if(index<int(rows.size()))Purchase(*rows[index]);
}
void RememberPurchase(int id){
    if(rebuying)return;const auto* item=Find(id);if(!item)return;
    const auto slot=[](const Item& v){return v.id>=1000?v.id==1002?1001:int(v.id):v.category==1?1:2;};
    std::erase_if(lastPurchases,[&](int previous){const auto* p=Find(previous);return p&&slot(*p)==slot(*item);});lastPurchases.push_back(id);
}
void ContinueRebuy(){
    if(!rebuying||waiting||!catalogReady)return;
    while(!rebuyQueue.empty()){
        const auto* item=Find(rebuyQueue.front());rebuyQueue.erase(rebuyQueue.begin());
        auto offer=item?offers.find(item->id):offers.end();
        if(item&&offer!=offers.end()&&offer->second.result==Result::Available&&balance>=offer->second.price){Purchase(*item);return;}
    }
    Close();
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
    case Kind::End:catalogReady=true;status=L"";break;
    case Kind::Result:
        waiting=false;
        if(p.result==Result::Success){++purchases;RememberPurchase(p.id);if(rebuying)Refresh();else Close();}
        else if(p.result==Result::Unavailable){status=L"当前状态无法购买；按 B / Esc 返回";}
        else{status=p.result==Result::Insufficient?L"金钱不足":L"购买未完成，请检查当前装备";}
        if(rebuying&&p.result!=Result::Success)Refresh();
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
    if(t.path.empty())return;
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
void Slot(const MenuRect& r,const std::wstring& label,int hit,bool enabled=true){
    if(presentation.keyboard.path.empty()){Button(r,std::to_wstring((hit+1)%10)+L"  "+label,hit,enabled);return;}
    Image(presentation.slotBackground[enabled&&hover==hit?1:0],r,enabled?255:110);
    Image(presentation.keyboard,{r.x+3,r.y+4,17,17},enabled?255:110);
    Text(r.x+10,r.y+7,std::to_wstring((hit+1)%10),enabled?"basetext":"labeldimtext");
    Text(r.x+32,r.y+7,label,enabled?"basetext":"labeldimtext");
}
void ItemImage(const Item& item,MenuRect area,int alpha=255){
    const auto it=presentation.images.find(item.id);if(it==presentation.images.end())return;
    const auto& image=it->second;const float fit=std::min(float(area.w)/image.width,float(area.h)/image.height);
    const int w=int(image.width*fit),h=int(image.height*fit);Image(image,{area.x+(area.w-w)/2,area.y+(area.h-h)/2,w,h},alpha);
}
bool CanPurchase(const Item& item){const auto offer=offers.find(item.id);return catalogReady&&!waiting&&offer!=offers.end()&&offer->second.result==Result::Available&&balance>=offer->second.price;}
void Layout()
{
    int w,h;surface->GetScreenSize(w,h);scale=std::max(.1f,std::min(float(w)/presentation.width,float(h)/presentation.height));left=(w-int(presentation.width*scale))/2;top=(h-int(presentation.height*scale))/2;
    if(searchEntry){const auto& r=presentation.search;searchEntry->SetBounds(X(r.x),Y(r.y),int(r.w*scale),int(r.h*scale));}
    const int height=std::max(9,int(9*scale));if(font&&height==fontHeight)return;
    if(!font){
        HMODULE module{};if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&InitializeBuyMenu),&module))if(auto r=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10)))if(auto d=LoadResource(module,r)){DWORD n{};privateFont=AddFontMemResourceEx(LockResource(d),SizeofResource(module,r),nullptr,&n);}
        font=surface->CreateFont();titleFont=surface->CreateFont();
    }
    const char* name=privateFont?presentation.font.c_str():"Microsoft YaHei";fontHeight=height;
    surface->AddGlyphSetToFont(font,name,height,0,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);surface->AddGlyphSetToFont(titleFont,name,std::max(11,int(11*scale)),800,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);
    if(searchEntry)searchEntry->SetFont(font);
}
int Hit(int x,int y)
{
    const float px=(x-left)/scale,py=(y-top)/scale;
    for(int i=0;i<10;++i)if(presentation.slots[i].Contains(px,py))return i;
    if(presentation.previous.Contains(px,py))return 10;if(presentation.next.Contains(px,py))return 11;
    if(MenuRect{presentation.width-25,5,20,20}.Contains(px,py))return 12;
    for(int i=0;i<2;++i)if(presentation.teams[i].Contains(px,py))return 20+i;
    for(int i=0;i<8;++i)if(presentation.favorites[i].Contains(px,py))return 30+i;
    if(presentation.search.Contains(px,py))return 40;
    if(presentation.rebuy.Contains(px,py))return 41;
    return -1;
}
void Click(int hit)
{
    if(searchEntry&&hit!=40)searchEntry->SetKeyBoardInputEnabled(false);
    if(hit>=0&&hit<10)Select(hit);
    else if(hit==12)Close();
    else if(hit==20||hit==21){team=hit-19;page=0;rowsCategory=-2;LoadFavorites();}
    else if(hit>=30&&hit<38){if(const auto* item=Find(favorites[hit-30]))Purchase(*item);}
    else if(hit==40){if(searchEntry){searchEntry->SetKeyBoardInputEnabled(true);searchEntry->RequestFocus();}}
    else if(hit==41&&!waiting&&!lastPurchases.empty()){
        rebuyQueue=lastPurchases;
        // A later weapon replacement must be bought before its remembered ammo.
        const auto rank=[](int id){return id>=2000?2:id>=1000?1:0;};
        std::stable_sort(rebuyQueue.begin(),rebuyQueue.end(),[&](int a,int b){return rank(a)<rank(b);});
        rebuying=true;ContinueRebuy();
    }
    else if((category>=0||!query.empty())&&(hit==10||hit==11)){const auto& rows=Rows();page=std::clamp(page+(hit==10?-1:1),0,std::max(0,(int(rows.size())-1)/9));}
}
void EditFavorite(int hit){
    if(hit>=30&&hit<38){favorites[hit-30]=0;SaveFavorites();return;}
    if(hit<0||hit>=9||(category<0&&query.empty()))return;
    const auto& rows=Rows();if(page*9+hit>=int(rows.size()))return;const auto id=rows[page*9+hit]->id;
    const auto found=std::find(favorites.begin(),favorites.end(),id);
    if(found!=favorites.end()){*found=0;status=L"已取消收藏";}
    else if(auto empty=std::find(favorites.begin(),favorites.end(),0);empty!=favorites.end()){*empty=id;status=L"已加入收藏";}
    else{status=L"收藏已满，右键点击收藏武器可移除";return;}
    SaveFavorites();
}
void Paint()
{
    if(!visible||!loaded||!surface)return;Layout();const auto root=surface->GetEmbeddedPanel();if(!root)return;surface->PushMakeCurrent(root,false);
    int screenWidth,screenHeight;surface->GetScreenSize(screenWidth,screenHeight);surface->DrawSetColor(0,0,0,100);surface->DrawFilledRect(0,0,screenWidth,screenHeight);
    const auto& f=presentation.frame;const int xs[]{0,f[0].width,presentation.width-f[2].width,presentation.width},ys[]{0,f[0].height,presentation.height-f[6].height,presentation.height};
    for(int i=0;i<9;++i){int x=i%3,y=i/3;Image(f[i],{xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y]});}
    const auto title=Wide(presentation.title);int titleWidth,titleHeight;surface->GetTextSize(titleFont,title.c_str(),titleWidth,titleHeight);Text((presentation.width-int(titleWidth/scale))/2,3,title,"basetext",true);
    Text(14,24,L"玩家武器列表");
    for(int i=0;i<2;++i){const auto& r=presentation.teams[i];Button(r,i?L"查看CT阵营武器":L"查看T阵营武器",20+i);if(team==i+1){surface->DrawSetColor(170,190,210,240);surface->DrawFilledRect(X(r.x),Y(r.y+r.h-1),X(r.x+r.w),Y(r.y+r.h));}}
    const auto& rows=Rows();
    for(int i=0;i<10;++i){
        if(category<0&&query.empty())Slot(presentation.slots[i],categories[i],i,i==9||catalogReady&&!waiting);
        else if(i==9)Slot(presentation.slots[i],L"返回武器分类",i);
        else if(page*9+i<int(rows.size())){const auto* item=rows[page*9+i];Slot(presentation.slots[i],Wide(item->name),i,CanPurchase(*item));}
    }
    if(category>=0||!query.empty()){if(page>0)Button(presentation.previous,L"上一页",10);if((page+1)*9<int(rows.size()))Button(presentation.next,L"下一页",11);if(rows.empty())Text(presentation.info.x,presentation.info.y,L"暂无可用武器","labeldimtext");}
    const Item* selected=nullptr;if((category>=0||!query.empty())&&hover>=0&&hover<9&&page*9+hover<int(rows.size()))selected=rows[page*9+hover];
    else if((category>=0||!query.empty())&&!rows.empty()&&page*9<int(rows.size()))selected=rows[page*9];
    if(hover>=30&&hover<38)selected=Find(favorites[hover-30]);
    if(selected){
        const auto& r=presentation.info;ItemImage(*selected,{r.x,r.y,r.w,r.h-35});
        Text(presentation.info.x,presentation.info.y+presentation.info.h-25,Wide(selected->name),"brightcontroltext");
        auto offer=offers.find(selected->id);if(offer!=offers.end())Text(presentation.info.x,presentation.info.y+presentation.info.h-7,L"价格：$"+std::to_wstring(offer->second.price));
    }
    Text(414,32,L"收藏武器");
    for(int i=0;i<8;++i){const auto& r=presentation.favorites[i];const auto* item=Find(favorites[i]);
        Image(presentation.favoriteBackground,r);if(hover==30+i)Image(presentation.slotBackground[1],r,130);
        Image(presentation.keyboard,{r.x+3,r.y+7,17,17});Text(r.x+9,r.y+11,std::wstring(1,wchar_t(std::towupper(favoriteKeys[i]))));
        if(item)ItemImage(*item,{r.x+24,r.y+2,r.w-27,r.h-4},CanPurchase(*item)?255:110);
    }
    Text(375,202,L"右键点击武器可收藏，右键点击收藏可移除。","labeldimtext");
    const auto& account=presentation.account;surface->DrawSetColor(0,0,0,130);surface->DrawFilledRect(X(account.x),Y(account.y),X(account.x+account.w),Y(account.y+account.h));
    Text(account.x+2,account.y+4,L"当前金钱");const auto money=std::to_wstring(balance);int moneyWidth,moneyHeight;surface->GetTextSize(font,money.c_str(),moneyWidth,moneyHeight);Text(account.x+account.w-4-int(moneyWidth/scale),account.y+4,money,"brightcontroltext");
    Button(presentation.rebuy,L"重新购买（R）",41,catalogReady&&!waiting&&!lastPurchases.empty());
    Text(16,presentation.height-28,status,"labeldimtext");surface->DrawFlushText();surface->PopMakeCurrent(root);
    if(searchEntry)vgui::ipanel()->PaintTraverse(searchEntry->GetVPanel(),true);++draws;
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks {
public:
    int GetAltitude()const override{return 31;}
    void Initialize(CreateInterfaceFn*,int)override{}void Start(cl_enginefuncs_s*,int)override{}void Shutdown()override{Close(false);}
    void Key_Event(int& down,int& key,const char*& binding,VGUI2Extension_CallbackContext* context)override{
        if(!visible||context->IsPost)return;
        // Let the native VGUI text entry own Unicode/IME, editing, and paste.
        // Its key focus consumes bindings instead of interpreting B/R as buys.
        if(searchEntry&&searchEntry->HasFocus()&&key!=27)return;
        if(binding&&(!std::strcmp(binding,"toggleconsole")||!std::strcmp(binding,"snapshot")||!std::strcmp(binding,"screenshot"))){if(down){if(!std::strcmp(binding,"toggleconsole"))Close(false);std::string command=std::string(binding)+"\n";engine.pfnClientCmd(command.data());}}
        else if(down){if(key==27||(binding&&!std::strcmp(binding,"buy")))Close();else if(key>='0'&&key<='9')Select(key=='0'?9:key-'1');else if(key==241){int x,y;input->GetCursorPos(x,y);mouseHeld=true;Click(Hit(x,y));}else if(key==242){int x,y;input->GetCursorPos(x,y);rightHeld=true;EditFavorite(Hit(x,y));}else if(key==149||key==150)Click(key==149?11:10);else if(key=='j')Click(team==1?21:20);else if(key=='r')Click(41);else if(key>0&&key<128&&std::strchr(favoriteKeys,key))Click(30+int(std::strchr(favoriteKeys,key)-favoriteKeys));else if(key==8)Select(9);}
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
    favoriteCvars[0]=engine.pfnRegisterVariable("csol_buy_favorites_t","0,0,0,0,0,0,0,0",FCVAR_ARCHIVE);
    favoriteCvars[1]=engine.pfnRegisterVariable("csol_buy_favorites_ct","0,0,0,0,0,0,0,0",FCVAR_ARCHIVE);
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
    Close(false);capability=false;loaded=false;offers.clear();visibleRows.clear();rowsCategory=-2;restoreMouse=false;swallowMouse=false;request=0;presentation={};lastPurchases.clear();query.clear();
    if(surface)for(const auto& [path,texture]:textures)if(texture)surface->DeleteTextureByID(texture);textures.clear();
}
void ShutdownBuyMenu(){ResetBuyMenu();if(searchEntry){searchEntry->MarkForDeletion();searchEntry=nullptr;}if(extension)extension->UnregisterBaseUICallbacks(&overlay);if(privateFont)RemoveFontMemResourceEx(privateFont);privateFont=nullptr;extension=nullptr;input=nullptr;surface=nullptr;clientUI=nullptr;}
void BuyMenuFrame()
{
    if(!visible)return;if(!Focused()){Close(false);return;}Layout();
    if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();surface->UnlockCursor();surface->SetCursor(2);
    int x,y;input->GetCursorPos(x,y);hover=Hit(x,y);const bool pressed=input->IsMouseDown(vgui::MOUSE_LEFT);if(pressed&&!mouseHeld)Click(hover);mouseHeld=pressed;
    const bool right=input->IsMouseDown(vgui::MOUSE_RIGHT);if(right&&!rightHeld)EditFavorite(hover);rightHeld=right;
    if(searchEntry){wchar_t value[49]{};searchEntry->GetText(value,sizeof(value));if(query!=value){query=value;rowsCategory=-2;page=0;}}
    ContinueRebuy();
    if((waiting||!catalogReady)&&engine.GetClientTime()-requestedAt>5){waiting=false;catalogReady=false;rebuying=false;rebuyQueue.clear();status=L"购买请求超时；请关闭后重试";}
}
void BuyMenuCreateMove(usercmd_t* cmd,int active)
{
    if(!cmd)return;
    if(!visible){if(active)RestoreMouse();if(swallowMouse){cmd->buttons&=~(IN_ATTACK|IN_ATTACK2);if(!input||(!input->IsMouseDown(vgui::MOUSE_LEFT)&&!input->IsMouseDown(vgui::MOUSE_RIGHT)))swallowMouse=false;}return;}
    cmd->buttons=0;cmd->forwardmove=cmd->sidemove=cmd->upmove=0;std::copy_n(angles,3,cmd->viewangles);engine.SetViewAngles(angles);
}
bool BuyMenuVisible(){return visible;}
void BuyMenuDiagnostics(std::ostream& out){out<<",\"buyMenu\":{\"capability\":"<<capability<<",\"visible\":"<<visible<<",\"loaded\":"<<loaded<<",\"waiting\":"<<waiting<<",\"team\":"<<team<<",\"category\":"<<category<<",\"search\":"<<bool(searchEntry)<<",\"offers\":"<<offers.size()<<",\"purchases\":"<<purchases<<",\"rejected\":"<<rejected<<",\"draws\":"<<draws<<",\"mouseRestores\":"<<restores<<'}';}
}
