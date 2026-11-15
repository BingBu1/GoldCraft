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
DWORD ownerThread{};
xcommand_t previousDown{}, previousUp{};
pfnUserMsgHook previousMessage{}, previousInit{};
std::uint64_t accepted{}, rejected{}, sent{}, consumed{};
bool initialized{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
bool Dispatch(int state)
{
    if (!Owner() || !initialized || !engine.pfnClientCmd) return false;
    const auto* grant = authority.Current();
    if (!grant || !grant->enabled) return false;
    const auto life = grant->life;
    return sprint.Dispatch(state, [life](const char* name)
    {
        // HW3782F6C0 appends to the client command buffer, including newline.
        // The extra life argument is a host authority token, not original CSO.
        std::array<char, 64> text{};
        const auto length = std::strlen(name);
        std::memcpy(text.data(), name, length); text[length] = ' ';
        const auto end = std::to_chars(text.data() + length + 1, text.data() + text.size() - 2, life).ptr;
        *end = '\n'; end[1] = 0;
        ++sent;
        engine.pfnClientCmd(text.data());
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
        if (!custom::DecodeStandaloneInput(bytes)) ++rejected;
        else
        {
            SynchronizeSession();
            const auto* old = authority.Current();
            const auto life = old ? old->life : 0;
            if (!initialized || !authority.Receive(bytes)) ++rejected;
            else
            {
                const auto* next = authority.Current();
                if (next->life != life || !next->enabled) sprint.Reset();
                ++accepted;
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
    ownerThread = GetCurrentThreadId(); initialized = true;
    InstallChainedUserMessage(custom::StandaloneInputMessage, Receive, previousMessage);
    InstallChainedUserMessage("InitHUD", Init, previousInit);
    InstallChainedCommand("+speed", Down, previousDown);
    InstallChainedCommand("-speed", Up, previousUp);
}
void ResetStandaloneMovementInput()
{
    if (!Owner()) return;
    authority.Reset(); sprint.Reset();
}
void ShutdownStandaloneMovementInput()
{
    if (!Owner()) return;
    ResetStandaloneMovementInput(); initialized = false;
}
void StandaloneMovementInputDiagnostics(std::ostream& out)
{
    out << "standaloneMovementInput initialized=" << initialized << " state=" << sprint.State()
        << " accepted=" << accepted << " rejected=" << rejected << " sent=" << sent << " consumed=" << consumed;
    if (const auto* grant = authority.Current())
        out << " life=" << grant->life << " revision=" << grant->revision << " enabled=" << grant->enabled;
    out << '\n';
}
}
