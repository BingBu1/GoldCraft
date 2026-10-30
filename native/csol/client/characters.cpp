// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "characters.hpp"
#include "weapon_inventory.hpp"
#include "../character_protocol.hpp"
#include "../character_menu.hpp"
#include "../image_codec.hpp"
#include <VGUI/ISurface.h>
#include <VGUI/IInput.h>
#include <VGUI/IClientVGUI.h>
#include <IVGUI2Extension.h>
#include <usercmd.h>
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
std::vector<Character> catalog;
std::array<CharacterState,33> characters;
vgui::ISurface* surface{};
vgui::IInput* input{};
IVGUI2Extension* extension{};
IClientVGUI* clientUI{};
vgui::HFont font{},titleFont{};
bool visible{},loaded{},selectionPending{};
bool serverCharacters{},appearanceMenu{};
bool focusSuspended{};
pfnUserMsgHook previousVGUI{},previousShowMenu{};
std::vector<std::vector<unsigned char>> menuFragments;
std::string menuText;
bool mouseFromEngine{};
bool originalMode{},originalLoaded{};
cvar_t* menuStyle{};
CharacterMenu original;
std::unordered_map<std::string,int> menuTextures;
std::unordered_map<unsigned,std::wstring> descriptions;
vgui::HFont originalFont{},originalTitle{};
HANDLE privateFont{};
int originalFontHeight{},hovered=-1,pendingId{},detailScroll{};
unsigned detailId{};
std::vector<std::wstring> detailLines;
int team=2,page{},chosen{},screenW{},screenH{},left{},top{},fontHeight{},lastButtons{};
float scale=1;
float menuAngles[3]{};
double nextSync{};
std::wstring status;
std::unordered_map<unsigned,int> portraits;
unsigned rejected{},messages{},draws{};
std::wstring Wide(const std::string& text)
{
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(size<=0)return L"";
    std::wstring out(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),size);return out;
}
void Close()
{
    if(!visible)return;
    visible=false;lastButtons=0;selectionPending=false;focusSuspended=false;
    appearanceMenu=false;
    pendingId=0;hovered=-1;
    if(surface)surface->SetCursor(1);
    if(downstream.IN_ActivateMouse)downstream.IN_ActivateMouse();
}
void UpdateFocus(bool focused)
{
    if(!visible)return;
    if(!focused){
        if(!appearanceMenu){Close();return;}
        focusSuspended=true;lastButtons=0;
    } else if(focusSuspended){
        focusSuspended=false;
        // The click which brought the window forward must not select a role.
        lastButtons=input && input->IsMouseDown(vgui::MOUSE_LEFT);
    }
}
void Cancel()
{
    const bool returnToTeams=appearanceMenu;
    Close();
    if(returnToTeams)engine.pfnServerCmd("csol_character_cancel\n");
}
bool Load()
{
    if(loaded)return true;
    try {auto bytes=ReadResource("models/csol/characters.json");catalog=ParseCharacters({reinterpret_cast<char*>(bytes.data()),bytes.size()});loaded=true;return true;}
    catch(const std::exception& error){engine.Con_Printf("CSOL: 人物资源尚未就绪：%s\n",error.what());return false;}
}
bool LoadOriginal()
{
    if(originalLoaded)return true;
    try {
        auto bytes=ReadResource("models/csol/classmenu2.json");original=ParseCharacterMenu({reinterpret_cast<char*>(bytes.data()),bytes.size()});
        descriptions.clear();for(const auto& [id,detail]:original.descriptions)descriptions.emplace(id,Wide(detail.text));
        originalLoaded=true;return true;
    }catch(const std::exception& e){engine.Con_Printf("CSOL: 原版菜单资源尚未就绪：%s\n",e.what());return false;}
}
void Layout()
{
    if(!surface)return;
    surface->GetScreenSize(screenW,screenH);
    scale=originalMode?std::min(float(screenW)/original.width,float(screenH)/original.height):std::min({screenW/640.f,screenH/460.f,1.6f});
    scale=std::max(.1f,scale);
    left=(screenW-int((originalMode?original.width:606)*scale))/2;
    top=(screenH-int((originalMode?original.height:420)*scale))/2;
}
std::vector<const Character*> Filtered()
{
    std::vector<const Character*> rows;for(const auto& c:catalog)if(c.team==team)rows.push_back(&c);return rows;
}
int PageSize(){return originalMode?9:10;}
void Open()
{
    if(visible){Close();return;}
    if(!surface || !Load())return;
    originalMode=menuStyle && menuStyle->value!=0 && LoadOriginal();
    Layout();hovered=-1;detailScroll=0;detailId=0;
    const auto* current=ViewCharacter();if(current){team=current->team;chosen=current->id;}
    auto rows=Filtered();page=0;
    for(size_t i=0;i<rows.size();++i)if(rows[i]->id==chosen)page=int(i/PageSize());
    if(!chosen && !rows.empty())chosen=rows.front()->id;
    status=L"选择角色后点击「使用角色」";visible=true;selectionPending=false;focusSuspended=false;engine.GetViewAngles(menuAngles);
    mouseFromEngine=false;lastButtons=input && input->IsMouseDown(vgui::MOUSE_LEFT);
    if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();
    surface->UnlockCursor();surface->SetCursor(2);
    engine.pfnServerCmd("csol_character_sync\n");
}
void SetMenuMode(bool value)
{
    if(engine.Cvar_SetValue)engine.Cvar_SetValue("csol_character_menu",value?1:0);
    if(visible){originalMode=value && LoadOriginal();hovered=-1;detailId=0;detailScroll=0;Layout();}
}
void OpenOriginal(){SetMenuMode(true);if(visible)Close();Open();}
void OpenSimple(){SetMenuMode(false);if(visible)Close();Open();}
bool OpenAppearance(int requestedTeam)
{
    if(!serverCharacters || !surface || !extension || !Load() ||
        (menuStyle && menuStyle->value!=0 && !LoadOriginal()))return false;
    // The team panel can still be visible when jointeam was issued outside
    // its own button handler. Close it through the client's public interface.
    if(clientUI)clientUI->HideAllVGUIMenu();
    if(visible)Close();
    Open();if(!visible)return false;
    appearanceMenu=true;team=requestedTeam;page=0;
    auto rows=Filtered();
    const auto* current=FindCharacter(catalog,chosen);
    if(!current || current->team!=team)chosen=rows.empty()?0:rows.front()->id;
    for(size_t i=0;i<rows.size();++i)if(rows[i]->id==chosen)page=int(i/PageSize());
    // Clear the native HUD menu and the inventory hook's number-key lease.
    unsigned char clear[5]{};
    if(previousShowMenu)previousShowMenu("ShowMenu",sizeof(clear),clear);
    status=L"选择角色进入游戏";
    return true;
}
int VGUIMenu(const char* name,int size,void* data)
{
    if(data && size>=1){
        const auto id=*static_cast<const unsigned char*>(data);
        if((id==26 || id==27) && OpenAppearance(id==26?1:2))return 1;
        if(appearanceMenu)Close();
    }
    return previousVGUI?previousVGUI(name,size,data):1;
}
int FlushMenuFragments()
{
    int result=1;
    for(auto& packet:menuFragments)if(previousShowMenu)result=previousShowMenu("ShowMenu",int(packet.size()),packet.data());
    menuFragments.clear();menuText.clear();return result;
}
int ShowMenu(const char* name,int size,void* data)
{
    const auto* bytes=static_cast<const unsigned char*>(data);
    const auto* end=data && size>=5?static_cast<const unsigned char*>(std::memchr(bytes+4,0,size-4)):nullptr;
    if(!end || end!=bytes+size-1 || menuText.size()+size>4096){
        FlushMenuFragments();return previousShowMenu?previousShowMenu(name,size,data):1;
    }
    menuFragments.emplace_back(bytes,bytes+size);menuText.append(reinterpret_cast<const char*>(bytes+4),end-bytes-4);
    if(bytes[3])return 1;
    const int requestedTeam=menuText=="#Terrorist_Select"?1:menuText=="#CT_Select"?2:0;
    if(requestedTeam && OpenAppearance(requestedTeam)){menuFragments.clear();menuText.clear();return 1;}
    if(appearanceMenu)Close();
    return FlushMenuFragments();
}
void Select()
{
    const auto* c=FindCharacter(catalog,chosen);if(!c)return;
    selectionPending=true;pendingId=c->id;std::string command="csol_character_select "+std::to_string(c->id)+"\n";engine.pfnServerCmd(command.data());
    status=L"正在选择角色…";
}
void ChangePage(int delta)
{
    auto rows=Filtered();page=std::clamp(page+delta,0,std::max(0,(int(rows.size())-1)/PageSize()));
    if(originalMode && !rows.empty()){chosen=rows[page*PageSize()]->id;detailId=0;detailScroll=0;}
    hovered=-1;
}
void SelectRow(int row)
{
    const auto rows=Filtered();if(rows.empty())return;
    // 0 is automatic selection; it must never address a tenth character.
    const int index=originalMode && row==9?engine.pfnRandomLong(0,int(rows.size())-1):page*PageSize()+row;
    if(index<0 || index>=int(rows.size()))return;
    chosen=rows[index]->id;detailScroll=0;if(originalMode)Select();
}
int OriginalHit(float x,float y)
{
    for(int i=0;i<10;++i)if(original.slots[i].Contains(x,y))return i;
    for(int i=0;i<2;++i)if(original.teams[i].Contains(x,y))return 10+i;
    if(original.previous.Contains(x,y))return 12;if(original.next.Contains(x,y))return 13;
    const auto close=original.closeButton.w?original.closeButton:MenuRect{original.width-25,6,17,17};
    if(close.Contains(x,y))return 14;
    return -1;
}
void Hover(int x,int y)
{
    if(!originalMode)return;
    const int hit=OriginalHit((x-left)/scale,(y-top)/scale);
    if(hit!=hovered && hit>=0 && hit<PageSize()){auto rows=Filtered();const int index=page*PageSize()+hit;if(index<int(rows.size())){chosen=rows[index]->id;detailScroll=0;}}
    hovered=hit;
}
void Click(int x,int y)
{
    const float px=(x-left)/scale,py=(y-top)/scale;
    if(originalMode){
        const int hit=OriginalHit(px,py);auto rows=Filtered();
        if(hit>=0 && hit<10)SelectRow(hit);
        else if(hit==10 || hit==11){team=hit-9;page=0;rows=Filtered();if(!rows.empty())chosen=rows.front()->id;detailScroll=0;}
        else if(hit==12 || hit==13)ChangePage(hit==12?-1:1);
        else if(hit==14)Cancel();else if(hit==15)SetMenuMode(false);
        return;
    }
    if(py>=370 && py<394 && px>=240 && px<350){SetMenuMode(true);return;}
    if(py>=25 && py<48 && px>=16 && px<216){team=px<116?1:2;page=0;auto rows=Filtered();if(!rows.empty())chosen=rows.front()->id;return;}
    auto rows=Filtered();
    if(px>=16 && px<216 && py>=50 && py<300){const size_t i=page*10+int((py-50)/25);if(i<rows.size())chosen=rows[i]->id;return;}
    if(py>=312 && py<338){if(px>=16 && px<88)page=std::max(0,page-1);if(px>=144 && px<216)page=std::min(int((rows.size()-1)/10),page+1);return;}
    if(py>=370 && py<402){if(px>=360 && px<480)Select();else if(px>=488 && px<588)Cancel();}
}
int Message(const char*,int size,void* data)
{
    CharacterState value;
    if(!data || size<0 || !DecodeCharacter({static_cast<const std::uint8_t*>(data),size_t(size)},value)){++rejected;return 0;}
    if(value.slot && value.id && (!Load() || !FindCharacter(catalog,value.id))){++rejected;return 0;}
    serverCharacters=true;
    ++messages;if(!value.slot)characters={};else characters[value.slot]=value;
    if(visible && selectionPending && (value.active || appearanceMenu) && value.id==pendingId){auto* local=engine.GetLocalPlayer();if(local && local->index==value.slot){status=L"角色已切换";Close();}}
    return 1;
}
int X(float x){return left+int(x*scale);}int Y(float y){return top+int(y*scale);}
void Box(int x,int y,int w,int h,int r,int g,int b,int a=255)
{surface->DrawSetColor(r,g,b,a);surface->DrawFilledRect(X(x),Y(y),X(x+w),Y(y+h));}
void Text(int x,int y,const std::wstring& text,int r=225,int g=229,int b=231,bool title=false)
{surface->DrawSetTextFont(title?titleFont:font);surface->DrawSetTextColor(r,g,b,255);surface->DrawSetTextPos(X(x),Y(y));surface->DrawPrintText(text.c_str(),int(text.size()));}
void MenuImage(const MenuTexture& image,const MenuRect& rect,int alpha=255)
{
    auto [it,inserted]=menuTextures.try_emplace(image.path,0);
    if(inserted){try{const auto decoded=DecodeTGA(ReadResource(image.path.c_str()));
        if(decoded.Width!=image.width || decoded.Height!=image.height)throw std::runtime_error("menu image dimensions changed");
        it->second=surface->CreateNewTextureID(true);surface->DrawSetTextureRGBA(it->second,decoded.RGBA.data(),decoded.Width,decoded.Height,1,true);
    }catch(const std::exception& e){engine.Con_Printf("CSOL menu: %s\n",e.what());}}
    if(it->second){surface->DrawSetColor(255,255,255,alpha);surface->DrawSetTexture(it->second);surface->DrawTexturedRect(X(rect.x),Y(rect.y),X(rect.x+rect.w),Y(rect.y+rect.h));}
}
void OriginalText(int x,int y,const std::wstring& value,const char* color="basetext",bool title=false)
{
    const auto& c=original.colors.at(color);surface->DrawSetTextFont(title?originalTitle:originalFont);
    surface->DrawSetTextColor(c[0],c[1],c[2],c[3]);surface->DrawSetTextPos(X(x),Y(y));surface->DrawPrintText(value.c_str(),int(value.size()));
}
void OriginalCenteredText(const MenuRect& r,const std::wstring& value,const char* color="basetext",bool title=false)
{
    int w,h;surface->GetTextSize(title?originalTitle:originalFont,value.c_str(),w,h);
    OriginalText(r.x+int((r.w-w/scale)/2),r.y+int((r.h-h/scale)/2),value,color,title);
}
void OriginalButton(const MenuRect& r,const std::wstring& label,int hit,bool enabled=true,bool selected=false)
{
    const int state=hovered==hit?(input && input->IsMouseDown(vgui::MOUSE_LEFT)?2:1):(selected?2:0);
    const auto& tiles=original.buttons[state];const int a=std::min(tiles[0].width,r.w/2),b=std::min(tiles[2].width,r.w/2);
    MenuImage(tiles[0],{r.x,r.y,a,r.h},enabled?255:90);MenuImage(tiles[1],{r.x+a,r.y,r.w-a-b,r.h},enabled?255:90);MenuImage(tiles[2],{r.x+r.w-b,r.y,b,r.h},enabled?255:90);
    OriginalCenteredText(r,label,hovered==hit || selected?"brightcontroltext":"basetext");
}
void PrepareOriginalFont()
{
    const int size=std::max(9,int(original.fontSize*scale));if(originalFont && size==originalFontHeight)return;
    if(!originalFont){
        HMODULE module{};
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&InitializeCharacters),&module)){
            if(auto resource=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10))){
                if(auto data=LoadResource(module,resource)){DWORD count{};privateFont=AddFontMemResourceEx(LockResource(data),SizeofResource(module,resource),nullptr,&count);}
            }
        }
        originalFont=surface->CreateFont();originalTitle=surface->CreateFont();
    }
    const char* name=privateFont?original.font.c_str():"Microsoft YaHei";
    surface->AddGlyphSetToFont(originalFont,name,size,original.fontWeight,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);
    surface->AddGlyphSetToFont(originalTitle,name,std::max(11,int(original.titleSize*scale)),original.titleWeight,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);
    originalFontHeight=size;detailId=0;
}
void PaintOriginal()
{
    PrepareOriginalFont();const auto& f=original.frame;
    surface->DrawSetColor(0,0,0,150);surface->DrawFilledRect(0,0,screenW,screenH);
    // Keep the content aspect ratio while the frame covers wide displays.
    const int frameLeft=original.fullScreenFrame?7-int(left/scale):0;
    const int frameTop=original.fullScreenFrame?11-int(top/scale):0;
    const int frameRight=original.fullScreenFrame?int((screenW-left)/scale):original.width;
    const int frameBottom=original.fullScreenFrame?int((screenH-top)/scale):original.height;
    const int xs[]{frameLeft,frameLeft+f[0].width,frameRight-f[2].width,frameRight},ys[]{frameTop,frameTop+f[0].height,frameBottom-f[6].height,frameBottom};
    for(int i=0;i<9;++i){const int x=i%3,y=i/3;MenuImage(f[i],{xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y]});}
    OriginalCenteredText(original.title.w?original.title:MenuRect{0,6,original.width,14},L"选择角色","basetext",true);
    for(int i=0;i<2;++i)OriginalButton(original.teams[i],Wide(original.teamNames[i]),10+i,true,team==i+1);
    const auto rows=Filtered();
    for(int i=0;i<10;++i){const int index=page*PageSize()+i;const auto& r=original.slots[i];const auto* c=i<9 && index<int(rows.size())?rows[index]:nullptr;
        if(i<9 && !c)continue;
        if(!original.keyboard.path.empty()){
            MenuImage(original.slotBackground[hovered==i || (c && chosen==c->id)?1:0],r);
            MenuImage(original.keyboard,{r.x+3,r.y+4,17,r.h-7});
            OriginalCenteredText({r.x+3,r.y+4,17,r.h-7},std::to_wstring((i+1)%10));
        }else OriginalButton(r,L"",i);
        OriginalText(r.x+32,r.y+int((r.h-originalFontHeight/scale)/2),c?Wide(c->name):L"自动选择",c && chosen==c->id?"brightcontroltext":"basetext");
        const auto* current=ViewCharacter();
        if(!appearanceMenu && c && current && current->id==c->id && !original.pin.path.empty())MenuImage(original.pin,{r.x+r.w-19,r.y+5,13,13});}
    if(page>0)OriginalButton(original.previous,Wide(original.previousLabel),12);
    if((page+1)*PageSize()<int(rows.size()))OriginalButton(original.next,Wide(original.nextLabel),13);
    OriginalCenteredText(original.pageLabel.w?original.pageLabel:MenuRect{88,307,56,17},std::to_wstring(page+1)+L" / "+std::to_wstring(std::max(1,(int(rows.size())+PageSize()-1)/PageSize())));
    const auto* c=FindCharacter(catalog,chosen);
    if(c){
        const auto found=original.descriptions.find(c->id);
        if(found!=original.descriptions.end()){
            const auto& d=found->second;MenuRect r=d.image;r.x+=original.info.x;r.y+=original.info.y;
            auto it=portraits.find(c->id);
            if(it==portraits.end()){int texture=0;try{const auto image=DecodeTGA(ReadResource(c->portrait.c_str()));texture=surface->CreateNewTextureID(true);surface->DrawSetTextureRGBA(texture,image.RGBA.data(),image.Width,image.Height,1,true);}catch(const std::exception& e){engine.Con_Printf("CSOL portrait: %s\n",e.what());}it=portraits.emplace(c->id,texture).first;}
            if(it->second){surface->DrawSetColor(255,255,255,255);surface->DrawSetTexture(it->second);surface->DrawTexturedRect(X(r.x),Y(r.y),X(r.x+r.w),Y(r.y+r.h));}
            if(d.defaultCaps && original.stats.maximum){
                const auto& stats=original.stats;
                OriginalText(stats.heading.x,stats.heading.y,Wide(stats.headingText),"statheading");
                const auto& color=original.colors.at("statbar");
                for(size_t i=0;i<stats.names.size();++i){
                    OriginalText(stats.labels[i].x,stats.labels[i].y,Wide(stats.names[i]));
                    const auto& bar=stats.bars[i];const int value=(*d.defaultCaps)[i];
                    const int width=bar.w*std::min(value,stats.maximum)/stats.maximum;
                    if(width)Box(bar.x,bar.y,width,bar.h,color[0],color[1],color[2],color[3]);
                    OriginalText(stats.values[i].x,stats.values[i].y,std::to_wstring(value),"statvalue");
                }
            }
            const int x=original.info.x+d.description.x,y=original.info.y+d.description.y,width=std::min(d.description.w,original.width-x-16);
            if(detailId!=c->id){
                detailLines.clear();std::wstring line;int used=0;
                for(wchar_t ch:descriptions[c->id]){if(ch=='\r')continue;wchar_t character[]{ch,0};int w,h;surface->GetTextSize(originalFont,character,w,h);
                    if(ch=='\n' || (used+w>width*scale && !line.empty())){detailLines.push_back(std::move(line));line.clear();used=0;}
                    if(ch!='\n'){line+=ch;used+=w;}}
                if(!line.empty())detailLines.push_back(std::move(line));detailId=c->id;
            }
            const float lineHeight=(originalFontHeight+2)/scale;
            const int capacity=std::max(1,int(std::min(d.description.h,original.height-y-60)/lineHeight));
            detailScroll=std::clamp(detailScroll,0,std::max(0,int(detailLines.size())-capacity));
            for(int i=0;i<capacity && i+detailScroll<int(detailLines.size());++i)OriginalText(x,y+int(i*lineHeight),detailLines[i+detailScroll],"labeldimtext");
            if(int(detailLines.size())>capacity)OriginalText(x,original.height-54,L"滚轮查看介绍","labeldimtext");
        }
    }
    const int closeState=hovered==14?(input&&input->IsMouseDown(vgui::MOUSE_LEFT)?2:1):0;
    MenuImage(original.close[closeState],original.closeButton.w?original.closeButton:MenuRect{original.width-24,7,15,15});
    if(selectionPending)OriginalText(22,original.height-28,status,"labeldimtext");
}
void Paint()
{
    if(!visible || !surface)return;
    Layout();
    if(originalMode){const auto root=surface->GetEmbeddedPanel();if(!root)return;surface->PushMakeCurrent(root,false);PaintOriginal();surface->DrawFlushText();surface->PopMakeCurrent(root);++draws;return;}
    const int height=std::max(12,int(14*scale));
    if(!font || fontHeight!=height){if(!font){font=surface->CreateFont();titleFont=surface->CreateFont();}fontHeight=height;
        surface->AddGlyphSetToFont(font,"Microsoft YaHei",height,400,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);
        surface->AddGlyphSetToFont(titleFont,"Microsoft YaHei",height+3,600,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);}
    const auto root=surface->GetEmbeddedPanel();if(!root)return;surface->PushMakeCurrent(root,false);
    Box(0,0,606,420,26,30,33,248);Box(0,0,606,3,168,172,164);Text(16,5,L"角色选择",236,238,230,true);
    Box(16,25,100,23,team==1?85:42,team==1?88:46,team==1?72:48);Text(40,29,L"恐怖分子");
    Box(116,25,100,23,team==2?85:42,team==2?88:46,team==2?72:48);Text(140,29,L"反恐精英");
    const auto rows=Filtered();
    for(int i=0;i<10;++i){const size_t index=page*10+i;if(index>=rows.size())break;const auto* c=rows[index];
        Box(16,50+i*25,200,24,c->id==chosen?110:43,c->id==chosen?112:47,c->id==chosen?88:49);
        Text(22,54+i*25,std::to_wstring((i+1)%10)+L"  "+Wide(c->name));}
    Box(16,312,72,26,55,60,61);Text(28,317,L"上一页");Box(144,312,72,26,55,60,61);Text(156,317,L"下一页");
    Text(94,317,std::to_wstring(page+1)+L" / "+std::to_wstring((rows.size()+9)/10));
    if(const auto* c=FindCharacter(catalog,chosen)){
        auto it=portraits.find(c->id);
        if(it==portraits.end()){
            int texture=0;try{const auto image=DecodeTGA(ReadResource(c->portrait.c_str()));texture=surface->CreateNewTextureID(true);
                surface->DrawSetTextureRGBA(texture,image.RGBA.data(),image.Width,image.Height,1,true);}catch(const std::exception& e){engine.Con_Printf("CSOL portrait: %s\n",e.what());}
            it=portraits.emplace(c->id,texture).first;
        }
        Box(246,50,340,260,16,20,22);
        if(it->second){surface->DrawSetColor(255,255,255,255);surface->DrawSetTexture(it->second);surface->DrawTexturedRect(X(250),Y(54),X(582),Y(306));}
        Text(250,322,Wide(c->name),238,222,155,true);Text(250,347,c->male?L"男性角色":L"女性角色");
    }
    Box(360,370,120,32,111,119,87);Text(385,378,L"使用角色");Box(488,370,100,32,58,63,64);Text(520,378,L"取消");
    Box(240,370,110,24,58,63,64);Text(252,375,L"原版菜单");
    Text(16,395,status,175,184,180);surface->DrawFlushText();surface->PopMakeCurrent(root);++draws;
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks {
public:
    int GetAltitude()const override{return 30;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{Close();}
    void Key_Event(int& down,int& key,const char*& binding,VGUI2Extension_CallbackContext* context)override{
        if(!visible || context->IsPost)return;
        if(binding && (!std::strcmp(binding,"snapshot") || !std::strcmp(binding,"screenshot") || !std::strcmp(binding,"toggleconsole"))){
            if(down){std::string command=std::string(binding)+"\n";engine.pfnClientCmd(command.data());}
            if(context->pPluginReturnValue)*static_cast<int*>(context->pPluginReturnValue)=1;context->Result=VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS;return;
        }
        if(down && !focusSuspended){if(key==27)Cancel();else if(key==13)Select();else if(key>='0' && key<='9'){
            SelectRow(key=='0'?9:key-'1');}
            else if(key==239 || key==240){
                int x=0,y=0;if(input)input->GetCursorPos(x,y);
                if(originalMode && original.info.Contains((x-left)/scale,(y-top)/scale))detailScroll=std::max(0,detailScroll+(key==239?1:-1));
                else ChangePage(key==239?1:-1);}
            else if(key==130 || key==131){team=key==130?1:2;page=0;auto rows=Filtered();if(!rows.empty())chosen=rows.front()->id;}
            else if(key==149 || key==150 || (originalMode && (key=='-' || key=='=' || key=='+')))ChangePage((key==149 || key=='=' || key=='+')?1:-1);
            else if(key==241){mouseFromEngine=true;int x,y;surface->SurfaceGetCursorPos(x,y);Click(x,y);}}
        // BaseUI returns one for consumed keys. Skip the post callbacks too:
        // the extension resets Result before post and would return real_ret=0.
        if(context->pPluginReturnValue)*static_cast<int*>(context->pPluginReturnValue)=1;context->Result=VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS;
    }
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int& message,unsigned int& wparam,long&,VGUI2Extension_CallbackContext* context)override{
        if(!visible || context->IsPost)return;
        if(message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !wparam))UpdateFocus(false);
        else if(message==WM_SETFOCUS || (message==WM_ACTIVATEAPP && wparam))UpdateFocus(true);
    }
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)csol::client::Paint();}
    void HideGameUI(VGUI2Extension_CallbackContext*)override{}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)Cancel();}
    void HideConsole(VGUI2Extension_CallbackContext*)override{}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)Cancel();}
} overlay;
}
void InitializeCharacters()
{
    engine.pfnHookUserMsg(CharacterMessage,Message);engine.pfnAddCommand("csol_characters",Open);
    previousVGUI=api->HookUserMsg("VGUIMenu",VGUIMenu);
    previousShowMenu=api->HookUserMsg("ShowMenu",ShowMenu);
    engine.pfnAddCommand("csol_characters_original",OpenOriginal);engine.pfnAddCommand("csol_characters_simple",OpenSimple);
    menuStyle=engine.pfnRegisterVariable("csol_character_menu","1",FCVAR_ARCHIVE);
    const auto factory=api->GetEngineFactory();surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
    const auto clientFactory=api->GetClientFactory();
    clientUI=clientFactory?static_cast<IClientVGUI*>(clientFactory(CLIENTVGUI_INTERFACE_VERSION,nullptr)):nullptr;
    const auto inputModule=GetModuleHandleA("vgui2.dll");
    const auto inputFactory=inputModule?Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(inputModule)):nullptr;
    input=inputFactory?static_cast<vgui::IInput*>(inputFactory(VGUI_INPUT_INTERFACE_VERSION,nullptr)):nullptr;
    const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");const auto pluginFactory=module?Sys_GetFactory(module):nullptr;
    extension=pluginFactory?static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    if(extension && surface)extension->RegisterBaseUICallbacks(&overlay);
}
void ShutdownCharacters(){ResetCharacters();if(extension)extension->UnregisterBaseUICallbacks(&overlay);if(privateFont)RemoveFontMemResourceEx(privateFont);privateFont=nullptr;extension=nullptr;surface=nullptr;input=nullptr;}
void ResetCharacters()
{
    Close();characters={};catalog.clear();loaded=false;nextSync=0;serverCharacters=false;
    menuFragments.clear();menuText.clear();
    if(surface)for(const auto& [id,texture]:portraits)if(texture)surface->DeleteTextureByID(texture);portraits.clear();
    if(surface)for(const auto& [path,texture]:menuTextures)if(texture)surface->DeleteTextureByID(texture);menuTextures.clear();
    originalLoaded=false;original={};descriptions.clear();detailLines.clear();detailId=0;detailScroll=0;
}
void CharacterFrame()
{
    if(visible && surface){
        // Modern SDL BaseUI does not call the legacy WndProc callback. Check
        // our own process while the menu is open, before polling mouse state.
        DWORD foreground{};
        GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        UpdateFocus(foreground==GetCurrentProcessId());
    }
    if(visible && !focusSuspended && surface){if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();surface->UnlockCursor();surface->SetCursor(2);}
    if(visible && !focusSuspended && input){
        const bool pressed=input->IsMouseDown(vgui::MOUSE_LEFT);
        int x,y;input->GetCursorPos(x,y);Hover(x,y);
        if(!mouseFromEngine && pressed && !lastButtons)Click(x,y);
        lastButtons=pressed;
    }
    // Before a map is active GoldSrc GetLocalPlayer may return an offset into
    // a null entity allocation. HUD_Frame also runs during the loading screen.
    const char* level=engine.pfnGetLevelName();
    if(!level || !*level || !view.maxclients || view.playernum<0)return;
    auto* local=engine.GetEntityByIndex(view.playernum+1);const double now=engine.GetClientTime();
    if(local && local->index>0 && local->index<=32 && !characters[local->index].userid && now>=nextSync){nextSync=now+2;engine.pfnServerCmd("csol_character_sync\n");}
}
void CharacterCreateMove(float frame,usercmd_t* cmd,int active)
{
    WeaponInventoryCreateMove(frame,cmd,active);
    if(!visible || !cmd)return;
    cmd->buttons=0;cmd->forwardmove=cmd->sidemove=cmd->upmove=0;
    // Suppress camera motion while keeping the engine's normal input lifecycle.
    std::copy_n(menuAngles,3,cmd->viewangles);engine.SetViewAngles(menuAngles);
}
const Character* ViewCharacter()
{
    const int index=view.playernum+1;
    if(index<1 || index>32 || !characters[index].active)return nullptr;
    return FindCharacter(catalog,characters[index].id);
}
void CharacterDiagnostics(std::ostream& out)
{
    const auto* c=ViewCharacter();out<<",\"characters\":{\"catalog\":"<<catalog.size()<<",\"active\":"<<(c?c->id:0)<<",\"visible\":"<<visible
        <<",\"chosen\":"<<chosen<<",\"team\":"<<team<<",\"page\":"<<page<<",\"mouseInput\":"<<(input!=nullptr)
        <<",\"messages\":"<<messages<<",\"rejected\":"<<rejected<<",\"draws\":"<<draws
        <<",\"original\":"<<originalMode<<",\"originalLoaded\":"<<originalLoaded<<",\"hovered\":"<<hovered<<",\"pending\":"<<pendingId
        <<",\"privateFont\":"<<(privateFont!=nullptr)<<",\"width\":"<<screenW<<",\"height\":"<<screenH<<",\"detailLines\":"<<detailLines.size()
        <<",\"appearance\":"<<appearanceMenu<<",\"server\":"<<serverCharacters<<",\"suspended\":"<<focusSuspended<<'}';
}
}
