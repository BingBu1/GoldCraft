// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace guest::cof
{
struct FlinchActivityHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*makeVectors)(void*) noexcept {};
    bool (*lastHitgroup)(void*, int&) noexcept {};
    // Return backend delivery separately from the model lookup result.
    bool (*lookup)(void*, int activity, int& sequence) noexcept {};
};
enum class FlinchActivityResult : unsigned char { Applied, Cancelled, MissingBackend, BackendFailed };

constexpr int FlinchActivityForHitgroup(int hitgroup) noexcept
{
    switch(hitgroup)
    {
    case 1: return 70;
    case 3: return 72;
    case 4: case 10: case 11: case 18: return 73;
    case 5: case 12: case 13: case 19: return 74;
    case 6: case 14: case 15: return 75;
    case 7: case 16: case 17: return 76;
    default: return 26;
    }
}

// CoF MP0036b91c /10089C80. This selects an original activity, not a host
// sequence or duration. Task29 and MaintainSchedule own its later installation.
inline FlinchActivityResult SelectFlinchActivity(const FlinchActivityHost& host, int& activity) noexcept
{
    if(!host.current||!host.makeVectors||!host.lastHitgroup||!host.lookup)
        return FlinchActivityResult::MissingBackend;
    const auto invoke=[&](auto callback,auto&&... args) noexcept {
        if(!host.current(host.context))return FlinchActivityResult::Cancelled;
        const bool delivered=callback(host.context,args...);
        if(!host.current(host.context))return FlinchActivityResult::Cancelled;
        return delivered?FlinchActivityResult::Applied:FlinchActivityResult::BackendFailed;
    };
    auto result=invoke(host.makeVectors);
    if(result!=FlinchActivityResult::Applied)return result;
    int hitgroup{};
    result=invoke(host.lastHitgroup,hitgroup);
    if(result!=FlinchActivityResult::Applied)return result;
    int selected=FlinchActivityForHitgroup(hitgroup),sequence{};
    result=invoke(host.lookup,selected,sequence);
    if(result!=FlinchActivityResult::Applied)return result;
    if(sequence==-1)
    {
        result=invoke(host.lookup,26,sequence);
        if(result!=FlinchActivityResult::Applied)return result;
        selected=sequence==-1?59:26;
    }
    activity=selected;
    return FlinchActivityResult::Applied;
}
}
