// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "resource_refresh.hpp"
#include "menu_font.hpp"
#include "menu_input.hpp"
#include "menu_panel.hpp"
#include "../menu_layout.hpp"
#include "overlay_layers.hpp"
#include "characters.hpp"
#include "weapons/weapon_inventory.hpp"
#include "scenario/core/scenario_upgrades.hpp"
#include "../character_protocol.hpp"
#include "../character_menu.hpp"
#include "../image_codec.hpp"
#include <VGUI/ISurface.h>
#include <VGUI/IInput.h>
#include <VGUI/IClientVGUI.h>
#include <IVGUI2Extension.h>
#include <usercmd.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ostream>
#include <memory>
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
vgui::ISchemeManager* schemes{};
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
MenuButtonInput leftInput;
bool originalMode{},originalLoaded{};
cvar_t* menuStyle{};
CharacterMenu original;
std::unordered_map<std::string,int> menuTextures;
std::unordered_map<const MenuTexture*,int> imageRefs;
std::unordered_map<unsigned,std::wstring> descriptions,characterNames;
std::array<std::wstring,2> teamLabels;
std::array<std::wstring,5> statLabels;
std::wstring previousLabel,nextLabel,statHeading;
vgui::HFont originalFont{},originalTitle{};
vgui::HFont tabFont{},closeFont{};
int originalRecipeTall{},titleRecipeTall{},tabRecipeTall{};
MenuFontCache menuFonts;
menu::Viewport originalViewport{640,480};
menu::Rect originalRoot;
std::array<menu::Rect,2> teamPixels;
int fontScreenW{},fontScreenH{};
bool fontRefreshPending{};
std::array<std::vector<const Character*>,2> teamRows;
HANDLE privateFont{};
int originalFontHeight{},hovered=-1,pendingId{},detailScroll{};
unsigned detailId{};
std::vector<std::wstring> detailLines;
struct DescriptionGlyph {wchar_t character;int x,y;};
// Current client TextImage 181701390 / 181700940. Keep original wchar
// indices: shortcut markers participate in wrapping before Paint skips them.
struct DescriptionLayout {
    std::vector<DescriptionGlyph> glyphs;
    std::vector<int> widths;
    std::vector<size_t> breaks;
    void Prepare(std::wstring_view text,int width,vgui::HFont face,int tall)
    {
        glyphs.clear();widths.resize(text.size());breaks.clear();
        int used=0;
        for(size_t i=0;i<text.size();++i){
            if(i==0 && (text[i]=='\r' || text[i]=='\n'))continue;
            widths[i]=surface->GetCharacterWidth(face,text[i]);
            used+=widths[i];
            if(text[i]=='\r')used=0;
            else if(used>width){if(text[i]=='\n')used=0;else{breaks.push_back(i);used=widths[i];}}
            else if(text[i]=='\n')used=0;
        }
        int x=0,y=0;size_t next=0;glyphs.reserve(text.size());
        for(size_t i=0;i<text.size();++i){
            const auto ch=text[i];
            if(ch=='\r')continue;
            if(ch=='\n'){x=0;y+=tall;continue;}
            if(ch=='&'){if(i+1<text.size() && text[i+1]=='&')++i;else continue;}
            if(next<breaks.size() && i==breaks[next]){
                x=0;y+=tall;++next;
                if(ch==' ')continue;
            }
            glyphs.push_back({ch,x,y});x+=widths[i];
        }
    }
} descriptionLayout;
std::unique_ptr<MenuLayerPanel> detailRoot;
MenuPaintPanel *detailHelper{},*detailLabel{};
unsigned preparedDetail{};
vgui::HFont preparedDetailFont{};
int preparedDetailWidth{},preparedDetailScreenW{},preparedDetailScreenH{};
const CharacterDescription* preparedDescription{};
int preparedPortrait{};
std::array<std::wstring,5> preparedStatValues;
int team=2,page{},chosen{},screenW{},screenH{},left{},top{},fontHeight{};
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
void Close(bool activateMouse=true)
{
    if(detailRoot)detailRoot->SetVisible(false);
    if(!visible)return;
    visible=false;leftInput.Reset();selectionPending=false;focusSuspended=false;
    appearanceMenu=false;
    pendingId=0;hovered=-1;
    if(surface)surface->SetCursor(1);
    if(activateMouse&&downstream.IN_ActivateMouse)downstream.IN_ActivateMouse();
}
void UpdateFocus(bool focused)
{
    if(!visible)return;
    if(!focused){
        if(!appearanceMenu){Close();return;}
        focusSuspended=true;leftInput.Reset();
    } else if(focusSuspended){
        focusSuspended=false;
        // The click which brought the window forward must not select a role.
        leftInput.Reset(input && input->IsMouseDown(vgui::MOUSE_LEFT));
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
    try {auto bytes=ReadResource("models/csol/characters.json");catalog=ParseCharacters({reinterpret_cast<char*>(bytes.data()),bytes.size()});
        for(auto& rows:teamRows)rows.clear();
        characterNames.clear();
        for(const auto& item:catalog){teamRows[item.team-1].push_back(&item);characterNames.emplace(item.id,Wide(item.name));}
        loaded=true;return true;}
    catch(const std::exception& error){engine.Con_Printf("CSOL: 人物资源尚未就绪：%s\n",error.what());return false;}
}
void PrepareMenuImages();
void PreparePortrait(const Character& character);
bool LoadOriginal()
{
    if(originalLoaded)return true;
    try {
        auto bytes=ReadResource("models/csol/classmenu2.json");original=ParseCharacterMenu({reinterpret_cast<char*>(bytes.data()),bytes.size()});
        descriptions.clear();for(const auto& [id,detail]:original.descriptions)descriptions.emplace(id,Wide(detail.text));
        for(int i=0;i<2;++i)teamLabels[i]=Wide(original.teamNames[i]);
        for(int i=0;i<5;++i)statLabels[i]=Wide(original.stats.names[i]);
        previousLabel=Wide(original.previousLabel);nextLabel=Wide(original.nextLabel);statHeading=Wide(original.stats.headingText);
        fontScreenW=fontScreenH=0;PrepareMenuImages();originalLoaded=true;return true;
    }catch(const std::exception& e){engine.Con_Printf("CSOL: 原版菜单资源尚未就绪：%s\n",e.what());return false;}
}
void Layout()
{
    if(!surface)return;
    surface->GetScreenSize(screenW,screenH);
    if(originalMode && original.normalizedLayout){
        originalViewport=menu::Viewport(screenW,screenH);
        const auto& r=original.root;
        originalRoot=originalViewport.Root(r.x,r.y,r.w,r.h);
        scale=originalViewport.YScale();left=originalRoot.x;top=originalRoot.y;
        return;
    }
    scale=originalMode?std::min(float(screenW)/original.width,float(screenH)/original.height):std::min({screenW/640.f,screenH/460.f,1.6f});
    scale=std::max(.1f,scale);
    left=(screenW-int((originalMode?original.width:606)*scale))/2;
    top=(screenH-int((originalMode?original.height:420)*scale))/2;
}
const std::vector<const Character*>& Filtered()
{
    return teamRows[team-1];
}
int PageSize(){return originalMode?9:10;}
void Open()
{
    if(visible){Close();return;}
    if(!surface || !Load())return;
    originalMode=menuStyle && menuStyle->value!=0 && LoadOriginal();
    Layout();hovered=-1;detailScroll=0;detailId=0;
    const auto* current=ViewCharacter();if(current){team=current->team;chosen=current->id;}
    const auto& rows=Filtered();page=0;
    for(size_t i=0;i<rows.size();++i)if(rows[i]->id==chosen)page=int(i/PageSize());
    if(!chosen && !rows.empty())chosen=rows.front()->id;
    status=L"选择角色后点击「使用角色」";visible=true;selectionPending=false;focusSuspended=false;engine.GetViewAngles(menuAngles);
    leftInput.Reset(input && input->IsMouseDown(vgui::MOUSE_LEFT));
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
    const auto& rows=Filtered();
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
    const auto& rows=Filtered();page=std::clamp(page+delta,0,std::max(0,(int(rows.size())-1)/PageSize()));
    if(originalMode && !rows.empty()){chosen=rows[page*PageSize()]->id;detailId=0;detailScroll=0;}
    hovered=-1;
}
void SelectRow(int row)
{
    const auto& rows=Filtered();if(rows.empty())return;
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
menu::Rect PixelBounds(const MenuRect& r);
std::array<menu::Rect,2> CharacterTabBounds();
menu::Rect CharacterCloseBounds();
int OriginalPixelHit(int x,int y)
{
    if(!original.normalizedLayout)return OriginalHit((x-left)/scale,(y-top)/scale);
    // Inverting a float scale shifts the last pixel of some controls. Use the
    // exact integer bounds that were used for drawing instead.
    for(int i=0;i<10;++i)if(PixelBounds(original.slots[i]).Contains(x,y))return i;
    const auto tabs=CharacterTabBounds();
    for(int i=0;i<2;++i)if(tabs[i].Contains(x,y))return 10+i;
    if(PixelBounds(original.previous).Contains(x,y))return 12;
    if(PixelBounds(original.next).Contains(x,y))return 13;
    if(CharacterCloseBounds().Contains(x,y))return 14;
    return -1;
}
void Hover(int x,int y)
{
    if(!originalMode)return;
    const int hit=OriginalPixelHit(x,y);
    if(hit!=hovered && hit>=0 && hit<PageSize()){const auto& rows=Filtered();const int index=page*PageSize()+hit;if(index<int(rows.size())){chosen=rows[index]->id;detailScroll=0;}}
    hovered=hit;
}
void Click(int x,int y)
{
    const float px=(x-left)/scale,py=(y-top)/scale;
    if(originalMode){
        const int hit=OriginalPixelHit(x,y);const auto& rows=Filtered();
        if(hit>=0 && hit<10)SelectRow(hit);
        else if(hit==10 || hit==11){surface->PlaySound(MenuTabClickSound);team=hit-9;page=0;const auto& selectedRows=Filtered();if(!selectedRows.empty())chosen=selectedRows.front()->id;detailScroll=0;}
        else if(hit==12 || hit==13)ChangePage(hit==12?-1:1);
        else if(hit==14)Cancel();else if(hit==15)SetMenuMode(false);
        return;
    }
    if(py>=370 && py<394 && px>=240 && px<350){SetMenuMode(true);return;}
    if(py>=25 && py<48 && px>=16 && px<216){team=px<116?1:2;page=0;const auto& rows=Filtered();if(!rows.empty())chosen=rows.front()->id;return;}
    const auto& rows=Filtered();
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
float HorizontalScale(){return originalMode && original.normalizedLayout?originalViewport.XScale():scale;}
int X(float x){return left+int(x*HorizontalScale());}int Y(float y){return top+int(y*scale);}
menu::Rect PixelBounds(const MenuRect& r)
{
    return {X(r.x),Y(r.y),int(r.w*HorizontalScale()),int(r.h*scale)};
}
std::array<menu::Rect,2> CharacterTabBounds()
{
    if(!original.normalizedLayout)return {PixelBounds(original.teams[0]),PixelBounds(original.teams[1])};
    return teamPixels;
}
menu::Rect CharacterCloseBounds()
{
    return original.normalizedLayout?menu::CloseButton(originalRoot,screenH):
        PixelBounds(original.closeButton.w?original.closeButton:MenuRect{original.width-25,6,17,17});
}
void Box(int x,int y,int w,int h,int r,int g,int b,int a=255)
{const auto rect=PixelBounds({x,y,w,h});surface->DrawSetColor(r,g,b,a);surface->DrawFilledRect(rect.x,rect.y,rect.x+rect.w,rect.y+rect.h);}
void Text(int x,int y,const std::wstring& text,int r=225,int g=229,int b=231,bool title=false)
{surface->DrawSetTextFont(title?titleFont:font);surface->DrawSetTextColor(r,g,b,255);surface->DrawSetTextPos(X(x),Y(y));surface->DrawPrintText(text.c_str(),int(text.size()));}
void PrepareMenuImage(const MenuTexture& image)
{
    if(image.path.empty())return;
    auto [it,inserted]=menuTextures.try_emplace(image.path,0);
    if(inserted){try{const auto decoded=DecodeTGA(ReadResource(image.path.c_str()));
        if(decoded.Width!=image.width || decoded.Height!=image.height)throw std::runtime_error("menu image dimensions changed");
        it->second=surface->CreateNewTextureID(true);surface->DrawSetTextureRGBA(it->second,decoded.RGBA.data(),decoded.Width,decoded.Height,1,true);
    }catch(const std::exception& e){engine.Con_Printf("CSOL menu: %s\n",e.what());}}
    imageRefs.emplace(&image,it->second);
}
void PrepareMenuImages()
{
    imageRefs.clear();
    for(const auto& image:original.frame)PrepareMenuImage(image);
    for(const auto& group:original.buttons)for(const auto& image:group)PrepareMenuImage(image);
    for(const auto& group:original.tabs)for(const auto& image:group)PrepareMenuImage(image);
    for(const auto& image:original.close)PrepareMenuImage(image);
    for(const auto& image:original.slotBackground)PrepareMenuImage(image);
    PrepareMenuImage(original.keyboard);PrepareMenuImage(original.pin);PrepareMenuImage(original.stats.gauge);
}
void PreparePortrait(const Character& character)
{
    if(portraits.contains(character.id))return;
    int texture{};try{const auto image=DecodeTGA(ReadResource(character.portrait.c_str()));
        texture=surface->CreateNewTextureID(true);surface->DrawSetTextureRGBA(texture,image.RGBA.data(),image.Width,image.Height,1,true);
    }catch(const std::exception& error){engine.Con_Printf("CSOL portrait: %s\n",error.what());}
    portraits.emplace(character.id,texture);
}
void MenuImagePixels(const MenuTexture& image,const menu::Rect& pixels,int alpha=255)
{
    const auto it=imageRefs.find(&image);if(it==imageRefs.end()||!it->second)return;
    surface->DrawSetColor(255,255,255,alpha);surface->DrawSetTexture(it->second);surface->DrawTexturedRect(pixels.x,pixels.y,pixels.x+pixels.w,pixels.y+pixels.h);
}
void MenuImage(const MenuTexture& image,const MenuRect& rect,int alpha=255)
{MenuImagePixels(image,PixelBounds(rect),alpha);}
void OriginalText(int x,int y,const std::wstring& value,const char* color="basetext",bool title=false)
{
    const auto& c=MenuColor(original.colors,color);surface->DrawSetTextFont(title?originalTitle:originalFont);
    surface->DrawSetTextColor(c[0],c[1],c[2],c[3]);surface->DrawSetTextPos(X(x),Y(y));surface->DrawPrintText(value.c_str(),int(value.size()));
}
void OriginalCenteredText(const MenuRect& r,const std::wstring& value,const char* color="basetext",bool title=false)
{
    int w,h;surface->GetTextSize(title?originalTitle:originalFont,value.c_str(),w,h);
    const auto pixels=PixelBounds(r);const auto& c=MenuColor(original.colors,color);
    surface->DrawSetTextFont(title?originalTitle:originalFont);surface->DrawSetTextColor(c[0],c[1],c[2],c[3]);
    surface->DrawSetTextPos(pixels.x+(pixels.w-w)/2,pixels.y+(pixels.h-h)/2);surface->DrawPrintText(value.c_str(),int(value.size()));
}
void OriginalTab(int index)
{
    const int hit=10+index;const bool selected=team==index+1||leftInput.Selected(hit);
    const bool depressed=team==index+1||leftInput.Depressed(hit);
    const auto& images=original.tabs[TabVisualGroup(selected,leftInput.Armed(hit),depressed)];
    const auto rect=CharacterTabBounds()[index];const auto pieces=menu::TabSlices(rect,images[0].width);
    for(int i=0;i<3;++i)MenuImagePixels(images[i],pieces[i]);
    int width{},height{};surface->GetTextSize(tabFont,teamLabels[index].c_str(),width,height);
    const auto color=selected?(index?std::array<int,4>{183,202,229,255}:std::array<int,4>{229,188,188,255}):original.colors.at("basetext");
    surface->DrawSetTextFont(tabFont);surface->DrawSetTextColor(color[0],color[1],color[2],color[3]);
    surface->DrawSetTextPos(rect.x+(rect.w-width)/2,rect.y+(rect.h-height)/2);surface->DrawPrintText(teamLabels[index].c_str(),int(teamLabels[index].size()));
}
void OriginalButton(const MenuRect& r,const std::wstring& label,int hit,bool enabled=true,bool selected=false)
{
    const int state=hovered==hit?(leftInput.Depressed(hit)?2:1):(selected?2:0);
    const auto& tiles=original.buttons[state];const int a=std::min(tiles[0].width,r.w/2),b=std::min(tiles[2].width,r.w/2);
    MenuImage(tiles[0],{r.x,r.y,a,r.h},enabled?255:90);MenuImage(tiles[1],{r.x+a,r.y,r.w-a-b,r.h},enabled?255:90);MenuImage(tiles[2],{r.x+r.w-b,r.y,b,r.h},enabled?255:90);
    OriginalCenteredText(r,label,hovered==hit || selected?"brightcontroltext":"basetext");
}
bool PrepareOriginalFont()
{
    if(!surface || !schemes || !extension)return false;
    if(!privateFont){
        try{
            const auto bytes=ReadResource(MenuFontResource);DWORD count{};
            privateFont=RegisterResourceFont(privateFont,MenuFontResource,original.font.c_str(),bytes,&count);
        }catch(const std::exception& error){engine.Con_DPrintf("CSOL menu font: %s\n",error.what());return false;}
        if(!privateFont)return false;
    }
    if(fontRefreshPending){
        menuFonts.RebindFamily(originalFont,original.font);
        menuFonts.RebindFamily(originalTitle,original.font);
        menuFonts.RebindFamily(tabFont,original.font);
        fontRefreshPending=false;
    }
    if(originalFont && originalTitle && tabFont && closeFont && fontScreenW==screenW && fontScreenH==screenH)return true;
    const auto body=menuFonts.GetCSOScaled(*surface,original.font,9,0,vgui::FONTFLAG_ANTIALIAS);
    const auto title=menuFonts.GetCSOScaled(*surface,original.font,12,0,vgui::FONTFLAG_ANTIALIAS);
    const auto tab=menuFonts.GetCSOScaled(*surface,original.font,8,0,vgui::FONTFLAG_ANTIALIAS);
    const auto close=menuFonts.GetCSOScaled(*surface,"Marlett",11,0,vgui::FONTFLAG_SYMBOL);
    if(!body.handle || !title.handle || !tab.handle || !close.handle)return false;
    if(originalFont!=body.handle || originalFontHeight!=body.tall)detailId=0;
    originalFont=body.handle;originalTitle=title.handle;tabFont=tab.handle;closeFont=close.handle;
    originalFontHeight=body.tall;originalRecipeTall=body.recipeTall;titleRecipeTall=title.recipeTall;tabRecipeTall=tab.recipeTall;
    fontScreenW=screenW;fontScreenH=screenH;
    if(original.normalizedLayout){
        std::array<int,2> widths{};for(int i=0;i<2;++i){int height{};const auto text=teamLabels[i];surface->GetTextSize(tabFont,text.c_str(),widths[i],height);}
        teamPixels=menu::TeamTabs(originalViewport,originalRoot,16,34,17,widths);
    }
    return true;
}
void PaintDescription()
{
    const auto& color=MenuColor(original.colors,"labeldimtext");
    surface->DrawSetTextFont(originalFont);surface->DrawSetTextColor(color[0],color[1],color[2],color[3]);
    for(const auto& glyph:descriptionLayout.glyphs){surface->DrawSetTextPos(glyph.x,glyph.y);surface->DrawUnicodeChar(glyph.character);}
}
void PaintDetailHelper()
{
    struct Origin {int x=left,y=top;~Origin(){left=x;top=y;}} origin;
    left=top=0;
    const auto image=PixelBounds(original.scenarioImage);
    if(preparedPortrait){surface->DrawSetColor(255,255,255,255);surface->DrawSetTexture(preparedPortrait);surface->DrawTexturedRect(image.x,image.y,image.x+image.w,image.y+image.h);}
    if(preparedDescription && preparedDescription->defaultCaps && original.stats.maximum){
        const auto& stats=original.stats;OriginalText(stats.heading.x,stats.heading.y,statHeading,"statheading");
        for(size_t i=0;i<stats.names.size();++i){
            OriginalText(stats.labels[i].x,stats.labels[i].y,statLabels[i]);
            const auto area=PixelBounds(stats.bars[i]);
            surface->DrawSetColor(0,0,0,255);surface->DrawFilledRect(area.x,area.y,area.x+area.w,area.y+area.h);
            const int width=int(float(area.w-4)*(float((*preparedDescription->defaultCaps)[i])/30.0f));
            if(width>0 && area.h>4)MenuImagePixels(stats.gauge,{area.x+2,area.y+2,std::min(width,area.w-2),area.h-4});
            OriginalText(stats.values[i].x,stats.values[i].y,preparedStatValues[i],"statvalue");
        }
    }
}
void RetireDetailPanels()
{
    detailRoot.reset();detailHelper=detailLabel=nullptr;preparedDescription=nullptr;
    preparedDetail=0;preparedDetailFont=0;descriptionLayout.glyphs.clear();
}
void PrepareDetailPanels()
{
    const bool active=visible && originalMode && original.normalizedLayout && ScenarioHudLevel()>=0;
    if(!active){if(detailRoot)detailRoot->SetVisible(false);return;}
    if(!surface || !originalFont || !vgui::ivgui() || !vgui::ipanel())return;
    const auto detail=original.descriptions.find(chosen);const auto text=descriptions.find(chosen);
    if(detail==original.descriptions.end() || text==descriptions.end()){if(detailRoot)detailRoot->SetVisible(false);return;}
    const auto parent=surface->GetEmbeddedPanel();if(!parent)return;
    const auto info=PixelBounds(original.info);
    const int width=int(original.scenarioDescription.w*HorizontalScale());
    if(!detailRoot){
        detailRoot=std::make_unique<MenuLayerPanel>(parent,"CSOLCharacterDetails");
        detailRoot->SetMouseInputEnabled(false);detailRoot->SetKeyBoardInputEnabled(false);
        detailHelper=new MenuPaintPanel(detailRoot.get(),"ClassHelperPanel",PaintDetailHelper);
        detailLabel=new MenuPaintPanel(detailHelper,"infolabel",PaintDescription);
    }
    if(preparedDetail!=unsigned(chosen) || preparedDetailFont!=originalFont || preparedDetailWidth!=width ||
       preparedDetailScreenW!=screenW || preparedDetailScreenH!=screenH){
        detailRoot->SetBounds(originalRoot.x,originalRoot.y,originalRoot.w,originalRoot.h);
        detailHelper->SetBounds(info.x-originalRoot.x,info.y-originalRoot.y,info.w,info.h);
        const auto& r=original.scenarioDescription;
        detailLabel->SetBounds(int(r.x*HorizontalScale()),int(r.y*scale),width,int(r.h*scale));
        descriptionLayout.Prepare(text->second,width,originalFont,originalFontHeight);
        preparedDescription=&detail->second;
        preparedPortrait=0;if(const auto image=portraits.find(chosen);image!=portraits.end())preparedPortrait=image->second;
        if(preparedDescription->defaultCaps)for(size_t i=0;i<preparedStatValues.size();++i)
            preparedStatValues[i]=std::to_wstring(menu::ProgressMirror(float((*preparedDescription->defaultCaps)[i])/30.0f,0.3f));
        preparedDetail=chosen;preparedDetailFont=originalFont;preparedDetailWidth=width;
        preparedDetailScreenW=screenW;preparedDetailScreenH=screenH;
    }
    detailRoot->SetVisible(true);
}
void PaintOriginal()
{
    if(!PrepareOriginalFont())return;const auto& f=original.frame;
    surface->DrawSetColor(0,0,0,150);surface->DrawFilledRect(0,0,screenW,screenH);
    // Keep the content aspect ratio while the frame covers wide displays.
    const int frameLeft=original.fullScreenFrame?7-int(left/scale):0;
    const int frameTop=original.fullScreenFrame?11-int(top/scale):0;
    const int frameRight=original.fullScreenFrame?int((screenW-left)/scale):original.width;
    const int frameBottom=original.fullScreenFrame?int((screenH-top)/scale):original.height;
    const int xs[]{frameLeft,frameLeft+f[0].width,frameRight-f[2].width,frameRight},ys[]{frameTop,frameTop+f[0].height,frameBottom-f[6].height,frameBottom};
    for(int i=0;i<9;++i){const int x=i%3,y=i/3;MenuImage(f[i],{xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y]});}
    if(original.normalizedLayout){
        int width{},height{};surface->GetTextSize(originalTitle,L"选择角色",width,height);
        const auto& color=original.colors.at("basetext");surface->DrawSetTextFont(originalTitle);surface->DrawSetTextColor(color[0],color[1],color[2],color[3]);
        surface->DrawSetTextPos(left+(originalRoot.w-width)/2,top+12);surface->DrawPrintText(L"选择角色",4);
    }else OriginalCenteredText(original.title.w?original.title:MenuRect{0,6,original.width,14},L"选择角色","basetext",true);
    for(int i=0;i<2;++i){if(original.normalizedLayout)OriginalTab(i);else OriginalButton(original.teams[i],teamLabels[i],10+i,true,team==i+1);}
    const auto& rows=Filtered();
    for(int i=0;i<10;++i){const int index=page*PageSize()+i;const auto& r=original.slots[i];const auto* c=i<9 && index<int(rows.size())?rows[index]:nullptr;
        if(i<9 && !c)continue;
        if(!original.keyboard.path.empty()){
            MenuImage(original.slotBackground[hovered==i || (c && chosen==c->id)?1:0],r);
            MenuImage(original.keyboard,{r.x+3,r.y+4,17,r.h-7});
            OriginalCenteredText({r.x+3,r.y+4,17,r.h-7},std::to_wstring((i+1)%10));
        }else OriginalButton(r,L"",i);
        OriginalText(r.x+32,r.y+int((r.h-originalFontHeight/scale)/2),c?characterNames.at(c->id):L"自动选择",c && chosen==c->id?"brightcontroltext":"basetext");
        const auto* current=ViewCharacter();
        if(!appearanceMenu && c && current && current->id==c->id && !original.pin.path.empty())MenuImage(original.pin,{r.x+r.w-19,r.y+5,13,13});}
    if(page>0)OriginalButton(original.previous,previousLabel,12);
    if((page+1)*PageSize()<int(rows.size()))OriginalButton(original.next,nextLabel,13);
    if(!original.normalizedLayout)OriginalCenteredText(original.pageLabel.w?original.pageLabel:MenuRect{88,307,56,17},std::to_wstring(page+1)+L" / "+std::to_wstring(std::max(1,(int(rows.size())+PageSize()-1)/PageSize())));
    const auto* c=FindCharacter(catalog,chosen);
    if(original.normalizedLayout && ScenarioHudLevel()>=0){
        if(detailRoot)detailRoot->PaintOverlay();
    }else if(c){
        const auto found=original.descriptions.find(c->id);
        if(found!=original.descriptions.end()){
            const auto& d=found->second;
            const bool scenario=original.normalizedLayout && ScenarioHudLevel()>=0;
            struct Origin {
                int x=left,y=top;
                ~Origin(){left=x;top=y;}
            } origin;
            if(original.normalizedLayout){const auto area=PixelBounds(original.info);left=area.x;top=area.y;}
            MenuRect r=scenario?original.scenarioImage:d.image;
            if(!original.normalizedLayout){r.x+=original.info.x;r.y+=original.info.y;}
            auto it=portraits.find(c->id);
            if(it!=portraits.end()&&it->second){const auto area=PixelBounds(r);surface->DrawSetColor(255,255,255,255);surface->DrawSetTexture(it->second);surface->DrawTexturedRect(area.x,area.y,area.x+area.w,area.y+area.h);}
            if(d.defaultCaps && original.stats.maximum && (!original.normalizedLayout || scenario)){
                const auto& stats=original.stats;
                OriginalText(stats.heading.x,stats.heading.y,statHeading,"statheading");
                const auto& color=original.colors.at("statbar");
                for(size_t i=0;i<stats.names.size();++i){
                    OriginalText(stats.labels[i].x,stats.labels[i].y,statLabels[i]);
                    const auto& bar=stats.bars[i];const int value=(*d.defaultCaps)[i];
                    if(original.normalizedLayout){
                        const auto area=PixelBounds(bar);
                        surface->DrawSetColor(0,0,0,255);surface->DrawFilledRect(area.x,area.y,area.x+area.w,area.y+area.h);
                        const float progress=float(value)/30.0f;
                        const int width=int(float(area.w-4)*progress);
                        // The original panel clips its image at its own bounds;
                        // its two-pixel inset is an actual pixel value.
                        if(width>0 && area.h>4)MenuImagePixels(stats.gauge,{area.x+2,area.y+2,std::min(width,area.w-2),area.h-4});
                    }else{
                        const int width=bar.w*std::min(value,stats.maximum)/stats.maximum;
                        if(width)Box(bar.x,bar.y,width,bar.h,color[0],color[1],color[2],color[3]);
                    }
                    OriginalText(stats.values[i].x,stats.values[i].y,std::to_wstring(original.normalizedLayout?menu::ProgressMirror(float(value)/30.0f,0.3f):value),"statvalue");
                }
            }
            const auto description=scenario?original.scenarioDescription:d.description;
            const int x=(original.normalizedLayout?0:original.info.x)+description.x,y=(original.normalizedLayout?0:original.info.y)+description.y;
            const int width=original.normalizedLayout?description.w:std::min(description.w,original.width-x-16);
            if(detailId!=c->id){
                detailLines.clear();std::wstring line;int used=0;
                for(wchar_t ch:descriptions[c->id]){if(ch=='\r')continue;wchar_t character[]{ch,0};int w,h;surface->GetTextSize(originalFont,character,w,h);
                    if(ch=='\n' || (used+w>width*HorizontalScale() && !line.empty())){detailLines.push_back(std::move(line));line.clear();used=0;}
                    if(ch!='\n'){line+=ch;used+=w;}}
                if(!line.empty())detailLines.push_back(std::move(line));detailId=c->id;
            }
            const float lineHeight=(originalFontHeight+2)/scale;
            const int capacity=std::max(1,int((original.normalizedLayout?description.h:std::min(description.h,original.height-y-60))/lineHeight));
            detailScroll=std::clamp(detailScroll,0,std::max(0,int(detailLines.size())-capacity));
            for(int i=0;i<capacity && i+detailScroll<int(detailLines.size());++i)OriginalText(x,y+int(i*lineHeight),detailLines[i+detailScroll],"labeldimtext");
            if(!original.normalizedLayout && int(detailLines.size())>capacity)OriginalText(x,original.height-54,L"滚轮查看介绍","labeldimtext");
        }
    }
    const int closeState=hovered==14?(input&&input->IsMouseDown(vgui::MOUSE_LEFT)?2:1):0;
    if(original.normalizedLayout){
        const auto rect=CharacterCloseBounds();int width{},height{};surface->GetTextSize(closeFont,L"r",width,height);
        const auto& c=original.colors.at("borderbright");surface->DrawSetTextFont(closeFont);surface->DrawSetTextColor(c[0],c[1],c[2],c[3]);
        surface->DrawSetTextPos(rect.x+int(std::ceil(2.0f*screenH/480.0f)),rect.y+int(std::ceil(float(screenH)/480.0f)));surface->DrawPrintText(L"r",1);
    }else MenuImage(original.close[closeState],original.closeButton.w?original.closeButton:MenuRect{original.width-24,7,15,15});
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
    const auto& rows=Filtered();
    for(int i=0;i<10;++i){const size_t index=page*10+i;if(index>=rows.size())break;const auto* c=rows[index];
        Box(16,50+i*25,200,24,c->id==chosen?110:43,c->id==chosen?112:47,c->id==chosen?88:49);
        Text(22,54+i*25,std::to_wstring((i+1)%10)+L"  "+characterNames.at(c->id));}
    Box(16,312,72,26,55,60,61);Text(28,317,L"上一页");Box(144,312,72,26,55,60,61);Text(156,317,L"下一页");
    Text(94,317,std::to_wstring(page+1)+L" / "+std::to_wstring((rows.size()+9)/10));
    if(const auto* c=FindCharacter(catalog,chosen)){
        auto it=portraits.find(c->id);
        Box(246,50,340,260,16,20,22);
        if(it!=portraits.end()&&it->second){surface->DrawSetColor(255,255,255,255);surface->DrawSetTexture(it->second);surface->DrawTexturedRect(X(250),Y(54),X(582),Y(306));}
        Text(250,322,characterNames.at(c->id),238,222,155,true);Text(250,347,c->male?L"男性角色":L"女性角色");
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
        if(key==241 && !focusSuspended){
            int x{},y{};input->GetCursorPos(x,y);Hover(x,y);
            const int hit=originalMode?OriginalPixelHit(x,y):0;
            if(leftInput.Event(down!=0,hit)>=0)Click(x,y);
        }else if(down && !focusSuspended){if(key==27)Cancel();else if(key==13)Select();else if(key>='0' && key<='9'){
            SelectRow(key=='0'?9:key-'1');}
            else if(key==239 || key==240){
                int x=0,y=0;if(input)input->GetCursorPos(x,y);
                if(originalMode && original.info.Contains((x-left)/scale,(y-top)/scale))detailScroll=std::max(0,detailScroll+(key==239?1:-1));
                else ChangePage(key==239?1:-1);}
            else if(key==130 || key==131){team=key==130?1:2;page=0;const auto& rows=Filtered();if(!rows.empty())chosen=rows.front()->id;}
            else if(key==149 || key==150 || (originalMode && (key=='-' || key=='=' || key=='+')))ChangePage((key==149 || key=='=' || key=='+')?1:-1);
            }
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
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)QueueOverlayPaint(OverlayLayer::CharacterMenu,csol::client::Paint);}
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
    schemes=inputFactory?static_cast<vgui::ISchemeManager*>(inputFactory(VGUI_SCHEME_INTERFACE_VERSION,nullptr)):nullptr;
    const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");const auto pluginFactory=module?Sys_GetFactory(module):nullptr;
    extension=pluginFactory?static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    if(extension && surface)extension->RegisterBaseUICallbacks(&overlay);
}
void ShutdownCharacters(){ResetCharacters();if(extension)extension->UnregisterBaseUICallbacks(&overlay);if(privateFont)RemoveFontMemResourceEx(privateFont);privateFont=nullptr;extension=nullptr;surface=nullptr;input=nullptr;schemes=nullptr;clientUI=nullptr;menuFonts.Invalidate();originalFont=originalTitle=tabFont=closeFont=0;fontScreenW=fontScreenH=0;}
void ResetCharacters()
{
    RetireDetailPanels();
    Close();characters={};catalog.clear();loaded=false;nextSync=0;serverCharacters=false;
    menuFragments.clear();menuText.clear();
    if(surface)for(const auto& [id,texture]:portraits)if(texture)surface->DeleteTextureByID(texture);portraits.clear();
    if(surface)for(const auto& [path,texture]:menuTextures)if(texture)surface->DeleteTextureByID(texture);menuTextures.clear();imageRefs.clear();
    originalLoaded=false;original={};descriptions.clear();characterNames.clear();for(auto& rows:teamRows)rows.clear();detailLines.clear();detailId=0;detailScroll=0;fontScreenH=0;
}
void InvalidateCharacterResources()
{
    RetireDetailPanels();
    Close(false);catalog.clear();loaded=false;
    menuFonts.InvalidateFailures();fontRefreshPending=true;fontScreenW=fontScreenH=0;
    if(surface)for(const auto& [id,texture]:portraits)if(texture)surface->DeleteTextureByID(texture);
    if(surface)for(const auto& [path,texture]:menuTextures)if(texture)surface->DeleteTextureByID(texture);
    portraits.clear();menuTextures.clear();imageRefs.clear();originalLoaded=false;original={};descriptions.clear();characterNames.clear();for(auto& rows:teamRows)rows.clear();
    detailLines.clear();detailId=0;detailScroll=0;
}
void PrepareCharacterResourceFonts(ResourceFontPreparation& output)
{
    if(!surface||!originalFont||!ResourceFontIsTracked(&privateFont))return;
    const auto bytes=ReadResource("models/csol/classmenu2.json");
    const auto next=ParseCharacterMenu({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
    output.sources.push_back({&privateFont,MenuFontResource,next.font});
    output.recipes.push_back({originalFont,next.font,originalRecipeTall,0,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff});
    if(originalTitle && originalTitle!=originalFont)output.recipes.push_back({originalTitle,next.font,titleRecipeTall,0,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff});
    if(tabFont && tabFont!=originalFont && tabFont!=originalTitle)output.recipes.push_back({tabFont,next.font,tabRecipeTall,0,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff});
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
    if(visible && !focusSuspended && surface){Layout();if(originalMode)PrepareOriginalFont();}
    if(visible && !focusSuspended && input){
        const bool pressed=input->IsMouseDown(vgui::MOUSE_LEFT);
        int x,y;input->GetCursorPos(x,y);Hover(x,y);
        const int hit=originalMode?OriginalPixelHit(x,y):0;
        if(leftInput.Poll(pressed,hit)>=0)Click(x,y);
    }
    if(visible && surface)if(const auto* character=FindCharacter(catalog,chosen))PreparePortrait(*character);
    PrepareDetailPanels();
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
bool CharacterMenuVisible(){return visible;}
void CloseCharacterMenuForOverlay(){Close(false);}
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
