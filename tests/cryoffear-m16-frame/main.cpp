// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/m16_control.hpp"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace guest::cof;
namespace
{
using U=std::uint32_t;
U Bits(float value){return std::bit_cast<U>(value);}
float Float(U value){return std::bit_cast<float>(value);}
struct Fixture
{
    MagazineActionState state;
    M16FrameOwner owner;
    float now{},deadline{},delta{},entryTime{};
    unsigned change{},nth{},ordinal{};
    int sequence{},alternate{},reject{-1},cancel{-1};
    bool valid{true},stale{};
    std::vector<U> events;
    std::vector<U> Snapshot() const
    {
        return {U(state.action),state.aiming,state.pendingReload,state.loading,Bits(state.primary),
            Bits(state.secondary),Bits(state.extra),Bits(state.idle),Bits(deadline),Bits(now),
            owner.flags,Bits(owner.velocityX),Bits(owner.velocityY),U(sequence)};
    }
    bool Observe(unsigned kind,int argument=0)
    {
        events.push_back(kind);events.push_back(U(argument));const auto snapshot=Snapshot();
        events.insert(events.end(),snapshot.begin(),snapshot.end());++ordinal;
        if(ordinal==nth)
        {
            if(change&1)now+=delta;
            if(change&2)owner.flags^=0x40000;
            if(change&4)owner.velocityX=owner.velocityY=0;
            if(change&8)sequence=alternate;
            if(change&16)owner.flags=0x40000;
            if(change&32)owner.velocityX=20;
        }
        if(int(ordinal)==cancel)valid=false;
        return int(ordinal)!=reject;
    }
    M16FrameHost Host()
    {
        return {this,[](void* p)noexcept{return static_cast<Fixture*>(p)->valid;},
            [](void* p)noexcept{auto& f=*static_cast<Fixture*>(p);return f.stale?f.entryTime:f.now;},
            [](void* p,M16FrameOwner& out)noexcept{out=static_cast<Fixture*>(p)->owner;return true;},
            [](void* p,M16FrameEvent kind,int seq)noexcept
            {
                auto& f=*static_cast<Fixture*>(p);if(kind==M16FrameEvent::ViewAnimation)f.sequence=seq;
                const bool accepted=f.Observe(unsigned(kind),kind==M16FrameEvent::ViewAnimation?seq:0);
                if(kind==M16FrameEvent::Reload){f.state.loading=true;f.state.action=2;}
                return accepted;
            },
            [](void* p,float& out)noexcept
            {
                auto& f=*static_cast<Fixture*>(p);const int seq=f.sequence;
                const bool accepted=f.Observe(3,seq);
                out=seq>=38?0.F:float(1+seq)/8.F;return accepted;
            }};
    }
};
std::vector<U> Parse(const std::string& line){std::istringstream stream(line);stream>>std::hex;std::vector<U> row;U v;while(stream>>v)row.push_back(v);return row;}
int checks{};
void Check(bool value){++checks;if(!value){std::cerr<<"frame edge check "<<checks<<" failed\n";std::exit(2);}}
}
int main(int argc,char** argv)
{
    if(argc<3)return 1;
    std::ifstream input(argv[1]),expected(argv[2]);if(!input||!expected)return 1;
    std::string line,answer;int cases{};std::size_t callbacks{};
    while(std::getline(input,line))
    {
        const auto row=Parse(line);if(row.size()!=20)return 3;if(!std::getline(expected,answer))return 4;
        Fixture f;f.now=Float(row[0]);f.state.action=int(row[1]);f.state.aiming=row[2]!=0;
        f.state.pendingReload=row[3]!=0;f.state.loading=row[4]!=0;f.state.primary=Float(row[5]);
        f.state.secondary=Float(row[6]);f.state.extra=Float(row[7]);f.state.idle=Float(row[8]);
        f.deadline=Float(row[9]);f.owner={Float(row[11]),Float(row[12]),Float(row[15]),row[10],row[13]!=0,row[14]!=0};
        f.change=row[16];f.nth=row[17];f.delta=Float(row[18]);f.alternate=int(row[19]);
        f.entryTime=f.now;f.stale=argc>3&&std::string(argv[3])=="--stale-time";
        Check(AdvanceM16DerivedFrame(f.state,f.deadline,f.Host())==M16ControlResult::Applied);
        f.Observe(4);auto got=f.Snapshot();got.insert(got.end(),f.events.begin(),f.events.end());
        auto wanted=Parse(answer);
        if(got!=wanted)
        {
            std::cerr<<"frame case "<<cases<<" differs\n";
            for(std::size_t i=0;i<std::min(got.size(),wanted.size());++i)if(got[i]!=wanted[i])
                std::cerr<<i<<": "<<std::hex<<got[i]<<" expected "<<wanted[i]<<std::dec<<'\n';
            std::cerr<<"sizes "<<got.size()<<' '<<wanted.size()<<'\n';return 5;
        }
        callbacks+=f.ordinal;++cases;
    }
    if(!cases)return 6;if(std::getline(expected,line))return 7;
    for(int i=1;i<=3;++i)
    {
        Fixture f;f.now=10;f.owner.flags=0x40000;f.owner.velocityX=3;f.reject=i;
        auto result=AdvanceM16DerivedFrame(f.state,f.deadline,f.Host());
        Check(i<3?result==M16ControlResult::ConsumerRejected:result==M16ControlResult::Applied);
        Fixture cancelled;cancelled.now=10;cancelled.owner.flags=0x40000;cancelled.owner.velocityX=3;cancelled.cancel=i;
        result=AdvanceM16DerivedFrame(cancelled.state,cancelled.deadline,cancelled.Host());
        Check(i<3?result==M16ControlResult::Cancelled:result==M16ControlResult::Applied);
    }
    Fixture missing;auto host=missing.Host();host.duration=nullptr;
    Check(AdvanceM16DerivedFrame(missing.state,missing.deadline,host)==M16ControlResult::MissingHost);
    Fixture special;special.state.special=1;
    Check(AdvanceM16DerivedFrame(special.state,special.deadline,special.Host())==M16ControlResult::UnsupportedState);
    std::cout<<"M16 derived frame: "<<cases<<" original cases, "<<callbacks<<" observed callbacks, "<<checks<<" checks\n";
}
