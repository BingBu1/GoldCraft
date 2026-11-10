// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/characters.cpp"
#include <iostream>
#include <fstream>
#include "../csol-hud-style/surface_stub.hpp"
#include "../csol-hud-style/interface_fixture.hpp"
#include <map>
#include <source_location>
#include "panel_fixture.hpp"
extern vgui::IVGui* g_pVGui;
extern vgui::IPanel* g_pVGuiPanel;
vgui::IInput2* g_pVGuiInput2{};
vgui::ISurface2* g_pVGuiSurface2{};
vgui::ISchemeManager2* g_pVGuiSchemeManager2{};
void vgui::IVGui::RemoveTickSignal(vgui::VPANEL){}
class MouseInput final : public vgui::IInput {
public:
    bool down{};int x{},y{};
    void SetMouseFocus(vgui::VPANEL) override{}
    void SetMouseCapture(vgui::VPANEL) override{}
    void GetKeyCodeText(vgui::KeyCode,char*,int) override{}
    vgui::VPANEL GetFocus() override{return 0;}
    vgui::VPANEL GetMouseOver() override{return 0;}
    void SetCursorPos(int a,int b) override{x=a;y=b;}
    void GetCursorPos(int& a,int& b) override{a=x;b=y;}
    bool WasMousePressed(vgui::MouseCode) override{return false;}
    bool WasMouseDoublePressed(vgui::MouseCode) override{return false;}
    bool IsMouseDown(vgui::MouseCode code) override{return code==vgui::MOUSE_LEFT && down;}
    void SetCursorOveride(vgui::HCursor) override{}
    vgui::HCursor GetCursorOveride() override{return 0;}
    bool WasMouseReleased(vgui::MouseCode) override{return false;}
    bool WasKeyPressed(vgui::KeyCode) override{return false;}
    bool IsKeyDown(vgui::KeyCode) override{return false;}
    bool WasKeyTyped(vgui::KeyCode) override{return false;}
    bool WasKeyReleased(vgui::KeyCode) override{return false;}
    vgui::VPANEL GetAppModalSurface() override{return 0;}
    void SetAppModalSurface(vgui::VPANEL) override{}
    void ReleaseAppModalSurface() override{}
    void GetCursorPosition(int& a,int& b) override{GetCursorPos(a,b);}
};

struct MenuSurface final : vgui::HudTestSurface {
    int width{1280},height{720},added{},characterWidths{},textSizes{};vgui::HFont next{1};std::map<vgui::HFont,int> heights;
    PanelHost* panels{};PanelSurface* panelSurface{};int textX{},textY{};
    struct DrawnGlyph {wchar_t character;int x,y;std::array<int,4> clip;};
    std::vector<DrawnGlyph> drawn;
    void GetScreenSize(int& w,int& h) override{w=width;h=height;}
    vgui::VPANEL GetEmbeddedPanel() override{return 1;}
    vgui::HFont CreateFont() override{return next++;}
    bool AddGlyphSetToFont(vgui::HFont h,const char*,int tall,int,int,int,int,int,int) override{
        if(heights.contains(h))throw std::runtime_error("immutable font reconfigured");
        heights[h]=tall;++added;return true;
    }
    int GetFontTall(vgui::HFont h) override{return heights.at(h);}
    int GetCharacterWidth(vgui::HFont,int ch) override{++characterWidths;return ch=='W'?9:ch=='i'?2:ch=='\r'||ch=='\n'?0:5;}
    void GetTextSize(vgui::HFont h,const wchar_t* text,int& w,int& tall) override{++textSizes;tall=heights.at(h);w=int(std::wcslen(text))*tall;}
    void DrawSetTextPos(int x,int y) override{textX=x;textY=y;}
    void DrawUnicodeChar(wchar_t ch) override{
        if(!panels || !panelSurface || panelSurface->stack.empty())return;
        const auto panel=panelSurface->stack.back();int x,y;panels->GetAbsPos(panel,x,y);
        DrawnGlyph glyph{ch,x+textX,y+textY,{}};
        panels->GetClipRect(panel,glyph.clip[0],glyph.clip[1],glyph.clip[2],glyph.clip[3]);drawn.push_back(glyph);
    }
};
struct MenuSchemes final : vgui::ISchemeManager {
    int height{720},scaled{};
    vgui::HScheme LoadSchemeFromFile(const char*,const char*) override{return 0;}
    void ReloadSchemes() override{}
    vgui::HScheme GetDefaultScheme() override{return 0;}
    vgui::HScheme GetScheme(const char*) override{return 0;}
    vgui::IImage* GetImage(const char*,bool) override{return nullptr;}
    vgui::HTexture GetImageID(const char*,bool) override{return 0;}
    vgui::IScheme* GetIScheme(vgui::HScheme) override{return nullptr;}
    void Shutdown(bool) override{}
    int GetProportionalScaledValue(int v) override{++scaled;return int(v*(height/480.f));}
    int GetProportionalNormalizedValue(int v) override{return v;}
};

CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};
std::vector<std::byte> ReadResource(const char*){throw std::runtime_error("unexpected resource read");}
void WeaponInventoryCreateMove(float,usercmd_t*,int){}
int ScenarioHudLevel(){return 1;}
void* RegisterResourceFont(void*&,const char*,const char*,std::span<const std::byte>,unsigned long*){throw std::runtime_error("unexpected font registration");}
}
int main(int argc,char** argv)
{
    using namespace csol;using namespace csol::client;int checks=0;
    static int lookups=0,sends=0;static cl_entity_t entity;entity.index=1;
    engine.GetLocalPlayer=[]()->cl_entity_t*{throw std::runtime_error("unsafe loading-screen local entity read");};
    engine.pfnGetLevelName=[]()->const char*{return "";};
    engine.GetEntityByIndex=[](int index)->cl_entity_t*{++lookups;return index==1?&entity:nullptr;};
    engine.GetClientTime=[]()->float{return 1;};engine.pfnServerCmd=[](const char*)->int{++sends;return 1;};
    try {
        CharacterFrame();if(lookups||sends)return 1;++checks;
        engine.pfnGetLevelName=[]()->const char*{return "maps/cs_assault.bsp";};
        CharacterFrame();if(lookups||sends)return 2;++checks;
        view.maxclients=32;view.playernum=0;CharacterFrame();if(lookups!=1||sends!=1)return 3;++checks;
        CharacterFrame();if(lookups!=2||sends!=1)return 4;++checks;
        characters[1]={1,0,0,34};engine.GetClientTime=[]()->float{return 5;};CharacterFrame();if(sends!=1)return 5;++checks;
        engine.pfnGetLevelName=[]()->const char*{return "";};CharacterFrame();if(lookups!=3)return 6;++checks;
        Character character;character.id=168;catalog={character};loaded=true;chosen=168;
        engine.GetLocalPlayer=[]()->cl_entity_t*{return &entity;};
        auto packet=EncodeCharacter({1,1,168,34});visible=true;selectionPending=false;
        Message(CharacterMessage,int(packet.size()),packet.data());if(!visible)return 8;++checks;
        selectionPending=true;pendingId=168;Message(CharacterMessage,int(packet.size()),packet.data());if(visible)return 9;++checks;
        catalog.clear();for(int i=1;i<=24;++i){Character c;c.id=i;c.team=i<=12?1:2;catalog.push_back(c);}
        for(auto& rows:teamRows)rows.clear();for(const auto& c:catalog)teamRows[c.team-1].push_back(&c);
        visible=true;left=100;top=50;scale=1.5f;team=1;page=0;chosen=1;
        MouseInput mouse;input=&mouse;mouse.SetCursorPos(X(20),Y(80));mouse.down=true;
        CharacterFrame();if(chosen!=1)return 10;++checks;
        mouse.down=false;CharacterFrame();if(chosen!=2)return 10;++checks;mouse.down=true;CharacterFrame();
        mouse.SetCursorPos(X(20),Y(105));CharacterFrame();if(chosen!=2)return 11;++checks;
        mouse.down=false;CharacterFrame();if(chosen!=3)return 12;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(150),Y(35));mouse.down=true;CharacterFrame();mouse.down=false;CharacterFrame();if(team!=2||chosen!=13)return 13;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(150),Y(320));mouse.down=true;CharacterFrame();mouse.down=false;CharacterFrame();if(page!=1)return 14;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(30),Y(320));mouse.down=true;CharacterFrame();mouse.down=false;CharacterFrame();if(page!=0)return 15;++checks;
        auto key=[&](int code){int down=1,result=0;const char* binding=nullptr;VGUI2Extension_CallbackContext context;context.pPluginReturnValue=&result;overlay.Key_Event(down,code,binding,&context);return result==1 && context.Result==VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS;};
        if(!key(149)||page!=1)return 16;++checks;
        if(!key('2')||chosen!=24)return 17;++checks;
        if(!key(150)||page!=0)return 18;++checks;
        if(!key(130)||team!=1||chosen!=1)return 19;++checks;
        if(!key(131)||team!=2||chosen!=13)return 20;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(400),Y(385));mouse.down=true;
        const int before=sends;CharacterFrame();CharacterFrame();if(sends!=before)return 21;++checks;mouse.down=false;CharacterFrame();if(sends!=before+1||!selectionPending)return 21;++checks;
        usercmd_t command{};command.buttons=3;command.forwardmove=200;command.sidemove=100;
        engine.SetViewAngles=[](float*){};CharacterCreateMove(.01f,&command,1);if(command.buttons||command.forwardmove||command.sidemove)return 22;++checks;
        if(!key(27)||visible||selectionPending)return 23;++checks;
        if(argc!=2)throw std::runtime_error("supply the generated original menu JSON");
        std::ifstream file(argv[1],std::ios::binary);std::string json((std::istreambuf_iterator<char>(file)),{});
        original=ParseCharacterMenu(json);originalLoaded=true;originalMode=true;visible=true;page=0;team=1;chosen=1;hovered=-1;mouse.down=false;
        auto check=[&](bool v,std::source_location at=std::source_location::current()){++checks;if(!v)throw std::runtime_error("character check "+std::to_string(checks)+" at line "+std::to_string(at.line()));};
        check(original.descriptions.size()==151 && original.width==606 && original.height==420);
        MenuSurface fake;MenuSchemes proportional;surface=&fake;schemes=&proportional;extension=&interface_fixture::extension;
        privateFont=reinterpret_cast<HANDLE>(1);teamLabels={L"TER",L"CT"};Layout();check(PrepareOriginalFont());
        const auto firstFont=originalFont;const auto firstTabs=teamPixels;const int initialAdded=fake.added,initialScaled=proportional.scaled;
        check(PrepareOriginalFont() && fake.added==initialAdded && proportional.scaled==initialScaled);
        fake.width=1920;fake.height=1080;proportional.height=1080;Layout();check(PrepareOriginalFont() && originalFont==firstFont && fake.added==initialAdded);
        check(originalRecipeTall==14 && titleRecipeTall==19 && tabRecipeTall==12 && proportional.scaled==initialScaled);
        check(teamPixels[0].x!=firstTabs[0].x && teamPixels[0].w==firstTabs[0].w && teamPixels[0].h>firstTabs[0].h);
        fake.width=1280;fake.height=720;proportional.height=720;Layout();check(PrepareOriginalFont() && originalFont==firstFont);
        const int afterResize=fake.added;check(PrepareOriginalFont() && fake.added==afterResize);
        auto glyphs=[&](std::wstring_view value,int width,std::initializer_list<DescriptionGlyph> expected){
            descriptionLayout.Prepare(value,width,originalFont,13);check(descriptionLayout.glyphs.size()==expected.size());
            size_t n=0;for(const auto& want:expected){const auto& got=descriptionLayout.glyphs[n++];check(got.character==want.character && got.x==want.x && got.y==want.y);}
        };
        const int textBefore=fake.textSizes;
        glyphs(L"WiW",11,{{'W',0,0},{'i',9,0},{'W',0,13}});
        glyphs(L"W",8,{{'W',0,13}}); // original preserves the initial empty line
        glyphs(L"\nWi\rW",11,{{'W',0,13},{'i',9,13},{'W',11,13}});
        glyphs(L"&&i",20,{{'&',0,0},{'i',5,0}});
        glyphs(L"&Wi",11,{{'W',0,13},{'i',9,13}});
        glyphs(L"W i",10,{{'W',0,0},{'i',0,13}});
        glyphs(L"W&ii",10,{{'W',0,0},{'i',9,0},{'i',11,0}}); // skipped marker leaves its original break cursor pending
        check(fake.textSizes==textBefore);
        PanelHost panelHost;VGuiHost vguiHost(panelHost);PanelSurface panelSurface;
        g_pVGui=&vguiHost;g_pVGuiPanel=&panelHost;g_pVGuiSurface2=&panelSurface;
        struct Retire {~Retire(){RetireDetailPanels();g_pVGui=nullptr;g_pVGuiPanel=nullptr;g_pVGuiSurface2=nullptr;}} retire;
        fake.panels=&panelHost;fake.panelSurface=&panelSurface;
        descriptions[1]=L"WiW";chosen=1;visible=true;
        // Scenario-only override in the controlled boundary below.
        PrepareDetailPanels();
        check(detailRoot && detailHelper && detailLabel);
        auto& helperNode=panelHost.nodes.at(detailHelper->GetVPanel());
        auto& labelNode=panelHost.nodes.at(detailLabel->GetVPanel());
        check(helperNode.parent==detailRoot->GetVPanel() && labelNode.parent==detailHelper->GetVPanel());
        check(helperNode.w==int(original.info.w*HorizontalScale()) && helperNode.h==int(original.info.h*scale));
        check(labelNode.h==int(original.scenarioDescription.h*scale));
        int cx,cy,cr,cb;panelHost.GetClipRect(detailLabel->GetVPanel(),cx,cy,cr,cb);
        int hx,hy;panelHost.GetAbsPos(detailHelper->GetVPanel(),hx,hy);
        check(original.info.h==380); // paged resource, distinct from ordinary/constructor size
        check(cb==std::min({originalRoot.y+originalRoot.h,hy+helperNode.h,hy+labelNode.y+labelNode.h}));
        check(cr==std::min({originalRoot.x+originalRoot.w,hx+helperNode.w,hx+labelNode.x+labelNode.w}));
        const int stableWidths=fake.characterWidths;
        const auto* stableGlyphs=descriptionLayout.glyphs.data();
        PrepareDetailPanels();detailRoot->PaintOverlay();
        check(fake.characterWidths==stableWidths && descriptionLayout.glyphs.data()==stableGlyphs);
        check(panelSurface.pushes>=2 && panelSurface.stack.empty());
        int dx,dy;panelHost.GetAbsPos(detailLabel->GetVPanel(),dx,dy);
        check(fake.drawn.size()==3 && fake.drawn[0].x==dx && fake.drawn[0].y==dy);
        check(fake.drawn[1].x==dx+9 && fake.drawn[2].clip==std::array<int,4>{cx,cy,cr,cb});
        descriptions[2]=L"ii";chosen=2;PrepareDetailPanels();
        check(preparedDetail==2 && fake.characterWidths==stableWidths+2 && descriptionLayout.glyphs.size()==2);
        fake.width=1000;fake.height=700;proportional.height=700;Layout();check(PrepareOriginalFont());PrepareDetailPanels();
        check(preparedDetailScreenW==1000 && preparedDetailScreenH==700 && preparedDetailWidth==int(original.scenarioDescription.w*HorizontalScale()));
        const auto bodyFace=originalFont;originalFont=originalTitle;
        const int beforeFace=fake.characterWidths;PrepareDetailPanels();
        check(preparedDetailFont==originalTitle && fake.characterWidths==beforeFace+2);
        originalFont=bodyFace;PrepareDetailPanels();
        // Host-boundary regression: deliberately overflow both ancestors;
        // resource dimensions above are preserved by the production adapter.
        detailRoot->SetSize(100,150);detailHelper->SetBounds(80,100,400,200);detailLabel->SetBounds(0,127,300,80);
        panelHost.GetClipRect(detailLabel->GetVPanel(),cx,cy,cr,cb);
        check(cr==originalRoot.x+100 && cb==originalRoot.y+150 && cb<cy);
        const int beforeEmpty=panelSurface.pushes;detailRoot->PaintOverlay();check(panelSurface.pushes==beforeEmpty+1); // helper paints, empty label does not
        const int beforeOrdinary=panelSurface.pushes;detailRoot->PaintTraverse(true);
        check(panelSurface.pushes==beforeOrdinary);
        Close();check(!detailRoot->IsVisible());RetireDetailPanels();check(panelHost.nodes.size()==1);
        g_pVGui=nullptr;g_pVGuiPanel=nullptr;g_pVGuiSurface2=nullptr;
        surface=nullptr;visible=true;
        check(&Filtered()==&teamRows[0] && Filtered().size()==12 && Filtered()[0]==&catalog[0]);
        const int beforeHover=sends;
        auto r=original.slots[1];Hover(X(r.x+10),Y(r.y+10));check(chosen==2 && sends==beforeHover);
        Hover(X(-10),Y(-10));check(chosen==2 && sends==beforeHover);
        check(key('3') && sends==beforeHover+1 && pendingId==3);
        r=original.slots[4];Hover(X(r.x+10),Y(r.y+10));check(chosen==5 && pendingId==3);
        packet=EncodeCharacter({1,1,3,34});Message(CharacterMessage,int(packet.size()),packet.data());check(!visible && !selectionPending);
        visible=true;const int beforeClick=sends;r=original.slots[5];mouse.SetCursorPos(X(r.x+10),Y(r.y+10));mouse.down=true;
        CharacterFrame();CharacterFrame();check(sends==beforeClick && leftInput.Held());
        mouse.down=false;CharacterFrame();check(sends==beforeClick+1 && pendingId==6);
        auto event=[&](bool pressed){int down=pressed,keycode=241,result=0;const char* binding=nullptr;VGUI2Extension_CallbackContext context;context.pPluginReturnValue=&result;overlay.Key_Event(down,keycode,binding,&context);check(result==1 && context.Result==VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS);};
        // Both callbacks can arrive between HUD frames; release dispatches once.
        const int fast=sends;event(true);check(sends==fast);event(false);check(sends==fast+1);
        mouse.down=true;CharacterFrame();check(sends==fast+1); // stale poll cannot recapture
        mouse.down=false;CharacterFrame();check(sends==fast+1);
        // Event press followed by a missing engine release falls back to polling.
        event(true);mouse.down=true;CharacterFrame();mouse.down=false;CharacterFrame();check(sends==fast+2);
        // Press on a row then release outside cancels the captured command.
        const int cancel=sends;event(true);mouse.SetCursorPos(-100,-100);event(false);check(sends==cancel);
        CharacterFrame();
        check(key('=') && page==1 && chosen==10);check(key('=') && page==1);check(key('-') && page==0 && chosen==1);
        // Check the actual integer Hit path at every inclusive/exclusive edge.
        for(auto dimensions: {std::pair{640,480},std::pair{1000,700},std::pair{1920,1080},std::pair{800,1000}}){
            fake.width=dimensions.first;fake.height=dimensions.second;proportional.height=fake.height;surface=&fake;
            Layout();check(PrepareOriginalFont());surface=nullptr;
            auto bounds=[&](const menu::Rect& b,int hit){
                check(b.w>0 && b.h>0);check(OriginalPixelHit(b.x,b.y)==hit);
                check(OriginalPixelHit(b.x+b.w-1,b.y+b.h-1)==hit);
                check(OriginalPixelHit(b.x-1,b.y)!=hit);check(OriginalPixelHit(b.x+b.w,b.y)!=hit);
                check(OriginalPixelHit(b.x,b.y-1)!=hit);check(OriginalPixelHit(b.x,b.y+b.h)!=hit);
            };
            bounds(PixelBounds(original.slots[0]),0);bounds(PixelBounds(original.previous),12);
            // The last tab row overlaps row0 in the original resources; z-order
            // fidelity remains unproved. Test only its unambiguous top edge.
            const auto tab=CharacterTabBounds()[0];check(OriginalPixelHit(tab.x,tab.y)==10);
            check(OriginalPixelHit(tab.x+tab.w-1,tab.y)==10);
            check(OriginalPixelHit(tab.x-1,tab.y)!=10);check(OriginalPixelHit(tab.x+tab.w,tab.y)!=10);
            check(OriginalPixelHit(tab.x,tab.y-1)!=10);bounds(CharacterCloseBounds(),14);
        }
        originalMode=false;chosen=1;selectionPending=false;check(key('2') && chosen==2 && !selectionPending);
        void* window=nullptr;unsigned int msg=WM_ACTIVATEAPP,param=0;long lparam=0;VGUI2Extension_CallbackContext focus;
        overlay.CallEngineSurfaceWndProc(window,msg,param,lparam,&focus);check(!visible);
        visible=true;appearanceMenu=true;
        overlay.CallEngineSurfaceWndProc(window,msg,param,lparam,&focus);check(visible && appearanceMenu && focusSuspended);
        const int beforeSuspended=sends;check(key('1') && sends==beforeSuspended && focusSuspended);
        mouse.SetCursorPos(X(400),Y(385));mouse.down=true;
        param=1;overlay.CallEngineSurfaceWndProc(window,msg,param,lparam,&focus);check(visible && appearanceMenu && !focusSuspended);
        check(leftInput.Held() && leftInput.Captured()==-1);CharacterFrame();check(sends==beforeSuspended);
        mouse.down=false;CharacterFrame();mouse.down=true;CharacterFrame();check(sends==beforeSuspended);mouse.down=false;CharacterFrame();check(sends==beforeSuspended+1);
        Close();
        for(const char* invalid:{"{}","[]","{\"version\":2}"}){bool rejected=false;try{ParseCharacterMenu(invalid);}catch(const std::exception&){rejected=true;}check(rejected);}
        // Appearance confirmation may arrive before the first Spawn activates
        // the model. A different player's state must never accept our choice.
        visible=true;appearanceMenu=true;team=1;chosen=1;selectionPending=true;pendingId=1;
        // Browsing either team is allowed by the current human request; only
        // the authenticated local server confirmation may accept the choice.
        check(key(131) && team==2 && pendingId==1 && selectionPending);
        check(key(130) && team==1 && pendingId==1 && selectionPending);
        packet=EncodeCharacter({2,0,1,99});Message(CharacterMessage,int(packet.size()),packet.data());check(visible);
        packet=EncodeCharacter({1,0,1,34});Message(CharacterMessage,int(packet.size()),packet.data());check(!visible);
        visible=true;appearanceMenu=true;const int beforeCancel=sends;check(key(27) && sends==beforeCancel+1 && !appearanceMenu);
        static int nativeVGUI=0,nativeMenus=0;static std::string forwarded;
        previousVGUI=[](const char*,int,void*){++nativeVGUI;return 42;};
        previousShowMenu=[](const char*,int size,void* data){++nativeMenus;auto* b=static_cast<char*>(data);if(size>=5)forwarded+=b+4;return 43;};
        unsigned char vgui[1]{26};serverCharacters=false;
        check(VGUIMenu("VGUIMenu",1,vgui)==42 && nativeVGUI==1);
        serverCharacters=true;check(VGUIMenu("VGUIMenu",1,vgui)==42); // no usable surface: native fallback
        vgui[0]=2;check(VGUIMenu("VGUIMenu",1,vgui)==42); // team menu is untouched
        auto textPacket=[&](std::string text,bool more){std::vector<unsigned char> b{31,0,255,static_cast<unsigned char>(more)};b.insert(b.end(),text.begin(),text.end());b.push_back(0);return b;};
        auto fragment=textPacket("#Terrorist_",true);check(ShowMenu("ShowMenu",int(fragment.size()),fragment.data())==1 && nativeMenus==0);
        fragment=textPacket("Select",false);check(ShowMenu("ShowMenu",int(fragment.size()),fragment.data())==43 && nativeMenus==2 && forwarded=="#Terrorist_Select");
        forwarded.clear();fragment=textPacket("ZP buy menu",false);check(ShowMenu("ShowMenu",int(fragment.size()),fragment.data())==43 && forwarded=="ZP buy menu");
        unsigned char invalidMenu[5]{1,0,255,0,'x'};check(ShowMenu("ShowMenu",4,invalidMenu)==43);
        previousVGUI=nullptr;previousShowMenu=nullptr;
        mouse.down=false;input=nullptr;
        std::cout<<"Character frame lifecycle checks: "<<checks<<" passed\n";
    }catch(const std::exception& e){std::cerr<<e.what();return 7;}
}
