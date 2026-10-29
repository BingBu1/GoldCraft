// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace spawn_test
{
namespace
{
struct Reader
{
    std::ifstream file;
    explicit Reader(const char* path):file(path,std::ios::binary) { if(!file) throw std::runtime_error("missing frozen fixture"); }
    std::uint32_t U32()
    {
        unsigned char b[4]; file.read(reinterpret_cast<char*>(b),4); if(!file) throw std::runtime_error("truncated fixture");
        return b[0]|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);
    }
    int I32() { return std::bit_cast<std::int32_t>(U32()); }
    float F32() { return std::bit_cast<float>(U32()); }
    std::string String()
    { auto n=U32(); if(n>65536) throw std::runtime_error("large fixture string"); std::string result(n,'\0'); file.read(result.data(),n); if(!file) throw std::runtime_error("short string"); return result; }
    bool Done() { return file.peek()==std::char_traits<char>::eof(); }
};
std::string Read(const std::filesystem::path& path)
{ std::ifstream input(path,std::ios::binary); if(!input) throw std::runtime_error("missing actual resource"); return {std::istreambuf_iterator<char>(input),{}}; }
void Keys(Reader& input,CBaseEntity& entity,unsigned count)
{
    for(unsigned i=0;i<count;++i)
    {
        const auto name=input.String(),value=input.String();
        KeyValueData kv{nullptr,const_cast<char*>(name.c_str()),const_cast<char*>(value.c_str()),0}; entity.KeyValue(&kv);
    }
}
}
void ActualCases(const char* common,const char* pointPath,const char* volumePath,const char* navPath)
{
    SpawnEngine e; e.files.clear();
    for(const auto* name:{"resource/maplist.csv","maps/zbs_wave_zs_deadend.cso","maps/zbs_zs_deadend.cso","maps/ZBS_ZT_zs_deadend.cso","maps/ZBS_TRG_zs_deadend.cso"})
        e.files[name]=Read(std::filesystem::path(common)/name);
    std::map<int,std::vector<csol::scenario::SpawnPoint>> expected;
    Reader points(pointPath); Require(points.U32()==0x544e5053 && points.U32()==184,"read original 184 BSP marker fixture");
    for(unsigned i=0;i<184;++i)
    {
        csol::scenario::SpawnPoint p; p.entityIndex=points.U32(); const auto kind=points.U32(); p.id=points.I32(); p.team=points.I32(); points.I32();
        for(auto& value:p.origin) value=points.F32(); for(auto& value:p.angles) value=points.F32(); const auto key=points.I32(); const auto count=points.U32();
        auto& entity=e.Add(kind?"zombiespawn":"playerspawn",static_cast<int>(p.entityIndex));
        if(kind) { auto& m=static_cast<CCSOLMonsterSpawn&>(entity); m.m_iId=0; m.m_iTeam=3; m.m_iMaxSpawn=0; }
        else { auto& s=static_cast<CCSOLPlayerSpawn&>(entity); s.m_iId=0; s.m_iTeam=0; }
        entity.pev->origin=Vector(p.origin[0],p.origin[1],p.origin[2]); entity.pev->angles=Vector(p.angles[0],p.angles[1],p.angles[2]);
        Keys(points,entity,count); entity.Spawn(); if(!kind) expected[key].push_back(p);
    }
    Require(points.Done(),"BSP point bytes consumed exactly");
    Reader volumes(volumePath); Require(volumes.U32()==0x56475254 && volumes.U32()==30,"read actual trigger geometry fixture");
    for(unsigned i=0;i<30;++i)
    {
        const int index=static_cast<int>(volumes.U32()),id=volumes.I32(); volumes.I32(); std::array<float,18> values;
        for(auto& value:values) value=volumes.F32(); volumes.I32(); const auto count=volumes.U32();
        auto& entity=e.Trigger(index,0,Vector(values[6],values[7],values[8]),Vector(values[9],values[10],values[11]));
        Keys(volumes,entity,count); Check(entity.m_iId==id,"actual trigger key applied");
    }
    Require(volumes.Done(),"BSP volume bytes consumed exactly");
    Reader nav(navPath); Require(nav.U32()==0x4756414e && nav.U32()==1247,"actual NAV 1247 geometry records"); Geometry geometry;
    for(int i=0;i<1247;++i)
    {
        nav.U32(); std::array<float,8> v; for(auto& value:v) value=nav.F32();
        geometry.Add(Vector(v[0],v[1],v[2]),Vector(v[3],v[4],v[5]),v[6],v[7]);
        const auto* extent=geometry.owned.back()->GetExtent();
        Check(Equal(extent->lo,Vector(v[0],v[1],v[2])) && Equal(extent->hi,Vector(v[3],v[4],v[5])),"real SDK CNavArea preserves source corners");
    }
    Require(nav.Done(),"NAV geometry bytes consumed exactly");
    CCSOLScenarioMap map; Events events;
    Require(map.BeginMap("zs_deadend") && map.CollectEntities(),"actual catalog/four resources and SDK entities collected");
    Check(map.Tables()->rounds.rows.size()==2 && map.SpawnPoints().Points().Players().size()==2,"actual two-round two-player-group source");
    auto& player=e.Client(1,Vector(99999,99999,99999));
    CCSOLMonsterPool pool; Require(pool.Initialize(),"actual combination pool"); Deaths deaths; CCSOLScenarioOccupancy space(pool,deaths);
    Require(space.Initialize(geometry.areas),"actual NAV extents initialize production occupancy");
    CCSOLScenarioPlayerSpawns selector(space); int offset=0; csol::scenario::ConfigError error;
    std::size_t cursor=0;
    for(std::size_t round=0;round<2;++round)
    {
        Require(map.BeginRound(round,0,0,events),"actual round start");
        const int group=round==0?1:2; const auto& independent=expected.at((2<<16)|group);
        for(int pass=0;pass<2;++pass)
        {
            const auto index=cursor%independent.size(); const auto& wanted=independent[index]; cursor=(index+1)%independent.size();
            auto* result=selector.Select(map,player,offset,error);
            Check(result && Equal(result->pev->origin,Vector(wanted.origin[0],wanted.origin[1],wanted.origin[2])) &&
                Equal(result->pev->angles,Vector(wanted.angles[0],wanted.angles[1],wanted.angles[2])),"actual unblocked spawn coordinate and angle match independent BSP order");
        }
        Space blocked; blocked.roomResult=false; CCSOLScenarioPlayerSpawns fallback(blocked);
        cursor=(cursor+independent.size())%independent.size(); const auto& wanted=independent[cursor]; cursor=(cursor+1)%independent.size();
        auto* result=fallback.Select(map,player,offset,error);
        Check(result && Equal(result->pev->origin,Vector(wanted.origin[0],wanted.origin[1],wanted.origin[2]+80.0f)) && blocked.origins.size()==independent.size(),"actual all-occupied group uses original +80 cursor position");
    }
    Check(deaths.requests.empty() && e.setOrigins==0 && Equal(player.pev->origin,Vector(99999,99999,99999)),"actual-data selector does not synthesize death or player placement");
    Require(pool.Shutdown() && space.Shutdown() && map.EndMap(),"actual owner cleanup");
}
}
