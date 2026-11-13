// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../scenario/core/human_kick.hpp"
#include <iosfwd>

namespace csol::client
{
// Read-only presentation arbitration supplied by the authoritative kick
// snapshot. This never starts, completes or predicts a kick locally.
bool HumanKickActive() noexcept;
namespace human_kick_detail
{
// Host snapshots own availability. The native engine routes chat text before
// dispatching commands; this gate does not bind or synthesize an F key.
class InputState
{
public:
    bool Receive(std::span<const std::byte> bytes)
    {
        const auto value = scenario::human_kick::Decode(bytes);
        // Host life serials do not wrap within a connection. Retired Ready
        // snapshots cannot replace a newer life's authoritative state.
        if (!value || value->life < value_.life) return false;
        value_ = *value;
        return true;
    }
    void Reset() { value_ = {}; }
    const scenario::human_kick::Snapshot& Value() const { return value_; }
    bool Enabled() const { return (value_.flags & scenario::human_kick::Enabled) != 0; }
    bool Ready() const
    {
        constexpr auto ready = scenario::human_kick::Enabled | scenario::human_kick::Ready;
        return (value_.flags & (ready | scenario::human_kick::Active)) == ready;
    }
    bool OwnImpulse(int argc, const char* argument, bool gameplay) const
    {
        return gameplay && Enabled() && argc == 2 && argument &&
            argument[0] == '1' && argument[1] == '0' && argument[2] == '0' && !argument[3];
    }
private:
    scenario::human_kick::Snapshot value_{};
};
}
void InitializeHumanKick();
void ResetHumanKick();
void ShutdownHumanKick();
void HumanKickFrame();
void HumanKickVideoInit();
void InvalidateHumanKickResources();
void DrawHumanKick(float time, int intermission);
void HumanKickDiagnostics(std::ostream&);
}
