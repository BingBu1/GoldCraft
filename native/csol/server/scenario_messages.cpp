// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_messages.h"

#include <bit>
#include <algorithm>
#include <array>

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
    if (!g_engfuncs.pfnRegUserMsg) return false;
    const int id = REG_USER_MSG("MPToCL", -1);
    // GoldSrc transmits a user-message number as a byte. A failed registration
    // must not make MESSAGE_BEGIN send an engine service opcode instead.
    if (id < 64 || id > 255) return false;
    const int upgrades = REG_USER_MSG("ZBSVer2", -1);
    if (upgrades < 64 || upgrades > 255) return false;
    m_messageId = id;
    m_upgradeMessageId = upgrades;
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
