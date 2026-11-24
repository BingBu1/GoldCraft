// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/core/standalone_movement_input.cpp"
#include "../csol-hud-style/allocation_probe.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

namespace input_test
{
namespace custom = csol::scenario::custom;
unsigned checks{}, lookups{}, downCalls{}, upCalls{}, queues{}, readyQueues{}, serverCalls{}, synchronizations{}, chains{};
bool resetOnSync{};
cmd_function_t down{}, up{};
pfnUserMsgHook message{}, init{};
xcommand_t onQueue{};
std::array<char, 64> lastCommand{}, lastReady{};
void Check(bool value, const char* why)
{ ++checks; if (!value) { std::printf("FAIL %s\n", why); std::exit(1); } }
void StockDown() { ++downCalls; }
void StockUp() { ++upCalls; }
int Chained(const char*, int, void*) { ++chains; return 73; }
pfnUserMsgHook Message(const char* name, pfnUserMsgHook callback)
{
    ++lookups;
    auto& target = std::strcmp(name, custom::StandaloneInputMessage) ? init : message;
    const auto previous = target; target = callback; return previous;
}
int Queue(const char* text)
{
    const auto size = std::strlen(text);
    Check(size < lastCommand.size(), "queued life-tagged command fits its fixed stack budget");
    if (!std::strncmp(text, custom::StandaloneInputReadyCommand, sizeof(custom::StandaloneInputReadyCommand) - 1))
    {
        ++readyQueues; std::memcpy(lastReady.data(), text, size + 1);
        if (onQueue) onQueue();
        return 0;
    }
    ++queues;
    std::memcpy(lastCommand.data(), text, size + 1);
    if (onQueue) onQueue();
    return 0; // Original sender consumes the edge even when the callback returns0.
}
void Send(custom::StandaloneInputGrant grant)
{
    const auto bytes = custom::EncodeStandaloneInput(grant);
    Check(bytes && message, "actual registered standalone grant route exists");
    Check(message(custom::StandaloneInputMessage, int(bytes->size()), const_cast<std::byte*>(bytes->data())) == 73,
        "grant callback preserves the downstream user-message return value");
}
void Probe(custom::StandaloneInputProbe value)
{
    const auto bytes = custom::EncodeStandaloneInputProbe(value);
    Check(bytes && message, "actual registered readiness probe route exists");
    Check(message(custom::StandaloneInputMessage, int(bytes->size()), const_cast<std::byte*>(bytes->data())) == 73,
        "readiness probe preserves the downstream user-message return value");
}
void Activate(std::uint64_t life)
{
    Probe({life, 1});
    Send({life, 2, true});
}
}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
void SynchronizeSession()
{
    ++input_test::synchronizations;
    if (input_test::resetOnSync) { ResetStandaloneMovementInput(); input_test::resetOnSync = false; }
}
}
int main()
{
    using namespace csol::client;
    namespace custom = csol::scenario::custom;
    using input_test::Check;
    custom::StandaloneInputAuthority protocol;
    custom::StandaloneInputGrant grant{7, 1, true};
    const auto bytes = custom::EncodeStandaloneInput(grant);
    Check(bytes && bytes->size() == 14 && protocol.Receive(*bytes), "fixed-width little-endian authority roundtrip");
    Check(protocol.Current()->life == 7 && protocol.Current()->revision == 1 && protocol.Current()->enabled,
        "decoded grant retains authority identity");
    Check(!protocol.Receive(*bytes), "same revision cannot replay");
    for (unsigned i = 0; i != 8; ++i)
    {
        auto bad = *bytes; bad[1] = std::byte(i + 2);
        Check(!protocol.Receive(bad), "unknown flags cannot mutate the grant");
    }
    Check(!custom::EncodeStandaloneInput({0, 1, true}) && !custom::EncodeStandaloneInput({1, 0, true}),
        "zero life or revision cannot be published");
    Check(!custom::DecodeStandaloneInput(std::span(bytes->data(), bytes->size() - 1)), "truncated policy is rejected");
    auto bad = *bytes; bad[0] = std::byte{1};
    Check(!protocol.Receive(bad), "other protocol versions are rejected");
    const auto probeBytes = custom::EncodeStandaloneInputProbe({7, 1});
    Check(probeBytes && custom::DecodeStandaloneInputProbe(*probeBytes) &&
        !custom::DecodeStandaloneInput(*probeBytes) && !custom::DecodeStandaloneInputProbe(*bytes),
        "a readiness probe cannot be mistaken for either an enabled or retired grant");
    grant.enabled = false; grant.revision = 2;
    Check(protocol.Receive(*custom::EncodeStandaloneInput(grant)), "authority retires a native life");
    grant.enabled = true; grant.revision = 3;
    Check(!protocol.Receive(*custom::EncodeStandaloneInput(grant)), "retired life cannot reactivate");
    grant.life = 8; grant.revision = 1;
    Check(protocol.Receive(*custom::EncodeStandaloneInput(grant)), "new life has its own revision sequence");
    grant.life = 7; grant.revision = UINT32_MAX;
    Check(!protocol.Receive(*custom::EncodeStandaloneInput(grant)), "an older life cannot win by revision");
    protocol.Reset();
    Check(protocol.Receive(*custom::EncodeStandaloneInput({1, 1, false})) &&
        !protocol.Receive(*custom::EncodeStandaloneInput({1, 2, true})), "first-seen retirement cannot be undone");

    metahook_api_t meta{}; api = &meta;
    input_test::down.function = input_test::StockDown; input_test::up.function = input_test::StockUp;
    input_test::message = input_test::init = input_test::Chained;
    meta.FindCmd = [](const char* name) -> cmd_function_t*
    { ++input_test::lookups; return std::strcmp(name, "+speed") ? &input_test::up : &input_test::down; };
    meta.HookCmd = [](const char* name, xcommand_t callback) -> xcommand_t
    {
        ++input_test::lookups;
        auto& row = std::strcmp(name, "+speed") ? input_test::up : input_test::down;
        const auto old = row.function; row.function = callback; return old;
    };
    meta.HookUserMsg = input_test::Message;
    meta.FindUserMsgHook = [](const char*) -> usermsg_t* { ++input_test::lookups; return nullptr; };
    engine.pfnHookUserMsg = [](const char* name, pfnUserMsgHook callback) { input_test::Message(name, callback); return 1; };
    engine.pfnClientCmd = input_test::Queue;
    engine.pfnServerCmd = [](const char*) { ++input_test::serverCalls; return 1; };
    engine.Cmd_Argv = [](int) -> char* { Check(false, "original standalone input never parses a physical key number"); return nullptr; };
    InitializeStandaloneMovementInput();
    InitializeStandaloneMovementInput();
    Check(!StandaloneSprintAvailable(), "HUD does not offer sprint before an authoritative grant");
    input_test::down.function(); input_test::up.function();
    Check(input_test::downCalls == 1 && input_test::upCalls == 1 && !input_test::queues,
        "ordinary CS without grant keeps both original speed callbacks");
    input_test::Send({7, 2, true});
    Check(!authority.Current(), "a grant without the current installed-consumer probe cannot enable input");
    const auto readiness = input_test::readyQueues;
    input_test::Probe({7, 1});
    Check(input_test::readyQueues == readiness + 1 && !authority.Current() &&
        !std::strcmp(input_test::lastReady.data(), "csol_sprint_ready 7 1\n"),
        "installed handlers acknowledge the exact life and probe revision before grant");
    input_test::Probe({7, 1});
    Check(input_test::readyQueues == readiness + 1, "duplicate probe does not queue another acknowledgement");
    input_test::Send({8, 2, true}); input_test::Send({7, 1, true});
    Check(!authority.Current(), "foreign life and pre-probe revision cannot activate input");
    input_test::Send({7, 2, true});
    Check(StandaloneSprintAvailable(), "HUD reads the same enabled grant as native input");
    input_test::up.function();
    Check(input_test::upCalls == 2 && !input_test::queues, "initial state0 release falls through as in original");
    input_test::onQueue = [] { Check(sprint.State() == 1, "state is committed before command-buffer callback"); };
    input_test::down.function(); input_test::onQueue = nullptr;
    Check(input_test::queues == 1 && input_test::downCalls == 1 &&
        !std::strcmp(input_test::lastCommand.data(), "sprintON 7\n"), "rising edge queues named client command and consumes native slowdown");
    input_test::down.function();
    Check(input_test::downCalls == 2 && input_test::queues == 1, "repeated state falls through instead of always consuming");
    input_test::Send({7, 3, true});
    input_test::down.function();
    Check(input_test::downCalls == 3 && input_test::queues == 1, "same-life policy refresh cannot manufacture a new key edge");
    input_test::up.function();
    Check(input_test::upCalls == 2 && input_test::queues == 2 &&
        !std::strcmp(input_test::lastCommand.data(), "sprintOFF 7\n"), "falling edge is queued despite native callback return0");
    input_test::up.function();
    Check(input_test::upCalls == 3 && input_test::queues == 2, "repeated release preserves native callback fallback");
    input_test::down.function(); input_test::down.function(); input_test::up.function();
    Check(sprint.State() == 0 && input_test::queues == 4 && input_test::downCalls == 4,
        "two keydowns and one release retain original one-state behavior, not CoF two-key ownership");
    input_test::Send({7, 4, false}); input_test::Send({7, 5, true});
    Check(!StandaloneSprintAvailable(), "HUD withdraws the offer when that grant is revoked");
    input_test::down.function();
    Check(input_test::queues == 4 && input_test::downCalls == 5, "retired policy continues ordinary native input");
    input_test::Activate(8); input_test::down.function();
    Check(!std::strcmp(input_test::lastCommand.data(), "sprintON 8\n"), "new authoritative life resets the original edge state");
    const auto sync = input_test::synchronizations;
    input_test::message(custom::StandaloneInputMessage, -1, nullptr);
    Check(input_test::synchronizations == sync && sprint.State() == 1, "malformed packet has no session-reset side effect");
    input_test::Send({7, UINT32_MAX, true});
    Check(sprint.State() == 1 && authority.Current()->life == 8, "stale grant cannot reset a held current input");
    input_test::onQueue = [] { input_test::up.function(); };
    input_test::up.function(); input_test::onQueue = nullptr;
    Check(input_test::queues == 6 && sprint.State() == 0 && input_test::upCalls == 4,
        "reentrant repeated edge falls through without recursive command submission");
    input_test::onQueue = ResetStandaloneMovementInput;
    input_test::down.function(); input_test::onQueue = nullptr;
    Check(!authority.Current() && sprint.State() == 0, "connection reset during queue cannot be overwritten on callback return");
    input_test::Activate(1);
    engine.pfnClientCmd = nullptr; input_test::down.function(); engine.pfnClientCmd = input_test::Queue;
    Check(sprint.State() == 0, "missing host command API neither consumes nor commits an edge");
    input_test::down.function();
    Check(!std::strcmp(input_test::lastCommand.data(), "sprintON 1\n"), "restored command API admits the pending native edge");
    std::thread foreign([] { input_test::up.function(); }); foreign.join();
    Check(sprint.State() == 1, "foreign thread cannot alter the owning client's input state");
    Check(input_test::init("InitHUD", 0, nullptr) == 73 && !authority.Current() && sprint.State() == 0,
        "InitHUD retires authority and preserves downstream callback");
    input_test::Activate(UINT64_MAX); input_test::down.function();
    Check(!std::strcmp(input_test::lastCommand.data(), "sprintON 18446744073709551615\n"), "maximum life identity fits without truncation");
    const auto lookups = input_test::lookups, queued = input_test::queues;
    allocation_probe::calls = 0; allocation_probe::active = true;
    for (unsigned i = 0; i != 1000; ++i) { input_test::up.function(); input_test::down.function(); }
    allocation_probe::active = false;
    Check(!allocation_probe::calls && input_test::lookups == lookups && input_test::queues == queued + 2000,
        "2000 actual input edges allocate nothing and perform no command/message lookup");
    Check(!input_test::serverCalls, "input always queues ClientCmd and never invokes ServerCmd directly");
    ShutdownStandaloneMovementInput();
    const auto stoppedQueues = input_test::queues, stoppedDown = input_test::downCalls;
    input_test::down.function(); input_test::up.function();
    Check(input_test::queues == stoppedQueues && input_test::downCalls == stoppedDown + 1,
        "retained callbacks safely fall back after plugin shutdown");
    InitializeStandaloneMovementInput(); input_test::down.function();
    Check(input_test::queues == stoppedQueues && !authority.Current(), "reinitialization never reuses the old life grant");
    engine.pfnClientCmd = nullptr;
    const auto noApiReady = input_test::readyQueues;
    input_test::Probe({1, 1}); engine.pfnClientCmd = input_test::Queue;
    Check(input_test::readyQueues == noApiReady && !probe.life,
        "missing command buffer neither acknowledges nor records a ready consumer");
    input_test::onQueue = ResetStandaloneMovementInput;
    input_test::Probe({1, 1}); input_test::onQueue = nullptr;
    Check(!probe.life && !authority.Current(), "reentrant reset during readiness acknowledgement is retained");
    auto heads = CaptureChainedCommandHeads();
    input_test::down.function = input_test::StockDown; input_test::up.function = input_test::StockUp;
    RefreshChainedCommandHeads(heads); InitializeStandaloneMovementInput();
    input_test::Activate(1); input_test::down.function();
    Check(input_test::queues == stoppedQueues + 1, "native HUD callback replacement starts one fresh command chain");
    ShutdownStandaloneMovementInput();
    const auto missingHeads = CaptureChainedCommandHeads();
    input_test::down.function = input_test::up.function = nullptr;
    RefreshChainedCommandHeads(missingHeads); InitializeStandaloneMovementInput();
    const auto missingCommandsReady = input_test::readyQueues;
    input_test::Probe({2, 1}); input_test::Send({2, 2, true});
    Check(!commandsReady && input_test::readyQueues == missingCommandsReady && !authority.Current(),
        "missing original speed commands cannot manufacture a ready consumer from retained downstream pointers");
    ShutdownStandaloneMovementInput();
    const auto hookCommand = meta.HookCmd; meta.HookCmd = nullptr;
    InitializeStandaloneMovementInput();
    Check(!initialized && !commandsReady, "missing native hook API leaves input uninitialized");
    meta.HookCmd = hookCommand;
    const auto phaseHeads = CaptureChainedCommandHeads();
    input_test::down.function = input_test::StockDown;
    input_test::up.function = input_test::StockUp;
    RefreshChainedCommandHeads(phaseHeads); InitializeStandaloneMovementInput();
    const auto noonDown = input_test::downCalls, noonUp = input_test::upCalls;
    const auto beforeDusk = input_test::queues;
    input_test::down.function();
    input_test::Activate(1);
    input_test::up.function();
    Check(input_test::downCalls == noonDown + 1 && input_test::upCalls == noonUp + 1 &&
        input_test::queues == beforeDusk, "a shift press begun in noon releases the original walk binding after dusk arrives");
    input_test::down.function();
    Check(sprint.State() == 1 && input_test::queues == beforeDusk + 1,
        "the next shift press activates sprint");
    input_test::Send({1, 3, false});
    input_test::Probe({2, 1});
    input_test::up.function();
    Check(sprint.State() == 0 && input_test::upCalls == noonUp + 2 && input_test::queues == beforeDusk + 1,
        "revoking while held clears sprint and restores native key release");
    input_test::Send({2, 2, true}); input_test::down.function();
    Check(!std::strcmp(input_test::lastCommand.data(), "sprintON 2\n"),
        "a later dusk activates sprint under a newly acknowledged token");
    ShutdownStandaloneMovementInput();
    std::printf("CSOL StandAlone actual input adapter: %u checks passed\n", input_test::checks);
}
