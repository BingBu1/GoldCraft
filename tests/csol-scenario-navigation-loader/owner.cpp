// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <unordered_map>

namespace nav_loader_test
{
namespace
{
Place Resolve(const char* name)
{
    const std::string_view n=name;
    if(n=="known"||n=="duplicate")return 17;
    if(n=="last")return 29;
    return UNDEFINED_PLACE;
}
void CompareActual(const NavigationFile& file,const CCSOLScenarioNavigation& owner)
{
    Require(owner.Areas().size()==file.areas.size(),"owner publishes every parsed area");
    std::unordered_map<unsigned,CNavArea*> ids;
    for(auto* a:owner.Areas())ids.emplace(a->GetID(),a);
    std::size_t edges{},hiding{},sevens{};
    for(std::size_t i=0;i<file.areas.size();++i)
    {
        const auto& r=file.areas[i];const auto* a=owner.Areas()[i];
        Check(a->GetID()==r.id&&a->GetAttributes()==r.attributes&&a->GetPlace()==0,"actual SDK area identity attribute place");
        const auto& ext=*a->GetExtent();
        Check(ext.lo.x==r.low.x&&ext.lo.y==r.low.y&&ext.lo.z==r.low.z&&ext.hi.x==r.high.x&&ext.hi.y==r.high.y&&ext.hi.z==r.high.z,"SDK original extents in file order");
        for(unsigned direction=0;direction<4;++direction)
        {
            const auto* list=a->GetAdjacentList(static_cast<NavDirType>(direction));
            Require(list->size()==r.connections[direction].size(),"SDK connection list length exact");auto it=list->begin();
            for(auto id:r.connections[direction]) {Check(it->area==(id&&ids.contains(id)?ids.at(id):nullptr),"SDK connection resolves exact target");++it;++edges;}
        }
        Require(a->GetHidingSpotList()->size()==r.hidingSpots.size(),"SDK hiding list length exact");auto spot=a->GetHidingSpotList()->begin();
        for(const auto& s:r.hidingSpots)
        {
            const auto* p=(*spot)->GetPosition();
            Check((*spot)->GetID()==s.id&&(*spot)->GetFlags()==s.flags&&p->x==s.position.x&&p->y==s.position.y&&p->z==s.position.z,"SDK hiding identity position flags");++spot;++hiding;
        }
        Require(a->GetApproachInfoCount()==static_cast<int>(r.approaches.size()),"SDK fixed approach count exact");
        for(std::size_t n=0;n<r.approaches.size();++n)
        {
            const auto& p=r.approaches[n];const auto* actual=a->GetApproachInfo(static_cast<int>(n));
            const auto resolve=[&](unsigned id){return id&&ids.contains(id)?ids.at(id):nullptr;};
            Check(actual->here.area==resolve(p.here)&&actual->prev.area==resolve(p.previous)&&actual->next.area==resolve(p.next),"SDK approach pointer conversion preserves nulls");
            Check(static_cast<unsigned>(actual->prevToHereHow)==p.previousHow&&static_cast<unsigned>(actual->hereToNextHow)==p.nextHow,"SDK approach traversal including unspecified sentinel preserved");
            sevens+=(p.previousHow==7)+(p.nextHow==7);
        }
    }
    Check(edges==4175&&hiding==24&&sevens==3,"complete actual SDK graph populations");
}
}
void OwnerCases(const char* actualNav)
{
    Host host;BotGlobals globals;Files files;csol::scenario::ResourceError error;
    const auto data=Read(std::filesystem::u8path(actualNav));NavigationFile parsed;ConfigError parseError;
    Require(ReadNavigationV5(Bytes(data),parsed,parseError),"parse actual map for semantic owner comparison");
    files.contents["maps/zs_deadend.nav"]=data;
    auto owner=CCSOLScenarioNavigation::Load("zs_deadend",files.Api(),error,1);
    Require(owner&&owner->World().Ready(),"actual first map loads despite BSP size mismatch");
    Check(owner->BspSizeMismatch()&&owner->StoredBspSize()==7361964&&owner->UnresolvedReferences()==640,"BSP mismatch and unresolved 640 are diagnostics not whole-map rejection");
    Check(files.buffers.empty()&&files.order==std::vector<std::string>({"load:maps/zs_deadend.nav","free:maps/zs_deadend.nav"}),"engine file released exactly once after successful owner construction");
    CompareActual(parsed,*owner);globals.CheckUnchanged();owner.reset();globals.CheckUnchanged();

    auto rich=RichFile();rich.places.push_back("last");rich.areas.push_back(Area(44,550));rich.areas[3].place=4;
    files.contents["maps/rich.nav"]=Encode(rich);
    owner=CCSOLScenarioNavigation::Load("rich",files.Api(),error,321,Resolve);
    Require(bool(owner),"rich owner uses actual SDK storage and local place resolver");
    Check(!owner->BspSizeMismatch()&&FindArea(*owner,41)->GetPlace()==17&&FindArea(*owner,42)->GetPlace()==29&&FindArea(*owner,43)->GetPlace()==0&&FindArea(*owner,44)->GetPlace()==0,"place indices use original filtered unique directory");
    const auto* links=FindArea(*owner,41)->GetAdjacentList(NORTH);auto it=links->begin();
    Require(links->size()==4,"duplicate and null SDK links retained");
    Check((it++)->area==FindArea(*owner,42)&&(it++)->area==FindArea(*owner,42)&&(it++)->area==nullptr&&(it++)->area==nullptr,"ConnectTo deduplication not substituted");
    auto* encounter=FindArea(*owner,41)->GetSpotEncounter(FindArea(*owner,41),FindArea(*owner,42));
    Require(encounter&&encounter->spotList.size()==3,"SDK encounter and all spot records retained");
    auto order=encounter->spotList.begin();
    Check(order->spot&&order->spot->GetID()==71&&order->t==0,"first encounter spot resolves");++order;
    Check(order->spot&&order->spot->GetID()==72&&order->t==1,"last endpoint byte255 normalizes to one");++order;
    Check(!order->spot&&order->t==128.0f/255.0f,"unresolved encounter spot is retained with position");
    Check(encounter->path.from.z==36&&encounter->path.to.z==36,"real SDK portal endpoints receive original eye height36");
    globals.CheckUnchanged();owner.reset();globals.CheckUnchanged();

    for(std::string_view name:{"","../escape","a/b","a\\b","a.b"})
    {const auto count=files.order.size();Check(!CCSOLScenarioNavigation::Load(name,files.Api(),error),"invalid map name rejected");Check(files.order.size()==count,"invalid map avoids engine callback");}
    Check(!CCSOLScenarioNavigation::Load("missing",files.Api(),error)&&error.path=="maps/missing.nav","missing nav reports exact resource path");
    files.contents["maps/bad.nav"]="bad";
    Check(!CCSOLScenarioNavigation::Load("bad",files.Api(),error)&&files.buffers.empty(),"corrupt nav releases engine file and returns no owner");
    files.reportedLength=-1;
    Check(!CCSOLScenarioNavigation::Load("rich",files.Api(),error)&&files.buffers.empty(),"negative engine length rejected and allocation released");files.reportedLength.reset();
    NavigationFile empty;owner=Load(files,empty);Check(!owner->World().Ready()&&owner->Areas().empty(),"zero-area loader returns metadata owner but no navigable world");owner.reset();
    globals.CheckUnchanged();
}
void LifetimeCases()
{
    Host host;BotGlobals globals;
    ordinary_test::allocation_probe::Begin();
    {
        Files files;auto owner=Load(files,RichFile());auto old=std::shared_ptr<const CCSOLMonsterNavigation>(owner);
        std::weak_ptr<CCSOLScenarioNavigation> weak=owner;auto* area=owner->Areas().front();
        const auto generation=owner->World().Generation();owner->Invalidate();owner.reset();
        Check(!weak.expired()&&!old->World().Ready()&&old->World().Generation()>generation,"invalidated map retained by old behavior lease");
        Check(area->GetID()==41&&area->GetAdjacentList(NORTH)->size()==4,"retained old graph lists alive while query lease exists");
        auto next=Load(files,LadderFile());Check(next->World().Ready()&&next->Areas().front()!=area,"new map storage independent of old lease");
        old.reset();Check(weak.expired(),"old NAV storage expires after last query lease");weak.reset();next.reset();
    }
    ordinary_test::allocation_probe::End();
    Check(!ordinary_test::allocation_probe::Overflowed()&&ordinary_test::allocation_probe::Outstanding()==0,"all owned graph lists spots file and control blocks released");
    globals.CheckUnchanged();
    // Scenario-owned open head is the one intentional SDK search cleanup;
    // independent Bot head was separately checked across all ordinary loads.
    CNavArea::ClearSearchLists();
    {
        Files files;auto owner=Load(files,LadderFile());auto* a=owner->Areas().front();
        a->SetTotalCost(1);a->AddToOpenList();Check(!CNavArea::IsOpenListEmpty(),"owned area is real SDK open head");
        owner.reset();Check(CNavArea::IsOpenListEmpty(),"owner destructor clears its own dangling SDK open head");
    }
}
}
