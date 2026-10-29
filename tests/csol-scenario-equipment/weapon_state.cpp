// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "../../native/csol/server/scenario_weapon_state.h"
#include "../../native/csol/server/weapons.h"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace equipment_test
{
namespace
{
constexpr const char* ItemPath = "resource/item.csv";
constexpr const char* DefaultItemsPath = "resource/defaultitems.csv";
[[noreturn]] void UnexpectedWeaponCall()
{ throw std::runtime_error("weapon gameplay virtual method is outside the state test"); }

// Preserve actual SDK weapon layout, RTTI, fields and EHANDLE behavior. These
// unrelated gameplay methods must never execute; they are not fake successes.
struct Weapon final : CBasePlayerWeapon
{
    std::function<void()> onIsWeapon;
    int weaponQueries{};
    BOOL IsWeapon() override
    {
        ++weaponQueries; const auto callback = onIsWeapon; if (callback) callback(); return TRUE;
    }
    void OnCreate() override {}
    void OnDestroy() override {}
    void Spawn() override { UnexpectedWeaponCall(); }
    int Save(CSave&) override { UnexpectedWeaponCall(); }
    int Restore(CRestore&) override { UnexpectedWeaponCall(); }
    void SetObjectCollisionBox() override { UnexpectedWeaponCall(); }
    CBaseEntity* Respawn() override { UnexpectedWeaponCall(); }
    int AddToPlayer(CBasePlayer*) override { UnexpectedWeaponCall(); }
    int AddDuplicate(CBasePlayerItem*) override { UnexpectedWeaponCall(); }
    BOOL CanDeploy() override { UnexpectedWeaponCall(); }
    void Holster(int) override { UnexpectedWeaponCall(); }
    void Drop() override { UnexpectedWeaponCall(); }
    void Kill() override { UnexpectedWeaponCall(); }
    void AttachToPlayer(CBasePlayer*) override { UnexpectedWeaponCall(); }
    void ItemPostFrame() override { UnexpectedWeaponCall(); }
    int PrimaryAmmoIndex() override { UnexpectedWeaponCall(); }
    int SecondaryAmmoIndex() override { UnexpectedWeaponCall(); }
    int UpdateClientData(CBasePlayer*) override { UnexpectedWeaponCall(); }
    int ExtractAmmo(CBasePlayerWeapon*) override { UnexpectedWeaponCall(); }
    int ExtractClipAmmo(CBasePlayerWeapon*) override { UnexpectedWeaponCall(); }
    BOOL PlayEmptySound() override { UnexpectedWeaponCall(); }
    void ResetEmptySound() override { UnexpectedWeaponCall(); }
    void SendWeaponAnim(int, int) override { UnexpectedWeaponCall(); }
    BOOL IsUseable() override { UnexpectedWeaponCall(); }
    void RetireWeapon() override { UnexpectedWeaponCall(); }
};
struct Projectile final : CBaseEntity, CSOLWeaponDamageSource
{
    int launchId{};
    void OnCreate() override {}
    void OnDestroy() override {}
    int DamageWeaponId() const override { return launchId; }
};
struct AttributeSourceState
{
    int playerQueries{}, sourceQueries{};
    std::vector<int> ids{220};
    std::function<void()> onPlayer, onSource;
};
struct AttributeSource final : CBaseEntity, CSOLWeaponDamageSource
{
    std::shared_ptr<AttributeSourceState> probe = std::make_shared<AttributeSourceState>();
    void OnCreate() override {}
    void OnDestroy() override {}
    BOOL IsPlayer() override
    { const auto keep = probe; ++keep->playerQueries; const auto callback = keep->onPlayer; if (callback) callback(); return FALSE; }
    int DamageWeaponId() const override
    {
        const auto keep = probe; const auto index = keep->sourceQueries++;
        const int value = keep->ids[std::min<std::size_t>(index, keep->ids.size() - 1)];
        const auto callback = keep->onSource; if (callback) callback(); return value;
    }
};
template<class T> struct Entity
{
    edict_t& ed;
    T* value{};
    Entity(Engine& engine, int slot) : ed(engine.edicts[slot]) { Create(); }
    ~Entity() { Destroy(); }
    void Create()
    {
        Require(!value && !ed.pvPrivateData && ed.free, "unused actual SDK entity slot");
        ed.free = FALSE;
        ed.v = {};
        ed.v.pContainingEntity = &ed;
        value = GetClassPtr<CCSEntity>(reinterpret_cast<T*>(&ed.v));
        Require(value && value->pev == &ed.v, "actual SDK entity construction");
    }
    void Destroy()
    {
        if (!value) return;
        value->OnDestroy();
        delete value->m_pEntity;
        value->m_pEntity = nullptr;
        value->~T();
        std::free(ed.pvPrivateData);
        ed.pvPrivateData = nullptr;
        ed.free = TRUE;
        ++ed.serialnumber;
        value = nullptr;
    }
    T& Get() const { return *value; }
};
void Load(CCSOLScenarioWeaponState& state, const std::string& bytes, const std::string& defaults)
{
    difficulty_test::Files files;
    files.contents[ItemPath] = bytes;
    files.contents[DefaultItemsPath] = defaults;
    ResourceError error;
    const bool loaded = state.Load(files.Api(), error);
    if (!loaded) std::cerr << error.path << ": " << error.detail.message << '\n';
    Require(loaded && state.Current(), "actual item resource loads through production file API");
    Check(files.order == std::vector<std::string>{std::string("load:") + ItemPath,
        std::string("free:") + ItemPath, std::string("load:") + DefaultItemsPath,
        std::string("free:") + DefaultItemsPath},
        "catalog and defaults are owned before their exact engine buffers are poisoned and freed");
    Check(!state.Load(files.Api(), error) && state.Current(), "loaded state rejects repeated load without losing snapshot");
}
void Item(CCSOLScenarioWeaponState& state, CBasePlayer& player, int source, int expected)
{
    std::int32_t item = -1;
    ConfigError error;
    Check(state.Item(player, source, item, error) && item == expected,
        "production source lookup returns expected catalog item");
}
void Unresolved(CCSOLScenarioWeaponState& state, CBasePlayer& player, int source)
{
    std::int32_t item = -1;
    ConfigError error;
    Check(!state.Item(player, source, item, error) && item == 0 && !error.message.empty(),
        "unresolved selection fails explicitly and clears previous caller item");
}
void DefaultResourceCases(const std::string& items, const std::string& defaults)
{
    const std::array<std::optional<std::string>, 12> invalid{
        std::nullopt, std::string{}, "Knife,ThrowWeapon\r\n",
        "Knife,ThrowWeapon\r\n161,31\r\n161,31\r\n",
        "Knife,Knife,ThrowWeapon\r\n161,161,31\r\n",
        "Knife,ThrowWeapon,ThrowWeapon\r\n161,31,31\r\n",
        "knife,ThrowWeapon\r\n161,31\r\n",
        "Knife,ThrowWeapon\r\n161\r\n",
        "Knife,ThrowWeapon\r\n164,31\r\n",
        "Knife,ThrowWeapon\r\n161,428\r\n",
        "Knife,ThrowWeapon\r\n745,31\r\n",
        "Knife,ThrowWeapon\r\n161,2147483647\r\n"};
    for (const auto& broken : invalid)
    {
        difficulty_test::Files files;
        files.contents[ItemPath] = items;
        if (broken) files.contents[DefaultItemsPath] = *broken;
        CCSOLScenarioWeaponState state;
        ResourceError error;
        Check(!state.Load(files.Api(), error) && !state.Current() && error.path == DefaultItemsPath &&
            !error.detail.message.empty() && files.buffers.empty(),
            "missing malformed or incompatible default selection fails without retaining an engine buffer");
        Check(state.DefaultSelection() == std::array<std::int32_t, 2>{} &&
            !state.ValidSelection({161, 31}), "failed defaults never publish a partial item catalog");
        files.contents[DefaultItemsPath] = defaults;
        Check(state.Load(files.Api(), error) && state.DefaultSelection() == std::array<std::int32_t, 2>{161, 31},
            "a failed resource attempt can load corrected actual defaults without stale partial state");
    }
    CCSOLScenarioWeaponState reordered;
    Load(reordered, items, "ThrowWeapon,unused,Knife\r\n31,0,161\r\n");
    Check(reordered.DefaultSelection() == std::array<std::int32_t, 2>{161, 31},
        "default selection columns are resolved by their names rather than fixed offsets");

    // Exact IDs alone are insufficient: both native implementation records
    // must still have their verified Equipment/category/InGameID tuple.
    const std::array<std::pair<const char*, const char*>, 6> wrongCatalog{{
        {"161,defaultKnife,defaultKnife,Class,11,4000", "31,HEGrenade,HEGrenade,Equipment,6,2003"},
        {"161,defaultKnife,defaultKnife,Equipment,7,4000", "31,HEGrenade,HEGrenade,Equipment,6,2003"},
        {"161,defaultKnife,defaultKnife,Equipment,11,4001", "31,HEGrenade,HEGrenade,Equipment,6,2003"},
        {"161,defaultKnife,defaultKnife,Equipment,11,4000", "31,HEGrenade,HEGrenade,SkinItem,6,2003"},
        {"161,defaultKnife,defaultKnife,Equipment,11,4000", "31,HEGrenade,HEGrenade,Equipment,11,2003"},
        {"161,defaultKnife,defaultKnife,Equipment,11,4000", "31,HEGrenade,HEGrenade,Equipment,6,144"}}};
    for (const auto& [knife, he] : wrongCatalog)
    {
        difficulty_test::Files files;
        files.contents[ItemPath] = std::string(";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n") +
            knife + "\r\n" + he + "\r\n";
        files.contents[DefaultItemsPath] = defaults;
        CCSOLScenarioWeaponState state;
        ResourceError error;
        Check(!state.Load(files.Api(), error) && !state.Current() && error.path == DefaultItemsPath && files.buffers.empty(),
            "known default IDs with incompatible class category or implementation are rejected");
    }
}
CSOLWeaponPartEffect Effect(std::uint32_t id, int marker, bool passive, int kind = 1)
{
    CSOLWeaponPartEffect row;
    row.id = id;
    row.tier = marker;
    row.passive = passive;
    row.kind = kind;
    row.modes = {1u << 15};
    row.parameters = {double(marker), double(marker) + 0.5, -double(marker)};
    return row;
}
void Collect(CCSOLScenarioWeaponState& state, CBasePlayer& player, CBaseEntity* source,
    const std::vector<int>& expected, int mode = 15, int kind = 1)
{
    std::vector<CSOLWeaponPartEffect> rows{Effect(9999, -1, false)};
    Require(state.CollectParts(player, source, mode, kind, false, false, rows), "public part collector succeeds");
    std::vector<int> actual;
    for (const auto& row : rows) actual.push_back(row.tier);
    Check(actual == expected, "original direct/carried/slot/row ordering with duplicates preserved");
    for (const auto& row : rows)
        Check(row.parameters[0] == double(row.tier) && row.parameters[1] == double(row.tier) + 0.5,
            "ordered collected records preserve owned double parameters");
}

template<class T> bool AttributeEquals(const CCSOLScenarioWeaponState& state,
    std::uint32_t key, int kind, T expected)
{
    const auto value = state.Attribute(key, kind);
    return value && std::holds_alternative<T>(*value) && std::get<T>(*value) == expected;
}

void AttributeDirectoryCases(const std::string& items, const std::string& defaults)
{
    CCSOLScenarioWeaponState state;
    Check(!state.CreateAttributes(726, 1) && !state.CloneAttributes(0, 1) &&
        !state.SetAttribute(0, 1, CSOLWeaponAttributeValue{1.0}, 0) &&
        !state.Attribute(0, 1) && state.AttributeNumber(0, 1) == 0 && !state.HasAdditionalAttributeObjects(),
        "constructed but unloaded attribute owner does not publish or mutate its default record");
    Load(state, items, defaults);
    for (const auto key : {std::uint32_t{0}, std::uint32_t{123456}, UINT32_MAX})
        for (const int kind : {1, 5, 6, 7, 1000, 2000, 4006})
            Check(!state.Attribute(key, kind) && state.AttributeNumber(key, kind) == 0,
                "actual default key zero has missing kinds and missing directory keys fall back to that empty record");
    Check(!state.HasAdditionalAttributeObjects(), "default temporary has retired and one retained node does not pass original live>=2 gate");
    Require(state.SetAttribute(0, 1, CSOLWeaponAttributeValue{std::int32_t{-17}}, 0), "install original four-byte metadata subtype");
    Check(AttributeEquals(state, 0, 1, std::int32_t{-17}) && state.AttributeNumber(0, 1) == 0,
        "raw32 metadata retains its signed payload but Number is zero");
    constexpr std::uint64_t rawDoubleBits = 0x400a000000000000ULL;
    Require(state.SetAttribute(0, 5, CSOLWeaponAttributeValue{rawDoubleBits}, 1), "install opaque eight-byte metadata subtype");
    Check(AttributeEquals(state, 0, 5, rawDoubleBits) && state.AttributeNumber(0, 5) == 0,
        "raw64 payload resembling double3.25 remains metadata and is never reinterpreted numerically");
    Require(state.SetAttribute(0, 6, CSOLWeaponAttributeValue{-3.25}, 0), "install actual double subtype");
    Check(AttributeEquals(state, 0, 6, -3.25) && state.AttributeNumber(0, 6) == -3.25 &&
        state.AttributeNumber(UINT32_MAX, 6) == -3.25,
        "only the double subtype is numeric and missing-key reads resolve the real default value");
    Require(state.SetAttribute(UINT32_MAX, 6, CSOLWeaponAttributeValue{0.75}, 0), "setter follows missing-key fallback");
    Check(state.AttributeNumber(0, 6) == 0.75 && state.AttributeNumber(123456, 6) == 0.75 &&
        !state.HasAdditionalAttributeObjects(), "missing-key setter replaces default without allocating another attribute container");
    Require(state.SetAttribute(0, 1, CSOLWeaponAttributeValue{2.5}, 0), "flag-zero replacement changes value subtype");
    Check(AttributeEquals(state, 0, 1, 2.5) && state.AttributeNumber(0, 1) == 2.5,
        "setter flag zero permits replacement and is not an overwrite permission gate");
    auto detached = state.Attribute(0, 1); Require(detached.has_value(), "query returns an owned value copy");
    *detached = CSOLWeaponAttributeValue{99.0};
    Check(state.AttributeNumber(0, 1) == 2.5, "mutating returned variant cannot silently edit authoritative storage");
    Require(state.SetAttribute(0, 7, CSOLWeaponAttributeValue{-0.0}, 1), "double negative zero is a valid stored value");
    Check(std::bit_cast<std::uint64_t>(state.AttributeNumber(0, 7)) == 0x8000000000000000ULL,
        "double getter retains negative-zero bits");
    Require(state.SetAttribute(0, 1, std::nullopt, 0), "empty optional removes a present kind");
    Check(!state.Attribute(0, 1) && state.AttributeNumber(0, 1) == 0 &&
        state.SetAttribute(0, 1, std::nullopt, 1) && !state.Attribute(0, 1),
        "empty value deletes rather than inserting a null record and deleting an absent valid kind succeeds");
    for (const int kind : {1, 3999, 4000, 4001, 4002, 4003, 4004, 4005, 4006})
    {
        Require(state.SetAttribute(0, kind, CSOLWeaponAttributeValue{double(kind)}, 1), "legal kind accepted including both cache-exclusion boundaries");
        Check(AttributeEquals(state, 0, kind, double(kind)), "legal boundary kind stores its actual typed value");
        Require(state.SetAttribute(0, kind, std::nullopt, 0), "legal boundary kind can be deleted with flag zero");
    }
    for (const int kind : {std::numeric_limits<int>::min(), -1, 0, 4007, std::numeric_limits<int>::max()})
        Check(!state.SetAttribute(0, kind, CSOLWeaponAttributeValue{1.0}, 1) &&
            !state.SetAttribute(0, kind, std::nullopt, 0) && !state.Attribute(0, kind) &&
            state.AttributeNumber(0, 6) == 0.75 && !state.HasAdditionalAttributeObjects(),
            "invalid kind cannot insert, erase unrelated values or create a counted object");
    const auto first = state.CreateAttributes(726, rawDoubleBits);
    const auto second = state.CreateAttributes(220, UINT64_MAX);
    Check(first == std::optional<std::uint32_t>{3} && second == std::optional<std::uint32_t>{5} &&
        state.HasAdditionalAttributeObjects(), "first two explicit creates retain temporary-plus-node key progression3/5 after value-only operations");
    Require(first && second, "created keys exist before metadata lookup");
    Check(AttributeEquals(state, *first, 1000, std::int32_t{726}) && AttributeEquals(state, *first, 2000, rawDoubleBits) &&
        AttributeEquals(state, *second, 1000, std::int32_t{220}) && AttributeEquals(state, *second, 2000, UINT64_MAX) &&
        state.AttributeNumber(*first, 1000) == 0 && state.AttributeNumber(*first, 2000) == 0 && !state.Attribute(*first, 6),
        "v74 creates fresh metadata records instead of copying changed key-zero modifiers");
}

void AttributeCloneCases(const std::string& items, const std::string& defaults)
{
    CCSOLScenarioWeaponState state; Load(state, items, defaults);
    const auto source = state.CreateAttributes(144, 0x0102030405060708ULL);
    Require(source && *source == 3, "source create has original local key progression");
    Require(state.SetAttribute(*source, 1, CSOLWeaponAttributeValue{1.25}, 0) &&
        state.SetAttribute(*source, 5, CSOLWeaponAttributeValue{std::int32_t{33}}, 1), "source has number and metadata before clone");
    const auto copy = state.CloneAttributes(*source, 0x8877665544332211ULL);
    Require(copy && *copy == 5, "clone allocates a temporary and retained copied node");
    Check(AttributeEquals(state, *copy, 1000, std::int32_t{144}) &&
        AttributeEquals(state, *copy, 2000, std::uint64_t{0x8877665544332211ULL}) &&
        AttributeEquals(state, *source, 2000, std::uint64_t{0x0102030405060708ULL}) &&
        state.AttributeNumber(*copy, 1) == 1.25 && AttributeEquals(state, *copy, 5, std::int32_t{33}),
        "v7C copies source kinds and types but replaces only the copy's kind2000 metadata");
    Require(state.SetAttribute(*source, 1, CSOLWeaponAttributeValue{2.5}, 0), "replace source shared value after clone");
    Require(state.SetAttribute(*copy, 5, std::nullopt, 0), "delete copied metadata independently");
    Check(state.AttributeNumber(*source, 1) == 2.5 && state.AttributeNumber(*copy, 1) == 1.25 &&
        AttributeEquals(state, *source, 5, std::int32_t{33}) && !state.Attribute(*copy, 5),
        "source replacement and copied-kind deletion do not mutate the other record's retained values");
    Require(state.SetAttribute(*copy, 1, CSOLWeaponAttributeValue{std::uint64_t{17}}, 0), "clone may replace numeric value with raw metadata");
    Check(state.AttributeNumber(*copy, 1) == 0 && state.AttributeNumber(*source, 1) == 2.5,
        "type replacement in clone changes only its own Number result");
    Require(state.SetAttribute(0, 7, CSOLWeaponAttributeValue{0.125}, 1), "default record can have an explicit trusted value");
    const auto fallback = state.CloneAttributes(UINT32_MAX, 99);
    Require(fallback && *fallback == 7, "missing clone source resolves key zero and still consumes normal copy constructors");
    Check(state.AttributeNumber(*fallback, 7) == 0.125 && !state.Attribute(*fallback, 1000) &&
        AttributeEquals(state, *fallback, 2000, std::uint64_t{99}) && !state.Attribute(0, 2000),
        "missing-source clone uses actual default kinds without synthesizing weaponID or changing key zero");
    Require(state.SetAttribute(0, 7, std::nullopt, 0), "delete default after fallback clone");
    Check(state.AttributeNumber(*fallback, 7) == 0.125 && !state.Attribute(0, 7),
        "fallback clone retains its own kind tree when the default changes later");
}

void AttributeInstanceCases(Engine& engine, CBasePlayer& firstOwner, CBasePlayer& nextOwner,
    const std::string& items, const std::string& defaults)
{
    Entity<Weapon> weapon(engine, 48), unrelated(engine, 49);
    weapon.Get().m_iId = 144; unrelated.Get().m_iId = 726;
    CCSOLScenarioWeaponState state; Load(state, items, defaults);
    Check(state.AttributeKey(weapon.Get()) == 0 && state.SetAttributeKey(weapon.Get(), 0) &&
        !state.HasAdditionalAttributeObjects(), "new SDK instance uses the real default key without creating an attribute object");
    const auto key = state.CreateAttributes(144, 10), otherKey = state.CreateAttributes(726, 20);
    Require(key && otherKey && state.SetAttributeKey(weapon.Get(), *key) && state.SetAttributeKey(unrelated.Get(), *otherKey),
        "explicit existing records bind to independent actual SDK weapon handles");
    Check(!state.SetAttributeKey(weapon.Get(), UINT32_MAX) && state.AttributeKey(weapon.Get()) == *key,
        "unknown explicit binding is rejected even though directory reads have a default fallback");
    Require(state.SetParts(weapon.Get(), {11, 12}) && state.SetItemIdentity(weapon.Get(), 428), "install parts and catalog identity alongside key");
    Check(state.AttributeKey(weapon.Get()) == *key, "parts and initial item selection do not erase attribute binding");
    Require(state.SetItemIdentity(weapon.Get(), 6011) && state.SetParts(weapon.Get(), {0, 13}), "change compatible item identity and part slots");
    Check(state.AttributeKey(weapon.Get()) == *key && state.Parts(weapon.Get()) == std::array<std::uint32_t,2>{0,13},
        "compatible item and part replacement keep the same attribute directory key");
    weapon.Get().m_pPlayer = &firstOwner;
    Check(state.AttributeKey(weapon.Get()) == *key, "first holder sees instance binding");
    weapon.Get().m_pPlayer = nullptr;
    Check(state.AttributeKey(weapon.Get()) == *key, "dropped instance retains attribute key without an owner");
    weapon.Get().m_pPlayer = &nextOwner;
    Check(state.AttributeKey(weapon.Get()) == *key && state.AttributeKey(unrelated.Get()) == *otherKey,
        "holder changes preserve instance identity and do not reassign unrelated weapon state");
    weapon.Get().pev->flags |= FL_KILLME;
    Check(state.AttributeKey(weapon.Get()) == 0 && !state.SetAttributeKey(weapon.Get(), 0),
        "pending removal rejects attribute binding and hides the retired instance key");
    weapon.Get().pev->flags &= ~FL_KILLME;
    const EHANDLE old(&weapon.Get()); weapon.Destroy(); weapon.Create(); weapon.Get().m_iId = 144;
    Check(!old.Get<CBaseEntity>() && state.AttributeKey(weapon.Get()) == 0 && state.AttributeKey(unrelated.Get()) == *otherKey,
        "actual entity destruction and edict serial reuse cannot inherit an old key");
    Require(state.SetAttributeKey(weapon.Get(), *otherKey), "new generation may explicitly bind an existing server-owned record");
    Check(state.AttributeKey(weapon.Get()) == *otherKey && state.AttributeKey(unrelated.Get()) == *otherKey &&
        AttributeEquals(state, *key, 1000, std::int32_t{144}) && state.HasAdditionalAttributeObjects(),
        "binding is independent of weaponID metadata and destroying one weapon does not destroy manager-owned attribute nodes");
    Require(state.SetAttributeKey(weapon.Get(), 0), "restore default binding explicitly");
    Check(state.AttributeKey(weapon.Get()) == 0 && state.HasAdditionalAttributeObjects(),
        "removing an instance binding does not pretend the retained attribute directory has no extra objects");
}

void AttributeRetirementCases(Engine& engine, const std::string& items, const std::string& defaults)
{
    Entity<Weapon> weapon(engine, 48); weapon.Get().m_iId = 144;
    for (const bool revoke : {false, true})
    {
        auto access = std::make_shared<CSOLScenarioEntityAccess>();
        CCSOLScenarioWeaponState state(access); Load(state, items, defaults);
        const auto key = state.CreateAttributes(144, 77);
        Require(key && state.SetAttributeKey(weapon.Get(), *key) &&
            state.SetAttribute(*key, 1, CSOLWeaponAttributeValue{1.5}, 0), "live retiring owner contains a bound modifier");
        if (revoke) access->Revoke(); else state.Invalidate();
        Check(!state.Current() && !state.HasAdditionalAttributeObjects() && state.AttributeKey(weapon.Get()) == 0 &&
            !state.Attribute(*key, 1) && !state.Attribute(0, 1) && state.AttributeNumber(*key, 1) == 0 &&
            !state.CreateAttributes(144, 1) && !state.CloneAttributes(*key, 2) &&
            !state.SetAttribute(*key, 1, CSOLWeaponAttributeValue{2.0}, 0) &&
            !state.SetAttribute(*key, 1, std::nullopt, 1) && !state.SetAttributeKey(weapon.Get(), 0),
            "invalidation or revoked map access blocks every attribute read, constructor, setter and instance binding");
        state.Invalidate(); state.Invalidate();
        Check(!state.Current() && !state.HasAdditionalAttributeObjects(), "repeated retirement remains closed");
        CCSOLScenarioWeaponState nextMap; Load(nextMap, items, defaults);
        Check(!nextMap.HasAdditionalAttributeObjects() && nextMap.AttributeKey(weapon.Get()) == 0 &&
            nextMap.CreateAttributes(144, 3) == std::optional<std::uint32_t>{3},
            "fresh owner restarts from its own default object and sequence without inheriting a retained old map key");
    }
}

void AttributeWeaponCases(Engine& engine, CBasePlayer& actor, CBasePlayer& playerSource,
    const std::string& items, const std::string& defaults)
{
    Entity<Weapon> primary(engine,48), secondary(engine,49), knife(engine,50), he(engine,51);
    Entity<AttributeSource> source(engine,52);
    primary.Get().m_iId=144; secondary.Get().m_iId=220; knife.Get().m_iId=29; he.Get().m_iId=4;
    const auto savedInventory = std::array{actor.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT], actor.m_rgpPlayerItems[PISTOL_SLOT],
        actor.m_rgpPlayerItems[KNIFE_SLOT], actor.m_rgpPlayerItems[GRENADE_SLOT]};
    auto* oldActive = actor.m_pActiveItem; auto* oldSourceActive = playerSource.m_pActiveItem;
    struct Restore
    {
        std::function<void()> callback; ~Restore() { callback(); }
    } restore{[&]
    {
        for(int slot=PRIMARY_WEAPON_SLOT;slot<=GRENADE_SLOT;++slot) actor.m_rgpPlayerItems[slot]=savedInventory[slot-PRIMARY_WEAPON_SLOT];
        actor.m_pActiveItem=oldActive; playerSource.m_pActiveItem=oldSourceActive;
        playerSource.pev->flags &= ~FL_SPECTATOR;
    }};
    actor.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&primary.Get(); actor.m_rgpPlayerItems[PISTOL_SLOT]=&secondary.Get();
    actor.m_rgpPlayerItems[KNIFE_SLOT]=&knife.Get(); actor.m_rgpPlayerItems[GRENADE_SLOT]=&he.Get();
    actor.m_pActiveItem=&primary.Get(); playerSource.m_pActiveItem=&secondary.Get();
    CCSOLScenarioWeaponState state; Load(state,items,defaults); EHANDLE result(&he.Get());
    Check(state.AttributeWeapon(actor,&playerSource,result) && result.Get<CBaseEntity>()==&secondary.Get(),
        "player inflictor uses that player's active gun rather than the attacker's active gun");
    playerSource.pev->flags |= FL_SPECTATOR;
    Check(state.AttributeWeapon(actor,&playerSource,result) && !result.Get(), "spectator source cannot borrow its active weapon");
    playerSource.pev->flags &= ~FL_SPECTATOR;
    Check(!state.AttributeWeapon(actor,nullptr,result) && !result.Get(), "unresolved world-wrapper source is rejected and clears prior output");
    Check(state.AttributeWeapon(actor,&he.Get(),result) && !result.Get(), "HE carried only in slot four is not found by original slots one through three query");
    Check(state.AttributeWeapon(actor,&secondary.Get(),result) && result.Get<CBaseEntity>()==&secondary.Get(),
        "weapon inflictor resolves a matching carried instance");
    auto probe=source.Get().probe; probe->ids={999,144};
    Check(state.AttributeWeapon(actor,&source.Get(),result) && result.Get<CBaseEntity>()==&primary.Get() && probe->sourceQueries==2,
        "projectile source ID is queried initially and freshly for each candidate instead of memoized");
    probe->sourceQueries=0; probe->ids={4};
    Check(state.AttributeWeapon(actor,&source.Get(),result) && !result.Get() && probe->sourceQueries==4,
        "three carried candidates each requery source ID but grenade slot stays excluded");
    probe->sourceQueries=0; probe->ids={0};
    Check(state.AttributeWeapon(actor,&source.Get(),result) && !result.Get() && probe->sourceQueries==1,
        "zero source ID short circuits before candidate traversal");
    probe->sourceQueries=0; probe->ids={220};
    primary.Get().onIsWeapon=[&]{actor.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&secondary.Get();};
    Check(state.AttributeWeapon(actor,&source.Get(),result) && result.Get<CBaseEntity>()==&secondary.Get() && probe->sourceQueries==2,
        "slot is reread after IsWeapon callback replacement");
    primary.Get().onIsWeapon={}; actor.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&primary.Get();
    probe->sourceQueries=0; probe->ids={144};
    probe->onSource=[&]{if(probe->sourceQueries==2)actor.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&secondary.Get();};
    Check(state.AttributeWeapon(actor,&source.Get(),result) && result.Get<CBaseEntity>()==&secondary.Get(),
        "matched captured candidate is followed by original final slot reread after source callback");
    probe->onSource={}; actor.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&primary.Get();
    bool ownerCurrent=false;
    const auto valid=[](const void* context){return *static_cast<const bool*>(context);};
    result=&he.Get();
    Check(!state.AttributeWeapon(actor,nullptr,result,valid,&ownerCurrent) && !result.Get(),
        "already retired owner rejects even a null source and clears output");
    const int queriesBefore=probe->playerQueries;result=&he.Get();
    Check(!state.AttributeWeapon(actor,&source.Get(),result,valid,&ownerCurrent) && !result.Get() &&
        probe->playerQueries==queriesBefore, "already retired owner rejects before invoking the live source virtual query");
    for(int cancellation=0;cancellation!=4;++cancellation)
    {
        ownerCurrent=true; probe->sourceQueries=0; probe->ids={144};
        if(cancellation==0)probe->onPlayer=[&]{ownerCurrent=false;};
        if(cancellation==1)probe->onSource=[&]{ownerCurrent=false;};
        if(cancellation==2)primary.Get().onIsWeapon=[&]{ownerCurrent=false;};
        if(cancellation==3)probe->onSource=[&]
        {
            if(probe->sourceQueries==2){primary.Destroy();primary.Create();primary.Get().m_iId=144;actor.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&primary.Get();}
        };
        result=&he.Get();
        Check(!state.AttributeWeapon(actor,&source.Get(),result,valid,&ownerCurrent) && !result.Get(),
            "virtual callbacks or real candidate serial reuse cancel without publishing a stale source handle");
        probe->onPlayer={};probe->onSource={};primary.Get().onIsWeapon={};
    }
    probe->onPlayer=[&]{source.Destroy();source.Create();}; result=&he.Get();
    Check(!state.AttributeWeapon(actor,&source.Get(),result) && !result.Get(),
        "source destruction and same-slot recreation during IsPlayer invalidates the original source handle");
    probe->onPlayer={};
    state.Invalidate();result=&he.Get();
    Check(!state.AttributeWeapon(actor,&primary.Get(),result) && !result.Get(), "invalidated directory rejects source lookup and clears prior output");
}
}

void WeaponStateCases()
{
    Engine engine;
    auto& player = engine.NewPlayer();
    auto& other = engine.NewPlayer();
    Entity<Weapon> primary(engine, 33), secondary(engine, 34), knife(engine, 35),
        grenade1(engine, 36), grenade2(engine, 37);
    Entity<Projectile> projectile(engine, 38);
    primary.Get().m_iId = 726;
    secondary.Get().m_iId = 220;
    knife.Get().m_iId = 29;
    grenade1.Get().m_iId = 4;
    grenade2.Get().m_iId = 9;
    projectile.Get().launchId = 220;
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] = &primary.Get();
    player.m_rgpPlayerItems[PISTOL_SLOT] = &secondary.Get();
    player.m_rgpPlayerItems[KNIFE_SLOT] = &knife.Get();
    player.m_rgpPlayerItems[GRENADE_SLOT] = &grenade1.Get();
    grenade1.Get().m_pNext = &grenade2.Get();
    player.m_pActiveItem = &secondary.Get();
    Check(CSOL_DamageWeaponId(&player, &player) == 220, "direct player damage uses active weapon");
    Check(CSOL_DamageWeaponId(&primary.Get(), &player) == 726, "weapon inflictor uses its own ID");
    Check(CSOL_DamageWeaponId(&projectile.Get(), &player) == 220, "projectile exposes saved launch ID");
    player.m_pActiveItem = &primary.Get();
    Check(CSOL_DamageWeaponId(&player, &player) == 726 &&
        CSOL_DamageWeaponId(&projectile.Get(), &player) == 220,
        "switching active weapon changes direct source but not projectile attribution");
    Check(CSOL_DamageWeaponId(nullptr, &player) == 0, "null inflictor does not borrow active ID");
    player.m_pActiveItem = nullptr;
    Check(CSOL_DamageWeaponId(&player, &player) == 0, "unarmed player damage has no source ID");
    player.m_pActiveItem = &primary.Get();

    const auto actualItems = difficulty_test::Read(resourceRoot / "item.csv");
    const auto actualDefaults = difficulty_test::Read(resourceRoot / "defaultitems.csv");
    AttributeDirectoryCases(actualItems, actualDefaults);
    AttributeCloneCases(actualItems, actualDefaults);
    AttributeInstanceCases(engine, player, other, actualItems, actualDefaults);
    AttributeRetirementCases(engine, actualItems, actualDefaults);
    AttributeWeaponCases(engine, player, other, actualItems, actualDefaults);
    DefaultResourceCases(actualItems, actualDefaults);
    CCSOLScenarioWeaponState state;
    Load(state, actualItems, actualDefaults);
    Check(state.Parts(primary.Get()) == std::array<std::uint32_t, 2>{0, 0}, "new weapon starts with both part slots zero");
    Check(state.DefaultSelection() == std::array<std::int32_t, 2>{161, 31} && state.ValidSelection({161, 31}),
        "actual default resource resolves native knife161 and HE31 independently of weapon IDs29 and4");
    Check(state.SetItemIdentity(primary.Get(), 9638) && state.SetItemIdentity(secondary.Get(), 581) &&
        state.SetItemIdentity(knife.Get(), 161) && state.SetItemIdentity(grenade1.Get(), 31),
        "actual catalog identity matches each real weapon implementation");
    for (const int item : {428, 581, 9637, 6011, 43, 317, 122, 745})
        Check(!state.SetItemIdentity(primary.Get(), item),
            "unrelated weapons characters skin attachments and paint cannot become beamgunle identity");
    Check(!state.SetItemIdentity(primary.Get(), 0), "two metadata rows are not a synthetic item zero");
    Check(!state.SetItemIdentity(primary.Get(), 2147483647), "unknown item identity is rejected");
    for (const auto selection : {std::array<std::int32_t, 2>{31, 161}, {164, 31}, {161, 212},
        {161, 428}, {161, 9638}, {317, 31}, {122, 31}, {745, 31}, {161, 0}})
        Check(!state.ValidSelection(selection) && !state.SelectItems(player, 7, selection),
            "selection rejects unsupported variants wrong slots characters attachments and other Category6 weapons");
    Unresolved(state, player, 29);
    Unresolved(state, player, 4);
    Check(!state.SelectItems(player, 0, {161, 31}), "zero connection generation cannot install player selection");
    Require(state.SelectItems(player, 7, state.DefaultSelection()), "server-owned player selection is installed");
    Item(state, player, 29, 161);
    Item(state, player, 4, 31);
    Item(state, player, 726, 9638);
    Item(state, player, 725, 9637);
    Item(state, player, 220, 581);
    Item(state, player, 28, 21);
    Item(state, player, 0, 0);
    Item(state, player, 999999, 0);

    // Removing inventory preserves the local default selection. This models
    // the condition after the last HE has been thrown; actual factory/gate
    // checks are prepared separately and still require complete SDK execution.
    player.m_rgpPlayerItems[KNIFE_SLOT] = nullptr;
    player.m_rgpPlayerItems[GRENADE_SLOT] = nullptr;
    Item(state, player, 29, 161);
    Item(state, player, 4, 31);
    player.m_rgpPlayerItems[KNIFE_SLOT] = &knife.Get();
    player.m_rgpPlayerItems[GRENADE_SLOT] = &grenade1.Get();
    state.RemovePlayer(EHANDLE(&player), 6);
    Item(state, player, 4, 31);
    Require(state.SelectItems(player, 8, {161, 31}), "newer generation replaces selection for a retained player");
    Check(!state.SelectItems(player, 7, {161, 31}), "older generation cannot overwrite current selection");
    state.RemovePlayer(EHANDLE(&player), 7);
    Item(state, player, 29, 161);
    player.has_disconnected = true;
    Unresolved(state, player, 4);
    Check(!state.SelectItems(player, 9, {161, 31}), "disconnected player cannot install a selection");
    player.has_disconnected = false;
    player.pev->flags |= FL_KILLME;
    Unresolved(state, player, 29);
    player.pev->flags &= ~FL_KILLME;
    state.RemovePlayer(EHANDLE(&player), 8);
    Unresolved(state, player, 29);
    Require(state.SelectItems(player, 9, {161, 31}), "selection can be installed after exact generation removal");

    auto& recycledPlayer = engine.NewPlayer();
    const int recycledSlot = Engine::Index(recycledPlayer.edict());
    const EHANDLE retiredPlayer(&recycledPlayer);
    Require(state.SelectItems(recycledPlayer, 20, {161, 31}), "old real SDK player receives isolated selection");
    engine.Destroy(recycledPlayer);
    Require(!retiredPlayer.Get<CBaseEntity>(), "actual SDK player release increments the edict serial");
    // Reuse the same physical player slot through the fixture's real SDK
    // constructor, without declaring another player class or copying memory.
    engine.nextEntity = recycledSlot;
    auto& replacementPlayer = engine.NewPlayer();
    Unresolved(state, replacementPlayer, 4);
    Require(state.SelectItems(replacementPlayer, 21, {161, 31}), "reused slot gets its own selection generation");
    state.RemovePlayer(retiredPlayer, 20);
    Item(state, replacementPlayer, 4, 31);
    Item(state, player, 29, 161);
    state.RemovePlayer(EHANDLE(&replacementPlayer), 21);
    Unresolved(state, replacementPlayer, 29);

    Check(!state.RecordHeGrenade(projectile.Get()) && !state.RecordHeGrenade(grenade1.Get()) &&
        state.ProjectileSource(projectile.Get()) == 0,
        "a custom projectile or HE inventory weapon cannot masquerade as the native CGrenade projectile");
    Collect(state, player, &player, {});

    Require(state.SetParts(primary.Get(), {11, 12}) && state.SetParts(secondary.Get(), {11, 11}) &&
        state.SetParts(knife.Get(), {13, 0}) && state.SetParts(grenade1.Get(), {14, 0}) &&
        state.SetParts(grenade2.Get(), {15, 0}), "independent actual weapon instances receive part slots");
    std::vector<CSOLWeaponPartEffect> rows{
        Effect(11, 101, false), Effect(11, 102, false), Effect(11, 201, true),
        Effect(12, 103, false), Effect(12, 202, true), Effect(13, 203, true),
        Effect(14, 204, true), Effect(15, 205, true),
        Effect(11, 401, false, 4), Effect(11, 402, true, 4)};
    auto highMode = Effect(11, 501, false);
    highMode.modes = {0, 1u << 15};
    rows.push_back(highMode);
    Require(state.ReplacePartEffects(rows), "owned part catalog installation");
    rows[0].tier = -999;
    rows[0].parameters[0] = -999;
    Collect(state, player, &player, {101, 102, 103, 201, 202, 201, 201, 203, 204, 205});
    Collect(state, player, &primary.Get(), {101, 102, 103, 201, 202, 201, 201, 203, 204, 205});
    Collect(state, player, &projectile.Get(), {101, 102, 101, 102, 201, 202, 201, 201, 203, 204, 205});
    Collect(state, player, &player, {401}, 15, 4);
    Collect(state, player, &player, {501}, 47);
    Collect(state, player, &player, {}, 14);
    Collect(state, player, &player, {}, 15, 17);
    projectile.Get().launchId = 999999;
    Collect(state, player, &projectile.Get(), {201, 202, 201, 201, 203, 204, 205});
    Collect(state, player, nullptr, {201, 202, 201, 201, 203, 204, 205});
    projectile.Get().launchId = 220;
    for (const auto blocked : {std::array<bool, 2>{true, false}, {false, true}, {true, true}})
    {
        std::vector<CSOLWeaponPartEffect> output{Effect(99, 99, false)};
        Check(state.CollectParts(player, &player, 15, 1, blocked[0], blocked[1], output) && output.empty(),
            "either original qualification byte clears both direct and passive output");
    }
    {
        std::vector<CSOLWeaponPartEffect> output{Effect(99, 99, false)};
        Check(!state.CollectParts(player, &player, -1, 1, false, false, output) && output.empty(),
            "invalid mode clears caller output and fails");
    }
    auto bad = Effect(11, 999, false);
    bad.parameters[0] = std::numeric_limits<double>::quiet_NaN();
    Check(!state.ReplacePartEffects({bad}), "nonfinite effects cannot replace active catalog");
    bad = Effect(11, 999, false, 18);
    Check(!state.ReplacePartEffects({bad}), "unknown kind cannot replace active catalog");
    bad = Effect(11, 999, false);
    bad.modes.resize(2049);
    Check(!state.ReplacePartEffects({bad}), "oversized mode set cannot replace active catalog");
    Collect(state, player, &player, {101, 102, 103, 201, 202, 201, 201, 203, 204, 205});

    Check(state.MultiplierUpdates().empty(), "runtime item multiplier table begins with no updates");
    Require(state.SetItemMultiplier(9638, {7, 0}) && state.SetItemMultiplier(9638, {9, 2}) &&
        state.SetItemMultiplier(0, {0, 0}), "zero and repeated item multiplier writes are accepted");
    Check(std::vector<std::int32_t>(state.MultiplierUpdates().begin(), state.MultiplierUpdates().end()) ==
        std::vector<std::int32_t>{9638, 9638, 0}, "update sequence preserves repeated item keys");
    Check(!state.SetItemMultiplier(1, {0, std::numeric_limits<float>::infinity()}) &&
        state.MultiplierUpdates().size() == 3, "nonfinite multiplier rejects without appending update");
    state.ClearItemMultipliers();
    Check(state.MultiplierUpdates().empty(), "clear removes pending multiplier updates");

    // Change owner/storage, retaining the same SDK instance and EHANDLE serial.
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] = nullptr;
    player.m_pActiveItem = &secondary.Get();
    primary.Get().m_pPlayer = nullptr;
    Check(state.Parts(primary.Get()) == std::array<std::uint32_t, 2>{11, 12}, "dropped instance retains both installed parts");
    projectile.Get().launchId = 726;
    Collect(state, player, &projectile.Get(), {201, 201, 203, 204, 205});
    primary.Get().m_pPlayer = &other;
    other.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] = &primary.Get();
    other.m_pActiveItem = &primary.Get();
    Collect(state, other, &other, {101, 102, 103, 201, 202});
    Check(state.Parts(primary.Get()) == std::array<std::uint32_t, 2>{11, 12}, "new owner receives same instance state");

    other.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] = nullptr;
    other.m_pActiveItem = nullptr;
    const EHANDLE oldPrimary(&primary.Get());
    primary.Destroy();
    primary.Create();
    primary.Get().m_iId = 144;
    Check(!oldPrimary.Get<CBaseEntity>() && state.Parts(primary.Get()) == std::array<std::uint32_t, 2>{0, 0},
        "recycled real edict serial cannot inherit parts or old handle");
    Require(state.SetParts(primary.Get(), {16, 0}), "new generation gets independent part state");
    Require(state.SetItemIdentity(primary.Get(), 6011), "same implementation accepts real duplicate catalog item6011");
    other.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] = &primary.Get();
    Item(state, other, 144, 428);
    other.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] = nullptr;
    Check(state.Parts(secondary.Get()) == std::array<std::uint32_t, 2>{11, 11}, "pruning dead instance preserves unrelated weapon state");
    secondary.Get().pev->flags |= FL_KILLME;
    Check(CSOL_DamageWeaponId(&secondary.Get(), &player) == 0 &&
        !state.SetParts(secondary.Get(), {1, 2}), "pending removal cannot source damage or accept parts");
    Collect(state, player, &player, {203, 204, 205});
    secondary.Get().pev->flags &= ~FL_KILLME;

    state.Invalidate();
    std::vector<CSOLWeaponPartEffect> cancelled{Effect(99, 99, false)};
    Check(!state.Current() && !state.SetParts(primary.Get(), {11, 12}) &&
        !state.SetItemIdentity(primary.Get(), 428) && !state.SetItemMultiplier(428, {1, 2}) &&
        !state.ReplacePartEffects({Effect(11, 101, false)}) &&
        !state.CollectParts(player, &player, 15, 1, false, false, cancelled) && cancelled.empty(),
        "invalidated retained map owner rejects every mutation and collector");
    Check(state.DefaultSelection() == std::array<std::int32_t, 2>{} && !state.ValidSelection({161, 31}) &&
        !state.SelectItems(player, 10, {161, 31}) && !state.RecordHeGrenade(projectile.Get()) &&
        state.ProjectileSource(projectile.Get()) == 0, "invalidated map clears selection and denies projectile registration");
    Check(state.Parts(primary.Get()) == std::array<std::uint32_t, 2>{0, 0} && state.MultiplierUpdates().empty(),
        "invalidated map clears weapon state and updates");
    for (const auto* cancelledPath : {ItemPath, DefaultItemsPath})
    for (const bool onFree : {false, true})
    {
        difficulty_test::Files files;
        files.contents[ItemPath] = actualItems;
        files.contents[DefaultItemsPath] = actualDefaults;
        CCSOLScenarioWeaponState interrupted;
        const auto cancel = [&](std::string_view path) { if (path == cancelledPath) interrupted.Invalidate(); };
        if (onFree) files.onFree = cancel; else files.onLoad = cancel;
        ResourceError error;
        Check(!interrupted.Load(files.Api(), error) && !interrupted.Current() && files.buffers.empty(),
            "load/free callback invalidation releases exact resource without publishing catalog");
    }
    for (const auto* cancelledPath : {ItemPath, DefaultItemsPath})
    for (const bool onFree : {false, true})
    {
        difficulty_test::Files files;
        files.contents[ItemPath] = actualItems;
        files.contents[DefaultItemsPath] = actualDefaults;
        auto access = std::make_shared<CSOLScenarioEntityAccess>();
        CCSOLScenarioWeaponState interrupted(access);
        const auto revoke = [&](std::string_view path) { if (path == cancelledPath) access->Revoke(); };
        if (onFree) files.onFree = revoke; else files.onLoad = revoke;
        ResourceError error;
        Check(!interrupted.Load(files.Api(), error) && !interrupted.Current() && files.buffers.empty() &&
            interrupted.DefaultSelection() == std::array<std::int32_t, 2>{},
            "resource callback access revocation cancels publication and still releases each acquired buffer");
    }
    {
        auto access = std::make_shared<CSOLScenarioEntityAccess>();
        CCSOLScenarioWeaponState revoked(access);
        Load(revoked, actualItems, actualDefaults);
        Require(revoked.SelectItems(player, 30, {161, 31}), "access-controlled state has a live owned selection");
        access->Revoke();
        std::int32_t item = -1;
        ConfigError error;
        Check(!revoked.Current() && !revoked.Item(player, 29, item, error) && item == 0 &&
            !revoked.SelectItems(player, 31, {161, 31}) && !revoked.SetItemIdentity(primary.Get(), 428) &&
            !revoked.RecordHeGrenade(projectile.Get()) && revoked.ProjectileSource(projectile.Get()) == 0,
            "retained state cannot read selection or mutate projectile identity after map access is revoked");
    }
    player.m_pActiveItem = nullptr;
    for (auto& item : player.m_rgpPlayerItems) item = nullptr;
}
}
