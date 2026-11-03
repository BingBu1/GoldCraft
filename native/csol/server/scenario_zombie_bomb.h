// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_monster_factory.h"
#include <array>

class CCSOLScenarioRuntime;
struct CSOLZombieBombState;

inline constexpr const char* CSOLZombieBombModel = "models/w_zombibomb.mdl";
inline constexpr const char* CSOLZombieBombEvent = "events/createexplo.sc";
inline constexpr std::array<const char*, 2> CSOLZombieBombBounceSounds{
    "zombi/Zombi_Bomb_Bounce_1.wav", "zombi/Zombi_Bomb_Bounce_2.wav"};

// The flight belongs to a map/round, independently of the throwing monster.
// Attribution must resolve the creator's pool lease on every callback.
struct CSOLZombieBombHitSource
{
    CBaseEntity* (*resolve)(const void*);
    CBaseEntity* (*creator)(const void*);
    const void* context;
    CBaseEntity* Current() const { return resolve ? resolve(context) : nullptr; }
    CBaseEntity* Creator() const { return creator ? creator(context) : nullptr; }
};

bool CSOL_ApplyZombieBombDamage(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const CSOLZombieBombHitSource& source, float damage, csol::scenario::ConfigError& error);

class CCSOLZombieBomb final : public CBaseAnimating
{
public:
    void Spawn() override;
    void OnDestroy() override;
    void FlightThink();
    void BounceTouch(CBaseEntity* other);
    bool IsDamageSource() const;
    CBaseEntity* DamageCreator() const;
    static bool Create(CSOLMonsterSpawnCall& call,
        const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
        const Vector& velocity, float damage, csol::scenario::ConfigError& error);
private:
    friend struct CSOLZombieBombState;
    void Explode();
    std::shared_ptr<CSOLZombieBombState> m_state;
};
