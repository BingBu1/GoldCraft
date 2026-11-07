// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Independent of GameRules and edict storage. A retained callback may keep
// this token alive after ServerDeactivate, but can never reopen an old map.
// This is access authority only; it does not keep its C++ service owner alive.
class CSOLScenarioEntityAccess final
{
public:
    CSOLScenarioEntityAccess() = default;
    CSOLScenarioEntityAccess(const CSOLScenarioEntityAccess&) = delete;
    CSOLScenarioEntityAccess& operator=(const CSOLScenarioEntityAccess&) = delete;
    bool Allowed() const { return m_allowed; }
    void Revoke() { m_allowed = false; }

private:
    bool m_allowed{true};
};
