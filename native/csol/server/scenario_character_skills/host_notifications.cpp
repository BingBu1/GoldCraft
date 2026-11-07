// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "host_notifications.hpp"
#include "host.hpp"
#include "../scenario/core/scenario_session.h"
#include "../../character_skill_protocol.hpp"
#include "../../character_skill_resources.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <bit>
#include <charconv>
#include <map>
#include <string>
#include <vector>

namespace csol::scenario::skills
{
namespace
{
struct Resources
{
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    int effectMessage{}, mpMessage{}, mp2Message{}, gateMessage{}, healModel{}, damageModel{}, followSprite{}, explosionSprite{}, lotusModel{};
    int shieldAttackSprite{}, shieldHealSprite{}, smokeWaveModel{}, escortSprite{}, storedAttackSprite{};
    int dragonShieldRetireModel{}, groupAttackSprite{}, groupHealSprite{}, pulseAttackModel{}, pulseHealModel{};
    int buffSkillModel{};
    std::vector<std::uint32_t> pendingWorldRetirements;
    bool frozenWrath{};
    std::map<std::string, std::vector<std::string>> voices;
};
std::shared_ptr<Resources> resources;
std::uint32_t nextManagedWorldId = 1;
bool sending{};
struct Sending
{
    std::shared_ptr<Resources> bound = resources;
    Sending() { sending = true; }
    ~Sending();
};
bool Fail(ConfigError& error, const char* message) { error.message = message; return false; }
bool Current(const CCSOLScenarioSession& session)
{
    return resources && resources->session.lock().get() == &session && resources->access &&
        resources->access->Allowed() && session.Map().EntityAccess() == resources->access;
}
Sending::~Sending()
{
    const auto session = bound ? bound->session.lock() : nullptr;
    // Finish any open packet before retiring an effect. Keep the guard held
    // during the drain: a callback may retire another instance, but cannot
    // start a nested message or create a replacement inside this packet.
    if (session && g_engfuncs.pfnMessageBegin && g_engfuncs.pfnMessageEnd && g_engfuncs.pfnWriteByte)
        while (resources == bound && Current(*session) && !bound->pendingWorldRetirements.empty())
        {
            const auto retired = bound->pendingWorldRetirements.back();
            bound->pendingWorldRetirements.pop_back();
            const auto bytes = csol::skillfx::EncodeRemoveWorld(retired);
            MESSAGE_BEGIN(MSG_ALL, bound->effectMessage);
            for (auto byte : bytes) WRITE_BYTE(byte);
            MESSAGE_END();
        }
    sending = false;
}
CBasePlayer* Player(CCSOLScenarioSession& session, const EHANDLE& handle)
{
    if (!Current(session)) return nullptr;
    auto* player = handle.Get<CBasePlayer>();
    return player && player->pev && !(player->pev->flags & FL_KILLME) && !player->has_disconnected ? player : nullptr;
}
bool Send(CCSOLScenarioSession& session, CBasePlayer* target, const std::vector<std::uint8_t>& bytes)
{
    if (!Current(session) || sending || bytes.empty() || bytes.size() > 192 ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const auto bound = resources;
    const int id = bound->effectMessage;
    {
        const Sending guard;
        if (target) MESSAGE_BEGIN(MSG_ONE, id, nullptr, target->pev);
        else MESSAGE_BEGIN(MSG_ALL, id);
        for (auto b : bytes) WRITE_BYTE(b);
        MESSAGE_END();
    }
    return resources == bound && Current(session);
}
bool RemoveManagedWorld(CCSOLScenarioSession& session, std::uint32_t& effectId)
{
    if (!effectId) return true;
    const auto retired = effectId;
    effectId = 0;
    if (!Current(session)) return false;
    if (sending)
    {
        resources->pendingWorldRetirements.push_back(retired);
        return true;
    }
    return Send(session, nullptr, csol::skillfx::EncodeRemoveWorld(retired));
}
bool PublishManagedWorld(CCSOLScenarioSession& session, const EHANDLE& owner,
    std::uint32_t& effectId, float scale, ConfigError& error)
{
    auto* player = Player(session, owner);
    if (!player || sending) return false;
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte)
        return Fail(error, "character managed effect transport is unavailable");
    const auto bound = resources;
    const int index = player->entindex();
    const auto current = [&]
    { return resources == bound && Current(session) && Player(session, owner) == player; };
    if (!current() || index < 1 || index > 32) return false;
    if (!RemoveManagedWorld(session, effectId) || !current()) return false;
    if (!nextManagedWorldId) return Fail(error, "character managed effect identity is exhausted");
    csol::skillfx::ManagedWorldEffect effect;
    effect.id = nextManagedWorldId++;
    effect.skin = 3;
    effect.world.model.model = static_cast<std::uint16_t>(bound->buffSkillModel);
    effect.world.model.owner = static_cast<std::uint16_t>(index);
    effect.world.model.frameMax = 0; // Original ModelFrames counts body variants.
    effect.world.model.life = 0;
    effect.world.model.frameRate = 1;
    effect.world.model.alpha = 255;
    effect.world.model.renderMode = 0;
    effect.world.model.scale = scale;
    effect.world.model.fadeSpeed = effect.world.model.fadeIn = 0;
    effect.world.flags = csol::skillfx::Loop | csol::skillfx::GroundFollow |
        csol::skillfx::Persist | csol::skillfx::LocalFirstPersonViewOrigin;
    if (!csol::skillfx::Valid(effect)) return Fail(error, "character managed world effect is invalid");
    // Publish the identity before engine callbacks, so nested retirement can
    // queue removal after this creation packet without retaining this reference.
    effectId = effect.id;
    const bool sent = Send(session, nullptr, csol::skillfx::Encode(effect));
    if (!current())
    {
        if (resources == bound && Current(session))
            Send(session, nullptr, csol::skillfx::EncodeRemoveWorld(effect.id));
        return false;
    }
    return sent;
}
bool Read(const char* path, std::string& out, ConfigError& error)
{
    int size{};
    auto* bytes = g_engfuncs.pfnLoadFileForMe(path, &size);
    if (!bytes) { error.message = std::string("missing character skill resource: ") + path; return false; }
    if (size > 0 && size <= 4 * 1024 * 1024) out.assign(reinterpret_cast<const char*>(bytes), size);
    g_engfuncs.pfnFreeFile(bytes);
    return !out.empty() || Fail(error, "character skill resource is empty or too large");
}
}

bool PrecacheSkillHost(const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    resources.reset();
    if (!session || !session->Map().EntityAccessAllowed() || sending) return false;
    if (!g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile || !g_engfuncs.pfnRegUserMsg ||
        !g_engfuncs.pfnPrecacheModel || !g_engfuncs.pfnPrecacheSound || !g_engfuncs.pfnPrecacheGeneric ||
        !g_engfuncs.pfnGetFileSize)
        return Fail(error, "character skill precache API is unavailable");
    auto candidate = std::make_shared<Resources>();
    candidate->session = session; candidate->access = session->Map().EntityAccess();
    const auto current = [&] { return candidate->access->Allowed() && session->Map().EntityAccess() == candidate->access; };
    const auto present = [&](const auto& paths)
    {
        for (const auto* path : paths)
        {
            const int size = g_engfuncs.pfnGetFileSize(path);
            if (!current()) return false;
            if (size <= 0)
            { error.message = std::string("missing character skill resource: ") + path; return false; }
        }
        return true;
    };
    if (!present(csol::skillfx::Models) || !present(csol::skillfx::Dependencies) ||
        !present(csol::skillfx::Sounds) || !present(csol::skillfx::ClientResources)) return false;
    std::string eventText, sentenceText;
    if (!Read("resource/ModeEvent/modeevent.csv", eventText, error) || !current() ||
        !Read("sound/sentences.json", sentenceText, error) || !current()) return false;
    const auto* tables = session->Map().Tables();
    if (!tables || !tables->mapId) return Fail(error, "character skill map identity is unavailable");
    CsvDocument events;
    if (!ReadCsv(eventText, events, error)) return false;
    for (const auto& row : events.records)
    {
        if (row.fields.empty() || row.fields.front().empty() || row.fields.front().front() == ';') continue;
        if (row.fields.size() < 3) return Fail(error, "invalid mode event table row");
        int id{}; const auto& text = row.fields.front();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return Fail(error, "invalid mode event map id");
        if (id == *tables->mapId && row.fields[2] == "FrozenWrath") candidate->frozenWrath = true;
    }
    rapidjson::Document sentences;
    // Original common table includes comments in the legacy locale encoding;
    // the skill keys and resource paths themselves are ASCII.
    sentences.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(sentenceText.data(), sentenceText.size());
    if (sentences.HasParseError() || !sentences.IsObject() || !sentences.HasMember("MRAD_BUFFCLASS_SKILL") ||
        !sentences["MRAD_BUFFCLASS_SKILL"].IsObject()) return Fail(error, "skill activation sentence table is invalid");
    std::vector<std::string> defaultVoices;
    for (auto it = sentences["MRAD_BUFFCLASS_SKILL"].MemberBegin(); it != sentences["MRAD_BUFFCLASS_SKILL"].MemberEnd(); ++it)
    {
        std::vector<std::string> paths;
        const auto add = [&](const rapidjson::Value& value)
        {
            if (!value.IsString()) return false;
            std::string path(value.GetString(), value.GetStringLength());
            if (std::string_view(it->name.GetString()) == "buffdefault")
            {
                if (csol::skillfx::ExpandRadio(path, "buffclass").empty()) return false;
            }
            else if (!csol::skillfx::ValidRadioPath(path)) return false;
            paths.push_back(std::move(path)); return true;
        };
        if (it->value.IsArray()) { for (const auto& value : it->value.GetArray()) if (!add(value)) return Fail(error, "invalid skill radio path"); }
        else if (!add(it->value)) return Fail(error, "invalid skill radio path");
        if (std::string_view(it->name.GetString()) == "buffdefault")
        { defaultVoices = std::move(paths); continue; }
        for (const auto& path : paths)
        {
            const auto full = "sound/" + path;
            if (g_engfuncs.pfnGetFileSize(full.c_str()) <= 0) return Fail(error, "character skill radio file is missing");
            if (!current()) return false;
            PRECACHE_GENERIC(const_cast<char*>(full.c_str()));
            if (!current()) return false;
        }
        candidate->voices.emplace(it->name.GetString(), std::move(paths));
    }
    if (!PrecacheCharacterActions(session, error) || !current()) return false;
    std::vector<std::string> sources;
    if (!SkillCharacterSourceNames(*session, sources)) return false;
    for (const auto& source : sources)
    {
        if (!source.starts_with("buffclass")) continue;
        const auto key = csol::skillfx::RadioKey(source);
        if (candidate->voices.contains(key)) continue;
        std::vector<std::string> paths;
        for (const auto& pattern : defaultVoices)
        {
            auto path = csol::skillfx::ExpandRadio(pattern, source);
            if (path.empty()) return Fail(error, "invalid expanded character radio path");
            const auto full = "sound/" + path;
            const bool present = g_engfuncs.pfnGetFileSize(full.c_str()) > 0;
            if (!current()) return false;
            // Earlier characters have no active-skill sentence. Do not
            // register a fictitious template file for them.
            if (!present) continue;
            PRECACHE_GENERIC(const_cast<char*>(full.c_str()));
            if (!current()) return false;
            paths.push_back(std::move(path));
        }
        if (!paths.empty()) candidate->voices.emplace(key, std::move(paths));
    }
    candidate->effectMessage = REG_USER_MSG(csol::skillfx::MessageName, -1); if (!current()) return false;
    candidate->mpMessage = REG_USER_MSG("MPToCL", -1); if (!current()) return false;
    candidate->mp2Message = REG_USER_MSG("MPToCL2", -1); if (!current()) return false;
    candidate->gateMessage = REG_USER_MSG(csol::skillgate::MessageName,
        static_cast<int>(csol::skillgate::MessageBytes)); if (!current()) return false;
    for (int id : {candidate->effectMessage, candidate->mpMessage, candidate->mp2Message, candidate->gateMessage})
        if (id < 64 || id > 255) return Fail(error, "character skill message registration failed");
    for (const auto* path : csol::skillfx::Dependencies)
    { PRECACHE_GENERIC(const_cast<char*>(path)); if (!current()) return false; }
    for (const auto* path : csol::skillfx::ClientResources)
    { PRECACHE_GENERIC(const_cast<char*>(path)); if (!current()) return false; }
    candidate->healModel = PRECACHE_MODEL("models/ef_buffclassskill_heal.mdl"); if (!current()) return false;
    candidate->damageModel = PRECACHE_MODEL("models/ef_buffclassskill_damage.mdl"); if (!current()) return false;
    candidate->followSprite = PRECACHE_MODEL("sprites/ef_buffclass20s2.spr"); if (!current()) return false;
    candidate->explosionSprite = PRECACHE_MODEL("sprites/ef_zbs73_explosion.spr"); if (!current()) return false;
    candidate->lotusModel = PRECACHE_MODEL("models/ef_buffclass26-3_skill.mdl"); if (!current()) return false;
    candidate->shieldAttackSprite = PRECACHE_MODEL("sprites/ef_buffclass24s3tr_skill.spr"); if (!current()) return false;
    candidate->shieldHealSprite = PRECACHE_MODEL("sprites/ef_buffclass24s3ct_skill.spr"); if (!current()) return false;
    candidate->smokeWaveModel = PRECACHE_MODEL("models/ef_meatwall_wave.mdl"); if (!current()) return false;
    candidate->escortSprite = PRECACHE_MODEL("sprites/ef_buffcalss24s4_skill.spr"); if (!current()) return false;
    candidate->storedAttackSprite = PRECACHE_MODEL("sprites/ef_buffclass24s1tr_skill.spr"); if (!current()) return false;
    candidate->dragonShieldRetireModel = PRECACHE_MODEL("models/ef_scorpion_hole.mdl"); if (!current()) return false;
    candidate->groupAttackSprite = PRECACHE_MODEL("sprites/ef_buffclass23s3tr_skill01.spr"); if (!current()) return false;
    candidate->groupHealSprite = PRECACHE_MODEL("sprites/ef_buffclass23s3ct_skill01.spr"); if (!current()) return false;
    candidate->pulseAttackModel = PRECACHE_MODEL("models/ef_buffclass22s4tr.mdl"); if (!current()) return false;
    candidate->pulseHealModel = PRECACHE_MODEL("models/ef_buffclass22s4ct.mdl"); if (!current()) return false;
    candidate->buffSkillModel = PRECACHE_MODEL("models/ef_buffskill.mdl"); if (!current()) return false;
    const int slowWall = PRECACHE_MODEL("models/ef_y21s3buffclassct.mdl"); if (!current()) return false;
    const int slowDebuff = PRECACHE_MODEL("sprites/ef_sbmine_debuff.spr"); if (!current()) return false;
    if (!candidate->healModel || !candidate->damageModel || !candidate->followSprite || !candidate->explosionSprite ||
        !candidate->lotusModel || !candidate->shieldAttackSprite || !candidate->shieldHealSprite ||
        !candidate->smokeWaveModel || !candidate->escortSprite || !candidate->storedAttackSprite || !candidate->dragonShieldRetireModel ||
        !candidate->groupAttackSprite || !candidate->groupHealSprite || !candidate->pulseAttackModel ||
        !candidate->pulseHealModel || candidate->buffSkillModel <= 0 ||
        candidate->buffSkillModel > 0xffff || !slowWall || !slowDebuff)
        return Fail(error, "character skill effect model is unavailable");
    for (const auto* path : csol::skillfx::ClientWorldSprites)
    {
        const int index = PRECACHE_MODEL(const_cast<char*>(path));
        if (!current()) return false;
        if (index <= 0 || index > 0xffff)
        { error.message = std::string("character skill sprite precache failed: ") + path; return false; }
    }
    for (const auto* path : csol::skillfx::Sounds)
    { PRECACHE_SOUND(const_cast<char*>(path + 6)); if (!current()) return false; }
    resources = std::move(candidate);
    return true;
}
bool SkillHostReady(const CCSOLScenarioSession& session) { return Current(session); }

bool PublishSlowWallMonsterEffect(CCSOLScenarioSession& session, const EHANDLE& target, ConfigError& error)
{
    if (!Current(session) || sending) return false;
    auto* entity = target.Get<CBaseEntity>();
    if (!entity || !entity->pev || (entity->pev->flags & FL_KILLME)) return false;
    const int index = entity->entindex();
    if (!Current(session) || target.Get<CBaseEntity>() != entity || index < 1 || index > 0x7fff) return false;
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong)
        return Fail(error, "slow wall effect transport is unavailable");
    const int id = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(199); WRITE_SHORT(index); WRITE_BYTE(62);
    // Original106E4CC8 uses PF_WriteFloat_I for height and duration.
    WRITE_LONG(std::bit_cast<std::int32_t>(60.0f));
    WRITE_LONG(std::bit_cast<std::int32_t>(10.0f));
    MESSAGE_END();
    return Current(session) && target.Get<CBaseEntity>() == entity;
}

bool PublishEliteDeath(CCSOLScenarioSession& session, int appearance, ConfigError& error)
{
    if (!Current(session) || sending) return false;
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort)
        return Fail(error, "elite death effect transport is unavailable");
    const int id = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(27); WRITE_BYTE(26); WRITE_SHORT(appearance);
    MESSAGE_END();
    return Current(session);
}

bool PublishEliteAbilities(CCSOLScenarioSession& session, int appearance,
    std::span<const int> abilities, ConfigError& error)
{
    if (!Current(session) || sending) return false;
    if (appearance < 0 || appearance > 0x7fff || abilities.size() > 14 ||
        !std::is_sorted(abilities.begin(), abilities.end()) ||
        std::adjacent_find(abilities.begin(), abilities.end()) != abilities.end() ||
        std::any_of(abilities.begin(), abilities.end(), [](int id) { return id < 0 || id > 13; }))
        return Fail(error, "elite ability message is invalid");
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort)
        return Fail(error, "elite ability transport is unavailable");
    const int id = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, id);
    //10967367: the ordered selected set is transmitted independently of HP.
    WRITE_BYTE(27); WRITE_BYTE(28); WRITE_SHORT(appearance); WRITE_SHORT(static_cast<int>(abilities.size()));
    for (const int ability : abilities) WRITE_SHORT(ability);
    MESSAGE_END();
    return Current(session);
}

bool PublishEliteHealth(CCSOLScenarioSession& session, const EHANDLE& target,
    int appearance, float ratio, ConfigError& error)
{
    if (!Current(session) || sending) return false;
    auto* entity = target.Get<CBaseEntity>();
    if (!entity || !entity->pev || (entity->pev->flags & FL_KILLME)) return false;
    const int index = entity->entindex();
    if (!Current(session) || target.Get<CBaseEntity>() != entity || index < 1) return false;
    if (appearance < 0 || appearance > 0x7fff || !std::isfinite(ratio))
        return Fail(error, "elite health message is invalid");
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong)
        return Fail(error, "elite health transport is unavailable");
    const int id = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(27); WRITE_BYTE(27); WRITE_SHORT(appearance);
    WRITE_LONG(std::bit_cast<std::int32_t>(ratio)); WRITE_LONG(index);
    MESSAGE_END();
    return Current(session) && target.Get<CBaseEntity>() == entity;
}

bool PublishEliteInvincible(CCSOLScenarioSession& session, const EHANDLE& target,
    float duration, ConfigError& error)
{
    if (!Current(session) || sending) return false;
    auto* entity = target.Get<CBaseEntity>();
    if (!entity || !entity->pev || (entity->pev->flags & FL_KILLME)) return false;
    const int index = entity->entindex();
    if (!Current(session) || target.Get<CBaseEntity>() != entity || index < 1 || index > 0x7fff) return false;
    if (!std::isfinite(duration) || duration < 0 || duration > 0x7fff)
        return Fail(error, "elite invincibility duration is invalid");
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort)
        return Fail(error, "elite invincibility transport is unavailable");
    const int id = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, id);
    //10968359 converts duration to an integer before writing the signed short.
    WRITE_BYTE(112); WRITE_SHORT(index); WRITE_SHORT(static_cast<int>(duration));
    MESSAGE_END();
    return Current(session) && target.Get<CBaseEntity>() == entity;
}

bool PublishEliteSide(CCSOLScenarioSession& session, const EHANDLE& target,
    bool oldDoubleSide, float height, float duration, ConfigError& error)
{
    if (!Current(session) || sending) return false;
    auto* entity = target.Get<CBaseEntity>();
    if (!entity || !entity->pev || (entity->pev->flags & FL_KILLME)) return false;
    const int index = entity->entindex();
    if (!Current(session) || target.Get<CBaseEntity>() != entity || index < 1 || index > 0x7fff) return false;
    if (!std::isfinite(height) || !std::isfinite(duration) || duration < 0)
        return Fail(error, "elite double-side message is invalid");
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong)
        return Fail(error, "elite double-side transport is unavailable");
    const int id = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, id);
    WRITE_BYTE(199); WRITE_SHORT(index); WRITE_BYTE(60 + static_cast<int>(oldDoubleSide));
    WRITE_LONG(std::bit_cast<std::int32_t>(height)); WRITE_LONG(std::bit_cast<std::int32_t>(duration));
    MESSAGE_END();
    return Current(session) && target.Get<CBaseEntity>() == entity;
}

bool PublishSkillGate(CCSOLScenarioSession& session, const EHANDLE& target,
    const csol::skillgate::Snapshot& snapshot, ConfigError& error)
{
    auto* player = Player(session, target);
    if (!player || sending) return false;
    std::array<std::uint8_t, csol::skillgate::MessageBytes> bytes{};
    if (!csol::skillgate::Encode(snapshot, bytes)) return Fail(error, "character skill gate snapshot is invalid");
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte)
        return Fail(error, "character skill gate transport is unavailable");
    const int message = resources->gateMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ONE, message, nullptr, player->pev);
    for (const auto value : bytes) WRITE_BYTE(value);
    MESSAGE_END();
    return Current(session);
}

bool SkillMapHasFrozenWrath(CCSOLScenarioSession& session, bool& present, ConfigError& error)
{
    if (!Current(session)) return Fail(error, "character skill map features are not loaded");
    present = resources->frozenWrath; return true;
}

bool PrepareSkillVisual(const std::shared_ptr<CCSOLScenarioSession>& session,
    const EHANDLE& owner, int subtype, ConfigError& error)
{
    if (!session) return false;
    auto* player = Player(*session, owner); if (!player) return false;
    if (subtype == 51 || subtype == 52 || subtype == 53)
    {
        const int index = player->entindex();
        if (!Current(*session) || index < 1 || index > 32) return false;
        csol::skillfx::WorldEffect effect;
        effect.model.model = static_cast<std::uint16_t>(resources->damageModel);
        effect.model.owner = static_cast<std::uint16_t>(index);
        effect.model.origin = {0,0,1}; effect.model.scale = 0; effect.model.frameMax = 15;
        //106F51D0/106F5370: original flags2000/secondary800 preserve the
        // temporary entity and follow the ground. Ground tracing itself adds
        // one unit; this packet supplies a second one-unit local offset.
        effect.flags = csol::skillfx::GroundFollow | csol::skillfx::Persist;
        if (!Send(*session, nullptr, csol::skillfx::Encode(effect))) return false;
        return PlaySkillVoice(*session, owner, subtype, error);
    }
    if (subtype == 26 || subtype == 27)
    {
        if (!PlaySkillVoice(*session, owner, subtype, error) || !(player = Player(*session, owner))) return false;
        csol::skillfx::WorldEffect effect;
        effect.model.model = static_cast<std::uint16_t>(subtype == 26 ? resources->pulseAttackModel : resources->pulseHealModel);
        // Original1133CE00 uses the feet position without a floor trace.
        // TE145 frameMax0 asks the client for ModelFrameCount; flag10000 loops.
        for (unsigned i = 0; i < 3; ++i)
        {
            const float value = player->pev->origin[i] - (i == 2 ? (player->pev->flags & FL_DUCKING ? 18.0f : 36.0f) : 0.0f);
            effect.model.origin[i] = static_cast<float>(static_cast<int>(value * 8)) * .125f;
        }
        effect.model.life = 1; effect.model.frameRate = effect.model.frameMax = 0;
        effect.model.alpha = 255; effect.model.scale = effect.model.fadeIn = effect.model.fadeSpeed = 0;
        effect.flags = csol::skillfx::Loop;
        return Send(*session, nullptr, csol::skillfx::Encode(effect));
    }
    if (subtype == 30 || subtype == 32 || subtype == 33 || subtype == 38 || subtype == 54)
    {
        if (!PlaySkillVoice(*session, owner, subtype, error) || !(player = Player(*session, owner)) || sending) return false;
        const int index = player->entindex();
        {
            const Sending guard;
            MESSAGE_BEGIN(MSG_ALL, resources->mpMessage);
            WRITE_BYTE(4); WRITE_SHORT(index); WRITE_BYTE(40); MESSAGE_END();
        }
        if (subtype == 38) return Current(*session);
        player = Player(*session, owner);
        return player && PublishSkillCircle(*session, {player->pev->origin.x, player->pev->origin.y,
            player->pev->origin.z}, subtype != 33, error);
    }
    if (subtype == 22 || subtype == 23 || subtype == 28 || subtype == 29 || subtype == 39 ||
        subtype == 37 || subtype == 41 || subtype == 42 || subtype == 43 || subtype == 44 || subtype == 45 || subtype == 46)
        return PlaySkillVoice(*session, owner, subtype, error);
    if (subtype == 40)
    {
        if (!PlaySkillVoice(*session, owner, subtype, error) || !(player = Player(*session, owner))) return false;
        return PublishSkillCircle(*session, {player->pev->origin.x, player->pev->origin.y,
            player->pev->origin.z}, false, error);
    }
    if (subtype == 8 || subtype == 9 || subtype == 12 || subtype == 15 || subtype == 17 || subtype == 18 || subtype == 19 || subtype == 31)
    {
        if (subtype == 18)
        {
            if (sending) return false;
            const Sending guard;
            MESSAGE_BEGIN(MSG_ONE, resources->mpMessage, nullptr, player->pev);
            WRITE_BYTE(0x36); WRITE_BYTE(0); WRITE_LONG(std::bit_cast<std::int32_t>(1.0f));
            WRITE_BYTE(255); WRITE_BYTE(10); WRITE_BYTE(10); WRITE_BYTE(200); MESSAGE_END();
            if (!(player = Player(*session, owner))) return false;
        }
        // Type31 emits the sound before constructing its follow effect.
        if (subtype == 31 && (!PlaySkillVoice(*session, owner, subtype, error) ||
            !(player = Player(*session, owner)))) return false;
        csol::skillfx::ModelEffect effect;
        const int index = player->entindex();
        if (!Current(*session) || index < 1 || index > 32) return false;
        effect.model = static_cast<std::uint16_t>(resources->followSprite);
        effect.owner = effect.excludedOwner = static_cast<std::uint16_t>(index);
        effect.life = 2; effect.frameRate = 15; effect.frameMax = 30;
        effect.alpha = 255; effect.fadeSpeed = effect.fadeIn = 0;
        if (subtype == 17 || subtype == 18) effect.scale = 0;
        return Send(*session, nullptr, csol::skillfx::Encode(effect)) &&
            (subtype == 31 || PlaySkillVoice(*session, owner, subtype, error));
    }
    const bool heal = subtype == 1 || subtype == 3 || subtype == 6 || subtype == 14 || subtype == 20 || subtype == 47;
    const bool damage = subtype == 2 || subtype == 4 || subtype == 5 || subtype == 7 ||
        subtype == 16 || subtype == 34 || subtype == 48 || subtype == 49 || subtype == 50 || subtype == 55;
    if (!heal && !damage) return Fail(error, "character skill preparation visual is not implemented");
    if (subtype == 1 && !session->Messages().SendMonsterHealing(player->entindex(), player->pev->origin, true)) return false;
    if (!Current(*session) || !(player = Player(*session, owner))) return false;
    if (subtype == 3 || subtype == 6 || subtype == 14 || subtype == 20)
    {
        if (sending) return false;
        const int index = player->entindex(); const int message = resources->mpMessage;
        const Sending guard;
        if (subtype == 14 || subtype == 20) MESSAGE_BEGIN(MSG_ALL, message);
        else MESSAGE_BEGIN(MSG_PVS, message, player->pev->origin);
        WRITE_BYTE(4); WRITE_SHORT(index); WRITE_BYTE(subtype == 14 || subtype == 20 ? 40 : 44); MESSAGE_END();
    }
    if (!Current(*session) || !(player = Player(*session, owner))) return false;
    csol::skillfx::ModelEffect effect;
    effect.model = static_cast<std::uint16_t>(heal ? resources->healModel : resources->damageModel);
    if (subtype == 14) effect.scale = 0;
    // GoldSrc original WRITE_COORD truncates to eighths before HW decoding.
    for (unsigned i = 0; i < 3; ++i)
    {
        const float value = player->pev->origin[i] - (i == 2 ? 30.0f : 0.0f);
        effect.origin[i] = static_cast<float>(static_cast<int>(value * 8)) * .125f;
    }
    if (!Send(*session, nullptr, csol::skillfx::Encode(effect))) return false;
    if (subtype == 7)
    {
        player = Player(*session, owner); if (!player || sending) return false;
        const Sending guard;
        MESSAGE_BEGIN(MSG_ONE, resources->mpMessage, nullptr, player->pev);
        WRITE_BYTE(0x36); WRITE_BYTE(0); WRITE_LONG(std::bit_cast<std::int32_t>(1.0f));
        WRITE_BYTE(255); WRITE_BYTE(10); WRITE_BYTE(10); WRITE_BYTE(200); MESSAGE_END();
    }
    return Current(*session) && PlaySkillVoice(*session, owner, subtype, error);
}

bool PublishStatusDuration(CCSOLScenarioSession& session, const EHANDLE& target, int status,
    float duration, std::array<std::uint8_t, 3> color, ConfigError& error)
{
    auto* player = Player(session, target); if (!player || sending) return false;
    if (status < 0 || status > 32767 || !std::isfinite(duration)) return Fail(error, "invalid character skill status duration");
    const auto bits = std::bit_cast<std::int32_t>(duration); const int message = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ONE, message, nullptr, player->pev);
    WRITE_BYTE(0x51); WRITE_SHORT(status); WRITE_LONG(bits);
    for (auto b : color) WRITE_BYTE(b);
    MESSAGE_END(); return Current(session);
}
bool PublishSkillCircle(CCSOLScenarioSession& session, std::array<float, 3> origin, bool damage, ConfigError& error)
{
    if (!Current(session)) return false;
    csol::skillfx::ModelEffect effect;
    effect.model = static_cast<std::uint16_t>(damage ? resources->damageModel : resources->healModel);
    effect.scale = 0;
    for (unsigned i = 0; i < origin.size(); ++i)
    {
        const float value = origin[i] - (i == 2 ? 30.0f : 0.0f);
        if (!std::isfinite(value) || std::abs(value) > 32768) return Fail(error, "skill circle origin is invalid");
        effect.origin[i] = static_cast<float>(static_cast<int>(value * 8)) * .125f;
    }
    return Send(session, nullptr, csol::skillfx::Encode(effect));
}
bool PublishSkillScreenRGBA(CCSOLScenarioSession& session, const EHANDLE& target, int status,
    float duration, std::array<std::uint8_t, 4> color, ConfigError& error)
{
    auto* player = Player(session, target); if (!player || sending) return false;
    if (status < 0 || status > 255 || !std::isfinite(duration))
        return Fail(error, "invalid character skill screen duration");
    const auto bits = std::bit_cast<std::int32_t>(duration); const int message = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ONE, message, nullptr, player->pev);
    WRITE_BYTE(0x50); WRITE_BYTE(status); WRITE_LONG(bits);
    for (auto b : color) WRITE_BYTE(b);
    MESSAGE_END(); return Current(session);
}
bool PublishLoveBombExplosion(CCSOLScenarioSession& session, std::array<float, 3> origin, ConfigError& error)
{
    if (!Current(session) || sending) return false;
    for (float value : origin)
        if (!std::isfinite(value) || std::abs(value) > 32768) return Fail(error, "love bomb explosion origin is invalid");
    const Vector original(origin.data());
    Vector position = original;
    for (unsigned i = 0; i < 3; ++i)
    {
        const float random = RANDOM_FLOAT(i == 2 ? 0 : -10, i == 2 ? 5 : 10);
        if (!Current(session)) return false;
        position[i] += random;
        if (!std::isfinite(position[i])) return Fail(error, "love bomb explosion origin is invalid");
    }
    const int scale = RANDOM_LONG(15, 17);
    if (!Current(session)) return false;
    const int sprite = resources->explosionSprite;
    const Sending guard;
    MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, original);
    WRITE_BYTE(TE_EXPLOSION);
    for (unsigned i = 0; i < 3; ++i) WRITE_COORD(position[i]);
    WRITE_SHORT(sprite); WRITE_BYTE(scale); WRITE_BYTE(15); WRITE_BYTE(0); MESSAGE_END();
    return Current(session);
}
bool PublishLotusSkillEffect(CCSOLScenarioSession& session, std::array<float, 3> origin, float duration, ConfigError& error)
{
    if (!Current(session)) return false;
    csol::skillfx::ModelEffect effect;
    effect.model = static_cast<std::uint16_t>(resources->lotusModel);
    // Original106EF99D raises the retained ground point by one unit. The
    // sender's *10 values are wire fixed-point, not lifetime/scale multipliers.
    origin[2] += 1;
    effect.origin = origin; effect.life = duration; effect.scale = 1.5f;
    effect.alpha = 255; effect.fadeIn = effect.fadeSpeed = 0;
    if (!csol::skillfx::Valid(effect)) return Fail(error, "lotus skill effect parameters are invalid");
    return Send(session,nullptr,csol::skillfx::Encode(effect));
}
bool PublishShieldSkillEffect(CCSOLScenarioSession& session, const EHANDLE& owner, int subtype, ConfigError& error)
{
    auto* player = Player(session, owner);
    if (!player || (subtype != 41 && subtype != 42)) return false;
    const int index = player->entindex();
    if (!Current(session) || index < 1 || index > 32) return false;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(subtype == 41 ? resources->shieldAttackSprite : resources->shieldHealSprite);
    effect.model.owner = static_cast<std::uint16_t>(index);
    effect.model.frameMax = 30; effect.model.life = 1; effect.model.frameRate = 30;
    effect.model.alpha = 255; effect.model.fadeSpeed = effect.model.fadeIn = 0;
    effect.model.scale = 4.4f; effect.model.renderMode = 5;
    effect.angles = {90, 0, 0};
    effect.flags = csol::skillfx::Loop | csol::skillfx::GroundFollow | csol::skillfx::NoInterpolation;
    // MP106ED750/106ED560 -> TE145: 10*39.37/88 scale, truncated to
    // tenths. The client follows feet with hull2/world-only ground tracing.
    if (!csol::skillfx::Valid(effect)) return Fail(error, "shield skill world effect is invalid");
    return Send(session, nullptr, csol::skillfx::Encode(effect));
}
bool PublishEscortSkillEffect(CCSOLScenarioSession& session, const EHANDLE& owner, ConfigError& error)
{
    auto* player = Player(session, owner);
    if (!player) return false;
    const int index = player->entindex();
    if (!Current(session) || index < 1 || index > 32) return false;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(resources->escortSprite);
    effect.model.owner = static_cast<std::uint16_t>(index);
    effect.model.life = 1.5f; effect.model.frameRate = 30; effect.model.frameMax = 45;
    effect.model.scale = 0.5f; effect.model.alpha = 255;
    effect.model.fadeSpeed = effect.model.fadeIn = 0;
    effect.flags = csol::skillfx::Loop | csol::skillfx::HideLocalFirstPerson;
    // Native secondary0x400 hides this owner's effect only in first person.
    // Keep it in third person and for the other connected players.
    if (!csol::skillfx::Valid(effect)) return Fail(error, "escort skill world effect is invalid");
    return Send(session, nullptr, csol::skillfx::Encode(effect));
}
bool PublishStoredAttackSkillEffect(CCSOLScenarioSession& session, const EHANDLE& owner, ConfigError& error)
{
    auto* player = Player(session, owner);
    if (!player) return false;
    const int index = player->entindex();
    if (!Current(session) || index < 1 || index > 32) return false;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(resources->storedAttackSprite);
    effect.model.owner = static_cast<std::uint16_t>(index);
    effect.model.life = 1; effect.model.frameRate = 30; effect.model.frameMax = 12;
    effect.model.alpha = 255; effect.model.fadeSpeed = effect.model.fadeIn = 0;
    effect.model.scale = 3.5f; //trunc(((8*39.37)/88)*10)/10 in106E7711..773A.
    effect.angles = {90,0,0};
    effect.flags = csol::skillfx::GroundFollow | csol::skillfx::NoInterpolation;
    if (!csol::skillfx::Valid(effect)) return Fail(error, "stored attack skill world effect is invalid");
    return Send(session, nullptr, csol::skillfx::Encode(effect));
}
bool PublishGroupSkillEffect(CCSOLScenarioSession& session, const EHANDLE& owner,
    int subtype, float duration, ConfigError& error)
{
    // Type37's ten-second buff has a separate fixed one-second pulse.
    //106ED0BA..106ED176 matches the existing stored-attack sprite exactly.
    if (subtype == 37) return PublishStoredAttackSkillEffect(session, owner, error);
    auto* player = Player(session, owner);
    if (!player || sending) return false;
    if ((subtype != 32 && subtype != 33) || !std::isfinite(duration) || duration > 60)
        return Fail(error, "group character skill effect parameters are invalid");
    const int index = player->entindex();
    if (!Current(session) || index < 1 || index > 32 || Player(session, owner) != player) return false;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(subtype == 32 ? resources->groupAttackSprite : resources->groupHealSprite);
    effect.model.owner = static_cast<std::uint16_t>(index);
    //106EC584/106EC010 -> TE145:300 tenths/sec,30 frames, pitch90.
    // Native lifetime narrows to tenths; any negative value is indefinite.
    effect.model.life = duration < 0 ? 0 : static_cast<float>(static_cast<int>(duration * 10)) * .1f;
    effect.model.frameRate = 30; effect.model.frameMax = 30;
    effect.model.alpha = 255; effect.model.scale = 3.5f;
    effect.model.fadeIn = effect.model.fadeSpeed = 0;
    effect.angles = {90,0,0};
    effect.flags = csol::skillfx::Loop | csol::skillfx::GroundFollow | csol::skillfx::NoInterpolation;
    if (duration < 0) effect.flags |= csol::skillfx::Indefinite;
    if (!csol::skillfx::Valid(effect)) return Fail(error, "group character skill world effect is invalid");
    return Send(session, nullptr, csol::skillfx::Encode(effect));
}
bool PublishClearSkillStatus(CCSOLScenarioSession& session, const EHANDLE& target, int status, ConfigError& error)
{
    auto* player = Player(session, target);
    if (!player || sending) return false;
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte ||
        !g_engfuncs.pfnWriteShort || status < 0 || status > 32767)
        return Fail(error, "character skill status clear transport is unavailable");
    const int message = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ONE, message, nullptr, player->pev);
    WRITE_BYTE(207); WRITE_SHORT(status); MESSAGE_END();
    return Current(session);
}
bool PublishDragonShieldRetirementEffect(CCSOLScenarioSession& session, const EHANDLE& target, ConfigError& error)
{
    auto* player = Player(session, target);
    if (!player || sending) return false;
    if (!g_engfuncs.pfnEmitSound) return Fail(error, "dragon shield retirement sound transport is unavailable");
    const int index = player->entindex();
    if (!Current(session) || index < 1 || index > 32 || Player(session, target) != player) return false;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(resources->dragonShieldRetireModel);
    effect.model.owner = static_cast<std::uint16_t>(index);
    //106DE8A4..106DE93D executes TE145 with sequence1, rate10 tenths,
    // life3 tenths and a one-unit offset in addition to ground following.
    effect.model.sequence = 1; effect.model.frameMax = 21;
    effect.model.origin[2] = 1; effect.model.life = .3f; effect.model.frameRate = 1;
    effect.model.scale = 0; effect.model.alpha = 255;
    effect.model.fadeIn = effect.model.fadeSpeed = 0;
    effect.flags = csol::skillfx::GroundFollow | csol::skillfx::Persist;
    if (!csol::skillfx::Valid(effect)) return Fail(error, "dragon shield retirement world effect is invalid");
    if (!Send(session, nullptr, csol::skillfx::Encode(effect))) return false;
    if (!(player = Player(session, target))) return false;
    EMIT_SOUND_DYN2(player->edict(), CHAN_AUTO, "weapons/gunkata_skill_last_exp.wav", 1, .8f, 0, 100);
    return Current(session);
}
bool PublishAuraSkillEffect(CCSOLScenarioSession& session, const EHANDLE& owner,
    std::uint32_t& effectId, bool enabled, ConfigError& error)
{
    // Original106EA7E0/106EA9C0 retain the TE165 constructor's zero scale.
    return enabled ? PublishManagedWorld(session, owner, effectId, 0, error) :
        RemoveManagedWorld(session, effectId);
}
bool PublishReflectionSkillEffect(CCSOLScenarioSession& session, const EHANDLE& owner,
    std::uint32_t& effectId, bool enabled, ConfigError& error)
{
    //106EC7D0 applies the same uniform2*.3 scale to all three axes.
    return enabled ? PublishManagedWorld(session, owner, effectId, .6f, error) :
        RemoveManagedWorld(session, effectId);
}
bool PublishReflectionSkillPulse(CCSOLScenarioSession& session, const EHANDLE& owner, ConfigError& error)
{
    auto* player = Player(session, owner);
    if (!player || sending) return false;
    const auto bound = resources;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(bound->buffSkillModel);
    for (unsigned axis = 0; axis < 3; ++axis) effect.model.origin[axis] = player->pev->origin[axis];
    effect.model.origin[2] -= (player->pev->flags & FL_DUCKING) ? 18.0f : 36.0f;
    //106F1691..106F1796 is an independent TE145 at the captured position,
    // not another attached instance. Raw0x10000 requests looping, not persist.
    effect.model.frameMax = 0;
    effect.model.life = 1;
    effect.model.frameRate = 0;
    effect.model.alpha = 255;
    effect.model.renderMode = 5;
    effect.model.scale = .6f;
    effect.model.fadeSpeed = 5;
    effect.model.fadeIn = 0;
    effect.flags = csol::skillfx::Loop;
    if (!csol::skillfx::Valid(effect)) return Fail(error, "reflection pulse parameters are invalid");
    return Send(session, nullptr, csol::skillfx::Encode(effect)) &&
        resources == bound && Player(session, owner) == player;
}
bool PublishShieldReturnCircle(CCSOLScenarioSession& session,
    std::array<float, 3> origin, ConfigError& error, bool damage)
{
    if (!Current(session)) return false;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(damage ? resources->damageModel : resources->healModel);
    for (unsigned i = 0; i < origin.size(); ++i)
    {
        const float value = origin[i] - (i == 2 ? 30.0f : 0.0f);
        if (!std::isfinite(value) || std::abs(value) > 32768)
            return Fail(error, "shield return circle origin is invalid");
        effect.model.origin[i] = static_cast<float>(static_cast<int>(value * 8)) * .125f;
    }
    //106EE115 -> original MP/HW: life.4,fadeIn.4,fadeSpeed8,rate1,
    // alpha200,scale0. It has no native PERSIST flag.
    effect.model.scale = 0;
    return Send(session, nullptr, csol::skillfx::Encode(effect));
}
bool PublishSmokeSkillStart(CCSOLScenarioSession& session, const EHANDLE& owner,
    std::array<float, 3> origin, ConfigError& error)
{
    auto* player = Player(session, owner);
    if (!player || !session.SmokeEvent() || !g_engfuncs.pfnPlaybackEvent || !g_engfuncs.pfnEmitAmbientSound)
        return Fail(error, "character smoke event transport is unavailable");
    for (const float value : origin)
        if (!std::isfinite(value) || std::abs(value) > 32768) return Fail(error, "character smoke origin is invalid");
    Vector position(origin.data()), color(125,125,125);
    g_engfuncs.pfnPlaybackEvent(0, nullptr, session.SmokeEvent(), 0,
        position, color, 4.2f + 1.0f, 150, static_cast<int>(2.5f * 39.37f), 10, 0, 0);
    player = Player(session, owner);
    if (!player) return false;
    EMIT_AMBIENT_SOUND(player->edict(), position,
        "zs_skill/buffclass22s2ct_zbs_skill.wav", .5f, .8f, 0, 100);
    return Current(session);
}
bool PublishSmokeSkillWave(CCSOLScenarioSession& session, std::array<float, 3> origin, ConfigError& error)
{
    if (!Current(session)) return false;
    csol::skillfx::WorldEffect effect;
    effect.model.model = static_cast<std::uint16_t>(resources->smokeWaveModel);
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!std::isfinite(origin[i]) || std::abs(origin[i]) > 32768)
            return Fail(error, "character smoke wave origin is invalid");
        effect.model.origin[i] = static_cast<float>(static_cast<int>(origin[i] * 8)) * .125f;
    }
    // MP106EA674 ->115302F0 ->HW37850920: fade starts at0.1 seconds,
    // frameRate1, no looping/persistence, and native model-default scale0.
    effect.model.life = .1f; effect.model.frameRate = 1; effect.model.frameMax = 256;
    effect.model.alpha = 255; effect.model.fadeSpeed = 5; effect.model.fadeIn = 0;
    effect.model.scale = 0; effect.model.renderMode = 5;
    return Send(session, nullptr, csol::skillfx::Encode(effect));
}
bool PublishHealingDuration(CCSOLScenarioSession& session, const EHANDLE& target, float duration, ConfigError& error)
{ return PublishStatusDuration(session, target, 27, duration, {255,255,255}, error); }
bool PublishAmmoDuration(CCSOLScenarioSession& session, const EHANDLE& target, float duration, ConfigError& error)
{
    auto* player = Player(session, target); if (!player || sending) return false;
    if (!std::isfinite(duration)) return Fail(error, "invalid character ammo duration");
    const auto bits = std::bit_cast<std::int32_t>(duration); const int message = resources->mpMessage;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ONE, message, nullptr, player->pev);
    WRITE_BYTE(0x36); WRITE_BYTE(0); WRITE_LONG(bits);
    WRITE_BYTE(255); WRITE_BYTE(233); WRITE_BYTE(196); WRITE_BYTE(255); MESSAGE_END(); return Current(session);
}
bool PublishInfiniteAmmo(CCSOLScenarioSession& session, const EHANDLE& target, bool enabled, ConfigError&)
{
    auto* player = Player(session, target); if (!player || sending) return false;
    const int index = player->entindex(), message = resources->mp2Message;
    if (!Current(session) || index < 1 || index > 32) return false;
    const Sending guard;
    MESSAGE_BEGIN(MSG_ALL, message); WRITE_BYTE(0x4A); WRITE_SHORT(index); WRITE_BYTE(enabled); MESSAGE_END();
    return Current(session);
}
bool PlaySkillVoice(CCSOLScenarioSession& session, const EHANDLE& owner, int subtype, ConfigError& error)
{
    auto* player = Player(session, owner); if (!player) return false;
    const char* sample{};
    if (subtype == 22 || subtype == 23 || subtype == 24 || subtype == 25 || subtype == 26 || subtype == 27 || subtype == 28 || subtype == 29 || subtype == 30 || subtype == 31 || subtype == 32 || subtype == 33 || subtype == 36 || subtype == 37 || subtype == 38 || subtype == 39 || subtype == 40 ||
        subtype == 41 || subtype == 42 || subtype == 43 || subtype == 44 || subtype == 45 || subtype == 46 || subtype == 54 || subtype == 55)
        sample = "zs_skill/zs_skill1.wav";
    else if (subtype == 1 || subtype == 3 || subtype == 5 || subtype == 6 ||
        subtype == 16 || subtype == 19 || subtype == 34 || subtype == 47)
        sample = "player/buffclass19s1ct/buffclass19s1ct_useskill.wav";
    else if (subtype == 2 || subtype == 4 || subtype == 7 || subtype == 8 || subtype == 9 ||
        subtype == 12 || subtype == 14 || subtype == 15 || subtype == 17 || subtype == 18 || subtype == 20 || subtype == 48 || subtype == 49 || subtype == 50 ||
        subtype == 51 || subtype == 52 || subtype == 53)
        sample = "player/buffclass19s1tr/buffclass19s1tr_useskill.wav";
    else return Fail(error, "character skill preparation sound is not implemented");
    EMIT_SOUND_DYN2(player->edict(), CHAN_AUTO, sample, .5f, .8f, 0, 100);
    return Current(session);
}
bool PlayActivationRadio(CCSOLScenarioSession& session, const EHANDLE& owner, ConfigError& error)
{
    auto* player = Player(session, owner); if (!player) return false;
    std::string source;
    if (!SkillCharacterSourceName(*player, source)) return Fail(error, "character skill radio identity is unavailable");
    source = csol::skillfx::RadioKey(source);
    const auto resource = resources;
    const auto found = resource->voices.find(source);
    // An absent sentence has no audio. It must not repeat the v40 sound.
    if (found == resource->voices.end() || found->second.empty()) return true;
    const int count = static_cast<int>(found->second.size());
    const int index = count == 1 ? 0 : RANDOM_LONG(0, count - 1);
    if (!Current(session) || !(player = Player(session, owner))) return false;
    return Send(session, player, csol::skillfx::EncodeRadio(found->second[index]));
}
float ActivationUiDelay() { return 1.7f; } // MP10044520 / float bits3FD9999A.
}
