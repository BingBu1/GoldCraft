// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_player_spawn.h"
#include "scenario_monster_pool.h"

#include <list>
#include <vector>

class CNavArea;

// These operations must be supplied by the actual scenario death/round owner.
// BeforeSpawnClear updates the optional special-monster collection. Death must
// use null inflictor/attacker, suppress the death sound and use the original
// default damage context; a declined/delayed death is not a blocked spawn.
class CCSOLSpawnClearDeaths
{
public:
    virtual ~CCSOLSpawnClearDeaths() = default;
    virtual void BeforeSpawnClear(CCSOLScenarioMonster& monster) = 0;
    virtual void RequestSpawnClearDeath(CCSOLScenarioMonster& monster) = 0;
};

// Mode 15 occupancy is a separate 39.37-unit grid derived from the loaded NAV
// area extents. It is not CNavAreaGrid and does not use its 300-unit cells.
// Own alongside the pool; the pool and death callbacks outlive this object.
class CCSOLScenarioOccupancy final : public CCSOLScenarioSpawnSpace
{
public:
    CCSOLScenarioOccupancy(CCSOLMonsterPool& pool, CCSOLSpawnClearDeaths& deaths)
        : m_pool(pool), m_deaths(deaths) {}
    bool Initialize(const std::list<CNavArea*>& areas);
    bool ResetGrid();
    bool Shutdown();
    // Call at the original attribute stage, before spawn targets or behavior
    // callbacks. Moving a slot or periodic refreshing never changes priority.
    bool ApplyPriority(CCSOLScenarioMonster& monster);
    // Only the grid-registration portion of the factory's post-origin step;
    // the original separate spawn-overlap resolution still belongs to it.
    bool Register(CCSOLScenarioMonster& monster, bool allowReturnedDeath = false);
    struct Neighbor
    {
        std::size_t slot{};
        std::uint64_t lease{};
    };
    // Movement uses the original radius1 query, without MakeRoom's solid or
    // priority filters. Entries follow row, column, then the existing chain.
    bool MovementNeighbors(const Vector& origin,
        std::array<Neighbor, CCSOLMonsterPool::Capacity>& output, std::size_t& count);
    // Factory overlap uses radius2 and processes deaths in snapshot order.
    // Unlike player MakeRoom it must not pre-reject the entire query.
    bool SpawnNeighbors(const Vector& origin,
        std::array<Neighbor, CCSOLMonsterPool::Capacity>& output, std::size_t& count);
    CCSOLScenarioMonster* ResolveNeighbor(const Neighbor& neighbor) const;
    bool RefreshOccupancy() override;
    bool MakeRoom(const Vector& origin) override;
    bool Ready() const override
    {
        return m_ready && m_error.empty() &&
            m_pool.State() == CSOLMonsterPoolState::Ready && !m_pool.Busy();
    }
    const std::string& Error() const { return m_error; }

private:
    struct Slot
    {
        std::int8_t previous{-1}, next{-1}, priority{};
        std::int16_t x{-1}, y{-1};
        const csol::scenario::MonsterType* type{};
        std::uint64_t lease{};
    };
    bool Fail(const char* message);
    bool PoolReady();
    bool Cell(const Vector& origin, int& x, int& y);
    bool Move(std::size_t slot, const Vector& origin);
    bool Neighbors(int x, int y, int radius, std::array<std::size_t, CCSOLMonsterPool::Capacity>& slots,
        std::size_t& count);
    std::int8_t& Head(int x, int y) { return m_heads[static_cast<std::size_t>(y) * m_width + x]; }
    bool Index(int index) const { return index >= 0 && index < static_cast<int>(m_slots.size()); }

    CCSOLMonsterPool& m_pool;
    CCSOLSpawnClearDeaths& m_deaths;
    std::array<Slot, CCSOLMonsterPool::Capacity> m_slots{};
    std::vector<std::int8_t> m_heads;
    float m_minX{}, m_minY{}, m_inverseCell{};
    int m_width{}, m_height{};
    bool m_ready{}, m_busy{};
    std::string m_error;
};
