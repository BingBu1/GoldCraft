// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "../resource_refresh.hpp"
#include "../buy_menu.hpp"
#include "../scenario/core/scenario_map_vote.hpp"
#include "guest_cof_crosshair.hpp"
#include "weapon_inventory.hpp"
#include "../../../guest/cof/catalog.hpp"
#include "../../../guest/cof/crosshair_protocol.hpp"
#include "../../../guest/cof/presentation_protocol.hpp"
#include "../../../guest/cof/client/crosshair.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include <limits>

namespace csol::client
{
namespace
{
const GoldCraftConnectionAPI* connection{};
guest::cof::CrosshairReceiver receiver;
guest::cof::PresentationState presentation;
cvar_t *option{},*drawHud{};
SCREENINFO screen{};
DWORD ownerThread{};
std::uint64_t generation{},serial{1};
bool initialized{};
bool Owner() noexcept {return ownerThread&&ownerThread==GetCurrentThreadId();}
void Advance() noexcept
{if(serial)serial=serial==std::numeric_limits<std::uint64_t>::max()?0:serial+1;}
bool Synchronize() noexcept
{
    if(!Owner()||!initialized||!serial||!connection)return false;
    const auto revision=serial;
    const auto now=connection->generation();
    if(!Owner()||serial!=revision||!initialized)return false;
    if(now!=generation){Advance();generation=now;receiver.Reset();presentation={};screen={};}
    return now&&serial;
}
int Receive(const char*,int length,void* bytes)
{
    if(Synchronize()&&length>=0&&bytes)
        receiver.Receive({static_cast<const std::byte*>(bytes),std::size_t(length)});
    return 1;
}
}
void InitializeGuestCofCrosshair() {InitializeGuestCofCrosshair(GoldCraftConnectionInterface());}
void InitializeGuestCofCrosshair(const GoldCraftConnectionAPI* provider)
{
    if(ownerThread&&!Owner())return;
    Advance();initialized=false;connection=nullptr;receiver.Reset();presentation={};screen={};
    option=drawHud=nullptr;ownerThread=GetCurrentThreadId();generation=0;
    if(!serial||!provider||provider->size<sizeof(*provider)||provider->version!=1||!provider->generation||
       !api||!api->HookUserMsg||!engine.pfnHookUserMsg||!engine.pfnGetCvarPointer||
       !engine.pfnRegisterVariable||!engine.pfnFillRGBA||!engine.pfnGetScreenInfo)return;
    option=engine.pfnGetCvarPointer("cof_crosshair");
    if(!option)option=engine.pfnRegisterVariable("cof_crosshair","1",FCVAR_ARCHIVE);
    drawHud=engine.pfnGetCvarPointer("hud_draw");
    if(!option||!drawHud)return;
    const auto old=api->HookUserMsg(guest::cof::CrosshairMessage,Receive);
    if(!old)engine.pfnHookUserMsg(guest::cof::CrosshairMessage,Receive);
    else if(old!=Receive)
    {
        api->HookUserMsg(guest::cof::CrosshairMessage,old);
        throw std::runtime_error("CoF crosshair message already has a consumer");
    }
    connection=provider;initialized=true;
}
void ResetGuestCofCrosshair() noexcept
{if(Owner()){Advance();receiver.Reset();presentation={};}}
void GuestCofCrosshairVideoInit() noexcept {if(Owner()){Advance();screen={};}}
void ShutdownGuestCofCrosshair() noexcept
{
    if(!Owner())return;
    ResetGuestCofCrosshair();initialized=false;connection=nullptr;option=drawHud=nullptr;
    screen={};generation=0;ownerThread=0;
}
void GuestCofCrosshairFrame() noexcept
{
    if(!Synchronize()||!ResourceReadsAllowed())return;
    if(screen.iWidth>0&&screen.iHeight>0)return;
    SCREENINFO next{};next.iSize=sizeof(next);
    const auto revision=serial;
    if(engine.pfnGetScreenInfo(&next)&&Synchronize()&&serial==revision&&next.iWidth>0&&next.iHeight>0)
        screen=next;
}
bool GuestCofCrosshairReady() noexcept
{return Synchronize()&&option&&drawHud&&screen.iWidth>0&&screen.iHeight>0;}
void GuestCofCrosshairPresentation(const guest::cof::PresentationState& state) noexcept
{
    if(!Synchronize())return;
    Advance();presentation=state;
    receiver.Present(state.key,state.revision,state.originalWeapon==3);
}
void DrawGuestCofCrosshair(int hidden,int intermission,bool) noexcept
{
    if(!GuestCofCrosshairReady()||!ResourceReadsAllowed()||intermission||view.health<=0||
       view.spectator||view.paused||drawHud->value==0||BuyMenuVisible()||ScenarioMapVoteVisible())return;
    const auto& weapon=CurrentExtendedWeapon();
    const auto* snapshot=receiver.Current();
    if(!snapshot||!weapon.state||weapon.id!=guest::cof::Pistols[0].hostId||weapon.clip<0||
       presentation.originalWeapon!=3)return;
    std::array<guest::cof::client::CrosshairRect,3> bars;
    if(!guest::cof::client::PistolCrosshair({screen.iWidth,screen.iHeight,3,hidden,true,true,
        presentation.aimEnabled,false,option->value,snapshot->scalar},bars))return;
    const auto revision=serial;
    for(const auto& bar:bars)
    {
        if(!Synchronize()||serial!=revision||!ResourceReadsAllowed())return;
        engine.pfnFillRGBA(bar.x,bar.y,bar.width,bar.height,255,0,0,200);
    }
}
}
