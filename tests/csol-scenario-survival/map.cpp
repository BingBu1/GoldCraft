// SPDX-License-Identifier: GPL-3.0-or-later
#include "../csol-scenario-frame/frame_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include "actual_lostcity_points.inc"

int main(int argc,char** argv)
{
    int checks{},failures{};
    auto check=[&](bool value,const char* label){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}};
    try
    {
        if(argc!=2)throw std::runtime_error("usage: map_tests common-root");
        const std::filesystem::path root=argv[1];
        FrameEngine engine;engine.files.clear();
        // Current production lookup uses the engine callback, not the SDK
        // classname hash. Match ReHLDS PF_find_Shared's ascending edicts and
        // world sentinel. A missing callback must not masquerade as no points.
        engine.Add("worldspawn",0);
        g_engfuncs.pfnFindEntityByString=[](edict_t* previous,const char* field,const char* value)->edict_t*
        {
            if(std::strcmp(field,"classname")!=0)
                throw std::runtime_error("unexpected entity search field");
            if(!value)return nullptr;
            auto& fixture=*Engine::active;
            const int first=previous?Engine::Index(previous):0;
            edict_t* found{};int best=std::numeric_limits<int>::max();
            for(const auto& candidate:fixture.edicts)
            {
                const int index=Engine::Index(candidate.get());
                if(!candidate->free&&index>first&&index<best&&candidate->v.classname&&
                    std::strcmp(STRING(candidate->v.classname),value)==0)
                {found=candidate.get();best=index;}
            }
            return found?found:Engine::FromOffset(0);
        };
        for(const auto* path:{"resource/maplist.csv","maps/zbs_wave_zs_lostcity.cso",
            "maps/zbs_zs_lostcity.cso","maps/ZBS_ZT_zs_lostcity.cso"})
        {
            std::ifstream f(root/path,std::ios::binary);
            if(!f)throw std::runtime_error("actual common resource missing");
            engine.files[path]={std::istreambuf_iterator<char>(f),{}};
        }
        for(const auto& point:ActualLostcityPoints)
        {
            CBaseEntity* entity{};
            if(point.monster)entity=&engine.Monster(point.index,point.id,point.quota,point.team);
            else entity=&engine.Add("info_player_start",point.index);
            entity->pev->origin=Vector(point.x,point.y,point.z);
            entity->pev->angles=Vector(point.pitch,point.yaw,point.roll);
        }
        CCSOLScenarioMap map;FrameEvents events;FrameRules rules;
        check(map.BeginMap("zs_lostcity"),"actual type0 catalog and resources load without TRG");
        check(map.CollectEntities(),"actual native player start plus zombie markers collect without zbstrigger");
        check(map.SpawnPoints().PlayerPointCount(2,0)==15,"actual native fallback has15 CT spawn points");
        check(map.SpawnPoints().Points().Monsters().size()==58,"all58 source monster points collected");
        for(const auto& point:ActualLostcityPoints)
        {
            if(!point.monster)continue;
            const auto* actual=map.SpawnPoints().Points().FindMonsterPoint(point.team,point.id);
            check(actual&&actual->point.entityIndex==std::size_t(point.index)&&
                actual->point.origin==std::array<float,3>{point.x,point.y,point.z}&&
                actual->point.angles==std::array<float,3>{point.pitch,point.yaw,point.roll}&&
                actual->maxSpawn==point.quota,"source monster identity position angle and quota retained");
        }
        check(map.TriggerVolumes().Volumes().Volumes().empty(),"no invented trigger geometry");
        const auto* proxy=map.SpawnPoints().SelectPlayerSpawn(2,0,0);
        check(proxy&&proxy->pev->origin!=g_vecZero,"real SDK point selector returns native point through internal proxy");
        rules.Started(true);rules.m_bFreezePeriod=false;rules.m_bRoundTerminating=false;
        std::size_t calls{};
        auto dispatch=[](void* context,const csol::scenario::WaveRow&,csol::scenario::RowClock&){++*static_cast<std::size_t*>(context);};
        for(std::size_t index=0;index<5;++index)
        {
            check(map.BeginRound(index,1000+float(index)*210,20,events),"actual SDK ordinary round begins");
            check(!map.Round().Group()&&map.State()==CSOLScenarioMapState::RoundActive,"type0 publishes active round with no TRG group");
            const auto wanted=map.Tables()->rounds.rows[index].waves;
            const auto actual=map.Round().Waves().ActiveWaves();
            check(std::equal(actual.begin(),actual.end(),wanted.begin(),wanted.end()),"SDK owner activates every actual round wave");
            engine.globals.time=1020+float(index)*210;
            const auto frame=map.UpdateFrame(rules,engine.globals.time,1,dispatch,&calls,events);
            check(frame.valid&&frame.managerTailReached&&frame.waves.valid,"type0 frame reaches ordinary waves and health-summary tail");
            check(engine.slots.empty(),"no spatial player scan without trigger group");
        }
        check(calls>0,"actual SDK frame dispatched source rows");
        const auto generation=map.RoundGeneration();
        check(!map.BeginRound(5,9999,20,events)&&map.RoundGeneration()==generation,"past-final round rejected without reviving old callbacks");
        check(map.EndMap()&&map.BeginMap("zs_lostcity"),"same-map load invalidates previous round identity");
    }
    catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
    std::cout<<"survival actual SDK map: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
