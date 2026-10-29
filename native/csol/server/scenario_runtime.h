// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <chrono>
#include <string>

class CCSOLScenarioSession;
class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioDrops;
class CCSOLScenarioGameplay;
class CCSOLScenarioDeathServices;
class CCSOLScenarioMonsterServices;
class CCSOLMonsterDamageServices;
class CSOLScenarioRecoveryNotice;
class CSOLScenarioSdkScope;
namespace csol::scenario { class TriggerWaveEvents; }

struct CSOLScenarioCtCounts
{
    int alive{}, total{};
    bool valid{};
};

struct CSOLScenarioRoomOptions
{
    // v1C4's default is not established. Required only for an equipped money
    // extractor; absence must not fabricate a room option of zero.
    std::optional<int> moneyOption;
    bool noRibbon{};
    // Local room option, initialized like the proven Options constructor's
    // byte408 write. This is mutable server authority, not a claim that CSO's
    // later option writers are restored or that floating damage is always on.
    std::uint8_t floatingDamageDisplay{1};
};

struct CSOLScenarioArchiveMap
{
    std::string name;
    std::optional<int> id;
};

// C++ services have a lifetime independent of the raw SDK rules pointer.
// Entity access is revoked immediately; releasing borrowed resources is a
// separate operation performed only at a post-engine-free SDK boundary.
class CCSOLScenarioRuntime final
{
public:
    CCSOLScenarioRuntime();
    void RequestClose();
    bool TryFinalize();
    bool ReadyForArchive() const;
    const std::optional<CSOLScenarioArchiveMap>& ArchiveMap() const { return m_archiveMap; }
    // Cached before entity access is revoked. The archive stores local values
    // only and keeps unknown account rewards pending, never delivered.
    void SetArchiveRoot(std::string directory);
    // Original toggle order: publish switch -> live CT effects -> live map
    // targets. A failed/cancelled operation is not retried as a same-value
    // update; its caller must reject or retire the incomplete rule instance.
    bool SetClearPointProtection(bool enabled);
    bool Closing() const { return m_closing; }
    std::uint64_t Generation() const { return m_generation; }

    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<CCSOLScenarioDrops> drops;
    std::shared_ptr<CCSOLScenarioGameplay> gameplay;
    std::shared_ptr<CCSOLScenarioDeathServices> deathServices;
    std::shared_ptr<CCSOLScenarioMonsterServices> monsterServices;
    // The concrete damage stages are installed before Precache chooses its
    // resource scope. Gameplay is assembled only after those tables/models
    // load; unsupported selected abilities/Boss paths report their own errors.
    std::shared_ptr<CCSOLMonsterDamageServices> monsterDamageServices;
    // Positive recovery requires a concrete notification policy. Unknown
    // special-character state remains an explicit error, not a successful skip.
    std::shared_ptr<CSOLScenarioRecoveryNotice> recoveryNotice;
    std::shared_ptr<csol::scenario::TriggerWaveEvents> waveEvents;
    // One InitializePlayerCounts snapshot. Drops must not substitute
    // spawnable capacity or re-scan halfway through a death callback.
    CSOLScenarioCtCounts ctCounts;
    // Authoritative local room state, separate from individual loadouts.
    CSOLScenarioRoomOptions room;
    // Original manager+135A, initialized/reset independently of +1359's wave
    // dispatch stop and the root's damage-suppression/termination bit.
    bool monsterLogicStopped{};

private:
    friend class CSOLScenarioSdkScope;
    std::size_t m_operations{};
    std::uint64_t m_generation{1};
    bool m_closing{}, m_finalizing{}, m_finalized{};
    std::string m_archiveRoot, m_archiveDirectory, m_archiveError;
    bool m_archiveSaved{}, m_shutdownPrepared{};
    std::optional<CSOLScenarioArchiveMap> m_archiveMap;
    std::chrono::steady_clock::time_point m_archiveRetry{};
};
