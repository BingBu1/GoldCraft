// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/resource_refresh.hpp"
#include "../../native/csol/client/weapons/guest_cof_crosshair.hpp"
#include "../../native/csol/client/weapons/weapon_inventory.hpp"
#include "../../native/guest/cof/presentation_protocol.hpp"
#include "../../native/guest/cof/crosshair_protocol.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <thread>
namespace csol::client
{metahook_api_t* api{};cl_enginefunc_t engine{};ref_params_t view{};}
namespace
{
using namespace csol::client;
using namespace guest::cof;
unsigned checks{},failures{},lookups{},screens{},fills{};
std::uint64_t generation{3};
bool menu{},vote{},retireOnFill{},replaceScreen{};
cvar_t option{},hudDraw{};
csol::WeaponCurrent current;
pfnUserMsgHook callback{};
struct Rect {int x,y,w,h;};
std::array<Rect,3> rectangles;
void Check(bool value,int line){++checks;if(!value){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(...) Check((__VA_ARGS__),__LINE__)
std::uint64_t Generation(){return generation;}
pfnUserMsgHook Hook(const char* name,pfnUserMsgHook next)
{CHECK(!std::strcmp(name,CrosshairMessage));const auto old=callback;if(old)callback=next;return old;}
int RegisterMessage(const char* name,pfnUserMsgHook next)
{CHECK(!std::strcmp(name,CrosshairMessage));callback=next;return 1;}
cvar_t* Lookup(const char* name)
{++lookups;if(!std::strcmp(name,"cof_crosshair"))return &option;CHECK(!std::strcmp(name,"hud_draw"));return &hudDraw;}
int Screen(SCREENINFO* value)
{CHECK(value->iSize==sizeof(*value));++screens;value->iWidth=1920;value->iHeight=1200;if(replaceScreen)++generation;return 1;}
void Fill(int x,int y,int w,int h,int r,int g,int b,int a)
{CHECK(r==255&&g==0&&b==0&&a==200);if(fills<3)rectangles[fills]={x,y,w,h};++fills;if(retireOnFill)++generation;}
PresentationState state{{9,10,11},2,3,false,0,0};
CrosshairSnapshot snap{state.key,state.revision,1,3};
void Present(){GuestCofCrosshairPresentation(state);}
void Send(CrosshairSnapshot value)
{CrosshairPacket bytes{};CHECK(EncodeCrosshair(value,bytes));CHECK(callback(CrosshairMessage,int(bytes.size()),bytes.data())==1);}
void Draw(unsigned expected,int hidden=0,int intermission=0)
{fills=0;DrawGuestCofCrosshair(hidden,intermission,true);CHECK(fills==expected);}
int Other(const char*,int,void*){return 1;}
}
namespace csol::client
{
const WeaponCurrent& CurrentExtendedWeapon(){return current;}
bool BuyMenuVisible(){return menu;}
bool ScenarioMapVoteVisible(){return vote;}
}
int main()
{
    GoldCraftConnectionAPI provider{sizeof(provider),1,Generation};
    InitializeGuestCofCrosshair(nullptr);CHECK(!GuestCofCrosshairReady());
    metahook_api_t table{};api=&table;table.HookUserMsg=Hook;
    engine.pfnHookUserMsg=RegisterMessage;engine.pfnGetCvarPointer=Lookup;
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{CHECK(false);return nullptr;};
    engine.pfnGetScreenInfo=Screen;engine.pfnFillRGBA=Fill;
    option.value=hudDraw.value=1;current.id=896;current.state=1;current.clip=15;view.health=100;
    InitializeGuestCofCrosshair(&provider);GuestCofCrosshairFrame();CHECK(GuestCofCrosshairReady());
    Present();Draw(0);Send(snap);Draw(3);
    CHECK(rectangles[0].x==969&&rectangles[0].y==600&&rectangles[0].w==19&&rectangles[0].h==1);
    CHECK(rectangles[1].x==932&&rectangles[1].y==600&&rectangles[2].x==959&&rectangles[2].y==609);
    const auto cold=lookups,prepared=screens;
    for(int i=0;i<100;++i){GuestCofCrosshairFrame();Draw(3);}
    CHECK(lookups==cold&&screens==prepared);
    auto stale=snap;stale.scalar=9;Send(stale);Draw(3);CHECK(rectangles[0].x==969);
    ++stale.sequence;Send(stale);Draw(3);CHECK(rectangles[0].x==987);
    for(int mask:{1,4,8,13})Draw(0,mask);Draw(3,2);Draw(0,0,1);
    for(auto* value:{&view.spectator,&view.paused}){*value=1;Draw(0);*value=0;}
    view.health=0;Draw(0);view.health=100;current.id=16;Draw(0);current.id=896;
    current.clip=-1;Draw(0);current.clip=15;
    option.value=0;Draw(0);option.value=1;hudDraw.value=0;Draw(0);hudDraw.value=1;
    menu=true;Draw(0);menu=false;vote=true;Draw(0);vote=false;
    resource_refresh_detail::readsAllowed=false;Draw(0);resource_refresh_detail::readsAllowed=true;
    state.aimEnabled=true;++state.revision;Present();snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(0);
    state.aimEnabled=false;++state.revision;Present();Send(snap);Draw(0);
    snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(3);
    // Actual P345 identity shares the proved red pistol crosshair geometry,
    // while its owning presentation/revision must match the active inventory.
    current.id=897;Draw(0);
    state.originalWeapon=7;++state.revision;Present();
    snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(3);
    state.aimEnabled=true;++state.revision;Present();snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(0);
    // The real original leftwheel shares the three-bar stage. A previous
    // pistol presentation must not render after switching its host identity.
    current.id=898;current.clip=5;Draw(0);
    state.originalWeapon=21;state.aimEnabled=false;state.revolverAngle=144;++state.revision;Present();
    snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(3);
    current.clip=75;Draw(3);
    // Reliable cylinder state may precede the next unreliable spread snapshot.
    // Keep only the last actually accepted value for this same weapon/aim.
    state.revolverAngle=132;++state.revision;Present();Draw(3);
    const auto previousGap=rectangles[0].x;
    auto oldAngle=snap;oldAngle.scalar=10;++oldAngle.sequence;Send(oldAngle);Draw(3);
    CHECK(rectangles[0].x==previousGap); // Older revision still cannot change it.
    snap.presentationRevision=state.revision;snap.sequence=oldAngle.sequence+1;snap.scalar=9;Send(snap);Draw(3);
    CHECK(rectangles[0].x==987);
    state.revolverAngle=120;++state.revision;Present();Draw(3);CHECK(rectangles[0].x==987);
    const auto stableCylinderLookups=lookups,stableCylinderScreens=screens;
    for(int i=0;i<100;++i){GuestCofCrosshairFrame();Draw(3);}
    CHECK(lookups==stableCylinderLookups&&screens==stableCylinderScreens);
    ++state.key.generation;++state.revision;Present();Draw(0);Send(snap);Draw(0);
    snap.key=state.key;snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(3);
    state.aimEnabled=true;++state.revision;Present();snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(0);
    state.aimEnabled=false;++state.revision;Present();Draw(0); // Aim exit needs its own accepted snapshot.
    snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(3);
    state.originalWeapon=3;state.revolverAngle.reset();current.id=896;++state.revision;Present();Draw(0);
    snap.presentationRevision=state.revision;++snap.sequence;Send(snap);Draw(3);
    GuestCofCrosshairVideoInit();CHECK(!GuestCofCrosshairReady());GuestCofCrosshairFrame();Draw(3);
    auto wrong=snap;++wrong.key.lifeEpoch;++wrong.sequence;Send(wrong);Draw(3);
    ++generation;Draw(0);GuestCofCrosshairFrame();Draw(0);Present();Send(snap);Draw(3);
    retireOnFill=true;Draw(1);retireOnFill=false;Draw(0);
    GuestCofCrosshairFrame();Present();Send(snap);Draw(3);
    bool foreign=true;
    std::thread other([&]{foreign=GuestCofCrosshairReady();ResetGuestCofCrosshair();ShutdownGuestCofCrosshair();});other.join();
    CHECK(!foreign&&GuestCofCrosshairReady());Draw(3);
    state.originalWeapon=0;++state.revision;Present();Draw(0);
    callback=Other;bool conflict=false;try{InitializeGuestCofCrosshair(&provider);}catch(...){conflict=true;}
    CHECK(conflict&&callback==Other&&!GuestCofCrosshairReady());callback=nullptr;
    InitializeGuestCofCrosshair(&provider);replaceScreen=true;GuestCofCrosshairFrame();replaceScreen=false;
    CHECK(!GuestCofCrosshairReady());GuestCofCrosshairFrame();CHECK(GuestCofCrosshairReady());
    ShutdownGuestCofCrosshair();CHECK(!GuestCofCrosshairReady());
    // Protocol scope/replay and malformed input transaction guarantees.
    CrosshairReceiver receiver;CrosshairPacket packet;auto valid=snap;valid.key={1,2,3};valid.presentationRevision=2;valid.sequence=4;
    CHECK(EncodeCrosshair(valid,packet));CHECK(receiver.Receive(packet)==CrosshairAcceptance::Inactive);
    receiver.Present(valid.key,2,true);CHECK(receiver.Receive(packet)==CrosshairAcceptance::Accepted);
    CHECK(receiver.Receive(packet)==CrosshairAcceptance::Replayed);
    auto saved=*receiver.Current();for(std::size_t n=0;n<packet.size();++n)
        CHECK(receiver.Receive({packet.data(),n})==CrosshairAcceptance::Invalid&&*receiver.Current()==saved);
    packet[1]=std::byte{11};CHECK(receiver.Receive(packet)==CrosshairAcceptance::Invalid&&*receiver.Current()==saved);
    std::uint8_t quantized=99;CHECK(QuantizeCrosshair(3.99F,quantized)&&quantized==3);
    for(float invalid:{-1.0F,11.0F,std::bit_cast<float>(0x7fc12345u),std::bit_cast<float>(0x7f800000u)})
    {quantized=99;CHECK(!QuantizeCrosshair(invalid,quantized)&&quantized==99);}
    std::printf("actual crosshair bridge/codec checks=%u failures=%u; no physical device/render/gameplay acceptance\n",checks,failures);
    return failures?1:0;
}
