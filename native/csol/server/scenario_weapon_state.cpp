// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_weapon_state.h"
#include "scenario_monster_damage.h"
#include "weapons.h"
#include "game_rules_gate.h"
#include "scenario_runtime.h"
#include "scenario_player_runtime.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <memory>
#include <random>

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

CCSOLScenarioWeaponState::AttributeRecord::AttributeRecord(AttributeCounters& owner)
    : counts(owner)
{
    ++counts.sequence;
    ++counts.live;
}

CCSOLScenarioWeaponState::AttributeRecord::AttributeRecord(const AttributeRecord& source)
    : counts(source.counts), values(source.values)
{
    // Copy the kind tree/shared values, but initialize a fresh false cache.
    if (std::bit_cast<std::int32_t>(counts.live) > 0) ++counts.sequence;
    ++counts.live;
}

CCSOLScenarioWeaponState::AttributeRecord::~AttributeRecord()
{
    if (--counts.live == 0) counts.sequence = 0;
}

bool CCSOLScenarioWeaponState::AttributeRecord::Set(int kind,
    std::optional<CSOLWeaponAttributeValue> value, std::uint8_t invalidateCache)
{
    if (kind < 1 || kind > 4006) return false;
    if (value) values.insert_or_assign(kind, std::make_shared<const CSOLWeaponAttributeValue>(*value));
    else values.erase(kind);
    if (invalidateCache && (kind < 4000 || kind > 4005))
        std::fill(cache.begin(), cache.end(), false);
    cacheState = 0;
    return true;
}

CCSOLScenarioWeaponState::CCSOLScenarioWeaponState(std::shared_ptr<const CSOLScenarioEntityAccess> access)
    : m_access(std::move(access))
{
    // Original default initialization constructs a temporary and copies it
    // into key0: sequence=2/live=1 on return, not an absent directory.
    AttributeRecord empty(m_attributeCounters);
    m_attributes.try_emplace(0, empty);
}

const CCSOLScenarioWeaponState::AttributeRecord*
CCSOLScenarioWeaponState::ResolveAttributes(std::uint32_t key) const
{
    if (!Current()) return nullptr;
    auto found = m_attributes.find(key);
    if (found == m_attributes.end()) found = m_attributes.find(0);
    return found == m_attributes.end() ? nullptr : &found->second;
}

std::optional<std::uint32_t> CCSOLScenarioWeaponState::CreateAttributes(
    std::int32_t weaponId, std::uint64_t raw2000)
{
    if (!Current()) return std::nullopt;
    const auto key = m_attributeCounters.sequence + std::uint32_t{1};
    AttributeRecord temporary(m_attributeCounters);
    temporary.Set(1000, CSOLWeaponAttributeValue{weaponId}, 1);
    temporary.Set(2000, CSOLWeaponAttributeValue{raw2000}, 1);
    // Original callers return the preselected key even on a collision.
    // try_emplace must not copy/replace a record if that key already exists.
    m_attributes.try_emplace(key, temporary);
    return key;
}

std::optional<std::uint32_t> CCSOLScenarioWeaponState::CloneAttributes(
    std::uint32_t sourceKey, std::uint64_t raw2000)
{
    const auto* source = ResolveAttributes(sourceKey);
    if (!source) return std::nullopt;
    const auto key = m_attributeCounters.sequence + std::uint32_t{1};
    AttributeRecord temporary(*source);
    temporary.Set(2000, CSOLWeaponAttributeValue{raw2000}, 1);
    m_attributes.try_emplace(key, temporary);
    return key;
}

bool CCSOLScenarioWeaponState::SetAttribute(std::uint32_t key, int kind,
    std::optional<CSOLWeaponAttributeValue> value, std::uint8_t invalidateCache)
{
    if (!Current()) return false;
    auto found = m_attributes.find(key);
    if (found == m_attributes.end()) found = m_attributes.find(0);
    return found != m_attributes.end() && found->second.Set(kind, std::move(value), invalidateCache);
}

std::optional<CSOLWeaponAttributeValue> CCSOLScenarioWeaponState::Attribute(std::uint32_t key, int kind) const
{
    const auto* record = ResolveAttributes(key);
    if (!record) return std::nullopt;
    const auto found = record->values.find(kind);
    return found == record->values.end() ? std::nullopt : std::optional{*found->second};
}

double CCSOLScenarioWeaponState::AttributeNumber(std::uint32_t key, int kind) const
{
    const auto value = Attribute(key, kind);
    if (value)
        if (const auto* number = std::get_if<double>(&*value)) return *number;
    return 0.0;
}

bool CCSOLScenarioWeaponState::HasAdditionalAttributeObjects() const
{
    return Current() && std::bit_cast<std::int32_t>(m_attributeCounters.live) >= 2;
}

bool CCSOLScenarioWeaponState::SetAttributeKey(CBasePlayerWeapon& weapon, std::uint32_t key)
{
    // This explicit host binding requires a real record. Directory reads of
    // missing keys still fall back to key0, as the original v84 does.
    if (!Current() || !m_attributes.contains(key)) return false;
    if (auto* state = Edit(weapon)) { state->attributeKey = key; return true; }
    return false;
}

std::uint32_t CCSOLScenarioWeaponState::AttributeKey(const CBasePlayerWeapon& weapon) const
{
    const auto* state = Current() && Live(&weapon) ? Find(weapon) : nullptr;
    return state ? state->attributeKey : 0;
}

bool CCSOLScenarioWeaponState::AttributeWeapon(CBasePlayer& attacker,
    CBaseEntity* inflictor, EHANDLE& output,
    bool (*ownerValid)(const void*), const void* ownerContext) const
{
    output = nullptr;
    if (ownerValid && !ownerValid(ownerContext)) return false;
    if (!Current() || !Live(&attacker) || (inflictor && !Live(inflictor))) return false;
    const EHANDLE actor(&attacker), source(inflictor);
    const auto current = [&]
    {
        return (!ownerValid || ownerValid(ownerContext)) &&
            Current() && actor.Get<CBaseEntity>() == &attacker && Live(&attacker) &&
            (!inflictor || (source.Get<CBaseEntity>() == inflictor && Live(inflictor)));
    };
    // CSO materializes a world wrapper here. Its concrete source-ID behavior
    // is unresolved; do not silently turn that branch into no attributes.
    if (!inflictor) return false;
    const bool playerSource = inflictor->IsPlayer() != FALSE;
    if (!current()) return false;
    if (playerSource)
    {
        auto* player = dynamic_cast<CBasePlayer*>(source.Get<CBaseEntity>());
        if (!player) return false;
        output = Active(*player);
        return true;
    }
    const auto sourceIdOf = [&]()
    {
        auto* entity = source.Get<CBaseEntity>();
        if (const auto* weapon = dynamic_cast<const CBasePlayerWeapon*>(entity)) return weapon->m_iId;
        if (const auto* projectile = dynamic_cast<const CSOLWeaponDamageSource*>(entity))
            return projectile->DamageWeaponId();
        return entity ? ProjectileSource(*entity) : 0;
    };
    const int sourceId = sourceIdOf();
    if (!current()) return false;
    if (!sourceId) return true;
    for (int slot = PRIMARY_WEAPON_SLOT; slot <= KNIFE_SLOT; ++slot)
    {
        auto* item = attacker.m_rgpPlayerItems[slot];
        if (!Live(item)) continue;
        const bool weapon = item->IsWeapon() != FALSE;
        if (!current()) return false;
        if (!weapon) continue;
        // The original re-reads this slot after IsWeapon and calls the
        // source's v138 again for every candidate. Either callback may
        // replace an inventory item; do not keep the earlier raw pointer.
        const EHANDLE candidate(attacker.m_rgpPlayerItems[slot]);
        const int id = sourceIdOf();
        if (!current()) return false;
        auto* selected = dynamic_cast<CBasePlayerWeapon*>(candidate.Get<CBaseEntity>());
        if (!Live(selected)) return false;
        if (selected->m_iId != id) continue;
        auto* result = dynamic_cast<CBasePlayerWeapon*>(attacker.m_rgpPlayerItems[slot]);
        if (!Live(result)) return false;
        output = result;
        return true;
    }
    return true;
}

bool CCSOLScenarioWeaponState::PrepareAttributeDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, std::optional<float>& recovery,
    csol::scenario::ConfigError& error, std::uint32_t (*seed)()) const
{
    recovery.reset();
    const auto session = runtime ? runtime->session : nullptr;
    const auto owned = [&]
    {
        return runtime && !runtime->Closing() && session && runtime->session == session &&
            session->Weapons().get() == this && Current() &&
            call.Spawn().BelongsTo(session->Map()) && call.Current();
    };
    if (!owned()) return false;
    // Retain the actual directory and player registry through engine/entropy
    // callbacks; replacing either cancels the old operation.
    const auto keepAlive = session->Weapons();
    const auto players = runtime->players;
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE actor(player);
    const auto state = players ? players->Find(actor) : nullptr;
    if (!state) { error.message = "weapon attribute attacker state is unavailable"; return false; }
    const auto current = [&]
    {
        return owned() && runtime->players == players && players->Active() &&
            players->Find(actor) == state && call.Player() == player;
    };
    if (!current()) return call.Cancel();
    bool isPlayer{};
    try { isPlayer = player->IsPlayer() != FALSE; }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
    if (!current()) return call.Cancel();
    if (!isPlayer || !HasAdditionalAttributeObjects()) return true;
    if (!call.Inflictor())
    { error.message = "weapon attribute world-wrapper source is unavailable"; return false; }
    EHANDLE handle;
    bool selected{};
    try
    {
        selected = AttributeWeapon(*player, call.Inflictor(), handle, [](const void* context)
            { return (*static_cast<const decltype(current)*>(context))(); }, &current);
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
    if (!current()) return call.Cancel();
    if (!selected)
    { error.message = "weapon attribute source selection failed"; return false; }
    auto* weapon = handle.Get<CBasePlayerWeapon>();
    if (!weapon) return true;
    const auto valid = [&] { return current() && handle.Get<CBasePlayerWeapon>() == weapon && Live(weapon); };
    const auto number = [&](int kind) { return AttributeNumber(AttributeKey(*weapon), kind); };
    const auto multiply = [&](double bonus)
    {
        const float damage = static_cast<float>(static_cast<double>(call.damage) * (1.0 + bonus));
        if (!std::isfinite(bonus) || !std::isfinite(damage))
        { error.message = "weapon attribute damage is not finite"; return false; }
        call.damage = damage;
        return true;
    };
    if (!multiply(number(1))) return false;
    const double chance = number(5);
    // COMISD/JBE skips NaN as well as <=epsilon. Do not clamp chance to1.
    if (chance > 0x1p-23)
    {
        std::uint32_t value{};
        try { value = seed ? seed() : static_cast<std::uint32_t>(std::random_device{}()); }
        catch (...)
        {
            if (!valid()) return call.Cancel();
            error.message = "weapon attribute entropy source failed";
            return false;
        }
        if (!valid()) return call.Cancel();
        const double roll = csol::scenario::WeaponAttributeRandom(value);
        // The original samples kind6 and D after entropy/MT. Earlier kind5
        // remains fixed even if a callback changed the directory or damage.
        if (chance >= roll)
        {
            if (!multiply(number(6))) return false;
            call.Current()->HostState().damageEffect = 8;
        }
    }
    if (!valid()) return call.Cancel();
    const double ratio = number(7);
    if (ratio > 0x1p-23)
    {
        const float amount = static_cast<float>(static_cast<double>(call.damage) * ratio);
        if (!std::isfinite(amount))
        { error.message = "weapon attribute recovery amount is not finite"; return false; }
        recovery = amount;
    }
    return true;
}

bool CCSOLScenarioWeaponState::ApplyAttributeDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error,
    std::uint32_t (*seed)()) const
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto directory = session ? session->Weapons() : nullptr;
    const auto players = runtime ? runtime->players : nullptr;
    const auto current = [&]
    {
        return runtime && !runtime->Closing() && session && runtime->session == session &&
            directory.get() == this && session->Weapons() == directory && Current() &&
            runtime->players == players && call.Spawn().BelongsTo(session->Map()) && call.Current();
    };
    if (!current()) return false;
    std::optional<float> recovery;
    const bool prepared = PrepareAttributeDamage(call, runtime, recovery, error, seed);
    if (!current()) return call.Cancel();
    if (!prepared) return false;
    if (!recovery) return true;
    if (!players)
    { error.message = "attribute recovery player owner is unavailable"; return false; }
    bool applied{};
    const bool recovered = players->ApplyAttributeRecovery(call, runtime, *recovery, applied, error,
        [](const void* context) { return (*static_cast<const decltype(current)*>(context))(); }, &current);
    // Original v4C result is ignored: a normal health gate rejection does not
    // reject the hit. The host success flag still propagates failure/cancellation.
    if (!current()) return call.Cancel();
    return recovered;
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
    return projectile ? projectile->DamageWeaponId() : CSOL_ScenarioProjectileSource(*inflictor);
}

bool CCSOLScenarioWeaponState::Load(csol::scenario::ResourceFileApi files,
    csol::scenario::ResourceError& error)
{
    constexpr auto path = "resource/item.csv";
    error = {};
    if (!Accessible() || m_loaded || !files.load || !files.release)
    { error = {path, {0, "invalid weapon item catalog lifecycle or file API"}}; return false; }
    int length{};
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(path, &length), files.release);
    if (!Accessible()) return false;
    if (!bytes || length <= 0 || length > 8 * 1024 * 1024)
    { error = {path, {0, "weapon item catalog is missing or too large"}}; return false; }
    csol::scenario::CsvDocument document;
    if (!csol::scenario::ReadCsv({reinterpret_cast<const char*>(bytes.get()), std::size_t(length)}, document, error.detail))
    { error.path = path; return false; }
    std::map<std::int32_t, std::int32_t> items;
    std::map<std::int32_t, CatalogItem> catalog;
    // ItemTable's callback ignores both metadata rows (';' and ';ID').
    for (std::size_t row = 2; row < document.records.size(); ++row)
    {
        const auto& record = document.records[row];
        const auto& fields = record.fields;
        std::int32_t item{}, category{}, inGame{};
        if (fields.size() < 6 || !Integer(fields[0], item) || !Integer(fields[4], category) ||
            !Integer(fields[5], inGame))
        { error = {path, {record.byteOffset, "invalid weapon item catalog row"}}; return false; }
        catalog.emplace(item, CatalogItem{category, inGame, fields[3] == "Equipment"});
        // Characters have a separate original index. Repeated weapon keys
        // retain the first row, including cannon144's later cosmetic entry.
        if (category != 7 && !fields[5].empty()) items.emplace(inGame, item);
    }
    bytes.reset();
    if (!Accessible()) return false;
    constexpr auto defaultPath = "resource/defaultitems.csv";
    length = 0;
    bytes.reset(files.load(defaultPath, &length));
    if (!Accessible()) return false;
    if (!bytes || length <= 0 || length > 8 * 1024 * 1024)
    { error = {defaultPath, {0, "default weapon selection is missing or too large"}}; return false; }
    csol::scenario::CsvDocument defaults;
    if (!csol::scenario::ReadCsv({reinterpret_cast<const char*>(bytes.get()), std::size_t(length)}, defaults, error.detail))
    { error.path = defaultPath; return false; }
    std::array<std::int32_t, 2> selection{};
    constexpr std::array<std::string_view, 2> names{"Knife", "ThrowWeapon"};
    if (defaults.records.size() != 2)
    { error = {defaultPath, {0, "expected one default weapon selection row"}}; return false; }
    const auto& header = defaults.records[0].fields;
    const auto& values = defaults.records[1].fields;
    for (std::size_t slot = 0; slot < names.size(); ++slot)
    {
        const auto column = std::find(header.begin(), header.end(), names[slot]);
        if (column == header.end() || std::count(header.begin(), header.end(), names[slot]) != 1 ||
            std::size_t(column - header.begin()) >= values.size() ||
            !Integer(values[column - header.begin()], selection[slot]))
        { error = {defaultPath, {0, "invalid default knife or HE selection column"}}; return false; }
        const auto found = catalog.find(selection[slot]);
        // These are verified native implementation IDs in item.csv, not the
        // weapon/HUD IDs 29 and 4. Category6 alone also includes cannon/beamgun.
        if (found == catalog.end() || !found->second.equipment ||
            found->second.category != (slot == 0 ? 11 : 6) ||
            found->second.inGame != (slot == 0 ? 4000 : 2003))
        { error = {defaultPath, {0, "default selection has no implemented native knife or HE"}}; return false; }
    }
    bytes.reset();
    if (!Accessible()) return false;
    auto itemCatalog = std::make_shared<const csol::scenario::CsvDocument>(std::move(document));
    m_items = std::move(items);
    m_catalog = std::move(catalog);
    m_itemCatalog = std::move(itemCatalog);
    m_defaultSelection = selection;
    m_loaded = true;
    return true;
}

void CCSOLScenarioWeaponState::Invalidate()
{
    m_valid = false;
    m_loaded = false;
    m_items.clear(); m_catalog.clear(); m_weapons.clear(); m_parts.clear();
    m_itemCatalog.reset();
    m_defaultSelection = {}; m_selections.clear(); m_heGrenades.clear();
    m_attributes.clear();
    m_enhancementDamage.reset();
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
bool CCSOLScenarioWeaponState::SetEnhancementLevel(CBasePlayerWeapon& weapon, std::uint16_t level)
{
    if (auto* state = Edit(weapon)) { state->enhancementLevel = level; return true; }
    return false;
}

std::uint16_t CCSOLScenarioWeaponState::EnhancementLevel(const CBasePlayerWeapon& weapon) const
{
    const auto* state = Current() && Live(&weapon) ? Find(weapon) : nullptr;
    return state ? state->enhancementLevel : 0;
}

bool CCSOLScenarioWeaponState::ReplaceEnhancementDamage(
    std::map<std::int32_t, std::vector<std::uint32_t>> percentages)
{
    if (!Current()) return false;
    m_enhancementDamage = std::move(percentages);
    return true;
}

bool CCSOLScenarioWeaponState::SetAddonWeaponInput(CBasePlayerWeapon& weapon,
    std::optional<std::int32_t> value)
{
    if (auto* state = Edit(weapon)) { state->addonInput = value; return true; }
    return false;
}

std::optional<std::int32_t> CCSOLScenarioWeaponState::AddonWeaponInput(const CBasePlayerWeapon& weapon) const
{
    const auto* state = Current() && Live(&weapon) ? Find(weapon) : nullptr;
    return state ? state->addonInput : std::nullopt;
}

bool CCSOLScenarioWeaponState::NotifyDamageBuff(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error) const
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto owned = [&]
    { return runtime && !runtime->Closing() && session && runtime->session == session &&
        session->Weapons().get() == this && Current() &&
        call.Spawn().BelongsTo(session->Map()) && call.Current(); };
    if (!owned()) return call.Cancel();
    const auto keepAlive = session->Weapons();
    if ((static_cast<std::uint32_t>(call.bits) & 0x08000000u) || !(call.damage > 0)) return true;
    auto* attacker = call.Player();
    if (!attacker) return true;
    const EHANDLE actor(attacker);
    const auto players = runtime->players;
    const auto state = players ? players->Find(actor) : nullptr;
    if (!state) { error.message = "scenario weapon hint player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && runtime->players == players && players->Find(actor) == state && call.Player() == attacker; };
    try
    {
        auto* inflictor = call.Inflictor();
        const auto* active = inflictor == attacker ? Active(*attacker) : nullptr;
        // With no valid active weapon, the original still queries a nonnull
        // source's raw v138, including a player source. The shared helper's
        // attacker shortcut would return0 before that callback.
        const int source = active ? active->m_iId : CSOL_DamageWeaponId(inflictor, nullptr);
        if (!current()) return call.Cancel();
        // Direct ItemTable.v10 lookup, including key0. This is not Item()'s
        // knife/HE selection or linked-skin fallback used by statistics.
        const auto item = m_items.find(source);
        if (item == m_items.end()) return true;
        const auto tables = session->Equipment();
        if (!tables) { error.message = "scenario weapon hint buff definitions are unavailable"; return false; }
        const auto bucket = tables->buffs.find(item->second);
        if (bucket == tables->buffs.end()) return true;
        // SharedData.v3DC -> CBuffManager.v0C reads the static +29C tree.
        // v1C returns the definition's item ID without side effects; mode15
        // accepts type0. Winner priority cannot change this existence result.
        const bool present = std::any_of(bucket->second.begin(), bucket->second.end(), [&](const auto& buff)
        { return buff.type == 0 && (!item->second || !buff.wornOnly || buff.item == item->second); });
        if (!present) return true;
        const auto messages = session->Messages();
        const bool sent = messages.SendWeaponDamageBuffHint(actor.Get());
        if (!current()) return call.Cancel();
        if (!sent) { error.message = "scenario weapon damage buff hint failed"; return false; }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioWeaponState::ApplyEnhancementDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error) const
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto owned = [&]
    { return runtime && !runtime->Closing() && session && runtime->session == session &&
        session->Weapons().get() == this && Current() &&
        call.Spawn().BelongsTo(session->Map()) && call.Current(); };
    if (!owned()) return call.Cancel();
    const auto keepAlive = session->Weapons();
    auto* attacker = call.Player();
    auto* inflictor = call.Inflictor();
    if (!attacker || !inflictor) return true;
    const auto players = runtime->players;
    const EHANDLE actor(attacker);
    const auto state = players ? players->Find(actor) : nullptr;
    if (!state)
    { error.message = "scenario enhancement player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && runtime->players == players && players->Find(actor) == state && call.Player() == attacker; };
    if (!g_engfuncs.pfnEntOffsetOfPEntity)
    { error.message = "scenario enhancement source validation is unavailable"; return false; }
    try
    {
        const int offset = OFFSET(inflictor->edict());
        if (!current()) return call.Cancel();
        if (!offset) return true;
        const bool playerSource = inflictor->IsPlayer() != FALSE;
        if (!current()) return call.Cancel();
        EHANDLE selected;
        if (playerSource && inflictor == attacker)
        {
            auto* item = attacker->m_pActiveItem;
            if (!Live(item)) return true;
            const EHANDLE before(item);
            const bool isWeapon = item->IsWeapon() != FALSE;
            if (!current()) return call.Cancel();
            if (!isWeapon) return true;
            // The original handle conversion occurs after the virtual call.
            if (attacker->m_pActiveItem == item && before.Get<CBaseEntity>() != item)
                return call.Cancel();
            selected = Active(*attacker);
        }
        else
        {
            const auto sourceId = [&] { return CSOL_DamageWeaponId(call.Inflictor(), nullptr); };
            const int initialSource = sourceId();
            if (!current()) return call.Cancel();
            if (!initialSource) return true;
            // This consumer scans four single slot heads, including grenade
            // slot4. AttributeWeapon's 1..3 scan is a different original path.
            for (int slot = PRIMARY_WEAPON_SLOT; slot <= GRENADE_SLOT; ++slot)
            {
                auto* item = attacker->m_rgpPlayerItems[slot];
                if (!Live(item)) continue;
                const EHANDLE before(item);
                const bool isWeapon = item->IsWeapon() != FALSE;
                if (!current()) return call.Cancel();
                if (!isWeapon) continue;
                if (attacker->m_rgpPlayerItems[slot] == item && before.Get<CBaseEntity>() != item)
                    return call.Cancel();
                auto* weapon = dynamic_cast<CBasePlayerWeapon*>(attacker->m_rgpPlayerItems[slot]);
                if (!Live(weapon)) return call.Cancel();
                const EHANDLE candidate(weapon);
                const int candidateId = weapon->m_iId;
                const int id = sourceId();
                if (!current()) return call.Cancel();
                if (candidateId != id) continue;
                if (attacker->m_rgpPlayerItems[slot] == weapon && candidate.Get<CBaseEntity>() != weapon)
                    return call.Cancel();
                selected = attacker->m_rgpPlayerItems[slot];
                break;
            }
        }
        auto* weapon = dynamic_cast<CBasePlayerWeapon*>(selected.Get<CBaseEntity>());
        if (!Live(weapon)) return true;
        const auto level = EnhancementLevel(*weapon);
        if (level <= 8) return true;
        // The original directly queries ItemTable.v10(weapon+E4). Do not
        // substitute knife/HE selections, source aliases or linked skins.
        const auto item = m_items.find(weapon->m_iId);
        if (item == m_items.end()) return true;
        if (!m_enhancementDamage)
        { error.message = "scenario enhancement damage directory has not been imported"; return false; }
        const auto record = m_enhancementDamage->find(item->second);
        if (record == m_enhancementDamage->end()) return true;
        const std::size_t index = level - 9;
        if (index >= record->second.size()) return true;
        // Unsigned DWORD -> double *0.01 -> float, then separate MULSS/ADDSS.
        const float ratio = static_cast<float>(static_cast<double>(record->second[index]) * 0.01);
        const float bonus = call.damage * ratio;
        const float damage = call.damage + bonus;
        if (!std::isfinite(damage))
        { error.message = "scenario enhancement damage is not finite"; return false; }
        call.damage = damage;
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
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
void CCSOLScenarioWeaponState::AppendParts(const CBasePlayerWeapon& weapon, bool passive,
    int mode, int kind, std::vector<CSOLWeaponPartEffect>& output) const
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
}

bool CCSOLScenarioWeaponState::CollectParts(CBasePlayer& attacker, CBaseEntity* inflictor,
    int mode, int kind, bool blocked21F2, bool blocked34F0, std::vector<CSOLWeaponPartEffect>& output,
    bool (*ownerValid)(const void*), const void* ownerContext) const
{
    output.clear();
    if (!Current() || (ownerValid && !ownerValid(ownerContext)) || !Live(&attacker) || mode < 0) return false;
    if (blocked21F2 || blocked34F0) return true;
    // Player inflictors use their own active item; projectiles find the
    // attacker's carried weapon using the saved launch ID, never current ID.
    CBasePlayerWeapon* direct{};
    if (auto* player = dynamic_cast<CBasePlayer*>(inflictor); Live(player)) direct = Active(*player);
    else if (Live(inflictor))
    {
        const EHANDLE actor(&attacker), source(inflictor);
        const int id = CSOL_DamageWeaponId(inflictor, &attacker);
        // Source queries can retire the map or replace an edict. Check the
        // owner before resolving either handle or revisiting the inventory.
        if (!Current() || (ownerValid && !ownerValid(ownerContext)) ||
            actor.Get<CBaseEntity>() != &attacker || source.Get<CBaseEntity>() != inflictor ||
            !Live(&attacker) || !Live(inflictor)) return false;
        direct = CarriedSource(attacker, id);
    }
    if (direct) AppendParts(*direct, false, mode, kind, output);
    if (kind != 4) Carried(attacker, [&](const CBasePlayerWeapon& weapon) { AppendParts(weapon, true, mode, kind, output); });
    return true;
}

bool CCSOLScenarioWeaponState::RecordPartBloodSucking(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error) const
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto owned = [&]
    { return runtime && !runtime->Closing() && session && runtime->session == session &&
        session->Weapons().get() == this && Current() &&
        call.Spawn().BelongsTo(session->Map()) && call.Current(); };
    if (!owned()) return call.Cancel();
    const auto keepAlive = session->Weapons();
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE actor(player);
    const auto players = runtime->players;
    const auto state = players ? players->Find(actor) : nullptr;
    if (!state) { error.message = "scenario blood-sucking player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && runtime->players == players && players->Find(actor) == state && call.Player() == player; };
    try
    {
        std::vector<CSOLWeaponPartEffect> records;
        const bool collected = CollectParts(*player, call.Inflictor(), 15, 9,
            state->profile.weaponEffectsBlocked21F2 != 0, state->profile.weaponEffectsBlocked34F0, records,
            [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current);
        if (!current()) return call.Cancel();
        if (!collected) { error.message = "scenario blood-sucking part source failed"; return false; }
        double ratio{};
        for (const auto& record : records) ratio += record.parameters[0];
        // The original sums in double, then uses one x87 product/store with
        // the current float damage. Keep the thread's actual control word.
        const float damage = call.damage;
        double amount;
#if defined(_MSC_VER) && defined(_M_IX86)
        __asm
        {
            fld ratio
            fmul damage
            fstp amount
        }
#elif defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("fldl %1; fmuls %2; fstpl %0"
            : "=m"(amount) : "m"(ratio), "m"(damage) : "st");
#else
#error Scenario part recovery requires the supported x86 x87 target
#endif
        if (!std::isfinite(amount))
        { error.message = "scenario blood-sucking part amount is not finite"; return false; }
        if (!(std::abs(amount) > 0x1p-52)) return true;
        if (!players->AddBloodSucking(actor, static_cast<float>(amount)))
        { error.message = "scenario blood-sucking accumulator overflowed"; return false; }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioWeaponState::BloodSuckingLimit(CSOLPlayerRecoveryCall& call,
    double& percentage, csol::scenario::ConfigError& error) const
{
    percentage = 0;
    const auto runtime = call.Runtime();
    const auto session = runtime ? runtime->session : nullptr;
    const auto current = [&]
    { return call.Current() && session && session->Weapons().get() == this && Current(); };
    if (!current()) return call.Cancel();
    const auto keepAlive = session->Weapons();
    auto* player = call.Player();
    const auto state = runtime->players->Find(call.PlayerHandle());
    std::vector<CSOLWeaponPartEffect> records;
    const auto blocked = [&]
    { return state->profile.weaponEffectsBlocked21F2 != 0 || state->profile.weaponEffectsBlocked34F0; };
    try
    {
        // source0: IsWeapon precedes a SECOND read of the current active
        // handle. A callback may switch guns; the new gun supplies parts.
        if (!blocked())
        {
            auto* item = player->m_pActiveItem;
            if (Live(item))
            {
                const EHANDLE previous(item);
                const bool weapon = item->IsWeapon() != FALSE;
                if (!current()) return call.Cancel();
                if (player->m_pActiveItem == item &&
                    (previous.Get<CBaseEntity>() != item || !Live(item))) return call.Cancel();
                if (weapon)
                    if (auto* active = Active(*player)) AppendParts(*active, false, 15, 9, records);
            }
        }
        // The passive collector checks flags again, then checks IsWeapon on
        // each inventory node. It does not select only the damage-source gun.
        if (!blocked())
        {
            const auto append = [&](CBasePlayerItem* item)
            {
                if (!Live(item)) return true;
                const EHANDLE handle(item);
                const bool weapon = item->IsWeapon() != FALSE;
                if (!current() || handle.Get<CBaseEntity>() != item || !Live(item)) return call.Cancel();
                if (weapon)
                    if (auto* active = dynamic_cast<CBasePlayerWeapon*>(item)) AppendParts(*active, true, 15, 9, records);
                return true;
            };
            for (int slot = PRIMARY_WEAPON_SLOT; slot <= KNIFE_SLOT; ++slot)
                if (!append(player->m_rgpPlayerItems[slot])) return false;
            if (!gpGlobals || gpGlobals->maxEntities <= 0)
            { error.message = "scenario blood-sucking inventory bound is unavailable"; return false; }
            int remaining = gpGlobals->maxEntities;
            for (auto* item = player->m_rgpPlayerItems[GRENADE_SLOT]; Live(item); item = item->m_pNext)
            {
                if (--remaining < 0)
                { error.message = "scenario blood-sucking grenade chain is cyclic"; return false; }
                if (!append(item)) return false;
            }
        }
        for (const auto& record : records) percentage += record.parameters[1];
        if (!std::isfinite(percentage))
        { error.message = "scenario blood-sucking percentage overflowed"; return false; }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioWeaponState::ApplyPartHitEffects(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error,
    std::uint32_t (*seed)()) const
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto owned = [&]
    { return runtime && !runtime->Closing() && session && runtime->session == session &&
        session->Weapons().get() == this && Current() &&
        call.Spawn().BelongsTo(session->Map()) && call.Current(); };
    if (!owned()) return call.Cancel();
    const auto keepAlive = session->Weapons();
    auto* attacker = call.Player();
    if (!attacker) return true;
    const EHANDLE actor(attacker);
    const auto players = runtime->players;
    const auto state = players ? players->Find(actor) : nullptr;
    if (!state) { error.message = "scenario part-hit player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && runtime->players == players && players->Find(actor) == state && call.Player() == attacker; };
    std::vector<CSOLWeaponPartEffect> records;
    const auto collect = [&](int kind)
    {
        const bool found = CollectParts(*attacker, call.Inflictor(), 15, kind,
            state->profile.weaponEffectsBlocked21F2 != 0, state->profile.weaponEffectsBlocked34F0,
            records, [](const void* context) { return (*static_cast<const decltype(current)*>(context))(); }, &current);
        if (!current()) return call.Cancel();
        if (!found) { error.message = "scenario part-hit source collection failed"; return false; }
        return true;
    };
    const auto initialize = [&](std::mt19937& engine)
    {
        std::uint32_t value{};
        try { value = seed ? seed() : static_cast<std::uint32_t>(std::random_device{}()); }
        catch (...)
        {
            if (!current()) return call.Cancel();
            error.message = "scenario part-hit entropy source failed";
            return false;
        }
        if (!current()) return call.Cancel();
        engine.seed(value);
        return true;
    };
    try
    {
        if (!collect(8)) return false;
        if (!records.empty())
        {
            std::mt19937 engine;
            if (!initialize(engine)) return false;
            double chance{}, multiplier = 1.0;
            for (const auto& record : records)
            {
                chance += record.parameters[0];
                multiplier = std::max(multiplier, record.parameters[1]);
            }
            if (!std::isfinite(chance))
            { error.message = "scenario part critical probability overflowed"; return false; }
            if (chance >= csol::scenario::WeaponAttributeRandom(engine))
            {
                const float damage = static_cast<float>(static_cast<double>(call.damage) * multiplier);
                if (!std::isfinite(damage))
                { error.message = "scenario part critical damage overflowed"; return false; }
                call.Current()->HostState().damageEffect = 8;
                call.damage = damage;
            }
        }
        // The original signed skill predicate is separate from the later
        // statistics-Boss classification. Bosses still receive part8 above.
        if (static_cast<std::int16_t>(call.Current()->Type()->skill) >= 1000) return true;
        for (int kind : {10, 11})
        {
            if (!collect(kind)) return false;
            if (records.empty()) continue;
            std::mt19937 engine;
            if (!initialize(engine)) return false;
            double duration{}, multiplier{};
            for (const auto& record : records)
            {
                const double roll = csol::scenario::WeaponAttributeRandom(engine);
                if (record.parameters[0] >= roll && record.parameters[1] > duration)
                {
                    duration = record.parameters[1];
                    multiplier = record.parameters[2];
                }
            }
            if (!(duration > 0)) continue;
            if (!gpGlobals || !std::isfinite(gpGlobals->time))
            { error.message = "scenario part-hit clock is unavailable"; return false; }
            const double until = static_cast<double>(gpGlobals->time) + duration;
            if (!std::isfinite(until))
            { error.message = "scenario part-hit deadline overflowed"; return false; }
            auto* monster = call.Current();
            auto& host = monster->HostState();
            auto& speed = monster->SpeedState();
            auto& deadline = kind == 10 ? speed.impulseProtectionUntil : speed.boostUntil;
            if (until > deadline)
            {
                deadline = until;
                if (kind == 11) speed.boostAdd = multiplier;
                host.damageEffect = kind;
                host.damageEffectMagnitude = duration;
            }
        }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}
bool CCSOLScenarioWeaponState::SetItemIdentity(CBasePlayerWeapon& weapon, std::int32_t item)
{
    if (!Current() || !Live(&weapon)) return false;
    const auto found = m_catalog.find(item);
    if (found == m_catalog.end() || !found->second.equipment) return false;
    if (weapon.m_iId == WEAPON_KNIFE || weapon.m_iId == WEAPON_HEGRENADE)
    { if (!NativeSelectionItem(item, weapon.m_iId)) return false; }
    else if (found->second.category == 7 || found->second.inGame != Alias(weapon.m_iId)) return false;
    if (auto* state = Edit(weapon)) { state->item = item; return true; }
    return false;
}

bool CCSOLScenarioWeaponState::NativeSelectionItem(std::int32_t item, int source) const
{
    const auto found = m_catalog.find(item);
    if (!Current() || found == m_catalog.end() || !found->second.equipment) return false;
    if (source == WEAPON_KNIFE) return found->second.category == 11 && found->second.inGame == 4000;
    return source == WEAPON_HEGRENADE && found->second.category == 6 && found->second.inGame == 2003;
}

bool CCSOLScenarioWeaponState::ValidSelection(std::array<std::int32_t, 2> items) const
{
    return NativeSelectionItem(items[0], WEAPON_KNIFE) && NativeSelectionItem(items[1], WEAPON_HEGRENADE);
}

bool CCSOLScenarioWeaponState::SelectItems(CBasePlayer& player, std::uint64_t generation,
    std::array<std::int32_t, 2> items)
{
    if (!ValidSelection(items) || !generation || !Live(&player) || player.has_disconnected) return false;
    std::erase_if(m_selections, [](const Selection& state) { return !Live(state.player.Get<CBaseEntity>()); });
    for (auto& state : m_selections)
        if (state.player.Get<CBaseEntity>() == &player)
        {
            if (state.generation > generation) return false;
            state = {EHANDLE(&player), generation, items};
            return true;
        }
    m_selections.push_back({EHANDLE(&player), generation, items});
    return true;
}

void CCSOLScenarioWeaponState::RemovePlayer(const EHANDLE& player, std::uint64_t generation)
{
    if (!Current()) return;
    std::erase_if(m_selections, [&](const Selection& state)
    { return state.generation == generation && state.player.Get() == player.Get(); });
}

bool CCSOLScenarioWeaponState::RecordHeGrenade(CBaseEntity& grenade)
{
    if (!Current() || !Live(&grenade) || !dynamic_cast<CGrenade*>(&grenade)) return false;
    std::erase_if(m_heGrenades, [](const EHANDLE& handle) { return !Live(handle.Get<CBaseEntity>()); });
    for (const auto& handle : m_heGrenades) if (handle.Get<CBaseEntity>() == &grenade) return true;
    m_heGrenades.emplace_back(&grenade);
    return true;
}

int CCSOLScenarioWeaponState::ProjectileSource(const CBaseEntity& entity) const
{
    if (!Current() || !Live(&entity)) return 0;
    for (const auto& handle : m_heGrenades)
        if (handle.Get<CBaseEntity>() == &entity) return WEAPON_HEGRENADE;
    return 0;
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
    if (source == WEAPON_KNIFE || source == WEAPON_HEGRENADE)
    {
        if (Live(&attacker) && !attacker.has_disconnected)
            for (const auto& state : m_selections)
                if (state.player.Get<CBaseEntity>() == &attacker)
                { item = state.items[source == WEAPON_KNIFE ? 0 : 1]; return true; }
        error.message = "player knife or HE selection is unresolved";
        return false;
    }
    const int alias = Alias(source);
    const auto found = m_items.find(alias);
    if (found != m_items.end()) { item = found->second; return true; }
    auto* weapon = CarriedSource(attacker, source);
    if (weapon)
        if (const auto* state = Find(*weapon); state && state->item) { item = *state->item; return true; }
    // With no carried candidate there is no linked-skin fallback to inspect.
    if (!weapon) return true;
    error.message = "linked weapon item identity is unresolved";
    return false;
}
bool CCSOLScenarioWeaponState::ApplyItemMultiplier(CSOLMonsterDamageCall& call, int capturedSource, bool blocked21F2,
    csol::scenario::ConfigError& error) const
{
    if (!Current() || !call.Current()) return false;
    auto* player = call.Player();
    if (!player || blocked21F2 || !Active(*player) || (static_cast<std::uint32_t>(call.bits) & 0x38000000u)) return true;
    // Initial empty table is an actual owned runtime state. An installed zero
    // value is distinct: the lookup below must multiply by zero, not skip it.
    if (m_multipliers.empty()) return true;
    std::int32_t item{};
    if (!Item(*player, capturedSource, item, error)) return false;
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
    const auto current = [&] { return Current() && call.Current() && call.Player() == player; };
    const auto sum = [&](int kind, std::size_t member, double& value)
    {
        std::vector<CSOLWeaponPartEffect> records;
        const bool collected = CollectParts(*player, call.Inflictor(), 15, kind, blocked21F2, blocked34F0,
            records, [](const void* context) { return (*static_cast<const decltype(current)*>(context))(); }, &current);
        if (!current()) return call.Cancel();
        if (!collected) return false;
        value = 0;
        for (const auto& record : records) value += record.parameters[member];
        return std::isfinite(value);
    };
    double damage{}, knockback{}, flinch{}, boss{};
    if (!sum(1, 0, damage) || !sum(6, 1, knockback) || !sum(7, 1, flinch))
    {
        if (!current()) return call.Cancel();
        error.message = "invalid weapon part damage sum";
        return false;
    }
    double total = knockback + damage;
    total = flinch + total;
    if (static_cast<std::int16_t>(monster->Type()->skill) >= 1000)
    {
        if (!sum(14, 0, boss))
        {
            if (!current()) return call.Cancel();
            error.message = "invalid Boss part damage sum";
            return false;
        }
        total = boss + total;
    }
    if (!std::isfinite(total)) { error.message = "weapon part damage sum overflowed"; return false; }
    if (std::abs(total) > 0x1p-52)
        call.damage = static_cast<float>(static_cast<double>(call.damage) * (1.0 + total));
    if (!std::isfinite(call.damage)) { error.message = "weapon part multiplier overflowed damage"; return false; }
    return call.Current() != nullptr;
}
