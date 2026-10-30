// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_upgrades.h"
#include "game_rules_gate.h"
#include <algorithm>
#include <cmath>

using csol::scenario::UpgradeStat;

namespace
{
struct Busy
{
    bool& flag;
    explicit Busy(bool& value) : flag(value) { flag = true; }
    ~Busy() { flag = false; }
};
int Effective(const csol::scenario::UpgradeAttribute& value)
{
    return static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(value.level) + value.bonus, value.cap));
}
int Truncate(float value)
{
    return static_cast<double>(value) >= -2147483648.0 && static_cast<double>(value) < 2147483648.0 ?
        static_cast<int>(value) : INT32_MIN;
}
}

std::shared_ptr<CCSOLScenarioPlayerUpgrades> CCSOLScenarioPlayerUpgrades::Create(CBasePlayer& player,
    std::shared_ptr<const csol::scenario::UpgradeTables> tables,
    std::shared_ptr<CCSOLScenarioUpgradeServices> services,
    CCSOLScenarioMessages messages, float experienceRatio, std::uint64_t generation)
{
    if (!tables || !services || !generation || !std::isfinite(experienceRatio)) return {};
    auto state = std::make_shared<CCSOLScenarioPlayerUpgrades>();
    state->m_wall = std::make_shared<CSOLScenarioWallPlayer>();
    state->m_wall->player = &player;
    state->m_wall->generation = generation;
    state->m_wall->attackLevel = 1;
    state->m_tables = std::move(tables);
    state->m_services = std::move(services);
    state->m_messages = messages;
    state->m_experienceRatio = experienceRatio;
    return state->Current() ? state : nullptr;
}

bool CCSOLScenarioPlayerUpgrades::Current() const
{
    if (!m_valid || !m_services || !m_services->Current() || !m_valid) return false;
    auto* entity = m_wall ? m_wall->player.Get<CBaseEntity>() : nullptr;
    return entity && !(entity->pev->flags & FL_KILLME) && dynamic_cast<CBasePlayer*>(entity);
}

CBasePlayer* CCSOLScenarioPlayerUpgrades::Player() const
{
    return Current() ? dynamic_cast<CBasePlayer*>(m_wall->player.Get<CBaseEntity>()) : nullptr;
}

bool CCSOLScenarioPlayerUpgrades::Fail(const char* reason)
{
    if (m_error.message.empty()) m_error.message = reason;
    return false;
}

void CCSOLScenarioPlayerUpgrades::Invalidate()
{
    m_valid = false;
    m_classId = -1;
    if (m_wall) ++m_wall->generation;
    m_progress.Clear();
}

bool CCSOLScenarioPlayerUpgrades::Configure(int classId, std::uint8_t feature, bool elevenLevelLimit)
{
    auto keepAlive = shared_from_this();
    if (m_busy || !Current()) return false;
    Busy busy(m_busy);
    m_error = {};
    m_classId = -1;
    const auto* type = m_tables->classes.Find(classId);
    if (!type) return Fail("scenario character upgrade class is unavailable");
    std::array<csol::scenario::UpgradeAttribute, 5> attributes;
    int experience = 0;
    const bool firstConfiguration = !m_progress.State();
    if (const auto* previous = m_progress.State())
    { attributes = previous->attributes; experience = previous->experience; }
    else
        attributes.fill({1, 21, 0});
    for (std::size_t i = 0; i < attributes.size(); ++i)
    {
        if (type->increases[i] < 0 || type->increases[i] >= INT32_MAX)
            return Fail("scenario character upgrade cap is invalid");
        attributes[i].cap = type->increases[i] + 1;
    }
    const bool initialized = m_services->Initialize(m_wall->player, classId, attributes);
    if (!Current()) return false;
    if (!initialized) return Fail("scenario character initialization failed");
    if (!m_progress.Initialize(m_tables, classId, attributes, experience, m_error)) return false;
    m_classId = classId;
    m_wall->characterFeature = feature;
    m_wall->elevenLevelLimit = elevenLevelLimit;
    // Initialize publishes existing levels; it does not award XP or heal.
    bool published = PublishCurrent();
    if (published && firstConfiguration)
    {
        // MP's independent InitHUD path starts the experience bar at zero.
        // Only a new player state needs that initial packet; changing class
        // retains XP and its last transmitted percentage.
        auto* player = Player();
        published = player && m_messages.SendUpgradeExperience(player->edict(), 0);
        published = Current() && published;
    }
    if (!published) m_classId = -1;
    return published;
}

bool CCSOLScenarioPlayerUpgrades::Publish()
{
    auto keepAlive = shared_from_this();
    if (m_busy || !Current()) return false;
    Busy busy(m_busy);
    return PublishCurrent();
}

bool CCSOLScenarioPlayerUpgrades::PublishCurrent()
{
    auto* player = Player();
    const auto* state = m_progress.State();
    if (!player || !state) return false;
    const int level = state->level + state->bonus;
    const bool sent = m_messages.SendUpgradeTotal(player->edict(), level, false);
    if (!Current() || !sent) return false;
    for (unsigned stat = 0; stat < 5; ++stat)
        if (!SendStat(static_cast<UpgradeStat>(stat))) return false;
    return true;
}

bool CCSOLScenarioPlayerUpgrades::SendStat(UpgradeStat stat)
{
    auto* player = Player();
    const auto* state = m_progress.State();
    const auto index = static_cast<std::size_t>(stat);
    if (!player || !state || index >= 5) return false;
    const auto attribute = state->attributes[index];
    const bool sent = m_messages.SendUpgradeStat(player->edict(), static_cast<unsigned>(stat), Effective(attribute), attribute.cap);
    return Current() && sent;
}

bool CCSOLScenarioPlayerUpgrades::RefreshBonuses()
{
    auto keepAlive = shared_from_this();
    if (m_busy || !Current() || !m_progress.State()) return false;
    Busy busy(m_busy);
    const int previous = m_progress.State()->bonus;
    if (!m_progress.RefreshBonuses(keepAlive, m_error)) return false;
    // The periodic original sends the six notices only when the aggregate
    // changes. HP/speed have already been applied per attribute in order.
    return previous == m_progress.State()->bonus || PublishCurrent();
}

bool CCSOLScenarioPlayerUpgrades::Update(float now, bool force)
{
    auto keepAlive = shared_from_this();
    if (m_busy || !Current() || !m_progress.State()) return false;
    if (!std::isfinite(now)) return Fail("scenario upgrade HUD time is not finite");
    if (!force && !(now > m_nextUpdate)) return true;
    Busy busy(m_busy);
    m_nextUpdate = now + 0.5f;
    const int previousBonus = m_progress.State()->bonus;
    if (!m_progress.RefreshBonuses(keepAlive, m_error)) return false;
    if (previousBonus != m_progress.State()->bonus && !PublishCurrent()) return false;

    const auto* state = m_progress.State();
    if (!state) return false;
    const int level = state->level + state->bonus;
    int percent = 0;
    if (level < state->cap)
    {
        const auto* threshold = m_tables->levels.ExperienceLevel(level);
        if (!threshold || threshold->experience <= 0)
            return Fail("scenario upgrade experience threshold is missing or nonpositive");
        // MP 108FA8AD: convert both integers to float32, multiply XP by
        // 100, divide, then truncate. Integer arithmetic/double/reversed
        // operations differ at percentage boundaries. Effective cap sends 0.
        const float scaled = static_cast<float>(state->experience) * 100.0f;
        percent = Truncate(scaled / static_cast<float>(threshold->experience));
    }
    if (!force && percent == m_experiencePercent) return true;
    auto* player = Player();
    if (!player) return false;
    const int previousPercent = m_experiencePercent;
    m_experiencePercent = percent;
    const bool sent = m_messages.SendUpgradeExperience(player->edict(), percent);
    if (!Current()) return false;
    if (!sent) m_experiencePercent = previousPercent;
    return sent;
}

bool CCSOLScenarioPlayerUpgrades::AddExperience(int amount)
{
    auto keepAlive = shared_from_this();
    auto* player = Player();
    if (m_busy || !player || !m_progress.State()) return false;
    Busy busy(m_busy);
    m_error = {};
    const bool alive = player->IsAlive();
    if (!Current()) return false;
    if (!alive || amount < 0) return true; // Original raw-entry rejection.
    const auto* state = m_progress.State();
    if (state->level >= state->cap)
        return m_progress.AddAdjustedExperience(0, keepAlive, m_error);
    const bool modified = m_services->ModifyExperience(m_wall->player, amount);
    if (!Current()) return false;
    if (!modified) return Fail("scenario EXP_RATIO effect evaluation failed");
    const float scaled = static_cast<float>(amount) * m_experienceRatio;
    if (!std::isfinite(scaled)) return Fail("scenario difficulty experience is not finite");
    return m_progress.AddAdjustedExperience(Truncate(scaled), keepAlive, m_error);
}

bool CCSOLScenarioPlayerUpgrades::ModifyMoney(int& amount)
{
    auto keepAlive = shared_from_this();
    if (!Current()) return false;
    std::optional<float> percent;
    const bool read = m_services->MoneyAddPercent(m_wall->player, percent);
    if (!Current()) return false;
    if (!read) return Fail("scenario character money ability is unavailable");
    if (!percent) return true;
    if (!std::isfinite(*percent)) return Fail("scenario character money modifier is not finite");
    // MP uses three distinct float operations before cvttss2si.
    const float base = static_cast<float>(amount);
    const float product = base * *percent;
    const float added = product / 100.0f;
    const float result = base + added;
    if (!std::isfinite(result)) return Fail("scenario character money modifier is not finite");
    amount = Truncate(result);
    return true;
}

bool CCSOLScenarioPlayerUpgrades::ApplyMovement(float weaponSpeed)
{
    auto keepAlive = shared_from_this();
    float added{};
    if (!std::isfinite(weaponSpeed) || !StatValue(UpgradeStat::Speed, added)) return false;
    const float speed = added + weaponSpeed;
    if (!std::isfinite(speed)) return Fail("scenario movement speed is not finite");
    auto* player = Player();
    if (!player) return false;
    player->pev->maxspeed = speed;
    float gravity = 1.0f;
    const bool modified = m_services->ModifyJump(m_wall->player, gravity);
    if (!Current()) return false;
    if (!modified || !std::isfinite(gravity)) return Fail("scenario JUMP effect evaluation failed");
    player = Player();
    if (!player) return false;
    player->pev->gravity = gravity;
    return true;
}

bool CCSOLScenarioPlayerUpgrades::ReadBonus(UpgradeStat stat, std::int32_t& output)
{
    if (!Current()) return false;
    const bool read = m_services->ReadBonus(m_wall->player, stat, output);
    return Current() && read;
}

bool CCSOLScenarioPlayerUpgrades::StatValue(UpgradeStat stat, float& value) const
{
    return Current() && m_progress.StatValue(stat, value);
}

bool CCSOLScenarioPlayerUpgrades::ApplyHealth()
{
    float maximum{};
    if (!StatValue(UpgradeStat::Health, maximum) || maximum < 0) return false;
    auto* player = Player();
    if (!player) return false;
    player->pev->max_health = maximum;
    return true;
}

bool CCSOLScenarioPlayerUpgrades::ApplySpawnHealth()
{
    auto keepAlive = shared_from_this();
    if (!ApplyHealth()) return false;
    auto* player = Player();
    if (!player) return false;
    player->pev->health = player->pev->max_health;
    return true;
}

bool CCSOLScenarioPlayerUpgrades::ResetSpeed()
{
    auto* player = Player();
    if (!player) return false;
    // All attributes invoke the actual hookable SDK ResetMaxSpeed. The rules
    // speed consumer must also apply the current table value when switching
    // weapons; a one-off pev write here would lose upgrades on every switch.
    player->ResetMaxSpeed();
    // The SDK hook returns void. A required movement/JUMP failure rejects the
    // rules, and must also stop this XP operation before healing or deducting
    // its threshold. Service lifetime alone does not signal that failure.
    return Current() && !CSOL_RulesRejected();
}

bool CCSOLScenarioPlayerUpgrades::BonusChanged(UpgradeStat stat, int, int)
{
    return stat == UpgradeStat::Health ? ApplyHealth() :
        stat == UpgradeStat::Speed ? ResetSpeed() : Current();
}

bool CCSOLScenarioPlayerUpgrades::LevelIncreased(UpgradeStat stat, const csol::scenario::UpgradeProgressState& state)
{
    auto* player = Player();
    if (!player) return false;
    const int level = state.level + state.bonus;
    const bool sent = m_messages.SendUpgradeTotal(player->edict(), level, true);
    if (!Current() || !sent || !SendStat(stat)) return false;
    if (stat == UpgradeStat::Health && !ApplyHealth()) return false;
    return ResetSpeed();
}

bool CCSOLScenarioPlayerUpgrades::ReachedEffectiveCap(const csol::scenario::UpgradeProgressState&)
{
    if (!Current()) return false;
    const bool marked = m_services->ReachedEffectiveCap(m_wall->player);
    return Current() && marked;
}

bool CCSOLScenarioPlayerUpgrades::HealAfterUpgrade()
{
    auto* player = Player();
    if (!player) return false;
    const float health = std::min(player->pev->max_health, player->pev->health + 100.0f);
    if (!std::isfinite(health)) return false;
    player->pev->health = health;
    return true;
}

bool CCSOLScenarioPlayers::Bind(std::shared_ptr<CCSOLScenarioPlayerUpgrades> player)
{
    if (!player || !player->Current()) return false;
    const auto wall = player->WallState();
    auto* entity = wall->player.Get<CBaseEntity>();
    if (!entity || !g_engfuncs.pfnIndexOfEdict) return false;
    const int index = entity->entindex();
    if (!player->Current() || index < 1 || index >= static_cast<int>(m_players.size())) return false;
    auto& previous = m_players[index];
    if (previous == player) return true;
    if (previous) previous->Invalidate();
    previous = std::move(player);
    return true;
}

std::shared_ptr<CCSOLScenarioPlayerUpgrades> CCSOLScenarioPlayers::Upgrades(const EHANDLE& player) const
{
    // 32 fixed slots, no engine callback needed to derive an index. Check a
    // service's map lifetime before touching its possibly old edict handle.
    for (std::size_t index = 1; index < m_players.size(); ++index)
    {
        const auto state = m_players[index];
        if (state && state->Current() && m_players[index] == state &&
            state->WallState()->player.Get() == player.Get()) return state;
    }
    return {};
}

std::shared_ptr<CSOLScenarioWallPlayer> CCSOLScenarioPlayers::Find(const EHANDLE& player)
{
    const auto state = Upgrades(player);
    return state ? state->WallState() : nullptr;
}

bool CCSOLScenarioPlayers::CountDamageScalePlayers(int& count) const
{
    if (!gpGlobals || !g_engfuncs.pfnIndexOfEdict) return false;
    const int limit = std::min(gpGlobals->maxClients, static_cast<int>(m_players.size()) - 1);
    int eligible{};
    for (int index = 1; index <= limit; ++index)
    {
        const auto state = m_players[index];
        if (!state || !state->Current() || m_players[index] != state || state->ConfiguredClass() <= 0)
            continue;
        const auto wall = state->WallState();
        auto* entity = wall->player.Get<CBaseEntity>();
        if (!entity || (entity->pev->flags & FL_DORMANT)) continue;
        const int actualIndex = entity->entindex();
        if (!state->Current() || m_players[index] != state || state->ConfiguredClass() <= 0)
            continue;
        entity = wall->player.Get<CBaseEntity>();
        if (actualIndex != index || !entity || (entity->pev->flags & FL_DORMANT)) continue;
        // MP 108F56B0 counts registered class records, including dead players.
        // Neither native team/health nor a profile-supplied flag is this gate.
        ++eligible;
    }
    count = eligible;
    return true;
}

bool CCSOLScenarioPlayers::ModifyMoney(const EHANDLE& player, int& amount)
{
    const auto state = Upgrades(player);
    return state && state->ModifyMoney(amount);
}

bool CCSOLScenarioPlayers::AddExperience(const EHANDLE& player, int amount)
{
    const auto state = Upgrades(player);
    return state && state->AddExperience(amount);
}

void CCSOLScenarioPlayers::Remove(const EHANDLE& player)
{
    for (std::size_t index = 1; index < m_players.size(); ++index)
    {
        const auto state = m_players[index];
        if (state && state->Current() && m_players[index] == state &&
            state->WallState()->player.Get() == player.Get())
        { state->Invalidate(); m_players[index].reset(); }
    }
}

void CCSOLScenarioPlayers::Clear()
{
    for (auto& state : m_players)
    {
        if (state) state->Invalidate();
        state.reset();
    }
}
