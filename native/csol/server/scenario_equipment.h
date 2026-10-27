// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_player_upgrades.h"
#include "../scenario/equipment.hpp"
#include <functional>

class CCSOLScenarioEquipment final : public CCSOLScenarioUpgradeServices,
    public std::enable_shared_from_this<CCSOLScenarioEquipment>
{
public:
    // The scenario event owner must handle the original service event 0x26,
    // including its eligibility gates. An absent sink is a reported failure.
    using CapEvent = std::function<bool(const EHANDLE&)>;
    CCSOLScenarioEquipment(std::shared_ptr<const csol::scenario::EquipmentTables> tables, CapEvent capEvent);
    bool BindPlayer(CBasePlayer& player, int classId, csol::scenario::EquipmentLoadout loadout);
    void RemovePlayer(const EHANDLE& player);
    void Invalidate();
    bool Current() const override;
    bool Initialize(const EHANDLE& player, int classId,
        std::array<csol::scenario::UpgradeAttribute, 5>& attributes) override;
    bool ReadBonus(const EHANDLE& player, csol::scenario::UpgradeStat stat, int& value) override;
    bool ModifyExperience(const EHANDLE& player, int& amount) override;
    bool MoneyAddPercent(const EHANDLE& player, std::optional<float>& percent) override;
    bool ModifyJump(const EHANDLE& player, float& gravity) override;
    bool ReachedEffectiveCap(const EHANDLE& player) override;
    // Rules v1D8 applies the ordered named effects after modern ATK. The
    // caller supplies the original skill-based Boss predicate for this stage.
    bool ModifyCreatureDamage(const EHANDLE& player, bool skillBoss, float& damage);
private:
    struct Player
    {
        EHANDLE handle;
        std::size_t index{};
        int classId{};
        std::vector<csol::scenario::EquipmentLoadout::ClassCaps> caps;
        bool valid{true}, reachedCap{}, dispatching{}, eventFailed{};
    };
    bool Current(const Player& player) const;
    void Prune();
    std::shared_ptr<Player> Find(const EHANDLE& player);
    std::shared_ptr<const csol::scenario::EquipmentTables> m_tables;
    csol::scenario::EquipmentAbilities m_abilities;
    std::array<std::shared_ptr<Player>, 33> m_players;
    CapEvent m_capEvent;
    std::uint64_t m_generation{1};
    bool m_valid{true}, m_binding{};
};
