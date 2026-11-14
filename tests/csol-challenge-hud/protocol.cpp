// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/phase_snapshot.hpp"
#include "../../native/csol/scenario/custom/phase_art.hpp"
#include "../../native/csol/scenario/custom/stamina_snapshot.hpp"
#include "../../native/csol/scenario/custom/action_hud_layout.hpp"
#include "../../native/csol/scenario/core/scenario_abilities.hpp"
#include "../../native/csol/hud_style.hpp"
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
    for(unsigned i=0;i<NightAnimationFrames*3;++i)
        Check(NightFrame(std::uint64_t(i)*NightAnimationFrameMs)==i%NightAnimationFrames,"animation loops by real frame duration and bounded atlas index");
}
void Stamina()
{
    StaminaSnapshot value{1,1,37.5f,100,true,false,false};
    auto bytes=EncodeStamina(value);Check(bytes&&bytes->size()==22,"fixed22-byte authoritative stamina snapshot");
    auto read=DecodeStamina(*bytes);Check(read&&read->amount==37.5f&&read->maximum==100&&read->visible,"exact float amount round trip");
    StaminaPresentation state;Check(state.Receive(*bytes)&&state.Sample()->amount==37.5f,"first owner accepted");
    Check(!state.Receive(*bytes),"duplicate never resets stamina");
    value.revision=2;value.amount=0;value.exhausted=true;bytes=EncodeStamina(value);
    Check(state.Receive(*bytes)&&state.Sample()->exhausted,"real exhausted state retained");
    value={1,3,0,0,false,false,false};bytes=EncodeStamina(value);Check(state.Receive(*bytes)&&!state.Sample()->visible,"retirement hides old stamina");
    value={2,1,100,100,true,true,false};bytes=EncodeStamina(value);Check(state.Receive(*bytes)&&state.Sample()->infinite,"new life noon uses infinite flag");
    value.life=1;value.revision=100;bytes=EncodeStamina(value);Check(!state.Receive(*bytes),"old life rejected even with higher revision");
    value.life=3;value.amount=101;Check(!EncodeStamina(value),"amount above capacity rejected");
    value.amount=std::numeric_limits<float>::quiet_NaN();Check(!EncodeStamina(value),"nonfinite amount rejected");
    value.amount=100;value.exhausted=true;Check(!EncodeStamina(value),"infinite and exhausted conflict rejected");
    value.exhausted=false;const auto good=*EncodeStamina(value);
    for(unsigned size=0;size<good.size();++size)Check(!DecodeStamina(std::span(good).first(size)),"truncated stamina rejected");
    auto bad=good;bad[1]=std::byte{8};Check(!DecodeStamina(bad),"unknown flags rejected");
    state.Reset();Check(!state.Sample(),"reset removes previous server owner");
    for(auto [w,h]:{std::pair{640,480},{1280,720},{1920,1080},{2560,1080},{3840,2160},{7680,4320}})
    {
        const auto p=StaminaLayout(w,h);Check(p.x>=0&&p.y>=0&&p.x+p.width<=w&&p.y+p.height<=h,"stamina safe geometry across resolutions");
        Check(p.kickY+26==h-p.padding,"F hint follows user's bottom-edge arrow with safe padding");
        csol::abilities::State skill;Check(skill.Restore({1,30,15,0}),"actual skill geometry is available");
        for(bool elevated:{false,true})
        {
            const auto frame=skill.Layout(w,h,elevated);
            for(unsigned i=0;i<frame.count;++i)
            {const auto& q=frame.quads[i];Check(p.x>=q.x+q.width+p.padding||p.y>=q.y+q.height+p.padding||p.y+p.height+p.padding<=q.y,"stamina clears actual normal and raised skill rectangles");
             Check(p.kickY>=q.y+q.height+p.padding,"bottom F hint stays below actual skill rectangles");}
        }
        Check(p.x>=w/2+165+p.padding||p.y+p.height+p.padding<=h-188||p.y>=h-122,"stamina clears actual330px purchase hint");
        for(float scale:{.5f,1.f,1.5f,2.f})
        {
            const auto original=csol::hud_style::MakeLayout(w,h,scale);
            const auto custom=StaminaLayout(w,h,scale);
            const auto overlap=[&](int x,int y,int width,int height)
            {return x<original.card.x+original.card.width&&x+width>original.card.x&&y<original.card.y+original.card.height&&y+height>original.card.y;};
            Check(!overlap(custom.x,custom.y,custom.width,custom.height),"stamina clears actual weapon card across HUD scales");
            Check(!overlap(w/2-54,custom.kickY,108,26),"bottom F hint clears actual weapon card across HUD scales");
        }
    }
    const auto arrow=StaminaLayout(1280,720);
    Check(arrow.x==813&&arrow.y+arrow.height==712&&arrow.kickY==686,"1280x720 preserves screenshot's right anchor and moves both controls to bottom");
}
}
int main(){Protocol();Layouts();Icons();Stamina();std::printf("challenge phase HUD: %u checks passed\n",checks);}
