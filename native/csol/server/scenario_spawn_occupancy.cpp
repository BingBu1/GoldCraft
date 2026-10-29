// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_spawn_occupancy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace
{
struct OccupancyOperation
{
    bool& busy;
    explicit OccupancyOperation(bool& value) : busy(value) { busy = true; }
    ~OccupancyOperation() { busy = false; }
};

bool Finite(const Vector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool ExtentSize(float low, float high, float inverseCell, int& count)
{
    const float difference = high - low;
    const float cells = difference * inverseCell;
    // Original records use signed shorts for grid coordinates. Do not create
    // a grid whose legal index turns into a negative record coordinate.
    if (!std::isfinite(cells) || cells < 0 || cells >= 32768.0f) return false;
    count = static_cast<int>(cells) + 1;
    return true;
}
}

bool CCSOLScenarioOccupancy::Fail(const char* message)
{
    if (m_error.empty()) m_error = message;
    return false;
}

bool CCSOLScenarioOccupancy::PoolReady()
{
    if (!m_pool.EntityAccessAllowed() || m_pool.State() != CSOLMonsterPoolState::Ready || m_pool.Busy())
        return Fail("scenario occupancy pool is unavailable");
    return true;
}

bool CCSOLScenarioOccupancy::Initialize(const std::list<CNavArea*>& areas)
{
    if (m_busy || m_ready) return false;
    OccupancyOperation operation(m_busy);
    m_error.clear();
    if (!PoolReady()) return false;
    if (areas.empty()) return Fail("scenario occupancy requires loaded NAV areas");
    float minX = std::numeric_limits<float>::max(), minY = minX;
    float maxX = std::numeric_limits<float>::lowest(), maxY = maxX;
    for (const auto* area : areas)
    {
        if (!area) return Fail("scenario NAV has a null area");
        const auto& extent = *area->GetExtent();
        if (!std::isfinite(extent.lo.x) || !std::isfinite(extent.lo.y) ||
            !std::isfinite(extent.hi.x) || !std::isfinite(extent.hi.y))
            return Fail("scenario NAV has invalid XY extents");
        // Original maps can contain individually reversed corners (deadend
        // area 3148). Preserve the original low/high roles when aggregating;
        // validate the resulting grid, without normalizing the NAV geometry.
        minX = std::min(minX, extent.lo.x);
        minY = std::min(minY, extent.lo.y);
        maxX = std::max(maxX, extent.hi.x);
        maxY = std::max(maxY, extent.hi.y);
    }
    // Original mode-15 scale is 1.0. Its reciprocal is computed in double and
    // rounded to float before coordinate subtraction/multiplication.
    const float inverse = static_cast<float>(1.0 / static_cast<double>(39.369998931884766f));
    int width{}, height{};
    if (!ExtentSize(minX, maxX, inverse, width) || !ExtentSize(minY, maxY, inverse, height))
        return Fail("scenario occupancy grid exceeds signed-short coordinates");
    const auto cells = static_cast<std::size_t>(width) * height;
    // Invalid NAV extents must not exhaust the x86 server address space. This
    // explicit 16 MiB bound is a host resource limit, not an original CSO cap.
    if (cells > 16 * 1024 * 1024) return Fail("scenario occupancy grid exceeds host memory limit");
    try { m_heads.assign(cells, -1); }
    catch (const std::bad_alloc&) { return Fail("scenario occupancy grid allocation failed"); }
    m_minX = minX;
    m_minY = minY;
    m_inverseCell = inverse;
    m_width = width;
    m_height = height;
    for (auto& slot : m_slots)
    {
        slot.previous = slot.next = -1;
        slot.x = slot.y = -1;
    }
    m_ready = true;
    return true;
}

bool CCSOLScenarioOccupancy::ResetGrid()
{
    if (m_busy || !Ready()) return false;
    std::fill(m_heads.begin(), m_heads.end(), -1);
    for (auto& slot : m_slots)
    {
        slot.previous = slot.next = -1;
        slot.x = slot.y = -1;
    }
    return true;
}

bool CCSOLScenarioOccupancy::Shutdown()
{
    if (m_busy) return false;
    m_ready = false;
    m_heads.clear();
    m_slots = {};
    m_width = m_height = 0;
    m_error.clear();
    return true;
}

bool CCSOLScenarioOccupancy::ApplyPriority(CCSOLScenarioMonster& monster)
{
    if (m_busy || !Ready() || !PoolReady()) return false;
    if (!m_pool.Borrowed(monster) || !monster.Type())
        return Fail("scenario occupancy attributes have no borrowed type");
    auto& slot = m_slots[monster.Slot()];
    slot.priority = monster.Type()->priority;
    slot.type = monster.Type();
    slot.lease = m_pool.Lease(monster);
    return true;
}

bool CCSOLScenarioOccupancy::Cell(const Vector& origin, int& x, int& y)
{
    if (!Finite(origin)) return Fail("scenario occupancy received a nonfinite position");
    const auto axis = [&](float position, float minimum, int count, int& result)
    {
        const float difference = position - minimum;
        const float scaled = difference * m_inverseCell;
        if (!std::isfinite(scaled) || scaled < -2147483648.0f || scaled >= 2147483648.0f)
            return false;
        result = std::clamp(static_cast<int>(scaled), 0, count - 1);
        return true;
    };
    if (!axis(origin.x, m_minX, m_width, x) || !axis(origin.y, m_minY, m_height, y))
        return Fail("scenario occupancy coordinate conversion overflowed");
    return true;
}

bool CCSOLScenarioOccupancy::Move(std::size_t index, const Vector& origin)
{
    int x{}, y{};
    if (!Cell(origin, x, y)) return false;
    auto& slot = m_slots[index];
    if (slot.x == x && slot.y == y) return true;
    if (slot.x != -1 && slot.y != -1)
    {
        if (slot.x < 0 || slot.x >= m_width || slot.y < 0 || slot.y >= m_height ||
            (slot.previous != -1 && !Index(slot.previous)) ||
            (slot.next != -1 && !Index(slot.next)))
            return Fail("scenario occupancy slot has corrupt links");
        if (slot.previous == -1)
        {
            if (Head(slot.x, slot.y) != static_cast<std::int8_t>(index))
                return Fail("scenario occupancy head does not own its slot");
        }
        else if (m_slots[slot.previous].next != static_cast<std::int8_t>(index))
            return Fail("scenario occupancy predecessor does not own its slot");
        if (slot.next != -1 && m_slots[slot.next].previous != static_cast<std::int8_t>(index))
            return Fail("scenario occupancy successor does not own its slot");
        if (slot.previous == -1) Head(slot.x, slot.y) = slot.next;
        else m_slots[slot.previous].next = slot.next;
        if (slot.next != -1) m_slots[slot.next].previous = slot.previous;
    }
    const auto head = Head(x, y);
    if (head != -1 && !Index(head)) return Fail("scenario occupancy destination head is corrupt");
    slot.x = static_cast<std::int16_t>(x);
    slot.y = static_cast<std::int16_t>(y);
    slot.previous = -1;
    slot.next = head;
    if (head != -1) m_slots[head].previous = static_cast<std::int8_t>(index);
    Head(x, y) = static_cast<std::int8_t>(index);
    return true;
}

bool CCSOLScenarioOccupancy::Register(CCSOLScenarioMonster& monster, bool allowReturnedDeath)
{
    if (m_busy || !Ready() || !PoolReady()) return false;
    const bool returnedDeath = allowReturnedDeath && monster.pev->deadflag == DEAD_DEAD &&
        (monster.pev->effects & EF_NODRAW) != 0 && m_pool.Lease(monster) != 0;
    if ((!m_pool.Borrowed(monster) && !returnedDeath) || m_slots[monster.Slot()].type != monster.Type() ||
        m_slots[monster.Slot()].lease != m_pool.Lease(monster))
        return Fail("scenario occupancy registration precedes type attributes");
    OccupancyOperation operation(m_busy);
    return Move(monster.Slot(), monster.pev->origin);
}

bool CCSOLScenarioOccupancy::RefreshOccupancy()
{
    if (m_busy || !Ready() || !PoolReady()) return false;
    OccupancyOperation operation(m_busy);
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        auto* monster = m_pool.At(index);
        if (!monster) return Fail("scenario occupancy lost a pool entity");
        // Dead/non-solid entries keep their old links; do not rebuild the grid
        // or change the order of stationary entries on every refresh.
        if (monster->pev->solid != SOLID_NOT)
        {
            if (!m_slots[index].lease || m_slots[index].lease != m_pool.Lease(*monster) ||
                m_slots[index].type != monster->Type())
                return Fail("scenario occupancy refresh precedes type attributes");
            if (!Move(index, monster->pev->origin)) return false;
        }
    }
    return true;
}

bool CCSOLScenarioOccupancy::Neighbors(int x, int y, int radius,
    std::array<std::size_t, CCSOLMonsterPool::Capacity>& output, std::size_t& count)
{
    count = 0;
    std::array<bool, CCSOLMonsterPool::Capacity> visited{};
    for (int row = std::max(0, y - radius); row <= std::min(m_height - 1, y + radius); ++row)
    for (int column = std::max(0, x - radius); column <= std::min(m_width - 1, x + radius); ++column)
    {
        int previous = -1;
        for (int index = Head(column, row); index != -1; index = m_slots[index].next)
        {
            if (!Index(index) || visited[index] || count == output.size())
                return Fail("scenario occupancy contains an invalid or cyclic chain");
            const auto& slot = m_slots[index];
            if (slot.x != column || slot.y != row || slot.previous != previous)
                return Fail("scenario occupancy cell and slot links disagree");
            visited[index] = true;
            output[count++] = static_cast<std::size_t>(index);
            previous = index;
        }
    }
    return true;
}

bool CCSOLScenarioOccupancy::MovementNeighbors(const Vector& origin,
    std::array<Neighbor, CCSOLMonsterPool::Capacity>& output, std::size_t& count)
{
    count = 0;
    if (m_busy || !Ready() || !PoolReady()) return false;
    OccupancyOperation operation(m_busy);
    int x{}, y{};
    if (!Cell(origin, x, y)) return false;
    std::array<std::size_t, CCSOLMonsterPool::Capacity> slots{};
    std::size_t found{};
    if (!Neighbors(x, y, 1, slots, found)) return false;
    for (std::size_t i = 0; i < found; ++i)
        output[i] = {slots[i], m_slots[slots[i]].lease};
    count = found;
    return true;
}

CCSOLScenarioMonster* CCSOLScenarioOccupancy::ResolveNeighbor(const Neighbor& neighbor) const
{
    if (!Ready() || m_busy || neighbor.slot >= m_slots.size() || !neighbor.lease ||
        m_slots[neighbor.slot].lease != neighbor.lease) return nullptr;
    auto* monster = m_pool.At(neighbor.slot);
    return monster && m_pool.Lease(*monster) == neighbor.lease ? monster : nullptr;
}

bool CCSOLScenarioOccupancy::SpawnNeighbors(const Vector& origin,
    std::array<Neighbor, CCSOLMonsterPool::Capacity>& output, std::size_t& count)
{
    count = 0;
    if (m_busy || !Ready() || !PoolReady()) return false;
    OccupancyOperation operation(m_busy);
    int x{}, y{};
    if (!Cell(origin, x, y)) return false;
    std::array<std::size_t, CCSOLMonsterPool::Capacity> slots{};
    std::size_t found{};
    if (!Neighbors(x, y, 2, slots, found)) return false;
    for (std::size_t i = 0; i < found; ++i)
        output[i] = {slots[i], m_slots[slots[i]].lease};
    count = found;
    return true;
}

bool CCSOLScenarioOccupancy::MakeRoom(const Vector& origin)
{
    if (m_busy || !Ready() || !PoolReady()) return false;
    const auto access = m_pool.EntityAccess();
    OccupancyOperation operation(m_busy);
    int x{}, y{};
    if (!Cell(origin, x, y)) return false;
    std::array<std::size_t, CCSOLMonsterPool::Capacity> neighbors{};
    std::array<EntityHandle<CCSOLScenarioMonster>, CCSOLMonsterPool::Capacity> candidates{};
    std::array<std::size_t, CCSOLMonsterPool::Capacity> candidateSlots{};
    std::array<const csol::scenario::MonsterType*, CCSOLMonsterPool::Capacity> candidateTypes{};
    std::array<std::uint64_t, CCSOLMonsterPool::Capacity> candidateLeases{};
    std::size_t count{}, candidateCount{};
    if (!Neighbors(x, y, 2, neighbors, count)) return false;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto index = neighbors[i];
        auto* monster = m_pool.At(index);
        if (!monster) return Fail("scenario occupancy query lost a pool entity");
        if (monster->pev->solid == SOLID_NOT) continue;
        if (!Finite(monster->pev->origin) || !Finite(monster->pev->maxs))
            return Fail("scenario occupancy monster has invalid bounds");
        if (m_slots[index].type != monster->Type() || !m_slots[index].lease ||
            m_slots[index].lease != m_pool.Lease(*monster))
            return Fail("scenario occupancy query precedes type attributes");
        const Vector delta = monster->pev->origin - origin;
        // Mode15 has margin3; the original mode59-only margin is different.
        const float radiusX = (monster->pev->maxs.x + 16.0f) - 3.0f;
        const float radiusY = (monster->pev->maxs.y + 16.0f) - 3.0f;
        const float radiusZ = (monster->pev->maxs.z + 36.0f) - 3.0f;
        if (!(radiusX > std::fabs(delta.x) && radiusY > std::fabs(delta.y) &&
            radiusZ > std::fabs(delta.z))) continue;
        // Complete this pass before invoking any death callbacks: a later
        // protected slot must refuse the spawn without killing earlier slots.
        if (m_slots[index].priority != 0) return false;
        candidates[candidateCount] = monster;
        candidateTypes[candidateCount] = monster->Type();
        candidateLeases[candidateCount] = m_slots[index].lease;
        candidateSlots[candidateCount++] = index;
    }
    for (std::size_t i = 0; i < candidateCount; ++i)
    {
        if (!access->Allowed() || !PoolReady()) return false;
        auto* monster = candidates[i].Get<CCSOLScenarioMonster>();
        if (!monster || m_pool.At(candidateSlots[i]) != monster || !PoolReady() ||
            monster->Type() != candidateTypes[i] || m_pool.Lease(*monster) != candidateLeases[i])
            return Fail("scenario occupancy death callback changed the pool");
        const auto* type = monster->Type();
        if (type && (type->skill >= 1000 || type->eventClass > 149)) continue;
        const bool prepared = m_deaths.BeforeSpawnClear(*monster);
        if (!access->Allowed() || !PoolReady() || candidates[i].Get<CBaseEntity>() != monster ||
            m_pool.At(candidateSlots[i]) != monster ||
            monster->Type() != candidateTypes[i] || m_pool.Lease(*monster) != candidateLeases[i])
            return Fail("scenario pre-death callback changed a pool lease");
        if (!prepared) return Fail("scenario spawn-clear preparation failed");
        const bool requested = m_deaths.RequestSpawnClearDeath(*monster);
        // Golden-monster death may restore the original type in this same
        // lease. Only a new lease/entity invalidates the completed request.
        if (!access->Allowed() || !PoolReady() || candidates[i].Get<CBaseEntity>() != monster ||
            m_pool.At(candidateSlots[i]) != monster ||
            m_pool.Lease(*monster) != candidateLeases[i])
            return Fail("scenario death callback changed a pool lease");
        if (!requested) return Fail("scenario spawn-clear death request failed");
    }
    // A skipped, delayed or declined death does not change the original result.
    return true;
}
