// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/flinch_task.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>

using namespace guest::cof;
namespace
{
unsigned long long checks{},allocations{};
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"task29 check %llu failed\n",checks);std::exit(1);}}
constexpr std::array<std::string_view,8> names{"monster_normal","monster_bosschainsaw","monster_booksimon",
    "monster_doctorboss","monster_sewerboss","MONSTER_BOOKSIMON","monster_booksimon_extra",""};
constexpr int lookups[][4]={{3,3,3,3},{-1,-1,-1,-1},{3,-1,-1,-1},{-1,3,-1,7},
    {-2,0,1,2},{-1,-1,0,0},{3,4,5,6},{-1,-1,3,4}};
struct Context
{
    FlinchTaskState state;
    std::array<int,2> groups{};
    unsigned mode{},vectors{},lookup{},steps{},cancelAt{},failAt{},used{};
    int group{};bool current{true};std::array<unsigned,16> events{};
    bool Step(){++steps;if(steps==cancelAt)current=false;return steps!=failAt;}
};
bool Current(void* p) noexcept{return static_cast<Context*>(p)->current;}
bool Vectors(void* p) noexcept
{auto& c=*static_cast<Context*>(p);Check(c.vectors<2);c.events[c.used++]=1;c.group=c.groups[c.vectors++];return c.Step();}
bool Group(void* p,int& result) noexcept
{auto& c=*static_cast<Context*>(p);result=c.group;return c.Step();}
bool Lookup(void* p,int activity,int& result) noexcept
{
    auto& c=*static_cast<Context*>(p);Check(c.lookup<4);c.events[c.used++]=2;c.events[c.used++]=activity;
    result=lookups[c.mode][c.lookup++];if(c.mode>=6&&c.lookup==2)c.state.conditions^=0x40000000U;
    return c.Step();
}
FlinchActivityHost Host(Context& c){return {&c,Current,Vectors,Group,Lookup};}
bool EqualState(const FlinchTaskState& s,std::ifstream& input)
{
    for(unsigned got:{s.conditions,unsigned(s.status),unsigned(s.idealActivity),unsigned(s.sequenceFinished)})
    {unsigned wanted{};if(!(input>>std::hex>>wanted)||got!=wanted)return false;++checks;}
    return true;
}
}
void* operator new(std::size_t size){++allocations;if(auto* p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
int main(int argc,char** argv)
{
    if(argc!=3)return 2;std::ifstream input(argv[1]),expected(argv[2]);if(!input||!expected)return 2;
    std::array<std::uint32_t,10> row{};unsigned cases{};
    while(input>>std::hex>>row[0])
    {
        for(unsigned i=1;i<row.size();++i)if(!(input>>std::hex>>row[i]))return 3;
        if(row[0]>=names.size()||row[8]>=std::size(lookups))return 3;
        Context c;c.state={row[2],std::bit_cast<int>(row[3]),std::bit_cast<int>(row[4]),std::bit_cast<int>(row[5])};
        c.groups={std::bit_cast<int>(row[6]),std::bit_cast<int>(row[7])};c.mode=row[8];
        const FlinchTaskOwner owner{names[row[0]],std::bit_cast<int>(row[1])};const auto before=allocations;
        Check(BeginSmallFlinchTask(c.state,owner,Host(c))==FlinchActivityResult::Applied);
        Check(allocations==before);if(!EqualState(c.state,expected))return 5;
        unsigned length{},want{};if(!(expected>>std::hex>>length)||length!=c.used)return 5;
        for(unsigned i=0;i<length;++i){if(!(expected>>std::hex>>want)||want!=c.events[i])return 5;++checks;}
        c.state.conditions=row[9];Check(RunSmallFlinchTask(c.state,Host(c))==FlinchActivityResult::Applied);
        if(!EqualState(c.state,expected))return 5;++cases;
    }
    if(!input.eof())return 3;if(!cases)return 6;unsigned extra{};if(expected>>std::hex>>extra)return 7;
    for(unsigned step=1;step<=8;++step)for(bool cancel:{false,true})
    {
        Context c;c.state={0,1,777,0};c.groups={1,3};c.mode=3;
        if(cancel)c.cancelAt=step;else c.failAt=step;
        Check(BeginSmallFlinchTask(c.state,{"monster_normal",0},Host(c))==
            (cancel?FlinchActivityResult::Cancelled:FlinchActivityResult::BackendFailed));
        Check(c.steps==step&&c.state.idealActivity==777&&c.state.status==1);
    }
    Context c;c.state={0,1,777,1};c.current=false;
    Check(RunSmallFlinchTask(c.state,Host(c))==FlinchActivityResult::Cancelled&&c.state.status==1);
    auto h=Host(c);h.current=nullptr;
    Check(BeginSmallFlinchTask(c.state,{"monster_normal",0},h)==FlinchActivityResult::MissingBackend);
    Check(RunSmallFlinchTask(c.state,h)==FlinchActivityResult::MissingBackend);
    c={};c.groups={1,3};c.mode=0;const auto before=allocations;
    for(unsigned i=0;i<10000;++i)
    {
        c.used=c.vectors=c.lookup=c.steps=0;c.state={0,1,777,1};
        Check(BeginSmallFlinchTask(c.state,{"monster_normal",0},Host(c))==FlinchActivityResult::Applied);
        Check(c.state.idealActivity==72&&RunSmallFlinchTask(c.state,Host(c))==FlinchActivityResult::Applied&&c.state.status==4);
    }
    Check(allocations==before);
    std::printf("flinch task29: %u original cases, %llu checks passed\n",cases,checks);
}
