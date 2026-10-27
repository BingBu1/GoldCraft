// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_equipment.h"

using namespace csol::scenario;

CCSOLScenarioEquipment::CCSOLScenarioEquipment(std::shared_ptr<const EquipmentTables> tables, CapEvent capEvent)
    : m_tables(std::move(tables)), m_abilities(m_tables), m_capEvent(std::move(capEvent)) {}

bool CCSOLScenarioEquipment::Current() const { return m_valid && m_tables != nullptr; }

bool CCSOLScenarioEquipment::Current(const Player& player) const
{
    if (!Current() || !player.valid) return false;
    auto* entity = player.handle.Get<CBaseEntity>();
    return entity && !(entity->pev->flags & FL_KILLME) && dynamic_cast<CBasePlayer*>(entity);
}

void CCSOLScenarioEquipment::Prune()
{
    for (std::size_t index = 1; index < m_players.size(); ++index)
        if (m_players[index] && !Current(*m_players[index]))
        {
            m_players[index]->valid = false;
            m_players[index].reset();
            m_abilities.Remove(index);
        }
}

std::shared_ptr<CCSOLScenarioEquipment::Player> CCSOLScenarioEquipment::Find(const EHANDLE& handle)
{
    Prune();
    if (!Current() || !handle.Get()) return {};
    for (const auto& player : m_players)
        if (player && player->handle.Get() == handle.Get() &&
            player->handle.GetSerialNumber() == handle.GetSerialNumber()) return player;
    return {};
}

bool CCSOLScenarioEquipment::BindPlayer(CBasePlayer& player, int classId, EquipmentLoadout loadout)
{
    auto keepAlive = shared_from_this();
    if (!Current() || m_binding) return false;
    struct Binding { bool& flag; ~Binding() { flag = false; } } binding{m_binding};
    m_binding = true;
    const auto generation = m_generation;
    const EHANDLE handle(&player);
    const int index = player.entindex();
    // ENTINDEX is an engine callback; do not use the old C++ reference afterward.
    auto* entity = handle.Get<CBaseEntity>();
    if (!Current() || m_generation != generation || !entity || entity != &player || (entity->pev->flags & FL_KILLME) ||
        index < 1 || index >= static_cast<int>(m_players.size())) return false;
    Prune();
    if (m_players[index] && m_players[index]->dispatching) return false;
    auto candidate = std::make_shared<Player>();
    candidate->handle = handle;
    candidate->index = index;
    candidate->classId = classId;
    candidate->caps = std::move(loadout.classCaps);
    if (const auto previous = m_players[index])
    {
        // Changing equipment or character is not a new player-session reward.
        candidate->reachedCap = previous->reachedCap;
        candidate->eventFailed = previous->eventFailed;
    }
    if (!m_abilities.Bind(index, std::move(loadout))) return false;
    if (m_players[index]) m_players[index]->valid = false;
    m_players[index] = std::move(candidate);
    ++m_generation;
    return true;
}

void CCSOLScenarioEquipment::RemovePlayer(const EHANDLE& handle)
{
    // A removal also cancels a first binding suspended in an engine callback.
    ++m_generation;
    Prune();
    for (std::size_t index = 1; index < m_players.size(); ++index)
        if (m_players[index] && m_players[index]->handle.Get() == handle.Get() &&
            m_players[index]->handle.GetSerialNumber() == handle.GetSerialNumber())
        {
            m_players[index]->valid = false;
            m_players[index].reset();
            m_abilities.Remove(index);
        }
}

void CCSOLScenarioEquipment::Invalidate()
{
    ++m_generation;
    m_valid = false;
    for (auto& player : m_players)
    {
        if (player) player->valid = false;
        player.reset();
    }
    m_abilities.Clear();
}

bool CCSOLScenarioEquipment::Initialize(const EHANDLE& handle, int classId,
    std::array<UpgradeAttribute, 5>& attributes)
{
    const auto player = Find(handle);
    if (!player || player->classId != classId) return false;
    for (const auto& record : player->caps)
        if (record.classId == classId)
        {
            for (std::size_t stat = 0; stat < attributes.size(); ++stat) attributes[stat].cap = record.values[stat];
            break;
        }
    constexpr std::array<const char*, 5> initial{
        "INITIAL_HEALTH", "INITIAL_ATTACK_DAMAGE", "INITIAL_SPEED", "INITIAL_ARMOR", "INITIAL_AMMO"};
    constexpr std::array<const char*, 5> maximum{
        "MAX_HEALTH_LEVEL", "MAX_ATTACK_DAMAGE_LEVEL", "MAX_SPEED_LEVEL", "MAX_ARMOR_LEVEL", "MAX_AMMO_LEVEL"};
    for (std::size_t stat = 0; stat < attributes.size(); ++stat)
        if (!m_abilities.IntegerEffect(player->index, initial[stat], attributes[stat].level) ||
            !m_abilities.IntegerEffect(player->index, maximum[stat], attributes[stat].cap)) return false;
    return Current(*player);
}

bool CCSOLScenarioEquipment::ReadBonus(const EHANDLE& handle, UpgradeStat stat, int& value)
{
    const auto player = Find(handle);
    return player && m_abilities.Bonus(player->index, stat, value);
}

bool CCSOLScenarioEquipment::ModifyExperience(const EHANDLE& handle, int& amount)
{
    const auto player = Find(handle);
    return player && m_abilities.IntegerEffect(player->index, "EXP_RATIO", amount);
}

bool CCSOLScenarioEquipment::MoneyAddPercent(const EHANDLE& handle, std::optional<float>& percent)
{
    const auto player = Find(handle);
    return player && m_abilities.MoneyAddPercent(player->index, percent);
}

bool CCSOLScenarioEquipment::ModifyJump(const EHANDLE& handle, float& gravity)
{
    const auto player = Find(handle);
    return player && m_abilities.Effect(player->index, "JUMP", gravity);
}

bool CCSOLScenarioEquipment::ModifyCreatureDamage(const EHANDLE& handle, bool skillBoss, float& damage)
{
    const auto player = Find(handle);
    return player && m_abilities.Effect(player->index,
        skillBoss ? "BOSS_DAMAGE" : "ZOMBIE_DAMAGE", damage) && Current(*player);
}

bool CCSOLScenarioEquipment::ReachedEffectiveCap(const EHANDLE& handle)
{
    auto keepAlive = shared_from_this();
    const auto player = Find(handle);
    if (!player || player->dispatching || player->eventFailed) return false;
    if (player->reachedCap) return true;
    if (!m_capEvent) return false;
    player->reachedCap = true;
    player->dispatching = true;
    const bool sent = m_capEvent(handle);
    player->dispatching = false;
    if (!sent) player->eventFailed = true;
    return sent && Current(*player);
}
