// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <map>
#include "../csol-characters/panel_fixture.hpp"
#include "native_stubs.hpp"
#include <vstdlib/IKeyValuesSystem.h>
#include <deque>
extern vgui::IVGui* g_pVGui;
extern vgui::IPanel* g_pVGuiPanel;
extern vgui::ISystem* g_pVGuiSystem;
extern vgui::ILocalize* g_pVGuiLocalize;
extern IKeyValuesSystem* g_pKeyValuesSystem;
void vgui::IVGui::RemoveTickSignal(vgui::VPANEL){}

namespace buy_native {
struct Keys final : IKeyValuesSystem {
    // SDK message maps cache symbols process-wide. Preserve their identity
    // across controlled host scopes, like the actual global KeyValuesSystem.
    inline static std::vector<std::string> symbols;int allocations{};
    void RegisterSizeofKeyValues(int) override{}
    void* AllocKeyValuesMemory(int n) override{++allocations;return std::calloc(1,n);}
    void FreeKeyValuesMemory(void* p) override{--allocations;std::free(p);}
    HKeySymbol GetSymbolForString(const char* name) override{
        for(size_t i=0;i<symbols.size();++i)if(!_stricmp(symbols[i].c_str(),name))return int(i);
        symbols.emplace_back(name);return int(symbols.size()-1);
    }
    const char* GetStringForSymbol(HKeySymbol i) override{return symbols.at(i).c_str();}
    void GetLocalizedFromANSI(const char* text,wchar_t* out,int n) override{MultiByteToWideChar(CP_UTF8,0,text,-1,out,n/sizeof(wchar_t));}
    void GetANSIFromLocalized(const wchar_t* text,char* out,int n) override{WideCharToMultiByte(CP_UTF8,0,text,-1,out,n,nullptr,nullptr);}
    void AddKeyValuesToMemoryLeakList(void*,HKeySymbol) override{}
    void RemoveKeyValuesFromMemoryLeakList(void*) override{}
};
struct Host final : PanelHost {
    std::map<vgui::VPANEL,bool> enabled;
    bool IsEnabled(vgui::VPANEL p) override{const auto it=enabled.find(p);return it==enabled.end()||it->second;}
    void SetEnabled(vgui::VPANEL p,bool value) override{enabled[p]=value;}
    void SetSize(vgui::VPANEL p,int w,int h) override{
        auto& n=nodes[p];const bool changed=n.w!=w||n.h!=h;PanelHost::SetSize(p,w,h);
        if(changed && n.client)n.client->OnSizeChanged(w,h);
    }
    void SetParent(vgui::VPANEL p,vgui::VPANEL parent) override{
        PanelHost::SetParent(p,parent);if(parent && nodes[parent].client)nodes[parent].client->OnChildAdded(p);
    }
    const char* GetName(vgui::VPANEL p) override{return nodes[p].client?nodes[p].client->GetName():"Embedded";}
    const char* GetClassName(vgui::VPANEL p) override{return nodes[p].client?nodes[p].client->GetClassName():"Embedded";}
    vgui::HScheme GetScheme(vgui::VPANEL p) override{return nodes[p].client?nodes[p].client->GetScheme():0;}
    void PerformApplySchemeSettings(vgui::VPANEL p) override{if(nodes[p].client)nodes[p].client->PerformApplySchemeSettings();}
    void Think(vgui::VPANEL p) override{if(nodes[p].client)nodes[p].client->Think();}
    void SendMessage(vgui::VPANEL p,KeyValues* message,vgui::VPANEL from) override{if(nodes[p].client)nodes[p].client->OnMessage(message,from);}
    // Match the pinned VPanelWrapper's client dispatch. Navigation state stays
    // in the real SDK Panel/EditablePanel/FocusNavGroup, never in this boundary.
    bool RequestInfo(vgui::VPANEL p,KeyValues* data) override{return nodes[p].client&&nodes[p].client->RequestInfo(data);}
    void RequestFocus(vgui::VPANEL p,int direction) override{if(nodes[p].client)nodes[p].client->RequestFocus(direction);}
    bool RequestFocusPrev(vgui::VPANEL p,vgui::VPANEL previous) override{return nodes[p].client&&nodes[p].client->RequestFocusPrev(previous);}
    bool RequestFocusNext(vgui::VPANEL p,vgui::VPANEL previous) override{return nodes[p].client&&nodes[p].client->RequestFocusNext(previous);}
    vgui::VPANEL GetCurrentKeyFocus(vgui::VPANEL p) override{return nodes[p].client?nodes[p].client->GetCurrentKeyFocus():0;}
    int GetTabPosition(vgui::VPANEL p) override{return nodes[p].client?nodes[p].client->GetTabPosition():0;}
    vgui::VPANEL Named(vgui::VPANEL parent,const char* name){
        for(auto child:nodes[parent].children){if(!std::strcmp(GetName(child),name))return child;if(auto found=Named(child,name))return found;}
        return 0;
    }
};
struct Gui final : VGuiHost {
    struct Message {vgui::VPANEL target,from;KeyValues* data;};
    std::deque<Message> messages;
    explicit Gui(Host& h):VGuiHost(h){}
    ~Gui(){for(auto m:messages)m.data->deleteThis();}
    void PostMessage(vgui::VPANEL target,KeyValues* data,vgui::VPANEL from,float) override{messages.push_back({target,from,data});}
    vgui::VPANEL HandleToPanel(vgui::HPanel p) override{return host.nodes.contains(p)?p:0;}
    void RunFrame() override{
        unsigned count=0;
        while(!messages.empty()){
            if(++count>10000)throw std::runtime_error("unbounded SDK message loop");
            const auto m=messages.front();messages.pop_front();
            if(auto it=host.nodes.find(m.target);it!=host.nodes.end() && it->second.client)it->second.client->OnMessage(m.data,m.from);
            m.data->deleteThis();
        }
    }
};
struct Input final : vgui::InputBoundary {
    int x{},y{};vgui::VPANEL focus{},capture{};bool left{};
    Host* keyHost{};vgui::VPANEL keyRoot{};
    void SetMouseFocus(vgui::VPANEL p) override{focus=p;}
    vgui::VPANEL GetFocus() override{return keyHost&&keyRoot&&keyHost->IsVisible(keyRoot)?keyHost->GetCurrentKeyFocus(keyRoot):focus;}
    vgui::VPANEL GetMouseOver() override{return focus;}
    void SetMouseCapture(vgui::VPANEL p) override{capture=p;}
    vgui::VPANEL GetMouseCapture() override{return capture;}
    void SetCursorPos(int a,int b) override{x=a;y=b;}
    void GetCursorPos(int& a,int& b) override{a=x;b=y;}
    void GetCursorPosition(int& a,int& b) override{a=x;b=y;}
    bool IsMouseDown(vgui::MouseCode c) override{return c==vgui::MOUSE_LEFT&&left;}
};
struct Scheme final : vgui::SchemeBoundary {
    const char* GetResourceString(const char*) override{return "";}
    vgui::HFont GetFont(const char*,bool) override{return 11;}
    vgui::HFont GetHDFont(const char*,bool,bool) override{return 11;}
    Color GetColor(const char*,Color fallback) override{return fallback;}
};
struct Schemes final : vgui::SchemeManagerBoundary {
    Scheme value;
    vgui::IScheme* GetIScheme(vgui::HScheme) override{return &value;}
    int GetProportionalScaledValue(int v) override{return v;}
    int GetProportionalNormalizedValue(int v) override{return v;}
    int GetProportionalScaledValueEx(vgui::HScheme,int v) override{return v;}
    int GetProportionalNormalizedValueEx(vgui::HScheme,int v) override{return v;}
    int GetProportionalScaledValueEx(vgui::IScheme2*,int v) override{return v;}
    int GetProportionalNormalizedValueEx(vgui::IScheme2*,int v) override{return v;}
};
struct Localize final : vgui::LocalizeBoundary {
    int ConvertANSIToUnicode(const char* text,wchar_t* out,int n) override{return MultiByteToWideChar(CP_UTF8,0,text,-1,out,n/sizeof(wchar_t));}
    int ConvertUnicodeToANSI(const wchar_t* text,char* out,int n) override{return WideCharToMultiByte(CP_UTF8,0,text,-1,out,n,nullptr,nullptr);}
};
struct Surface final : vgui::SurfaceStub {
    Host& host;Gui& gui;int& width;int& height;
    std::vector<vgui::VPANEL> stack;
    unsigned pushes{},widthQueries{},draws{},solves{};vgui::HFont face{};int x{},y{};
    struct Draw {vgui::VPANEL panel;vgui::HFont font;int x,y,length;std::array<int,4> clip;};
    std::vector<Draw> text;bool record{};
    vgui::VPANEL embedded{1};
    Surface(Host& h,Gui& g,int& w,int& t):host(h),gui(g),width(w),height(t){stack.reserve(64);text.reserve(4096);}
    vgui::VPANEL GetEmbeddedPanel() override{return embedded;}
    void PushMakeCurrent(vgui::VPANEL p,bool) override{stack.push_back(p);++pushes;}
    void PopMakeCurrent(vgui::VPANEL p) override{if(stack.empty()||stack.back()!=p)throw std::runtime_error("SDK surface stack");stack.pop_back();}
    float DrawGetAlphaMultiplier() override{return 1;}
    bool ShouldPaintChildPanel(vgui::VPANEL p) override{return host.IsVisible(p);}
    void GetScreenSize(int& w,int& h) override{w=width;h=height;}
    void GetProportionalBase(int& w,int& h) override{w=640;h=480;}
    int GetFontTall(vgui::HFont f) override{return f==12?18:f==15?20:13;}
    int GetCharacterWidth(vgui::HFont f,int ch) override{++widthQueries;return ch=='\n'||ch=='\r'?0:ch==' '?3:f==15?10:7;}
    void GetCharABCwide(vgui::HFont f,int ch,int& a,int& b,int& c) override{a=c=0;b=GetCharacterWidth(f,ch);}
    void GetTextSize(vgui::HFont f,const wchar_t* value,int& w,int& h) override{w=0;for(;*value;++value)w+=GetCharacterWidth(f,*value);h=GetFontTall(f);}
    void SolveTraverse(vgui::VPANEL p,bool) override{
        ++solves;host.PerformApplySchemeSettings(p);host.Think(p);
        for(int i=0;i<host.GetChildCount(p);++i)SolveTraverse(host.GetChild(p,i),true);
        gui.RunFrame();
    }
    void DrawSetTextFont(vgui::HFont f) override{face=f;}
    void DrawSetTextPos(int a,int b) override{x=a;y=b;}
    void DrawGetTextPos(int& a,int& b) override{a=x;b=y;}
    void DrawPrintText(const wchar_t*,int n,vgui::FontDrawType_t) override{
        ++draws;if(!record || stack.empty())return;const auto panel=stack.back();int ax,ay;host.GetAbsPos(panel,ax,ay);
        Draw d{panel,face,x+ax,y+ay,n,{}};host.GetClipRect(panel,d.clip[0],d.clip[1],d.clip[2],d.clip[3]);text.push_back(d);
    }
};
}
