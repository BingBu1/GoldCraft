// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK Panel lifecycle/traversal with a controlled host VPanel boundary.
#pragma once
#include <VGUI/IVGui.h>
#include <VGUI/IClientPanel.h>
#include "../vgui-resource-refresh/stubs.hpp"
#include <vector>
#include <algorithm>
struct PanelHost : vgui::IPanel {
    struct Node {vgui::IClientPanel* client{};vgui::VPANEL parent{};std::vector<vgui::VPANEL> children;int x{},y{},w{4096},h{4096};bool visible{true},mouse{true},keyboard{true};};
    std::map<vgui::VPANEL,Node> nodes;
    void Init(vgui::VPANEL p,vgui::IClientPanel* c) override{nodes[p].client=c;}
    void SetPos(vgui::VPANEL p,int x,int y) override{nodes[p].x=x;nodes[p].y=y;}
    void GetPos(vgui::VPANEL p,int& x,int& y) override{x=nodes[p].x;y=nodes[p].y;}
    void SetSize(vgui::VPANEL p,int w,int h) override{nodes[p].w=w;nodes[p].h=h;}
    void GetSize(vgui::VPANEL p,int& w,int& h) override{w=nodes[p].w;h=nodes[p].h;}
    void SetMinimumSize(vgui::VPANEL,int,int) override{}
    void GetMinimumSize(vgui::VPANEL,int& w,int& h) override{w=h=0;}
    void SetZPos(vgui::VPANEL,int) override{}
    int GetZPos(vgui::VPANEL) override{return 0;}
    void GetAbsPos(vgui::VPANEL p,int& x,int& y) override{const auto& n=nodes[p];x=n.x;y=n.y;if(n.parent){int px,py;GetAbsPos(n.parent,px,py);x+=px;y+=py;}}
    void GetClipRect(vgui::VPANEL p,int& x,int& y,int& r,int& b) override{GetAbsPos(p,x,y);const auto& n=nodes[p];r=x+n.w;b=y+n.h;if(n.parent){int px,py,pr,pb;GetClipRect(n.parent,px,py,pr,pb);x=std::max(x,px);y=std::max(y,py);r=std::min(r,pr);b=std::min(b,pb);}}
    void SetInset(vgui::VPANEL,int,int,int,int) override{}
    void GetInset(vgui::VPANEL,int& l,int& t,int& r,int& b) override{l=t=r=b=0;}
    void SetVisible(vgui::VPANEL p,bool v) override{nodes[p].visible=v;}
    bool IsVisible(vgui::VPANEL p) override{return nodes[p].visible;}
    void SetParent(vgui::VPANEL p,vgui::VPANEL parent) override{auto& n=nodes[p];if(n.parent){auto& c=nodes[n.parent].children;c.erase(std::remove(c.begin(),c.end(),p),c.end());}n.parent=parent;if(parent)nodes[parent].children.push_back(p);}
    int GetChildCount(vgui::VPANEL p) override{return int(nodes[p].children.size());}
    vgui::VPANEL GetChild(vgui::VPANEL p,int i) override{return nodes[p].children.at(i);}
    vgui::VPANEL GetParent(vgui::VPANEL p) override{return nodes[p].parent;}
    void MoveToFront(vgui::VPANEL) override{}
    void MoveToBack(vgui::VPANEL) override{}
    bool HasParent(vgui::VPANEL p,vgui::VPANEL parent) override{for(p=GetParent(p);p;p=GetParent(p))if(p==parent)return true;return false;}
    bool IsPopup(vgui::VPANEL) override{return false;}
    void SetPopup(vgui::VPANEL,bool) override{}
    bool Render_GetPopupVisible(vgui::VPANEL) override{return false;}
    void Render_SetPopupVisible(vgui::VPANEL,bool) override{}
    vgui::HScheme GetScheme(vgui::VPANEL) override{return 0;}
    bool IsProportional(vgui::VPANEL) override{return false;}
    bool IsAutoDeleteSet(vgui::VPANEL p) override{return nodes[p].client->IsAutoDeleteSet();}
    void DeletePanel(vgui::VPANEL p) override{nodes[p].client->DeletePanel();}
    void SetKeyBoardInputEnabled(vgui::VPANEL p,bool v) override{nodes[p].keyboard=v;}
    void SetMouseInputEnabled(vgui::VPANEL p,bool v) override{nodes[p].mouse=v;}
    bool IsKeyBoardInputEnabled(vgui::VPANEL p) override{return nodes[p].keyboard;}
    bool IsMouseInputEnabled(vgui::VPANEL p) override{return nodes[p].mouse;}
    void Solve(vgui::VPANEL) override{}
    const char* GetName(vgui::VPANEL) override{return "fixture";}
    const char* GetClassName(vgui::VPANEL) override{return "fixture";}
    void SendMessage(vgui::VPANEL,KeyValues*,vgui::VPANEL) override{}
    void Think(vgui::VPANEL) override{}
    void PerformApplySchemeSettings(vgui::VPANEL) override{}
    void PaintTraverse(vgui::VPANEL p,bool force,bool allow=true) override{nodes[p].client->PaintTraverse(force,allow);}
    void Repaint(vgui::VPANEL) override{}
    vgui::VPANEL IsWithinTraverse(vgui::VPANEL,int,int,bool) override{return 0;}
    void OnChildAdded(vgui::VPANEL,vgui::VPANEL) override{}
    void OnSizeChanged(vgui::VPANEL,int,int) override{}
    void InternalFocusChanged(vgui::VPANEL,bool) override{}
    bool RequestInfo(vgui::VPANEL,KeyValues*) override{return false;}
    void RequestFocus(vgui::VPANEL,int) override{}
    bool RequestFocusPrev(vgui::VPANEL,vgui::VPANEL) override{return false;}
    bool RequestFocusNext(vgui::VPANEL,vgui::VPANEL) override{return false;}
    vgui::VPANEL GetCurrentKeyFocus(vgui::VPANEL) override{return 0;}
    int GetTabPosition(vgui::VPANEL) override{return 0;}
    vgui::SurfacePlat* Plat(vgui::VPANEL) override{return nullptr;}
    void SetPlat(vgui::VPANEL,vgui::SurfacePlat*) override{}
    vgui::Panel* GetPanel(vgui::VPANEL p,const char*) override{return dynamic_cast<vgui::Panel*>(nodes[p].client);}
    bool IsEnabled(vgui::VPANEL) override{return true;}
    void SetEnabled(vgui::VPANEL,bool) override{}
    vgui::IClientPanel* Client(vgui::VPANEL p) override{return nodes[p].client;}
    const char* GetModuleName(vgui::VPANEL) override{return "fixture";}
};
struct VGuiHost : vgui::IVGui {
    PanelHost& host;vgui::VPANEL next{2};explicit VGuiHost(PanelHost& h):host(h){}
    bool Init(CreateInterfaceFn*,int) override{return true;}
    void Shutdown() override{}
    void Start() override{}
    void Stop() override{}
    bool IsRunning() override{return true;}
    void RunFrame() override{}
    void ShutdownMessage(unsigned) override{}
    vgui::VPANEL AllocPanel() override{host.nodes[next]={};return next++;}
    void FreePanel(vgui::VPANEL p) override{host.nodes.erase(p);}
    void DPrintf(const char*,...) override{}
    void DPrintf2(const char*,...) override{}
    void SpewAllActivePanelNames() override{}
    vgui::HPanel PanelToHandle(vgui::VPANEL p) override{return p;}
    vgui::VPANEL HandleToPanel(vgui::HPanel p) override{return p;}
    void MarkPanelForDeletion(vgui::VPANEL) override{}
    void AddTickSignal(vgui::VPANEL,int) override{}
    void RemoveTickSignal(vgui::VPANEL) override{}
    void PostMessage(vgui::VPANEL,KeyValues*,vgui::VPANEL,float) override{}
    vgui::HContext CreateContext() override{return 0;}
    void DestroyContext(vgui::HContext) override{}
    void AssociatePanelWithContext(vgui::HContext,vgui::VPANEL) override{}
    void ActivateContext(vgui::HContext) override{}
    void SetSleep(bool) override{}
    bool GetShouldVGuiControlSleep() override{return false;}
};
struct PanelSurface final : vgui::SurfaceStub {
    std::vector<vgui::VPANEL> stack;int pushes{};
    void PushMakeCurrent(vgui::VPANEL p,bool) override{stack.push_back(p);++pushes;}
    void PopMakeCurrent(vgui::VPANEL p) override{if(stack.empty() || stack.back()!=p)throw std::runtime_error("unbalanced Panel surface");stack.pop_back();}
    float DrawGetAlphaMultiplier() override{return 1;}
    bool ShouldPaintChildPanel(vgui::VPANEL) override{return true;}
};
