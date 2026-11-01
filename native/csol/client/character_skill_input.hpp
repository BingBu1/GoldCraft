// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../character_skill_gate.hpp"
#include "../scenario_abilities.hpp"
#include <algorithm>

namespace csol::client
{
// The original progress messages draw the panel. Only a server snapshot may
// claim its activation key; a normal server's ZBSVer2 traffic cannot do so.
class CharacterSkillInput
{
public:
    void Reset() { *this = {}; }
    void ResetLife()
    {
        legacy_ = {}; gate_ = {}; installed_ = gateInstalled_ = pending_ = false;
        received_ = gateTime_ = requested_ = 0; progress_ = -1;
        // A new life on a gate-capable server must wait for its new gate,
        // even if the legacy HUD snapshot happens to arrive first.
    }
    void Restore(const abilities::Snapshot& value, double now)
    {
        if (!std::isfinite(now)) return;
        abilities::State check;
        if (!check.Restore(value)) return;
        if (legacy_.type != value.type) pending_ = false;
        if (gateInstalled_ && gate_.subtype != value.type) gateInstalled_ = false;
        legacy_ = value; installed_ = true; received_ = now; progress_ = -1;
    }
    void Restore(const skillgate::Snapshot& value, double now)
    {
        if (!std::isfinite(now) || !skillgate::Valid(value)) return;
        if (gate_.subtype != value.subtype) pending_ = false;
        gate_ = value; gateInstalled_ = gateServer_ = true; gateTime_ = now;
    }
    void Observe(const abilities::Message& message, double now)
    {
        if (message.operation == abilities::Operation::Number || !std::isfinite(now)) return;
        if (!installed_ || gateServer_) return;
        if (message.value < .001f || unsigned(message.type) != legacy_.type)
        { ResetLife(); return; }
        if (message.operation == abilities::Operation::Cooldown)
            Restore(abilities::Snapshot{legacy_.type,message.value,0,message.duration},now);
        else progress_ = message.value;
    }
    unsigned Type() const
    { return gateServer_ ? (gateInstalled_ ? gate_.subtype : 0) : (installed_ ? legacy_.type : 0); }
    bool OwnsKey() const { return Type() != 0; }
    bool HasGate() const { return gateInstalled_; }
    unsigned Flags() const { return gateInstalled_ ? gate_.flags : 0; }
    bool Manual() const
    {
        if (gateServer_) return gateInstalled_ && (gate_.flags & skillgate::Manual);
        const auto* resource = abilities::ResourceFor(Type());
        return resource && resource->key;
    }
    bool Ready(double now) const
    {
        if (!OwnsKey() || !std::isfinite(now)) return false;
        if (gateServer_) return gateInstalled_ && skillgate::CanRequest(gate_);
        if (progress_ >= 0) return Manual() && progress_ >= 1;
        return Manual() && now >= received_ && legacy_.elapsed + (now - received_) >= legacy_.cooldown;
    }
    bool Request(double now, bool gameplay)
    {
        if (!gameplay || !Ready(now)) return false;
        // Suppress key-repeat bursts, but allow a later retry if the server
        // rejected a request or no state change arrived. Never predict success.
        if (pending_ && (now < requested_ || now - requested_ < .25)) return false;
        requested_ = now; pending_ = true; return true;
    }
    int PrepareSeconds(double now) const { return Seconds(gate_.prepareRemaining, now); }
    int ActiveSeconds(double now) const { return Seconds(gate_.activeRemaining, now); }
private:
    int Seconds(float duration, double now) const
    {
        if (!gateInstalled_ || !std::isfinite(now)) return 0;
        // Clamp before converting an untrusted float to an integer.
        return static_cast<int>(std::ceil(std::clamp(double(duration) - std::max(0.0, now - gateTime_), 0.0, 86400.0)));
    }
    abilities::Snapshot legacy_{};
    skillgate::Snapshot gate_{};
    double received_{}, gateTime_{}, requested_{};
    float progress_{-1};
    bool installed_{}, gateInstalled_{}, gateServer_{}, pending_{};
};
}
