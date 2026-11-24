// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../command_chain.hpp"
#include "../../user_message_chain.hpp"
#include "standalone_movement_input.hpp"
#include "../../../scenario/custom/standalone_input.hpp"
#include <charconv>
#include <cstring>
#include <ostream>

namespace csol::client
{
namespace
{
namespace custom = csol::scenario::custom;
custom::StandaloneInputAuthority authority;
custom::StandaloneSprintEdge sprint;
custom::StandaloneInputProbe probe;
DWORD ownerThread{};
xcommand_t previousDown{}, previousUp{};
pfnUserMsgHook previousMessage{}, previousInit{};
std::uint64_t accepted{}, rejected{}, sent{}, consumed{};
bool initialized{}, commandsReady{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
void Queue(const char* name, std::uint64_t life, std::optional<std::uint32_t> revision = {})
{
    std::array<char, 64> text{};
    const auto length = std::strlen(name);
    std::memcpy(text.data(), name, length); text[length] = ' ';
    auto end = std::to_chars(text.data() + length + 1, text.data() + text.size() - 2, life).ptr;
    if (revision)
    {
        *end++ = ' ';
        end = std::to_chars(end, text.data() + text.size() - 2, *revision).ptr;
    }
    *end = '\n'; end[1] = 0;
    ++sent;
    engine.pfnClientCmd(text.data());
}
const custom::StandaloneInputGrant* ActiveSprintGrant() noexcept
{
    if (!Owner() || !initialized || !commandsReady || !engine.pfnClientCmd) return nullptr;
    const auto* grant = authority.Current();
    return grant && grant->enabled ? grant : nullptr;
}
bool Dispatch(int state)
{
    const auto* grant = ActiveSprintGrant();
    if (!grant) return false;
    const auto life = grant->life;
    return sprint.Dispatch(state, [life](const char* name)
    {
        // HW3782F6C0 appends to the client command buffer, including newline.
        // The extra life argument is a host authority token, not original CSO.
        Queue(name, life);
    });
}
void Down()
{
    if (Dispatch(1)) { ++consumed; return; }
    if (previousDown) previousDown();
}
void Up()
{
    if (Dispatch(0)) { ++consumed; return; }
    if (previousUp) previousUp();
}
int Receive(const char* name, int size, void* data)
{
    if (Owner() && initialized)
    {
        // Invalid packets cannot retire the current connection as a side
        // effect. Synchronization is needed before admitting a valid grant.
        const auto bytes = data && size == int(custom::StandaloneInputMessageSize) ?
            std::span(static_cast<const std::byte*>(data), std::size_t(size)) : std::span<const std::byte>{};
        const auto challenge = custom::DecodeStandaloneInputProbe(bytes);
        const auto grant = custom::DecodeStandaloneInput(bytes);
        if (!challenge && !grant) ++rejected;
        else
        {
            SynchronizeSession();
            if (!initialized || !commandsReady || !engine.pfnClientCmd) ++rejected;
            else if (challenge)
            {
                if (challenge->life <= probe.life) ++rejected;
                else
                {
                    probe = *challenge;
                    authority.Reset(); sprint.Reset();
                    ++accepted;
                    Queue(custom::StandaloneInputReadyCommand, challenge->life, challenge->revision);
                }
            }
            else
            {
                const auto* old = authority.Current();
                const auto life = old ? old->life : 0;
                if (!probe.life || grant->life != probe.life || grant->revision <= probe.revision ||
                    !authority.Receive(bytes)) ++rejected;
                else
                {
                    const auto* next = authority.Current();
                    if (next->life != life || !next->enabled) sprint.Reset();
                    ++accepted;
                }
            }
        }
    }
    return previousMessage ? previousMessage(name, size, data) : 1;
}
int Init(const char* name, int size, void* data)
{
    ResetStandaloneMovementInput();
    return previousInit ? previousInit(name, size, data) : 1;
}
}
void InitializeStandaloneMovementInput()
{
    if (ownerThread && !Owner()) return;
    ownerThread = GetCurrentThreadId(); initialized = commandsReady = false;
    if (!api || !api->HookCmd || !api->FindCmd || !api->HookUserMsg ||
        !engine.pfnHookUserMsg || !engine.pfnClientCmd) return;
    const bool stockCommands = command_chain::Current("+speed") && command_chain::Current("-speed");
    InstallChainedUserMessage(custom::StandaloneInputMessage, Receive, previousMessage);
    InstallChainedUserMessage("InitHUD", Init, previousInit);
    InstallChainedCommand("+speed", Down, previousDown);
    InstallChainedCommand("-speed", Up, previousUp);
    const auto installed = [](xcommand_t& previous)
    {
        return std::any_of(command_chain::installed.begin(), command_chain::installed.end(),
            [&previous](const command_chain::Link& link) { return link.previous == &previous; });
    };
    commandsReady = stockCommands && previousDown && previousUp && installed(previousDown) && installed(previousUp);
    initialized = true;
}
bool StandaloneSprintAvailable() noexcept { return ActiveSprintGrant() != nullptr; }
void ResetStandaloneMovementInput()
{
    if (!Owner()) return;
    authority.Reset(); sprint.Reset(); probe = {};
}
void ShutdownStandaloneMovementInput()
{
    if (!Owner()) return;
    ResetStandaloneMovementInput(); initialized = commandsReady = false;
}
void StandaloneMovementInputDiagnostics(std::ostream& out)
{
    out << "standaloneMovementInput initialized=" << initialized << " commandsReady=" << commandsReady
        << " probeLife=" << probe.life << " probeRevision=" << probe.revision << " state=" << sprint.State()
        << " accepted=" << accepted << " rejected=" << rejected << " sent=" << sent << " consumed=" << consumed;
    if (const auto* grant = authority.Current())
        out << " life=" << grant->life << " revision=" << grant->revision << " enabled=" << grant->enabled;
    out << '\n';
}
}
