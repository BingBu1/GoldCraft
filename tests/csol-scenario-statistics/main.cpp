// SPDX-License-Identifier: GPL-3.0-or-later
// Expectations come from fixed MP 11380280/113EE860 and its original table,
// never from executing the production predicate to manufacture an oracle.
#include "monster_appearances.hpp"
#include "monster_types.hpp"
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <source_location>
#include <stdexcept>

using namespace csol::scenario;
namespace
{
int checks{};
void Check(bool value,const std::source_location location=std::source_location::current())
{++checks;if(!value)throw std::runtime_error("statistics check line "+std::to_string(location.line()));}
void Expect(const MonsterType* type,int cached,int current,bool expected)
{
    bool result=!expected;ConfigError error{91,"old"};
    Check(MonsterStatisticsBoss(type,cached,current,result,error));
    Check(result==expected&&error.message.empty()&&error.byteOffset==0);
}
void Reject(const MonsterType& type,int cached,int current)
{
    bool result=true;ConfigError error;
    Check(!MonsterStatisticsBoss(&type,cached,current,result,error));
    Check(!error.message.empty()&&!result);
}
void Branches()
{
    Expect(nullptr,0,0,false);
    MonsterType type;type.id=31805;type.appearance="missing";
    Expect(&type,0,0,false); // excluded type never consults map or appearance
    for(int id:{32000,32001})
    {
        type.id=id;type.column18=false;
        Expect(&type,143,98,false);Expect(&type,98,143,true);
        Expect(&type,98,361,true);Expect(&type,143,361,false);
        Expect(&type,98,0,true); // cached-map branch does not ask current map
        Reject(type,0,98);Reject(type,-1,98);
    }
    type.id=30200;type.column18=false;
    Expect(&type,0,361,false); // map361 gate runs before missing appearance
    type.column18=true;Reject(type,98,361);
    type.appearance="zbs_bossL_big02";Expect(&type,0,361,true);
    type.column18=false;Expect(&type,0,98,true);Reject(type,98,0);Reject(type,98,-1);
    type.id=1;type.appearance="zombi_host";type.column18=true;Expect(&type,98,361,false);
    for(const char* unknown:{"", "missing", "ZOMBI_HOST", "18", "models/monster/zbs_bossL_big02.mdl"})
    {type.appearance=unknown;Reject(type,98,98);}
    // Type-ID/effective-key and AI skill cannot substitute the statistics flag.
    type.id=31805;type.column20=30200;type.appearance="zbs_bossL_big02";Expect(&type,98,98,false);
    type.id=30200;type.column20=31805;Expect(&type,98,98,true);
    for(int skill:{-32768,-1,0,999,1000,1002,32767})
    {
        type.id=1;type.skill=static_cast<std::int16_t>(skill);type.appearance="zombi_origin";Expect(&type,98,98,false);
        type.id=30200;type.appearance="zbs_bossL_big02";Expect(&type,98,98,true);
    }
    // Original synthetic type32800 has skill1000, appearance1: not statistics Boss.
    type.id=32800;type.skill=1000;type.appearance="zombi_origin";Expect(&type,98,98,false);
}
std::string Read(const char* path)
{std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error(std::string("missing input: ")+path);return {std::istreambuf_iterator<char>(stream),{}};}
struct Reader
{
    std::string bytes;std::size_t at{};
    std::uint32_t Word()
    {
        Check(at+4<=bytes.size());std::uint32_t value{};
        for(int i=0;i<4;++i)value|=std::uint32_t(static_cast<unsigned char>(bytes[at++]))<<(8*i);
        return value;
    }
    std::int32_t Signed(){return std::bit_cast<std::int32_t>(Word());}
    std::string Text(){const auto length=Word();Check(length<=bytes.size()-at);auto result=bytes.substr(at,length);at+=length;return result;}
};
void Actual(const char* oracle,const char* typeFile)
{
    Reader input{Read(oracle)};Check(input.bytes.substr(0,4)=="SBOS");input.at=4;
    Check(input.Word()==1);const auto rows=input.Word();Check(rows==143);
    const auto catalog=MonsterAppearances();Check(catalog.size()==rows);
    struct Original{unsigned index;bool boss;};std::map<std::string,Original> first;
    unsigned bossAppearances{};
    for(unsigned i=0;i<rows;++i)
    {
        const auto index=input.Word();const auto name=input.Text();const auto flag=input.Word();Check(index==i&&flag<=1);
        Check(catalog[i].index==index&&catalog[i].name==name&&catalog[i].healthSummary==bool(flag));
        first.emplace(name,Original{index,bool(flag)});bossAppearances+=flag;
    }
    Check(bossAppearances==49);
    for(const auto& [name,original]:first)
    {
        const auto* found=FindMonsterAppearance(name);Check(found&&found->index==original.index);
        MonsterType type;type.id=1;type.appearance=name;type.skill=original.boss?0:1002;
        Expect(&type,98,98,original.boss); // opposing skill deliberately cannot alter original byte
    }
    const auto encoded=Read(typeFile);MonsterTypeTable types;ConfigError error;
    Check(ReadMonsterTypeResource(std::as_bytes(std::span(encoded.data(),encoded.size())),types,error));
    const auto count=input.Word();Check(count==38&&types.rows.size()==count&&types.byId.size()==count);
    unsigned ordinary{},bosses{};
    for(unsigned i=0;i<count;++i)
    {
        const int id=input.Signed(),key=input.Signed();const auto appearance=input.Text();
        const int skill=input.Signed();const auto flag=input.Word();Check(flag<=1);
        const auto* type=types.Find(key);Check(type&&type->id==id&&type->appearance==appearance&&type->skill==skill);
        Expect(type,98,98,bool(flag));if(flag){++bosses;Check(id==30200);}else ++ordinary;
    }
    Check(ordinary==37&&bosses==1&&input.at==input.bytes.size());
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==1)Branches();
        else if(argc==4&&std::string_view(argv[1])=="--actual")Actual(argv[2],argv[3]);
        else throw std::runtime_error("usage: statistics [--actual original-oracle.bin ZBS_ZT_zs_deadend.cso]");
        std::cout<<"statistics checks="<<checks<<'\n';return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
