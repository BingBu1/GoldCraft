// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>

namespace guest::cof
{
// Authoritative process-local identity; never a packet field or persistent ID.
struct FirstDrawConnection
{
    std::uint64_t map{};
    unsigned slot{};
    int serial{},userid{};
    std::uintptr_t player{};
    bool operator==(const FirstDrawConnection&) const = default;
};
class FirstDrawLifecycle final
{
public:
    bool Bind(FirstDrawConnection key,bool& output) noexcept
    {
        if(!key.map||!key.slot||key.slot>32||key.serial<0||key.userid<=0||!key.player)return false;
        if(!key_||*key_!=key){key_=key;drawn_=false;}
        // Original ordinary cooperative Spawn/death retains the private flag.
        output=drawn_;return true;
    }
    bool MarkDrawn(FirstDrawConnection key) noexcept
    {
        if(!key_||*key_!=key)return false;
        drawn_=true;return true;
    }
    void Reset() noexcept {key_.reset();drawn_=false;}
private:
    std::optional<FirstDrawConnection> key_;
    bool drawn_{};
};
}
