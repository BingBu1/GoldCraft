// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "host.hpp"
#include "host_actions.hpp"
#include "host_notifications.hpp"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/game_rules_gate.h"
#include "../scenario/core/human_kick.h"
#include "../characters.h"
#include "../../character_catalog.hpp"
#include "../../character_voice.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>

namespace csol::scenario::skills
{
namespace
{
struct Resources
{
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    CharacterActionTable actions;
    std::vector<csol::Character> characters;
    std::array<int, 2> viewmodels{};
    // Resolved CSO titles bodies by radio_text key; empty when the optional
    // table is absent, in which case the original key itself is sent.
    std::map<std::string, std::string> emotionMenus;
};
std::shared_ptr<Resources> resources;
// The original client localizes these keys from CSO titles.txt. A stock CS
// client lacks them, so the server sends the same body text instead.
constexpr char EmotionTitlesPath[] = "resource/csol/cso_titles.txt";
// MP stores 15 in player+E10 (m_iMenu) for the cheer menu. ReGameDLL's
// _Menu ends at Menu_ClientBuy=14, so 15 does not alias a stock menu.
constexpr int EmotionMenu = 15;

bool LoadFile(const char* path, std::string& output, ConfigError& error)
{
    int size{};
    if (!g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile)
    { error.message = "character action file API is unavailable"; return false; }
    auto* bytes = g_engfuncs.pfnLoadFileForMe(path, &size);
    if (!bytes) { error.message = std::string("missing character action resource: ") + path; return false; }
    if (size > 0 && size <= 2 * 1024 * 1024)
        output.assign(reinterpret_cast<const char*>(bytes), size);
    g_engfuncs.pfnFreeFile(bytes);
    if (output.empty()) { error.message = "character action resource is empty or too large"; return false; }
    return true;
}

class Action;
std::array<std::weak_ptr<Action>, 33> actions;
// J emotions have no skill host to own them; keep the last started one.
std::array<std::shared_ptr<Action>, 33> emotions;

class Action final : public std::enable_shared_from_this<Action>
{
public:
    Action(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
        const std::shared_ptr<const CSOLScenarioPlayerRuntimeState>& state,
        const std::shared_ptr<CCSOLScenarioSession>& session,
        const CSOLScenarioActiveSkill* owner)
        : players(players), state(state), session(session), player(state->player),
          access(session->Map().EntityAccess()), generation(state->generation),
          owner(owner), data(resources) {}

    struct LifeScope
    {
        Action& action;
        explicit LifeScope(Action& value) : action(value)
        {
            if (!action.operationDepth)
                if (const auto state = action.state.lock()) action.operationLife = state->skillLifeSerial;
            ++action.operationDepth;
        }
        ~LifeScope() { --action.operationDepth; }
    };

    bool Current() const
    {
        const auto p = players.lock(); const auto s = state.lock(); const auto map = session.lock();
        return access && access->Allowed() && p && p->Active() && s && map &&
            map->Map().EntityAccess() == access && s->generation == generation &&
            (!operationDepth || s->skillLifeSerial == operationLife) &&
            p->Find(player) == s && (!owner || s->activeSkill.get() == owner) && player.Get<CBasePlayer>();
    }
    CBasePlayer* Player() const { return Current() ? player.Get<CBasePlayer>() : nullptr; }
    // A skill action and a J emotion are separate objects for one player.
    bool OtherActionPlaying(CBasePlayer& p) const
    {
        const int index = p.entindex();
        if (index < 1 || index > 32) return false;
        const auto other = actions[index].lock();
        return other && other.get() != this && other->player.Get<CBasePlayer>() == &p &&
            other->Current() && other->playing;
    }
    // Slot 5 is the skill action5; menuselect case 15 passes slots 1..count.
    // Both share 114D01B0 and one per-player playing flag (player+3000).
    bool Start(int slot, bool& started, ConfigError& error)
    {
        LifeScope life(*this);
        started = false;
        auto* p = Player();
        if (!p || !gpGlobals) return false;
        if (playing || OtherActionPlaying(*p) || p->pev->deadflag != DEAD_NO || p->pev->health <= 0) return true;
        auto* item = p->m_pActiveItem;
        if (!item || item->m_flStartThrow > 0 || item->m_flReleaseThrow > 0) return true;
        // Until the original per-weapon zoom flag is imported, preserve a
        // scoped weapon's state. The stock ItemInfo flags are another enum.
        // 114D0300: only non-action5 slots print the zoom notice.
        if (p->m_iFOV != 90 && p->m_iFOV != 0)
        {
            if (slot != 5) ClientPrint(p->pev, HUD_PRINTCENTER, "当前状态下无法使用表情");
            return Current();
        }
        if (!ActionButtonsAllowed(static_cast<std::uint32_t>(p->pev->button), constraint.Active(gpGlobals->time))) return true;
        const EHANDLE itemHandle(item);
        if (!item->CanDeploy()) return Current();
        p = Player();
        if (!p || !itemHandle.Get() || p->m_pActiveItem != item) return false;
        int modelId{};
        if (!SkillCharacterModelId(*p, modelId) || !data)
        { error.message = "selected character has no action table identity"; return false; }
        const auto found = data->actions.find(modelId);
        if (found == data->actions.end())
        { error.message = "selected character has no original action5"; return false; }
        const auto row = found->second;
        // Skill action5 exists even on a four-item J menu. A menu request
        // must still pass MP's 1..count check, including slot 5.
        if ((!owner || slot != 5) && (slot < 1 || slot > row.emotionCount)) return Current();
        const CharacterEmotionAction entry = slot == 5 ?
            CharacterEmotionAction{row.sequence, row.weaponAnimation, row.frameRate} : row.emotions[slot];
        // Verify the deployed, appended model rather than trusting the CSV.
        const auto* header = static_cast<const studiohdr_t*>(GET_MODEL_PTR(p->edict()));
        if (!Current() || itemHandle.Get<CBasePlayerItem>() != item || Player()->m_pActiveItem != item) return false;
        if (!header || entry.sequence >= header->numseq || !data->viewmodels[row.viewModel])
        { error.message = "character skill sequence/viewmodel is not precached"; return false; }
        struct StartRollback
        {
            Action& action;
            bool& started;
            ~StartRollback()
            {
                if (!started)
                {
                    action.playing = false;
                    action.sequence = -1;
                    action.deadline = 0;
                    action.weapon = EHANDLE{};
                }
            }
        } rollback{*this, started};
        playing = true; sequence = entry.sequence; weapon = itemHandle;
        p->pev->sequence = sequence;
        p->pev->frame = 0;
        p->ResetSequenceInfo();
        if (!Current() || itemHandle.Get<CBasePlayerItem>() != item || Player()->m_pActiveItem != item) return false;
        p = Player();
        p->m_flFrameRate = entry.frameRate;
        p->pev->weaponanim = entry.weaponAnimation;
        if (!WeaponAnimation(*p, entry.weaponAnimation))
        { error.message = "character skill WeaponAnim sender is unavailable"; return false; }
        if (!Current() || itemHandle.Get<CBasePlayerItem>() != item || Player()->m_pActiveItem != item) return false;
        p = Player();
        if (item->m_iId == WEAPON_KNIFE)
        {
            item->Holster(0);
            if (!Current() || !itemHandle.Get() || Player()->m_pActiveItem != item) return false;
        }
        if (auto* gun = dynamic_cast<CBasePlayerWeapon*>(item)) gun->m_fInReload = FALSE;
        item->m_flStartThrow = 0;
        item->m_flReleaseThrow = -1;
        p = Player();
        // ReGameDLL's CLIENT_WEAPONS timer is a remaining duration.
        p->m_flNextAttack = 1000000.0f;
        p->pev->viewmodel = data->viewmodels[row.viewModel];
        p->pev->weaponmodel = 0;
        deadline = 0;
        const int index = p->entindex();
        if (!Current() || index < 1 || index > 32) return false;
        actions[index] = shared_from_this();
        started = true;
        return true;
    }
    bool Cancel(bool restore)
    {
        LifeScope life(*this);
        auto* p = Player();
        const bool wasPlaying = playing;
        // DefaultDeploy calls Cancel(false), so retire before calling Deploy.
        playing = false; sequence = -1; deadline = 0; weapon = EHANDLE{};
        if (!restore)
        {
            // Reset may run inside an old-life callback. Retire local state
            // even then, without issuing a new-life movement callback.
            const bool constrained = constraint.present;
            constraint = {};
            if (constrained && Current())
            {
                Player()->ResetMaxSpeed();
                if (!Current()) return false;
            }
        }
        if (!p || !wasPlaying) return Current();
        if (p->pev->deadflag != DEAD_NO || p->pev->health <= 0) return true;
        if (!restore) return true;
        auto* item = p->m_pActiveItem;
        if (!item) { p->m_flNextAttack = 0; return true; }
        const EHANDLE handle(item);
        item->Deploy();
        if (!Current() || !handle.Get()) return false;
        p = Player();
        return p && p->m_pActiveItem == item && WeaponAnimation(*p, p->pev->weaponanim) && Current();
    }
    bool Tick()
    {
        LifeScope life(*this);
        auto* p = Player();
        if (!p || !gpGlobals) return false;
        const float now = gpGlobals->time;
        if (playing)
        {
            if (p->pev->deadflag != DEAD_NO || p->pev->health <= 0) return Cancel(false);
            if (p->m_pActiveItem != weapon.Get<CBasePlayerItem>())
            { if (!Cancel(false)) return false; }
            else if (!ActionButtonsAllowed(static_cast<std::uint32_t>(p->pev->button), constraint.Active(now)) ||
                ActionFinished(now, deadline, p->m_fSequenceFinished != FALSE))
            { if (!Cancel(true)) return false; }
        }
        if (constraint.Expired(now)) return RemoveConstraint();
        return Current();
    }
    bool AddConstraint(float duration, ConfigError& error)
    {
        LifeScope life(*this);
        if (!Current() || !gpGlobals) return false;
        const float expires = gpGlobals->time + duration;
        if (!std::isfinite(duration) || !std::isfinite(expires))
        { error.message = "character action constraint has an invalid deadline"; return false; }
        constraint = {true, expires};
        Player()->ResetMaxSpeed();
        return Current();
    }
    bool RemoveConstraint()
    {
        LifeScope life(*this);
        if (!Current()) return false;
        const bool had = constraint.present;
        constraint = {};
        if (had) Player()->ResetMaxSpeed();
        return Current();
    }
    static bool WeaponAnimation(CBasePlayer& p, int animation)
    {
        if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
        const int body = p.pev->body;
        MESSAGE_BEGIN(MSG_ONE, SVC_WEAPONANIM, nullptr, p.pev);
        WRITE_BYTE(animation); WRITE_BYTE(body); MESSAGE_END();
        return true;
    }
    std::weak_ptr<CCSOLScenarioPlayerRuntime> players;
    std::weak_ptr<const CSOLScenarioPlayerRuntimeState> state;
    std::weak_ptr<CCSOLScenarioSession> session;
    EHANDLE player, weapon;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::uint64_t generation{}, operationLife{};
    unsigned operationDepth{};
    int sequence{-1};
    const CSOLScenarioActiveSkill* owner{};
    std::shared_ptr<Resources> data;
    bool playing{};
    float deadline{};
    ActionConstraint constraint;
};

std::shared_ptr<Action> FindAction(CBasePlayer& player)
{
    const int index = player.entindex();
    if (index < 1 || index > 32) return {};
    auto result = actions[index].lock();
    if (!result || result->player.Get<CBasePlayer>() != &player) return {};
    return result;
}

class SdkHost final : public ActivationHost
{
public:
    SdkHost(std::shared_ptr<Action> action, SkillTraits traits,
        ActivationState& timers, std::shared_ptr<HostEffects> effects)
        : action(std::move(action)), traits(traits), timers(timers), effects(std::move(effects)) {}
    bool Current() const override { return effects && effects->Current() && action && action->Current(); }
    bool ReadTime(float& value, ConfigError&) const override
    { if (!Current() || !gpGlobals) return false; value = gpGlobals->time; return std::isfinite(value); }
    bool FrozenWrathBlocks(bool& value, ConfigError& error) override
    { return Current() && effects->FrozenWrathBlocks(value, error); }
    bool PlayerAlive(bool& value, ConfigError&) override
    { auto* p = action->Player(); value = p && p->pev->deadflag == DEAD_NO && p->pev->health > 0; return Current(); }
    bool ScenarioTargetAvailable(bool& value, ConfigError& error) override
    { return Current() && effects->TargetAvailable(value, error); }
    bool NotifyNotReady(ConfigError&) override
    {
        auto* p = action->Player(); if (!p) return false;
        ClientPrint(p->pev, HUD_PRINTCENTER, "技能尚未准备就绪");
        return Current();
    }
    bool StartCharacterAction5(bool& started, ConfigError& error) override
    { return action->Start(5, started, error); }
    bool PrepareEffects(ActivationState& state, bool& prepared, ConfigError& error) override
    { return Current() && effects->Prepare(state, prepared, error); }
    bool AddActionConstraint33(float duration, ConfigError& error) override
    { return action->AddConstraint(duration, error); }
    bool AdjustCooldown(float requested, float& readyAt, ConfigError& error) override
    {
        float now{}, reduction{};
        if (!ReadTime(now, error) || !effects->CooldownReduction(reduction, error) || !Current()) return false;
        if (!std::isfinite(reduction)) { error.message = "skill cooldown reduction is not finite"; return false; }
        readyAt = skills::AdjustCooldown(now, requested, reduction);
        return true;
    }
    bool PlayerModelId(int& value, ConfigError& error) override
    {
        auto* p = action->Player(); if (!p) return false;
        if (!SkillCharacterModelId(*p, value)) { error.message = "skill character identity is unavailable"; return false; }
        return Current();
    }
    bool PlayActivationVoice(ConfigError& error) override { return Current() && effects->Voice(error); }
    bool PublishActivation(int subtype, bool progress, float value, ConfigError& error) override
    {
        if (!Current()) return false;
        if (progress) { error.message = "skill progress sender is not implemented"; return false; }
        float delay{};
        if (!effects->UiDelay(delay, error) || !Current()) return false;
        const auto session = action->session.lock(); auto* p = action->Player();
        return session && p && session->Messages().SendActiveSkillCooldown(p->edict(), subtype, value, delay);
    }
    bool HasActionConstraint33(bool& value, ConfigError&) override
    { value = action->constraint.present; return Current(); }
    bool CharacterActionPlaying(bool& value, ConfigError&) override
    { value = action->playing; return Current(); }
    bool RemoveActionConstraint33(ConfigError&) override { return action->RemoveConstraint(); }
    bool StartEffects(ConfigError& error) override { return Current() && effects->Start(timers, error); }
    bool ActiveEffects(ConfigError& error) override
    { return Current() && (traits.emptyActive48 || effects->Active(timers, error)); }
    bool EndEffects(ConfigError& error) override
    { return Current() && (traits.emptyEnd4C || effects->End(timers, error)); }
    bool UpdateEffects(ConfigError& error) override
    { return Current() && (traits.emptyUpdate50 || effects->Update(timers, error)); }
    std::shared_ptr<Action> action;
    SkillTraits traits;
    ActivationState& timers;
    std::shared_ptr<HostEffects> effects;
};
}

bool PrecacheCharacterActions(const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    if (!session || !session->Map().EntityAccessAllowed()) return false;
    auto candidate = std::make_shared<Resources>();
    candidate->session = session; candidate->access = session->Map().EntityAccess();
    std::string csv, catalog;
    if (!LoadFile("classes2/emotion.csv", csv, error) || !candidate->access->Allowed() ||
        !ReadCharacterActions(csv, candidate->actions, error) ||
        !LoadFile("models/csol/characters.json", catalog, error) || !candidate->access->Allowed()) return false;
    try { candidate->characters = csol::ParseCharacters(catalog); }
    catch (const std::exception& e) { error.message = e.what(); return false; }
    if (!g_engfuncs.pfnGetFileSize) { error.message = "character emotion file API is unavailable"; return false; }
    const int titlesSize = g_engfuncs.pfnGetFileSize(EmotionTitlesPath);
    if (!candidate->access->Allowed()) return false;
    if (titlesSize > 0)
    {
        std::string titles;
        if (!LoadFile(EmotionTitlesPath, titles, error) || !candidate->access->Allowed()) return false;
        for (const auto& [id, row] : candidate->actions)
        {
            if (!row.emotionCount || candidate->emotionMenus.contains(row.emotionMenu)) continue;
            std::string body;
            if (!ReadTitleMessage(titles, std::string_view(row.emotionMenu).substr(1), body))
            { error.message = "character emotion menu is missing from CSO titles: " + row.emotionMenu; return false; }
            candidate->emotionMenus.emplace(row.emotionMenu, std::move(body));
        }
    }
    // Client 5050 (02A41BAD) reads sound/sentences.json locally and plays the
    // view character's voice. Advertise the table and every emotion wav a
    // catalog character can select; absent files only stay silent.
    {
        std::string text;
        csol::voice::SentenceTable sentences;
        if (!LoadFile("sound/sentences.json", text, error) || !candidate->access->Allowed()) return false;
        if (!csol::voice::ReadSentences(text, sentences, "MRAD_EMOTION"))
        { error.message = "character emotion sentence table is invalid"; return false; }
        csol::voice::ExpandTemplates(sentences,
            [](const std::string& path) { return g_engfuncs.pfnGetFileSize(path.c_str()) > 0; });
        if (!candidate->access->Allowed()) return false;
        std::set<std::string_view> voices;
        for (const auto& c : candidate->characters) voices.insert(csol::voice::CharacterVoiceKey(c.id, c.male));
        PRECACHE_GENERIC(const_cast<char*>("sound/sentences.json"));
        if (!candidate->access->Allowed()) return false;
        std::set<std::string> files;
        int missing{};
        for (const auto& [name, entry] : sentences)
            for (const auto& [voice, list] : entry)
                if (voices.contains(voice)) files.insert(list.begin(), list.end());
        for (const auto& file : files)
        {
            const auto full = "sound/" + file;
            const int size = g_engfuncs.pfnGetFileSize(full.c_str());
            if (!candidate->access->Allowed()) return false;
            if (size <= 0) { ++missing; continue; }
            PRECACHE_GENERIC(const_cast<char*>(full.c_str()));
            if (!candidate->access->Allowed()) return false;
        }
        if (missing) ALERT(at_console, "CSOL emotion voices: %d of %d wav files are missing\n", missing, int(files.size()));
    }
    const std::array<const char*, 2> paths{"models/v_emotion0.mdl", "models/v_emotion1.mdl"};
    for (unsigned i = 0; i < paths.size(); ++i)
    {
        const int index = PRECACHE_MODEL(const_cast<char*>(paths[i]));
        if (!candidate->access->Allowed()) return false;
        if (!index) { error.message = "character emotion viewmodel precache failed"; return false; }
        candidate->viewmodels[i] = ALLOC_STRING(paths[i]);
        if (!candidate->access->Allowed()) return false;
    }
    resources = std::move(candidate); actions = {}; emotions = {};
    return true;
}

bool SkillCharacterModelId(CBasePlayer& player, int& modelId)
{
    if (!resources || !resources->access || !resources->access->Allowed()) return false;
    const char* model = CSOL_CharacterModel(&player);
    if (!model) return false;
    const auto found = std::find_if(resources->characters.begin(), resources->characters.end(),
        [&](const auto& row) { return row.model == model; });
    if (found == resources->characters.end()) return false;
    modelId = found->id;
    return true;
}
bool SkillCharacterSourceName(CBasePlayer& player, std::string& name)
{
    int id{};
    if (!SkillCharacterModelId(player, id)) return false;
    const auto found = resources->actions.find(id);
    if (found == resources->actions.end()) return false;
    name = found->second.sourceName;
    return true;
}
bool SkillCharacterSourceNames(CCSOLScenarioSession& session, std::vector<std::string>& names)
{
    if (!resources || resources->session.lock().get() != &session || !resources->access ||
        !resources->access->Allowed() || session.Map().EntityAccess() != resources->access) return false;
    names.clear();
    for (const auto& [id, action] : resources->actions) names.push_back(action.sourceName);
    return true;
}

std::shared_ptr<ActivationHost> CreateHost(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const SkillTraits& traits, const ClassModDefinition& definition,
    const CSOLScenarioActiveSkill* owner, ActivationState& timers,
    std::shared_ptr<HostEffects> effects, ConfigError& error)
{
    if (!players || !players->Active() || !state || players->Find(state->player) != state ||
        !session || !session->Map().EntityAccessAllowed() || !effects || !owner ||
        definition.type != 10 || definition.subtype != traits.subtype || !resources ||
        resources->session.lock() != session)
    { error.message = "character skill SDK host is not initialized for this map"; return {}; }
    return std::make_shared<SdkHost>(std::make_shared<Action>(players, state, session, owner),
        traits, timers, std::move(effects));
}
bool CharacterActionPostThink(CBasePlayer& player)
{ const auto action = FindAction(player); return !action || action->Tick(); }
bool PreserveCharacterAction(CBasePlayer& player, int activity)
{
    if (CSOL_PreserveHumanKickAnimation(player, activity)) return true;
    const auto action = FindAction(player);
    return action && action->Current() && action->playing && activity != PLAYER_DIE &&
        player.pev->deadflag == DEAD_NO && player.pev->health > 0;
}
bool CancelCharacterAction(CBasePlayer& player, bool restore)
{ const auto action = FindAction(player); return !action || action->Cancel(restore); }
float CharacterActionMaxSpeed(CBasePlayer& player, float speed)
{
    const auto action = FindAction(player);
    return action && action->Current() && gpGlobals && action->constraint.Active(gpGlobals->time)
        ? std::min(speed, 1.0f) : speed;
}
bool OpenCharacterEmotionMenu(CBasePlayer& player)
{
    // cheer 11410660..11410723 on mode15 ordinary maps: no alive check here;
    // the row must exist with count>0, then ShowMenu and m_iMenu=15.
    const auto data = resources;
    int modelId{};
    if (!data || !data->access || !data->access->Allowed() || !SkillCharacterModelId(player, modelId)) return false;
    const auto found = data->actions.find(modelId);
    if (found == data->actions.end() || found->second.emotionCount <= 0) return false;
    const auto& row = found->second;
    const auto menu = data->emotionMenus.find(row.emotionMenu);
    std::string text = menu != data->emotionMenus.end() ? menu->second : row.emotionMenu;
    ShowMenu(&player, EmotionMenuKeys(row.emotionCount), -1, FALSE, text.data());
    if (!data->access->Allowed()) return false;
    player.m_iMenu = static_cast<_Menu>(EmotionMenu);
    return true;
}
bool CharacterEmotionMenuOpen(const CBasePlayer& player) { return player.m_iMenu == EmotionMenu; }
bool SelectCharacterEmotion(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<const CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, int slot, ConfigError& error)
{
    // menuselect case 15 (1140EFA5): the same gate and commit as action5,
    // with the row's slot, an extra flag of 0 and no duration.
    if (!players || !state || !session || !resources || resources->session.lock() != session ||
        players->Find(state->player) != state) return false;
    auto action = std::make_shared<Action>(players, state, session, nullptr);
    auto* p = action->Player();
    if (!p) return false;
    const int index = p->entindex();
    bool started{};
    if (!action->Start(slot, started, error)) return false;
    if (started && index >= 1 && index <= 32) emotions[index] = std::move(action);
    return true;
}
}
