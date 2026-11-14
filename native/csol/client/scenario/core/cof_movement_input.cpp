// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "../../command_chain.hpp"
#include "../../buy_menu.hpp"
#include "../../characters.hpp"
#include "scenario_map_vote.hpp"
#include "cof_movement_input.hpp"
#include "../../../scenario/custom/movement_input.hpp"
#include <in_buttons.h>
#include <usercmd.h>
#include <charconv>
#include <cstring>
#include <ostream>

namespace csol::client
{
namespace
{
namespace custom = csol::scenario::custom;
using NativeCreateMove = void (*)(float, usercmd_t*, int);
using NativeMaximum = float (*)();
DWORD ownerThread{};
custom::MovementInputAuthority authority;
custom::SprintKeys sprint;
std::array<cvar_t*, 4> variables{};
NativeMaximum* maximumSlot{};
NativeMaximum nativeMaximum{};
NativeMaximum* retainedSlot{};
void* retainedCreate{};
NativeCreateMove nativeCreate{};
hook_t* createHook{};
pfnUserMsgHook previousMessage{}, previousInit{};
xcommand_t previousDown{}, previousUp{};
std::uint64_t epoch{}, translated{}, refused{}, stale{};
unsigned depth{};
bool installed{}, enabled{}, closing{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
int Key() noexcept
{
    const char* text = engine.Cmd_Argv ? engine.Cmd_Argv(1) : nullptr;
    if (!text || !*text) return -1;
    int key{}; const auto* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, key);
    return parsed.ec == std::errc{} && parsed.ptr == end && key > 0 ? key : 0;
}
void Down()
{
    if (Owner() && enabled) sprint.Down(Key());
    if (previousDown) previousDown();
}
void Up()
{
    if (Owner()) sprint.Up(Key());
    if (previousUp) previousUp();
}
float Maximum()
{
    // Zero means defer the native CreateMove weapon-speed pre-cap. Actual
    // server/client PM still owns the raw3D and final percentage limits.
    if (Owner() && enabled && depth && !closing) return 0;
    return nativeMaximum ? nativeMaximum() : 0;
}
void FinishClose()
{
    if (!closing || depth) return;
    if (createHook) RemoveHook(createHook);
    createHook = nullptr; installed = false; maximumSlot = nullptr;
    nativeCreate = nullptr; variables = {};
}
struct InputScope
{
    std::array<float, 4> saved{}, applied{};
    NativeMaximum previous{};
    bool ready{};
    InputScope(const custom::MovementInputPolicy& policy)
    {
        for (unsigned i = 0; i != 4; ++i)
        {
            if (!variables[i] || !std::isfinite(variables[i]->value)) return;
            saved[i] = variables[i]->value;
            applied[i] = i == 3 ? 1 : policy.axes[i];
        }
        if (!maximumSlot || !*maximumSlot || *maximumSlot == Maximum) return;
        previous = *maximumSlot;
        for (unsigned i = 0; i != 4; ++i) variables[i]->value = applied[i];
        *maximumSlot = Maximum; ready = true; ++depth;
    }
    ~InputScope()
    {
        if (!ready) return;
        if (maximumSlot && *maximumSlot == Maximum) *maximumSlot = previous;
        for (unsigned i = 0; i != 4; ++i)
            if (variables[i]->value == applied[i]) variables[i]->value = saved[i];
        --depth;
    }
};
void NativeInput(float frame, usercmd_t* command, int active)
{
    if (!nativeCreate) return;
    if (!Owner() || !enabled || !installed || depth || !command)
    { nativeCreate(frame, command, active); return; }
    const auto* policy = authority.Current();
    if (!active || view.health <= 0 || view.spectator || CharacterMenuVisible() ||
        BuyMenuVisible() || ScenarioMapVoteVisible())
    { sprint.Reset(); nativeCreate(frame, command, active); return; }
    if (!policy || !policy->enabled)
    { nativeCreate(frame, command, active); return; }
    const auto life = policy->life;
    const auto currentEpoch = epoch;
    bool ready{};
    {
        const InputScope scope(*policy); ready = scope.ready;
        nativeCreate(frame, command, active);
    }
    if (!ready) { ++refused; FinishClose(); return; }
    if (!enabled || epoch != currentEpoch || !authority.Current() || authority.Current()->life != life)
    {
        // A connection/life retired during a native callback cannot leak a
        // command generated under the obsolete reference into the new owner.
        command->forwardmove = command->sidemove = command->upmove = 0;
        command->buttons &= ~IN_RUN; ++stale; FinishClose(); return;
    }
    command->buttons &= ~IN_RUN;
    if (sprint.Held()) command->buttons |= IN_RUN;
    ++translated;
}
int Receive(const char* name, int size, void* bytes)
{
    if (Owner() && enabled && bytes && size == int(custom::MovementInputMessageSize))
    {
        const auto packet = std::span(static_cast<const std::byte*>(bytes), std::size_t(size));
        if (custom::DecodeMovementInput(packet))
        {
            SynchronizeSession();
            const auto before = authority.Current() ? authority.Current()->life : 0;
            if (installed && authority.Receive(packet))
            {
                if (!authority.Current()->enabled || authority.Current()->life != before)
                { sprint.Reset(); ++epoch; }
            }
            else ++refused;
        }
        else ++refused;
    }
    return previousMessage ? previousMessage(name, size, bytes) : 1;
}
int Init(const char* name, int size, void* bytes)
{ ResetCofMovementInput(); return previousInit ? previousInit(name, size, bytes) : 1; }
}
bool InitializeCofMovementInput()
{
    if (ownerThread && !Owner()) return false;
    ownerThread = GetCurrentThreadId(); enabled = true; closing = false;
    InstallChainedUserMessage(custom::MovementInputMessage, Receive, previousMessage);
    InstallChainedUserMessage("InitHUD", Init, previousInit);
    InstallChainedCommand("+speed", Down, previousDown);
    InstallChainedCommand("-speed", Up, previousUp);
    if (createHook) return installed;
    if (!api || !api->ResolveGameSymbol || !engine.pfnGetCvarPointer) return false;
    void *create{}, *slot{};
    if (api->ResolveGameSymbol(api->GetClientBase(), "GoldCraftCofInput_CreateMove", MH_GAMESYMBOL_KIND_FUNCTION, &create) != MH_GAMESYMBOL_OK ||
        api->ResolveGameSymbol(api->GetClientBase(), "GoldCraftCofInput_MaximumSlot", MH_GAMESYMBOL_KIND_GLOBAL, &slot) != MH_GAMESYMBOL_OK ||
        !create || !slot) return false;
    constexpr std::array names{"cl_forwardspeed", "cl_backspeed", "cl_sidespeed", "cl_movespeedkey"};
    for (unsigned i = 0; i != names.size(); ++i)
    { variables[i] = engine.pfnGetCvarPointer(names[i]); if (!variables[i]) return false; }
    maximumSlot = static_cast<NativeMaximum*>(slot);
    if (!*maximumSlot) return false;
    if (retainedSlot != maximumSlot || retainedCreate != create)
    {
        if (*maximumSlot == Maximum) return false;
        // Capture once per native binding. An opaque outer plugin may keep
        // Maximum after this scope or HUD shutdown; its head is not a fallback.
        nativeMaximum = *maximumSlot;
        retainedSlot = maximumSlot; retainedCreate = create;
    }
    createHook = Hook(create, reinterpret_cast<void*>(NativeInput), reinterpret_cast<void**>(&nativeCreate));
    installed = createHook && nativeCreate;
    return installed;
}
void ResetCofMovementInput()
{
    if (!Owner()) return;
    ++epoch; authority.Reset(); sprint.Reset();
}
void ShutdownCofMovementInput()
{
    if (!Owner()) return;
    ResetCofMovementInput(); enabled = false; closing = true;
    FinishClose();
}
void CofMovementInputDiagnostics(std::ostream& out)
{
    out << "cofMovementInput matched=" << installed << " translated=" << translated
        << " refused=" << refused << " retired=" << stale << " sprintHeld=" << sprint.Held();
    if (const auto* value = authority.Current())
        out << " life=" << value->life << " revision=" << value->revision << " enabled=" << value->enabled;
    out << '\n';
}
}
