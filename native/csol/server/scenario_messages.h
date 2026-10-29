// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

struct edict_s;

// Register while preparing the selected scenario, before sending events. The
// engine owns user-message IDs; never retain a guessed ID from the original DLL.
class CCSOLScenarioMessages
{
public:
    bool Register();
    bool SendWaveEvent(std::span<const std::uint8_t, 4> payload) const;
    // Original Boss selection: MPToCL { BYTE27, BYTE7, SHORT id }. There is
    // no trailing string; the caller suppresses the -1 sentinel.
    bool SendBossSkillStart(std::int16_t id) const;
    bool SendRoundClearSound() const;
    // Adapt the original final-clear TextMsg to stock CS's single-byte
    // destination format. CSO's extra three bytes require its own decoder.
    bool SendAllRoundsClear(int textMessageId) const;
    bool SendRoundOutcome(bool success) const;
    bool SendRoundRewardState(bool pastRewardRound) const;
    // Original10828CC0: ZBSVer2 { BYTE18, STRING, five signed LONGs }.
    // The result owner supplies verified statistics, never guessed defaults.
    // A stock GoldSrc user message allows at most192 payload bytes.
    bool SendFinalResult(edict_s* recipient, std::string_view text,
        std::span<const std::int32_t, 5> values) const;
    // The native CS scoreboard consumes CT/TERRORIST. CSO's C/T labels
    // need client mapping before they can replace these native team names.
    bool SendNativeTeamScore(int messageId, bool ct, std::int16_t score) const;
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
    // Original head-hit statistic: ZBSVer2 { BYTE 2, LONG signed count }.
    bool SendMonsterHeadHits(edict_s* recipient, std::int32_t count) const;
    // Original kill chain: ZBSVer2 { BYTE 1, LONG signed count }.
    bool SendMonsterKillChain(edict_s* recipient, std::int32_t count) const;
    // MPToCL/199: short entity, byte subtype, two raw IEEE754 float32s.
    // The caller checks OFFSET and its owner before this no-lookup write.
    bool SendMonsterDamageEffect(edict_s* recipient, int monsterIndex,
        int subtype, float height, float magnitude) const;
    // Self recovery: MPToCL/206, style6, flags4, truncated positive delta
    // (which may be zero). The policy resolves the current player index and
    // validates its owner before calling; no entity lookup occurs in a packet.
    bool SendRecoveryNumber(edict_s* recipient, int playerIndex, std::int32_t amount) const;
    // Original ClassMod type2 damage notification: MPToCL { AA, player, D }.
    // D is signed and may be zero/negative; the caller owns float truncation
    // and resolves the recipient/index before opening the packet.
    bool SendClassModMonsterDamage(edict_s* recipient, int playerIndex, std::int32_t amount) const;
    // Original damage-buff hint has one byte only, reliably to the attacker.
    bool SendWeaponDamageBuffHint(edict_s* recipient) const;
    // Original ordinary skill12: 0x51, short10, float1, RGB255. The float is
    // four raw bytes, not GoldSrc's quantized WriteCoord representation.
    bool SendMonsterContactEffect(edict_s* recipient) const;
    // Original broadcast 27/2 uses the low 16 bits of the truncated frags.
    bool SendPlayerFrags(int playerIndex, std::int32_t frags) const;
    // Mode15 classification produces at most six candidates per call.
    // NoRibbon affects account submission only, never this client message.
    bool SendAnnounceRibbons(edict_s* recipient, std::span<const std::uint8_t> ids) const;
    // MPToCL2/0x40 uses globally allocated IDs in each player's label list.
    // A duration is a raw IEEE754 float, including the permanent sentinel.
    bool SendStatusLabel(edict_s* recipient, std::int32_t label, bool style,
        std::int32_t type, float remaining) const;
    bool RemoveStatusLabel(edict_s* recipient, std::int32_t label) const;
    bool ClearStatusLabels(edict_s* recipient) const;
    int MessageId() const { return m_messageId; }
private:
    friend class CCSOLScenarioDeathNotice;
    // The traversal resolves OFFSET and checks its session immediately after
    // that engine callback. This private entry performs no second lookup.
    bool WriteMonsterDeath(edict_s* recipient, int monsterIndex) const;
    int m_messageId{};
    int m_upgradeMessageId{};
    int m_statusMessageId{};
};
