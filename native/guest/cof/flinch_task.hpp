// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "flinch_activity.hpp"
#include <cstdint>
#include <string_view>

namespace guest::cof
{
struct FlinchTaskState
{
    std::uint32_t conditions{};
    int status{},idealActivity{},sequenceFinished{};
};
struct FlinchTaskOwner
{
    std::string_view classname;
    int skin{};
};
inline void CompleteSmallFlinchTask(FlinchTaskState& state) noexcept
{ if(!(state.conditions&0x40000000U))state.status=4; }

// Original StartTask1000BC60 case29. The scheduler already owns task startup
// and later SetActivity; this operation only updates the original task fields.
inline FlinchActivityResult BeginSmallFlinchTask(FlinchTaskState& state,
    const FlinchTaskOwner& owner,const FlinchActivityHost& host) noexcept
{
    if(!host.current)return FlinchActivityResult::MissingBackend;
    if(!host.current(host.context))return FlinchActivityResult::Cancelled;
    if(((owner.classname=="monster_bosschainsaw"||owner.classname=="monster_sewerboss")&&owner.skin==1)||
        owner.classname=="monster_booksimon"||owner.classname=="monster_doctorboss")
        return FlinchActivityResult::Applied;
    int activity{};
    auto result=SelectFlinchActivity(host,activity);
    if(result!=FlinchActivityResult::Applied)return result;
    if(activity==59)
    {
        CompleteSmallFlinchTask(state);
        return FlinchActivityResult::Applied;
    }
    // The original calls the selector again, rather than reusing its probe.
    result=SelectFlinchActivity(host,activity);
    if(result!=FlinchActivityResult::Applied)return result;
    state.idealActivity=activity;
    return FlinchActivityResult::Applied;
}

// Original RunTask1000B2F0 case29: the real animation completion flag is the
// only completion gate here. No authored stun duration or fallback timeout.
inline FlinchActivityResult RunSmallFlinchTask(FlinchTaskState& state,
    const FlinchActivityHost& host) noexcept
{
    if(!host.current)return FlinchActivityResult::MissingBackend;
    if(!host.current(host.context))return FlinchActivityResult::Cancelled;
    if(state.sequenceFinished)CompleteSmallFlinchTask(state);
    return FlinchActivityResult::Applied;
}
}
