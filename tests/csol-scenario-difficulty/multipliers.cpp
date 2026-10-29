// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.hpp"
#include "actual_expected.hpp"
#include "../../native/csol/scenario/difficulty_monsters.hpp"
#include "../csol-scenario-rules-session/resources.hpp"
#include <limits>
using namespace difficulty_test;
using namespace csol::scenario;

namespace
{
std::string Factors(const std::array<std::string,6>& values)
{
    std::vector<std::string> fields(18,"1"); fields[0]="99";fields[1]="name";
    for(std::size_t priority=0;priority<3;++priority) {
        fields[10+priority*3]=values[priority*2];fields[11+priority*3]=values[priority*2+1];
    }
    std::string csv="header\r\n";
    for(std::size_t i=0;i<fields.size();++i){if(i)csv+=',';csv+=fields[i];}
    return csv+"\r\n";
}
void LoadFactors(Files& f,DifficultyResources& d,const std::array<std::string,6>& values)
{
    f.contents[DefaultPath]=Encrypt(Factors(values));ResourceError error;
    Require(d.BeginMap("map_session_test",1,f.Api(),error),"independent multiplier fixture loaded");
}
MonsterType Type(int id,int priority,int health,int speed)
{
    MonsterType t;t.id=id;t.priority=static_cast<std::int8_t>(priority);t.health=health;
    t.speed=static_cast<std::int16_t>(speed);t.attackDamage=71;t.appearance="literal";return t;
}
MonsterTypeTable Types(std::initializer_list<MonsterType> values)
{
    MonsterTypeTable t;t.source.sourceBytes="unaltered source bytes";t.rows=values;
    for(std::size_t i=0;i<t.rows.size();++i){t.rows[i].recordIndex=i+1;t.byId[t.rows[i].Key()]=i;}
    return t;
}
std::vector<std::pair<std::int32_t,std::int16_t>> Values(const MonsterTypeTable& table)
{
    std::vector<std::pair<std::int32_t,std::int16_t>> result;
    for(const auto& t:table.rows)result.emplace_back(t.health,t.speed);
    return result;
}
void Direct()
{
    Files f;DifficultyResources d;ConfigError error;
    auto table=Types({Type(1,0,9,101)});
    const auto initial=Values(table);
    Check(!ApplyMonsterDifficulty(table,d,error)&&Values(table)==initial,"unloaded multiplier rejects without mutation");
    LoadFactors(f,d,{"0.5","0.5","2","2","3","-2"});
    auto overridden=Type(90,0,99,99);overridden.column20=1;
    table=Types({Type(1,0,15,15),overridden,Type(2,1,3,20000),Type(3,2,-3,-20000),Type(4,-1,9,101),Type(5,3,9,101)});
    const auto* pointer=table.Find(1);const auto raw=table.source.sourceBytes;
    Require(ApplyMonsterDifficulty(table,d,error),"all priority factors apply atomically");
    Check(table.rows[0].health==15&&table.rows[0].speed==15,"overridden source row is never scaled");
    Check(pointer==table.Find(1)&&pointer==&table.rows[1]&&pointer->health==49&&pointer->speed==49,"effective override key scales in place and truncates toward zero");
    Check(table.rows[2].health==6&&table.rows[2].speed==-25536,"priority one speed retains original signed low16 store");
    Check(table.rows[3].health==-9&&table.rows[3].speed==-25536,"priority two uses separate columns and negative inputs");
    Check(table.rows[4].health==9&&table.rows[4].speed==101&&table.rows[5].health==9&&table.rows[5].speed==101,"other priorities are unchanged");
    Check(table.source.sourceBytes==raw&&std::all_of(table.rows.begin(),table.rows.end(),[](const MonsterType& t){return t.attackDamage==71&&t.appearance=="literal";}),"raw table and attack/appearance are not pre-scaled");

    struct Edge {int health,speed;const char* hp;const char* sp;bool success;int wantHealth,wantSpeed;};
    const Edge edges[]{
        {16777217,1,"1","1",true,16777216,1},
        {2147483520,1,"1","1",true,2147483520,1},
        {std::numeric_limits<int>::max(),1,"1","1",false,0,0},
        {std::numeric_limits<int>::min(),1,"1","1",true,std::numeric_limits<int>::min(),1},
        {std::numeric_limits<int>::min(),1,"1.00000011920928955078125","1",false,0,0},
        {-3,-3,"0.5","0.5",true,-1,-1},
        {3,20000,"0","-2",true,0,25536},
        {1,32767,"1","65536",true,1,0},
        {1,-32768,"1","65536",true,1,0},
        {1,-32768,"1","65537",false,0,0},
        {2,1,"3.4028234663852886e38","1",false,0,0},
        {1,1,"nan","1",false,0,0},
        {1,1,"1","1e99999",false,0,0}
    };
    for(const auto& edge:edges) {
        LoadFactors(f,d,{edge.hp,edge.sp,"2","2","1","1"});
        table=Types({Type(1,1,5,5),Type(2,0,edge.health,edge.speed)});const auto before=Values(table);
        const bool applied=ApplyMonsterDifficulty(table,d,error);
        Check(applied==edge.success,"float32/int32 conversion boundary accepted or rejected as specified");
        if(applied)Check(table.rows[1].health==edge.wantHealth&&table.rows[1].speed==edge.wantSpeed&&table.rows[0].health==10,"literal int32 and signed16 expected output");
        else Check(Values(table)==before&&!error.message.empty(),"failure in later record leaves all earlier records untouched");
    }
    LoadFactors(f,d,{"2","2","2","2","2","2"});
    for(bool badRow:{false,true}) {
        table=Types({Type(1,0,10,10),Type(2,1,10,10)});const auto before=Values(table);
        if(badRow)table.byId[2]=100;else table.byId[3]=1;
        Check(!ApplyMonsterDifficulty(table,d,error)&&Values(table)==before,"invalid effective index refuses all writes");
    }
    table={};Require(ApplyMonsterDifficulty(table,d,error),"loaded difficulty accepts no effective types");
    f.contents[DefaultPath]=Encrypt("header\r\n99,name,1\r\n");ResourceError resource;
    Require(d.BeginMap("map_session_test",1,f.Api(),resource),"short valid table remains a table");
    table=Types({Type(1,0,10,10)});const auto before=Values(table);
    Check(!ApplyMonsterDifficulty(table,d,error)&&Values(table)==before,"missing required factor fails without invented neutral value");
}
void MapLifecycle()
{
    Files f;f.contents=session_test::Resources();DifficultyResources d;MapResources map;ConfigError error;ResourceError resource;
    LoadFactors(f,d,{"2","2","2","2","2","2"});
    Check(!map.ApplyDifficulty(d,error),"no loaded map types cannot mark multiplier applied");
    Require(map.BeginMap("map_session_test",f.Api(),resource,1),"real required map tables load together");
    auto first=[&] {const auto* t=map.Tables();Require(t&&!t->monsterTypes.byId.empty(),"fixture effective type exists");return t->monsterTypes.Find(t->monsterTypes.byId.begin()->first);};
    const auto* type=first();const auto original=type->health;
    Require(type->priority>=0&&type->priority<=2&&original>0&&original<1000000,"small scalable map fixture value");
    Require(map.ApplyDifficulty(d,error),"first map scaling commits");
    Check(first()==type&&type->health==original*2,"first map scale changes stable type in place");
    Require(map.ApplyDifficulty(d,error),"second map application allowed as no-op");
    Check(type->health==original*2,"once gate prevents compounded factors");
    const auto old=f.contents.at("maps/ZBS_ZT_map_session_test.cso");f.contents["maps/ZBS_ZT_map_session_test.cso"]="bad";
    Check(!map.Reload(f.Api(),resource),"failed map reload reports error");
    Require(map.ApplyDifficulty(d,error),"failed reload retains prior once gate");
    Check(first()==type&&type->health==original*2,"failed reload retains table pointer and exactly one scale");
    f.contents["maps/ZBS_ZT_map_session_test.cso"]=old;
    Require(map.Reload(f.Api(),resource),"successful reload restores unscaled source records");
    Check(first()->health==original,"successful reload drops prior multiplier");
    Require(map.ApplyDifficulty(d,error),"successful reload resets once gate");Check(first()->health==original*2,"new table applies exactly once");
    map.Clear();Check(!map.Tables()&&!map.ApplyDifficulty(d,error),"clear removes scaled types and gate");
    Require(map.BeginMap("map_session_test",f.Api(),resource,1)&&map.ApplyDifficulty(d,error),"new map starts fresh scaling lifecycle");
    Check(first()->health==original*2,"new map does not retain old scaled value");
}
void Actual(const std::filesystem::path& directory)
{
    Files f;f.contents[DefaultPath]=Read(directory/"ZBS_Difficulty_Default.cso");
    const auto encoded=Read(directory/"ZBS_ZT_zs_deadend.cso");
    DifficultyResources d;ResourceError resource;ConfigError error;
    for(std::uint16_t row=1;row<=7;++row) {
        MonsterTypeTable table;Require(ReadMonsterTypeResource(Bytes(encoded),table,error),"real deadend type resource loads");
        Require(table.rows.size()==actual_expected::monsters.size(),"real source retains 38 monster rows");
        const auto raw=table.source.sourceBytes;std::vector<const MonsterType*> pointers;
        for(const auto& expected:actual_expected::monsters)pointers.push_back(table.Find(expected.key));
        Require(d.BeginMap("zs_deadend",row,f.Api(),resource),"real default row captured for scaling");
        Require(ApplyMonsterDifficulty(table,d,error),"all real map types fit selected original conversion range");
        for(std::size_t i=0;i<table.rows.size();++i) {
            const auto& t=table.rows[i];const auto& original=actual_expected::monsters[i];const auto& value=actual_expected::scaled[row-1][i];
            Check(t.recordIndex==original.record&&t.id==original.id&&t.Key()==original.key&&t.priority==original.priority&&t.attackDamage==original.attack,"original identity priority and attack survive scale");
            Check(t.health==value.health&&t.speed==value.speed,"real type HP/speed match independent float32 static translation");
            Check(table.Find(original.key)==pointers[i],"real effective record pointers remain stable");
        }
        Check(table.source.sourceBytes==raw,"real type raw bytes preserved after scaling");
    }
}
}
int main(int argc,char** argv)
{
    try {
        Require(argc>=2,"bounded multiplier group required");const std::string mode=argv[1];
        if(mode=="--synthetic"){Direct();MapLifecycle();}
        else if(mode=="--actual"){Require(argc==3,"frozen resource directory required");Actual(argv[2]);}
        else throw std::runtime_error("unknown multiplier group");
    }catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"difficulty-multipliers "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
