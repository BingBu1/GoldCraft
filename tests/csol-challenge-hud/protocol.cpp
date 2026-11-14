// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/phase_snapshot.hpp"
#include "../../native/csol/scenario/custom/phase_art.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace csol::scenario::custom;
namespace
{
unsigned checks{};
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"phase HUD: %s\n",why);std::exit(1);}}
void Protocol()
{
    PhaseSnapshot noon{Phase::Noon,true,1,1,180};
    auto bytes=EncodePhase(noon);Check(bytes&&bytes->size()==19,"fixed19-byte host protocol");
    const auto decoded=DecodePhase(*bytes);
    Check(decoded&&decoded->phase==Phase::Noon&&decoded->preparing&&decoded->revision==1&&decoded->challenge==1&&decoded->remaining==180,
        "round-trip retains all authoritative fields");
    PhasePresentation state;
    Check(state.Receive(*bytes,10),"first current-server snapshot accepted");
    Check(state.Sample(100)->remaining==180,"preparation does not consume server-owned countdown");
    Check(!state.Receive(*bytes,11),"duplicate revision cannot reset the countdown");
    noon.revision=2;noon.preparing=false;bytes=EncodePhase(noon);
    Check(state.Receive(*bytes,100),"server thaw starts displayed countdown");
    Check(state.Sample(160)->remaining==120,"only current phase counts down");
    Check(state.Sample(400)->remaining==0&&state.Sample(400)->phase==Phase::Noon,"expired packet never predicts the next phase");
    PhaseSnapshot dusk{Phase::Dusk,false,3,1,120};bytes=EncodePhase(dusk);
    Check(state.Receive(*bytes,400),"only server dusk message changes phase");
    noon.revision=4;bytes=EncodePhase(noon);
    Check(!state.Receive(*bytes,401),"newer wire revision cannot roll the same challenge back to noon");
    PhaseSnapshot night{Phase::Night,false,4,1,0};bytes=EncodePhase(night);
    Check(state.Receive(*bytes,401)&&state.Sample(10000)->phase==Phase::Night&&state.Sample(10000)->remaining==0,"eternal night has no invented countdown");
    PhaseSnapshot ended{Phase::Ended,false,5,1,0};bytes=EncodePhase(ended);
    Check(state.Receive(*bytes,402),"result retires displayed challenge");
    noon.challenge=2;noon.revision=6;noon.preparing=true;bytes=EncodePhase(noon);
    Check(state.Receive(*bytes,403),"new server challenge can legitimately return to pending noon");
    dusk.revision=7;bytes=EncodePhase(dusk);Check(!state.Receive(*bytes,404),"old challenge remains rejected even with a later revision");
    Check(!state.Sample(402)&&!state.Sample(std::numeric_limits<double>::quiet_NaN()),"invalid presentation clock cannot expose future data");
    state.Reset();Check(!state.Sample(500),"connection reset clears phase owner");
    for(unsigned index=0;index<19;++index)
    {
        std::array<std::byte,19> truncated{};
        Check(!DecodePhase(std::span(truncated).first(index)),"every truncated message rejected");
    }
    const auto good=*EncodePhase(noon);
    for(const unsigned index:{0u,1u,2u}){auto bad=good;bad[index]=std::byte{255};Check(!DecodePhase(bad),"bad version/phase/boolean cannot enter state");}
    noon.remaining=std::numeric_limits<float>::quiet_NaN();Check(!EncodePhase(noon),"nonfinite duration rejected");
    noon.remaining=86401;Check(!EncodePhase(noon),"duration bound matches configuration");
    night.remaining=1;Check(!EncodePhase(night),"night cannot advertise a timer");
    ended.preparing=true;Check(!EncodePhase(ended),"ended message cannot be preparing");
    // Serial arithmetic remains valid across revision wrap; map/challenge
    // monotonicity is checked independently of the transport counter.
    state.Reset();night.remaining=0;night.revision=0xffffffffu;bytes=EncodePhase(night);
    Check(state.Receive(*bytes,1),"high initial revision accepted");night.revision=1;bytes=EncodePhase(night);
    Check(state.Receive(*bytes,2),"serial revision wrap accepted without resetting time authority");
}
void Layouts()
{
    for(const auto [w,h]:{std::pair{640,480},{800,600},{1024,768},{1280,720},{1920,1080},{1920,1200},{2560,1080},{3840,2160},{7680,4320}})
    {
        const auto p=PhaseLayout(w,h);
        Check(p.x>=0&&p.y>=0&&p.x+p.width<=w&&p.y+p.height<=h,"panel stays within current viewport");
        Check(p.y>int(11+h*.1380208283662796f),"phase card starts below original Boss panel");
        Check(p.icon>0&&p.icon<=p.height&&p.icon+p.padding<p.width,"square icon retains equal axes and panel fit");
        Check(p.fontHeight>=12&&p.fontHeight<=32&&p.fontHeight%2==0,"font recipe comes from eleven bounded resolution tiers");
    }
    Check(!PhaseLayout(0,480).width&&!PhaseLayout(640,0).width&&!PhaseLayout(20000,800).width,"invalid viewport cannot publish geometry");
    const auto first=PhaseLayout(1280,720),wide=PhaseLayout(2560,1080),again=PhaseLayout(1280,720);
    Check(first.x==again.x&&first.y==again.y&&first.icon==again.icon&&wide.x>first.x,"resolution round-trip does not accumulate offsets");
}
void Icons()
{
    std::array<PhaseIcon,3> images;
    for(unsigned i=0;i<3;++i)
    {
        images[i]=MakePhaseIcon(Phase(i));unsigned nonzero{},transparent{},partial{};
        for(unsigned p=3;p<images[i].size();p+=4){nonzero+=images[i][p]!=0;transparent+=images[i][p]==0;partial+=images[i][p]>0&&images[i][p]<255;}
        Check(nonzero>100&&transparent>100&&partial>0,"procedural icon has real transparency and antialiased edges");
    }
    Check(images[0]!=images[1]&&images[1]!=images[2]&&images[0]!=images[2],"three stages have distinct artwork");
    const auto ended=MakePhaseIcon(Phase::Ended);
    Check(std::all_of(ended.begin(),ended.end(),[](auto value){return value==0;}),"ended phase creates no icon");
}
}
int main(){Protocol();Layouts();Icons();std::printf("challenge phase HUD: %u checks passed\n",checks);}
