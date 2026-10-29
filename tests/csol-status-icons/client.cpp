// SPDX-License-Identifier: GPL-3.0-or-later
// Link the real status consumer and session.cpp. Only host callbacks and the
// drawing boundary are controlled; the production parser/state/layout run.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/status_icons.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <source_location>

namespace fixture
{
int checks{},forwards{},registrations{},prepares{},resets{},draws{},sessionResets{};
float now=10,hud=1;
int width=1280,height=720;
bool screen=true,existing=true;
std::string level="maps/zs_deadend.bsp";
cvar_t option{};
pfnUserMsgHook receiver{};
std::vector<std::byte> lastForward;
std::vector<csol::status::Quad> quads;
std::size_t count{};
void Check(bool condition,const std::source_location where=std::source_location::current())
{++checks;if(!condition)throw std::runtime_error("status check line "+std::to_string(where.line()));}
int Previous(const char* name,int size,void* data)
{
    Check(std::strcmp(name,"MPToCL2")==0);++forwards;lastForward.clear();
    if(data&&size>0){auto* p=static_cast<std::byte*>(data);lastForward.assign(p,p+size);}return 73;
}
std::vector<std::byte> Add(int id,int type=7,unsigned style=0,float duration=-1)
{
    std::vector<std::byte> out{std::byte{0x40},std::byte{0}};
    const auto word=[&](std::uint32_t value){for(int i=0;i<4;++i)out.push_back(std::byte((value>>(8*i))&255));};
    word(std::bit_cast<std::uint32_t>(id));out.push_back(std::byte(style));word(std::bit_cast<std::uint32_t>(type));word(std::bit_cast<std::uint32_t>(duration));return out;
}
std::vector<std::byte> Remove(int id)
{
    std::vector<std::byte> out{std::byte{0x40},std::byte{1}};
    for(int i=0;i<4;++i)out.push_back(std::byte((std::bit_cast<std::uint32_t>(id)>>(8*i))&255));return out;
}
int Send(const std::vector<std::byte>& bytes)
{Check(receiver!=nullptr);return receiver("MPToCL2",int(bytes.size()),const_cast<std::byte*>(bytes.data()));}
void Draw(float time)
{now=time;quads.clear();count=0;csol::client::DrawStatusIcons(time);}
void Fresh(float time=10)
{csol::client::ResetStatusIcons();option.value=1;hud=1;screen=true;width=1280;height=720;Draw(time);}
bool Near(float a,float b){return std::abs(a-b)<0.00001f;}
}
namespace csol::client
{
cl_enginefunc_t engine{};metahook_api_t* api{};
void ClearScene(){++fixture::sessionResets;}
void ResetWeapons(bool){}
void ResetCamera(){}
void ResetProjectiles(){}
void ResetCharacters(){}
void ResetStatusIconGraphics(){++fixture::resets;}
void PrepareStatusIconGraphics(){++fixture::prepares;}
void DrawStatusIconGraphics(const status::ProtectionIcons& icons,int width,int height)
{
    ++fixture::draws;fixture::count=icons.VisibleCount();
    std::array<status::Quad,status::Capacity*2> data;
    const auto size=icons.Layout(width,height,29,29,data);
    fixture::quads.assign(data.begin(),data.begin()+size);
}
}
namespace
{
using namespace fixture;using namespace csol::client;
void Wire()
{
    // Literal little-endian payload verifies signed IDs and raw float bits.
    const std::vector<std::byte> bytes{std::byte{0x40},std::byte{0},std::byte{0xfe},std::byte{0xff},std::byte{0xff},std::byte{0xff},
        std::byte{0},std::byte{7},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0x40}};
    csol::status::Message parsed;Check(csol::status::Decode(bytes,parsed)==csol::status::ParseResult::Parsed);
    Check(parsed.id==-2&&parsed.type==7&&parsed.style==0&&parsed.remaining==2);
    Fresh();const auto native=forwards;Check(Send(bytes)==1);Draw(10);Check(count==1);
    for(std::size_t n=1;n<bytes.size();++n)
        Check(Send(std::vector<std::byte>(bytes.begin(),bytes.begin()+n))==1);
    auto extra=bytes;extra.push_back(std::byte{0});Check(Send(extra)==1);
    for(float duration:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
        Check(Send(Add(8,7,0,duration))==1);
    for(auto bad:std::vector<std::vector<std::byte>>{{std::byte{0x40},std::byte{1}},{std::byte{0x40},std::byte{2},std::byte{0}}})Check(Send(bad)==1);
    Check(forwards==native);Draw(10);Check(count==1);
    const int result=existing?73:1;
    for(auto unknown:std::vector<std::vector<std::byte>>{{std::byte{0x39},std::byte{7}},{std::byte{0x40},std::byte{3},std::byte{17}},Add(9,8),Add(9,7,1)})
    {const auto before=forwards;Check(Send(unknown)==result);Check(forwards==before+(existing?1:0));if(existing)Check(lastForward==unknown);}
    const auto before=forwards;Check(receiver("MPToCL2",15,nullptr)==result&&forwards==before+(existing?1:0));
    Draw(10);Check(count==1);
    Check(Send(Remove(-2))==result);Draw(10);Check(!count);
}
void OwnershipAndOptions()
{
    Fresh();option.value=0;Send(Add(1));Draw(10);Check(!count);
    option.value=0.0001f;Send(Add(1));Draw(10);Check(!count);
    option.value=1;Draw(10);Check(!count); // disabled additions are not replayed
    Send(Add(11));Send(Add(11));Draw(10);Check(count==2&&quads.size()==2);
    option.value=0;Draw(10);Check(count==2);Send(Remove(11));Draw(10);Check(count==1);
    Send({std::byte{0x40},std::byte{2}});Draw(10);Check(!count);
    option.value=1;for(int id=0;id<35;++id)Send(Add(id));Draw(10);Check(count==30&&quads.size()==30);
    Check(quads[0].left==463&&quads[0].top==692&&quads[0].right==492&&quads[0].bottom==721);
    Check(quads[9].left==787&&quads[10].left==463&&quads[10].top==656&&quads[29].top==620);
    Send(Remove(0));Send(Add(100));Draw(10);Check(count==30);
    Send(Remove(34));Draw(10);Check(count==30); // overflow add never became owned
    const auto previousDraws=draws;hud=0;Draw(20);Check(draws==previousDraws);hud=1;Draw(20);Check(count==30);
}
void LifetimeAndLayout()
{
    Fresh(50);Send(Add(7,7,0,2));Draw(50);Check(count==1&&quads.size()==1&&quads[0].bottom-quads[0].top==29);
    Draw(51);Check(count==1&&quads.size()==2);
    Check(quads[0].top==692&&quads[0].bottom==707&&Near(quads[0].alpha,70.0f/255));
    Check(quads[1].top==707&&quads[1].bottom==721&&quads[1].alpha==1&&Near(quads[1].v0,15.0f/29));
    Check(quads[0].u1==1&&quads[1].v1==1); // original 29px is clipped, never stretched to 30
    Draw(50.5f);Check(quads.size()==2&&quads[0].bottom==707); // negative delta clamps to zero
    Draw(51.5f);Check(count==1&&quads.size()==1&&Near(quads[0].alpha,70.0f/255));
    Draw(51.51f);Check(!count); // ratio zero stays visible until next positive delta
    Fresh();Send(Add(8,7,0,0));Draw(10);Check(!count);
    Send(Add(8,7,0,std::numeric_limits<float>::max()));Draw(1000);Check(count==1&&quads[0].alpha==1);
    width=480;height=360;Draw(1000);Check(quads[0].left==63&&quads[0].top==332&&quads[0].right-quads[0].left==29);
    screen=false;const auto prior=draws;Draw(1001);Check(draws==prior);screen=true;
    Fresh();Send(Add(9,7,0,2));hud=0;Draw(13);hud=1;Draw(13);Check(!count);
}
void Lifecycle()
{
    Fresh(100);Send(Add(1,7,0,2));const auto prep=prepares;
    StatusIconsFrame();Check(prepares==prep+1);const auto reset=resets;
    StatusIconsVideoInit();Check(resets==reset+1);Draw(101);Check(count==1&&quads.size()==2);
    level="maps/zs_deadend.bsp";SynchronizeFrameSession();Draw(101);Check(count==1);
    const auto session=sessionResets;level="maps/cs_assault.bsp";Send(Add(2));Draw(102);
    Check(sessionResets==session+1&&count==1&&quads.size()==1&&quads[0].alpha==1);
    level.clear();SynchronizeFrameSession();Draw(103);Check(!count);
    level="maps/zs_deadend.bsp";Send(Add(3));now=200;SynchronizeFrameSession();now=199.8f;SynchronizeFrameSession();Draw(104);Check(count==1);
    now=100;SynchronizeFrameSession();Draw(100);Check(!count);
    Send(Add(4));ShutdownStatusIcons();Draw(100);Check(!count);
}
}
int main(int argc,char** argv)
{
    using namespace fixture;using namespace csol::client;
    try
    {
        existing=argc<2||std::strcmp(argv[1],"--no-previous")!=0;option.value=1;
        metahook_api_t hooks{};api=&hooks;
        hooks.HookUserMsg=[](const char* name,pfnUserMsgHook handler)->pfnUserMsgHook
        {Check(!std::strcmp(name,"MPToCL2"));const auto old=receiver?receiver:existing?Previous:nullptr;receiver=handler;return old;};
        engine.pfnHookUserMsg=[](const char*,pfnUserMsgHook handler)->int{++registrations;receiver=handler;return 1;};
        engine.pfnGetCvarPointer=[](const char* name)->cvar_t*{Check(!std::strcmp(name,"cl_weaponparts_hud"));return existing?&option:nullptr;};
        engine.pfnRegisterVariable=[](const char* name,const char* value,int flags)->cvar_t*
        {Check(!std::strcmp(name,"cl_weaponparts_hud")&&!std::strcmp(value,"1")&&(flags&FCVAR_ARCHIVE));return &option;};
        engine.pfnGetCvarFloat=[](const char* name)->float{Check(!std::strcmp(name,"hud_draw"));return hud;};
        engine.pfnGetScreenInfo=[](SCREENINFO* out)->int{Check(out->iSize==sizeof(*out));out->iWidth=width;out->iHeight=height;return screen;};
        engine.pfnGetLevelName=[]()->const char*{return level.c_str();};engine.GetClientTime=[]()->float{return now;};
        InitializeStatusIcons();InitializeStatusIcons();Check(registrations==(existing?0:1));
        SynchronizeSession();Wire();OwnershipAndOptions();LifetimeAndLayout();Lifecycle();
        std::cout<<"status consumer/session checks: "<<checks<<'\n';return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
