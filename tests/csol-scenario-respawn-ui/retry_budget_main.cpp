// SPDX-License-Identifier: GPL-3.0-or-later
// Executes the production scoreboard resolver against controlled public API
// responses. No binary offsets, synthetic vtables, or graphical acceptance.
#include "native/csol/client/scenario/core/scenario_retry_scoreboard.hpp"
#include <iostream>
#include <map>
#include <source_location>
#include <vector>

namespace
{
using namespace csol::scenario_respawn_ui;
unsigned checks{},failures{};
void Check(bool ok,const char* label,std::source_location at=std::source_location::current())
{++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<" line="<<at.line()<<'\n';}}
struct Tree
{
    struct Node
    {
        std::string name;
        vgui::VPANEL parent{};
        vgui::HPanel handle{};
        bool visible{true};
        std::vector<vgui::VPANEL> children;
    };
    std::map<vgui::VPANEL,Node> nodes;
    std::map<vgui::HPanel,vgui::VPANEL> handles;
    vgui::HPanel nextHandle{100};
    unsigned searches{},nameReads{};
    void Add(vgui::VPANEL id,const char* name,vgui::VPANEL parent=0)
    {
        const auto handle=nextHandle++;
        nodes.emplace(id,Node{name,parent,handle});handles.emplace(handle,id);
        if(parent)nodes.at(parent).children.push_back(id);
    }
    void Erase(vgui::VPANEL id)
    {
        auto& node=nodes.at(id);
        if(node.parent)std::erase(nodes.at(node.parent).children,id);
        handles.erase(node.handle);nodes.erase(id);
    }
    const char* GetName(vgui::VPANEL id){++nameReads;return nodes.at(id).name.c_str();}
    int GetChildCount(vgui::VPANEL id){++searches;return int(nodes.at(id).children.size());}
    vgui::VPANEL GetChild(vgui::VPANEL id,int index){return nodes.at(id).children.at(unsigned(index));}
    vgui::VPANEL GetParent(vgui::VPANEL id){return nodes.at(id).parent;}
    bool IsVisible(vgui::VPANEL id){return nodes.at(id).visible;}
    vgui::HPanel PanelToHandle(vgui::VPANEL id){return nodes.at(id).handle;}
    vgui::VPANEL HandleToPanel(vgui::HPanel handle)
    {const auto found=handles.find(handle);return found==handles.end()?0:found->second;}
};
void VisibilityAndLifetime()
{
    Tree tree;tree.Add(1,"root");tree.Add(2,"viewport",1);tree.Add(3,"ClientScoreBoard",2);
    tree.nodes.at(3).visible=false;
    RetryScoreboard board;
    Check(!board.Visible(tree,tree,1,100),"hidden native scoreboard does not show retry footer");
    const auto searched=tree.searches;
    tree.nodes.at(3).visible=true;
    Check(board.Visible(tree,tree,1,101)==3&&tree.searches==searched,
        "native panel visibility immediately shows cached footer without a key state or tree rescan");
    tree.nodes.at(2).visible=false;
    Check(!board.Visible(tree,tree,1,102),"hidden native viewport suppresses visible child");
    tree.nodes.at(2).visible=true;tree.nodes.at(1).visible=false;
    Check(!board.Visible(tree,tree,1,103),"hidden embedded root suppresses footer");
    tree.nodes.at(1).visible=true;
    tree.Erase(3);
    Check(!board.Visible(tree,tree,1,104),"retired HPanel is resolved before any stale panel access");
    tree.Add(3,"different dialog",2);tree.Add(4,"ClientScoreBoard",2);
    Check(!board.Visible(tree,tree,1,105),"missing-panel retry is bounded and cannot reuse a raw panel id");
    Check(board.Visible(tree,tree,1,1100)==4,"replacement scoreboard discovered after bounded retry");
    tree.Add(9,"unrelated root");tree.nodes.at(4).parent=9;
    std::erase(tree.nodes.at(2).children,vgui::VPANEL{4});
    Check(!board.Visible(tree,tree,1,1101),"detached scoreboard cannot paint in another viewport");
    tree.nodes.at(4).parent=2;tree.nodes.at(2).children.push_back(4);
    Check(board.Visible(tree,tree,1,2100)==4,"reattached native scoreboard can be rediscovered");
    tree.handles.erase(tree.nodes.at(1).handle);
    tree.nodes.at(1).handle=tree.nextHandle++;
    tree.handles.emplace(tree.nodes.at(1).handle,1);
    Check(board.Visible(tree,tree,1,2101)==4,"replacement root generation invalidates old discovery throttle");
    Check(!board.Visible(tree,tree,0,2102),"missing embedded root retires all handles");
    Check(board.Visible(tree,tree,1,2102)==4,"new root can rediscover immediately after full reset");
    board.Reset();
    Check(board.Visible(tree,tree,1,1)==4,"explicit video or session reset permits a fresh client clock");
}
void BoundedDiscovery()
{
    Tree wide;wide.Add(1,"root");
    for(vgui::VPANEL i=2;i<260;++i)wide.Add(i,"child",1);
    RetryScoreboard board;
    Check(!board.Visible(wide,wide,1,0)&&wide.nameReads==1,"excessive child list is not traversed or allocated per frame");
    const auto searches=wide.searches;
    Check(!board.Visible(wide,wide,1,999)&&wide.searches==searches,"failed tree discovery is throttled for one second");
    Tree cycle;cycle.Add(1,"root");cycle.nodes.at(1).children.push_back(1);
    board.Reset();
    Check(!board.Visible(cycle,cycle,1,0)&&cycle.nameReads==1024,"cyclic malformed tree cannot stall HUD rendering");
    Tree ancestry;ancestry.Add(1,"root");ancestry.Add(2,"viewport",1);ancestry.Add(3,"ClientScoreBoard",2);
    ancestry.nodes.at(2).parent=2;
    board.Reset();
    Check(!board.Visible(ancestry,ancestry,1,0),"cyclic parent chain never marks detached board visible");
}
void FooterPlacement()
{
    const Rect native{60,48,520,340};
    const auto below=RetryBudgetFooter(native,640,480);
    Check(below.width==165&&below.height==68&&below.y>=native.y+native.height&&
        below.x>=0&&below.x+below.width<=640&&below.y+below.height<=480,
        "native520x340 board keeps its player list unobscured on640x480");
    const Rect low{60,150,520,300};
    const auto above=RetryBudgetFooter(low,640,480);
    Check(above.width==165&&above.y+above.height<=low.y,"bottom-edge board moves footer above without covering rows");
    Check(!RetryBudgetFooter({0,0,640,480},640,480).width,"fullscreen board with no outside space has no overlapping footer");
    Check(!RetryBudgetFooter(native,160,60).width,"too-small viewport cannot draw beyond its bounds");
    const auto clamped=RetryBudgetFooter({-100,20,200,200},640,480);
    Check(clamped.width==165&&clamped.x==0,"offscreen board footer remains within viewport horizontally");
}
}
int main()
{
    try
    {
        VisibilityAndLifetime();BoundedDiscovery();FooterPlacement();
        std::cout<<"scenario_retry_budget checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
