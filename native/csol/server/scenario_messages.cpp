// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_messages.h"
#include "damage_numbers.h"
#include "../scenario_abilities.hpp"
#include "../scenario_monster_effects.hpp"
#include "../playerclip.hpp"
#include "../oberon_hint.hpp"
#include "../frozen_death.hpp"
#include "../frozen_smoke.hpp"

#include <bit>
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>

namespace
{
// GoldSrc messages run on the server thread. Keep the open-packet guard outside
// a map-owned object: a message hook can reset/replace that object before End.
bool g_scenarioMessageOpen{};
struct ScenarioMessageScope
{
    ScenarioMessageScope() { g_scenarioMessageOpen = true; }
    ~ScenarioMessageScope() { g_scenarioMessageOpen = false; }
};
}

bool CCSOLScenarioMessages::Register()
{
    m_messageId = 0;
    m_upgradeMessageId = 0;
    m_statusMessageId = 0;
    m_skillSnapshotId = 0;
    m_monsterEffectId = 0;
    m_playerClipMessageId = 0;
    m_frozenIceMessageId = 0;
    m_frozenSmokeMessageId = 0;
    m_consumableIconMessageId = 0;
    m_respawnTextMessageId = 0;
    m_respawnHintMessageId = 0;
    m_valueSyncMessageId = 0;
    if (!g_engfuncs.pfnRegUserMsg) return false;
    const int id = REG_USER_MSG("MPToCL", -1);
    // GoldSrc transmits a user-message number as a byte. A failed registration
    // must not make MESSAGE_BEGIN send an engine service opcode instead.
    if (id < 64 || id > 255) return false;
    const int upgrades = REG_USER_MSG("ZBSVer2", -1);
    if (upgrades < 64 || upgrades > 255) return false;
    const int status = REG_USER_MSG("MPToCL2", -1);
    if (status < 64 || status > 255) return false;
    const int skillSnapshot = REG_USER_MSG("CsoSkState", 15);
    if (skillSnapshot < 64 || skillSnapshot > 255) return false;
    const int monsterEffect = REG_USER_MSG(csol::monster_effects::Message, 7);
    if (monsterEffect < 64 || monsterEffect > 255) return false;
    const int playerClip = REG_USER_MSG(csol::playerclip::Message, -1);
    if (playerClip < 64 || playerClip > 255) return false;
    const int frozenIce = REG_USER_MSG(csol::frozen_death::IceMessage,
        static_cast<int>(csol::frozen_death::IcePayloadSize));
    if (frozenIce < 64 || frozenIce > 255) return false;
    const int frozenSmoke = REG_USER_MSG(csol::frozen_smoke::SmokeMessage,
        static_cast<int>(csol::frozen_smoke::PayloadSize));
    if (frozenSmoke < 64 || frozenSmoke > 255) return false;
    const int consumableIcon = REG_USER_MSG("HudIcon", 2);
    if (consumableIcon < 64 || consumableIcon > 255) return false;
    const int respawnText = REG_USER_MSG("TextMsg", -1);
    if (respawnText < 64 || respawnText > 255) return false;
    const int respawnHint = REG_USER_MSG("HudTextArgs", -1);
    if (respawnHint < 64 || respawnHint > 255) return false;
    const int valueSync = REG_USER_MSG("ValueSync", -1);
    if (valueSync < 64 || valueSync > 255) return false;
    m_messageId = id;
    m_upgradeMessageId = upgrades;
    m_statusMessageId = status;
    m_skillSnapshotId = skillSnapshot;
    m_monsterEffectId = monsterEffect;
    m_playerClipMessageId = playerClip;
    m_frozenIceMessageId = frozenIce;
    m_frozenSmokeMessageId = frozenSmoke;
    m_consumableIconMessageId = consumableIcon;
    m_respawnTextMessageId = respawnText;
    m_respawnHintMessageId = respawnHint;
    m_valueSyncMessageId = valueSync;
    return true;
}

bool CCSOLScenarioMessages::SendMonsterDisarmImpact(const float* origin) const
{
    return WriteMonsterExplosion(origin, csol::monster_effects::Impact, -20.0f);
}

bool CCSOLScenarioMessages::SendMonsterZombieBomb(const float* origin) const
{
    // MP114A7E80 sends the unshifted projectile origin. The later damage
    // routine moves its own origin down by 20; it must not move this effect.
    return WriteMonsterExplosion(origin, csol::monster_effects::ZombieBomb, 0.0f);
}

bool CCSOLScenarioMessages::WriteMonsterExplosion(const float* origin, int effect, float zOffset) const
{
    if (g_scenarioMessageOpen || !m_monsterEffectId || !origin ||
        !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2]) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteCoord) return false;
    const int id = m_monsterEffectId;
    const Vector position(origin);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PVS, id, position);
    WRITE_BYTE(effect);
    WRITE_COORD(position.x);
    WRITE_COORD(position.y);
    WRITE_COORD(position.z + zOffset);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRoundClock(int round, int total, float elapsed,
    edict_s* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || round < 1 || total < round || total > 255 ||
        !std::isfinite(elapsed) || elapsed < 0 || (recipient && recipient->free) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    WRITE_BYTE(27);
    WRITE_BYTE(3);
    WRITE_BYTE(round);
    WRITE_BYTE(total);
    WRITE_LONG(std::bit_cast<std::int32_t>(elapsed));
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendPlayerClip(std::span<const std::uint8_t> payload,
    edict_s* recipient) const
{
    if (g_scenarioMessageOpen || !m_playerClipMessageId || !csol::playerclip::Read(payload) ||
        (recipient && recipient->free) || !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte) return false;
    const int id = m_playerClipMessageId;
    // Copy before opening a packet: message hooks can retire the map service.
    std::array<std::uint8_t, 30> copy{};
    std::copy(payload.begin(), payload.end(), copy.begin());
    const auto size = payload.size();
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    for (std::size_t i = 0; i < size; ++i) WRITE_BYTE(copy[i]);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterSkillShake(edict_t* recipient) const
{
    if (g_scenarioMessageOpen || !recipient || recipient->free || gmsgShake < 64 || gmsgShake > 255 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteShort) return false;
    const int id = gmsgShake;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_SHORT(0xA000);
    WRITE_SHORT(0x2000);
    WRITE_SHORT(0x6400);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendOberonImpact(edict_t* recipient, bool stun) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(stun ? 15 : 3);
    if (stun) WRITE_BYTE(3);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendOberonDebris(const float* origin, const float* size,
    bool landing, int modelIndex) const
{
    if (g_scenarioMessageOpen || !origin || !size || modelIndex <= 0 || modelIndex > 32767 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteCoord)
        return false;
    for (int axis = 0; axis != 3; ++axis)
        if (!std::isfinite(origin[axis]) || !std::isfinite(size[axis])) return false;
    const Vector position(origin), extent(size);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, position);
    WRITE_BYTE(TE_BREAKMODEL);
    WRITE_COORD(position.x);
    WRITE_COORD(position.y);
    WRITE_COORD(position.z);
    WRITE_COORD(extent.x);
    WRITE_COORD(extent.y);
    WRITE_COORD(extent.z);
    WRITE_COORD(0);
    WRITE_COORD(0);
    WRITE_COORD(landing ? 100 : 200);
    WRITE_BYTE(landing ? 10 : 5);
    WRITE_SHORT(modelIndex);
    WRITE_BYTE(landing ? 20 : 10);
    WRITE_BYTE(25);
    WRITE_BYTE(64);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendOberonExplosion(const float* origin, const float* visualOrigin, int modelIndex) const
{
    if (g_scenarioMessageOpen || !origin || !visualOrigin || modelIndex <= 0 || modelIndex > 32767 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteCoord)
        return false;
    for (int axis = 0; axis != 3; ++axis)
        if (!std::isfinite(origin[axis]) || !std::isfinite(visualOrigin[axis])) return false;
    const Vector center(origin), position(visualOrigin);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, center);
    WRITE_BYTE(TE_EXPLOSION);
    WRITE_COORD(position.x);
    WRITE_COORD(position.y);
    WRITE_COORD(position.z);
    WRITE_SHORT(modelIndex);
    WRITE_BYTE(30);
    WRITE_BYTE(30);
    WRITE_BYTE(0);
    // CSO adds one more zero byte. Stock CS has no such extension; sending
    // that byte would turn it into the next service opcode in this packet.
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterTrapDirection(edict_t* recipient, const float* origin) const
{
    if (g_scenarioMessageOpen || !m_monsterEffectId || !recipient || recipient->free || !origin ||
        !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2]) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteCoord) return false;
    const int id = m_monsterEffectId;
    const Vector position(origin);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(csol::monster_effects::TrapDirection);
    WRITE_COORD(position.x);
    WRITE_COORD(position.y);
    WRITE_COORD(position.z);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendStatusLabel(edict_t* recipient, std::int32_t label,
    bool style, std::int32_t type, float remaining) const
{
    if (g_scenarioMessageOpen || !m_statusMessageId || !recipient || recipient->free ||
        !std::isfinite(remaining) || !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_statusMessageId;
    const auto duration = std::bit_cast<std::int32_t>(remaining);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0x40);
    WRITE_BYTE(0);
    WRITE_LONG(label);
    WRITE_BYTE(style ? 1 : 0);
    WRITE_LONG(type);
    WRITE_LONG(duration);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendOberonHint(edict_s* recipient,
    const csol::oberon_hint::Event& event) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte) return false;
    const auto payload = csol::oberon_hint::Write(event);
    if (!payload) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    for (const auto byte : *payload) WRITE_BYTE(byte);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendFrozenIce(const csol::frozen_death::IceEvent& event) const
{
    if (g_scenarioMessageOpen || !m_frozenIceMessageId ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const auto payload = csol::frozen_death::WriteIce(event);
    if (!payload) return false;
    const int id = m_frozenIceMessageId;
    const Vector center(event.center.data());
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PVS, id, center);
    for (const auto byte : *payload) WRITE_BYTE(byte);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendFrozenSmoke(const csol::frozen_smoke::Event& event) const
{
    if (g_scenarioMessageOpen || !m_frozenSmokeMessageId ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const auto payload = csol::frozen_smoke::Write(event);
    if (!payload) return false;
    const int id = m_frozenSmokeMessageId;
    const ScenarioMessageScope scope;
    // Original11493360 broadcasts the cloud even when a viewer is outside
    // the fragment packet's PVS. Do not merge the two recipients/scopes.
    MESSAGE_BEGIN(MSG_ALL, id);
    for (const auto byte : *payload) WRITE_BYTE(byte);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendBossSkillStart(std::int16_t message) const
{
    if (g_scenarioMessageOpen || !m_messageId || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(27);
    WRITE_BYTE(7);
    WRITE_SHORT(message);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendBossParticipation(edict_t* recipient, int rank,
    int playerIndex, std::int32_t hundreds, int percent) const
{
    if (g_scenarioMessageOpen || !m_messageId || (recipient && recipient->free) ||
        rank < 0 || rank > 32 || playerIndex < 0 || playerIndex > 32 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    WRITE_BYTE(76);
    WRITE_SHORT(rank);
    WRITE_BYTE(playerIndex);
    WRITE_LONG(hundreds);
    WRITE_BYTE(percent);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendBossHealthInit(int appearance, std::int32_t maximum,
    int entityIndex, edict_t* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || entityIndex <= 0 || (recipient && recipient->free) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    WRITE_BYTE(27);
    WRITE_BYTE(13);
    WRITE_SHORT(appearance);
    WRITE_LONG(maximum);
    WRITE_LONG(entityIndex);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendBossHealthRatio(int appearance, float health,
    float maximum, int entityIndex, edict_t* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || entityIndex <= 0 || (recipient && recipient->free) ||
        !std::isfinite(health) || !std::isfinite(maximum) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong) return false;
    // CSO's12278680 is its extended PF_WriteFloat_I, not the stock engine's
    // WriteCoord slot. Preserve the full IEEE754 ratio without quantization.
    const float ratio = maximum > 0 ? std::min(1.0f, health / maximum) : 0.0f;
    if (!std::isfinite(ratio)) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    WRITE_BYTE(27);
    WRITE_BYTE(27);
    WRITE_SHORT(appearance);
    WRITE_LONG(std::bit_cast<std::int32_t>(ratio));
    WRITE_LONG(entityIndex);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::RemoveStatusLabel(edict_t* recipient, std::int32_t label) const
{
    if (g_scenarioMessageOpen || !m_statusMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_statusMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0x40);
    WRITE_BYTE(1);
    WRITE_LONG(label);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::ClearStatusLabels(edict_t* recipient) const
{
    if (g_scenarioMessageOpen || !m_statusMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_statusMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0x40);
    WRITE_BYTE(2);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendUpgradeTotal(edict_t* recipient, int level, bool increased) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(increased ? 5 : 4);
    WRITE_LONG(level);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendUpgradeExperience(edict_t* recipient, int percent) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(3);
    WRITE_LONG(percent);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendActiveSkillSnapshot(edict_t* recipient,
    const csol::abilities::Snapshot& value) const
{
    if (g_scenarioMessageOpen || !m_skillSnapshotId || !recipient || recipient->free || value.type > 55 ||
        !std::isfinite(value.cooldown) || !std::isfinite(value.elapsed) || !std::isfinite(value.duration) ||
        value.cooldown < 0 || value.elapsed < 0 || value.elapsed > value.cooldown || value.duration < 0 ||
        (!value.type && (value.cooldown != 0 || value.elapsed != 0 || value.duration != 0)) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte ||
        !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_skillSnapshotId;
    const auto snapshot = value;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(1);
    WRITE_SHORT(snapshot.type);
    WRITE_LONG(std::bit_cast<std::int32_t>(snapshot.cooldown));
    WRITE_LONG(std::bit_cast<std::int32_t>(snapshot.elapsed));
    WRITE_LONG(std::bit_cast<std::int32_t>(snapshot.duration));
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendActiveSkillCooldown(edict_t* recipient, int subtype,
    float cooldown, float duration) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free ||
        subtype < 0 || subtype > 32767 || !std::isfinite(cooldown) || !std::isfinite(duration) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0x24);
    WRITE_SHORT(subtype);
    WRITE_LONG(std::bit_cast<std::int32_t>(cooldown));
    WRITE_LONG(std::bit_cast<std::int32_t>(duration));
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterKillChain(edict_t* recipient, std::int32_t count) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(1);
    WRITE_LONG(count);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterHeadHits(edict_t* recipient, std::int32_t count) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(2);
    WRITE_LONG(count);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterDamageEffect(edict_t* recipient, int monsterIndex,
    int subtype, float height, float magnitude) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        monsterIndex <= 0 || monsterIndex > 0xffff ||
        (subtype != 43 && subtype != 46 && subtype != 47 && subtype != 53) ||
        !std::isfinite(height) || !std::isfinite(magnitude) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong)
        return false;
    const int id = m_messageId;
    const auto index = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(monsterIndex));
    const auto first = std::bit_cast<std::int32_t>(height);
    const auto second = std::bit_cast<std::int32_t>(magnitude);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(199);
    WRITE_SHORT(index);
    WRITE_BYTE(subtype);
    // CSO's engine slot E0 is PF_WriteFloat_I, not stock WriteCoord.
    // Keep the raw four-byte values, without coordinate quantization.
    WRITE_LONG(first);
    WRITE_LONG(second);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRecoveryNumber(edict_t* recipient, int playerIndex,
    std::int32_t amount) const
{
    if (g_scenarioMessageOpen || playerIndex <= 0 || playerIndex > MAX_CLIENTS || amount < 0)
        return false;
    const int id = m_messageId;
    const CSOLDamageNumber message{static_cast<std::int16_t>(playerIndex), 6, 4, amount};
    // Reuse the common writer and hold the scenario guard too: a hook inside
    // this packet must not start a wave/status/death packet before its End.
    // This does not claim to lock unrelated callers of the engine messaging API.
    const ScenarioMessageScope scope;
    return CSOLWriteDamageNumber(id, recipient, message);
}

bool CCSOLScenarioMessages::SendUpgradeStat(edict_t* recipient, unsigned stat, int level, int cap) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free || stat >= 5 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(6 + stat * 2);
    WRITE_LONG(level);
    WRITE_LONG(cap);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendWeaponDamageBuffHint(edict_t* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte)
        return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0x4D);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendClassModMonsterDamage(edict_t* recipient, int playerIndex,
    std::int32_t amount) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        playerIndex < 1 || playerIndex > MAX_CLIENTS ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0xAA);
    WRITE_BYTE(playerIndex);
    WRITE_LONG(amount);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendWaveEvent(std::span<const std::uint8_t, 4> payload) const
{
    if (g_scenarioMessageOpen || !m_messageId || payload[0] != 0x1b || payload[1] != 7 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort) return false;
    const std::uint16_t id = static_cast<std::uint16_t>(payload[2] |
        (static_cast<std::uint16_t>(payload[3]) << 8));
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, m_messageId, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(0x1b);
    WRITE_BYTE(7);
    WRITE_SHORT(std::bit_cast<std::int16_t>(id));
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRoundClearSound() const
{
    if (g_scenarioMessageOpen || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteString) return false;
    const ScenarioMessageScope scope;
    // MP114C0F70 uses the engine's stufftext opcode and a fixed sentence.
    constexpr int stuffText = 9; // GoldSrc svc_stufftext, absent from this SDK's util.h.
    MESSAGE_BEGIN(MSG_BROADCAST, stuffText, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_STRING("spk \"RoundClear\"\n");
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRoundOutcome(bool success) const
{
    if (g_scenarioMessageOpen || !m_messageId || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(0x19);
    WRITE_BYTE(success ? 8 : 9);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendConsumableState(edict_s* recipient, unsigned index,
    float cooldown, int count) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free || index >= 4 ||
        !std::isfinite(cooldown) || cooldown < 0 || count < -32768 || count > 32767 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong || !g_engfuncs.pfnWriteShort) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(27);
    WRITE_BYTE(4);
    WRITE_BYTE(index);
    WRITE_LONG(std::bit_cast<std::int32_t>(cooldown));
    WRITE_SHORT(count);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendConsumableVisibility(edict_s* recipient, std::uint16_t mask) const
{
    if (g_scenarioMessageOpen || !m_consumableIconMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteShort) return false;
    const int id = m_consumableIconMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_SHORT(std::bit_cast<std::int16_t>(mask));
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRespawnText(edict_s* recipient, const char* token, const char* argument) const
{
    if (g_scenarioMessageOpen || !m_respawnTextMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteString) return false;
    const int id = m_respawnTextMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(HUD_PRINTCENTER);
    WRITE_STRING(token);
    if (argument) WRITE_STRING(argument);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRespawnCountdown(edict_s* recipient, bool automatic, int seconds) const
{
    if (seconds < 0) return false;
    char argument[16]{};
    const auto result = std::to_chars(argument, argument + sizeof(argument) - 1, seconds);
    if (result.ec != std::errc{}) return false;
    return SendRespawnText(recipient, automatic ? "#CSO_ZBS_AutoRespawnWait" : "#CSO_ZBS_RespawnWait", argument);
}

bool CCSOLScenarioMessages::SendRespawnUnavailable(edict_s* recipient) const
{
    return SendRespawnText(recipient, "#CSO_ZBS_CantRespawnAnymore", nullptr);
}

bool CCSOLScenarioMessages::SendRevivalHint(edict_s* recipient, bool retry) const
{
    if (g_scenarioMessageOpen || !m_respawnHintMessageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteString) return false;
    const int id = m_respawnHintMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_STRING(retry ? "#CSO_ZBS_UseRetryItem" : "#CSO_ZBS_UseRevivalItem");
    WRITE_BYTE(1);
    WRITE_BYTE(0);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRespawnReloadWait(edict_s* recipient, int seconds) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free || seconds < 0 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, id, nullptr, recipient);
    WRITE_BYTE(0x1C);
    WRITE_LONG(seconds);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRetryCountdown(int seconds) const
{
    if (g_scenarioMessageOpen || !m_messageId || seconds < -1 || seconds > 10 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(27);
    WRITE_BYTE(15);
    WRITE_BYTE(static_cast<std::uint8_t>(seconds));
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRetryAnnouncement(int playerIndex, int oneBasedRound) const
{
    if (g_scenarioMessageOpen || !m_messageId || playerIndex < 1 || playerIndex > MAX_CLIENTS ||
        oneBasedRound < 1 || oneBasedRound > 255 || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(27);
    WRITE_BYTE(16);
    WRITE_BYTE(playerIndex);
    WRITE_BYTE(oneBasedRound);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendGoldenNotice(bool killed) const
{
    if (g_scenarioMessageOpen || !m_respawnTextMessageId ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteString) return false;
    const int id = m_respawnTextMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_BROADCAST, id);
    WRITE_BYTE(4);
    WRITE_BYTE(0);
    WRITE_BYTE(7);
    WRITE_BYTE(0);
    WRITE_STRING(killed ? "#CSO_ZBS_KilledGoldenZB" : "#CSO_ZBS_SpawnedGoldenZB");
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendAllRoundsClear(int textMessageId) const
{
    const int id = textMessageId;
    if (g_scenarioMessageOpen || id < 64 || id > 255 || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteString)
        return false;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(HUD_PRINTCENTER);
    // CSO_CongAllRoundClear from common/resource/cso_chn.txt. The native CS
    // client lacks that token; use its Chinese text without a CSO-only prefix.
    WRITE_STRING("恭喜！！您已经成功通关！");
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRetryBudget(int remaining, int initial, edict_s* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || remaining < -1 || initial < -1 ||
        (recipient && recipient->free) || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    WRITE_BYTE(27);
    WRITE_BYTE(29);
    WRITE_LONG(remaining);
    WRITE_LONG(initial);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendTeamMonsterKills(int team, std::uint16_t count, edict_s* recipient) const
{
    if (g_scenarioMessageOpen || !m_valueSyncMessageId || (team != 1 && team != 2) ||
        (recipient && recipient->free) || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort) return false;
    const int id = m_valueSyncMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    WRITE_BYTE(team + 4); // Original manager1298/129A: keys5/6, player0.
    WRITE_BYTE(0);
    WRITE_SHORT(count);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendRoundRewardState(bool pastRewardRound) const
{
    if (g_scenarioMessageOpen || !m_messageId || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(0x24);
    WRITE_BYTE(pastRewardRound ? 1 : 0);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendNativeTeamScore(int messageId, bool ct, std::int16_t score) const
{
    const int id = messageId;
    if (g_scenarioMessageOpen || id < 64 || id > 255 || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteString || !g_engfuncs.pfnWriteShort) return false;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_STRING(ct ? "CT" : "TERRORIST");
    WRITE_SHORT(score);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendFinalResult(edict_t* recipient, std::string_view text,
    std::span<const std::int32_t, 5> values) const
{
    constexpr std::size_t maxTextBytes = 192 - 1 - 1 - 5 * sizeof(std::int32_t);
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !recipient || recipient->free ||
        text.size() > maxTextBytes || text.find('\0') != std::string_view::npos ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteString || !g_engfuncs.pfnWriteLong)
        return false;
    // Hooks may release the owner or replace caller-owned result storage.
    // Finish the entire packet from bounded local copies after MessageBegin.
    const int id = m_upgradeMessageId;
    std::array<char, maxTextBytes + 1> caption{};
    std::copy(text.begin(), text.end(), caption.begin());
    std::array<std::int32_t, 5> fields{};
    std::copy(values.begin(), values.end(), fields.begin());
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0x18);
    WRITE_STRING(caption.data());
    for (const auto value : fields) WRITE_LONG(value);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterContactEffect(edict_t* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong)
        return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(0x51);
    WRITE_SHORT(10);
    // CSO PF_WriteFloat_I writes the little-endian IEEE754 word; the stock
    // SDK's WriteLong transmits that same word without changing its bits.
    WRITE_LONG(std::bit_cast<std::int32_t>(1.0f));
    WRITE_BYTE(255);
    WRITE_BYTE(255);
    WRITE_BYTE(255);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendPlayerFrags(int playerIndex, std::int32_t frags) const
{
    if (g_scenarioMessageOpen || !m_messageId || playerIndex < 1 || playerIndex > 32 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort) return false;
    const int id = m_messageId;
    const auto value = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(frags));
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_BROADCAST, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(27);
    WRITE_BYTE(2);
    WRITE_BYTE(playerIndex);
    WRITE_SHORT(value);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterHealing(int monsterIndex, const float* origin, bool self) const
{
    if (g_scenarioMessageOpen || !m_messageId || !origin || monsterIndex < 1 || monsterIndex > 0xffff ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort) return false;
    const Vector position(origin);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PVS, id, position);
    WRITE_BYTE(4);
    WRITE_SHORT(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(monsterIndex)));
    WRITE_BYTE(self ? 13 : 9);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendAnnounceRibbons(edict_t* recipient,
    std::span<const std::uint8_t> ids) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        ids.empty() || ids.size() > 6 || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    // A message callback can release the owning service and its vector.
    // Copy the payload before opening the packet and never reread that owner.
    std::array<std::uint8_t, 6> payload{};
    const auto count = ids.size();
    std::copy(ids.begin(), ids.end(), payload.begin());
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(124);
    WRITE_BYTE(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) WRITE_BYTE(payload[i]);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterDeath(edict_t* recipient, int monsterIndex) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        monsterIndex <= 0 || monsterIndex > 0xffff ||
        !g_engfuncs.pfnEntOffsetOfPEntity || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte ||
        !g_engfuncs.pfnWriteShort || !OFFSET(recipient)) return false;

    return WriteMonsterDeath(recipient, monsterIndex);
}

bool CCSOLScenarioMessages::SendMonsterEffectRemoval(int monsterIndex, int effectId, bool builtIn) const
{
    if (g_scenarioMessageOpen || !m_messageId || monsterIndex <= 0 || monsterIndex > 0xffff ||
        effectId <= 0 || effectId > 0xffff || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort)
        return false;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, m_messageId, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(0x61);
    WRITE_SHORT(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(monsterIndex)));
    WRITE_SHORT(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(effectId)));
    WRITE_BYTE(builtIn ? 1 : 0);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::WriteMonsterDeath(edict_t* recipient, int monsterIndex) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        monsterIndex <= 0 || monsterIndex > 0xffff ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort) return false;

    // Complete the packet from copied scalar data: message hooks may change
    // entity state, so no monster/private pointer is read between writes.
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, m_messageId, nullptr, recipient);
    WRITE_BYTE(200);
    WRITE_SHORT(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(monsterIndex)));
    MESSAGE_END();
    return true;
}
