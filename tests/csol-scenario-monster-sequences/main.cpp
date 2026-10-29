// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/monster_sequences.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

int LookupSequence(void* model, const char* label);
// Static storage pulled in by the SDK precompiled headers; this target never
// constructs Bots or executes their gameplay lifecycle.
bool g_bAllowedCSBot{};
using namespace csol::scenario;
namespace
{
// Independently transcribed from initialization/type-sequence-cache.asm and
// sequence-cache-labels.json. Never generated from the production Names array.
constexpr std::array<std::string_view,33> Labels{
    "zbs_idle1","zbs_walk","zbs_run","zbs_jump","zbs_attack_walk","zbs_attack1_run","zbs_attack2_run",
    "zbs_aim_grenade_idle1","zbs_aim_grenade_walk","zbs_aim_grenade_run","zbs_shoot_grenade_idle1",
    "zbs_shoot_grenade_walk","zbs_shoot_grenade_run","head_flinch","gut_flinch","head","death1","death2",
    "death3","gutshot","left","back","right","forward","zbs_shoot_grenade_idle1","zbs_skill_dash",
    "zbs_skill1_idle","zbs_skill1_walk","zbs_skill1_run","skill1","zbs_climb","turnL","turnR"};
int checks{},failures{};
void Check(bool condition,const char* label)
{++checks;if(!condition){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
struct Lookups
{
    std::array<std::int32_t,34> results{};
    std::vector<std::string> labels;
    static int Call(void* context,const char* name)
    {
        auto& self=*static_cast<Lookups*>(context);
        const auto index=self.labels.size();self.labels.emplace_back(name);
        if(index>=self.results.size())throw std::runtime_error("unexpected extra lookup");
        return self.results[index];
    }
};
Lookups Unique()
{
    Lookups lookup;
    for(std::size_t i=0;i<lookup.results.size();++i)lookup.results[i]=static_cast<int>(1000+i);
    return lookup;
}
std::int16_t Signed16(std::int32_t value)
{return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));}
std::array<std::int16_t,33> Expected(const std::array<std::int32_t,34>& raw)
{
    std::array<std::int16_t,33> wanted{};
    for(std::size_t i=0;i<wanted.size();++i)wanted[i]=Signed16(raw[i]);
    // Original writes +96..+A6 as nine contiguous shorts when head is -1.
    if(wanted[15]==-1)for(std::size_t i=15;i<=23;++i)wanted[i]=Signed16(raw[33]);
    if(wanted[30]==-1)wanted[30]=wanted[1];
    if(wanted[31]==-1)wanted[31]=wanted[0];
    if(wanted[32]==-1)wanted[32]=wanted[0];
    return wanted;
}
void CacheCases()
{
    MonsterSequences output;
    Check(MonsterSequenceCount==33 && output.values.size()==33,"cache has thirty-three slots");
    for(auto value:output.values)Check(value==-1,"cache starts with unresolved signed indices");
    const auto names=MonsterSequenceNames();
    for(std::size_t i=0;i<Labels.size();++i)Check(names[i]==Labels[i],"public label order matches independent original contract");
    output.values.fill(123);const auto before=output.values;
    Check(!ResolveMonsterSequences(nullptr,nullptr,output) && output.values==before,"null lookup preserves complete prior cache");
    auto unique=Unique();
    Check(ResolveMonsterSequences(Lookups::Call,&unique,output) && output.values==Expected(unique.results),
        "each original lookup writes its own ordered slot");
    Check(output.values[10]!=output.values[24] && unique.labels.size()==33,
        "duplicate grenade-idle label is actually queried twice and stores distinct callback results");
    for(std::size_t i=0;i<Labels.size();++i)Check(unique.labels[i]==Labels[i],"observed callback requests match original order");
    for(std::size_t slot=0;slot<33;++slot)
    for(const std::int32_t value:{-1,65535,131071,-65537,32767,32768,65536,INT32_MAX,INT32_MIN})
    {
        auto lookup=Unique();lookup.results[slot]=value;output.values.fill(333);
        Check(ResolveMonsterSequences(Lookups::Call,&lookup,output) && output.values==Expected(lookup.results),
            "per-slot boundary return narrows before exact minus-one fallback tests");
        const bool death=slot==15 && Signed16(value)==-1;
        Check(lookup.labels.size()==(death?34u:33u) && (!death || lookup.labels.back()=="death"),
            "only narrowed head minus-one requests one common death fallback");
    }
    for(const std::int32_t death:{-1,0,32767,32768,65535,65536,131071,INT32_MIN,INT32_MAX})
    {
        auto lookup=Unique();lookup.results[15]=-1;lookup.results[33]=death;
        Check(ResolveMonsterSequences(Lookups::Call,&lookup,output) && output.values==Expected(lookup.results),
            "common death result replaces all nine death slots even when other named deaths resolved");
    }
    Lookups absent;absent.results.fill(-1);
    Check(ResolveMonsterSequences(Lookups::Call,&absent,output) && output.values==Expected(absent.results),
        "missing walk idle and fallback death retain minus-one without invented sequence zero");
    auto first=Unique();ResolveMonsterSequences(Lookups::Call,&first,output);
    auto second=Unique();for(auto& value:second.results)value+=2000;
    Check(ResolveMonsterSequences(Lookups::Call,&second,output) && output.values==Expected(second.results),
        "subsequent model context completely replaces previous indices without global cache reuse");
}
void Put(std::vector<char>& bytes,std::size_t offset,int value)
{std::memcpy(bytes.data()+offset,&value,sizeof(value));}
std::vector<char> Model(std::initializer_list<const char*> labels)
{
    std::vector<char> data(244+labels.size()*176);Put(data,164,static_cast<int>(labels.size()));Put(data,168,244);
    std::size_t i=0;for(const char* text:labels){std::memcpy(data.data()+244+i*176,text,std::strlen(text));++i;}
    return data;
}
int ModelLookup(void* context,const char* label){return LookupSequence(context,label);}
void SdkLookupCases()
{
    auto model=Model({"ZBS_IDLE1","zbs_idle1","ZbS_WaLk","DEATH","HEAD","TURNl","TuRnR"});
    Check(LookupSequence(nullptr,"zbs_idle1")==0,"actual SDK lookup returns zero for null model");
    Check(LookupSequence(model.data(),"zbs_idle1")==0 && LookupSequence(model.data(),"zbs_walk")==2,
        "actual SDK performs case-insensitive lookup and first duplicate wins");
    Check(LookupSequence(model.data(),"missing")==-1,"actual SDK missing label is ACT_INVALID");
    MonsterSequences output;
    Check(ResolveMonsterSequences(ModelLookup,model.data(),output) && output.values[0]==0 && output.values[1]==2 &&
        output.values[15]==4 && output.values[16]==-1 && output.values[30]==2 && output.values[31]==5 && output.values[32]==6,
        "production resolver consumes actual SDK lookup, without treating missing death1 as whole-group fallback");
    model=Model({"ZBS_IDLE1","ZBS_WALK","DEATH","death1"});
    Check(ResolveMonsterSequences(ModelLookup,model.data(),output),"real SDK lookup supports a death fallback model");
    for(std::size_t i=15;i<=23;++i)Check(output.values[i]==2,"missing head overwrites all nine slots with actual SDK death index");
}
int Oracle()
{
    Lookups lookup;
    while(std::cin>>lookup.results[0])
    {
        for(std::size_t i=1;i<lookup.results.size();++i)if(!(std::cin>>lookup.results[i]))return 2;
        lookup.labels.clear();MonsterSequences output;
        std::cout<<ResolveMonsterSequences(Lookups::Call,&lookup,output)<<' '<<lookup.labels.size();
        for(auto value:output.values)std::cout<<' '<<value;
        for(const auto& label:lookup.labels)std::cout<<' '<<label;
        std::cout<<'\n';
    }
    return std::cin.eof()?0:2;
}
int ActualModel()
{
    _setmode(_fileno(stdin),_O_BINARY);
    std::vector<char> bytes{std::istreambuf_iterator<char>(std::cin),{}};
    if(bytes.size()<172 || std::memcmp(bytes.data(),"IDST",4)!=0)return 3;
    int count{},offset{};std::memcpy(&count,bytes.data()+164,4);std::memcpy(&offset,bytes.data()+168,4);
    if(count<0 || offset<0 || static_cast<std::size_t>(offset)>bytes.size() ||
        static_cast<std::size_t>(count)>(bytes.size()-static_cast<std::size_t>(offset))/176)return 3;
    for(int i=0;i<count;++i)if(!std::memchr(bytes.data()+offset+static_cast<std::size_t>(i)*176,0,32))return 3;
    MonsterSequences output;
    if(!ResolveMonsterSequences(ModelLookup,bytes.data(),output))return 4;
    for(auto value:output.values)std::cout<<value<<' ';
    std::cout<<'\n';return 0;
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==2 && std::string_view(argv[1])=="--oracle")return Oracle();
        if(argc==2 && std::string_view(argv[1])=="--model")return ActualModel();
        CacheCases();SdkLookupCases();
    }
    catch(const std::exception& e){std::cerr<<"EXCEPTION: "<<e.what()<<'\n';return 2;}
    std::cout<<"monster_sequences checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
