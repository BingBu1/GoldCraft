// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

class CBaseEntity;
class CBaseDelay;

// Stack-only optional cancellation around the stock breakable/target code.
// Ordinary CS calls have no scope and retain their original behavior.
class CSOLBreakableScope
{
public:
    using Guard = std::function<bool()>;
    explicit CSOLBreakableScope(Guard valid, bool newOperation = false, bool retainForDelay = true);
    ~CSOLBreakableScope();
    CSOLBreakableScope(const CSOLBreakableScope&) = delete;
    CSOLBreakableScope& operator=(const CSOLBreakableScope&) = delete;
    static bool Current(const CBaseEntity* entity);
    // Delayed target chains retain the same guards, never a stack address.
    static Guard Capture();
    static Guard Follow(const CBaseEntity* entity);
private:
    bool Valid() const;
    Guard m_valid;
    bool m_newOperation;
    bool m_retainForDelay;
    CSOLBreakableScope* m_previous;
    static thread_local CSOLBreakableScope* s_active;
};

CBaseDelay* CSOLCreateBreakableDelay(CSOLBreakableScope::Guard& alive);
