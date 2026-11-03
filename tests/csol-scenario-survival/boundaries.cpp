// SPDX-License-Identifier: GPL-3.0-or-later
// Link-only edges of trigger_clearpoint, which shares scenario_trigger.cpp
// with the real marker class. Type0 has no TRG volumes or clearpoint callback;
// entering any of these boundaries is a test failure, never a successful stub.
#include "precompiled.h"
#include "game_rules_gate.h"
#include "scenario_breakable_guard.h"
#include <cstdlib>
#include <cstdio>

namespace
{
[[noreturn]] void Unexpected(const char* name)
{
    std::fprintf(stderr, "UNEXPECTED SURVIVAL BOUNDARY: %s\n", name);
    std::abort();
}
}
CSOLScenarioSdkScope::CSOLScenarioSdkScope() { Unexpected("SDK rules scope"); }
CSOLScenarioSdkScope::~CSOLScenarioSdkScope() { Unexpected("SDK rules scope release"); }
bool CSOLScenarioSdkScope::Current() const { Unexpected("SDK rules identity"); }
CSOLBreakableScope::CSOLBreakableScope(Guard, bool, bool) { Unexpected("clearpoint scope"); }
CSOLBreakableScope::~CSOLBreakableScope() { Unexpected("clearpoint scope release"); }
bool CSOLBreakableScope::Current(const CBaseEntity*) { Unexpected("clearpoint identity"); }
void CBaseTrigger::InitTrigger() { Unexpected("native trigger initialization"); }
void CBaseTrigger::KeyValue(KeyValueData*) { Unexpected("native trigger keyvalue"); }
void CBaseTrigger::ActivateMultiTrigger(CBaseEntity*) { Unexpected("native trigger activation"); }
