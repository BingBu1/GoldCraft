// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../scenario/custom/refund_cap.hpp"

namespace csol::scenario::custom
{
class RefundAccountScope;
// This tag is created only by the monster hit-money channel, after RNG/XP.
// Its owner pins the map ledger and the synchronous validity callback.
class RefundAwardScope final
{
public:
    using Current = bool (*)(const void*);
    RefundAwardScope(RefundCap*, RefundPair, const void* actor, Current, const void* context);
    ~RefundAwardScope();
    RefundAwardScope(const RefundAwardScope&) = delete;
    RefundAwardScope& operator=(const RefundAwardScope&) = delete;
private:
    friend class RefundAccountScope;
    RefundCap* ledger_{};
    RefundPair pair_;
    const void* actor_{};
    Current current_{};
    const void* context_{};
    RefundAwardScope* previous_{};
    bool claimed_{};
};

// Instantiate before ReAPI prehooks. Every nested ordinary AddAccount hides
// the outer tag; callOriginal still uses this wrapper's one-shot transaction.
class RefundAccountScope final
{
public:
    explicit RefundAccountScope(const void* actor);
    ~RefundAccountScope();
    RefundAccountScope(const RefundAccountScope&) = delete;
    RefundAccountScope& operator=(const RefundAccountScope&) = delete;
    static bool Prepare(const void* actor, int& amount);
    // Native clamping has finished; commit before the first message callback.
    static void Commit(const void* actor, int before, int after);
private:
    RefundAccountScope* previous_{};
    RefundAwardScope* award_{};
    const void* actor_{};
    int approved_{};
    bool started_{}, committed_{};
};
}
