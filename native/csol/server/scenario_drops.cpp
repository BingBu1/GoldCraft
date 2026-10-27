// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_drops.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr const char* BoxModel = "models/dropitem.mdl";
constexpr const char* EffectModel = "models/ef_special_box01.mdl";
constexpr const char* PickupSound = "zombi/Get_Box.wav";

std::int16_t SignedShort(int value)
{
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

bool CanReceive(CBasePlayer* player)
{
    // Original player vBC checks bit26 (spectator), not health/deadflag.
    return player && !(player->pev->flags & FL_SPECTATOR);
}
}

// EXPORT member-function definitions must have external linkage in the SDK.
class CCSOLScenarioDropItem final : public CBaseEntity
{
public:
    void Precache() override { CCSOLScenarioDrops::Precache(); }
    void Spawn() override;
    void OnDestroy() override;
    void EXPORT ItemThink();
    void EXPORT ItemTouch(CBaseEntity* other);
    bool Configure(std::shared_ptr<CCSOLScenarioDrops> owner, const EHANDLE& player,
        std::uint64_t generation, const csol::scenario::DropItemRule& rule);
    void Removing();

private:
    std::shared_ptr<CCSOLScenarioDrops> m_owner;
    EHANDLE m_player;
    std::uint64_t m_generation{};
    std::int32_t m_reward{};
    float m_deadline{};
    bool m_claimed{}, m_removing{}, m_destroying{};
};

LINK_ENTITY_TO_CLASS(zbsitem, CCSOLScenarioDropItem, CCSEntity)

void CCSOLScenarioDrops::Precache()
{
    PRECACHE_MODEL(const_cast<char*>(BoxModel));
    PRECACHE_MODEL(const_cast<char*>(EffectModel));
    PRECACHE_SOUND(const_cast<char*>(PickupSound));
}

bool CCSOLScenarioDrops::Active() const
{
    return m_active && m_session && m_players && m_players->Active() &&
        m_session->State() == CSOLScenarioSessionState::Active;
}

bool CCSOLScenarioDrops::Activate()
{
    if (m_active || !m_session || !m_players || !m_players->Active() ||
        m_session->State() != CSOLScenarioSessionState::Active) return false;
    m_active = true;
    return true;
}

CBasePlayer* CCSOLScenarioDrops::Recipient(const EHANDLE& player, std::uint64_t generation) const
{
    if (!Active() || !generation) return nullptr;
    const auto state = m_players->Find(player);
    if (!state || state->generation != generation) return nullptr;
    auto* entity = dynamic_cast<CBasePlayer*>(player.Get<CBaseEntity>());
    return entity;
}

void CCSOLScenarioDrops::RecordReward(const EHANDLE& player, std::uint64_t generation,
    std::int32_t reward)
{
    // Removal submits to the saved identity even after disconnect. Keep the
    // opaque generation in the request; never resolve it to a replacement
    // player, and never claim the external reward was delivered.
    if (!m_active || !generation) return;
    m_rewards.push_back({++m_sequence, generation, player, reward});
}

bool CCSOLScenarioDrops::Acknowledge(std::uint64_t sequence, std::uint64_t playerGeneration,
    CSOLScenarioDropReward::Status status)
{
    if (status == CSOLScenarioDropReward::Status::AwaitingHandler) return false;
    const auto found = std::find_if(m_rewards.begin(), m_rewards.end(), [&](const auto& request)
    { return request.sequence == sequence && request.playerGeneration == playerGeneration; });
    if (found == m_rewards.end() || found->status != CSOLScenarioDropReward::Status::AwaitingHandler)
        return false;
    found->status = status;
    return true;
}

void CCSOLScenarioDrops::RemoveItems()
{
    auto keepAlive = shared_from_this();
    if (m_removing) return;
    m_removing = true;
    // Detach the container before engine callbacks; raw-free can reenter.
    auto items = std::move(m_items);
    m_items.clear();
    for (const auto& handle : items)
    {
        if (!Active()) break;
        if (auto* item = dynamic_cast<CCSOLScenarioDropItem*>(handle.Get<CBaseEntity>()))
            UTIL_Remove(item);
    }
    m_removing = false;
}

void CCSOLScenarioDrops::Deactivate()
{
    auto keepAlive = shared_from_this();
    if (!m_active) return;
    RemoveItems();
    m_active = false;
}

void CCSOLScenarioDropItem::Spawn()
{
    // Both models and the sound were precached by the session during worldspawn.
    // A death can create this entity long after the engine closes precaching.
    pev->movetype = MOVETYPE_TOSS;
    pev->solid = SOLID_TRIGGER;
    SetThink(nullptr);
    SetTouch(nullptr);
    SET_MODEL(edict(), BoxModel);
}

bool CCSOLScenarioDropItem::Configure(std::shared_ptr<CCSOLScenarioDrops> owner,
    const EHANDLE& player, std::uint64_t generation, const csol::scenario::DropItemRule& rule)
{
    if (m_owner || m_removing || m_destroying || !owner || !owner->Active() || rule.dropEffect != 0)
        return false;
    m_owner = owner;
    m_player = player;
    m_generation = generation;
    m_reward = rule.rewardId;
    m_deadline = gpGlobals->time + SignedShort(rule.removeSeconds);
    const EHANDLE self(this);
    UTIL_SetSize(pev, Vector(-32, -32, 0), Vector(32, 32, 32));
    if (!owner->Active() || self.Get<CBaseEntity>() != this || m_removing) return false;
    SetThink(&CCSOLScenarioDropItem::ItemThink);
    SetTouch(&CCSOLScenarioDropItem::ItemTouch);
    pev->nextthink = gpGlobals->time;
    return true;
}

void CCSOLScenarioDropItem::ItemTouch(CBaseEntity* other)
{
    if (!m_owner || m_claimed || m_removing || m_destroying) return;
    auto owner = m_owner;
    const auto player = m_player;
    const auto generation = m_generation;
    const auto reward = m_reward;
    auto* recipient = owner->Recipient(player, generation);
    if (!CanReceive(recipient) || recipient != other) return;
    const EHANDLE self(this);
    EMIT_SOUND_DYN(recipient->edict(), CHAN_ITEM, PickupSound, 1.0f, 0.8f, 0, 100);
    if (!owner->Active() || self.Get<CBaseEntity>() != this || m_claimed || m_removing ||
        owner->Recipient(player, generation) != recipient) return;
    m_claimed = true;
    SetTouch(nullptr);
    UTIL_Remove(this);
    // No access to this after removal: the engine/plugin may free it now.
    owner->RecordReward(player, generation, reward);
}

void CCSOLScenarioDropItem::ItemThink()
{
    if (!m_owner || m_claimed || m_removing || m_destroying) return;
    auto owner = m_owner;
    pev->nextthink = gpGlobals->time + 1.0f;
    if (m_deadline > gpGlobals->time) return;
    auto* recipient = owner->Recipient(m_player, m_generation);
    if (CanReceive(recipient) && !(recipient->pev->flags & FL_DORMANT)) ItemTouch(recipient);
    else
    {
        // Invalidate the saved identity before removal so a replacement in
        // the same player slot cannot inherit the timed-out reward.
        m_generation = 0;
        UTIL_Remove(this);
    }
}

void CCSOLScenarioDropItem::Removing()
{
    if (m_removing) return;
    m_removing = true;
    SetTouch(nullptr);
    SetThink(nullptr);
    auto owner = m_owner;
    if (owner && !m_claimed)
    {
        m_claimed = true;
        owner->RecordReward(m_player, m_generation, m_reward);
    }
}

void CCSOLScenarioDropItem::OnDestroy()
{
    if (m_destroying) return;
    m_destroying = true;
    Removing();
    // GoldSrc frees pvPrivateData directly; it does not run this C++ destructor.
    auto owner = std::move(m_owner);
    CBaseEntity::OnDestroy();
    owner.reset();
}

bool CSOL_ScenarioItemRemoving(CBaseEntity* entity)
{
    auto* item = dynamic_cast<CCSOLScenarioDropItem*>(entity);
    if (!item) return true;
    const EHANDLE handle(item);
    item->Removing();
    return handle.Get<CBaseEntity>() == item && !(item->pev->flags & FL_KILLME);
}

bool CCSOLScenarioDrops::Position(CSOLMonsterDeathCall& call, const Vector& source,
    const EHANDLE& recipient, std::size_t created, Vector& result)
{
    #pragma clang fp contract(off)
    const auto navigation = m_session->ScenarioNavigation();
    if (!navigation || !navigation->World().Ready()) return false;
    const auto generation = navigation->World().Generation();
    const auto valid = [&] { return Active() && call.Current() &&
        navigation->World().Ready() && navigation->World().Generation() == generation; };
    const auto nearby = [&](const Vector& anchor, Vector& candidate)
    {
        candidate = anchor;
        if (created)
        {
            constexpr int directions[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
            const auto index = created - 1;
            const float distance = (static_cast<float>(index / 8) * 0.35f + 0.7f) * 39.3699989f;
            candidate.x += directions[index % 8][0] * distance;
            candidate.y += directions[index % 8][1] * distance;
        }
        if (navigation->World().AreaAt(candidate, 120.0f)) return valid();
        for (int attempt = 0; attempt < 20 && valid(); ++attempt)
        {
            const int x = std::rand() % 4 + 1;
            const int signX = (std::rand() & 1) ? -1 : 1;
            const int y = std::rand() % 4 + 1;
            const int signY = (std::rand() & 1) ? -1 : 1;
            candidate = anchor + Vector(x * signX * 19.6849995f, y * signY * 19.6849995f, 0);
            if (navigation->World().AreaAt(candidate, 120.0f)) return valid();
        }
        return false;
    };
    auto* player = dynamic_cast<CBasePlayer*>(recipient.Get<CBaseEntity>());
    if (!player || (player->pev->origin - source).Length() <= 2362.199951171875f)
        if (nearby(source, result)) return true;
    if (!valid()) return false;
    player = dynamic_cast<CBasePlayer*>(recipient.Get<CBaseEntity>());
    if (!player) { result = source; return true; }
    Vector forward, right;
    AngleVectors(player->pev->angles, forward, right, nullptr);
    const float side = (std::rand() & 1) ? -196.84999084472656f : 196.84999084472656f;
    const Vector anchor = player->pev->origin + forward * 196.84999084472656f +
        right * side + Vector(0, 0, 16);
    if (!nearby(anchor, result)) result = anchor;
    return valid();
}

bool CCSOLScenarioDrops::SpawnItem(CSOLMonsterDeathCall& call,
    const csol::scenario::DropItemRule& rule, const EHANDLE& recipient, std::size_t& created)
{
    auto* monster = call.Current();
    if (!Active() || !monster) return false;
    const auto state = m_players->Find(recipient);
    if (!state) return false;
    const auto generation = state->generation;
    Vector position;
    if (!Position(call, monster->pev->origin, recipient, created, position)) return false;
    // SDK Create returns its original private pointer even when DispatchSpawn
    // frees it. Capture the handle before that callback and re-resolve it.
    auto* edict = CREATE_NAMED_ENTITY(MAKE_STRING("zbsitem"));
    if (!Active() || !call.Current()) return false;
    if (FNullEnt(edict)) return true; // Failed creation still consumes this recipient.
    const EHANDLE handle(edict);
    auto* item = dynamic_cast<CCSOLScenarioDropItem*>(handle.Get<CBaseEntity>());
    if (!item)
    {
        if (auto* entity = handle.Get<CBaseEntity>()) UTIL_Remove(entity);
        else REMOVE_ENTITY(edict);
        return Active() && call.Current();
    }
    item->pev->owner = nullptr;
    item->pev->origin = position;
    item->pev->angles = g_vecZero;
    const int spawnResult = DispatchSpawn(edict);
    if (!Active() || !call.Current()) return false;
    item = dynamic_cast<CCSOLScenarioDropItem*>(handle.Get<CBaseEntity>());
    if (!item || item->pev->flags & FL_KILLME) return true;
    if (spawnResult < 0)
    {
        UTIL_Remove(item);
        return Active() && call.Current();
    }
    if (!Active() || m_removing || !call.Current() ||
        !item->Configure(shared_from_this(), recipient, generation, rule))
    {
        if (Active() && call.Current() && handle.Get<CBaseEntity>() == item) UTIL_Remove(item);
        return false;
    }
    // Compact handles opportunistically; an indefinitely running map must
    // not retain one dead handle for every box it has ever spawned.
    std::erase_if(m_items, [](const EHANDLE& entry)
    { auto* value = entry.Get<CBaseEntity>(); return !value || (value->pev->flags & FL_KILLME); });
    m_items.push_back(handle);
    TraceResult trace{};
    UTIL_TraceLine(item->pev->origin, item->pev->origin - Vector(0, 0, 8192),
        ignore_monsters, item->edict(), &trace);
    if (!Active() || !call.Current() || handle.Get<CBaseEntity>() != item || item->pev->flags & FL_KILLME)
        return false;
    if (trace.flFraction != 1.0f)
    {
        UTIL_SetOrigin(item->pev, trace.vecEndPos);
        if (!Active() || !call.Current() || handle.Get<CBaseEntity>() != item || item->pev->flags & FL_KILLME)
            return false;
        item->pev->flags |= FL_ONGROUND;
        item->pev->groundentity = trace.pHit;
    }
    item->pev->groundentity = nullptr;
    ++created;
    return true;
}

bool CCSOLScenarioDrops::MonsterKilled(CSOLMonsterDeathCall& call, int aliveCt, int totalCt,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    auto keepAlive = shared_from_this();
    auto* monster = call.Current();
    if (!Active() || m_removing || !monster || !monster->Type()) return false;
    // Local mode15 is hosted by a dedicated server. This is its explicit
    // policy, not an assumed CSO engine-table ABI offset.
    const bool dedicated = IS_DEDICATED_SERVER() != 0;
    if (!Active() || !(monster = call.Current())) return false;
    if (!dedicated) return true;
    const auto ids = monster->Type()->dropItems;
    std::size_t created{};
    for (const auto id : ids)
    {
        const auto found = m_session->DropRules().Find(SignedShort(id));
        // Engine callbacks may unload the old table while this call unwinds.
        const std::vector<csol::scenario::DropItemRule> rows(found.begin(), found.end());
        for (const auto& rule : rows)
        {
            if (!Active() || m_removing || !call.Current()) return false;
            const int random = std::rand() % 10000;
            if (!(static_cast<float>(random) < rule.probability * 100.0f) || aliveCt == 0) continue;
            if (aliveCt < 0 || aliveCt > 32 || totalCt < 0 || totalCt > 32 ||
                !gpGlobals || gpGlobals->maxClients < 1 || gpGlobals->maxClients > 32)
            { error.message = "scenario drop recipient count is invalid"; return false; }
            if (rule.dropEffect != 0)
            { error.message = "scenario drop model effect has no verified client consumer"; return false; }
            int start = 1, quota = 1, skip = 0;
            if (rule.ownerType == 0)
            {
                if (auto* attacker = call.Attacker()) start = attacker->entindex();
                if (!Active() || !call.Current()) return false;
                if (start < 1 || start > gpGlobals->maxClients) start = 1;
            }
            else if (rule.ownerType == 1) skip = std::rand() % aliveCt;
            else quota = totalCt;
            if (quota <= 0)
            { error.message = "scenario drop total CT snapshot is empty"; return false; }
            for (int scanned = 0; scanned < gpGlobals->maxClients; ++scanned)
            {
                auto* edict = INDEXENT((start - 1 + scanned) % gpGlobals->maxClients + 1);
                if (!Active() || !call.Current()) return false;
                if (!edict || edict->free || !edict->pvPrivateData) continue;
                auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(edict));
                // Preserve the original selection: spawn count only, no new
                // alive/team filter. Receipt validates identity separately.
                if (!player || SignedShort(player->m_iNumSpawns) <= 0) continue;
                if (skip > 0) { --skip; continue; }
                const EHANDLE recipient(player);
                for (int count = 0; count < rule.count; ++count)
                    if (!SpawnItem(call, rule, recipient, created)) return false;
                if (--quota == 0) break;
            }
        }
    }
    return Active() && call.Current();
}
