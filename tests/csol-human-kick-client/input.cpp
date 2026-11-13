// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/core/human_kick.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace kick = csol::scenario::human_kick;
using csol::client::human_kick_detail::InputState;
namespace
{
unsigned checks{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "human-kick input: %s\n", message); std::exit(1); }
}
void Apply(InputState& state, kick::Snapshot value)
{
    const auto bytes = kick::Encode(value);
    Check(bool(bytes), "fixture is a valid authoritative snapshot");
    Check(state.Receive(*bytes), "actual client accepts the authoritative wire snapshot");
}
}
int main()
{
    InputState state;
    Check(!state.OwnImpulse(2, "100", true), "ordinary server retains impulse100");
    Check(!state.Ready(), "ordinary server has no ready hint");
    Apply(state, {1, kick::Enabled | kick::Ready, 0});
    Check(state.OwnImpulse(2, "100", true), "enabled exact native impulse routes to server kick");
    Check(state.Ready(), "server Ready enables the hint");
    for (const char* argument : {"", "1", "10", "101", "0100", "+100", "100 ", "1000", "-100", "100;quit"})
        Check(!state.OwnImpulse(2, argument, true), "other impulse spelling remains native");
    Check(!state.OwnImpulse(2, nullptr, true), "missing argv remains native");
    Check(!state.OwnImpulse(1, "100", true), "missing argument remains native");
    Check(!state.OwnImpulse(3, "100", true), "extra arguments remain native");
    Check(!state.OwnImpulse(2, "100", false), "menu/chat/UI/console input context remains native");
    Apply(state, {1, kick::Enabled | kick::Active, kick::Cooldown});
    Check(!state.Ready(), "active kick cannot show ready");
    Check(state.OwnImpulse(2, "100", true), "server owns rejection of a repeated active request");
    Apply(state, {1, kick::Enabled, 0});
    Check(!state.Ready(), "zero remaining cannot manufacture a server Ready flag");
    Apply(state, {1, kick::Enabled, kick::Cooldown});
    Check(!state.Ready(), "cooldown snapshot cannot show ready");
    const auto prior = state.Value();
    auto invalid = *kick::Encode({2, kick::Enabled | kick::Ready, 0});
    invalid[0] = std::byte{2};
    Check(!state.Receive(invalid), "unsupported protocol version is rejected");
    Check(state.Value().life == prior.life && state.Value().flags == prior.flags,
        "malformed data cannot replace the last server state");
    Check(!state.Receive({invalid.data(), invalid.size() - 1}), "truncated data is rejected");
    auto extra = std::array<std::byte, kick::MessageSize + 1>{};
    Check(!state.Receive(extra), "oversized data is rejected");
    Apply(state, {2, 0, 0});
    Check(!state.Enabled() && !state.OwnImpulse(2, "100", true), "new disabled life restores native impulse");
    const auto retiredReady = *kick::Encode({1, kick::Enabled | kick::Ready, 0});
    Check(!state.Receive(retiredReady), "retired-life Ready cannot replace a newer authoritative life");
    Check(state.Value().life == 2 && !state.Enabled() && !state.Ready(),
        "rejected retired-life Ready cannot reenable the hint or own native input");
    Apply(state, {3, kick::Enabled | kick::Ready, 0});
    Check(state.Ready(), "new authoritative life can reenable the hint");
    state.Reset();
    Check(!state.Enabled() && !state.Ready() && state.Value().life == 0,
        "ResetHUD/session retirement clears authority");
    Check(!state.OwnImpulse(2, "100", true), "reset cannot retain ownership of native controls");
    Apply(state, {1, kick::Enabled | kick::Ready, 0});
    Check(state.Value().life == 1 && state.Ready(), "connection reset accepts the next server's life namespace");
    std::printf("human-kick client input: %u checks passed\n", checks);
}
