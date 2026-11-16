// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/flinch_activity.hpp"
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
void Check(bool value)
{ ++checks;if(!value){std::fprintf(stderr,"flinch check %llu failed\n",checks);std::exit(1);} }
struct Context
{
    int group{},nextGroup{},first{},fallback{};
    unsigned lookups{},steps{},cancelAt{},failAt{},used{};
    bool current{true};
    std::array<unsigned,8> events{};
    bool Step() noexcept {++steps;if(steps==cancelAt)current=false;return steps!=failAt;}
};
bool Current(void* p) noexcept {return static_cast<Context*>(p)->current;}
bool Vectors(void* p) noexcept
{auto& c=*static_cast<Context*>(p);c.events[c.used++]=1;c.group=c.nextGroup;return c.Step();}
bool Group(void* p,int& result) noexcept
{auto& c=*static_cast<Context*>(p);result=c.group;return c.Step();}
bool Lookup(void* p,int activity,int& result) noexcept
{auto& c=*static_cast<Context*>(p);c.events[c.used++]=2;c.events[c.used++]=activity;
 result=c.lookups++?c.fallback:c.first;return c.Step();}
FlinchActivityHost Host(Context& c) noexcept {return {&c,Current,Vectors,Group,Lookup};}
}
void* operator new(std::size_t size)
{++allocations;if(auto* value=std::malloc(size))return value;throw std::bad_alloc();}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}

int main(int argc,char** argv)
{
    if(argc!=3)return 2;
    std::ifstream input(argv[1]),expected(argv[2]);if(!input||!expected)return 2;
    std::array<std::uint32_t,4> row{};unsigned cases{};
    while(input>>std::hex>>row[0])
    {
        for(unsigned i=1;i<row.size();++i)if(!(input>>std::hex>>row[i]))return 3;
        Context c;c.group=std::bit_cast<int>(row[0]);c.nextGroup=std::bit_cast<int>(row[1]);
        c.first=std::bit_cast<int>(row[2]);c.fallback=std::bit_cast<int>(row[3]);
        int activity=-999;const auto before=allocations;
        Check(SelectFlinchActivity(Host(c),activity)==FlinchActivityResult::Applied);
        Check(allocations==before);
        unsigned want{},length{};
        if(!(expected>>std::hex>>want>>length)||want!=unsigned(activity)||length!=c.used)return 5;
        for(unsigned i=0;i<length;++i){if(!(expected>>std::hex>>want)||want!=c.events[i])return 5;++checks;}
        ++cases;
    }
    if(!input.eof())return 3;
    if(!cases)return 6;
    unsigned extra{};if(expected>>std::hex>>extra)return 7;
    for(unsigned step=1;step<=4;++step)
    {
        Context c;c.nextGroup=1;c.first=c.fallback=-1;c.cancelAt=step;int activity=999;
        Check(SelectFlinchActivity(Host(c),activity)==FlinchActivityResult::Cancelled);
        Check(c.steps==step&&activity==999);
        c={};c.nextGroup=1;c.first=c.fallback=-1;c.failAt=step;
        Check(SelectFlinchActivity(Host(c),activity)==FlinchActivityResult::BackendFailed);
        Check(c.steps==step&&activity==999);
    }
    Context c;int activity=999;c.current=false;
    Check(SelectFlinchActivity(Host(c),activity)==FlinchActivityResult::Cancelled);
    Check(c.steps==0&&activity==999);
    auto host=Host(c);host.lookup=nullptr;
    Check(SelectFlinchActivity(host,activity)==FlinchActivityResult::MissingBackend);
    Check(c.steps==0&&activity==999);
    c={};c.nextGroup=1;c.first=2;const auto before=allocations;
    for(unsigned i=0;i<10000;++i){c.steps=c.used=c.lookups=0;
        Check(SelectFlinchActivity(Host(c),activity)==FlinchActivityResult::Applied&&activity==70);}
    Check(allocations==before);
    std::printf("flinch activity: %u original cases, %llu checks passed\n",cases,checks);
}
