// SPDX-License-Identifier: GPL-3.0-or-later
// Link-only targets for the real SDK player hierarchy. Scanner execution may
// use the real constructors, RTTI, fields and inline Instance/edict methods;
// none of these unrelated gameplay virtual methods may execute in this test.
#include <cstdlib>
#include <iostream>
#include "precompiled.h"

namespace
{
[[noreturn]] void UnusedPlayer(const char* method)
{
    std::cerr << "UNEXPECTED PLAYER STUB: " << method << '\n'; std::abort();
}
}

void CBaseDelay::KeyValue(KeyValueData*) { UnusedPlayer("CBaseDelay::KeyValue"); }
int CBaseDelay::Save(CSave&) { UnusedPlayer("CBaseDelay::Save"); }
int CBaseDelay::Restore(CRestore&) { UnusedPlayer("CBaseDelay::Restore"); }
int CBaseAnimating::Save(CSave&) { UnusedPlayer("CBaseAnimating::Save"); }
int CBaseAnimating::Restore(CRestore&) { UnusedPlayer("CBaseAnimating::Restore"); }
void CBaseToggle::KeyValue(KeyValueData*) { UnusedPlayer("CBaseToggle::KeyValue"); }
int CBaseToggle::Save(CSave&) { UnusedPlayer("CBaseToggle::Save"); }
int CBaseToggle::Restore(CRestore&) { UnusedPlayer("CBaseToggle::Restore"); }

void CBaseMonster::KeyValue(KeyValueData*) { UnusedPlayer("CBaseMonster::KeyValue"); }
void CBaseMonster::TraceAttack(entvars_t*, float, Vector, TraceResult*, int) { UnusedPlayer("CBaseMonster::TraceAttack"); }
BOOL CBaseMonster::TakeDamage(entvars_t*, entvars_t*, float, int) { UnusedPlayer("CBaseMonster::TakeDamage"); }
BOOL CBaseMonster::TakeHealth(float, int) { UnusedPlayer("CBaseMonster::TakeHealth"); }
void CBaseMonster::Killed(entvars_t*, int) { UnusedPlayer("CBaseMonster::Killed"); }
float CBaseMonster::ChangeYaw(int) { UnusedPlayer("CBaseMonster::ChangeYaw"); }
BOOL CBaseMonster::HasHumanGibs() { UnusedPlayer("CBaseMonster::HasHumanGibs"); }
BOOL CBaseMonster::HasAlienGibs() { UnusedPlayer("CBaseMonster::HasAlienGibs"); }
void CBaseMonster::FadeMonster() { UnusedPlayer("CBaseMonster::FadeMonster"); }
void CBaseMonster::GibMonster() { UnusedPlayer("CBaseMonster::GibMonster"); }
Activity CBaseMonster::GetDeathActivity() { UnusedPlayer("CBaseMonster::GetDeathActivity"); }
void CBaseMonster::BecomeDead() { UnusedPlayer("CBaseMonster::BecomeDead"); }
BOOL CBaseMonster::ShouldFadeOnDeath() { UnusedPlayer("CBaseMonster::ShouldFadeOnDeath"); }
int CBaseMonster::IRelationship(CBaseEntity*) { UnusedPlayer("CBaseMonster::IRelationship"); }
void CBaseMonster::MonsterInitDead() { UnusedPlayer("CBaseMonster::MonsterInitDead"); }
void CBaseMonster::Look(int) { UnusedPlayer("CBaseMonster::Look"); }
CBaseEntity* CBaseMonster::BestVisibleEnemy() { UnusedPlayer("CBaseMonster::BestVisibleEnemy"); }
BOOL CBaseMonster::FInViewCone(CBaseEntity*) { UnusedPlayer("CBaseMonster::FInViewCone(entity)"); }
BOOL CBaseMonster::FInViewCone(const Vector*) { UnusedPlayer("CBaseMonster::FInViewCone(vector)"); }

void CBasePlayer::Spawn() { UnusedPlayer("CBasePlayer::Spawn"); }
void CBasePlayer::Precache() { UnusedPlayer("CBasePlayer::Precache"); }
int CBasePlayer::Save(CSave&) { UnusedPlayer("CBasePlayer::Save"); }
int CBasePlayer::Restore(CRestore&) { UnusedPlayer("CBasePlayer::Restore"); }
int CBasePlayer::ObjectCaps() { UnusedPlayer("CBasePlayer::ObjectCaps"); }
int CBasePlayer::Classify() { UnusedPlayer("CBasePlayer::Classify"); }
void CBasePlayer::TraceAttack(entvars_t*, float, Vector, TraceResult*, int) { UnusedPlayer("CBasePlayer::TraceAttack"); }
BOOL CBasePlayer::TakeDamage(entvars_t*, entvars_t*, float, int) { UnusedPlayer("CBasePlayer::TakeDamage"); }
BOOL CBasePlayer::TakeHealth(float, int) { UnusedPlayer("CBasePlayer::TakeHealth"); }
void CBasePlayer::Killed(entvars_t*, int) { UnusedPlayer("CBasePlayer::Killed"); }
void CBasePlayer::AddPoints(int, BOOL) { UnusedPlayer("CBasePlayer::AddPoints"); }
void CBasePlayer::AddPointsToTeam(int, BOOL) { UnusedPlayer("CBasePlayer::AddPointsToTeam"); }
BOOL CBasePlayer::AddPlayerItem(CBasePlayerItem*) { UnusedPlayer("CBasePlayer::AddPlayerItem"); }
BOOL CBasePlayer::RemovePlayerItem(CBasePlayerItem*) { UnusedPlayer("CBasePlayer::RemovePlayerItem"); }
int CBasePlayer::GiveAmmo(int, const char*, int) { UnusedPlayer("CBasePlayer::GiveAmmo"); }
void CBasePlayer::OnCreate() { UnusedPlayer("CBasePlayer::OnCreate"); }
void CBasePlayer::OnDestroy() { UnusedPlayer("CBasePlayer::OnDestroy"); }
const char* CBasePlayer::TeamID() { UnusedPlayer("CBasePlayer::TeamID"); }
BOOL CBasePlayer::FBecomeProne() { UnusedPlayer("CBasePlayer::FBecomeProne"); }
int CBasePlayer::Illumination() { UnusedPlayer("CBasePlayer::Illumination"); }
void CBasePlayer::ResetMaxSpeed() { UnusedPlayer("CBasePlayer::ResetMaxSpeed"); }
void CBasePlayer::Jump() { UnusedPlayer("CBasePlayer::Jump"); }
void CBasePlayer::Duck() { UnusedPlayer("CBasePlayer::Duck"); }
void CBasePlayer::PreThink() { UnusedPlayer("CBasePlayer::PreThink"); }
void CBasePlayer::PostThink() { UnusedPlayer("CBasePlayer::PostThink"); }
Vector CBasePlayer::GetGunPosition() { UnusedPlayer("CBasePlayer::GetGunPosition"); }
void CBasePlayer::UpdateClientData() { UnusedPlayer("CBasePlayer::UpdateClientData"); }
void CBasePlayer::ImpulseCommands() { UnusedPlayer("CBasePlayer::ImpulseCommands"); }
void CBasePlayer::RoundRespawn() { UnusedPlayer("CBasePlayer::RoundRespawn"); }
Vector CBasePlayer::GetAutoaimVector(float) { UnusedPlayer("CBasePlayer::GetAutoaimVector"); }
void CBasePlayer::Blind(float, float, float, int) { UnusedPlayer("CBasePlayer::Blind"); }
