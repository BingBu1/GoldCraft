// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "ehandle.h"
#include "scenario_entity_access.h"
#include "../scenario/map_resources.hpp"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

class CSOLMonsterDamageCall;
class CSOLPlayerRecoveryCall;
class CBasePlayerWeapon;
class CBasePlayer;
class CCSOLScenarioRuntime;

// Raw metadata uses Number() == 0 in CSO. Only the double alternative is a
// numeric modifier; a raw64 payload must never be reinterpreted as a double.
using CSOLWeaponAttributeValue = std::variant<std::int32_t, std::uint64_t, double>;

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
    explicit CCSOLScenarioWeaponState(std::shared_ptr<const CSOLScenarioEntityAccess> access = {});
    CCSOLScenarioWeaponState(const CCSOLScenarioWeaponState&) = delete;
    CCSOLScenarioWeaponState& operator=(const CCSOLScenarioWeaponState&) = delete;
    bool Load(csol::scenario::ResourceFileApi files, csol::scenario::ResourceError& error);
    void Invalidate();
    bool Current() const { return Accessible() && m_loaded; }
    std::shared_ptr<const csol::scenario::CsvDocument> ItemCatalog() const
    { return Current() ? m_itemCatalog : nullptr; }
    bool SetParts(CBasePlayerWeapon& weapon, std::array<std::uint32_t, 2> slots);
    std::array<std::uint32_t, 2> Parts(const CBasePlayerWeapon& weapon) const;
    // Original weapon word126 starts at0 and belongs to the entity, not its
    // current owner. Trusted server producers may update it after creation.
    bool SetEnhancementLevel(CBasePlayerWeapon& weapon, std::uint16_t level);
    std::uint16_t EnhancementLevel(const CBasePlayerWeapon& weapon) const;
    // Complete imported property.v294 directory, keyed by catalog item ID.
    // Uninitialized is distinct from an imported directory missing an item.
    bool ReplaceEnhancementDamage(std::map<std::int32_t, std::vector<std::uint32_t>> percentages);
    bool ApplyEnhancementDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error) const;
    // After hit rewards and before enhancement. This checks BuffWeapon's
    // static type0 definition, not the player's active equipment bonuses.
    bool NotifyDamageBuff(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error) const;
    // Signed weapon+160 input used by addon1. Its actual native producer is
    // not identified yet; do not infer it from ammo or weapon class names.
    bool SetAddonWeaponInput(CBasePlayerWeapon& weapon, std::optional<std::int32_t> value);
    std::optional<std::int32_t> AddonWeaponInput(const CBasePlayerWeapon& weapon) const;
    // Server-owned attribute directory, separate from parts and item IDs.
    // These explicit creation APIs do not enable the unresolved pickup
    // feature gate. raw2000 is the original service's opaque 8-byte value;
    // its producer and client synchronization must be installed separately.
    std::optional<std::uint32_t> CreateAttributes(std::int32_t weaponId, std::uint64_t raw2000);
    std::optional<std::uint32_t> CloneAttributes(std::uint32_t sourceKey, std::uint64_t raw2000);
    bool SetAttribute(std::uint32_t key, int kind, std::optional<CSOLWeaponAttributeValue> value,
        std::uint8_t invalidateCache);
    std::optional<CSOLWeaponAttributeValue> Attribute(std::uint32_t key, int kind) const;
    double AttributeNumber(std::uint32_t key, int kind) const;
    bool HasAdditionalAttributeObjects() const;
    bool SetAttributeKey(CBasePlayerWeapon& weapon, std::uint32_t key);
    std::uint32_t AttributeKey(const CBasePlayerWeapon& weapon) const;
    // Original player v26C: a player inflictor supplies its active weapon;
    // other sources search the attacker's single-weapon slots 1..3 only.
    // false means cancellation or an unsupported null world-wrapper source;
    // true with a null handle is a legitimate no-source result. Callers must
    // also revalidate the hit.
    bool AttributeWeapon(CBasePlayer& attacker, CBaseEntity* inflictor, EHANDLE& output,
        bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr) const;
    // Original mode15 arithmetic through kind7's recovery amount. This is
    // not the complete attribute stage: a present recovery (including 0 or
    // a negative amount) must go through the player's original recovery
    // service before the attacked notification and subsequent damage stages.
    // A seed callback replaces only the entropy boundary, never the MT/math.
    bool PrepareAttributeDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, std::optional<float>& recovery,
        csol::scenario::ConfigError& error, std::uint32_t (*seed)() = nullptr) const;
    // Complete this attribute stage: consume a present recovery amount through
    // the actual player owner before returning to attacked effects/attack buffs.
    // The full damage provider must propagate failure, preserving its prefix.
    bool ApplyAttributeDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime,
        csol::scenario::ConfigError& error, std::uint32_t (*seed)() = nullptr) const;
    // The original effect CSV's filename is not identified yet. Only accept
    // explicit server-owned records; never load the unrelated combine table.
    bool ReplacePartEffects(std::vector<CSOLWeaponPartEffect> effects);
    bool CollectParts(CBasePlayer& attacker, CBaseEntity* inflictor, int mode, int kind,
        bool blocked21F2, bool blocked34F0, std::vector<CSOLWeaponPartEffect>& output,
        bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr) const;
    // Before critical/attribute/attack buffs: part8 critical, then the
    // independent part10/11 deadlines. This does not clear damageEffect or
    // repeat the earlier basic part sum. Entropy is the only test seam.
    bool ApplyPartHitEffects(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error,
        std::uint32_t (*seed)() = nullptr) const;
    // Post-health part9 adds to the real deferred player module; it does not
    // heal immediately or substitute attribute kind7's self-recovery stage.
    bool RecordPartBloodSucking(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error) const;
    bool BloodSuckingLimit(CSOLPlayerRecoveryCall& call, double& percentage,
        csol::scenario::ConfigError& error) const;

    // These are item-catalog keys, not inventory/HUD weapon IDs. The first
    // float remains raw until its separate consumer is identified.
    bool SetItemMultiplier(std::int32_t item, std::array<float, 2> values);
    void ClearItemMultipliers();
    std::span<const std::int32_t> MultiplierUpdates() const { return m_multiplierUpdates; }
    // Instance identity must match its actual implementation. A catalog row
    // being present does not make a character/paint/other weapon compatible.
    bool SetItemIdentity(CBasePlayerWeapon& weapon, std::int32_t item);
    // source29/source4 query player selection, even after the last grenade's
    // inventory entity has gone. Only the implemented native knife/HE pair is
    // registered here; other selections require their real implementation.
    std::array<std::int32_t, 2> DefaultSelection() const { return m_defaultSelection; }
    bool ValidSelection(std::array<std::int32_t, 2> items) const;
    bool SelectItems(CBasePlayer& player, std::uint64_t generation, std::array<std::int32_t, 2> items);
    void RemovePlayer(const EHANDLE& player, std::uint64_t generation);
    // Called only by the actual native HE creation path, never by classname.
    bool RecordHeGrenade(CBaseEntity& grenade);
    int ProjectileSource(const CBaseEntity& entity) const;
    // Shared by damage and kill statistics: source IDs are mapped through
    // the actual catalog/owned weapon identity, never through HUD aliases.
    bool Item(CBasePlayer& attacker, int source, std::int32_t& item,
        csol::scenario::ConfigError& error) const;
    bool ApplyItemMultiplier(CSOLMonsterDamageCall& call, int capturedSource, bool blocked21F2,
        csol::scenario::ConfigError& error) const;
    bool ApplyPartDamage(CSOLMonsterDamageCall& call, bool blocked21F2, bool blocked34F0,
        csol::scenario::ConfigError& error) const;
private:
    bool Accessible() const { return m_valid && (!m_access || m_access->Allowed()); }
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    struct Weapon
    {
        EHANDLE handle;
        std::array<std::uint32_t, 2> parts{};
        std::optional<std::int32_t> item;
        std::uint32_t attributeKey{};
        std::uint16_t enhancementLevel{};
        std::optional<std::int32_t> addonInput;
    };
    struct AttributeCounters { std::uint32_t sequence{}, live{}; };
    struct AttributeRecord
    {
        explicit AttributeRecord(AttributeCounters& counts);
        AttributeRecord(const AttributeRecord& source);
        AttributeRecord& operator=(const AttributeRecord&) = delete;
        ~AttributeRecord();
        bool Set(int kind, std::optional<CSOLWeaponAttributeValue> value, std::uint8_t invalidateCache);
        AttributeCounters& counts;
        std::map<int, std::shared_ptr<const CSOLWeaponAttributeValue>> values;
        // Original +0C bit container and +1C byte. Their read-side business
        // meaning is unresolved; do not repurpose these as network dirty bits.
        std::vector<bool> cache = std::vector<bool>(33, false);
        std::uint8_t cacheState{};
    };
    const AttributeRecord* ResolveAttributes(std::uint32_t key) const;
    struct CatalogItem { std::int32_t category{}, inGame{}; bool equipment{}; };
    struct Selection
    {
        EHANDLE player;
        std::uint64_t generation{};
        std::array<std::int32_t, 2> items{};
    };
    bool NativeSelectionItem(std::int32_t item, int source) const;
    Weapon* Edit(CBasePlayerWeapon& weapon);
    const Weapon* Find(const CBasePlayerWeapon& weapon) const;
    void AppendParts(const CBasePlayerWeapon& weapon, bool passive, int mode, int kind,
        std::vector<CSOLWeaponPartEffect>& output) const;
    std::map<std::int32_t, std::int32_t> m_items;
    std::map<std::int32_t, CatalogItem> m_catalog;
    std::shared_ptr<const csol::scenario::CsvDocument> m_itemCatalog;
    std::array<std::int32_t, 2> m_defaultSelection{};
    std::vector<Selection> m_selections;
    std::vector<EHANDLE> m_heGrenades;
    std::vector<Weapon> m_weapons;
    // Counters outlive every record, including temporary copies. Their scope
    // is this map owner; no mutable state leaks into the next map session.
    AttributeCounters m_attributeCounters;
    std::map<std::uint32_t, AttributeRecord> m_attributes;
    std::map<std::uint32_t, std::vector<CSOLWeaponPartEffect>> m_parts;
    std::optional<std::map<std::int32_t, std::vector<std::uint32_t>>> m_enhancementDamage;
    std::map<std::int32_t, std::array<float, 2>> m_multipliers;
    std::vector<std::int32_t> m_multiplierUpdates;
    bool m_valid{true}, m_loaded{};
};
