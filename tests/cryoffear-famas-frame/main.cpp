// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/famas_frame.hpp"
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
    FamasFrameOwner owner;
    MagazineSequenceTimes times;
    float now{},deadline{},delta{},entryTime{};
    unsigned change{},nth{},ordinal{};
    int sequence{},alternate{},reject{-1},cancel{-1};
    bool valid{true},stale{};
    std::vector<U> events;
    std::vector<U> Snapshot() const
    {
        return {U(state.action),state.aiming,state.pendingReload,state.loading,Bits(state.primary),
            Bits(state.secondary),Bits(state.extra),Bits(state.idle),Bits(deadline),Bits(now),
            owner.flags,Bits(owner.velocityX),Bits(owner.velocityY),U(sequence),U(state.clip)};
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
            if(change&64)state.clip=!state.clip;
        }
        if(int(ordinal)==cancel)valid=false;
        return int(ordinal)!=reject;
    }
    FamasFrameHost Host()
    {
        return {this,[](void* p)noexcept{return static_cast<Fixture*>(p)->valid;},
            [](void* p)noexcept{auto& f=*static_cast<Fixture*>(p);return f.stale?f.entryTime:f.now;},
            [](void* p,FamasFrameOwner& out)noexcept{out=static_cast<Fixture*>(p)->owner;return true;},
            [](void* p,FamasFrameEvent kind,int seq)noexcept
            {
                auto& f=*static_cast<Fixture*>(p);if(kind==FamasFrameEvent::ViewAnimation)f.sequence=seq;
                const bool accepted=f.Observe(unsigned(kind),kind==FamasFrameEvent::ViewAnimation?seq:0);
                if(accepted&&kind==FamasFrameEvent::Reload){f.state.loading=true;f.state.action=2;}
                return accepted;
            },
            [](void* p,float& out)noexcept
            {
                auto& f=*static_cast<Fixture*>(p);const int seq=f.sequence;
                const bool accepted=f.Observe(3,seq);
                out=seq>=24?0.F:f.times.duration[seq];return accepted;
            }};
    }
};
std::vector<U> Parse(const std::string& line){std::istringstream stream(line);stream>>std::hex;std::vector<U> row;U v;while(stream>>v)row.push_back(v);return row;}
int checks{};
void Check(bool value){++checks;if(!value){std::cerr<<"frame edge check "<<checks<<" failed\n";std::exit(2);}}
}
int main(int argc,char** argv)
{
    if(argc!=4&&argc!=5)return 1;
    std::ifstream input(argv[1]),expected(argv[2]),timing(argv[3]);if(!input||!expected||!timing)return 1;
    MagazineSequenceTimes times;
    for(auto& value:times.duration){U bits{};if(!(timing>>std::hex>>bits))return 3;value=Float(bits);}
    std::string line,answer;int cases{};std::size_t callbacks{};
    while(std::getline(input,line))
    {
        const auto row=Parse(line);if(row.size()!=25)return 3;if(!std::getline(expected,answer))return 4;
        Fixture f;f.times=times;f.now=Float(row[0]);f.state.action=int(row[1]);f.state.aiming=row[2]!=0;
        f.state.pendingReload=row[3]!=0;f.state.loading=row[4]!=0;f.state.primary=Float(row[5]);
        f.state.secondary=Float(row[6]);f.state.extra=Float(row[7]);f.state.idle=Float(row[8]);
        f.deadline=Float(row[9]);f.owner={Float(row[11]),Float(row[12]),Float(row[15]),row[10],row[13]!=0,row[14]!=0};
        f.change=row[16];f.nth=row[17];f.delta=Float(row[18]);f.alternate=int(row[19]);
        f.state.clip=int(row[20]);
        f.entryTime=f.now;f.stale=argc==5&&std::string(argv[4])=="--stale-time";
        Check(AdvanceFamasDerivedFrame(f.state,f.deadline,f.Host())==FamasFrameResult::Applied);
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
    for(int i=1;i<=2;++i)
    {
        Fixture f;f.now=10;f.owner.flags=0x40000;f.owner.velocityX=3;f.reject=i;f.times.duration.fill(2.5F);
        Check(AdvanceFamasDerivedFrame(f.state,f.deadline,f.Host())==FamasFrameResult::AppliedWithoutPresentation);
        Check(f.state.action==(i==2?0:1));
        Fixture cancelled;cancelled.now=10;cancelled.owner.flags=0x40000;cancelled.owner.velocityX=3;cancelled.cancel=i;
        Check(AdvanceFamasDerivedFrame(cancelled.state,cancelled.deadline,cancelled.Host())==FamasFrameResult::Cancelled);
    }
    for(int action:{1,5})
    {
        Fixture f;f.now=10;f.state.action=action;f.reject=2;
        Check(AdvanceFamasDerivedFrame(f.state,f.deadline,f.Host())==FamasFrameResult::AppliedWithoutPresentation);
        Check(f.state.action==0);
        if(action==1)
        {
            Check(f.state.primary==float(double(f.now)+.33));
            Check(f.state.idle==float(double(f.now)+.35));
        }
        else Check(f.state.primary==0&&f.state.idle==0);
    }
    for(bool aimed:{false,true})
    {
        Fixture f;f.now=10;f.state.pendingReload=true;f.state.aiming=aimed;f.reject=1;
        Check(AdvanceFamasDerivedFrame(f.state,f.deadline,f.Host())==FamasFrameResult::AppliedWithoutPresentation);
        Check(f.state.pendingReload&&f.state.aiming==aimed&&!f.state.loading&&f.state.action==0);
    }
    Fixture missing;auto host=missing.Host();host.duration=nullptr;
    Check(AdvanceFamasDerivedFrame(missing.state,missing.deadline,host)==FamasFrameResult::Applied);
    Fixture moving;moving.now=10;moving.owner.flags=0x40000;moving.owner.velocityX=3;host=moving.Host();host.duration=nullptr;
    Check(AdvanceFamasDerivedFrame(moving.state,moving.deadline,host)==FamasFrameResult::AppliedWithoutPresentation);
    Check(moving.state.action==0&&moving.state.primary==0);
    Fixture special;special.state.special=1;
    Check(AdvanceFamasDerivedFrame(special.state,special.deadline,special.Host())==FamasFrameResult::UnsupportedState);
    std::cout<<"Famas derived frame: "<<cases<<" original cases, "<<callbacks<<" observed callbacks, "<<checks<<" checks\n";
}
