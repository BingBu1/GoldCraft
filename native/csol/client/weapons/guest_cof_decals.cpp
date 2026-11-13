// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof_decals.hpp"
#include "../../../guest/cof/resource_catalog.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include <limits>
namespace csol::client
{
namespace
{
using guest::cof::client::CustomDecalResult;
guest::cof::client::CustomDecals decals;
const GoldCraftConnectionAPI* connection{};
const model_s* preparedWorld{};
DWORD ownerThread{};
std::uint64_t observedGeneration{},serial{1};
cvar_t* enabled{};
GuestCofDecalState state{};
bool preparing{};
bool Owner() noexcept {return ownerThread&&ownerThread==GetCurrentThreadId();}
std::uint64_t Generation() noexcept {return connection?connection->generation():0;}
void Retire() noexcept
{
    if(serial)serial=serial==std::numeric_limits<std::uint64_t>::max()?0:serial+1;
    preparedWorld=nullptr;decals.ClearMap();
    const bool released=decals.ReleaseGL();state={released?GuestCofDecalStatus::Cancelled:GuestCofDecalStatus::WrongContext,observedGeneration};
}
bool Synchronize() noexcept
{
    if(!Owner())return false;const auto generation=Generation();
    if(generation!=observedGeneration){Retire();observedGeneration=generation;state.generation=generation;}
    return generation!=0&&serial!=0;
}
int Random(void*,int minimum,int maximum) {return engine.pfnRandomLong(minimum,maximum);}
guest::cof::client::CustomDecalRandom RandomSource() noexcept {return {nullptr,engine.pfnRandomLong?Random:nullptr};}
int Receive(const char*,int size,void* data)
{
    if(!Synchronize()||!enabled||!data||size<0)return 1;
    const auto before=serial;const auto generation=observedGeneration;
    const auto result=decals.Receive({static_cast<const std::uint8_t*>(data),std::size_t(size)},enabled->value,RandomSource());
    if(before==serial&&generation==Generation())state.message=result;
    return 1;
}
}
void InitializeGuestCofDecals(){InitializeGuestCofDecals(GoldCraftConnectionInterface());}
void InitializeGuestCofDecals(const GoldCraftConnectionAPI* provider)
{
    if(ownerThread&&!Owner())throw std::runtime_error("CoF decal initialization is on the wrong thread");
    ownerThread=GetCurrentThreadId();Retire();
    connection=provider&&provider->size>=sizeof(*provider)&&provider->version==1&&provider->generation?provider:nullptr;
    observedGeneration=Generation();state.generation=observedGeneration;
    enabled=engine.pfnGetCvarPointer("gl_customdecals");if(!enabled)enabled=engine.pfnRegisterVariable("gl_customdecals","1",FCVAR_ARCHIVE);
    constexpr auto* message=guest::cof::resources::CustomDecalMessage;
    const auto old=api->HookUserMsg(message,Receive);
    if(!old)engine.pfnHookUserMsg(message,Receive);
    else if(old!=Receive){api->HookUserMsg(message,old);throw std::runtime_error("CoF decal message already has a consumer");}
    state.status=enabled&&connection&&engine.pfnRandomLong?GuestCofDecalStatus::Armed:GuestCofDecalStatus::MissingBackend;
}
bool PrepareGuestCofDecalResources(const std::array<std::span<const std::uint8_t>,5>& images) noexcept
{
    if(!Synchronize()||preparing||!enabled||!engine.pfnRandomLong)return false;
    const auto before=serial,generation=observedGeneration;preparing=true;bool ready=false;
    try {ready=decals.InitializeGL(images);}catch(...){ready=false;}
    preparing=false;
    if(before!=serial||generation!=Generation()){Retire();return false;}
    state.resourcesPrepared=ready;state.coreProfile=ready&&decals.CoreProfile();state.status=ready?(state.mapPrepared?GuestCofDecalStatus::Prepared:GuestCofDecalStatus::ResourcesPrepared):GuestCofDecalStatus::ResourceFailed;
    return ready;
}
bool PrepareGuestCofDecalMap(model_s& world,bool hl25) noexcept
{
    if(!Synchronize()||preparing)return false;
    const auto before=serial,generation=observedGeneration;preparing=true;bool ready=false;
    try {ready=decals.PrepareMap(world,hl25);}catch(...){ready=false;}
    preparing=false;
    if(before!=serial||generation!=Generation()){Retire();return false;}
    preparedWorld=ready?&world:nullptr;state.mapPrepared=ready;
    state.status=ready?(state.resourcesPrepared?GuestCofDecalStatus::Prepared:GuestCofDecalStatus::Armed):GuestCofDecalStatus::MapFailed;
    if(ready&&enabled)decals.FlushPending(enabled->value,RandomSource());
    return ready&&before==serial&&generation==Generation();
}
void DrawGuestCofDecals(const model_s& world,const guest::cof::client::CustomDecalFrame& frame) noexcept
{
    if(!Synchronize()||!enabled||!state.resourcesPrepared||!state.mapPrepared||preparedWorld!=&world)return;
    state.draw=decals.Draw(frame,enabled->value);
}
void PrepareGuestCofDecalFrame() noexcept
{
    if(Synchronize()&&state.resourcesPrepared&&state.mapPrepared)decals.PrepareFrameGL();
}
void InvalidateGuestCofDecalResources() noexcept {if(Owner())Retire();}
void ClearGuestCofDecalMap() noexcept
{
    if(!Owner())return;
    if(serial)serial=serial==std::numeric_limits<std::uint64_t>::max()?0:serial+1;
    preparedWorld=nullptr;decals.ClearMap();state.mapPrepared=false;
    state.status=state.resourcesPrepared?GuestCofDecalStatus::ResourcesPrepared:GuestCofDecalStatus::Armed;
}
bool GuestCofDecalsVideoInit() noexcept
{
    if(!Owner())return false;Retire();return state.status!=GuestCofDecalStatus::WrongContext;
}
void ShutdownGuestCofDecals() noexcept
{
    if(ownerThread&&!Owner())return;Retire();connection=nullptr;observedGeneration=0;ownerThread=0;
}
GuestCofDecalState GuestCofDecalsState() noexcept
{
    if(!Synchronize())return {};auto result=state;result.marks=decals.Count();result.pending=decals.Pending();return result;
}
}
