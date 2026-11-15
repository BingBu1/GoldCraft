// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/m16_control.hpp"
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace guest::cof;
namespace
{
using U=std::uint32_t;
U Bits(float v){return std::bit_cast<U>(v);}float Float(U v){return std::bit_cast<float>(v);}
struct Fixture
{
    MagazineActionState state;
    M16InputOwner owner;
    int fireOnEmpty{};
    float now{},entryTime{},delta{};
    unsigned change{},nth{},ordinal{},alternateButtons{};
    int alternateMove{},reject{-1},cancel{-1};
    bool valid{true},stale{};
    std::vector<U> events;
    std::vector<U> Snapshot() const
    {
        return {U(state.clip),Bits(state.primary),Bits(state.secondary),Bits(state.ownerAttack),
            U(fireOnEmpty),owner.buttons,Bits(owner.activityBlock),U(owner.moveType),Bits(now)};
    }
    bool Observe(unsigned kind)
    {
        events.push_back(kind);auto snapshot=Snapshot();events.insert(events.end(),snapshot.begin(),snapshot.end());++ordinal;
        if(ordinal==nth)
        {
            if(change&1)now+=delta;
            if(change&2)owner.buttons=alternateButtons;
            if(change&4)owner.moveType=alternateMove;
            if(change&8)owner.activityBlock=1;
            if(change&16)state.clip=75;
        }
        if(int(ordinal)==cancel)valid=false;
        return int(ordinal)!=reject;
    }
    M16InputHost Host()
    {
        return {this,[](void* p)noexcept{return static_cast<Fixture*>(p)->valid;},
            [](void* p)noexcept{auto& f=*static_cast<Fixture*>(p);return f.stale?f.entryTime:f.now;},
            [](void* p,M16InputOwner& out)noexcept{out=static_cast<Fixture*>(p)->owner;return true;},
            [](void* p,M16InputEvent event)noexcept{return static_cast<Fixture*>(p)->Observe(unsigned(event));}};
    }
};
std::vector<U> Parse(const std::string& line){std::istringstream s(line);s>>std::hex;std::vector<U> row;U v;while(s>>v)row.push_back(v);return row;}
int checks{};void Check(bool v){++checks;if(!v){std::cerr<<"input edge "<<checks<<" failed\n";std::exit(2);}}
}
int main(int argc,char** argv)
{
    if(argc<3)return 1;std::ifstream input(argv[1]),answers(argv[2]);if(!input||!answers)return 1;
    std::string line,answer;int cases{};unsigned callbacks{};
    while(std::getline(input,line))
    {
        auto row=Parse(line);if(row.size()!=17)return 3;if(!std::getline(answers,answer))return 4;
        Fixture f;f.entryTime=Float(row[0]);f.now=f.entryTime+Float(row[15]);f.state.clip=int(row[1]);
        f.state.primary=Float(row[2]);f.state.secondary=Float(row[3]);f.state.ownerAttack=Float(row[4]);f.state.loading=row[5]!=0;
        f.owner={row[16]==0xffffffff?row[6]:row[16],Float(row[7]),int(row[8])};f.fireOnEmpty=int(row[9]);
        f.change=row[10];f.nth=row[11];f.delta=Float(row[12]);f.alternateButtons=row[13];f.alternateMove=int(row[14]);
        f.stale=argc>3&&std::string(argv[3])=="--stale-time";
        Check(DispatchM16FrameInput(f.state,f.fireOnEmpty,f.Host())==M16ControlResult::Applied);
        auto got=f.Snapshot();got.insert(got.end(),f.events.begin(),f.events.end());auto wanted=Parse(answer);
        if(got!=wanted)
        {
            std::cerr<<"input case "<<cases<<" differs; sizes "<<got.size()<<' '<<wanted.size()<<'\n';
            for(std::size_t i=0;i<std::min(got.size(),wanted.size());++i)if(got[i]!=wanted[i])
                std::cerr<<i<<": "<<std::hex<<got[i]<<" expected "<<wanted[i]<<std::dec<<'\n';
            return 5;
        }
        callbacks+=f.ordinal;++cases;
    }
    if(!cases)return 6;if(std::getline(answers,line))return 7;
    for(int i=1;i<=2;++i)
    {
        Fixture f;f.now=10;f.owner.buttons=1;f.state.clip=20;f.reject=i;
        Check(DispatchM16FrameInput(f.state,f.fireOnEmpty,f.Host())==M16ControlResult::ConsumerRejected);
        Fixture c;c.now=10;c.owner.buttons=1;c.cancel=i;
        Check(DispatchM16FrameInput(c.state,c.fireOnEmpty,c.Host())==M16ControlResult::Cancelled);
    }
    Fixture f;auto missing=f.Host();missing.owner=nullptr;
    Check(DispatchM16FrameInput(f.state,f.fireOnEmpty,missing)==M16ControlResult::MissingHost);
    std::cout<<"M16 input: "<<cases<<" original cases, "<<callbacks<<" observed callbacks, "<<checks<<" checks\n";
}
