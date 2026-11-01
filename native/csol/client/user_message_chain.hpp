// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <algorithm>

namespace csol::client
{
namespace message_chain
{
struct Link { const char* name; pfnUserMsgHook* previous; };
struct Head { pfnUserMsgHook* previous; pfnUserMsgHook callback; };
inline std::vector<Link> installed;

inline pfnUserMsgHook Current(const char* name)
{
    const auto* message = api->FindUserMsgHook(name);
    return message ? message->function : nullptr;
}
}

inline std::vector<message_chain::Head> CaptureChainedUserMessageHeads()
{
    std::vector<message_chain::Head> heads;
    for (const auto& link : message_chain::installed)
        heads.push_back({link.previous, message_chain::Current(link.name)});
    return heads;
}

inline void RefreshChainedUserMessageHeads(std::span<const message_chain::Head> heads)
{
    // Compare around the actual downstream HUD_Init. Stock messages are
    // re-registered there; extension messages may retain their existing chain.
    // ResetHUD, video changes and shutdown do not reset this installation state.
    std::erase_if(message_chain::installed, [heads](const message_chain::Link& link)
    {
        const auto before = std::find_if(heads.begin(), heads.end(),
            [&link](const message_chain::Head& head) { return head.previous == link.previous; });
        return before != heads.end() && before->callback != message_chain::Current(link.name);
    });
}

inline void InstallChainedUserMessage(const char* name, pfnUserMsgHook callback, pfnUserMsgHook& previous)
{
    auto& installed = message_chain::installed;
    if (std::any_of(installed.begin(), installed.end(),
        [&previous](const message_chain::Link& link) { return link.previous == &previous; })) return;
    installed.push_back({name, &previous});
    const auto old = api->HookUserMsg(name, callback);
    // Install once until an observed native replacement, including when a
    // different DLL wraps us. Its private downstream pointer is not traversable.
    if (old != callback) previous = old;
    if (!old) engine.pfnHookUserMsg(name, callback);
}
}
