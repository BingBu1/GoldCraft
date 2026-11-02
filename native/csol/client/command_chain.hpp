// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <algorithm>

namespace csol::client
{
namespace command_chain
{
struct Link { const char* name; xcommand_t* previous; };
struct Head { xcommand_t* previous; xcommand_t callback; };
inline std::vector<Link> installed;

inline xcommand_t Current(const char* name)
{
    const auto* command = api->FindCmd ? api->FindCmd(name) : nullptr;
    return command ? command->function : nullptr;
}
}

inline std::vector<command_chain::Head> CaptureChainedCommandHeads()
{
    std::vector<command_chain::Head> heads;
    for (const auto& link : command_chain::installed)
        heads.push_back({link.previous, command_chain::Current(link.name)});
    return heads;
}

inline void RefreshChainedCommandHeads(std::span<const command_chain::Head> heads)
{
    // Only a head changed by downstream HUD_Init starts a new hook chain.
    // An outer plugin may retain our callback in its private downstream pointer.
    std::erase_if(command_chain::installed, [heads](const command_chain::Link& link)
    {
        const auto before = std::find_if(heads.begin(), heads.end(),
            [&link](const command_chain::Head& head) { return head.previous == link.previous; });
        return before != heads.end() && before->callback != command_chain::Current(link.name);
    });
}

inline void InstallChainedCommand(const char* name, xcommand_t callback, xcommand_t& previous,
                                  bool createIfMissing = false)
{
    auto& installed = command_chain::installed;
    if (std::any_of(installed.begin(), installed.end(),
        [&previous](const command_chain::Link& link) { return link.previous == &previous; })) return;
    const auto old = api->HookCmd(name, callback);
    if (old != callback) previous = old;
    bool hooked = old || command_chain::Current(name) == callback;
    if (!hooked && createIfMissing && engine.pfnAddCommand)
    {
        engine.pfnAddCommand(name, callback);
        hooked = !api->FindCmd || command_chain::Current(name) == callback;
    }
    // A missing stock command can appear on a later initialization. Do not
    // mark it installed until HookCmd or AddCommand actually owns its head.
    if (hooked) installed.push_back({name, &previous});
}
}
