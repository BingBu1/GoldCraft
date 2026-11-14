// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace guest::cof
{
struct RevolverPlayerState
{
    bool firstDrawn{};
    float cylinderAngle{},cylinderGate{};
    std::optional<float> reloadUntil{};
};
// Process-local authoritative identity. None of these fields comes from a
// presentation packet or survives a native map/connection replacement.
struct RevolverConnection
{
    std::uint64_t map{};
    unsigned slot{};
    int serial{},userid{};
    std::uintptr_t player{};
    bool operator==(const RevolverConnection&) const = default;
};
class RevolverLifecycle final
{
public:
    bool Bind(RevolverConnection key,bool spawn,RevolverPlayerState& output) noexcept
    {
        if(!key.map||!key.slot||key.slot>32||key.serial<0||key.userid<=0||!key.player)return false;
        if(!key_||*key_!=key)
        {
            // Original new-player private calloc and ClientPut's entvars clear.
            key_=key;state_={};
        }
        // Original normal cooperative Spawn keeps the connection's first-draw,
        // angle and update gate, but resets the upper-body deadline to now.
        if(spawn)state_.reloadUntil.reset();
        output=state_;return true;
    }
    bool Store(RevolverConnection key,const RevolverPlayerState& value) noexcept
    {
        if(!key_||*key_!=key||!std::isfinite(value.cylinderAngle)||value.cylinderAngle<0||
            value.cylinderAngle>360||!std::isfinite(value.cylinderGate)||
            (value.reloadUntil&&!std::isfinite(*value.reloadUntil)))return false;
        state_=value;return true;
    }
    void Reset() noexcept { key_.reset();state_={}; }
private:
    std::optional<RevolverConnection> key_;
    RevolverPlayerState state_;
};
}
