// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_gameplay.h"
#include "../scenario/wave_events.hpp"

#include <cstdlib>
#include <list>
#include <algorithm>
#include <cmath>

CCSOLScenarioGameplay::CCSOLScenarioGameplay(CCSOLScenarioSession& session,
    CCSOLMonsterDeathServices& deathServices, CCSOLSpawnClearDeaths& spawnDeaths,
    CCSOLMonsterFactoryServices& factoryServices, int mapId,
    std::shared_ptr<const CCSOLMonsterSpawnOwner> owner)
    : m_session(session), m_owner(owner), m_hasOwner(owner != nullptr), m_spawnDeaths(spawnDeaths),
      m_deaths(session.Map(), session.Monsters(), session.Models(), session.Messages(), deathServices, mapId),
      m_occupancy(session.Monsters(), spawnDeaths), m_spawns(m_occupancy),
      m_factory(session.Map(), session.Monsters(), session.Models(), m_occupancy,
          m_deaths.Requests(), factoryServices, session.Messages(), std::move(owner)) {}

bool CCSOLScenarioGameplay::OwnerCurrent() const
{
    const auto owner = m_owner.lock();
    return !m_hasOwner || (owner && owner->OwnerCurrent());
}

bool CCSOLScenarioGameplay::Fail(const char* reason)
{
    if (m_error.message.empty()) m_error.message = reason && *reason ? reason : "scenario gameplay operation failed";
    return false;
}

bool CCSOLScenarioGameplay::Activate()
{
    const auto owner = m_owner.lock();
    if (!OwnerCurrent()) return false;
    if (m_active || m_updating || m_resetting ||
        m_session.State() != CSOLScenarioSessionState::Active)
        return Fail("scenario gameplay activated outside its collected map session");
    const auto* tables = m_session.Map().Tables();
    if (!tables || !SupportsMap(tables->mapName, tables->scenarioType))
        return Fail("scenario gameplay currently requires zs_deadend subtype1");
    const auto navigation = m_session.ScenarioNavigation();
    if (!navigation || !navigation->World().Ready())
        return Fail("scenario gameplay needs its loaded navigation graph");
    const auto areas = navigation->Areas();
    const std::list<CNavArea*> gridAreas(areas.begin(), areas.end());
    if (!m_occupancy.Initialize(gridAreas)) return Fail(m_occupancy.Error().c_str());
    m_spawns.BindEntityAccess(m_session.Map().EntityAccess());
    if (!m_spawns.BeginMap()) return Fail("scenario player spawn manager could not start");
    m_spawnGroupOffset = 0;
    m_error = {};
    ++m_generation;
    m_active = true;
    return true;
}

bool CCSOLScenarioGameplay::BeginRound(std::size_t roundIndex, float now, float freezeTime,
    csol::scenario::TriggerWaveEvents& events)
{
    const auto owner = m_owner.lock();
    if (!Active() || m_updating || m_resetting || m_selecting) return false;
    struct ResetScope { bool& flag; ~ResetScope() { flag = false; } } scope{m_resetting};
    m_resetting = true;
    ++m_generation;
    const auto generation = m_generation;
    // Death callbacks carry this dispatcher's generation. Cancel them before
    // resetting slots, then release caches which still borrow those slots.
    if (!m_deaths.Reset()) return Fail(m_deaths.Error().message.c_str());
    if (!m_session.Monsters().RestartRound()) return Fail(m_session.Monsters().Error().c_str());
    if (!m_factory.Reset()) return Fail(m_factory.Error().message.c_str());
    if (!m_occupancy.ResetGrid()) return Fail(m_occupancy.Error().c_str());
    m_spawnGroupOffset = 0; // mode15/subtype1; the next round supplies its own group.
    if (!m_session.BeginRound(roundIndex, now, freezeTime, events))
    {
        if (!Active() || generation != m_generation || m_session.Error().detail.message.empty()) return false;
        return Fail(m_session.Error().detail.message.c_str());
    }
    return Active() && generation == m_generation;
}

void CCSOLScenarioGameplay::Dispatch(void* context, const csol::scenario::WaveRow& row,
    csol::scenario::RowClock&)
{
    auto& self = *static_cast<CCSOLScenarioGameplay*>(context);
    if (!self.Active()) return;
    const auto generation = self.m_generation;
    if (row.IsEvent())
    {
        CCSOLScenarioEventSink events(self.m_session.Monsters(), self.m_session.Messages());
        csol::scenario::ConfigError error;
        const auto result = csol::scenario::DispatchWaveEvent(row, events, error);
        if (self.m_generation != generation || !self.Active()) return;
        if (result == csol::scenario::WaveEventResult::Invalid)
            self.Fail(error.message.c_str());
        else if (!events.Valid()) self.Fail("scenario wave event could not be sent");
        return;
    }
    int type{};
    if (!csol::scenario::SelectMonsterType(row, std::rand(), type))
    {
        self.Fail("scenario wave has an invalid weighted monster selection");
        return;
    }
    // The subtype1 controller creates the enemy team from the selected row.
    // The row clock has already advanced; pool/quota refusal is not a retry.
    // Mode15's loader initializes runtime row+58 to 1; only mode17 reads
    // the additional CSV column. Do not borrow zombiRatio as this argument.
    const auto result = self.m_factory.Spawn(TERRORIST, type, row.spawnPoint, 1);
    if (self.m_generation != generation || !self.Active()) return;
    if (result == CSOLMonsterSpawnResult::Failed)
        self.Fail(self.m_factory.Error().message.c_str());
}

bool CCSOLScenarioGameplay::Update(CHalfLifeMultiplay& rules, float thinkTime,
    int aliveCtCount, csol::scenario::TriggerWaveEvents& events)
{
    const auto owner = m_owner.lock();
    if (!Active() || m_updating || m_resetting || m_selecting) return false;
    struct UpdateScope { bool& flag; ~UpdateScope() { flag = false; } } scope{m_updating};
    m_updating = true;
    const auto generation = m_generation;
    if (!rules.IsGameStarted() || rules.IsFreezePeriod()) return true;
    if (rules.m_bRoundTerminating)
    {
        bool due{};
        if (!m_session.Map().AdvanceMonsterDrain(gpGlobals->time, due))
            return Fail("scenario round drain clock is invalid");
        if (!due) return true;
        const auto round = m_session.Map().RoundGeneration();
        const auto requests = m_deaths.Requests().Generation();
        const auto result = m_deaths.Drain(3, true);
        if (m_generation != generation || !Active() ||
            m_session.Map().RoundGeneration() != round ||
            m_deaths.Requests().Generation() != requests) return false;
        if (!result.valid) return Fail(result.error.message.c_str());
        if (!result.selected) m_session.Map().CompleteMonsterDrain();
        return true;
    }
    if (!m_spawns.UpdateOccupancy()) return Fail("scenario live occupancy refresh failed");
    if (m_generation != generation || !Active()) return false;
    const auto frame = m_session.Map().UpdateFrame(rules, thinkTime, aliveCtCount, Dispatch, this, events);
    if (m_generation != generation || !Active()) return false;
    if (!frame.valid)
    {
        if (m_session.Map().Error().detail.message.empty()) return false;
        return Fail(m_session.Map().Error().detail.message.c_str());
    }
    if (!m_deaths.Error().message.empty()) return Fail(m_deaths.Error().message.c_str());
    if (!m_deaths.Requests().Error().empty()) return Fail(m_deaths.Requests().Error().c_str());
    return true;
}

bool CCSOLScenarioGameplay::ScheduleRoundDrain(float now)
{
    return Active() && m_session.Map().ScheduleMonsterDrain(now, 0);
}

CCSOLPlayerSpawn* CCSOLScenarioGameplay::SelectSpawn(CBasePlayer& player)
{
    const auto owner = m_owner.lock();
    if (!Active() || m_selecting || m_session.Map().State() != CSOLScenarioMapState::RoundActive) return nullptr;
    struct SelectScope { bool& flag; ~SelectScope() { flag = false; } } scope{m_selecting};
    m_selecting = true;
    const auto generation = m_generation;
    csol::scenario::ConfigError error;
    auto* spot = m_spawns.Select(m_session.Map(), player, m_spawnGroupOffset, error);
    if (!Active() || generation != m_generation) return nullptr;
    if (!m_deaths.Error().message.empty() || !m_deaths.Requests().Error().empty())
    {
        Fail(m_deaths.Error().message.empty() ? m_deaths.Requests().Error().c_str() :
            m_deaths.Error().message.c_str());
        return nullptr;
    }
    if (!spot) Fail(error.message.empty() ? "scenario player spawn selection failed" : error.message.c_str());
    return spot;
}

bool CCSOLScenarioGameplay::Deactivate()
{
    m_active = false;
    ++m_generation;
    // Always cancel before the enclosing Session releases old-map edicts.
    if (!m_deaths.Reset()) return Fail(m_deaths.Error().message.c_str());
    if (m_updating || m_resetting || m_selecting)
        return false; // Deferred cleanup, not a gameplay failure.
    if (!m_session.Map().EntityAccessAllowed())
    {
        // Do not run round-reset effects against a map being freed. Keep type
        // caches until the engine's actual OnDestroy calls have detached them.
        if (!m_session.Monsters().Shutdown() || !m_session.Monsters().ForgetMap()) return false;
        if (!m_factory.Reset() || !m_occupancy.Shutdown()) return false;
        return true;
    }
    if (!m_session.Monsters().RestartRound()) return Fail(m_session.Monsters().Error().c_str());
    if (!m_factory.Reset()) return Fail(m_factory.Error().message.c_str());
    if (!m_occupancy.Shutdown()) return Fail(m_occupancy.Error().c_str());
    return true;
}

bool CCSOLScenarioGameplay::ResolveSpawnOverlap(CSOLMonsterSpawnCall& call,
    std::span<const std::size_t> protectedSlots, csol::scenario::ConfigError& error,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    #pragma clang fp contract(off)
    const auto owner = m_owner.lock();
    auto* current = call.Current();
    if (!Active() || !current || (ownerValid && !ownerValid(ownerContext))) return false;
    // Golden membership is queried live in CSO, including mutations caused
    // by earlier deaths. A borrowed snapshot cannot implement that contract.
    // This composition currently accepts only explicitly empty membership;
    // the concrete services must also hold Golden disabled during the call.
    if (!protectedSlots.empty())
    {
        error.message = "scenario spawn overlap needs the live Golden membership service";
        return false;
    }
    const auto generation = m_generation;
    const auto same = [&]()
    {
        return (!ownerValid || ownerValid(ownerContext)) && Active() &&
            generation == m_generation && call.Current();
    };
    const auto fail = [&](const char* reason)
    {
        if (same() && error.message.empty()) error.message = reason;
        return false;
    };
    const auto overlap = [](const entvars_t& older, const entvars_t& spawned)
    {
        const Vector difference = older.origin - spawned.origin;
        return std::fabs(difference.x) < (older.maxs.x + spawned.maxs.x) - 3.0f &&
            std::fabs(difference.y) < (older.maxs.y + spawned.maxs.y) - 3.0f &&
            std::fabs(difference.z) < (older.maxs.z + spawned.maxs.z) - 3.0f;
    };
    const auto clear = [&](CCSOLScenarioMonster& victim)
    {
        const EHANDLE handle(&victim);
        const auto lease = m_session.Monsters().Lease(victim);
        const bool prepared = m_spawnDeaths.BeforeSpawnClear(victim);
        if (!same()) return false;
        if (!prepared) return fail("scenario overlap pre-death service failed");
        auto* live = dynamic_cast<CCSOLScenarioMonster*>(handle.Get<CBaseEntity>());
        if (!live || m_session.Monsters().Lease(*live) != lease)
            return fail("scenario overlap pre-death changed the victim lease");
        CSOLMonsterDeathArguments arguments;
        arguments.suppressSound = true;
        const auto result = m_deaths.Requests().Request(*live, arguments);
        if (!same()) return false;
        if (result == CSOLDeathRequestResult::Failed)
            return fail(m_deaths.Requests().Error().c_str());
        return true;
    };
    std::array<CCSOLScenarioOccupancy::Neighbor, CCSOLMonsterPool::Capacity> neighbors{};
    std::size_t count{};
    if (!m_occupancy.SpawnNeighbors(current->pev->origin, neighbors, count))
        return fail(m_occupancy.Error().c_str());
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!same()) return false;
        current = call.Current();
        auto* older = m_occupancy.ResolveNeighbor(neighbors[i]);
        if (!older || older == current || older->pev->solid == SOLID_NOT ||
            !overlap(*older->pev, *current->pev)) continue;
        const auto* type = current->Type();
        if (!type) return fail("scenario overlap lost its spawn type");
        const auto* olderType = older->Type();
        if (olderType && (olderType->skill >= 1000 || olderType->eventClass > 149)) continue;
        if (!clear(*older)) return false;
    }
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        if (!same()) return false;
        auto* player = dynamic_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
        if (!same()) return false;
        if (!player) continue;
        const EHANDLE handle(player);
        const bool alive = player->IsAlive() != FALSE;
        if (!same()) return false;
        player = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
        current = call.Current();
        if (!alive || !player || !overlap(*player->pev, *current->pev)) continue;
        const auto* type = current->Type();
        if (!type) return fail("scenario player overlap lost its spawn type");
        if (type->priority != 0)
        {
            player->TakeDamage(player->pev, player->pev, 99999.0f, 0);
            if (!same()) return false;
            continue;
        }
        if (type->skill >= 1000 || type->eventClass > 149) return true;
        return clear(*current);
    }
    return same();
}
