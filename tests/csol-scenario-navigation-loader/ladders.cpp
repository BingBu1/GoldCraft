// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <limits>

namespace nav_loader_test
{
void LadderCases()
{
    for(auto direction:{NORTH,EAST,SOUTH,WEST})
    {
        Host host;BotGlobals globals;Files files;auto owner=Load(files,LadderFile());ConfigError error;
        const bool wide=direction==NORTH||direction==SOUTH;
        auto& entity=host.Ladder(wide?Vector(-20,-2,0):Vector(-2,-20,0),wide?Vector(20,2,100):Vector(2,20,100),true);
        host.onTrace=[&](std::size_t n,TraceResult& result){if(n==0&&(direction==NORTH||direction==WEST))result.flFraction=.5f;};
        Require(owner->BuildLadders(error),"real func_ladder enumeration and SDK ladder construction");
        Require(owner->LadderCount()==1&&owner->ClimberLadders().size()==1,"one ladder registered and targetname climber selected");
        const auto* ladder=owner->ClimberLadders().front();
        Check(ladder->m_dir==direction&&ladder->m_entity==&entity,"facing from actual x/y extent and trace branch");
        Check(ladder->m_bottom.z==0&&ladder->m_top.z==100&&ladder->m_length==100&&!ladder->m_isDangling,"flat endpoints length and dangling threshold");
        Check(ladder->m_bottomArea==FindArea(*owner,1)&&ladder->m_topForwardArea==FindArea(*owner,2),"actual world lookup selects lower and upper areas");
        Check(FindArea(*owner,1)->GetLadderList(LADDER_UP)->size()==1&&FindArea(*owner,2)->GetLadderList(LADDER_DOWN)->size()==4,"all original top-direction registrations retained");
        Require(host.traces.size()==7,"simple ladder uses facing two-clearance and four-top traces");
        const auto& facing=host.traces[0];
        Check(facing.from.z==25&&facing.to.z==75&&((wide&&facing.from.x==0&&facing.from.y==25)||(!wide&&facing.from.x==25&&facing.from.y==0)),"original facing offset25 and endpoint inset25");
        for(const auto& trace:host.traces)Check(trace.flags==1&&trace.ignored==entity.edict(),"ladder trace ignores monsters includes glass and skips its brush");
        const auto count=host.traces.size(),finds=host.finds;
        Check(owner->BuildLadders(error)&&host.traces.size()==count&&host.finds==finds,"successful ladder build is idempotent");
        globals.CheckUnchanged();owner.reset();globals.CheckUnchanged();
    }
    {
        Host host;Files files;auto file=LadderFile();file.areas[1].low.z=file.areas[1].high.z=file.areas[1].northEastZ=file.areas[1].southWestZ=110;
        auto owner=Load(files,file);ConfigError error;host.Ladder(Vector(-2,-20,0),Vector(2,20,100),true);
        Require(owner->BuildLadders(error),"slightly raised top landing");const auto* l=owner->ClimberLadders().front();
        Check(l->m_top.z==110&&l->m_length==100,"original top-area adjustment does not recompute earlier ladder length");
    }
    {
        Host host;Files files;auto file=LadderFile();file.areas[1].low.z=file.areas[1].high.z=file.areas[1].northEastZ=file.areas[1].southWestZ=130;
        auto owner=Load(files,file);ConfigError error;host.Ladder(Vector(-2,-20,73),Vector(2,20,100),true);
        Require(owner->BuildLadders(error),"bottom above surface build");
        Check(owner->ClimberLadders().front()->m_isDangling,"bottom gap strictly above72 is dangling");
    }
    {
        Host host;Files files;auto owner=Load(files,LadderFile());ConfigError error;host.Ladder(Vector(-2,-20,0),Vector(2,20,100));
        Require(owner->BuildLadders(error),"ordinary ladder without targetname");Check(owner->LadderCount()==1&&owner->ClimberLadders().empty(),"climber list filters targetname without hiding ordinary ladder");
    }
}
void LadderCancellationCases()
{
    // All seven engine callback boundaries of the simple successful ladder.
    for(std::size_t cancel=0;cancel<7;++cancel)
    {
        Host host;Files files;auto owner=Load(files,LadderFile());ConfigError error;
        host.Ladder(Vector(-2,-20,0),Vector(2,20,100),true);
        host.onTrace=[&](std::size_t n,TraceResult& result){if(n==cancel){owner->Invalidate();result.pHit=reinterpret_cast<edict_t*>(1);}};
        Check(!owner->BuildLadders(error),"map invalidation during a ladder trace cancels build");
        Check(!owner->World().Ready()&&owner->LadderCount()==0&&owner->ClimberLadders().empty()&&host.traces.size()==cancel+1,"cancel occurs before publishing or another callback");
        Check(!error.message.empty(),"cancelled ladder stage reports reason");
    }
    for(int kind=0;kind<3;++kind)
    {
        Host host;Files files;auto owner=Load(files,LadderFile());ConfigError error;
        auto& e=host.Ladder(Vector(-2,-20,0),Vector(2,20,100),true);
        host.onTrace=[&](std::size_t n,TraceResult& result)
        {
            if(n)return;
            if(kind==0)++e.edict()->serialnumber;
            else if(kind==1)e.edict()->free=TRUE;
            else result.flFraction=std::numeric_limits<float>::quiet_NaN();
        };
        Check(!owner->BuildLadders(error)&&owner->LadderCount()==0&&!owner->World().Ready()&&host.traces.size()==1,"expired serial free edict or nonfinite trace fails before publish");
    }
    {
        Host host;Files files;auto owner=Load(files,LadderFile());ConfigError error;
        host.Ladder(Vector(-2,-20,0),Vector(2,20,100));
        host.onFind=[&](std::size_t){owner->Invalidate();};
        Check(!owner->BuildLadders(error)&&host.traces.empty()&&owner->LadderCount()==0,"enumeration callback invalidation checked before returned edict access");
    }
    {
        Host host;Files files;auto owner=Load(files,LadderFile());ConfigError error;
        host.Ladder(Vector(-2,-20,0),Vector(2,20,100));
        host.Ladder(Vector(48,-20,0),Vector(52,20,100));
        host.onFind=[&](std::size_t n){if(n==1)owner->Invalidate();};
        Check(!owner->BuildLadders(error)&&!owner->World().Ready()&&owner->LadderCount()==1&&host.traces.size()==7,"later cancellation retains published storage for old leases but disables world");
    }
    {
        Host host;Files files;auto owner=Load(files,LadderFile());ConfigError error;std::size_t nested{};
        host.Ladder(Vector(-2,-20,0),Vector(2,20,100));
        host.onTrace=[&](std::size_t,TraceResult&)
        {ConfigError inner;Check(!owner->BuildLadders(inner)&&!inner.message.empty(),"recursive build rejected without corrupting outer traversal");++nested;};
        Check(owner->BuildLadders(error)&&nested==7&&owner->LadderCount()==1,"outer ladder build survives denied recursive request");
    }
    {
        Host host;Files files;auto file=LadderFile();for(auto& area:file.areas){area.low.x+=300;area.high.x+=300;}
        auto owner=Load(files,file);ConfigError error;host.Ladder(Vector(-2,-20,0),Vector(2,20,100));
        host.onTrace=[&](std::size_t n,TraceResult& result){if(n==3){owner->Invalidate();result.pHit=reinterpret_cast<edict_t*>(1);}};
        Check(!owner->BuildLadders(error)&&host.traces.size()==4&&owner->LadderCount()==0,"nested real world ground callback cancels before poison returned hit is inspected");
    }
    {
        Host host;Files files;auto owner=Load(files,LadderFile());auto pin=owner;std::weak_ptr<CCSOLScenarioNavigation> weak=owner;ConfigError error;
        host.Ladder(Vector(-2,-20,0),Vector(2,20,100));
        host.onTrace=[&](std::size_t n,TraceResult&){if(!n){owner->Invalidate();owner.reset();}};
        Check(!pin->BuildLadders(error)&&!weak.expired(),"caller shared lease keeps build storage live after map releases owner");
        pin.reset();Check(weak.expired(),"cancelled build storage released at last caller lease");
    }
}
}
