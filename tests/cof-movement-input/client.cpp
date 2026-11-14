// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/core/cof_movement_input.cpp"
#include "../../native/csol/scenario/custom/cof_ground_movement.hpp"
#include "../csol-hud-style/allocation_probe.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

namespace input_test
{
namespace custom = csol::scenario::custom;
unsigned checks{}, lookupCalls{}, nativeCalls{}, originalDown{}, originalUp{}, removed{};
bool matched{true}, nativeHeld{}, menu{}, vote{}, buying{}, resetOnSync{}, nesting{}, nested{};
std::array<float, 3> fractions{1, 1, 0};
std::array<cvar_t, 4> cvars{{
    {const_cast<char*>("cl_forwardspeed"), const_cast<char*>("400"), 0, 400, nullptr},
    {const_cast<char*>("cl_backspeed"), const_cast<char*>("400"), 0, 400, nullptr},
    {const_cast<char*>("cl_sidespeed"), const_cast<char*>("400"), 0, 400, nullptr},
    {const_cast<char*>("cl_movespeedkey"), const_cast<char*>("0.52"), 0, .52f, nullptr}}};
float NativeCap() { return 200; }
float ExternalCap() { return 175; }
float (*opaquePrevious)(){};
unsigned opaqueDepth{}, opaqueCalls{};
float OpaqueMaximum();
float (*maximum)() = NativeCap;
void (*input)(float, usercmd_t*, int){};
const char* keyText{"42"};
cmd_function_t down{}, up{};
pfnUserMsgHook message{}, init{};
std::function<void()> nativeCallback;
void Check(bool value, const char* why)
{ ++checks; if (!value) { std::printf("FAIL %s\n", why); std::exit(1); } }
float OpaqueMaximum()
{
    Check(++opaqueDepth == 1, "an opaque outer maximum callback cannot recurse through the adapter");
    ++opaqueCalls;
    const float value = opaquePrevious();
    --opaqueDepth;
    return value;
}
void StockDown() { ++originalDown; nativeHeld = true; }
void StockUp() { ++originalUp; nativeHeld = false; }
void NativeInput(float, usercmd_t* command, int active)
{
    ++nativeCalls;
    if (!command || !active) return;
    command->forwardmove = fractions[0] * cvars[fractions[0] < 0 ? 1 : 0].value;
    command->sidemove = fractions[1] * cvars[2].value;
    command->upmove = fractions[2] * 320;
    const float cap = maximum();
    if (cap != 0)
    {
        const auto limited = custom::LimitCofCommand({command->forwardmove, command->sidemove, command->upmove}, cap);
        Check(bool(limited), "explicit native command-boundary arithmetic is finite");
        command->forwardmove = limited->forward; command->sidemove = limited->side; command->upmove = limited->up;
    }
    if (nativeHeld)
    {
        command->forwardmove *= cvars[3].value;
        command->sidemove *= cvars[3].value;
        command->upmove *= cvars[3].value;
    }
    if (nesting && !nested)
    { nested = true; usercmd_t inner{}; input(0, &inner, 1); nested = false; }
    if (nativeCallback) nativeCallback();
}
pfnUserMsgHook Message(const char* name, pfnUserMsgHook callback)
{
    auto& value = std::strcmp(name, custom::MovementInputMessage) ? init : message;
    const auto old = value; value = callback; return old;
}
void Send(custom::MovementInputPolicy value)
{
    const auto bytes = custom::EncodeMovementInput(value);
    Check(bool(bytes) && message, "real registered input-policy transport is available");
    message(custom::MovementInputMessage, int(bytes->size()), const_cast<std::byte*>(bytes->data()));
}
std::array<float, 4> Values()
{ return {cvars[0].value, cvars[1].value, cvars[2].value, cvars[3].value}; }
}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
void SynchronizeSession()
{ if (input_test::resetOnSync) { ResetCofMovementInput(); input_test::resetOnSync = false; } }
bool CharacterMenuVisible() { return input_test::menu; }
bool BuyMenuVisible() { return input_test::buying; }
bool ScenarioMapVoteVisible() { return input_test::vote; }
hook_t* Hook(void* address, void* replacement, void** previous)
{
    input_test::Check(address == reinterpret_cast<void*>(input_test::NativeInput), "actual consumer hooks only the matched native CreateMove");
    *previous = address; input_test::input = reinterpret_cast<decltype(input_test::input)>(replacement);
    return reinterpret_cast<hook_t*>(1);
}
void RemoveHook(hook_t* value)
{
    input_test::Check(value == reinterpret_cast<hook_t*>(1), "actual consumer removes its own hook");
    ++input_test::removed; input_test::input = input_test::NativeInput;
}
}
int main()
{
    using namespace csol::client;
    using input_test::Check;
    namespace custom = csol::scenario::custom;
    custom::SprintKeys keys;
    keys.Down(1); keys.Down(2); keys.Down(3); keys.Up(1); keys.Up(3);
    Check(keys.Held(), "third physical key cannot overwrite a retained native two-key owner");
    keys.Up(2); Check(!keys.Held(), "both retained physical owners can release");
    custom::MovementInputAuthority protocol;
    custom::MovementInputPolicy wireValue{8, 7, true, {300, 200, 200}};
    const auto wire = custom::EncodeMovementInput(wireValue);
    Check(wire && protocol.Receive(*wire) && protocol.Current()->axes == wireValue.axes, "strict fixed-width policy roundtrip");
    Check(!protocol.Receive(*wire), "duplicate input policy is inert");
    auto bad = *wire; bad[1] = std::byte{2};
    Check(!protocol.Receive(bad), "invalid enabled flags cannot create input authority");
    Check(!custom::DecodeMovementInput(std::span(wire->data(), wire->size() - 1)), "truncated input policy is rejected");
    wireValue.revision = 8; wireValue.life = 7;
    Check(!protocol.Receive(*custom::EncodeMovementInput(wireValue)), "older native life cannot replace newer policy");
    wireValue.life = 9; wireValue.axes[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!custom::EncodeMovementInput(wireValue), "nonfinite authoritative input references are rejected");
    metahook_api_t meta{}; api = &meta;
    input_test::down.name = const_cast<char*>("+speed"); input_test::down.function = input_test::StockDown;
    input_test::up.name = const_cast<char*>("-speed"); input_test::up.function = input_test::StockUp;
    meta.FindCmd = [](const char* name) -> cmd_function_t* { return std::strcmp(name, "+speed") ? &input_test::up : &input_test::down; };
    meta.HookCmd = [](const char* name, xcommand_t callback) -> xcommand_t
    {
        auto& row = std::strcmp(name, "+speed") ? input_test::up : input_test::down;
        const auto old = row.function; row.function = callback; return old;
    };
    meta.HookUserMsg = input_test::Message;
    meta.FindUserMsgHook = [](const char*) -> usermsg_t* { return nullptr; };
    meta.GetClientBase = []() -> void* { return reinterpret_cast<void*>(2); };
    meta.ResolveGameSymbol = [](void*, const char* name, mh_gamesymbol_kind_t, void** out) -> mh_gamesymbol_status_t
    {
        ++input_test::lookupCalls;
        if (!input_test::matched) return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
        *out = !std::strcmp(name, "GoldCraftCofInput_CreateMove") ?
            reinterpret_cast<void*>(input_test::NativeInput) : reinterpret_cast<void*>(&input_test::maximum);
        return MH_GAMESYMBOL_OK;
    };
    engine.pfnHookUserMsg = [](const char* name, pfnUserMsgHook callback) { input_test::Message(name, callback); return 1; };
    engine.Cmd_Argv = [](int) -> char* { return const_cast<char*>(input_test::keyText); };
    engine.pfnGetCvarPointer = [](const char* name) -> cvar_t*
    { ++input_test::lookupCalls; for (auto& row : input_test::cvars) if (!std::strcmp(name, row.name)) return &row; return nullptr; };
    view.health = 100;
    Check(InitializeCofMovementInput(), "actual matched native input hook initializes");
    usercmd_t command{};
    input_test::down.function();
    input_test::input(0, &command, 1);
    Check(input_test::originalDown == 1 && command.forwardmove < 74 && command.sidemove < 74,
        "without authority ordinary native weapon cap and +speed continue unchanged");
    custom::MovementInputPolicy policy{1, 1, true, {300, 200, 200}};
    input_test::Send(policy);
    input_test::keyText = "42"; input_test::down.function();
    const auto baseline = input_test::Values();
    input_test::input(0, &command, 1);
    Check(command.forwardmove == 300 && command.sidemove == 200 && (command.buttons & IN_RUN),
        "actual adapter preserves uncapped native input fractions and emits only sprint intent");
    Check(input_test::Values() == baseline && input_test::maximum == input_test::NativeCap,
        "all numeric cvars and the exact native callback slot restore after input");
    Check(std::strcmp(input_test::cvars[0].string, "400") == 0 && std::strcmp(input_test::cvars[3].string, "0.52") == 0,
        "native archived config strings are never edited");
    const auto wish = custom::CofNormalGroundWish({command.forwardmove, command.sidemove, command.upmove}, {1, 0, 0}, {0, 1, 0}, 300, 75);
    Check(wish && std::abs(wish->speed - 225) < .001f, "actual output can enter the proven original PM math without losing diagonal magnitude");
    input_test::fractions = {.5f, 0, 0}; input_test::input(0, &command, 1);
    Check(command.forwardmove == 150, "half native key input remains half of authoritative300 reference");
    input_test::keyText = "43"; input_test::down.function();
    input_test::keyText = "42"; input_test::up.function(); input_test::input(0, &command, 1);
    Check(command.buttons & IN_RUN, "release of one physical key retains the other sprint key");
    input_test::keyText = "43"; input_test::up.function(); input_test::input(0, &command, 1);
    Check(!(command.buttons & IN_RUN), "last physical release clears sprint intent");
    input_test::keyText = ""; input_test::down.function(); input_test::input(0, &command, 1);
    Check(command.buttons & IN_RUN, "manual native +speed is an owned held key");
    input_test::up.function(); input_test::input(0, &command, 1);
    Check(!(command.buttons & IN_RUN), "manual release clears both held keys");
    input_test::fractions = {-1, 0, 1}; input_test::input(0, &command, 1);
    Check(command.forwardmove == -200 && command.upmove == 320, "back reference is separate and native up axis is preserved");
    input_test::fractions = {.5f, 0, 0};
    input_test::keyText = "42"; input_test::down.function();
    input_test::menu = true; input_test::input(0, &command, 1); input_test::menu = false;
    input_test::input(0, &command, 1);
    Check(!(command.buttons & IN_RUN), "menu transition clears old sprint intent");
    input_test::down.function(); view.health = 0; input_test::input(0, &command, 1); view.health = 100;
    input_test::input(0, &command, 1); Check(!(command.buttons & IN_RUN), "death cannot leave a held sprint intent");
    input_test::down.function(); input_test::input(0, &command, 0); input_test::input(0, &command, 1);
    Check(!(command.buttons & IN_RUN), "inactive native input clears intent");
    input_test::nativeCallback = [&] { input_test::cvars[0].value = 275; input_test::maximum = input_test::ExternalCap; };
    input_test::input(0, &command, 1); input_test::nativeCallback = nullptr;
    Check(input_test::cvars[0].value == 275 && input_test::maximum == input_test::ExternalCap && input_test::cvars[2].value == 400,
        "external callback changes survive while unaffected borrowed values restore");
    input_test::cvars[0].value = 400; input_test::maximum = input_test::NativeCap;
    input_test::nesting = true; input_test::input(0, &command, 1); input_test::nesting = false;
    Check(input_test::Values() == baseline && input_test::maximum == input_test::NativeCap, "nested native calls share one scoped borrow");
    input_test::nativeCallback = ResetCofMovementInput; input_test::input(0, &command, 1); input_test::nativeCallback = nullptr;
    Check(command.forwardmove == 0 && command.sidemove == 0 && !(command.buttons & IN_RUN) && input_test::Values() == baseline,
        "map retirement inside native input clears obsolete command and restores its borrowed state");
    policy.life = 2; policy.revision = 2; input_test::Send(policy);
    const auto lookups = input_test::lookupCalls;
    allocation_probe::calls = 0; allocation_probe::active = true;
    for (unsigned i = 0; i != 1000; ++i) input_test::input(0, &command, 1);
    allocation_probe::active = false;
    Check(!allocation_probe::calls && input_test::lookupCalls == lookups && input_test::Values() == baseline,
        "1000 actual hot input calls allocate nothing, look up no cvars/symbols and restore original values");
    policy.revision = 3; policy.enabled = false; policy.axes = {}; input_test::Send(policy);
    input_test::input(0, &command, 1);
    Check(command.forwardmove != 150, "authoritative disable returns to original native input");
    policy.revision = 4; policy.enabled = true; policy.axes = {300, 200, 200}; input_test::Send(policy);
    input_test::input(0, &command, 1); Check(command.forwardmove != 150, "retired life cannot reactivate via a later stale-enabled packet");
    input_test::init("InitHUD", 0, nullptr); policy.life = policy.revision = 1; input_test::Send(policy);
    input_test::input(0, &command, 1); Check(command.forwardmove == 150, "new connection can admit its own life numbering");
    usercmd_t foreign{}; std::thread thread([&] { input_test::input(0, &foreign, 1); }); thread.join();
    Check(foreign.forwardmove != 150 && input_test::Values() == baseline, "foreign threads execute unchanged native input without borrowed references");
    input_test::nativeCallback = []
    {
        input_test::opaquePrevious = input_test::maximum;
        input_test::maximum = input_test::OpaqueMaximum;
    };
    input_test::input(0, &command, 1); input_test::nativeCallback = nullptr;
    Check(input_test::maximum == input_test::OpaqueMaximum && input_test::maximum() == 200,
        "a newly installed opaque callback can retain the scoped maximum adapter");
    input_test::input(0, &command, 1);
    Check(command.forwardmove == 150 && input_test::maximum == input_test::OpaqueMaximum && input_test::maximum() == 200,
        "a later scoped borrow preserves the opaque head and its original fallback");
    ShutdownCofMovementInput();
    Check(input_test::removed == 1 && input_test::input == input_test::NativeInput, "actual shutdown retires its matched native hook");
    Check(input_test::maximum() == 200, "a retained maximum callback remains valid after adapter shutdown");
    Check(InitializeCofMovementInput(), "native hook can initialize again after orderly shutdown");
    input_test::input(0, &command, 1);
    Check(command.forwardmove != 150 && input_test::maximum() == 200,
        "ordinary input after reinitialization retains the original opaque callback chain");
    policy.life = 3; policy.revision = 5; input_test::Send(policy);
    input_test::input(0, &command, 1);
    Check(command.forwardmove == 150 && input_test::maximum() == 200,
        "reactivated input cannot replace a retained maximum fallback with its opaque caller");
    input_test::nativeCallback = ShutdownCofMovementInput;
    input_test::input(0, &command, 1); input_test::nativeCallback = nullptr;
    Check(input_test::removed == 2 && input_test::input == input_test::NativeInput && input_test::Values() == baseline && command.forwardmove == 0,
        "shutdown inside the original callback restores scoped state before removing the native hook");
    Check(input_test::maximum() == 200 && input_test::opaqueCalls >= 6,
        "in-flight shutdown also preserves the retained opaque callback chain");
    input_test::matched = false;
    Check(!InitializeCofMovementInput(), "unmatched client cannot admit a native input policy");
    ShutdownCofMovementInput();
    std::printf("CoF actual native input adapter: %u checks passed\n", input_test::checks);
}
