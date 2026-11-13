// SPDX-License-Identifier: GPL-3.0-or-later
#include "guest_cof_effects.hpp"
#include "../runtime.hpp"
#include "../../../guest/cof/resource_catalog.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include "../../../include/goldcraft/sound_precache_api.h"
#include <event_api.h>
#include <limits>
#include <string_view>

namespace csol::client
{
namespace
{
using guest::cof::client::RevolverEventResult;
using namespace guest::cof::resources;
constexpr std::string_view sample=RevolverSounds[static_cast<unsigned>(RevolverSound::Shell)].host;
const GoldCraftConnectionAPI* connection{};
DWORD ownerThread{};
std::uint64_t serial{1},preparedGeneration{};
bool registered{},prepared{};
RevolverEventResult last{RevolverEventResult::MissingBackend};
bool Owner() noexcept{return ownerThread&&ownerThread==GetCurrentThreadId();}
struct Lease
{
    std::uint64_t serial,generation;
    const GoldCraftConnectionAPI* connection;
};
bool Current(void* opaque) noexcept
{
    const auto& lease=*static_cast<const Lease*>(opaque);
    return Owner()&&registered&&prepared&&lease.serial&&lease.serial==serial&&
        lease.connection==connection&&lease.generation&&lease.generation==preparedGeneration&&
        connection->generation()==lease.generation&&lease.serial==serial;
}
void Receive(event_args_s* event)
{
    if(!event||!GuestCofRevolverEventsReady())return;
    Lease lease{serial,preparedGeneration,connection};
    const auto result=guest::cof::client::RevolverShellEvent({&engine,sample.data(),&lease,Current},*event);
    if(Owner()&&lease.serial==serial)last=result;
}
}
void InvalidateGuestCofRevolverEvents() noexcept
{
    if(!Owner())return;
    if(serial)serial=serial==std::numeric_limits<std::uint64_t>::max()?0:serial+1;
    prepared=false;preparedGeneration=0;last=RevolverEventResult::MissingBackend;
}
bool InitializeGuestCofRevolverEvents() noexcept
{return InitializeGuestCofRevolverEvents(GoldCraftConnectionInterface());}
bool InitializeGuestCofRevolverEvents(const GoldCraftConnectionAPI* provider) noexcept
{
    if((ownerThread&&!Owner())||!serial||!provider||provider->size<sizeof(*provider)||
        provider->version!=1||!provider->generation||!engine.pfnHookEvent)return false;
    if(registered&&connection==provider)return true;
    ownerThread=GetCurrentThreadId();InvalidateGuestCofRevolverEvents();connection=provider;
    registered=false;
    try{engine.pfnHookEvent(RevolverEvents[2].host,Receive);registered=true;}
    catch(...){connection=nullptr;return false;}
    return true;
}
bool PrepareGuestCofRevolverEvents(const GoldCraftSoundPrecacheAPI* sounds) noexcept
{
    if(!Owner()||!registered||!connection)return false;
    InvalidateGuestCofRevolverEvents();
    if(!serial||!sounds||sounds->size<sizeof(*sounds)||sounds->version!=1||!sounds->inspect||
        !engine.GetEntityByIndex||!engine.pfnRandomLong||!engine.pEventAPI||!engine.pEventAPI->EV_PlaySound)return false;
    const auto scope=serial,generation=connection->generation();
    if(!generation)return false;
    const auto flags=sounds->inspect(generation,sample.data(),std::uint32_t(sample.size()));
    if(!Owner()||serial!=scope||!connection||connection->generation()!=generation||serial!=scope||
        !(flags&GoldCraftSoundSignonRegistered))return false;
    preparedGeneration=generation;prepared=true;return true;
}
bool GuestCofRevolverEventsReady() noexcept
{
    if(!Owner()||!registered||!prepared||!connection)return false;
    const Lease lease{serial,preparedGeneration,connection};
    return Current(const_cast<Lease*>(&lease));
}
RevolverEventResult GuestCofLastRevolverEvent() noexcept
{return Owner()?last:RevolverEventResult::MissingBackend;}
void ShutdownGuestCofRevolverEvents() noexcept
{
    if(!Owner())return;
    InvalidateGuestCofRevolverEvents();registered=false;connection=nullptr;ownerThread=0;
}
}
