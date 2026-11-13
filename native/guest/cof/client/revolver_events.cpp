// SPDX-License-Identifier: GPL-3.0-or-later
#include "revolver_events.hpp"
#include <metahook.h>
#include <event_api.h>
#include <event_args.h>
#include <array>
#include <cstring>
namespace guest::cof::client
{
RevolverEventResult RevolverShellEvent(const RevolverEventHost& host,const event_args_s& event) noexcept
{
    const auto call=host;
    if(!call.engine||!call.current||!call.shellSample||!call.shellSample[0])return RevolverEventResult::MissingBackend;
    if(!call.current(call.context))return RevolverEventResult::Cancelled;
    auto& engine=*call.engine;
    if(!engine.GetEntityByIndex)return RevolverEventResult::MissingBackend;
    const int index=event.entindex;
    auto* entity=engine.GetEntityByIndex(index);
    if(!call.current(call.context))return RevolverEventResult::Cancelled;
    if(!entity)return RevolverEventResult::Ignored;
    auto* events=engine.pEventAPI;
    if(!events||!events->EV_PlaySound||!engine.pfnRandomLong)return RevolverEventResult::MissingBackend;
    const auto play=events->EV_PlaySound;
    std::array<float,3> origin;
    std::memcpy(origin.data(),entity->origin,sizeof(origin));
    const int pitch=engine.pfnRandomLong(0,30)+100;
    if(!call.current(call.context))return RevolverEventResult::Cancelled;
    play(index,origin.data(),0,call.shellSample,1.0F,.8F,0,pitch);
    return call.current(call.context)?RevolverEventResult::Issued:RevolverEventResult::Cancelled;
}
}
