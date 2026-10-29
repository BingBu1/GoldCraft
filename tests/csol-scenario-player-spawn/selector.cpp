// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace spawn_test
{
namespace
{
void DeadlineAndGroups()
{
    {
        SelectionFixture f; f.engine.globals.time=10;
        f.space.onRefresh=[&]{f.engine.globals.time=20;};
        const auto origin=f.player->pev->origin,angles=f.player->pev->v_angle;
        Check(f.Select()!=nullptr && f.space.refreshes==1 && Bits(f.selector.NextOccupancyRefresh())==Bits(20.0f+0.06666667014360428f),"Select refresh uses post-callback time");
        Check(Equal(origin,f.player->pev->origin) && Equal(angles,f.player->pev->v_angle) && f.engine.setOrigins==0,"selector never places actual player");
        f.space.onRefresh={}; f.engine.globals.time=std::nextafter(f.selector.NextOccupancyRefresh(),0.0f);
        Check(f.selector.UpdateOccupancy() && f.space.refreshes==1,"before deadline no periodic refresh");
        f.engine.globals.time=f.selector.NextOccupancyRefresh();
        Check(f.selector.UpdateOccupancy() && f.space.refreshes==2,"exact deadline refreshes");
        Check(f.Select()!=nullptr && f.space.refreshes==3,"Select forces refresh before next deadline");
        Check(f.selector.BeginMap() && f.selector.NextOccupancyRefresh()==0,"map reset clears shared deadline");
    }
    {
        SelectionFixture f; f.offset=20; f.space.onRefresh=[&]{f.offset=0;};
        auto* result=f.Select();
        Check(result && Equal(result->pev->origin,Vector(0,0,87)) && f.space.refreshes==1 && f.space.origins.empty(),"count snapshot zero still refreshes then original-group +80 fallback");
    }
    {
        SelectionFixture f; f.Points({Point(0),Point(200),Point(400,2),Point(600,2)});
        f.space.onRoom=[&](const Vector&){f.offset=1; f.space.roomResult=f.space.origins.size()==2;};
        auto* result=f.Select();
        Check(result && result->pev->origin.x==600 && f.space.origins.size()==2 && f.space.origins[0].x==0 && f.space.origins[1].x==600,"later attempt rereads group offset and shares cursor");
    }
    {
        SelectionFixture f; f.offset=1; f.Points({Point(10),Point(300,2),Point(500,2)}); f.space.roomResult=false;
        auto* result=f.Select();
        Check(result && Equal(result->pev->origin,Vector(10,0,87)) && f.space.origins.size()==2,"all-blocked fallback uses unadjusted group and skips room check");
    }
    {
        SelectionFixture f; f.offset=std::numeric_limits<std::int32_t>::max();
        f.Points({Point(0,0),Point(100,1)});
        auto* result=f.Select();
        Check(result && Equal(result->pev->origin,Vector(0,0,7)) && f.space.origins.size()==1,"group addition wraps int32 before low-short point key");
    }
    {
        SelectionFixture f; f.Points({Point(200,7)}); auto* result=f.Select();
        Check(result && Equal(result->pev->origin,Vector()) && f.space.origins.empty(),"missing original group returns zero proxy, not group0 or +80");
    }
    {
        SelectionFixture f; f.map.SpawnPoints().Points().Clear();
        for(int i=0;i<129;++i) f.map.SpawnPoints().Points().AppendPlayerPoint(Point(static_cast<float>(i)));
        Check(!f.Select() && f.space.refreshes==1 && f.space.origins.empty() && !f.error.message.empty(),"oversized group refuses only after mandatory refresh");
    }
}
void Players()
{
    for(int kind=0;kind<13;++kind)
    {
        SelectionFixture f; f.Points({Point(0)}); bool occupied=false;
        if(kind==0) { f.player->pev->origin=Vector(0,0,7); }
        else if(kind==1) { f.engine.Raw("player",2); }
        else if(kind==2) { f.engine.Add("info_target",2); }
        else
        {
            auto& other=f.engine.Client(2,Vector(0,0,7)); occupied=true;
            switch(kind)
            {
            case 3: other.edict()->free=TRUE; occupied=false; break;
            case 4: other.pev->health=0; occupied=false; break;
            case 5: other.pev->deadflag=DEAD_DEAD; occupied=false; break;
            case 6: other.pev->flags=std::bit_cast<std::int32_t>(0x80000000u); occupied=false; break;
            case 7: other.pev->flags=std::bit_cast<std::int32_t>(0x80000001u); break;
            case 8: other.pev->origin.x=64; occupied=false; break;
            case 9: other.pev->origin.x=std::nextafter(64.0f,0.0f); break;
            case 10: other.pev->origin.x=std::nextafter(64.0f,INFINITY); occupied=false; break;
            case 11: other.m_iTeam=TERRORIST; other.pev->origin.z=9999; other.pev->netname=0; other.pev->solid=SOLID_NOT; break;
            case 12: other.pev->origin.y=64; occupied=false; break;
            }
        }
        auto* result=f.Select();
        Check(result && result->pev->origin.z==(occupied?87.0f:7.0f) && f.space.origins.size()==static_cast<std::size_t>(!occupied),"real SDK player qualification and strict XY64 boundary");
    }
    {
        SelectionFixture f; f.Points({Point(0),Point(200)}); f.engine.Client(2,Vector(0,0,9999));
        auto* result=f.Select(); Check(result && result->pev->origin.x==200 && f.space.origins.size()==1 && f.space.origins.front().x==200,"player-blocked attempt never invokes MakeRoom");
    }
}
void FailureGates()
{
    for(int fault=0;fault<6;++fault)
    {
        SelectionFixture f;
        if(fault==0) f.space.ready=false;
        if(fault==1) f.space.refreshResult=false;
        if(fault==2) f.space.onRefresh=[&]{f.space.ready=false;};
        if(fault==3) f.space.onRoom=[&](const Vector&){f.space.ready=false;};
        if(fault==4) f.engine.globals.time=NAN;
        if(fault==5) f.space.onRefresh=[&]{f.engine.globals.time=INFINITY;};
        Check(!f.Select() && !f.error.message.empty(),"unavailable or failed refresh cannot silently fallback");
    }
    for(bool room:{false,true}) for(int mutation=0;mutation<5;++mutation)
    {
        SelectionFixture f; f.Points({Point(0)}); f.space.roomResult=room;
        f.space.onRoom=[&](const Vector&)
        {
            auto* proxy=f.map.SpawnPoints().PlayerProxy(); Require(proxy!=nullptr,"live proxy before external callback");
            if(mutation==0) proxy->pev->flags|=FL_KILLME;
            if(mutation==1) ++proxy->edict()->serialnumber;
            if(mutation==2) f.engine.ReplaceWithBase(proxy->edict());
            if(mutation==3) ++f.player->edict()->serialnumber;
            if(mutation==4) Require(f.map.EndMap(),"callback ends map");
        };
        const int created=f.engine.creates;
        Check(!f.Select() && !f.error.message.empty() && f.engine.creates==created && f.space.origins.size()==1,"true and false room callbacks both validate identity before retry/fallback");
    }
    for(int operation=0;operation<3;++operation)
    {
        SelectionFixture f;
        auto invalidate=[&]
        {
            if(operation==0) Require(f.map.EndMap(),"refresh callback ends map");
            if(operation==1) Require(f.map.BeginRound(1,0,0,f.events),"refresh callback changes round");
            if(operation==2)
            {
                Require(f.map.EndMap() && f.map.BeginMap("spawn_test"),"end/reopen same map");
                f.engine.Markers(); Require(f.map.CollectEntities() && f.map.BeginRound(0,0,0,f.events),"same-round ABA fully reactivated");
            }
        };
        f.space.onRefresh=invalidate;
        Check(!f.Select() && !f.error.message.empty() && f.space.origins.empty(),"refresh map/round/ABA change rejected before selection");
    }
    {
        SelectionFixture f; f.space.onRoom=[&](const Vector&)
        {
            csol::scenario::ConfigError nested;
            Check(!f.selector.BeginMap() && !f.selector.UpdateOccupancy() && !f.selector.Select(f.map,*f.player,f.offset,nested),"selector rejects nested operations");
        };
        Check(f.Select()!=nullptr && f.error.message.empty(),"nested selector refusal leaves outer operation valid");
    }
}
void ProxyCallbacks()
{
    for(int stage=0;stage<2;++stage) for(int lifecycle=0;lifecycle<3;++lifecycle)
    {
        SelectionFixture f;
        auto* old=f.map.SpawnPoints().PlayerProxy(); Require(old!=nullptr,"initial proxy exists"); old->pev->flags|=FL_KILLME;
        const auto generation=f.map.SpawnPoints().Generation();
        auto mutate=[&]
        {
            if(lifecycle==0) f.map.SpawnPoints().BeginMap();
            if(lifecycle==1) Require(f.map.EndMap(),"proxy callback ends map");
            if(lifecycle==2)
            {
                Check(f.map.SpawnPoints().SelectPlayerSpawn(2,1,0)==nullptr,"nested proxy selection rejected");
                f.map.SpawnPoints().BeginMap();
            }
        };
        if(stage==0) f.engine.onProxyCreate=mutate; else afterDispatch=[&](edict_t*){mutate();};
        Check(!f.Select() && f.map.SpawnPoints().Generation()!=generation && !f.error.message.empty() && f.space.origins.empty(),"Create/DispatchSpawn clear invalidates proxy before point-table access");
        f.engine.onProxyCreate={}; afterDispatch={};
        Check(f.map.SpawnPoints().Status()!=CSOLPointCollection::Ready,"invalidated operation cannot commit old Ready status");
    }
    {
        SpawnEngine e; e.Markers(); CCSOLSpawnPointManager points; points.BeginMap();
        const auto generation=points.Generation();
        e.onRemove=[&](CBaseEntity&){points.BeginMap();};
        Check(points.CollectMap()!=CSOLPointCollection::Ready && points.Generation()!=generation && points.Points().Players().empty() && points.Points().Monsters().empty(),"collection callback cannot commit old points after clear");
        e.onRemove={};
    }
    {
        SpawnEngine e; e.Markers(); CCSOLSpawnPointManager points;points.BeginMap();
        afterDispatch=[&](edict_t*){points.BeginMap();e.DestroyWorld();};
        Check(points.CollectMap()==CSOLPointCollection::NotCollected&&points.PlayerProxy()==nullptr&&e.edicts.empty(),"generation change after Dispatch must not resolve a freed old edict handle");
        afterDispatch={};e.Markers();
        Check(points.CollectMap()==CSOLPointCollection::Ready&&points.PlayerProxy()!=nullptr,"fresh map entities collect after old array destruction");
    }
}
void Integration()
{
    for(int priority:{0,1,2})
    {
        SelectionFixture f; f.Points({Point(0)});
        CCSOLMonsterPool pool; Require(pool.Initialize(),"integration pool initialization");
        Geometry geometry; geometry.Add(Vector(-100,-100,0),Vector(100,100,0)); Deaths deaths; CCSOLScenarioOccupancy space(pool,deaths);
        Require(space.Initialize(geometry.areas),"integration occupancy initialization");
        csol::scenario::MonsterType type; type.priority=static_cast<std::int8_t>(priority);
        auto* m=pool.Acquire(type,1,f.map.SpawnPoints().Points()); Require(m!=nullptr,"integration borrow");
        m->pev->origin=Vector(0,0,7); m->pev->maxs=Vector(16,16,36);
        Require(space.ApplyPriority(*m) && space.Register(*m),"integration attributes and registration");
        CCSOLScenarioPlayerSpawns selector(space); csol::scenario::ConfigError error;
        auto* result=selector.Select(f.map,*f.player,f.offset,error);
        Check(result && result->pev->origin.z==(priority?87.0f:7.0f) && deaths.requests.size()==static_cast<std::size_t>(!priority),"actual selector uses actual occupancy protection/death callbacks");
        const float deadline=selector.NextOccupancyRefresh(); Require(pool.Shutdown(),"integration external shutdown");
        f.engine.globals.time=deadline-0.01f;
        Check(!selector.UpdateOccupancy() && !selector.Select(f.map,*f.player,f.offset,error),"failed pool cannot hide behind timer or fallback");
    }
}
void ClearServiceFailures()
{
    for(bool requestFailure:{false,true})
    {
        SelectionFixture f; f.Points({Point(0)});
        csol::scenario::MonsterType type;
        CCSOLMonsterPool pool;
        Require(pool.BindEntityAccess(f.map.EntityAccess())&&pool.Initialize(),"failure integration binds actual map access and pool");
        Geometry geometry; geometry.Add(Vector(-100,-100,0),Vector(100,100,0));
        Deaths deaths; deaths.prepareResult=requestFailure; deaths.requestResult=!requestFailure;
        CCSOLScenarioOccupancy space(pool,deaths);
        Require(space.Initialize(geometry.areas),"failure integration initializes actual occupancy grid");
        auto* monster=pool.Acquire(type,1,f.map.SpawnPoints().Points()); Require(monster!=nullptr,"failure integration borrows real pool entity");
        monster->pev->origin=Vector(0,0,7); monster->pev->maxs=Vector(16,16,36);
        Require(space.ApplyPriority(*monster)&&space.Register(*monster),"failure integration installs actual monster attributes");
        const auto lease=pool.Lease(*monster);
        const auto origin=f.player->pev->origin;
        CCSOLScenarioPlayerSpawns selector(space); selector.BindEntityAccess(f.map.EntityAccess());
        csol::scenario::ConfigError error;
        auto* result=selector.Select(f.map,*f.player,f.offset,error);
        const auto* proxy=f.map.SpawnPoints().PlayerProxy();
        Check(!result&&!space.Ready()&&error.message=="scenario spawn occupancy became unavailable"&&
            proxy&&proxy->pev->origin.z==7.0f&&Equal(origin,f.player->pev->origin),
            "actual Select stops on pre-clear or death-service failure without returning the +80 fallback or moving the player");
        Check(deaths.before==std::vector<std::size_t>{monster->Slot()}&&
            deaths.requests.size()==static_cast<std::size_t>(requestFailure)&&pool.Lease(*monster)==lease&&pool.Borrowed(*monster),
            "selector failure preserves the actual victim lease and observes only the expected boundary prefix");
        Require(pool.Shutdown(),"failure integration detaches real pool before local type expires");
    }
}
}
void SelectorCases() { DeadlineAndGroups(); Players(); FailureGates(); ProxyCallbacks(); Integration(); ClearServiceFailures(); }
void ProxyCancellationNegative()
{
    for(int stage=0;stage<2;++stage)
    {
        SelectionFixture f;
        auto& points=f.map.SpawnPoints(); auto* old=points.PlayerProxy(); Require(old!=nullptr,"negative initial proxy"); old->pev->flags|=FL_KILLME;
        const auto creates=f.engine.creates,dispatches=f.engine.dispatches;
        auto cancel=[&]{points.BeginMap();};
        if(stage==0) f.engine.onProxyCreate=cancel; else afterDispatch=[&](edict_t* ed)
        {
            auto* proxy=dynamic_cast<CCSOLPlayerSpawnProxy*>(CBaseEntity::Instance(ed));
            Check(proxy&&!proxy->SpawnAccepted()&&points.PlayerProxy()==nullptr,"post-Dispatch callback precedes manager acceptance");
            cancel();
        };
        Require(!f.Select(),"canceled Select explicitly failed");
        f.engine.onProxyCreate={}; afterDispatch={};
        CCSOLPlayerSpawnProxy* pending=nullptr;
        // Lookup may first return the old FL_KILLME entity. Follow traversal
        // until the new live internal candidate is reached.
        CBaseEntity* scanned=nullptr;
        while((scanned=UTIL_FindEntityByClassname(scanned,CCSOLPlayerSpawnProxy::Classname)))
            if(UTIL_IsValidEntity(scanned->edict())) {pending=dynamic_cast<CCSOLPlayerSpawnProxy*>(scanned);break;}
        Require(pending!=nullptr,"canceled operation leaves a recoverable internal proxy");
        Check(pending->m_iId==-1&&!pending->SpawnAccepted()&&points.PlayerProxy()==nullptr,"canceled proxy is neither map marker nor accepted spawn");
        Check(f.engine.creates==creates+1&&f.engine.dispatches==dispatches+stage,"Create cancellation dispatches zero times; post-Dispatch cancellation once");
        f.engine.Player(707,7,2);
        Require(points.CollectMap()==CSOLPointCollection::Ready,"local cancel can collect fresh markers");
        Check(points.Points().Players().size()==1 && points.PlayerPointCount(2,0)==0 && points.PlayerPointCount(2,7)==1,
            stage==0?"known-negative Create cancellation must not leak default group0 marker":"known-negative DispatchSpawn cancellation must not leak default group0 marker");
        const auto* rawZero=points.Points().FindPlayerPoints(0,0);
        Check(points.PlayerProxy()==pending&&pending->SpawnAccepted()&&f.engine.creates==creates+1&&f.engine.dispatches==dispatches+stage+1,"retry accepts same pending proxy with one dispatch and no new entity");
        std::cout<<"proxy-cancel "<<(stage==0?"Create":"DispatchSpawn")<<" team0group0="<<(rawZero?rawZero->size():0)<<" team2group0="<<points.PlayerPointCount(2,0)<<" group7="<<points.PlayerPointCount(2,7)<<" groups="<<points.Points().Players().size()<<'\n';
    }
    for(bool rejectRetry:{false,true})
    {
        SelectionFixture f;auto& points=f.map.SpawnPoints();
        auto* old=points.PlayerProxy();Require(old!=nullptr,"repeated-cancel initial proxy");old->pev->flags|=FL_KILLME;
        const auto creates=f.engine.creates,dispatches=f.engine.dispatches;
        f.engine.onProxyCreate=[&]{points.BeginMap();};
        Require(!f.Select(),"first Create cancellation returns no proxy");f.engine.onProxyCreate={};
        CCSOLPlayerSpawnProxy* pending=nullptr;CBaseEntity* scanned=nullptr;
        while((scanned=UTIL_FindEntityByClassname(scanned,CCSOLPlayerSpawnProxy::Classname)))
            if(UTIL_IsValidEntity(scanned->edict())){pending=dynamic_cast<CCSOLPlayerSpawnProxy*>(scanned);break;}
        Require(pending!=nullptr,"first cancellation retains internal candidate");
        f.engine.Player(708,8,2);
        if(rejectRetry)f.engine.spawnFailure=Engine::SpawnFailure::Reject;
        else afterDispatch=[&](edict_t*){points.BeginMap();};
        Check(points.CollectMap()!=CSOLPointCollection::Ready&&points.PlayerProxy()==nullptr,"pending retry rejection or second cancellation remains unavailable");
        afterDispatch={};f.engine.spawnFailure=Engine::SpawnFailure::None;
        Check(f.engine.creates==creates+1&&f.engine.dispatches==dispatches+1&&!pending->SpawnAccepted(),"pending first retry uses same entity and never accepts canceled or rejected Spawn");
        Check(static_cast<bool>(pending->pev->flags&FL_KILLME)==rejectRetry,"only rejected pending proxy is marked for removal");
        Require(points.CollectMap()==CSOLPointCollection::Ready,"collection recovers after repeated cancellation or pending rejection");
        auto* completed=points.PlayerProxy();
        Check(completed&&completed->m_iId==-1&&f.engine.creates==creates+1+static_cast<int>(rejectRetry)&&f.engine.dispatches==dispatches+2,"recovery reuses canceled pending or allocates only after rejection");
        Check(rejectRetry?completed!=pending:completed==pending,"twice-canceled proxy identity survives final recovery");
        Check(points.Points().Players().size()==1&&points.PlayerPointCount(2,8)==1&&points.PlayerPointCount(2,0)==0&&!points.Points().FindPlayerPoints(0,0),"all recovery paths collect only legitimate fresh marker");
    }
}
}
