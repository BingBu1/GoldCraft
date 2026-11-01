// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

struct edict_s;
namespace csol::abilities { struct Snapshot; }
namespace csol::oberon_hint { struct Event; }
namespace csol::frozen_death { struct IceEvent; }
namespace csol::frozen_smoke { struct Event; }

// Register while preparing the selected scenario, before sending events. The
// engine owns user-message IDs; never retain a guessed ID from the original DLL.
class CCSOLScenarioMessages
{
public:
    bool Register();
    bool SendWaveEvent(std::span<const std::uint8_t, 4> payload) const;
    // Original MP11385F80: 27/3, one-based round, total rounds, and a raw
    // float32 game elapsed time. A recipient is used for authoritative replay.
    bool SendRoundClock(int round, int total, float elapsed,
        edict_s* recipient = nullptr) const;
    bool PlayerClipsReady() const { return m_playerClipMessageId != 0; }
    bool SendPlayerClip(std::span<const std::uint8_t> payload, edict_s* recipient = nullptr) const;
    // Original Boss selection: MPToCL { BYTE27, BYTE7, SHORT id }. There is
    // no trailing string; the caller suppresses the -1 sentinel.
    bool SendBossSkillStart(std::int16_t id) const;
    // Original rules354 visual subset: caller resolves one valid recipient
    // and rechecks its map/monster lease between packets.
    bool SendOberonHint(edict_s* recipient, const csol::oberon_hint::Event& event) const;
    // CSO TE147 is engine-private; preserve its frozen-death effect through
    // a dedicated host message, scoped to the original center's PVS.
    bool SendFrozenIce(const csol::frozen_death::IceEvent& event) const;
    bool SendFrozenSmoke(const csol::frozen_smoke::Event& event) const;
    bool SendBossParticipation(edict_s* recipient, int rank, int playerIndex,
        std::int32_t hundreds, int percent) const;
    bool SendBossHealthInit(int appearance, std::int32_t maximum, int entityIndex,
        edict_s* recipient = nullptr) const;
    // CSO PF_WriteFloat_I carries a raw float32 ratio; stock WRITE_COORD
    // would change both its byte length and its precision.
    bool SendBossHealthRatio(int appearance, float health, float maximum, int entityIndex,
        edict_s* recipient = nullptr) const;
    bool SendRoundClearSound() const;
    // Adapt the original final-clear TextMsg to stock CS's single-byte
    // destination format. CSO's extra three bytes require its own decoder.
    bool SendAllRoundsClear(int textMessageId) const;
    bool SendRoundOutcome(bool success) const;
    // Original MPToCL 27/4: byte slot, raw float32 cooldown, signed short count.
    // Call only with the authoritative inventory result; sending is not a debit.
    bool SendConsumableState(edict_s* recipient, unsigned index, float cooldown, int count) const;
    // HudIcon is a shared 16-bit mask. The owner supplies the whole mask.
    bool SendConsumableVisibility(edict_s* recipient, std::uint16_t mask) const;
    bool SendRespawnCountdown(edict_s* recipient, bool automatic, int seconds) const;
    bool SendRespawnUnavailable(edict_s* recipient) const;
    bool SendRevivalHint(edict_s* recipient, bool retry) const;
    // Original ZBSVer2 0x1C displays channel 6's "revival not cooled" text.
    bool SendRespawnReloadWait(edict_s* recipient, int seconds) const;
    // Original MPToCL 27/15 and 27/16 are all-client broadcasts.
    bool SendRetryCountdown(int seconds) const;
    bool SendRetryAnnouncement(int playerIndex, int oneBasedRound) const;
    bool SendRoundRewardState(bool pastRewardRound) const;
    // Original10828CC0: ZBSVer2 { BYTE0x18 (24), STRING, five signed LONGs }.
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
    bool SendUpgradeExperience(edict_s* recipient, int percent) const;
    bool SendUpgradeStat(edict_s* recipient, unsigned stat, int level, int cap) const;
    // Original active-ability initialization/activation: 0x24, SHORT subtype,
    // two raw IEEE754 float32 values. Initial second duration is zero.
    bool SendActiveSkillCooldown(edict_s* recipient, int subtype, float cooldown, float duration) const;
    bool SendActiveSkillSnapshot(edict_s* recipient, const csol::abilities::Snapshot&) const;
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
    // Original ordinary heal skill: MPToCL/4 in the healed entity's PVS,
    // SHORT entity and BYTE effect (13 self, 9 another monster).
    bool SendMonsterHealing(int monsterIndex, const float* origin, bool self) const;
    bool SendMonsterDisarmImpact(const float* origin) const;
    bool SendMonsterSkillShake(edict_t* recipient) const;
    bool SendMonsterTrapDirection(edict_t* recipient, const float* origin) const;
    // One packet per call: false sends the original view kick (3), true
    // sends the original stun sound (15/3). The attack owner checks the
    // recipient's life and session between kick, ScreenShake and sound.
    bool SendOberonImpact(edict_s* recipient, bool stun) const;
    bool SendOberonDebris(const float* origin, const float* size,
        bool landing, int modelIndex) const;
    bool SendOberonExplosion(const float* origin, const float* visualOrigin, int modelIndex) const;
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
    bool SendRespawnText(edict_s* recipient, const char* token, const char* argument) const;
    int m_messageId{};
    int m_upgradeMessageId{};
    int m_statusMessageId{};
    int m_skillSnapshotId{};
    int m_monsterEffectId{};
    int m_playerClipMessageId{};
    int m_frozenIceMessageId{};
    int m_frozenSmokeMessageId{};
    int m_consumableIconMessageId{};
    int m_respawnTextMessageId{};
    int m_respawnHintMessageId{};
};
