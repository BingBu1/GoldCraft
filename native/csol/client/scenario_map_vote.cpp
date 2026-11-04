// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "overlay_layers.hpp"
#include "scenario_map_vote.hpp"
#include "weapon_inventory.hpp"
#include "buy_menu.hpp"
#include "characters.hpp"
#include "../map_vote_presentation.hpp"
#include "../scenario/map_vote.hpp"
#include "../image_codec.hpp"
#include <VGUI/ISurface.h>
#include <VGUI/IInput.h>
#include <VGUI/IClientVGUI.h>
#include <IVGUI2Extension.h>
#include <vgui_controls/Panel.h>
#include <usercmd.h>
#include <in_buttons.h>
#include <algorithm>
#include <cmath>
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
namespace mv=csol::scenario::map_vote;
vgui::ISurface* surface{};
vgui::IInput* input{};
IClientVGUI* clientUI{};
IVGUI2Extension* extension{};
vgui::HFont font{};
HANDLE privateFont{};
mv::Presentation presentation;
mv::Snapshot state;
mv::Countdown countdown;
vgui::DHANDLE<vgui::Panel> listPanel;
std::unordered_map<std::string,int> textures;
bool loaded{},visible{},mouseHeld{},restoreMouse{},swallowMouse{},registered{},listPainting{},dragging{},gameUiVisible{};
int left{},top{},fontHeight{},scroll{},hover{-1},dragY{},dragStart{};
float scale{1},angles[3]{};
std::uint16_t selection{};
unsigned rejected{},draws{},submitted{},restores{};
constexpr int Confirm=100,CloseButton=101,ScrollTrack=102;

std::wstring Wide(std::string_view text)
{
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(size<=0)return{};std::wstring output(size,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),output.data(),size);return output;
}
bool Focused(){DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
unsigned Remaining()
{
    return countdown.Remaining(engine.GetClientTime());
}
void RestoreMouse()
{
    if(!restoreMouse||!Focused()||(engine.Con_IsVisible&&engine.Con_IsVisible())||gameUiVisible)return;
    if(BuyMenuVisible()||CharacterMenuVisible()){restoreMouse=false;return;}
    if(input){input->SetMouseCapture(0);input->SetCursorOveride(0);}
    if(surface){surface->SetCursor(1);surface->LockCursor();}
    if(downstream.IN_ClearStates)downstream.IN_ClearStates();
    if(downstream.IN_ActivateMouse)downstream.IN_ActivateMouse();
    restoreMouse=false;++restores;
}
void Close(bool gameplay=true)
{
    if(!visible)return;visible=false;swallowMouse=true;hover=-1;dragging=false;
    if(listPanel)listPanel->SetVisible(false);
    if(input){input->SetMouseCapture(0);input->SetCursorOveride(0);}
    restoreMouse=gameplay;if(gameplay)RestoreMouse();
}
bool Load()
{
    if(loaded)return true;
    if(!surface)return false;
    try{
        const auto bytes=ReadResource(mv::PresentationResource);
        presentation=mv::ParsePresentation({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
        const auto fontBytes=ReadResource(presentation.fontResource.c_str());DWORD count{};
        privateFont=AddFontMemResourceEx(const_cast<std::byte*>(fontBytes.data()),DWORD(fontBytes.size()),nullptr,&count);
        if(!privateFont||!count)throw std::runtime_error("original map-vote font unavailable");
        // Decode and upload once during preparation, outside every Paint call.
        for(const auto& path:mv::Resources(presentation))if(path.ends_with(".tga")){
            const auto image=DecodeTGA(ReadResource(path.c_str()));
            const int id=surface->CreateNewTextureID(true);if(!id)throw std::runtime_error("map-vote texture allocation failed");
            textures.emplace(path,id);surface->DrawSetTextureRGBA(id,image.RGBA.data(),image.Width,image.Height,1,true);
        }
        if(!listPanel&&g_pVGuiInput2&&g_pVGuiSurface2&&g_pVGui&&g_pVGuiPanel){
            listPanel=new vgui::Panel(nullptr,"CSOLMapVoteList");listPanel->SetParent(surface->GetEmbeddedPanel());
            listPanel->SetMouseInputEnabled(false);listPanel->SetKeyBoardInputEnabled(false);
            listPanel->SetPaintEnabled(false);listPanel->SetPaintBackgroundEnabled(false);listPanel->SetVisible(false);
        }
        if(!listPanel)throw std::runtime_error("map-vote clipping panel unavailable");
        loaded=true;return true;
    }catch(const std::exception& error){
        if(privateFont)RemoveFontMemResourceEx(privateFont);privateFont=nullptr;
        for(const auto& [path,id]:textures)if(id)surface->DeleteTextureByID(id);textures.clear();
        engine.Con_Printf("CSOL 选图资源：%s\n",error.what());return false;
    }
}
void Open()
{
    if(visible){Close();return;}
    if(!surface||!input||!extension||gameUiVisible||(engine.Con_IsVisible&&engine.Con_IsVisible())||!Remaining()||!Load())return;
    CloseBuyMenuForOverlay();CloseCharacterMenuForOverlay();
    if(clientUI)clientUI->HideAllVGUIMenu();ClearNativeWeaponSelection();
    visible=true;restoreMouse=false;hover=-1;scroll=0;selection=state.selection;
    listPanel->SetVisible(true);
    if(!selection&&state.count)selection=state.choices[0].id;
    engine.GetViewAngles(angles);mouseHeld=input->IsMouseDown(vgui::MOUSE_LEFT);
    if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();surface->UnlockCursor();surface->SetCursor(2);
}
int Receive(const char*,int size,void* data)
{
    SynchronizeSession();mv::Snapshot next;
    if(!data||size<0||!mv::Decode({static_cast<const std::byte*>(data),std::size_t(size)},next)){++rejected;return 0;}
    if(next.generation<state.generation || (next.generation==state.generation&&next.revision<state.revision))return 1;
    if(!Load()){++rejected;return 0;}
    for(unsigned i=0;i<next.count;++i)if(!presentation.Find(next.choices[i].id)){++rejected;return 0;}
    const bool newVote=next.generation!=state.generation;
    if(newVote)Close(false);
    if(!countdown.Observe(next,engine.GetClientTime()))return 1;
    state=next;
    if(state.phase!=mv::Phase::Open||!state.seconds){Close();return 1;}
    if(newVote)Open();return 1;
}
void Layout()
{
    int w{},h{};surface->GetScreenSize(w,h);
    scale=std::max(.1f,std::min({1.f,float(w)/presentation.width,float(h)/presentation.height}));
    left=(w-int(presentation.width*scale))/2;top=(h-int(presentation.height*scale))/2;
    if(listPanel)listPanel->SetBounds(left+int(presentation.list.x*scale),top+int(presentation.list.y*scale),int(presentation.list.w*scale),int(presentation.list.h*scale));
    const int height=std::max(8,int(presentation.fontSize*scale));
    if(font&&fontHeight==height)return;
    if(!font)font=surface->CreateFont();fontHeight=height;
    surface->AddGlyphSetToFont(font,presentation.font.c_str(),height,presentation.fontWeight,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);
}
int X(int value){return listPainting?int((value-presentation.list.x)*scale):left+int(value*scale);}
int Y(int value){return listPainting?int((value-presentation.list.y)*scale):top+int(value*scale);}
void Image(const MenuTexture& texture,const MenuRect& rect,int alpha=255)
{
    const auto it=textures.find(texture.path);if(it==textures.end()||!it->second)return;
    surface->DrawSetColor(255,255,255,alpha);surface->DrawSetTexture(it->second);
    surface->DrawTexturedRect(X(rect.x),Y(rect.y),X(rect.x+rect.w),Y(rect.y+rect.h));
}
void Panel(const std::array<MenuTexture,9>& images,const MenuRect& rect)
{
    const int cornerWidth=std::min(images[0].width,rect.w/2),cornerHeight=std::min(images[0].height,rect.h/2);
    const int xs[]{rect.x,rect.x+cornerWidth,rect.x+rect.w-cornerWidth,rect.x+rect.w};
    const int ys[]{rect.y,rect.y+cornerHeight,rect.y+rect.h-cornerHeight,rect.y+rect.h};
    for(unsigned i=0;i<9;++i){const int x=i%3,y=i/3;Image(images[i],{xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y]});}
}
void Text(int x,int y,const std::wstring& text,int alpha=255)
{
    surface->DrawSetTextFont(font);surface->DrawSetTextColor(255,255,255,alpha);
    surface->DrawSetTextPos(X(x),Y(y));surface->DrawPrintText(text.c_str(),int(text.size()));
}
std::wstring Fit(std::wstring text,int width)
{
    int w{},h{};surface->GetTextSize(font,text.c_str(),w,h);
    if(w<=int(width*scale))return text;
    while(!text.empty()){
        text.pop_back();const auto shortened=text+L"…";
        surface->GetTextSize(font,shortened.c_str(),w,h);if(w<=int(width*scale))return shortened;
    }
    return {};
}
void Button(const MenuRect& rect,const std::wstring& label,int hit,bool enabled=true)
{
    const unsigned index=enabled&&hover==hit?(input->IsMouseDown(vgui::MOUSE_LEFT)?2:1):0;
    const auto& parts=presentation.buttons[index];
    Image(parts[0],{rect.x,rect.y,parts[0].width,rect.h},enabled?255:100);
    Image(parts[1],{rect.x+parts[0].width,rect.y,rect.w-parts[0].width-parts[2].width,rect.h},enabled?255:100);
    Image(parts[2],{rect.x+rect.w-parts[2].width,rect.y,parts[2].width,rect.h},enabled?255:100);
    int w{},h{};surface->GetTextSize(font,label.c_str(),w,h);
    Text(rect.x+(rect.w-int(w/scale))/2,rect.y+(rect.h-int(h/scale))/2,label,enabled?255:100);
}
int MaxScroll(){return std::max(0,((int(state.count)+presentation.columns-1)/presentation.columns-1)*presentation.strideY+presentation.card.h-presentation.list.h);}
MenuRect Scrollbar(){return presentation.scrollbar;}
MenuRect Thumb(){const auto bar=Scrollbar();const int h=std::max(24,bar.h*presentation.list.h/(presentation.list.h+MaxScroll()));return {bar.x,bar.y+(MaxScroll()?scroll*(bar.h-h)/MaxScroll():0),bar.w,h};}
MenuRect Card(int slot)
{
    return {presentation.list.x+(slot%presentation.columns)*presentation.strideX,
        presentation.list.y+(slot/presentation.columns)*presentation.strideY-scroll,presentation.card.w,presentation.card.h};
}
int Hit(int x,int y)
{
    const float px=(x-left)/scale,py=(y-top)/scale;
    if(MaxScroll()&&Scrollbar().Contains(px,py))return ScrollTrack;
    if(presentation.list.Contains(px,py))for(int i=0;i<state.count;++i)if(Card(i).Contains(px,py))return i;
    if(presentation.confirm.Contains(px,py))return Confirm;
    if(presentation.close.Contains(px,py))return CloseButton;
    return -1;
}
void Click(int hit)
{
    if(hit>=0&&hit<state.count)selection=state.choices[hit].id;
    else if(hit==CloseButton)Close();
    else if(hit==ScrollTrack&&MaxScroll()){
        int x{},y{};input->GetCursorPos(x,y);const int logical=int((y-top)/scale);
        const auto thumb=Thumb();if(!thumb.Contains(float((x-left)/scale),float(logical)))
            scroll=std::clamp(scroll+(logical<thumb.y?-presentation.list.h:presentation.list.h),0,MaxScroll());
        dragging=true;dragY=logical;dragStart=scroll;
    }
    else if(hit==Confirm&&selection&&Remaining()){
        const std::string command=std::string(mv::CommandName)+" "+std::to_string(state.generation)+" "+std::to_string(selection)+"\n";
        engine.pfnServerCmd(const_cast<char*>(command.c_str()));++submitted;Close();
    }
}
void Paint()
{
    if(!visible||!surface||!loaded)return;Layout();const auto root=surface->GetEmbeddedPanel();if(!root)return;
    surface->PushMakeCurrent(root,false);
    Panel(presentation.frame,{0,0,presentation.width,presentation.height});
    Panel(presentation.infoBackground,presentation.info);
    surface->PopMakeCurrent(root);surface->PushMakeCurrent(listPanel->GetVPanel(),false);listPainting=true;
    for(int i=0;i<state.count;++i){
        const auto& count=state.choices[i];const auto* map=presentation.Find(count.id);if(!map)continue;
        const auto card=Card(i);if(card.y+card.h<=presentation.list.y||card.y>=presentation.list.y+presentation.list.h)continue;
        Panel(presentation.cards[0],card);
        const auto& image=presentation.cardImage;Image(map->thumbnail,{card.x+image.x,card.y+image.y,image.w,image.h});
        const auto& label=presentation.cardLabel;Text(card.x+label.x,card.y+label.y,Fit(Wide(map->name),label.w));
        Text(card.x+card.w-55,card.y+5,std::to_wstring(count.votes)+L"票");
        if(count.id==selection||hover==i){surface->DrawFlushText();Panel(presentation.cards[count.id==selection?1:2],card);}
    }
    surface->DrawFlushText();listPainting=false;surface->PopMakeCurrent(listPanel->GetVPanel());surface->PushMakeCurrent(root,false);
    if(MaxScroll()){
        const auto r=Thumb();const auto& parts=presentation.scrollThumb;
        Image(parts[0],{r.x,r.y,r.w,parts[0].height});
        Image(parts[1],{r.x,r.y+parts[0].height,r.w,r.h-parts[0].height-parts[2].height});
        Image(parts[2],{r.x,r.y+r.h-parts[2].height,r.w,parts[2].height});
    }
    Text(presentation.info.x+6,presentation.info.y+7,L"大灾变 · 地图投票");
    if(const auto* map=presentation.Find(selection)){
        Image(map->detail,presentation.preview);
        Text(presentation.info.x+7,presentation.preview.y+presentation.preview.h+10,Fit(Wide(map->name),presentation.info.w-14));
        // Preserve the original explicit line breaks and keep localization
        // within its allocated column at every screen scale.
        const auto text=Wide(map->description);std::size_t start{};int y=presentation.preview.y+presentation.preview.h+38;
        while(start<text.size()&&y+presentation.fontSize<presentation.confirm.y-45){
            const auto end=text.find(L'\n',start);Text(presentation.info.x+7,y,Fit(text.substr(start,end==text.npos?text.npos:end-start),presentation.info.w-14));
            if(end==text.npos)break;start=end+1;y+=presentation.fontSize+3;
        }
    }
    Text(presentation.info.x+8,presentation.confirm.y-44,L"剩余 "+std::to_wstring(Remaining())+L" 秒");
    unsigned ballots{};for(unsigned i=0;i<state.count;++i)ballots+=state.choices[i].votes;
    Text(presentation.info.x+8,presentation.confirm.y-24,L"已投票："+std::to_wstring(ballots)+L" / "+std::to_wstring(state.eligible));
    Button(presentation.confirm,L"确认投票",Confirm,selection&&Remaining());
    const unsigned closeState=hover==CloseButton?(input->IsMouseDown(vgui::MOUSE_LEFT)?2:1):0;
    Image(presentation.closeImages[closeState],presentation.close);
    surface->DrawFlushText();surface->PopMakeCurrent(root);++draws;
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks {
public:
    int GetAltitude()const override{return 35;}
    void Initialize(CreateInterfaceFn*,int)override{}void Start(cl_enginefuncs_s*,int)override{}void Shutdown()override{Close(false);}
    void Key_Event(int& down,int& key,const char*& binding,VGUI2Extension_CallbackContext* context)override{
        if(!visible||context->IsPost)return;
        if(binding&&(!std::strcmp(binding,"toggleconsole")||!std::strcmp(binding,"snapshot")||!std::strcmp(binding,"screenshot"))){
            if(down){if(!std::strcmp(binding,"toggleconsole"))Close(false);std::string command=std::string(binding)+"\n";engine.pfnClientCmd(command.data());}
        }else if(down){
            if(key==27)Close();else if(key==13)Click(Confirm);else if(key>='1'&&key<='9')Click(key-'1');
            else if(key==149||key==150)scroll=std::clamp(scroll+(key==149?48:-48),0,MaxScroll());
            else if(key==241){int x{},y{};input->GetCursorPos(x,y);mouseHeld=true;Click(Hit(x,y));}
        }
        if(context->pPluginReturnValue)*static_cast<int*>(context->pPluginReturnValue)=1;
        context->Result=VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS;
    }
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int& message,unsigned int& wparam,long&,VGUI2Extension_CallbackContext* context)override{
        if(visible&&!context->IsPost&&(message==WM_KILLFOCUS||(message==WM_ACTIVATEAPP&&!wparam)))Close(false);
    }
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)QueueOverlayPaint(OverlayLayer::MapVote,csol::client::Paint);}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUiVisible=false;}void HideConsole(VGUI2Extension_CallbackContext*)override{}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost){gameUiVisible=true;Close(false);}}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)Close(false);}
}overlay;
}
void InitializeScenarioMapVote()
{
    engine.pfnHookUserMsg(mv::MessageName,Receive);engine.pfnAddCommand("csol_mapvote",Open);
    const auto factory=api->GetEngineFactory();surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
    const auto clientFactory=api->GetClientFactory();clientUI=clientFactory?static_cast<IClientVGUI*>(clientFactory(CLIENTVGUI_INTERFACE_VERSION,nullptr)):nullptr;
    const auto module=GetModuleHandleA("vgui2.dll");const auto inputFactory=module?Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(module)):nullptr;
    input=inputFactory?static_cast<vgui::IInput*>(inputFactory(VGUI_INPUT_INTERFACE_VERSION,nullptr)):nullptr;
    const auto plugin=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");const auto pluginFactory=plugin?Sys_GetFactory(plugin):nullptr;
    extension=pluginFactory?static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    if(!registered&&extension&&surface&&input){extension->RegisterBaseUICallbacks(&overlay);registered=true;}
}
void ResetScenarioMapVote()
{
    Close(false);state={};countdown.Reset();loaded=false;selection=0;restoreMouse=false;swallowMouse=false;presentation={};
    if(surface)for(const auto& [path,texture]:textures)if(texture)surface->DeleteTextureByID(texture);textures.clear();
    if(privateFont)RemoveFontMemResourceEx(privateFont);privateFont=nullptr;fontHeight=0;
}
void ShutdownScenarioMapVote()
{
    ResetScenarioMapVote();if(registered&&extension)extension->UnregisterBaseUICallbacks(&overlay);
    if(listPanel){listPanel->MarkForDeletion();listPanel=nullptr;}
    registered=false;extension=nullptr;input=nullptr;surface=nullptr;clientUI=nullptr;font=0;
}
void ScenarioMapVoteFrame()
{
    if(!visible)return;
    if(!Focused()||gameUiVisible||BuyMenuVisible()||CharacterMenuVisible()){Close(false);return;}if(!Remaining()){Close();return;}Layout();
    if(downstream.IN_DeactivateMouse)downstream.IN_DeactivateMouse();surface->UnlockCursor();surface->SetCursor(2);
    int x{},y{};input->GetCursorPos(x,y);hover=Hit(x,y);
    const bool pressed=input->IsMouseDown(vgui::MOUSE_LEFT);if(pressed&&!mouseHeld)Click(hover);
    if(dragging&&pressed&&MaxScroll()){
        const auto thumb=Thumb();const int travel=Scrollbar().h-thumb.h;
        if(travel>0)scroll=std::clamp(dragStart+(int((y-top)/scale)-dragY)*MaxScroll()/travel,0,MaxScroll());
    }
    if(!pressed)dragging=false;mouseHeld=pressed;
}
void ScenarioMapVoteCreateMove(usercmd_t* command,int active)
{
    if(!command)return;
    if(!visible){if(active)RestoreMouse();if(swallowMouse){command->buttons&=~(IN_ATTACK|IN_ATTACK2);if(!input||(!input->IsMouseDown(vgui::MOUSE_LEFT)&&!input->IsMouseDown(vgui::MOUSE_RIGHT)))swallowMouse=false;}return;}
    command->buttons=0;command->forwardmove=command->sidemove=command->upmove=0;
    std::copy_n(angles,3,command->viewangles);engine.SetViewAngles(angles);
}
bool ScenarioMapVoteVisible(){return visible;}
void ScenarioMapVoteDiagnostics(std::ostream& output)
{
    output<<",\"mapVote\":{\"generation\":"<<state.generation<<",\"visible\":"<<visible
        <<",\"maps\":"<<unsigned(state.count)<<",\"draws\":"<<draws<<",\"submissions\":"<<submitted
        <<",\"rejected\":"<<rejected<<",\"mouseRestores\":"<<restores<<'}';
}
}
