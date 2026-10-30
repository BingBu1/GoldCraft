// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_session.h"
#include "../scenario/feature3.hpp"
#include <optional>
#include <list>
#include <tuple>
#include <utility>

class CSOLMonsterDeathCall;
class CSOLMonsterDamageCall;
class CSOLPlayerRecoveryCall;
class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioRuntime;
struct CSOLMonsterDeathContext;

// A trusted ClassMod loadout installs the actual ability object. The character
// feature byte is not this object, and an empty local loadout does not claim
// that the original character's default ability has been restored.
class CSOLScenarioActiveSkill
{
public:
    virtual ~CSOLScenarioActiveSkill() = default;
    virtual int Type() const = 0;
    virtual bool HudSnapshot(float, csol::abilities::Snapshot&) const { return false; }
    virtual bool Ready(CSOLMonsterDeathCall& call, bool& ready,
        csol::scenario::ConfigError& error) = 0;
    // Owns the second readiness check, probability, cooldown and real effects.
    virtual bool Activate(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) = 0;
};

// The same modern-player ability (+D8), with its distinct damage virtuals.
// A death-only implementation is not evidence that these methods are empty.
// Each callback may replace the registered ability; callers retain only the
// object required by the original call site and re-read the next one.
class CSOLScenarioDamageSkill : public CSOLScenarioActiveSkill
{
public:
    virtual bool ActiveForMonsterHit(CSOLMonsterDamageCall& call, bool& active,
        csol::scenario::ConfigError& error) = 0; // v10, not readiness vC.
    virtual bool ActiveMonsterHit(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error) = 0; // Actual type12 area effect.
    virtual bool AutoActivateOnBossDamage(CSOLMonsterDamageCall& call, bool& enabled,
        csol::scenario::ConfigError& error) = 0; // v14.
    virtual bool ActivateOnBossDamage(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error) = 0; // Full activation, including its readiness check.
    // amount is the original by-value argument. Context remains borrowed
    // through call, so later stages observe legitimate context mutations.
    virtual bool BossDamaged(CSOLMonsterDamageCall& call, float amount,
        csol::scenario::ConfigError& error) = 0; // v20.
    virtual bool MonsterDamaged(CSOLMonsterDamageCall& call, float amount,
        csol::scenario::ConfigError& error) = 0; // v24.
    virtual bool UsesDamageProgress(CSOLMonsterDamageCall& call, bool& enabled,
        csol::scenario::ConfigError& error) = 0; // v1C, not completion.
    virtual bool AdvanceDamageProgress(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error) = 0; // Increment and actual ZBSVer2/0x26 notification.
};

enum class CSOLScenarioKillSourcePhase { Attribution, Notification };
struct CSOLScenarioKillSource
{
    std::int32_t weapon{}, item{};
};

class CSOLScenarioKillComponent
{
public:
    virtual ~CSOLScenarioKillComponent() = default;
    virtual bool MonsterKilled(CSOLMonsterDeathCall& call, std::int32_t source,
        csol::scenario::ConfigError& error) = 0;
    // Original player+3478 is one registry, shared by kill v98 and recovery
    // v6C. A component must implement both; absence is not a successful no-op.
    virtual bool HealthRecovered(CSOLPlayerRecoveryCall& call, float amount,
        CSOLMonsterDeathContext& context, csol::scenario::ConfigError& error) = 0;
};

// The same player module registry broadcasts v104 after cloaking changes.
// Modules without an implemented v104 must not silently acknowledge it.
class CSOLScenarioCloakingObserver
{
public:
    virtual ~CSOLScenarioCloakingObserver() = default;
    virtual bool CloakingChanged(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error) = 0;
};

// Original external event7 has two positional values: [true, attacker].
// Handles/generations replace the private process pointer for local services;
// the metadata is lifetime validation, not extra original payload values.
struct CSOLScenarioMonsterHitEvent
{
    std::tuple<bool, EHANDLE> payload;
    std::uint64_t playerGeneration{}, mapGeneration{}, roundGeneration{};
};

class CSOLScenarioMonsterHitEvents
{
public:
    virtual ~CSOLScenarioMonsterHitEvents() = default;
    virtual bool Submit(int playerIndex, int event, const CSOLScenarioMonsterHitEvent& value,
        csol::scenario::ConfigError& error) = 0;
};

// The concrete notification owner supplies the original v720 configuration /
// mode-list gate, source validation, v3EC and the 206 writer. It may decline a
// notification according to those rules, but may not invent a default allow.
// Called only for positive actual recovery, after health has changed. Normal
// kind7 recovery uses the attacking player as both recipient and target.
// Return true when handled, including a normal decision not to send; false
// means host failure/cancellation and stops the remaining damage stages.
// The pure owner predicate belongs to the enclosing weapon operation. Check
// it after each notification callback, before looking up or sending anything
// else: the damage call alone does not own that weapon directory. The context
// is borrowed only for this synchronous call and must never be retained.
class CSOLScenarioRecoveryNotice
{
public:
    virtual ~CSOLScenarioRecoveryNotice() = default;
    virtual bool NotifySelfRecovery(CSOLPlayerRecoveryCall& call, float amount,
        CSOLMonsterDeathContext& context, csol::scenario::ConfigError& error,
        bool (*ownerValid)(const void*), const void* ownerContext) = 0;
};

struct CSOLScenarioKillSubscription
{
    std::uint64_t token{};
    bool enabled{};
    std::shared_ptr<CSOLScenarioKillComponent> component;
};

struct CSOLScenarioKillPosition
{
    std::uint64_t sequence{}, mapGeneration{}, roundGeneration{}, playerGeneration{};
    std::array<float, 3> origin{};
    // This host's local identity, not the original opaque account field.
    std::uint32_t localIdentity{};
    std::uint16_t team{}, source{};
    bool player{}, killed{true};
};

// Ordered local submissions from the kill-chain milestones. EventC6 has an
// explicitly empty payload; Ribbon44 is the filtered original kind6 result.
// Neither record asserts an official account identity or external award.
struct CSOLScenarioKillChainRecord
{
    enum class Kind { Ribbon44, EventC6 };
    Kind kind{};
    std::uint64_t sequence{}, playerGeneration{}, mapGeneration{}, roundGeneration{};
    std::uint32_t localIdentity{};
};

// Local dedicated-server authority. These fields are owned state, not guesses
// based on a model name, current weapon, team or client-provided message.
struct CSOLScenarioPlayerProfile
{
    csol::scenario::EquipmentLoadout equipment;
    std::uint8_t characterFeature{};
    bool elevenLevelLimit{}, moneyExtractor{};
    // Most consumers test nonzero; the original 0xAA sender skips exactly1.
    std::uint8_t weaponEffectsBlocked21F2{};
    bool weaponEffectsBlocked34F0{};
    // No value preserves the existing selection. Present values must have
    // an implemented knife/HE identity; this is never a client-provided ID.
    std::optional<std::array<std::int32_t, 2>> selectedWeapons;
};

struct CSOLScenarioStatusLabel
{
    std::int32_t id{}, type{}, group{};
    float deadline{}, createdAt{};
    bool style{};
};

class CSOLScenarioBuffCondition
{
public:
    virtual ~CSOLScenarioBuffCondition() = default;
    virtual bool Evaluate(CSOLMonsterDamageCall& call, bool& applies,
        csol::scenario::ConfigError& error) = 0;
};

// One authoritative StatBuff collection serves protection and damage queries.
// Field names describe verified consumers, not the original private ABI.
struct CSOLScenarioStatBuff
{
    std::uint64_t token{};
    std::int32_t group{}, source{}, subsource{};
    std::uint32_t mask{};
    float expires{};
    bool protection{}, renderHeld{}, criticalEnabled{};
    float criticalChance{}, criticalMultiplier{}, attackRatio{};
    std::shared_ptr<CSOLScenarioBuffCondition> condition;
    std::vector<std::int32_t> labels;
    bool Active(float now) const { return !(expires > 0 && now >= expires); }
};

// A trusted producer binds the actual live ability state. Missing this owner
// means unknown; a present owner with an empty optional/null feature means
// the producer has confirmed that the corresponding key is absent. This is
// not inferred from a character model or the kill-event enabled flag.
struct CSOLScenarioDisplayAbilities
{
    std::optional<std::uint8_t> feature28;
    std::optional<float> feature29;
    std::shared_ptr<csol::scenario::Feature3> feature3;
};

// Local dedicated-server provenance, not an original account/network field.
// Explicit profiles, including incomplete or empty imports, never fall back
// to defaults merely because a character is changed or Prepare runs again.
enum class CSOLScenarioClassModSource { Uninitialized, HostDefault, ExplicitProfile };

// Actual module12 constructor initializes these to0. Names retain the known
// field identities until their native producers are restored. Repeated
// Prepare/profile/character changes do not implicitly clear module state.
struct CSOLScenarioAddonDamageInputs
{
    std::uint8_t state11{};
    std::int32_t count18{};
    float deadline1C{};
    std::int32_t count48{};
};

// Pure local values, scoped by archive directory and player generation.
struct CSOLScenarioPlayerStatistics
{
    std::uint64_t generation{}, capEventSequence{};
    int classId{-1};
    bool capEventPending{};
    std::map<std::int32_t, std::uint32_t> weaponKills;
    std::map<std::int32_t, float> weaponDamage;
    std::uint32_t headHits{}, damageHits{}, killChain{}, bestKillChain{};
    float bossDamage{}, ordinaryDamage{}, lastKillAt{};
    csol::scenario::AnnounceCounts classification;
};

struct CSOLScenarioPlayerRuntimeState
{
    // Allocate one empty node when admitting the player. Retirement moves
    // maps and splices this node without allocation or retaining an edict.
    std::list<CSOLScenarioPlayerStatistics> statisticsRetirement;
    bool statisticsRetired{};
    EHANDLE player;
    std::uint64_t generation{}, profileRevision{}, appliedRevision{};
    int classId{-1};
    CSOLScenarioPlayerProfile profile;
    CSOLScenarioClassModSource classModSource{CSOLScenarioClassModSource::Uninitialized};
    csol::scenario::ClassModCurrent classMods;
    std::uint64_t classModRevision{}, classModProfileRevision{};
    // Original service event 0x26 is retained locally until its consumer
    // acknowledges it. Recording it does not grant a commercial reward.
    bool capEventPending{};
    std::uint64_t capEventSequence{};
    bool preparing{};
    // Local map-session statistics, identified by this registry generation.
    // These records do not claim persistent official account credit.
    std::map<std::int32_t, std::uint32_t> weaponKills;
    // Separate original item-catalog damage table. This local map-session
    // record does not claim persistence under an official account identity.
    std::map<std::int32_t, float> weaponDamage;
    std::uint32_t monsterHeadHits{};
    float bossDamage{}, ordinaryMonsterDamage{};
    std::uint32_t monsterDamageHits{};
    // Original +40/+2C counts wrap at 32 bits and compare as signed values.
    // +3C is sampled again only after the chain's successful final stage.
    std::uint32_t monsterKillChain{}, bestMonsterKillChain{};
    float lastMonsterKillAt{};
    csol::scenario::AnnounceCounts classification;
    // Signed group order, then registration order within a group.
    std::vector<CSOLScenarioStatBuff> buffs;
    std::vector<CSOLScenarioStatusLabel> labels;
    std::vector<std::int32_t> publishedLabels;
    bool labelsDirty{}, updatingBuffs{};
    std::uint64_t buffsRevision{};
    // Constructor evidence: original +3518 is zero. Do not reset it on every
    // hit or profile update; real timer producers and Spawn resets are separate.
    double recoveryBlockedUntil{};
    // Original +2F18 has only pre-constructor calloc evidence. This special
    // limit needs a trusted producer; normal recovery uses pev->max_health.
    std::optional<float> specialRecoveryMaximum;
    // v3EC reads these only after the current 21F2 flag. Their production
    // writers are not yet restored; unknown is distinct from a zero value.
    std::optional<std::uint8_t> recoveryDisplayState221B;
    std::optional<float> recoveryDisplayDeadline2F38;
    std::shared_ptr<CSOLScenarioDisplayAbilities> displayAbilities;
    std::vector<CSOLScenarioKillSubscription> killComponents;
    std::shared_ptr<CSOLScenarioActiveSkill> activeSkill;
    std::uint64_t activeSkillRevision{};
    std::optional<csol::scenario::ClassModDefinition> activeSkillDefinition;
    int activeSkillClass{-1};
    bool activeSkillPrepared{};
    bool updatingHud{}, hudReplayPending{};
    CSOLScenarioAddonDamageInputs addonDamage;
    // Shared module flag written during pre-damage and consumed after health
    // subtraction. Early rejection preserves it, including nested hits.
    bool addonCriticalPending{};
    // The original player initializer clears its registry, then installs
    // modules42/39/13/20/26/40. Module26 therefore exists before the first hit.
    // Profile/Prepare refresh does not reconstruct this player-owned module.
    bool bloodSuckingRegistered{true}, bloodSuckingEnabled{true}, updatingBloodSucking{};
    float bloodSuckingPending{}, bloodSuckingCapOverride{};
};

// Recovery belongs to a player and map/round, not to a monster. Kind7 adds
// its enclosing hit/weapon predicate; deferred module26 recovery has no hit.
// Original source=self and source=null both notify the recovering player.
// Components receive only delta/context, with this call supplying ownership.
class CSOLPlayerRecoveryCall
{
public:
    CSOLPlayerRecoveryCall(std::shared_ptr<CCSOLScenarioRuntime> runtime, EHANDLE player,
        bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);
    CBasePlayer* Current() const;
    CBasePlayer* Player() const { return Current(); }
    std::shared_ptr<CCSOLScenarioRuntime> Runtime() const { return m_runtime; }
    const EHANDLE& PlayerHandle() const { return m_player; }
    bool Cancel() { m_cancelled = true; return false; }
    bool Cancelled() const { return m_cancelled; }
private:
    std::shared_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> m_players;
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> m_state;
    EHANDLE m_player;
    std::uint64_t m_runtimeGeneration{}, m_roundGeneration{};
    bool (*m_ownerValid)(const void*);
    const void* m_ownerContext{};
    bool m_cancelled{};
};

class CCSOLScenarioPlayerRuntime final : public std::enable_shared_from_this<CCSOLScenarioPlayerRuntime>
{
public:
    explicit CCSOLScenarioPlayerRuntime(std::shared_ptr<CCSOLScenarioSession> session)
        : m_session(std::move(session)) {}
    bool Activate();
    void Invalidate();
    bool Prepare(CBasePlayer& player);
    bool UpdateHud(CBasePlayer& player, float now, bool force);
    void Remove(const EHANDLE& player);
    // Trusted server code supplies inventory/ability state. No client command
    // calls this setter. Untouched local players receive default ClassMod
    // records on first Combat Prepare; an explicit profile takes precedence.
    bool SetProfile(CBasePlayer& player, CSOLScenarioPlayerProfile profile);
    // Trusted complete0xCC records, shared with the upgrade-cap consumer.
    // Import does not invent a default preset or reset a skill every frame.
    bool SetClassModRecords(CBasePlayer& player,
        std::vector<csol::scenario::EquipmentLoadout::ClassCaps> records);
    bool RefreshClassMods(const EHANDLE& player, int classId, csol::scenario::ConfigError& error);
    bool CurrentClassMod(const EHANDLE& player, int type,
        std::optional<csol::scenario::ClassModDefinition>& result, csol::scenario::ConfigError& error);
    bool CurrentClassModPair(const EHANDLE& player, int type, int subtype,
        std::optional<csol::scenario::ClassModDefinition>& result, csol::scenario::ConfigError& error);
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> Find(const EHANDLE& player) const;
    // Current character/effect-adjusted level, read by inventory rule consumers.
    bool ReadUpgradeStat(const EHANDLE& player, csol::scenario::UpgradeStat stat, float& value) const;
    // Disconnect detaches profiles before the SDK sets has_disconnected.
    // Distinguish that window from an active player never registered here.
    bool WasRemoved(const EHANDLE& player) const;
    bool AcknowledgeCapEvent(const EHANDLE& player, std::uint64_t generation, std::uint64_t sequence);
    // Player portion of the clearpoint switch, including its same-value gate.
    // The complete manager operation must also update zbstargets; this method
    // alone is not that operation. A new map never reuses a player generation.
    bool SetPlayerClearPointProtection(bool enabled);
    bool ClearPointProtectionEnabled() const { return m_clearPointProtection; }
    std::uint64_t ClearPointProtectionRevision() const { return m_protectionOperation; }
    bool ProtectionBlocksDamage(const EHANDLE& player, float now) const;
    bool UpdatePlayerBuffs(CBasePlayer& player, float now);
    // Trusted server producers only. Display/render ownership is installed by
    // its concrete producer, never accepted through this damage-data entry.
    std::uint64_t InstallStatBuff(const EHANDLE& player, CSOLScenarioStatBuff buff);
    bool RemoveStatBuff(const EHANDLE& player, std::uint64_t token);
    // Separate original phases: critical before weapon properties / attacked
    // notification; attack ratio after that notification, before hit rewards.
    bool ApplyCriticalBuffDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    bool ApplyAttackBuffDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    bool SetAddonDamageInputs(const EHANDLE& player, CSOLScenarioAddonDamageInputs inputs);
    bool ApplyAddonDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    bool FinishAddonDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    bool SetRecoveryBlockedUntil(const EHANDLE& player, double deadline);
    bool SetSpecialRecoveryMaximum(const EHANDLE& player, std::optional<float> maximum);
    bool SetRecoveryDisplayState(const EHANDLE& player, std::optional<std::uint8_t> state,
        std::optional<float> deadline);
    bool SetDisplayAbilities(const EHANDLE& player, std::shared_ptr<CSOLScenarioDisplayAbilities> abilities);
    bool Feature3Active(const EHANDLE& player, bool& active, csol::scenario::ConfigError& error,
        bool (*ownerValid)(const void*), const void* ownerContext);
    // Only the mode15 category6/7 gate, not the full original category API.
    bool SpecialRecoveryDisplayAllowed(const EHANDLE& player, bool& allowed,
        csol::scenario::ConfigError& error, bool (*ownerValid)(const void*), const void* ownerContext);
    // Consume the present value from PrepareAttributeDamage immediately, before
    // attacked effects and attack buffs. This is self recovery with bits=0 and
    // a new default context, not the monster hit's context. Zero/negative amounts
    // still take the successful recovery path when the original gates allow.
    // Return false for host failure/cancellation; applied distinguishes a normal
    // gate rejection from a health write. A later failure retains that write.
    // An optional pure validity predicate propagates the enclosing weapon
    // directory's retirement after notification and each component callback.
    bool ApplyAttributeRecovery(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, float amount, bool& applied,
        csol::scenario::ConfigError& error,
        bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);
    // Original v4C/v50 with bits0 and a new context. Used by kind7 (self
    // source) and module26 (null source, which falls back to self at notice).
    // No monster identity is fabricated for deferred player updates.
    bool ApplyRecovery(CSOLPlayerRecoveryCall& call, float amount, bool& applied,
        csol::scenario::ConfigError& error);
    bool AddBloodSucking(const EHANDLE& player, float amount);
    bool SetBloodSuckingCap(const EHANDLE& player, float percentage);
    bool SetBloodSuckingEnabled(const EHANDLE& player, bool enabled);
    // Original module26.v10, reached from PlayerPostThink's module dispatcher.
    // Uses current equipment at consumption time, not the hit's source gun.
    bool UpdateBloodSucking(CSOLPlayerRecoveryCall& call, csol::scenario::ConfigError& error);
    // Revoke owned render and clear protection/label records without callbacks
    // or messages. Rules shutdown calls this before revoking map access;
    // client state reset belongs to the separate client lifecycle.
    void ReleaseProtectionState();
    // Distinct original phases. The complete death consumer must call weapon
    // statistics after the character ability branch, frags after manager /
    // clearpoint, and classification only after attribution and item drops.
    bool RecordActiveWeaponKill(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error);
    // Separate damage stages: call the first after addon pre-damage, before
    // friendly-fire rejection; call the second after health subtraction and
    // addon post-damage, before kill-chain processing. Neither runs on death.
    // v1D8 captures source before modern ATK, experience and addon callbacks.
    bool RecordWeaponDamage(CSOLMonsterDamageCall& call, int capturedSource,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    // v1DC captures hitgroup before addon post-damage, which may change it.
    bool RecordMonsterHeadHit(CSOLMonsterDamageCall& call, int capturedHitgroup,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    // v1DC, after addon/head-hit handling and before damage/hit statistics.
    // Map IDs are authoritative rule snapshots for the original Boss predicate.
    bool RecordMonsterKillChain(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, int cachedMapId, int currentMapId,
        csol::scenario::ConfigError& error);
    std::span<const CSOLScenarioKillChainRecord> KillChainRecords() const { return m_killChainRecords; }
    // A local persistence consumer acknowledges a saved prefix, not an award.
    bool AcknowledgeKillChainRecords(std::uint64_t throughSequence);
    // Called after the rule's monster-manager callbacks. The first method
    // captures +D8 once; the second resolves the player state again and reads
    // the current ability at each original virtual-call boundary. boss and
    // amount must be captured by the enclosing v1DC operation, not inferred
    // again after these callbacks. The separate component/event methods below
    // follow statistics; the complete AfterHealthDamage owner calls them once.
    bool NotifyActiveSkillMonsterHit(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    bool StopCharacterCloaking(CSOLMonsterDamageCall& call,
        const CSOLScenarioActiveSkill* skill, csol::scenario::ConfigError& error);
    bool RecordMonsterDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, bool boss, float amount,
        csol::scenario::ConfigError& error);
    bool NotifyMonsterHitComponent(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    bool NotifyClassModMonsterDamage(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    bool NotifyMonsterHitEvent(CSOLMonsterDamageCall& call,
        std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error);
    // A null service is the original v134 absent branch, not a fabricated
    // successful remote submission. Installed services own real consumption.
    bool SetMonsterHitEvents(std::shared_ptr<CSOLScenarioMonsterHitEvents> events);
    bool SetActiveSkill(const EHANDLE& player, std::shared_ptr<CSOLScenarioActiveSkill> skill);
    // Original root v168: validated attacker/Classify17, separate knife-source
    // predicate, optional actual type10 ability, then freshly read active gun.
    bool RootMonsterKilled(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error);
    bool AddMonsterFrag(CSOLMonsterDeathCall& call, bool& incremented, csol::scenario::ConfigError& error);
    // Two separate samples at their original positions in the death chain.
    // Attribution resolves catalog identity; Notification observes context[20]
    // and never resolves the catalog. A zero source still permits notification.
    bool ResolveKillSource(CSOLMonsterDeathCall& call, CSOLScenarioKillSourcePhase phase,
        CSOLScenarioKillSource& output, csol::scenario::ConfigError& error);
    // Called after drops. The cached root selection and current room selection
    // are separate inputs even when this host keeps both fixed for one map.
    bool ClassifyMonsterKill(CSOLMonsterDeathCall& call, int cachedMapId, int currentMapId,
        csol::scenario::AnnounceCandidates& output, csol::scenario::ConfigError& error);
    // Target registry contract: duplicate object registration is idempotent;
    // newly added entries take part on the next dispatch. Cancellation and
    // enable changes apply immediately, including within nested callbacks.
    std::uint64_t RegisterKillComponent(const EHANDLE& player,
        std::shared_ptr<CSOLScenarioKillComponent> component);
    bool SetKillComponentEnabled(const EHANDLE& player, std::uint64_t token, bool enabled);
    bool RemoveKillComponent(const EHANDLE& player, std::uint64_t token);
    bool NotifyMonsterKill(CSOLMonsterDeathCall& call, std::int32_t source,
        csol::scenario::ConfigError& error);
    // key1 records are retained until a real consumer acknowledges its prefix.
    // This is not the original global observer Flush or persistent rewards.
    std::span<const CSOLScenarioKillPosition> KillPositions() const { return m_killPositions; }
    bool AcknowledgeKillPositions(std::uint64_t throughSequence);
    // Produces allowed IDs; the death consumer submits the external request
    // (unless room NoRibbon), then sends MPToCL124. No account grant occurs here.
    bool ClassifyKill(CSOLMonsterDeathCall& call, int kind,
        csol::scenario::AnnounceCandidates& output, csol::scenario::ConfigError& error);
    bool Active() const;
    // Pure cached values, usable after RequestClose has revoked entity access.
    std::vector<CSOLScenarioPlayerStatistics> StatisticsSnapshot() const;
    const csol::scenario::ConfigError& Error() const { return m_error; }

private:
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> Resolve(const EHANDLE& player) const;
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> Ensure(CBasePlayer& player);
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> ResolveDamageStatisticPlayer(CSOLMonsterDamageCall& call,
        const std::shared_ptr<CCSOLScenarioRuntime>& runtime, csol::scenario::ConfigError& error);
    bool RecordCapEvent(const EHANDLE& player);
    bool RefreshActiveSkill(const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
        int classId, csol::scenario::ConfigError& error);
    bool StopDamageCloaking(CSOLMonsterDamageCall& call,
        const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state, int key,
        csol::scenario::ConfigError& error);
    bool Fail(const char* reason);
    void RetainStatistics(CSOLScenarioPlayerRuntimeState& state) noexcept;
    bool AddClearPointProtection(CBasePlayer& player);
    bool RemoveProtectionSource(CSOLScenarioPlayerRuntimeState& state, CBasePlayer& player,
        std::int32_t source, std::int32_t subsource);
    bool RemoveProtectionAt(CSOLScenarioPlayerRuntimeState& state, CBasePlayer& player, std::size_t index);
    bool PublishLabels(const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state, float now);
    bool ReadKillEntitySource(CSOLMonsterDeathCall& call, CBaseEntity& entity, int& value,
        csol::scenario::ConfigError& error);
    bool ReadEntitySource(CBaseEntity& entity, int& value, csol::scenario::ConfigError& error);
    bool AppendKillChainRecord(const CSOLScenarioPlayerRuntimeState& player,
        CSOLScenarioKillChainRecord::Kind kind, csol::scenario::ConfigError& error);
    bool NotifyKillChainRibbon(CSOLMonsterDamageCall& call,
        const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
        const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
        csol::scenario::ConfigError& error);

    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::array<std::shared_ptr<CSOLScenarioPlayerRuntimeState>, 33> m_players{};
    std::array<EHANDLE, 33> m_removedPlayers{};
    std::uint64_t m_generation{}, m_eventSequence{}, m_lifecycle{}, m_protectionOperation{};
    csol::scenario::ConfigError m_error;
    bool m_active{}, m_clearPointProtection{}, m_invalidating{}, m_activating{}, m_releasingBuffs{};
    std::uint64_t m_killSubscriptionSequence{}, m_killPositionSequence{};
    std::uint64_t m_buffSequence{};
    std::vector<CSOLScenarioKillPosition> m_killPositions;
    std::uint64_t m_killChainSequence{};
    std::vector<CSOLScenarioKillChainRecord> m_killChainRecords;
    std::list<CSOLScenarioPlayerStatistics> m_retiredStatistics;
    std::shared_ptr<CSOLScenarioMonsterHitEvents> m_monsterHitEvents;
    // RequestClose revokes rendering/access without invoking user destructors.
    // Release these only after the old player registry has been detached.
    std::vector<CSOLScenarioStatBuff> m_retiredBuffs;
    std::size_t m_componentDispatchDepth{};
};
