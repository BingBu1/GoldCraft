// SPDX-License-Identifier: GPL-3.0-or-later
// Only engine services and unrelated vtable targets are substituted. Production
// Spawn/KeyValue/Collect, entity export macros, GetClassPtr and layouts are real.
#include <cstring>
#include <cstdlib>
#include <iostream>
#include "precompiled.h"

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals{};
const Vector g_vecZero{};
bool g_bAllowedCSBot{};

namespace
{
[[noreturn]] void Unused(const char* method)
{
    std::cerr << "UNEXPECTED ENGINE STUB: " << method << '\n'; std::abort();
}
}
// These two methods are empty in the actual current cbase.cpp as well.
void CBaseEntity::OnCreate() {}
void CBaseEntity::OnDestroy() {}

int CBaseEntity::Save(CSave&) { Unused("Save"); }
int CBaseEntity::Restore(CRestore&) { Unused("Restore"); }
void CBaseEntity::SetObjectCollisionBox() { Unused("SetObjectCollisionBox"); }
void CBaseEntity::TraceAttack(entvars_t*, float, Vector, TraceResult*, int) { Unused("TraceAttack"); }
BOOL CBaseEntity::TakeDamage(entvars_t*, entvars_t*, float, int) { Unused("TakeDamage"); }
BOOL CBaseEntity::TakeHealth(float, int) { Unused("TakeHealth"); }
void CBaseEntity::Killed(entvars_t*, int) { Unused("Killed"); }
void CBaseEntity::TraceBleed(float, Vector, TraceResult*, int) { Unused("TraceBleed"); }
int CBaseEntity::DamageDecal(int) { Unused("DamageDecal"); }
BOOL CBaseEntity::IsInWorld() { Unused("IsInWorld"); }
CBaseEntity* CBaseEntity::GetNextTarget() { Unused("GetNextTarget"); }
BOOL CBaseEntity::FVisible(CBaseEntity*) { Unused("FVisible(entity)"); }
BOOL CBaseEntity::FVisible(const Vector&) { Unused("FVisible(vector)"); }
void CCSEntity::FireBullets(int, Vector&, Vector&, Vector&, float, int, int, int, entvars_t*) { Unused("wrapper FireBullets"); }
void CCSEntity::FireBuckshots(ULONG, Vector&, Vector&, Vector&, float, int, int, entvars_t*) { Unused("wrapper FireBuckshots"); }
Vector CCSEntity::FireBullets3(Vector&, Vector&, float, float, int, int, int, float, entvars_t*, bool, int) { Unused("wrapper FireBullets3"); }
extern "C" int A_strcmp(const char* left, const char* right) { return std::strcmp(left, right); }
