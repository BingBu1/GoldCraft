// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "ehandle.h"
#include "../scenario/map_resources.hpp"
#include <array>
#include <map>
#include <optional>
#include <span>
#include <vector>

class CSOLMonsterDamageCall;
class CBasePlayerWeapon;
class CBasePlayer;

struct CSOLWeaponPartEffect
{
    std::uint32_t id{};
    int tier{}, category{}, kind{};
    bool passive{};
    std::vector<std::uint32_t> modes;
    std::array<double, 3> parameters{};
};

// Owned by one map session. New weapons have two zero slots, as in the CSO
// base constructor. State follows the entity across drops/owner changes;
// EHANDLE serials prevent a recycled edict from inheriting the old loadout.
class CCSOLScenarioWeaponState final
{
public:
    bool Load(csol::scenario::ResourceFileApi files, csol::scenario::ResourceError& error);
    void Invalidate();
    bool Current() const { return m_valid && m_loaded; }
    bool SetParts(CBasePlayerWeapon& weapon, std::array<std::uint32_t, 2> slots);
    std::array<std::uint32_t, 2> Parts(const CBasePlayerWeapon& weapon) const;
    // The original effect CSV's filename is not identified yet. Only accept
    // explicit server-owned records; never load the unrelated combine table.
    bool ReplacePartEffects(std::vector<CSOLWeaponPartEffect> effects);
    bool CollectParts(CBasePlayer& attacker, CBaseEntity* inflictor, int mode, int kind,
        bool blocked21F2, bool blocked34F0, std::vector<CSOLWeaponPartEffect>& output) const;

    // These are item-catalog keys, not inventory/HUD weapon IDs. The first
    // float remains raw until its separate consumer is identified.
    bool SetItemMultiplier(std::int32_t item, std::array<float, 2> values);
    void ClearItemMultipliers();
    std::span<const std::int32_t> MultiplierUpdates() const { return m_multiplierUpdates; }
    // A selected knife/grenade or linked skin can supply its verified item
    // identity here. Ordinary weapons resolve from the real item.csv index.
    bool SetItemIdentity(CBasePlayerWeapon& weapon, std::int32_t item);
    // Shared by damage and kill statistics: source IDs are mapped through
    // the actual catalog/owned weapon identity, never through HUD aliases.
    bool Item(CBasePlayer& attacker, int source, std::int32_t& item,
        csol::scenario::ConfigError& error) const;
    bool ApplyItemMultiplier(CSOLMonsterDamageCall& call, bool blocked21F2,
        csol::scenario::ConfigError& error) const;
    bool ApplyPartDamage(CSOLMonsterDamageCall& call, bool blocked21F2, bool blocked34F0,
        csol::scenario::ConfigError& error) const;
private:
    struct Weapon
    {
        EHANDLE handle;
        std::array<std::uint32_t, 2> parts{};
        std::optional<std::int32_t> item;
    };
    Weapon* Edit(CBasePlayerWeapon& weapon);
    const Weapon* Find(const CBasePlayerWeapon& weapon) const;
    std::map<std::int32_t, std::int32_t> m_items;
    std::vector<std::int32_t> m_itemIds;
    std::vector<Weapon> m_weapons;
    std::map<std::uint32_t, std::vector<CSOLWeaponPartEffect>> m_parts;
    std::map<std::int32_t, std::array<float, 2>> m_multipliers;
    std::vector<std::int32_t> m_multiplierUpdates;
    bool m_valid{true}, m_loaded{};
};
