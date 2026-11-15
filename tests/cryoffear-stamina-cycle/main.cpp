// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/stamina_cycle.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <vector>
using namespace guest::cof;
namespace
{
unsigned checks{};std::size_t allocations{};
void CheckAt(bool ok,int line){++checks;if(!ok){std::cerr<<"stamina check "<<checks<<" line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
std::uint32_t Bits(float v){return std::bit_cast<std::uint32_t>(v);}
float Float(std::uint32_t v){return std::bit_cast<float>(v);}
struct Context
{
    StaminaCycleState state{};StaminaCycleOwner owner{};
    float now{},delta{},newAmount{},newPulse{};
    unsigned multi{},random{},change{},nth{},ordinal{},cancelAt{},failAt{};
    bool current=true,record=true;
    std::vector<std::uint32_t> events;
} c;
void Snapshot(std::vector<std::uint32_t>& v)
{
    auto& s=c.state;auto& o=c.owner;
    v.insert(v.end(),{Bits(s.amount),Bits(s.lastSent),Bits(s.nextCycle),Bits(s.auxiliaryTime),
        Bits(s.exhaustionActionDeadline),Bits(s.voiceDeadline),Bits(s.exhaustionAudioDeadline),Bits(s.hudPulse),
        o.flags,Bits(c.now),unsigned(o.infinite)|unsigned(o.alternateDrain)<<1|unsigned(o.alteredCycle)<<2|unsigned(o.rulesPresent)<<3,
        Bits(o.voiceVariant),unsigned(o.waterLevel)});
}
template<class... A>bool Observe(unsigned kind,A... args)
{
    if(c.record){c.events.insert(c.events.end(),{kind,sizeof...(A),static_cast<std::uint32_t>(args)...});Snapshot(c.events);}
    if(++c.ordinal==c.nth)
    {
        if(c.change&1)c.now+=c.delta;
        if(c.change&2)c.state.amount=c.newAmount;
        if(c.change&4)c.owner.alteredCycle=!c.owner.alteredCycle;
        if(c.change&8)c.state.hudPulse=c.newPulse;
        if(c.change&16)c.owner.flags^=0x80000;
        if(c.change&32)c.owner.voiceVariant=1-c.owner.voiceVariant;
    }
    if(c.ordinal==c.cancelAt)c.current=false;
    return c.ordinal!=c.failAt;
}
StaminaCycleHost Host()
{
    return {&c,[](void*) noexcept{return c.current;},[](void*) noexcept{return c.now;},
        [](void*,bool& out) noexcept{out=c.multi;return Observe(1);},
        [](void*,int lo,int hi,int& out) noexcept{Check(lo==0&&hi==1);out=int(c.random);return Observe(2,lo,hi);},
        [](void*,const StaminaVoiceEvent& e) noexcept{return Observe(3,unsigned(e.sample),e.channel,Bits(e.volume),Bits(e.attenuation),e.flags,e.pitch);},
        [](void*,const StaminaHudEvent& e) noexcept
        {
            switch(e.kind){case StaminaHudKind::Begin:return Observe(4,e.integer);
                case StaminaHudKind::Byte:return Observe(5,e.integer);
                case StaminaHudKind::Coord:return Observe(6,Bits(e.number));
                case StaminaHudKind::End:return Observe(7);}
            std::abort();
        }};
}
void Set(const std::array<std::uint32_t,22>& r)
{
    c.state={Float(r[2]),Float(r[3]),Float(r[4]),Float(r[5]),Float(r[6]),Float(r[7]),Float(r[8]),Float(r[9])};
    c.owner={r[10],Float(r[11]),Float(r[12]),int(r[13]),bool(r[14]&1),bool(r[14]&2),bool(r[14]&4),bool(r[14]&8)};
    c.now=Float(r[1]);c.multi=r[15];c.random=r[16];c.change=r[17];c.nth=r[18];c.delta=Float(r[19]);
    c.newAmount=Float(r[20]);c.newPulse=Float(r[21]);c.ordinal=c.failAt=c.cancelAt=0;c.events.clear();c.current=c.record=true;
}
}
void* operator new(std::size_t n){++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
int main(int argc,char** argv)
{
    if(argc!=3)return 1;
    std::ifstream cases(argv[1]),expected(argv[2]);Check(cases.good()&&expected.good());
    std::string line,answer;unsigned rows{},mismatches{};
    while(std::getline(cases,line))
    {
        Check(bool(std::getline(expected,answer)));std::istringstream input(line),want(answer);
        std::array<std::uint32_t,22> r{};for(auto& v:r)Check(bool(input>>std::hex>>v));Set(r);
        auto result=r[0]?SpawnStaminaCycle(c.state,Host()):AdvanceStaminaCycle(c.state,c.owner,Host());
        Check(result==StaminaCycleResult::Applied);
        std::vector<std::uint32_t> got,target;Snapshot(got);got.insert(got.end(),c.events.begin(),c.events.end());
        std::uint32_t value{};while(want>>std::hex>>value)target.push_back(value);
        if(got!=target)
        {
            ++mismatches;
            if(mismatches<5)
            {
                std::cerr<<"stamina mismatch row "<<rows<<" mode "<<r[0]<<" sizes "<<got.size()<<'/'<<target.size()<<'\n';
                for(unsigned at=0;at<std::min(got.size(),target.size());++at)if(got[at]!=target[at])
                    {std::cerr<<"field "<<at<<" got "<<std::hex<<got[at]<<" wanted "<<target[at]<<std::dec<<'\n';break;}
            }
        }
        ++rows;
    }
    Check(!std::getline(expected,answer)&&rows>1000&&mismatches==0);
    for(unsigned at=1;at<=7;++at)for(bool cancel:{false,true})
    {
        std::array<std::uint32_t,22> r{};r[1]=Bits(10);r[14]=8;r[15]=1;Set(r);
        if(cancel)c.cancelAt=at;else c.failAt=at;
        const auto result=AdvanceStaminaCycle(c.state,c.owner,Host());
        Check(result==(cancel?StaminaCycleResult::Cancelled:StaminaCycleResult::MissingBackend));
        Check(c.ordinal==at);
    }
    {auto h=Host();h.voice=nullptr;Check(AdvanceStaminaCycle(c.state,c.owner,h)==StaminaCycleResult::MissingBackend);}
    {std::array<std::uint32_t,22> r{};Set(r);c.state.amount=NAN;Check(AdvanceStaminaCycle(c.state,c.owner,Host())==StaminaCycleResult::InvalidInput);}
    std::array<std::uint32_t,22> r{};r[1]=Bits(10);r[2]=Bits(85);Set(r);c.record=false;
    const auto baseline=allocations;
    for(unsigned n=0;n<10000;++n){c.now+=.01F;Check(AdvanceStaminaCycle(c.state,c.owner,Host())==StaminaCycleResult::Applied);}
    Check(allocations==baseline);
    std::cout<<rows<<" complete original stamina cases; "<<checks<<" checks; zero mismatches; 10000 hot calls zero new\n";
}
