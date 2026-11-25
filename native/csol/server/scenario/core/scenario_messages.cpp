// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_messages.h"
#include "scenario_zombie_gibs.h"
#include "../../damage_numbers.h"
#include "../../../scenario/core/scenario_abilities.hpp"
#include "../../../scenario/monsters/scenario_monster_effects.hpp"
#include "../../../scenario/custom/monster_stains.hpp"
#include "../../../cso_tent.hpp"
#include "../../../scenario/core/playerclip.hpp"
#include "../../../scenario/bosses/oberon_hint.hpp"
#include "../../../scenario/monsters/frozen_death.hpp"
#include "../../../scenario/monsters/frozen_smoke.hpp"
#include "../../../scenario/core/wall_upgrade.hpp"
#include "../bosses/scenario_teddy_resources.h"
#include "../monsters/scenario_monster_effects.h"
#include "../monsters/scenario_monster_models.h"

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
std::array<int, 2> g_gibSplashDecals{-1, -1};
void WriteRawFloat(float value) { WRITE_LONG(std::bit_cast<std::int32_t>(value)); }
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
    const int monsterEffect = REG_USER_MSG(csol::monster_effects::Message, -1);
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
    // The engine has already loaded its base decal catalog at startup.
    g_gibSplashDecals = g_engfuncs.pfnDecalIndex
        ? std::array{DECAL_INDEX("{bigblood1"), DECAL_INDEX("{bigblood2")}
        : std::array{-1, -1};
    return true;
}

bool CCSOLScenarioMessages::SendMonsterDisarmImpact(const float* origin) const
{
    return WriteMonsterExplosion(origin, csol::monster_effects::Impact, -20.0f);
}

bool CCSOLScenarioMessages::SendMonsterHit(const float* origin, bool metal, int amount) const
{
    if (g_scenarioMessageOpen || !m_monsterEffectId || !origin || amount <= 0 ||
        amount > (metal ? 60 : 15) || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteCoord)
        return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(origin[i]) || origin[i] < -4096.0f || origin[i] > 4095.875f) return false;
    // Copy before any engine callback; message hooks can retire this session.
    const int id = m_monsterEffectId;
    const Vector position(origin);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PVS, id, position);
    WRITE_BYTE(metal ? csol::monster_effects::Spark : csol::monster_effects::Blood);
    WRITE_BYTE(amount);
    WRITE_COORD(position.x);
    WRITE_COORD(position.y);
    WRITE_COORD(position.z);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterStains(edict_s* recipient, std::span<const std::uint8_t> bytes) const
{
    using namespace csol::scenario::custom;
    if (g_scenarioMessageOpen || !m_monsterEffectId || !recipient ||
        bytes.size() <= MonsterStainHeaderSize || bytes.size() > MonsterStainPacketMax ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, m_monsterEffectId, nullptr, recipient);
    for (const auto byte : bytes) WRITE_BYTE(byte);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendMonsterZombieBomb(const float* origin) const
{
    // MP114A7E80 sends the unshifted projectile origin. The later damage
    // routine moves its own origin down by 20; it must not move this effect.
    return WriteMonsterExplosion(origin, csol::monster_effects::ZombieBomb, 0.0f);
}

bool CCSOLScenarioMessages::SendCsoTent(const csol::cso_tent::Model& tent, const Vector& pvsOrigin) const
{
    std::size_t size{};
    const auto payload = csol::cso_tent::Write(tent, size);
    if (g_scenarioMessageOpen || !m_monsterEffectId || !payload ||
        !std::isfinite(pvsOrigin.x) || !std::isfinite(pvsOrigin.y) || !std::isfinite(pvsOrigin.z) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_monsterEffectId;
    const Vector position(pvsOrigin);
    const ScenarioMessageScope scope;
    // MP1155CA50/1155CCF0: pev->origin selects the PVS independently of
    // payload coordinates (TE133 lowers the visible poison to the floor).
    MESSAGE_BEGIN(MSG_PVS, id, position);
    for (std::size_t i = 0; i < size; ++i) WRITE_BYTE((*payload)[i]);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendCsoTent(const csol::cso_tent::Sprite& tent) const
{
    const auto payload = csol::cso_tent::Write(tent);
    if (g_scenarioMessageOpen || !m_monsterEffectId || !payload ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_monsterEffectId;
    const ScenarioMessageScope scope;
    // MP1155CBD0/1155CDE0: MessageBegin(MSG_BROADCAST=0, ..., null, null).
    MESSAGE_BEGIN(MSG_BROADCAST, id);
    for (const auto byte : *payload) WRITE_BYTE(byte);
    MESSAGE_END();
    return true;
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

bool CCSOLScenarioMessages::SendWallUpgradeState(bool enabled, int level, edict_s* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || level < csol::wall_upgrade::InitialLevel ||
        level > csol::wall_upgrade::MaximumLevel || (recipient && recipient->free) ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, id, nullptr, recipient);
    WRITE_BYTE(27);
    WRITE_BYTE(10);
    WRITE_BYTE(enabled ? 1 : 0);
    WRITE_BYTE(level);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendWallUpgrader(int playerIndex) const
{
    if (g_scenarioMessageOpen || !m_messageId || playerIndex < 1 || playerIndex > 32 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id, nullptr, static_cast<edict_t*>(nullptr));
    WRITE_BYTE(27);
    WRITE_BYTE(11);
    WRITE_BYTE(playerIndex);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendWallUpgradeDenied(edict_s* recipient) const
{
    // MP114C0199 uses center TextMsg; keep the same existing stock-CS TextMsg
    // adaptation as the original scenario revival/continuation messages.
    return SendRespawnText(recipient, csol::wall_upgrade::DeniedText, nullptr);
}

bool CCSOLScenarioMessages::SendWallUpgradeIndicator(edict_s* recipient, int entityIndex, bool remove) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        entityIndex < 1 || entityIndex > 32767 || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort ||
        (!remove && (!g_engfuncs.pfnWriteCoord || !g_engfuncs.pfnWriteLong))) return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(remove ? 96 : 90);
    if (!remove) WRITE_BYTE(0);
    WRITE_SHORT(entityIndex);
    if (!remove)
    {
        WRITE_BYTE(51);
        for (const float offset : csol::wall_upgrade::IndicatorOffset) WRITE_COORD(offset);
        WRITE_LONG(std::bit_cast<std::int32_t>(csol::wall_upgrade::IndicatorDuration));
        WRITE_BYTE(1);
        WRITE_LONG(std::bit_cast<std::int32_t>(csol::wall_upgrade::IndicatorScale));
    }
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

bool CCSOLScenarioMessages::SendChangeLevelZone(int inside, int participants) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(29);
    WRITE_BYTE(inside);
    WRITE_BYTE(participants);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendChangeLevelTimer(int seconds) const
{
    if (g_scenarioMessageOpen || !m_upgradeMessageId || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const int id = m_upgradeMessageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(30);
    WRITE_BYTE(seconds);
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

bool CCSOLScenarioMessages::SendZombieBombProtection(edict_t* recipient, float duration) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        !std::isfinite(duration) || duration < 0 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong) return false;
    const int id = m_messageId;
    const auto bits = std::bit_cast<std::int32_t>(duration);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(54); WRITE_BYTE(0); WRITE_LONG(bits);
    WRITE_BYTE(190); WRITE_BYTE(190); WRITE_BYTE(190); WRITE_BYTE(255);
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

bool CSOLWriteGuardedDamageNumber(int messageId, edict_t* recipient, CSOLDamageNumber message)
{
    if (g_scenarioMessageOpen) return false;
    const ScenarioMessageScope scope;
    return CSOLWriteDamageNumber(messageId, recipient, message);
}

bool CCSOLScenarioMessages::SendAttackNumber(edict_t* recipient, CSOLDamageNumber message) const
{
    return CSOLWriteGuardedDamageNumber(m_messageId, recipient, message);
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

bool CCSOLScenarioMessages::SendBloodScreen(edict_s* recipient) const
{
    if (g_scenarioMessageOpen || !m_messageId || !recipient || recipient->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte)
        return false;
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_BYTE(50);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendTeddyTentacleShake(edict_s* recipient) const
{
    if (g_scenarioMessageOpen || !recipient || recipient->free || gmsgShake < 64 || gmsgShake > 255 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteShort)
        return false;
    const int id = gmsgShake;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ONE, id, nullptr, recipient);
    WRITE_SHORT(0x4000);
    WRITE_SHORT(0x1000);
    WRITE_SHORT(0x0300);
    MESSAGE_END();
    return true;
}

namespace
{
bool ZombieGibBloodEnabled()
{
    return g_Language != LANGUAGE_GERMAN && UTIL_ShouldShowBlood(BLOOD_COLOR_RED);
}

bool IsGibDecalHit(const TraceResult& hit)
{
    return !hit.fAllSolid && !hit.fStartSolid && hit.flFraction < 1.0f && hit.pHit &&
        !hit.pHit->free && !(hit.pHit->v.flags & FL_KILLME) &&
        (hit.pHit->v.solid == SOLID_BSP || hit.pHit->v.movetype == MOVETYPE_PUSHSTEP);
}

void SendGibBurstBlood(const Vector& origin, const CSOLMonsterEffectGuard& guard)
{
    if (g_pGameRules && g_sModelIndexBloodSpray && g_sModelIndexBloodDrop)
        UTIL_BloodDrips(origin, BLOOD_COLOR_RED, 80);
    if (!g_engfuncs.pfnTraceLine || !guard.Current()) return;

    // Leave the killing attack's engine trace in place after these visual rays.
    const auto previous = UTIL_GetGlobalTrace();
    const int traceFlags = gpGlobals->trace_flags;
    const EHANDLE previousHit(previous.pHit);
    std::array<Vector, 13> marked{};
    std::size_t marks{};
    const auto stain = [&](TraceResult& hit, int largeDecal)
    {
        if (!IsGibDecalHit(hit)) return;
        for (std::size_t i = 0; i < marks; ++i)
        {
            const auto delta = hit.vecEndPos - marked[i];
            if (DotProduct(delta, delta) < 16.0f * 16.0f) return;
        }
        marked[marks++] = hit.vecEndPos;
        if (largeDecal >= 0 && largeDecal < 512)
            UTIL_DecalTraceByIndex(&hit, largeDecal);
        else
            UTIL_BloodDecalTrace(&hit, BLOOD_COLOR_RED);
    };

    TraceResult down{};
    UTIL_TraceLine(origin, origin - Vector(0, 0, 128), ignore_monsters, nullptr, &down);
    if (!guard.Current()) return;
    const float depth = IsGibDecalHit(down) ? origin.z - down.vecEndPos.z + 8.0f : 72.0f;
    stain(down, g_gibSplashDecals[0]);

    // Overlap the native large decals at the blast center. The rays with
    // longer reach place smaller splashes and find nearby walls.
    constexpr float diagonal = 0.70710678f;
    const Vector directions[]{
        {1, 0, 0}, {diagonal, diagonal, 0}, {0, 1, 0}, {-diagonal, diagonal, 0},
        {-1, 0, 0}, {-diagonal, -diagonal, 0}, {0, -1, 0}, {diagonal, -diagonal, 0}
    };
    const float yaw = g_engfuncs.pfnRandomFloat ? RANDOM_FLOAT(0.0f, 6.2831853f) : 0.0f;
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    for (std::size_t i = 0; i < std::size(directions); ++i)
    {
        if (!guard.Current()) return;
        const auto& axis = directions[i];
        const Vector direction(axis.x * cosine - axis.y * sine, axis.x * sine + axis.y * cosine, 0);
        const bool core = i % 4 == 0;
        const float reach = core ? 32.0f : (i % 2 == 0 ? 58.0f : 132.0f);
        TraceResult hit{};
        UTIL_TraceLine(origin, origin + direction * reach -
            Vector(0, 0, depth), ignore_monsters, nullptr, &hit);
        if (!guard.Current()) return;
        stain(hit, core ? g_gibSplashDecals[1] : -1);
        if (i % 2 != 0) continue;
        if (!guard.Current()) return;
        UTIL_TraceLine(origin, origin + direction * 144.0f + Vector(0, 0, 12),
            ignore_monsters, nullptr, &hit);
        if (!guard.Current()) return;
        stain(hit, -1);
    }
    if (!guard.Current()) return;
    gpGlobals->trace_fraction = previous.flFraction;
    gpGlobals->trace_allsolid = static_cast<float>(previous.fAllSolid);
    gpGlobals->trace_startsolid = static_cast<float>(previous.fStartSolid);
    gpGlobals->trace_inopen = static_cast<float>(previous.fInOpen);
    gpGlobals->trace_inwater = static_cast<float>(previous.fInWater);
    gpGlobals->trace_endpos = previous.vecEndPos;
    gpGlobals->trace_plane_normal = previous.vecPlaneNormal;
    gpGlobals->trace_plane_dist = previous.flPlaneDist;
    gpGlobals->trace_ent = previousHit.Get();
    gpGlobals->trace_hitgroup = previous.iHitgroup;
    gpGlobals->trace_flags = traceFlags;
}

#ifdef REGAMEDLL_API
string_t g_zombieGibClass{};
int g_zombieGibsAlive{};

class CCSOLZombieGib final : public CGib
{
public:
    int ObjectCaps() override { return CGib::ObjectCaps() | FCAP_MUST_RELEASE; }
    void OnCreate() override { ++g_zombieGibsAlive; }
    void OnDestroy() override { --g_zombieGibsAlive; }

    void StainImpact(edict_t* surface)
    {
        if (!m_cBloodDecals || (pev->flags & FL_KILLME) || g_scenarioMessageOpen) return;
        auto hit = UTIL_GetGlobalTrace();
        if (hit.pHit != surface || !IsGibDecalHit(hit)) return;
        // Resting pieces can touch the same plane repeatedly on consecutive
        // physics ticks. Preserve distinct marks on a floor and adjacent wall.
        const auto delta = hit.vecEndPos - m_lastMark;
        if (m_marked && DotProduct(delta, delta) < 16.0f * 16.0f &&
            DotProduct(hit.vecPlaneNormal, m_lastNormal) > 0.9f) return;
        if (!ZombieGibBloodEnabled()) return;
        m_marked = true;
        m_lastMark = hit.vecEndPos;
        m_lastNormal = hit.vecPlaneNormal;
        --m_cBloodDecals;
        const ScenarioMessageScope scope;
        UTIL_BloodDecalTrace(&hit, BLOOD_COLOR_RED);
    }

private:
    Vector m_lastMark{}, m_lastNormal{};
    bool m_marked{};
};

bool SpawnBloodGibs(const Vector& origin, CSOLZombieGibModels models, int count,
    const CSOLMonsterEffectGuard& guard)
{
    constexpr int maxLivePieces = 40;
    if (g_zombieGibsAlive + count > maxLivePieces || !g_engfuncs.pfnCreateEntity ||
        !g_engfuncs.pfnPvAllocEntPrivateData || !g_engfuncs.pfnSetModel ||
        !g_engfuncs.pfnSetSize || !g_engfuncs.pfnSetOrigin || !g_engfuncs.pfnRandomFloat) return false;
    const auto names = CSOL_ScenarioGibModelNames({models.flesh, models.skull, models.legBone}, count);
    if (!names[0]) return false;

    std::array<EHANDLE, 5> pieces{};
    const float rotation = RANDOM_FLOAT(0.0f, 6.2831853f);
    for (int i = 0; i < count && guard.Current(); ++i)
    {
        const bool skull = i == 0 && names[1];
        const bool bone = i == 1 && names[2];
        auto* edict = CREATE_ENTITY();
        if (!edict || !guard.Current()) break;
        pieces[i] = EHANDLE(edict);
        auto* gib = GetClassPtr<CCSGib>(reinterpret_cast<CCSOLZombieGib*>(&edict->v));
        if (!guard.Current() || pieces[i].Get() != edict || (edict->v.flags & FL_KILLME)) break;
        gib->pev->origin = origin;
        gib->Spawn(names[skull ? 1 : bone ? 2 : 0]);
        if (!guard.Current() || pieces[i].Get() != edict || (edict->v.flags & FL_KILLME)) break;

        // A non-solid bounce still sweeps against BSP. GoldSrc links it into
        // the visibility leaves without linking it into solids or triggers.
        MAKE_STRING_CLASS("csol_zombie_gib", gib->pev);
        g_zombieGibClass = gib->pev->classname;
        gib->pev->solid = SOLID_NOT;
        gib->SetTouch(nullptr);
        gib->m_bloodColor = BLOOD_COLOR_RED;
        gib->m_cBloodDecals = 3;
        gib->SetThink(&CBaseEntity::SUB_Remove);
        gib->pev->nextthink = gpGlobals->time + 3.0f;
        const float yaw = rotation + (6.2831853f / static_cast<float>(count)) * static_cast<float>(i);
        const float speed = RANDOM_FLOAT(115.0f, 215.0f);
        gib->pev->velocity = {std::cos(yaw) * speed, std::sin(yaw) * speed,
            RANDOM_FLOAT(155.0f, 240.0f) + (skull ? 32.0f : 0.0f)};
        gib->pev->avelocity = {RANDOM_FLOAT(100.0f, 220.0f), RANDOM_FLOAT(120.0f, 300.0f), 0};
        SET_ORIGIN(edict, origin);
        if (!guard.Current() || pieces[i].Get() != edict || (edict->v.flags & FL_KILLME)) break;
        if (i + 1 == count) return true;
    }

    // An allocation or engine callback can retire a piece during setup.
    // Remove the partial group before falling back to the temporary models.
    if (guard.Current())
        for (auto& piece : pieces)
            if (auto* gib = piece.Get<CBaseEntity>()) UTIL_Remove(gib);
    return false;
}
#endif
}

bool CSOL_ZombieGibImpact(edict_t* surface, edict_t* moving)
{
#ifdef REGAMEDLL_API
    if (g_zombieGibsAlive && moving && !moving->free && moving->v.classname == g_zombieGibClass)
    {
        if (auto* gib = dynamic_cast<CCSOLZombieGib*>(GET_PRIVATE<CBaseEntity>(moving)))
        {
            gib->StainImpact(surface);
            return true;
        }
    }
#endif
    return false;
}

bool CCSOLScenarioMessages::SendZombieGibs(const Vector& origin, CSOLZombieGibModels models,
    int count, const CSOLMonsterEffectGuard& guard) const
{
    if (g_scenarioMessageOpen || models.flesh <= 0 || models.flesh > 32767 ||
        models.skull < 0 || models.skull > 32767 || models.legBone < 0 || models.legBone > 32767 ||
        (count != 4 && count != 5) || !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteCoord || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort)
        return false;
    std::int16_t x{}, y{}, z{}, skullZ{};
    if (!csol::cso_tent::detail::Coord(origin.x, x) ||
        !csol::cso_tent::detail::Coord(origin.y, y) ||
        !csol::cso_tent::detail::Coord(origin.z, z) ||
        (models.skull && !csol::cso_tent::detail::Coord(origin.z + 16.0f, skullZ))) return false;

    if (!guard.Current()) return false;
    const ScenarioMessageScope scope;
    const bool blood = gpGlobals && g_engfuncs.pfnCVarGetFloat && g_engfuncs.pfnRandomLong &&
        ZombieGibBloodEnabled();
#ifdef REGAMEDLL_API
    if (blood && SpawnBloodGibs(origin, models, count, guard))
    {
        SendGibBurstBlood(origin, guard);
        return guard.Current();
    }
#endif
    if (!guard.Current()) return false;
    const auto emit = [&](int model, int pieces, std::int16_t height, const Vector& extent,
                          int lift, int spread)
    {
        MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, origin);
        WRITE_BYTE(TE_BREAKMODEL);
        WRITE_COORD(x * .125f); WRITE_COORD(y * .125f); WRITE_COORD(height * .125f);
        WRITE_COORD(extent.x); WRITE_COORD(extent.y); WRITE_COORD(extent.z);
        WRITE_COORD(0); WRITE_COORD(0); WRITE_COORD(lift);
        WRITE_BYTE(spread); // Variation in tens of units/sec.
        WRITE_SHORT(model);
        WRITE_BYTE(pieces);
        WRITE_BYTE(30); // Three seconds.
        WRITE_BYTE(BREAK_FLESH);
        MESSAGE_END();
    };
    // Each stock packet selects bodies from a single model. Send the two
    // distinctive pieces first so random body selection can't omit either.
    if (models.skull)
    {
        emit(models.skull, 1, skullZ, {10, 10, 10}, 235, 13);
        if (!guard.Current()) return false;
        --count;
    }
    if (models.legBone)
    {
        emit(models.legBone, 1, z, {18, 18, 20}, 210, 15);
        if (!guard.Current()) return false;
        --count;
    }
    emit(models.flesh, count, z, {30, 30, 32}, 195, 18);
    if (blood && guard.Current()) SendGibBurstBlood(origin, guard);
    return guard.Current();
}

bool CCSOLScenarioMessages::SendTeddyTentacleDebris(const float* origin, int modelIndex, int count) const
{
    if (g_scenarioMessageOpen || !origin || modelIndex <= 0 || modelIndex > 32767 ||
        (count != 3 && count != 4) || !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteCoord)
        return false;
    // The original MP passes the real entity origin to MSG_PVS, while the
    // host codec truncates each payload coordinate to int32(origin * 8) and
    // writes its low 16 bits.  Do not apply the old signed-short range guard:
    // it rejects finite positions that the original client can receive.
    std::int16_t coordinates[3]{};
    for (int axis = 0; axis != 3; ++axis)
        if (!csol::cso_tent::detail::Coord(origin[axis], coordinates[axis])) return false;
    // MP113E9760 -> 113E7810. The caller consumes RandomLong(3,4) at
    // its original position; serialization neither draws RNG nor looks up a model.
    const Vector position(origin);
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, position);
    WRITE_BYTE(TE_BREAKMODEL);
    WRITE_COORD(static_cast<float>(coordinates[0]) * .125f);
    WRITE_COORD(static_cast<float>(coordinates[1]) * .125f);
    WRITE_COORD(static_cast<float>(coordinates[2]) * .125f);
    WRITE_COORD(6);
    WRITE_COORD(6);
    WRITE_COORD(2);
    WRITE_COORD(5);
    WRITE_COORD(5);
    WRITE_COORD(40);
    WRITE_BYTE(5);
    WRITE_SHORT(modelIndex);
    WRITE_BYTE(count);
    WRITE_BYTE(3); // Original truncation of .3 seconds * 10.
    WRITE_BYTE(64);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendTeddyBlackholeDebris(const float* origin, int modelIndex) const
{
    if (g_scenarioMessageOpen || !origin || modelIndex <= 0 || modelIndex > 32767 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte ||
        !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteCoord) return false;
    Vector encoded;
    for (unsigned axis = 0; axis != 3; ++axis)
    {
        std::int16_t coordinate{};
        if (!csol::cso_tent::detail::Coord(origin[axis], coordinate)) return false;
        encoded[axis] = static_cast<float>(coordinate) * .125f;
    }
    const Vector position(origin); // PVS routing retains the unquantized snapshot.
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, position);
    WRITE_BYTE(TE_BREAKMODEL);
    WRITE_COORD(encoded.x); WRITE_COORD(encoded.y); WRITE_COORD(encoded.z);
    WRITE_COORD(150); WRITE_COORD(150); WRITE_COORD(2);
    WRITE_COORD(0); WRITE_COORD(0); WRITE_COORD(150);
    WRITE_BYTE(5);
    WRITE_SHORT(modelIndex);
    WRITE_BYTE(20);
    WRITE_BYTE(5);
    WRITE_BYTE(64);
    MESSAGE_END();
    return true;
}

bool CCSOLScenarioMessages::SendTeddyBlackholeSmoke(const float* origin) const
{
    if (g_scenarioMessageOpen || !m_messageId || !origin || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong ||
        !g_engfuncs.pfnWriteCoord || !g_engfuncs.pfnWriteString) return false;
    Vector encoded;
    for (unsigned axis = 0; axis != 3; ++axis)
    {
        std::int16_t coordinate{};
        if (!csol::cso_tent::detail::Coord(origin[axis], coordinate)) return false;
        encoded[axis] = static_cast<float>(coordinate) * .125f;
    }
    const int id = m_messageId;
    const ScenarioMessageScope scope;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(88);
    WRITE_STRING(CSOLTeddyBlackholeSmokeSprite);
    WriteRawFloat(100.0f);
    WriteRawFloat(std::bit_cast<float>(std::uint32_t{0x3E32B8C0}));
    WriteRawFloat(40.0f);
    WRITE_LONG(5);
    WriteRawFloat(.09f);
    WriteRawFloat(1.0f);
    WRITE_LONG(50); WRITE_LONG(70);
    WriteRawFloat(.02f);
    WriteRawFloat(.5f);
    WRITE_LONG(2); WRITE_LONG(5);
    WRITE_LONG(10);
    WRITE_LONG(1);
    WRITE_LONG(0x34);
    WRITE_LONG(36);
    WriteRawFloat(50.0f);
    WRITE_BYTE(150); WRITE_BYTE(150); WRITE_BYTE(150);
    WRITE_COORD(encoded.x); WRITE_COORD(encoded.y); WRITE_COORD(encoded.z);
    MESSAGE_END();
    return true;
}

void CCSOLScenarioMessages::WarnVisualFailure(CSOLScenarioVisual visual, const char* detail) const
{
    static constexpr const char* names[]{
        "Boss skill hint", "Boss skill start", "Tentacle model", "Tentacle debris", "Tentacle shake",
        "Boss impact", "Boss blood", "Boss blood screen", "Teddy ground", "Teddy attack model",
        "Teddy sparks", "Teddy Meteor", "Teddy blackhole", "Teddy slide", "Oberon debris"
    };
    static_assert(std::size(names) == static_cast<unsigned>(CSOLScenarioVisual::Count) && std::size(names) <= 32);
    const auto index = static_cast<unsigned>(visual);
    if (index >= std::size(names) || !g_engfuncs.pfnAlertMessage) return;
    const auto mask = std::uint32_t{1} << index;
    if (m_visualWarnings & mask) return;
    // Publish before ALERT: a plugin callback can recurse or retire this map.
    m_visualWarnings |= mask;
    ALERT(at_console, "CSOL: optional visual unavailable [%s]: %s; gameplay continues (reported once per map)\n",
        names[index], detail ? detail : "effect message could not be sent");
}
