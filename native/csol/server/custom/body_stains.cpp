// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "body_stains.h"
#include "../../scenario/custom/body_stains.hpp"
#include "../../scenario/custom/monster_stains.hpp"
#include "../../scenario/monsters/monster_impact.hpp"
#include "../scenario/core/scenario_messages.h"
#include "../scenario/monsters/scenario_monster_pool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

extern float (*g_pBoneTransform)[MAXSTUDIOBONES][3][4];

namespace
{
namespace custom = csol::scenario::custom;

struct StainedWeapon
{
    const edict_t* entity{};
    int serial{};
    string_t viewName{}, worldName{};

    bool Matches(const CBasePlayer& owner) const noexcept
    {
        if (!entity || !owner.m_pActiveItem || owner.pev->viewmodel != viewName ||
            owner.pev->weaponmodel != worldName) return false;
        const auto* active = owner.m_pActiveItem->edict();
        return active == entity && active->serialnumber == serial && !(active->v.flags & FL_KILLME);
    }
};

struct Stain
{
    int serial{};
    unsigned body{}, weapon{};
};

std::array<Stain, custom::BodyStainSlotCount> stains;
float splashRadiusSquared{};
bool enabled{}, hasWeaponStains{};
unsigned deltaTables{}, generation{}, mapGeneration{};
std::uint32_t randomState{};

struct AttackTrace
{
    const CSOLBodyStainShot& shot;
    const CBaseEntity* target;
    const TraceResult* trace;
    bool recorded{}, melee{};
};
AttackTrace* attackTrace{};
struct PendingSplash
{
    const CSOLBodyStainShot* shot{};
    const CBaseEntity* victim{};
    Vector hit{};
};
PendingSplash pendingSplash;

struct MonsterMarks
{
    unsigned id{}, count{};
    int model{};
    std::array<custom::MonsterStain, custom::MonsterStainMax> marks;
};

struct StainRecipient
{
    int serial{};
    unsigned head{}, size{};
    float nextSend{};
    std::array<std::uint16_t, custom::BodyStainSlotCount> pending;
    // Bit 31 is a queue flag. The remaining bits are the last sent id/count.
    std::array<std::uint32_t, custom::BodyStainSlotCount> sent;
};

constexpr std::uint32_t queued = 1u << 31;
std::array<MonsterMarks, custom::BodyStainSlotCount> monsterMarks{};
std::array<StainRecipient, 32> recipients{};
std::uint32_t pendingRecipients{};
unsigned nextMonsterId{};

void QueueMonsterMarks(unsigned index, std::uint32_t field, const edict_t& host) noexcept
{
    if (host.free || !(host.v.flags & FL_CLIENT) || (host.v.flags & (FL_FAKECLIENT | FL_PROXY))) return;
    const auto slot = static_cast<unsigned>(ENTINDEX(&host)) - 1;
    if (slot >= recipients.size()) return;
    auto& channel = recipients[slot];
    if (channel.serial != host.serialnumber)
    {
        channel.head = channel.size = 0;
        channel.nextSend = 0;
        channel.sent.fill(0);
        channel.serial = host.serialnumber;
    }
    auto& sent = channel.sent[index];
    if (sent == field || (sent & queued)) return;
    sent |= queued;
    channel.pending[(channel.head + channel.size) % channel.pending.size()] = static_cast<std::uint16_t>(index);
    ++channel.size;
    pendingRecipients |= 1u << slot;
}

bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

Vector InverseRotate(const Vector& v, const float (&matrix)[3][4]) noexcept
{
    return {v.x * matrix[0][0] + v.y * matrix[1][0] + v.z * matrix[2][0],
        v.x * matrix[0][1] + v.y * matrix[1][1] + v.z * matrix[2][1],
        v.x * matrix[0][2] + v.y * matrix[1][2] + v.z * matrix[2][2]};
}

std::optional<custom::MonsterStain> LocateMonsterStain(CCSOLScenarioMonster& monster,
    const TraceResult& hit, const Vector& direction) noexcept
{
    if (!g_pBoneTransform || !g_engfuncs.pfnGetModelPtr || !g_engfuncs.pfnGetBonePosition ||
        monster.pev->modelindex <= 0 || !Finite(hit.vecEndPos) || !Finite(monster.pev->angles)) return {};
    const EHANDLE handle(&monster);
    auto* pool = monster.Pool();
    if (!pool || !pool->Borrowed(monster)) return {};
    const auto lease = pool->Lease(monster);
    const int model = monster.pev->modelindex;
    const auto current = [&] {
        return handle.Get<CBaseEntity>() == &monster && monster.Pool() == pool &&
            pool->Lease(monster) == lease && monster.pev->modelindex == model;
    };

    auto* entity = monster.edict();
    const auto* studio = static_cast<const studiohdr_t*>(GET_MODEL_PTR(entity));
    if (!current() || !studio || studio->id != 0x54534449 || studio->numbones <= 0 ||
        studio->numbones > MAXSTUDIOBONES || studio->numhitboxes <= 0 ||
        studio->hitboxindex < int(sizeof(studiohdr_t)) || studio->hitboxindex > studio->length ||
        studio->numhitboxes > (studio->length - studio->hitboxindex) / int(sizeof(mstudiobbox_t))) return {};
    std::array<mstudiobbox_t, MAXSTUDIOBONES> boxes;
    const int bones = studio->numbones, count = studio->numhitboxes;
    if (count > int(boxes.size())) return {};
    std::memcpy(boxes.data(), reinterpret_cast<const unsigned char*>(studio) + studio->hitboxindex,
        count * sizeof(mstudiobbox_t));
    const Vector origin = monster.pev->origin;
    const Vector angles = monster.pev->angles;
    Vector normal = hit.vecPlaneNormal;
    if (!Finite(normal) || normal.LengthSquared() < .25f)
        normal = -direction;
    if (!Finite(normal) || normal.LengthSquared() < .25f) return {};
    normal = normal.Normalize();
    Vector offset = hit.vecEndPos - origin;

    // ReHLDS uses inverted pitch for the collision Studio hull. GetBonePosition
    // supplies the same bone hierarchy with the ordinary entity angles.
    if (angles.x)
    {
        float ordinary[3][4], collision[3][4];
        const Vector hullAngles(-angles.x, angles.y, angles.z);
        AngleMatrix(angles, ordinary);
        AngleMatrix(hullAngles, collision);
        const auto toOrdinary = [&](const Vector& v) {
            const auto local = InverseRotate(v, collision);
            return Vector(DotProduct(local, Vector(ordinary[0])), DotProduct(local, Vector(ordinary[1])),
                DotProduct(local, Vector(ordinary[2])));
        };
        offset = toOrdinary(offset);
        normal = toOrdinary(normal);
    }

    custom::MonsterStain best{};
    float bestScore = std::numeric_limits<float>::infinity();
    int lastBone = -1;
    Vector localPoint, localNormal;
    for (int i = 0; i < count; ++i)
    {
        const auto box = boxes[i];
        if (box.bone < 0 || box.bone >= bones || (hit.iHitgroup && box.group != hit.iHitgroup)) continue;
        if (lastBone != box.bone)
        {
            // The SDK's blending interface receives ReHLDS's matrix buffer.
            // GetBonePosition populates only this bone and its parents.
            float boneOrigin[3]{NAN, NAN, NAN};
            GET_BONE_POSITION(entity, box.bone, boneOrigin, nullptr);
            if (!current()) return {};
            const auto& matrix = (*g_pBoneTransform)[box.bone];
            localPoint = InverseRotate(origin + offset - Vector(boneOrigin), matrix);
            localNormal = InverseRotate(normal, matrix);
            lastBone = box.bone;
        }
        if (!Finite(localPoint) || !Finite(localNormal) ||
            !Finite(box.bbmin) || !Finite(box.bbmax)) continue;
        Vector point;
        int faceAxis = 0;
        float faceSide = 1.f, faceScore = std::numeric_limits<float>::infinity();
        for (int axis = 0; axis < 3; ++axis)
        {
            if (box.bbmin[axis] > box.bbmax[axis]) { faceAxis = -1; break; }
            point[axis] = std::clamp(localPoint[axis], box.bbmin[axis], box.bbmax[axis]);
            const float alignment = localNormal[axis];
            const float lower = std::abs(localPoint[axis] - box.bbmin[axis]) + alignment;
            const float upper = std::abs(localPoint[axis] - box.bbmax[axis]) - alignment;
            if (lower < faceScore) { faceScore = lower; faceAxis = axis; faceSide = -1.f; }
            if (upper < faceScore) { faceScore = upper; faceAxis = axis; faceSide = 1.f; }
        }
        if (faceAxis < 0) continue;
        point[faceAxis] = faceSide < 0 ? box.bbmin[faceAxis] : box.bbmax[faceAxis];
        const float distance = (point - localPoint).LengthSquared();
        const float score = distance + (1.f - faceSide * localNormal[faceAxis]) * 2.f;
        // Traced hulls can stop outside the actual surface. Do not turn a
        // remote impact or invalid hitbox into a mark elsewhere on the model.
        if (distance > 24.f * 24.f || score >= bestScore ||
            std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)}) >= 2047.f) continue;
        bestScore = score;
        best.bone = static_cast<std::uint8_t>(box.bone);
        for (int axis = 0; axis < 3; ++axis)
        {
            best.point[axis] = static_cast<std::int16_t>(std::lround(point[axis] * custom::MonsterStainPositionScale));
            best.normal[axis] = axis == faceAxis ? static_cast<std::int8_t>(faceSide * custom::MonsterStainNormalScale) : 0;
        }
    }
    return std::isfinite(bestScore) ? std::optional(best) : std::nullopt;
}

void RecordMonsterMark(CCSOLScenarioMonster& monster, const TraceResult& hit, const Vector& direction) noexcept
{
    const auto slot = static_cast<unsigned>(monster.entindex());
    if (!slot || slot >= monsterMarks.size()) return;
    auto& entry = monsterMarks[slot];
    if (entry.model == monster.pev->modelindex && entry.count == entry.marks.size()) return;
    const auto contact = LocateMonsterStain(monster, hit, direction);
    if (!contact || !enabled) return;
    if (entry.model != monster.pev->modelindex || !entry.id)
    {
        if (nextMonsterId == custom::MonsterStainIdMax) return;
        entry.model = monster.pev->modelindex;
        entry.id = ++nextMonsterId;
        entry.count = 0;
    }
    for (unsigned i = 0; i < entry.count; ++i)
    {
        const auto& previous = entry.marks[i];
        if (previous.bone != contact->bone || !std::equal(std::begin(previous.normal), std::end(previous.normal), contact->normal))
            continue;
        int distance{};
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const int delta = previous.point[axis] - contact->point[axis];
            const int bounded = std::clamp(delta, -16, 16);
            distance += bounded * bounded;
        }
        if (distance < 16 * 16) return;
    }
    if (entry.count < entry.marks.size()) entry.marks[entry.count++] = *contact;
}


void AddHit(unsigned& mark) noexcept
{
    if (!mark)
    {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
    }
    mark = custom::AddBodyStainHit(mark, randomState);
}

const Stain* Find(unsigned slot, const edict_t& edict) noexcept
{
    if (!slot || slot >= stains.size()) return nullptr;
    const auto& entry = stains[slot];
    return entry.serial == edict.serialnumber ? &entry : nullptr;
}

Stain* Record(CBaseEntity& actor) noexcept
{
    const auto slot = static_cast<unsigned>(actor.entindex());
    if (!slot || slot >= stains.size()) return nullptr;
    const auto serial = actor.edict()->serialnumber;
    auto& entry = stains[slot];
    if (entry.serial != serial) entry = {serial};
    return &entry;
}

unsigned WeaponMark(CBasePlayerItem& weapon) noexcept
{
    const auto* edict = weapon.edict();
    if (edict->free || (edict->v.flags & FL_KILLME)) return 0;
    const auto* entry = Find(static_cast<unsigned>(weapon.entindex()), *edict);
    return entry ? entry->weapon : 0;
}
}

void CSOL_RegisterBodyStainDelta(bool player, delta_s* schema) noexcept
{
    const unsigned bit = player ? 2u : 1u;
    if (schema && g_engfuncs.pfnDeltaFindField && DELTA_FINDFIELD(schema, custom::BodyStainDeltaField) >= 0)
        deltaTables |= bit;
    else
        deltaTables &= ~bit;
}

bool CSOL_BodyStainDeltaReady() noexcept { return deltaTables == 3; }

bool CSOL_BodyStainsActive() noexcept { return enabled; }

void CSOL_ResetBodyStains() noexcept
{
    stains.fill({});
    monsterMarks.fill({});
    for (auto& channel : recipients) channel = {};
    pendingRecipients = nextMonsterId = 0;
    ++mapGeneration;
    enabled = hasWeaponStains = false;
    randomState = 0;
    splashRadiusSquared = 0;
    pendingSplash = {};
    ++generation;
}

void CSOL_SetBodyStainPolicy(bool active, float radius) noexcept
{
    active = active && CSOL_BodyStainDeltaReady();
    const float squared = active && std::isfinite(radius) && radius > 0 ? radius * radius : 0;
    if (enabled == active && splashRadiusSquared == squared) return;
    if (!randomState && active)
        randomState = std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
    enabled = active;
    splashRadiusSquared = squared;
    pendingSplash = {};
    ++generation;
}

void CSOL_ClearBodyStain(CBaseEntity& actor) noexcept
{
    const auto* entity = actor.edict();
    if (pendingSplash.victim == &actor || (pendingSplash.shot && pendingSplash.shot->owner_ == entity))
        pendingSplash = {};
    const auto slot = static_cast<unsigned>(actor.entindex());
    if (Find(slot, *entity)) stains[slot] = {};
    if (slot && slot < monsterMarks.size()) monsterMarks[slot].count = monsterMarks[slot].id = 0;
    if (actor.IsPlayer() && slot && slot <= recipients.size())
    {
        recipients[slot - 1] = {};
        pendingRecipients &= ~(1u << (slot - 1));
    }
}

void CSOL_CopyDroppedBodyStain(CWeaponBox& box, CBasePlayerItem& weapon) noexcept
{
    if (!hasWeaponStains) return;
    if (const auto mark = WeaponMark(weapon))
        if (auto* entry = Record(box)) entry->weapon = mark;
}

CSOLBodyStainShot::CSOLBodyStainShot(CBaseEntity* shooter, const Vector& source,
    const CBasePlayerItem* melee) noexcept
{
    if (!enabled || !shooter || (!melee && !splashRadiusSquared)) return;
    auto* player = dynamic_cast<CBasePlayer*>(shooter);
    if (!player || !player->pev->viewmodel || !player->pev->weaponmodel || !player->m_pActiveItem) return;
    const auto* active = player->m_pActiveItem->edict();
    if (active->v.flags & FL_KILLME) return;
    const auto slot = player->m_pActiveItem->iItemSlot();
    if (melee ? player->m_pActiveItem != melee : slot != PRIMARY_WEAPON_SLOT && slot != PISTOL_SLOT) return;
    if (!CaptureOwner(*player, source)) return;
    melee_ = melee != nullptr;
    weapon_ = active;
    weaponSerial_ = active->serialnumber;
    view_ = player->pev->viewmodel;
    held_ = player->pev->weaponmodel;
}

CSOLBodyStainShot::CSOLBodyStainShot(CBasePlayer& attacker, const Vector& source, Contact) noexcept
{
    CaptureOwner(attacker, source);
}

bool CSOLBodyStainShot::CaptureOwner(CBasePlayer& player, const Vector& source) noexcept
{
    if (!enabled || player.m_iTeam != CT || !player.IsAlive() ||
        player.has_disconnected || !Finite(source)) return false;
    owner_ = player.edict();
    ownerSerial_ = owner_->serialnumber;
    spawn_ = player.m_iNumSpawns;
    source_[0] = source.x;
    source_[1] = source.y;
    source_[2] = source.z;
    generation_ = generation;
    return true;
}

CSOLBodyStainShot::~CSOLBodyStainShot()
{
    if (pendingSplash.shot == this) pendingSplash = {};
}

void CSOLBodyStainShot::TraceAttack(CBaseEntity* target, entvars_t* attacker, float damage,
    const Vector& direction, TraceResult* trace, int damageType) const
{
    if (!owner_ || attacker != &owner_->v || (!melee_ && (damageType & DMG_BULLET) == 0))
    {
        target->TraceAttack(attacker, damage, direction, trace, damageType);
        return;
    }
    AttackTrace contact{*this, target, trace, false, melee_};
    struct Restore
    {
        AttackTrace* previous;
        ~Restore() { attackTrace = previous; }
    } restore{attackTrace};
    attackTrace = &contact;
    target->TraceAttack(attacker, damage, direction, trace, damageType);
}

bool CSOLBodyStainShot::PrepareContact(CCSOLScenarioMonster& target, const TraceResult* contact,
    TraceResult& trace) const noexcept
{
    if (!owner_ || !enabled || generation_ != generation) return false;
    const auto* targetEdict = target.edict();
    if (contact && contact->pHit == targetEdict && contact->flFraction < 1.f)
    {
        trace = *contact;
        return true;
    }
    if (!g_engfuncs.pfnTraceLine) return false;
    const Vector source(source_);
    UTIL_TraceLine(source, target.Center(), dont_ignore_monsters, const_cast<edict_t*>(owner_), &trace);
    return trace.pHit == targetEdict && trace.flFraction < 1.f;
}

void CSOLBodyStainShot::CommitContact(CCSOLScenarioMonster& target, const TraceResult& hit) const noexcept
{
    if (!owner_ || !enabled || generation_ != generation || hit.pHit != target.edict() ||
        csol::monster_impact::Classify("monster", STRING(target.pev->model), target.pev->solid,
            hit.iHitgroup, csol::monster_impact::UnknownBullet).kind != csol::monster_impact::Kind::Blood) return;
    const auto delta = hit.vecEndPos - Vector(source_);
    RecordMonsterMark(target, hit, delta.Normalize());
    Splash(hit.vecEndPos);
}

void CSOLBodyStainShot::Splash(const Vector& hit) const noexcept
{
    const auto* actor = owner_;
    if (!enabled || !splashRadiusSquared || splashed_ || generation_ != generation || actor->free ||
        actor->serialnumber != ownerSerial_ || !actor->pvPrivateData || !Finite(hit) ||
        (hit - Vector(source_)).LengthSquared() > splashRadiusSquared) return;
    auto& player = *static_cast<CBasePlayer*>(actor->pvPrivateData);
    if (player.m_iNumSpawns != spawn_ || player.m_iTeam != CT || !player.IsAlive() ||
        player.has_disconnected) return;
    if (weapon_)
    {
        const StainedWeapon weapon{weapon_, weaponSerial_, view_, held_};
        if (!weapon.Matches(player)) return;
        auto* mark = Record(*player.m_pActiveItem);
        if (!mark) return;
        AddHit(mark->weapon);
        hasWeaponStains = true;
    }

    splashed_ = true;
    if (auto* entry = Record(player)) AddHit(entry->body);
}

bool CSOL_IsMeleeStainTrace(const CCSOLScenarioMonster& monster, const TraceResult* trace) noexcept
{
    return attackTrace && attackTrace->melee && attackTrace->target == &monster && attackTrace->trace == trace;
}

void CSOL_RecordBodyStains(CCSOLScenarioMonster& monster, entvars_t* attacker,
    const TraceResult* trace, const Vector& direction) noexcept
{
    if (!enabled || !trace || !Finite(trace->vecEndPos)) return;
    const auto hit = trace->vecEndPos;
    RecordMonsterMark(monster, *trace, direction);

    auto* contact = attackTrace;
    if (!contact || contact->trace != trace || contact->target != &monster || contact->recorded) return;
    contact->recorded = true;
    const auto& shot = contact->shot;
    if (!splashRadiusSquared || shot.splashed_ || shot.generation_ != generation || attacker != &shot.owner_->v ||
        gMultiDamage.hEntity.Get<CBaseEntity>() != &monster || !(gMultiDamage.amount > 0)) return;
    const Vector distance = Vector(shot.source_[0], shot.source_[1], shot.source_[2]) - hit;
    if (DotProduct(distance, distance) <= splashRadiusSquared)
        pendingSplash = {&shot, &monster, hit};
}

void CSOL_ClearBodyStainSplash() noexcept
{
    pendingSplash = {};
}

void CSOL_ApplyBodyStainSplash(CBaseEntity& victim, entvars_t* inflictor, entvars_t* attacker) noexcept
{
    if (!pendingSplash.shot) return;
    const auto pending = std::exchange(pendingSplash, {});
    const auto& shot = *pending.shot;
    const auto* actor = shot.owner_;
    if (pending.victim == &victim && inflictor == &actor->v && attacker == &actor->v && gMultiDamage.amount > 0)
        shot.Splash(pending.hit);
}

void CSOL_FlushMonsterStains(const CCSOLScenarioMessages& messages) noexcept
{
    if (!enabled || !pendingRecipients || !gpGlobals) return;
    auto pending = pendingRecipients;
    const auto session = mapGeneration;
    while (pending && session == mapGeneration && enabled)
    {
        const auto slot = static_cast<unsigned>(std::countr_zero(pending));
        const auto bit = 1u << slot;
        pending &= ~bit;
        auto& channel = recipients[slot];
        if (channel.nextSend > gpGlobals->time) continue;
        auto* host = INDEXENT(slot + 1);
        if (!host || host->free || host->serialnumber != channel.serial || (host->v.flags & FL_DORMANT))
        {
            channel = {};
            pendingRecipients &= ~bit;
            continue;
        }
        // At most one bounded reliable message per recipient per interval.
        // Only leases included in the recipient's entity snapshots enter this queue.
        while (channel.size)
        {
            const auto index = channel.pending[channel.head];
            auto& sent = channel.sent[index];
            const auto& entry = monsterMarks[index];
            const unsigned start = custom::MonsterStainId(sent) == entry.id ? custom::MonsterStainCount(sent) : 0;
            if (start < entry.count)
            {
                std::array<std::uint8_t, custom::MonsterStainPacketMax> packet;
                const unsigned count = std::min(entry.count - start, custom::MonsterStainsPerMessage);
                const unsigned id = entry.id;
                packet[0] = custom::MonsterStainSubtype;
                packet[1] = static_cast<std::uint8_t>(index);
                packet[2] = static_cast<std::uint8_t>(index >> 8);
                packet[3] = static_cast<std::uint8_t>(id);
                packet[4] = static_cast<std::uint8_t>(id >> 8);
                packet[5] = static_cast<std::uint8_t>(id >> 16);
                packet[6] = static_cast<std::uint8_t>(start);
                for (unsigned i = 0; i < count; ++i)
                    custom::WriteMonsterStain(std::span<std::uint8_t, custom::MonsterStainMarkSize>(
                        packet.data() + custom::MonsterStainHeaderSize + i * custom::MonsterStainMarkSize,
                        custom::MonsterStainMarkSize), entry.marks[start + i]);
                const auto size = custom::MonsterStainHeaderSize + count * custom::MonsterStainMarkSize;
                const int serial = channel.serial;
                if (!messages.SendMonsterStains(host, {packet.data(), size}) || session != mapGeneration ||
                    serial != channel.serial || !channel.size) break;
                channel.nextSend = gpGlobals->time + .125f;
                sent = (id << custom::BodyStainHitBits) | (start + count);
                if (entry.id == id && start + count < entry.count)
                {
                    sent |= queued;
                    break;
                }
            }
            sent &= ~queued;
            channel.head = (channel.head + 1) % channel.pending.size();
            --channel.size;
            if (channel.nextSend > gpGlobals->time) break;
        }
        if (!channel.size) pendingRecipients &= ~bit;
    }
}

float CSOL_PackBodyStain(const edict_t& actor, unsigned index, bool player, bool local, const edict_t* host) noexcept
{
    if (!enabled) return 0;
    const auto* entry = Find(index, actor);
    if (!player)
    {
        if ((actor.v.effects & EF_NODRAW) || index >= monsterMarks.size()) return 0;
        const auto& marks = monsterMarks[index];
        if (marks.count && actor.v.modelindex == marks.model)
        {
            const auto field = (marks.id << custom::BodyStainHitBits) | marks.count;
            if (host) QueueMonsterMarks(index, field, *host);
            return std::bit_cast<float>(custom::MonsterStainTag | field);
        }
        return entry && entry->weapon ? custom::EncodeBodyStainField({0, entry->weapon, true}) : 0;
    }

    const auto body = entry ? entry->body : 0u;
    unsigned held{};
    if (hasWeaponStains && actor.pvPrivateData && actor.v.deadflag == DEAD_NO && actor.v.health > 0)
    {
        const auto* owner = static_cast<const CBasePlayer*>(actor.pvPrivateData);
        if (owner->m_pActiveItem) held = WeaponMark(*owner->m_pActiveItem);
    }
    if (!local && !held) return 0;
    return custom::EncodeBodyStainField({local ? body : 0u, held, true});
}
