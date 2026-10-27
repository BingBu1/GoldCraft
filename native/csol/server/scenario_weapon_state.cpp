// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_weapon_state.h"
#include "scenario_monster_damage.h"
#include "weapons.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>

#pragma clang fp contract(off)

namespace
{
bool Live(const CBaseEntity* entity)
{
    return entity && !(entity->pev->flags & FL_KILLME);
}
CBasePlayerWeapon* Active(CBasePlayer& player)
{
    return Live(player.m_pActiveItem) ? dynamic_cast<CBasePlayerWeapon*>(player.m_pActiveItem) : nullptr;
}
template<class Visitor> void Carried(CBasePlayer& player, Visitor&& visitor)
{
    // Original slots 1..3 contain single weapons; slot 4 is the grenade chain.
    for (int slot = PRIMARY_WEAPON_SLOT; slot <= KNIFE_SLOT; ++slot)
        if (auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player.m_rgpPlayerItems[slot]); Live(weapon))
            visitor(*weapon);
    int remaining = gpGlobals->maxEntities;
    for (auto* item = player.m_rgpPlayerItems[GRENADE_SLOT]; Live(item) && remaining-- > 0; item = item->m_pNext)
        if (auto* weapon = dynamic_cast<CBasePlayerWeapon*>(item)) visitor(*weapon);
}
CBasePlayerWeapon* CarriedSource(CBasePlayer& player, int source)
{
    CBasePlayerWeapon* result{};
    if (source) Carried(player, [&](CBasePlayerWeapon& weapon)
    {
        if (!result && weapon.m_iId == source) result = &weapon;
    });
    return result;
}
bool Integer(std::string_view text, std::int32_t& value)
{
    value = 0;
    const auto first = text.find_first_not_of(" \t\r\n\v\f");
    if (first == text.npos) return true;
    text.remove_prefix(first);
    if (text.front() == '+')
    {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9') return true;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} || parsed.ec == std::errc::invalid_argument;
}
int Alias(int source)
{
    switch (source)
    {
    case 48: return 469;
    case 49: return 471;
    case 50: return 468;
    case 72: return 464;
    case 123: return 465;
    case 171: return 470;
    case 291: return 466;
    case 292: return 467;
    default: return source;
    }
}
}

int CSOL_DamageWeaponId(CBaseEntity* inflictor, CBasePlayer* attacker)
{
    if (!Live(inflictor)) return 0;
    if (inflictor == attacker)
    {
        auto* weapon = Active(*attacker);
        return weapon ? weapon->m_iId : 0;
    }
    if (const auto* weapon = dynamic_cast<const CBasePlayerWeapon*>(inflictor)) return weapon->m_iId;
    const auto* projectile = dynamic_cast<const CSOLWeaponDamageSource*>(inflictor);
    return projectile ? projectile->DamageWeaponId() : 0;
}

bool CCSOLScenarioWeaponState::Load(csol::scenario::ResourceFileApi files,
    csol::scenario::ResourceError& error)
{
    constexpr auto path = "resource/item.csv";
    error = {};
    if (!m_valid || m_loaded || !files.load || !files.release)
    { error = {path, {0, "invalid weapon item catalog lifecycle or file API"}}; return false; }
    int length{};
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(path, &length), files.release);
    if (!m_valid) return false;
    if (!bytes || length <= 0 || length > 8 * 1024 * 1024)
    { error = {path, {0, "weapon item catalog is missing or too large"}}; return false; }
    csol::scenario::CsvDocument document;
    if (!csol::scenario::ReadCsv({reinterpret_cast<const char*>(bytes.get()), std::size_t(length)}, document, error.detail))
    { error.path = path; return false; }
    std::map<std::int32_t, std::int32_t> items;
    std::vector<std::int32_t> ids;
    // ItemTable's callback ignores both metadata rows (';' and ';ID').
    for (std::size_t row = 2; row < document.records.size(); ++row)
    {
        const auto& record = document.records[row];
        const auto& fields = record.fields;
        std::int32_t item{}, category{}, inGame{};
        if (fields.size() < 6 || !Integer(fields[0], item) || !Integer(fields[4], category) ||
            !Integer(fields[5], inGame))
        { error = {path, {record.byteOffset, "invalid weapon item catalog row"}}; return false; }
        ids.push_back(item);
        // Characters have a separate original index. Repeated weapon keys
        // retain the first row, including cannon144's later cosmetic entry.
        if (category != 7 && !fields[5].empty()) items.emplace(inGame, item);
    }
    bytes.reset();
    if (!m_valid) return false;
    m_items = std::move(items);
    m_itemIds = std::move(ids);
    m_loaded = true;
    return true;
}

void CCSOLScenarioWeaponState::Invalidate()
{
    m_valid = false;
    m_loaded = false;
    m_items.clear(); m_itemIds.clear(); m_weapons.clear(); m_parts.clear();
    ClearItemMultipliers();
}
const CCSOLScenarioWeaponState::Weapon* CCSOLScenarioWeaponState::Find(const CBasePlayerWeapon& weapon) const
{
    const auto found = std::find_if(m_weapons.begin(), m_weapons.end(), [&](const Weapon& state)
    { return state.handle.Get<CBaseEntity>() == &weapon; });
    return found == m_weapons.end() ? nullptr : &*found;
}
CCSOLScenarioWeaponState::Weapon* CCSOLScenarioWeaponState::Edit(CBasePlayerWeapon& weapon)
{
    if (!Current() || !Live(&weapon)) return nullptr;
    std::erase_if(m_weapons, [](const Weapon& state) { return !Live(state.handle.Get<CBaseEntity>()); });
    for (auto& state : m_weapons) if (state.handle.Get<CBaseEntity>() == &weapon) return &state;
    m_weapons.push_back({EHANDLE(&weapon), {}, {}});
    return &m_weapons.back();
}
bool CCSOLScenarioWeaponState::SetParts(CBasePlayerWeapon& weapon, std::array<std::uint32_t, 2> slots)
{
    if (auto* state = Edit(weapon)) { state->parts = slots; return true; }
    return false;
}
std::array<std::uint32_t, 2> CCSOLScenarioWeaponState::Parts(const CBasePlayerWeapon& weapon) const
{
    const auto* state = Current() ? Find(weapon) : nullptr;
    return state ? state->parts : std::array<std::uint32_t, 2>{};
}
bool CCSOLScenarioWeaponState::ReplacePartEffects(std::vector<CSOLWeaponPartEffect> effects)
{
    if (!Current()) return false;
    decltype(m_parts) candidate;
    for (auto& effect : effects)
    {
        if (effect.kind < 0 || effect.kind > 17 || effect.modes.size() > 2048 ||
            !std::all_of(effect.parameters.begin(), effect.parameters.end(), [](double value) { return std::isfinite(value); }))
            return false;
        candidate[effect.id].push_back(std::move(effect));
    }
    m_parts = std::move(candidate);
    return true;
}
bool CCSOLScenarioWeaponState::CollectParts(CBasePlayer& attacker, CBaseEntity* inflictor,
    int mode, int kind, bool blocked21F2, bool blocked34F0, std::vector<CSOLWeaponPartEffect>& output) const
{
    output.clear();
    if (!Current() || !Live(&attacker) || mode < 0) return false;
    if (blocked21F2 || blocked34F0) return true;
    const auto append = [&](const CBasePlayerWeapon& weapon, bool passive)
    {
        for (const auto id : Parts(weapon))
        {
            const auto found = m_parts.find(id);
            if (found == m_parts.end()) continue;
            for (const auto& effect : found->second)
                if (effect.passive == passive && effect.kind == kind &&
                    std::size_t(mode >> 5) < effect.modes.size() &&
                    (effect.modes[mode >> 5] & (std::uint32_t{1} << (mode & 31)))) output.push_back(effect);
        }
    };
    // Player inflictors use their own active item; projectiles find the
    // attacker's carried weapon using the saved launch ID, never current ID.
    CBasePlayerWeapon* direct{};
    if (auto* player = dynamic_cast<CBasePlayer*>(inflictor); Live(player)) direct = Active(*player);
    else if (Live(inflictor)) direct = CarriedSource(attacker, CSOL_DamageWeaponId(inflictor, &attacker));
    if (direct) append(*direct, false);
    if (kind != 4) Carried(attacker, [&](const CBasePlayerWeapon& weapon) { append(weapon, true); });
    return true;
}
bool CCSOLScenarioWeaponState::SetItemIdentity(CBasePlayerWeapon& weapon, std::int32_t item)
{
    if (std::find(m_itemIds.begin(), m_itemIds.end(), item) == m_itemIds.end()) return false;
    if (auto* state = Edit(weapon)) { state->item = item; return true; }
    return false;
}
bool CCSOLScenarioWeaponState::SetItemMultiplier(std::int32_t item, std::array<float, 2> values)
{
    if (!Current() || !std::isfinite(values[0]) || !std::isfinite(values[1])) return false;
    m_multipliers.insert_or_assign(item, values);
    m_multiplierUpdates.push_back(item);
    return true;
}
void CCSOLScenarioWeaponState::ClearItemMultipliers()
{
    m_multipliers.clear();
    m_multiplierUpdates.clear();
}
bool CCSOLScenarioWeaponState::Item(CBasePlayer& attacker, int source, std::int32_t& item,
    csol::scenario::ConfigError& error) const
{
    item = 0;
    if (!Current()) return false;
    if (!source) return true;
    const int alias = Alias(source);
    if (source != 4 && source != 29)
    {
        const auto found = m_items.find(alias);
        if (found != m_items.end()) { item = found->second; return true; }
    }
    auto* weapon = CarriedSource(attacker, source);
    if (weapon)
        if (const auto* state = Find(*weapon); state && state->item) { item = *state->item; return true; }
    // With no carried candidate there is no linked-skin fallback to inspect.
    // The selected knife/grenade paths still depend on separate player state.
    if (!weapon && source != 4 && source != 29) return true;
    error.message = "selected knife/grenade or linked weapon item identity is unresolved";
    return false;
}
bool CCSOLScenarioWeaponState::ApplyItemMultiplier(CSOLMonsterDamageCall& call, bool blocked21F2,
    csol::scenario::ConfigError& error) const
{
    if (!Current() || !call.Current()) return false;
    auto* player = call.Player();
    if (!player || blocked21F2 || !Active(*player) || (static_cast<std::uint32_t>(call.bits) & 0x38000000u)) return true;
    // Initial empty table is an actual owned runtime state. An installed zero
    // value is distinct: the lookup below must multiply by zero, not skip it.
    if (m_multipliers.empty()) return true;
    std::int32_t item{};
    if (!Item(*player, CSOL_DamageWeaponId(call.Inflictor(), player), item, error)) return false;
    const auto found = m_multipliers.find(item);
    if (found == m_multipliers.end()) return true;
    call.damage *= found->second[1];
    if (!std::isfinite(call.damage)) { error.message = "weapon item multiplier overflowed damage"; return false; }
    return call.Current() != nullptr;
}

bool CCSOLScenarioWeaponState::ApplyPartDamage(CSOLMonsterDamageCall& call,
    bool blocked21F2, bool blocked34F0, csol::scenario::ConfigError& error) const
{
    auto* monster = call.Current();
    if (!Current() || !monster) return false;
    auto* player = call.Player();
    if (!player) return true;
    monster->HostState().damageEffect = 0; // original target+90, before part queries
    const auto sum = [&](int kind, std::size_t member, double& value)
    {
        std::vector<CSOLWeaponPartEffect> records;
        if (!CollectParts(*player, call.Inflictor(), 15, kind, blocked21F2, blocked34F0, records)) return false;
        value = 0;
        for (const auto& record : records) value += record.parameters[member];
        return std::isfinite(value);
    };
    double damage{}, knockback{}, flinch{}, boss{};
    if (!sum(1, 0, damage) || !sum(6, 1, knockback) || !sum(7, 1, flinch))
    { error.message = "invalid weapon part damage sum"; return false; }
    double total = knockback + damage;
    total = flinch + total;
    if (static_cast<std::int16_t>(monster->Type()->skill) >= 1000)
    {
        if (!sum(14, 0, boss)) { error.message = "invalid Boss part damage sum"; return false; }
        total = boss + total;
    }
    if (!std::isfinite(total)) { error.message = "weapon part damage sum overflowed"; return false; }
    if (std::abs(total) > 0x1p-52)
        call.damage = static_cast<float>(static_cast<double>(call.damage) * (1.0 + total));
    if (!std::isfinite(call.damage)) { error.message = "weapon part multiplier overflowed damage"; return false; }
    return call.Current() != nullptr;
}
