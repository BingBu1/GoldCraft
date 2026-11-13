// SPDX-License-Identifier: GPL-3.0-or-later
#include "damage_refund_account.h"

#include <algorithm>
#include <cstdint>

namespace csol::scenario::custom
{
namespace
{
thread_local RefundAwardScope* pending{};
thread_local RefundAccountScope* active{};
}
RefundAwardScope::RefundAwardScope(RefundCap* ledger, RefundPair pair,
    const void* actor, Current current, const void* context)
    : ledger_(ledger), pair_(pair), actor_(actor), current_(current), context_(context), previous_(pending)
{
    pending = ledger ? this : nullptr;
}
RefundAwardScope::~RefundAwardScope() { pending = previous_; }

RefundAccountScope::RefundAccountScope(const void* actor) : previous_(active), actor_(actor)
{
    active = nullptr;
    if (pending && !pending->claimed_)
    {
        award_ = pending;
        award_->claimed_ = true;
        active = this;
    }
}
RefundAccountScope::~RefundAccountScope() { active = previous_; }

bool RefundAccountScope::Prepare(const void* actor, int& amount)
{
    if (!active) return true;
    auto& scope = *active;
    if (scope.started_) return false;
    scope.started_ = true;
    auto& award = *scope.award_;
    if (actor != scope.actor_ || actor != award.actor_ || !award.current_ || !award.current_(award.context_))
        return false;
    const auto remaining = award.ledger_->Remaining(award.pair_);
    if (!remaining) return false;
    if (amount > 0) amount = std::min(amount, *remaining);
    scope.approved_ = std::max(amount, 0);
    return true;
}
void RefundAccountScope::Commit(const void* actor, int before, int after)
{
    if (!active || !active->started_ || active->committed_ || actor != active->actor_) return;
    auto& scope = *active;
    scope.committed_ = true;
    // Account normalization is not newly granted hit money. A negative or
    // zero request cannot consume budget by correcting an invalid balance.
    const auto credited = std::min<std::int64_t>(scope.approved_,
        std::max<std::int64_t>(0, std::int64_t(after) - before));
    scope.award_->ledger_->Commit(scope.award_->pair_, static_cast<int>(credited));
}
}
