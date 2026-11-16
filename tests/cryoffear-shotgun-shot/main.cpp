// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/shotgun_shot.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace { std::size_t allocations{}; unsigned checks{}; }
void* operator new(std::size_t size)
{
    ++allocations;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace
{
using namespace guest::cof;
using Word = std::uint32_t;
Word Bits(float value) { return std::bit_cast<Word>(value); }
float Float(Word value) { return std::bit_cast<float>(value); }
void Check(bool value, int line)
{
    ++checks;
    if (!value) { std::cerr << "Shotgun shot failed at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value),__LINE__)
struct Context
{
    ShotgunShotState state;
    ShotgunShotFrame frame;
    std::vector<Word> events;
    float now{13.37F}, delta{.125F};
    Word effects{8}, calls{}, failAt{}, cancelAt{}, mutation{};
    int recoil{2};
    bool current{true}, record{true};
    std::array<Word,13> Snapshot() const noexcept
    {
        const auto& a=state.action;
        return {Word(a.clip),Bits(a.primary),Bits(a.secondary),Bits(a.idle),Bits(state.emptyDeadline),
            Word(a.reloadStage),Word(a.reloadAbort),Bits(state.pumpDeadline),state.pumpPending,
            Word(state.weaponVolume),Word(state.weaponFlash),effects,Bits(now)};
    }
    template<class... T>void Event(Word kind,T... values)
    {
        if(!record)return;
        events.insert(events.end(),{kind,sizeof...(T)+13,static_cast<Word>(values)...});
        const auto snapshot=Snapshot();events.insert(events.end(),snapshot.begin(),snapshot.end());
    }
    bool Step() noexcept
    {
        ++calls;
        if(calls==cancelAt)current=false;
        if(mutation)now+=delta;
        return calls!=failAt;
    }
};
bool Current(void* p) noexcept{return static_cast<Context*>(p)->current;}
float Time(void* p) noexcept{return static_cast<Context*>(p)->now;}
bool Effect(void* p,const ShotgunShotEvent& e) noexcept
{
    auto& c=*static_cast<Context*>(p);const auto& a=e.first;const auto& b=e.second;
    switch(e.kind)
    {
    case ShotgunShotEffect::Gunsmoke:CHECK(e.value==19&&e.other==32&&e.a==1);c.Event(6);break;
    case ShotgunShotEffect::FireEvent:c.Event(7,Bits(a[0]),Bits(a[1]),Bits(a[2]),Bits(b[0]),Bits(b[1]),Bits(b[2]),e.value,e.other);break;
    case ShotgunShotEffect::PlayerFire:c.Event(34);break;
    case ShotgunShotEffect::Shake:c.Event(9,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case ShotgunShotEffect::ViewAnimation:
        c.Event(3,e.value);
        if(c.mutation==2){c.state.action.clip=!c.state.action.clip;c.state.action.reloadStage=9;c.state.action.reloadAbort=!c.state.action.reloadAbort;}
        break;
    case ShotgunShotEffect::MuzzleFlash:c.effects|=2;break;
    case ShotgunShotEffect::Recoil:c.Event(8,Bits(a[0]),Bits(a[1]),Bits(a[2]));break;
    case ShotgunShotEffect::ShotState:c.Event(12,Bits(e.a));break;
    case ShotgunShotEffect::EmptyClick:c.Event(4);break;
    case ShotgunShotEffect::AmmoWarning:c.Event(43);break;
    case ShotgunShotEffect::Shell:c.Event(5,Bits(a[0]),Bits(a[1]),Bits(a[2]),Bits(b[0]),Bits(b[1]),Bits(b[2]),Bits(e.a),e.value,e.other);break;
    }
    // The original muzzle effect is a direct state write, not an external call.
    if(e.kind==ShotgunShotEffect::MuzzleFlash)return true;
    return c.Step();
}
bool MakeVectors(void* p,const ShotgunVector& a,ShotgunBasis& out) noexcept
{
    auto& c=*static_cast<Context*>(p);c.Event(30,Bits(a[0]),Bits(a[1]),Bits(a[2]));
    out={{1,0,0},{0,1,0},{0,0,1}};return c.Step();
}
bool GunPosition(void* p,ShotgunVector& out) noexcept{out={2,3,4};return static_cast<Context*>(p)->Step();}
bool AutoAim(void* p,float slope,ShotgunVector& out) noexcept
{auto& c=*static_cast<Context*>(p);c.Event(35,Bits(slope));out={1,0,0};return c.Step();}
bool Random(void* p,int lo,int hi,int& out) noexcept
{auto& c=*static_cast<Context*>(p);CHECK(lo==-4&&hi==4);c.Event(32,lo,hi);out=c.recoil;return c.Step();}
bool Bullet(void* p,const ShotgunBullet& b) noexcept
{
    auto& c=*static_cast<Context*>(p);
    c.Event(31,ShotgunBullet::Pellets,3,0,Bits(b.source[0]),Bits(b.source[1]),Bits(b.source[2]),
        Bits(b.direction[0]),Bits(b.direction[1]),Bits(b.direction[2]),Bits(b.spread[0]),Bits(b.spread[1]),
        Bits(b.spread[2]),Bits(ShotgunBullet::Distance),b.damage,b.seed);return c.Step();
}
bool SoundPosition(void* p,ShotgunVector& out) noexcept
{auto& c=*static_cast<Context*>(p);c.Event(41);out={12,14,16};return c.Step();}
bool Sound(void* p,const ShotgunVector& a) noexcept
{auto& c=*static_cast<Context*>(p);c.Event(42,0,1,1,Bits(a[0]),Bits(a[1]),Bits(a[2]));return c.Step();}
ShotgunShotHost Host(Context& c) noexcept
{return {&c,Current,Time,Effect,MakeVectors,GunPosition,AutoAim,Random,Bullet,SoundPosition,Sound};}
Context Initial()
{
    Context c;c.state.action.clip=5;c.state.action.reserve=50;c.state.weaponVolume=123;c.state.weaponFlash=456;
    c.frame.damage=37;c.frame.shellModel=31;c.frame.weaponEntity=19;c.frame.viewModel=32;
    c.frame.weaponOrigin=c.frame.ownerOrigin={11,12,13};c.frame.weaponViewOffset={1,2,3};
    c.frame.weaponVelocityY=5;c.frame.viewAngles={1,2,3};c.frame.attackPressed=true;c.frame.randomSeed=17;
    // Native globals store forward/up/right; this payload names forward/right/up.
    c.frame.initialBasis={{1,2,3},{7,8,9},{4,5,6}};c.events.reserve(512);return c;
}
std::vector<Word> Words(const std::string& text)
{
    std::istringstream input(text);std::vector<Word> values;Word v{};
    while(input>>std::hex>>v)values.push_back(v);
    return input.eof()?values:std::vector<Word>{};
}
void Safety()
{
    auto c=Initial();CHECK(ShotgunPrimaryAttack(c.state,c.frame,{})==ShotgunShotResult::MissingHost);
    CHECK(ShotgunPrimaryAttack(c.state,c.frame,Host(c))==ShotgunShotResult::Fired);
    const unsigned calls=c.calls;
    for(unsigned i=1;i<=calls;++i)
    {
        c=Initial();c.cancelAt=i;CHECK(ShotgunPrimaryAttack(c.state,c.frame,Host(c))==ShotgunShotResult::Cancelled);
        CHECK(c.calls==i&&c.state.action.primary==0);
        c=Initial();c.failAt=i;CHECK(ShotgunPrimaryAttack(c.state,c.frame,Host(c))==ShotgunShotResult::ConsumerRejected);
        CHECK(c.calls==i&&c.state.action.primary==0);
    }
    c=Initial();c.frame.damage=std::numeric_limits<float>::quiet_NaN();
    CHECK(ShotgunPrimaryAttack(c.state,c.frame,Host(c))==ShotgunShotResult::InvalidInput&&!c.calls);
    c=Initial();c.current=false;CHECK(ShotgunPrimaryAttack(c.state,c.frame,Host(c))==ShotgunShotResult::Cancelled&&!c.calls);
    c=Initial();c.record=false;const auto state=c.state;const auto before=allocations;
    for(unsigned i=0;i<10000;++i){c.state=state;c.calls=0;CHECK(ShotgunPrimaryAttack(c.state,c.frame,Host(c))==ShotgunShotResult::Fired);}
    CHECK(allocations==before);
}
}

int main(int argc,char** argv)
{
    if(argc!=3)return 1;
    std::ifstream cases(argv[1]),expected(argv[2]);if(!cases||!expected)return 1;
    std::string input,answer;unsigned count{};
    while(std::getline(cases,input))
    {
        const auto row=Words(input);if(row.size()!=26||row[0]>1)return 3;
        if(!std::getline(expected,answer))return 4;
        auto c=Initial();auto& a=c.state.action;auto& f=c.frame;
        c.now=Float(row[1]);a.clip=int(row[2]);a.reserve=int(row[3]);a.primary=Float(row[4]);a.secondary=Float(row[5]);
        a.idle=Float(row[6]);c.state.emptyDeadline=Float(row[7]);a.action=int(row[8]);a.special=int(row[9]);
        c.state.shotGate=int(row[10]);a.reloadStage=int(row[11]);a.reloadAbort=int(row[12]);a.aiming=row[13]!=0;
        f.infiniteAmmo=row[14]!=0;f.waterLevel=int(row[15]);f.attackPressed=row[16]!=0;f.damage=Float(row[17]);
        c.recoil=int(row[18]);f.eventOwner2814=int(row[19]);f.randomSeed=row[20];c.mutation=row[21];c.delta=Float(row[22]);
        c.state.pumpDeadline=Float(row[23]);c.state.pumpPending=row[24]!=0;
        for(auto* v:{&f.initialBasis.forward,&f.initialBasis.right,&f.initialBasis.up})for(auto& value:*v)value*=Float(row[25]);
        const auto result=row[0]?ShotgunPumpShell(f,Host(c)):ShotgunPrimaryAttack(c.state,f,Host(c));
        CHECK(result==ShotgunShotResult::Fired||result==ShotgunShotResult::Empty||result==ShotgunShotResult::Ineligible);
        const auto snapshot=c.Snapshot();std::vector<Word> actual(snapshot.begin(),snapshot.end());
        actual.insert(actual.end(),c.events.begin(),c.events.end());const auto wanted=Words(answer);
        if(actual!=wanted)
        {
            std::cerr<<"Original shotgun shot mismatch case "<<count<<" op="<<row[0]<<'\n';
            for(unsigned i=0;i<std::min(actual.size(),wanted.size());++i)
                if(actual[i]!=wanted[i])std::cerr<<i<<":"<<std::hex<<actual[i]<<" != "<<wanted[i]<<'\n';
            return 5;
        }
        checks+=unsigned(actual.size());++count;
    }
    if(!count)return 6;if(std::getline(expected,answer))return 7;
    Safety();std::cout<<"originalCases="<<count<<" checks="<<checks<<" failures=0\n";
}
