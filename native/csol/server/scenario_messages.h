// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

struct edict_s;

// Register while preparing the selected scenario, before sending events. The
// engine owns user-message IDs; never retain a guessed ID from the original DLL.
class CCSOLScenarioMessages
{
public:
    bool Register();
    bool SendWaveEvent(std::span<const std::uint8_t, 4> payload) const;
    // Original death notification: MSG_ONE, byte 200, entity-index short.
    // The death owner enumerates valid client slots, including dead players
    // and both teams, and calls this before beforeDeath/animation. This method
    // sends one packet; it does not replace that owner traversal with MSG_ALL.
    bool SendMonsterDeath(edict_s* recipient, int monsterIndex) const;
    // Original 0x61 attachment removal, broadcast on MPToCL. Model indices
    // and built-in effect IDs use distinct trailing flags (0 and 1).
    bool SendMonsterEffectRemoval(int monsterIndex, int effectId, bool builtIn) const;
    bool SendUpgradeTotal(edict_s* recipient, int level, bool increased) const;
    bool SendUpgradeStat(edict_s* recipient, unsigned stat, int level, int cap) const;
    // Original ordinary skill12: 0x51, short10, float1, RGB255. The float is
    // four raw bytes, not GoldSrc's quantized WriteCoord representation.
    bool SendMonsterContactEffect(edict_s* recipient) const;
    // Original broadcast 27/2 uses the low 16 bits of the truncated frags.
    bool SendPlayerFrags(int playerIndex, std::int32_t frags) const;
    // Mode15 classification produces at most six candidates per call.
    // NoRibbon affects account submission only, never this client message.
    bool SendAnnounceRibbons(edict_s* recipient, std::span<const std::uint8_t> ids) const;
    int MessageId() const { return m_messageId; }
private:
    friend class CCSOLScenarioDeathNotice;
    // The traversal resolves OFFSET and checks its session immediately after
    // that engine callback. This private entry performs no second lookup.
    bool WriteMonsterDeath(edict_s* recipient, int monsterIndex) const;
    int m_messageId{};
    int m_upgradeMessageId{};
};
