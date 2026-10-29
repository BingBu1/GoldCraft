// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine_fixture.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
int checks{},failures{};
void Check(bool ok,const char* message) {++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
void NamedProxyConstruction()
{
    Engine engine;CCSOLSpawnPointManager manager;engine.Player(41,7);
    edict_t* constructed=nullptr;
    engine.onNamedConstruction=[&](edict_t* ed)
    {
        constructed=ed;
        auto* proxy=dynamic_cast<CCSOLPlayerSpawnProxy*>(CBaseEntity::Instance(ed));
        Check(proxy&&proxy->m_iId==-1&&proxy->m_iTeam==0&&!proxy->SpawnAccepted(),"internal constructor excludes marker before first Spawn or acceptance");
        Check(engine.dispatches==0&&engine.spawnCalls[ed]==0&&manager.PlayerProxy()==nullptr,"construction is not DispatchSpawn or usable proxy");
        Check(UTIL_FindEntityByClassname(nullptr,CCSOLPlayerSpawnProxy::Classname)==nullptr,"runtime named lookup is registered only after engine constructor returns");
        Check(manager.CollectMap()==CSOLPointCollection::NotCollected,"nested constructor cannot finish busy collection");
    };
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"constructed internal proxy becomes usable only after manager dispatch acceptance");
    auto* proxy=dynamic_cast<CCSOLPlayerSpawnProxy*>(manager.PlayerProxy());
    Check(proxy&&proxy->edict()==constructed&&proxy->SpawnAccepted()&&proxy->m_iId==-1&&proxy->m_iTeam==2,"one accepted internal proxy retains reserved id and default team");
    Check(engine.creates==1&&engine.dispatches==1&&engine.spawnCalls[constructed]==1,"CREATE constructs once and manager Dispatch performs the only Spawn");
    Check(engine.namedRegistrations[constructed]==CCSOLPlayerSpawnProxy::Classname&&
        UTIL_FindEntityByClassname(nullptr,CCSOLPlayerSpawnProxy::Classname)==proxy&&
        UTIL_FindEntityByClassname(nullptr,"playerspawn")!=proxy,"internal classname and registered lookup remain consistent");
    const auto dispatches=engine.dispatches;
    Check(manager.SelectPlayerSpawn(2,7,0)==proxy&&engine.dispatches==dispatches,"completed proxy reuse does not dispatch twice");
}
void PendingInternalProxy()
{
    {
        Engine engine;CCSOLSpawnPointManager manager;engine.Player(41,7);
        auto& pending=static_cast<CCSOLPlayerSpawnProxy&>(engine.Add(CCSOLPlayerSpawnProxy::Classname,42));
        Check(pending.m_iId==-1&&!pending.SpawnAccepted()&&engine.spawnCalls[pending.edict()]==1,"direct entity Spawn does not imply Dispatch acceptance");
        Check(manager.CollectMap()==CSOLPointCollection::Ready&&manager.PlayerProxy()==&pending&&pending.SpawnAccepted(),"manager finishes unconfirmed existing internal proxy");
        Check(engine.creates==0&&engine.dispatches==1&&engine.spawnCalls[pending.edict()]==2,"unconfirmed existing proxy requires dispatch without allocating replacement");
    }
    {
        Engine engine;CCSOLSpawnPointManager manager;engine.Player(41,7);
        auto& renamed=static_cast<CCSOLPlayerSpawnProxy&>(engine.Construct(CCSOLPlayerSpawnProxy::Classname,42));
        renamed.pev->classname=Engine::AllocString("playerspawn");
        Check(manager.CollectMap()==CSOLPointCollection::Ready&&manager.PlayerProxy()!=&renamed,"internal type renamed as legacy cannot bypass Dispatch acceptance");
        Check(!renamed.SpawnAccepted()&&!(renamed.pev->flags&FL_KILLME)&&engine.creates==1&&engine.dispatches==1,"renamed foreign candidate is left untouched while valid internal proxy is created");
    }
}
void RejectProxy()
{
    Engine engine;CCSOLSpawnPointManager manager;
    auto& player=engine.Player(41);auto& monster=engine.Monster(42);
    engine.spawnFailure=Engine::SpawnFailure::Reject;
    Check(manager.CollectMap()==CSOLPointCollection::ProxyUnavailable,"DispatchSpawn rejection must prevent successful collection");
    Check(manager.PlayerProxy()==nullptr,"DispatchSpawn-rejected proxy must not be usable");
    Check(manager.Points().Players().empty()&&manager.Points().Monsters().empty(),"rejected proxy must not commit map points");
    Check(!(player.pev->flags&FL_KILLME)&&!(monster.pev->flags&FL_KILLME),"rejected proxy must not consume real map markers");
    Check(engine.creates==1&&engine.dispatches==1,"negative reaches real manager create and dispatch branch once");
    auto* rejected=dynamic_cast<CCSOLPlayerSpawnProxy*>(CBaseEntity::Instance(engine.edicts.back().get()));
    Check(rejected&&!rejected->SpawnAccepted()&&rejected->m_iId==-1&&engine.spawnCalls[rejected->edict()]==1,"Spawn followed by rejection never publishes acceptance or performs premature Spawn");
    Check(engine.removes==1&&engine.removed[0]>=10000,"live rejected proxy is removed without touching map markers");
    engine.spawnFailure=Engine::SpawnFailure::None;
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"collection retries after transient spawn refusal");
    Check(manager.PlayerPointCount(2,0)==1&&manager.Points().Monsters().size()==1,"rejected default-id proxy is not collected as a player marker on retry");
    Check(engine.creates==2&&engine.dispatches==2,"retry creates one fresh proxy");
}
bool WasRemoved(const Engine& engine,int index)
{ return std::find(engine.removed.begin(),engine.removed.end(),index)!=engine.removed.end(); }
std::size_t Position(const Engine& engine,const std::string& call)
{ return std::find(engine.calls.begin(),engine.calls.end(),call)-engine.calls.begin(); }
void ReadyCollection()
{
    Engine engine;CCSOLSpawnPointManager manager;
    auto& first=engine.Player(81,7,2);auto& second=engine.Player(29,7,2);
    auto& alias=engine.Player(40,65535,2);
    auto& proxy=engine.Player(7,-1,2);auto& spareProxy=engine.Player(8,-1,1);
    auto& oldMonster=engine.Monster(21,30,2);auto& lastMonster=engine.Monster(12,30,8);
    auto& otherMonster=engine.Monster(67,-1,4,0);
    auto& fallback=engine.Add("info_player_start",200);
    auto& deadPlayer=engine.Player(55,9);deadPlayer.pev->flags|=FL_KILLME;
    auto& freeMonster=engine.Monster(56,9);freeMonster.edict()->free=TRUE;
    const auto firstOrigin=first.pev->origin,secondOrigin=second.pev->origin,lastOrigin=lastMonster.pev->origin;
    Check(manager.Status()==CSOLPointCollection::NotCollected&&manager.PlayerProxy()==nullptr,"new manager has no collected state or cached proxy");
    engine.onRemove=[&](CBaseEntity& e)
    {
        if(e.entindex()==21) lastMonster.pev->origin=Vector(999,999,999);
        if(e.entindex()==81) second.pev->origin=Vector(888,888,888);
    };
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"existing proxy and valid BSP players produce ready status");
    Check(engine.creates==0&&engine.dispatches==0&&manager.PlayerProxy()==&proxy,"first existing full-id-minus-one proxy reused without spawn");
    Check(!(proxy.pev->flags&FL_KILLME)&&!(spareProxy.pev->flags&FL_KILLME),"all full-id-minus-one proxies survive marker collection");
    Check(manager.PlayerPointCount(2,7)==2&&manager.PlayerPointCount(2,-1)==1,"duplicate players append and65535 alias stays collectible");
    const auto* points=manager.Points().FindPlayerPoints(2,7);
    Check(points&&(*points)[0].entityIndex==81&&(*points)[1].entityIndex==29,"player traversal order is retained, not sorted by fixture index");
    Check(points&&(*points)[0].origin==std::array<float,3>{firstOrigin.x,firstOrigin.y,firstOrigin.z}&&
        (*points)[1].origin==std::array<float,3>{secondOrigin.x,secondOrigin.y,secondOrigin.z},"all player snapshots captured before remove callback mutates next point");
    const auto* monster=manager.Points().FindMonsterPoint(1,30);
    Check(monster&&monster->point.entityIndex==12&&monster->maxSpawn==8&&monster->consumed==0,"duplicate monster final record wins with reset quota");
    Check(monster&&monster->point.origin==std::array<float,3>{lastOrigin.x,lastOrigin.y,lastOrigin.z},"all monster snapshots captured before deletion callback changes winner");
    Check(manager.Points().Monsters().size()==2&&manager.Points().FindMonsterPoint(0,-1),"monster explicit team0 and negative id retained");
    Check(!WasRemoved(engine,200)&&!(fallback.pev->flags&FL_KILLME),"valid playerspawn group prevents info_player_start fallback consumption");
    Check(!WasRemoved(engine,55)&&!WasRemoved(engine,56)&&!manager.Points().FindPlayerPoints(2,9)&&!manager.Points().FindMonsterPoint(1,9),"dead or free markers are skipped");
    Check(engine.removed==std::vector<int>{21,12,67,81,29,40},"monster class consumed first then player class, each in traversal order");
    Check(Position(engine,"end:zombiespawn")<Position(engine,"remove:21")&&Position(engine,"end:playerspawn")<Position(engine,"remove:81"),"each entity class search completes before first marker removal");
    Check(manager.Points().ConsumePointQuota(1,30),"saved monster quota can be consumed");
    const int searches=engine.searches,removes=engine.removes,creates=engine.creates;
    Check(manager.CollectMap()==CSOLPointCollection::Ready&&engine.searches==searches&&engine.removes==removes&&engine.creates==creates,"ready recollection does not rescan or remove markers or create proxy");
    Check(manager.Points().FindMonsterPoint(1,30)->consumed==1,"ready recollection does not reset round quota implicitly");
    manager.Points().ResetMonsterQuotas();
    Check(manager.Points().FindMonsterPoint(1,30)->consumed==0&&manager.PlayerPointCount(2,7)==2,"explicit round quota reset keeps saved map points");
    (void)alias;(void)oldMonster;(void)otherMonster;
}
void HandlesDuringRemoval()
{
    Engine engine;CCSOLSpawnPointManager manager;
    engine.Player(1,-1);engine.Player(2);
    engine.Monster(10,10);auto& recycled=engine.Monster(11,11);auto& freed=engine.Monster(12,12);
    auto& killed=engine.Monster(13,13);auto& retyped=engine.Monster(14,14);
    engine.onRemove=[&](CBaseEntity& e)
    {
        if(e.entindex()!=10)return;
        ++recycled.edict()->serialnumber;freed.edict()->free=TRUE;killed.pev->flags|=FL_KILLME;
        retyped.pev->origin=Vector(123,123,123);
    };
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"callback invalidation does not prevent map collection");
    Check(manager.Points().Monsters().size()==5,"pre-removal snapshots survive later serial/free/kill changes");
    Check(engine.removed==std::vector<int>{10,14,2},"serial-reused free and killmarked later handles are skipped without raw-pointer deletion");
    Check(manager.Points().FindMonsterPoint(1,14)->point.origin==std::array<float,3>{14,15,16},"remaining live marker snapshot remains a value after mutation");
}
void FallbackAndMissing()
{
    {
        Engine engine;CCSOLSpawnPointManager manager;auto& proxy=engine.Player(1,-1);
        auto& a=engine.Add("info_player_start",20);auto& b=engine.Add("info_player_start",6);
        a.pev->team=1;b.pev->team=3;engine.Monster(70,4,3);
        auto& dead=engine.Add("info_player_start",45);dead.pev->flags|=FL_KILLME;
        Check(manager.CollectMap()==CSOLPointCollection::Ready,"info_player_start used only without normal playerspawn");
        Check(manager.PlayerPointCount(2,0)==2&&manager.PlayerPointCount(1,0)==0,"fallback collects every live start as team2 id0 regardless of native team");
        const auto* points=manager.Points().FindPlayerPoints(2,0);
        Check(points&&(*points)[0].entityIndex==20&&(*points)[1].entityIndex==6&&(*points)[1].angles==std::array<float,3>{9,10,11},"fallback retains all starts in search order with angles");
        Check(engine.removed==std::vector<int>{70,20,6}&&manager.PlayerProxy()==&proxy,"fallback markers removed after monster markers and proxy retained");
        Check(Position(engine,"end:info_player_start")<Position(engine,"remove:20"),"fallback completes scan before removals");
    }
    {
        Engine engine;CCSOLSpawnPointManager manager;engine.Monster(31,6,9);
        Check(manager.CollectMap()==CSOLPointCollection::NeedsSpawnFile,"no player or fallback points requests explicit SP fallback stage");
        Check(manager.Points().Players().empty()&&manager.Points().Monsters().empty(),"SP fallback state clears both point tables including collected monsters");
        Check(WasRemoved(engine,31)&&manager.PlayerProxy()&&manager.PlayerProxy()->m_iId==-1,"monster markers consumed but persistent proxy remains for missing-SP stage");
        const int searches=engine.searches,removes=engine.removes;
        engine.Player(50);Check(manager.CollectMap()==CSOLPointCollection::NeedsSpawnFile&&engine.searches==searches&&engine.removes==removes,"missing-SP state is stable until a map lifecycle reset");
        Check(manager.SelectPlayerSpawn(2,0,8)==nullptr,"unready map cannot select proxy");
    }
}
void ProxyFailures()
{
    for(auto failure:{Engine::CreateFailure::Null,Engine::CreateFailure::PrivateMissing})
    {
        Engine engine;CCSOLSpawnPointManager manager;engine.Player(20);engine.Monster(21);
        engine.createFailure=failure;
        Check(manager.CollectMap()==CSOLPointCollection::ProxyUnavailable&&manager.PlayerProxy()==nullptr,"failed engine creation/private allocation reports missing proxy");
        Check(!WasRemoved(engine,20)&&!WasRemoved(engine,21)&&manager.Points().Players().empty(),"create failure leaves map markers and empty snapshots");
    }
    for(auto failure:{Engine::SpawnFailure::Kill,Engine::SpawnFailure::Free,Engine::SpawnFailure::Recycle,Engine::SpawnFailure::ChangeClass,Engine::SpawnFailure::PrivateMissing})
    {
        Engine engine;CCSOLSpawnPointManager manager;engine.Player(20);engine.Monster(21);
        engine.spawnFailure=failure;
        Check(manager.CollectMap()==CSOLPointCollection::ProxyUnavailable&&manager.PlayerProxy()==nullptr,"removed recycled retyped or missing-private proxy rejected after Spawn");
        Check(!WasRemoved(engine,20)&&!WasRemoved(engine,21)&&manager.Points().Players().empty(),"post-spawn proxy invalidation cannot consume real markers");
    }
}
void SelectAndLifecycle()
{
    Engine engine;CCSOLSpawnPointManager manager;
    Check(manager.SelectPlayerSpawn(2,0,8)==nullptr&&engine.creates==0,"selection before collection does not create a proxy");
    auto& proxy=engine.Player(900,-1);
    engine.Player(10,7,2);engine.Player(11,7,2);engine.Player(12,7,2);
    engine.Player(20,8,1);engine.Player(21,8,1);
    engine.Player(30,9,3);engine.Player(31,9,3);engine.Player(32,9,2);
    engine.Monster(40,0,3);
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"selection fixture ready");
    Check(manager.PlayerPointCount(0,7)==0&&manager.PlayerPointCount(3,9)==2&&manager.PlayerPointCount(-1,9)==0,"Count treats team0 specially and does not normalize other teams to2");
    Check(manager.PlayerPointCount(2,65543)==3&&manager.PlayerPointCount(258,7)==3,"Count uses original narrow key aliases");
    proxy.pev->velocity=Vector(8,9,10);proxy.pev->v_angle=Vector(40,50,60);proxy.pev->fixangle=77;proxy.pev->solid=91;proxy.pev->effects=123;
    proxy.m_iTeam=19;const auto before=*proxy.pev;
    const auto groups=manager.Points().Players().size();
    auto select=[&](int team,int id,float z,int expected)
    {
        auto* selected=manager.SelectPlayerSpawn(team,id,z);
        Check(selected==&proxy,"selection returns persistent existing proxy");
        if(!selected)return;
        Check(selected->pev->origin==Vector(float(expected),float(expected+1),float(expected+2)+z)&&selected->pev->angles==Vector(float(expected+3),float(expected+4),float(expected+5)),"selection cursor and original point transform match expected sequence");
    };
    select(2,7,4,10); // cursor1
    Check(manager.SelectPlayerSpawn(0,7,999)==&proxy&&proxy.pev->origin==g_vecZero&&proxy.pev->angles==g_vecZero,"team0 zeroes proxy and ignores requested Z");
    select(1,8,-6,21); // shared cursor1, group2 ->0
    select(2,7,0,10); // shared0 ->1
    Check(manager.SelectPlayerSpawn(2,999,7)==&proxy&&proxy.pev->origin==g_vecZero&&proxy.pev->angles==g_vecZero,"missing group zeroes proxy and ignores requested Z");
    select(2,7,0,11); // missing did not advance
    Check(manager.Points().Players().size()==groups,"queries do not create empty lookup buckets");
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"recollection during selection stays ready");
    manager.Points().ResetMonsterQuotas();select(2,7,0,12); // round does not reset cursor2
    select(3,9,0,32); // non1/2→2 not explicit team3 group
    select(-1,9,0,32);select(256,7,0,10);select(2,65543,0,11);
    Check(proxy.pev->velocity==before.velocity&&proxy.pev->v_angle==before.v_angle&&proxy.pev->fixangle==before.fixangle&&proxy.pev->solid==before.solid&&proxy.pev->effects==before.effects&&proxy.m_iId==-1&&proxy.m_iTeam==19&&engine.setOrigins==0,"selection writes only origin and angles, retaining identity and unrelated entvars with no SetOrigin");
    // A stale serial must invalidate the handle; do not destroy the edict yet.
    ++proxy.edict()->serialnumber;proxy.m_iId=5;
    Check(manager.PlayerProxy()==nullptr,"serial changed cached proxy invalidates safely");
    auto* replacement=manager.SelectPlayerSpawn(2,7,0);
    Check(replacement&&replacement!=&proxy&&replacement->m_iId==-1&&engine.creates==1,"selection replaces invalid proxy without erasing saved points");
    Check(replacement&&replacement->pev->origin==Vector(12,13,14),"proxy replacement preserves selection cursor");
    // Simulate map memory being released before BeginMap; it must only forget handle.
    engine.ClearEdicts();const int searches=engine.searches,removes=engine.removes;
    manager.BeginMap();
    Check(manager.Status()==CSOLPointCollection::NotCollected&&manager.PlayerProxy()==nullptr&&manager.Points().Players().empty()&&manager.Points().Monsters().empty(),"BeginMap clears snapshots status and handle after old edicts are freed");
    Check(engine.searches==searches&&engine.removes==removes,"BeginMap never searches or removes previous map edicts");
    engine.Player(70,7);engine.Player(71,7);
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"new map collection succeeds with new proxy");
    auto* fresh=manager.SelectPlayerSpawn(2,7,1);
    Check(fresh&&fresh->pev->origin==Vector(70,71,73),"BeginMap resets shared cursor to first point");
    manager.BeginMap();manager.BeginMap();
    Check(manager.PlayerProxy()==nullptr&&manager.Status()==CSOLPointCollection::NotCollected,"BeginMap is repeatable without deleting live map entities");
}
void OversizedSelection()
{
    Engine engine;CCSOLSpawnPointManager manager;auto& proxy=engine.Player(1,-1);
    engine.Player(2);Check(manager.CollectMap()==CSOLPointCollection::Ready,"oversized selection fixture ready");
    for(int i=0;i<129;++i) manager.Points().AppendPlayerPoint({{float(i),float(i+1),float(i+2)},{1,2,3},2,8,std::size_t(i+100)});
    const auto origin=proxy.pev->origin;
    Check(manager.PlayerPointCount(2,8)==129,"count reports complete oversized group");
    Check(manager.SelectPlayerSpawn(2,8,0)==nullptr&&proxy.pev->origin==origin,"129 points fail safely before original signed-byte unsafe domain");
    for(int i=0;i<128;++i) manager.Points().AppendPlayerPoint({{float(i),0,0},{},2,9,std::size_t(i+300)});
    for(int i=0;i<130;++i)
    {
        auto* p=manager.SelectPlayerSpawn(2,9,0);
        Check(p&&p->pev->origin.x==float(i%128),"128-point maximum valid group cycles without signed index failure");
    }
}
void ForeignClassnames()
{
    {
        Engine engine;CCSOLSpawnPointManager manager;
        auto& fakePlayer=engine.Add("info_player_start",60);fakePlayer.pev->classname=Engine::AllocString("playerspawn");
        auto& fakeMonster=engine.Add("info_player_start",61);fakeMonster.pev->classname=Engine::AllocString("zombiespawn");
        engine.Player(62,7);engine.Monster(63,8);
        Check(manager.CollectMap()==CSOLPointCollection::Ready,"foreign same-classname bases do not prevent valid collection");
        Check(engine.creates==1&&manager.PlayerProxy()&&manager.PlayerProxy()->m_iId==-1,"foreign playerspawn base is never reused as proxy");
        Check(manager.PlayerPointCount(2,7)==1&&manager.Points().Players().size()==1&&manager.Points().Monsters().size()==1,"foreign same-classname bases are excluded from both snapshot tables");
        Check(!WasRemoved(engine,60)&&!WasRemoved(engine,61)&&engine.removed==std::vector<int>{63,62},"foreign bases remain untouched while true markers are consumed");
    }
    {
        Engine engine;CCSOLSpawnPointManager manager;engine.Player(70);engine.Monster(71);
        engine.spawnFailure=Engine::SpawnFailure::ReplaceWithBase;
        Check(manager.CollectMap()==CSOLPointCollection::ProxyUnavailable&&manager.PlayerProxy()==nullptr,"Spawn callback private-object replacement with same classname is rejected");
        Check(!WasRemoved(engine,70)&&!WasRemoved(engine,71)&&manager.Points().Players().empty(),"private-object replacement cannot consume map markers");
        engine.spawnFailure=Engine::SpawnFailure::None;
        Check(manager.CollectMap()==CSOLPointCollection::Ready&&manager.PlayerPointCount(2,0)==1,"retry ignores former same-classname base and collects only real points");
        Check(engine.creates==2&&!WasRemoved(engine,10001),"replaced foreign object remains owned by its callback and is not removed");
    }
    {
        Engine engine;CCSOLSpawnPointManager manager;auto& proxy=engine.Player(80,-1);engine.Player(81,2);engine.Player(82,2);
        Check(manager.CollectMap()==CSOLPointCollection::Ready,"cached-proxy replacement fixture ready");
        Check(manager.SelectPlayerSpawn(2,2,0)==&proxy,"true cached proxy initially selects first point");
        auto* ed=proxy.edict();const int serial=ed->serialnumber;engine.ReplaceWithBase(ed);
        Check(ed->serialnumber==serial&&manager.PlayerProxy()==nullptr,"same-edict same-serial replacement cannot bypass cached proxy RTTI check");
        auto* fresh=manager.SelectPlayerSpawn(2,2,0);
        Check(fresh&&fresh->edict()!=ed&&fresh->pev->origin==Vector(82,83,84),"cached foreign replacement is repaired with new true proxy preserving cursor");
        Check(!WasRemoved(engine,80),"foreign replacement is not deleted during proxy repair");
    }
}
class Reader
{
    std::ifstream file;
public:
    explicit Reader(const char* path):file(path,std::ios::binary) {if(!file)throw std::runtime_error("cannot open independent BSP fixture");}
    std::uint32_t U32()
    {
        unsigned char bytes[4];file.read(reinterpret_cast<char*>(bytes),4);if(!file)throw std::runtime_error("truncated fixture");
        return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
    }
    int I32(){return std::bit_cast<std::int32_t>(U32());}
    float Float(){return std::bit_cast<float>(U32());}
    std::string String(){std::string value(U32(),'\0');file.read(value.data(),value.size());if(!file)throw std::runtime_error("truncated string");return value;}
    bool Done(){return file.peek()==std::char_traits<char>::eof();}
};
bool Same(const csol::scenario::SpawnPoint& a,const csol::scenario::SpawnPoint& b)
{return a.origin==b.origin&&a.angles==b.angles&&a.id==b.id&&a.team==b.team&&a.entityIndex==b.entityIndex;}
void Actual(const char* path)
{
    using namespace csol::scenario;
    Reader input(path);Check(input.U32()==0x544e5053u,"actual independentSPNT signature");const auto count=input.U32();
    Check(count==184,"actual184 records");Engine engine;CCSOLSpawnPointManager manager;
    std::map<std::int32_t,std::vector<SpawnPoint>> players;
    std::map<std::int32_t,MonsterPoint> monsters;
    std::vector<int> expectedRemovals,playerRemovals;
    for(std::uint32_t i=0;i<count;++i)
    {
        SpawnPoint expected;expected.entityIndex=input.U32();const auto kind=input.U32();
        expected.id=input.I32();expected.team=input.I32();const int maximum=input.I32();
        for(auto& value:expected.origin)value=input.Float();for(auto& value:expected.angles)value=input.Float();
        const auto key=input.I32();const auto keys=input.U32();
        auto& entity=engine.Add(kind==0?"playerspawn":"zombiespawn",static_cast<int>(expected.entityIndex));
        // Add calls Spawn for normal fixtures; reset defaults before real BSP KV replay.
        if(kind==0) {auto& point=static_cast<CCSOLPlayerSpawn&>(entity);point.m_iId=0;point.m_iTeam=0;}
        else {auto& point=static_cast<CCSOLMonsterSpawn&>(entity);point.m_iId=0;point.m_iTeam=3;point.m_iMaxSpawn=0;}
        entity.pev->origin=Vector(expected.origin[0],expected.origin[1],expected.origin[2]);
        entity.pev->angles=Vector(expected.angles[0],expected.angles[1],expected.angles[2]);
        for(std::uint32_t j=0;j<keys;++j)
        {
            const auto name=input.String(),value=input.String();
            KeyValueData kv{nullptr,const_cast<char*>(name.c_str()),const_cast<char*>(value.c_str()),777};
            entity.KeyValue(&kv);
            Check(kv.fHandled==(name=="id"||name=="team2"||(kind==1&&name=="maxspawn")),"actual ordered KV handling remains correct in manager fixture");
        }
        entity.Spawn();entity.Activate();
        if(kind==0) {players[key].push_back(expected);playerRemovals.push_back(static_cast<int>(expected.entityIndex));}
        else {monsters[key]={expected,maximum,0};expectedRemovals.push_back(static_cast<int>(expected.entityIndex));}
    }
    Check(input.Done(),"actual independent fixture fully consumed");
    expectedRemovals.insert(expectedRemovals.end(),playerRemovals.begin(),playerRemovals.end());
    Check(manager.CollectMap()==CSOLPointCollection::Ready,"actual BSP collection ready with generated proxy");
    Check(engine.creates==1&&engine.dispatches==1&&manager.PlayerProxy()&&manager.PlayerProxy()->m_iId==-1,"actual BSP creates one persistent proxy");
    Check(manager.Points().Players().size()==2&&manager.Points().Monsters().size()==164,"actual BSP snapshots2 player groups and164 monster groups");
    for(const auto& [key,expected]:monsters)
    {
        const auto found=manager.Points().Monsters().find(key);Check(found!=manager.Points().Monsters().end(),"actual independent monster key found");
        if(found==manager.Points().Monsters().end())continue;
        Check(Same(found->second.point,expected.point)&&found->second.maxSpawn==expected.maxSpawn&&found->second.consumed==0,"actual monster snapshot complete and last duplicate wins");
    }
    std::uint8_t cursor{};
    for(const auto& [key,expected]:players)
    {
        const auto found=manager.Points().Players().find(key);Check(found!=manager.Points().Players().end(),"actual independent player key found");
        if(found==manager.Points().Players().end())continue;
        Check(found->second.size()==expected.size(),"actual player group preserves all entries");
        for(std::size_t i=0;i<std::min(found->second.size(),expected.size());++i)Check(Same(found->second[i],expected[i]),"actual player ordered full snapshots match independent oracle");
        for(std::size_t i=0;i<expected.size()+2;++i)
        {
            const auto index=static_cast<std::size_t>(cursor)%expected.size();const auto& point=expected[index];
            auto* proxy=manager.SelectPlayerSpawn(point.team,point.id,2.5f);
            Check(proxy&&proxy->pev->origin==Vector(point.origin[0],point.origin[1],point.origin[2]+2.5f)&&proxy->pev->angles==Vector(point.angles[0],point.angles[1],point.angles[2]),"actual group selection and wrap match independent ordered snapshots");
            cursor=static_cast<std::uint8_t>((index+1)%expected.size());
        }
    }
    Check(engine.removed==expectedRemovals&&engine.removes==184,"actual184 markers removed in per-class traversal order after collecting");
    const int searches=engine.searches,removes=engine.removes;
    Check(manager.CollectMap()==CSOLPointCollection::Ready&&engine.searches==searches&&engine.removes==removes,"actual second collection does not erase snapshots or repeat removals");
    Check(manager.Points().FindMonsterPoint(1,30)&&manager.Points().FindMonsterPoint(1,30)->point.entityIndex==202,"actual duplicate id30 winner has controlledBSP identity202");
    std::cout<<"actual zs_deadend manager:184 records,19 player markers/2 groups,165 monster markers/164 groups; BSP indices are mock identities only\n";
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==1){NamedProxyConstruction();PendingInternalProxy();RejectProxy();ReadyCollection();HandlesDuringRemoval();FallbackAndMissing();ProxyFailures();SelectAndLifecycle();OversizedSelection();ForeignClassnames();}
        else if(argc==3&&std::string_view(argv[1])=="--actual")Actual(argv[2]);
        else throw std::runtime_error("usage: point manager tests [--actual independent-points.bin]");
    }
    catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION: "<<e.what()<<'\n';}
    std::cout<<"point manager checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
