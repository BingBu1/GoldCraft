// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "user_message_chain.hpp"
#include "character_skill_effects.hpp"
#include "character_skill_display.hpp"
#include "camera.hpp"
#include "weapons/weapon_inventory.hpp"
#include "../character_skill_protocol.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <pm_defs.h>
#include <algorithm>
#include <climits>
#include <limits>
#include <cstring>

namespace csol::client
{
namespace
{
pfnUserMsgHook previousEffects{}, previousMP2{}, previousInit{}, previousReset{};
std::array<bool, 33> infiniteAmmo;
int infiniteSprite{};
struct Effect
{
    TEMPENTITY* temp{};
    model_t* model{};
    int token{};
    float birth{}, die{};
    skillfx::ModelEffect parameters;
    std::uint8_t worldFlags{};
    std::uint32_t managedId{};
};
std::array<Effect, 128> effects;
int nextToken{};
void Animate(TEMPENTITY*, float, float);
bool Owns(const Effect& effect)
{
    return effect.temp && effect.temp->callback == Animate && effect.temp->entity.model == effect.model &&
        effect.temp->entity.curstate.iuser4 == effect.token &&
        effect.temp->entity.curstate.fuser4 == effect.birth;
}
void Retire(Effect& effect)
{
    if (Owns(effect))
    {
        ForgetSceneEntity(&effect.temp->entity);
        effect.temp->die = -1;
        effect.temp->flags |= FTENT_NOMODEL;
    }
    effect = {};
}
void Animate(TEMPENTITY* temp, float, float now)
{
    const auto found = std::find_if(effects.begin(), effects.end(), [&](const Effect& effect)
    { return effect.temp == temp && Owns(effect); });
    if (found == effects.end()) { temp->die = now; temp->flags |= FTENT_NOMODEL; return; }
    auto& effect = *found;
    const auto token = effect.token;
    const auto current = [&] { return effect.token == token && Owns(effect); };
    if (!std::isfinite(now) || now < effect.birth) { Retire(effect); return; }
    const int alpha = effect.managedId ? int(effect.parameters.alpha) : skillfx::Alpha(effect.parameters, effect.die - now);
    // Zero at the start of a fade-in is alive; zero after expiry is dead.
    if (now > effect.die && alpha <= 0) { Retire(effect); return; }
    temp->entity.curstate.renderamt = std::clamp(alpha, 0, 255);
    if (effect.worldFlags & skillfx::HideLocalFirstPerson)
    {
        // CSO secondary0x400 (02A437E0): retain lifetime/animation while
        // suppressing the local first-person submission; camera changes
        // during the effect must reveal it again.
        const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
        if (!current()) return;
        const bool firstPerson = local && local->index == effect.parameters.owner && !CameraIsThirdPerson();
        if (!current()) return;
        if (firstPerson)
            temp->flags |= FTENT_NOMODEL;
        else temp->flags &= ~FTENT_NOMODEL;
    }
    if (effect.parameters.owner)
    {
        const auto* owner = engine.GetEntityByIndex ? engine.GetEntityByIndex(effect.parameters.owner) : nullptr;
        if (!current()) return;
        if (!owner || !owner->model || (owner->curstate.effects & EF_NODRAW)) { Retire(effect); return; }
        auto origin = std::array{owner->origin[0], owner->origin[1], owner->origin[2]};
        if (effect.worldFlags & skillfx::LocalFirstPersonViewOrigin)
        {
            // Original02A3FB9B..02A3FBE2 uses the current V_CalcRefdef eye
            // position minus17 for the local first-person owner, then applies
            // the same standing/crouching foot trace as other owners.
            const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
            if (!current()) return;
            const bool firstPerson = local && local->index == effect.parameters.owner && !CameraIsThirdPerson();
            if (!current()) return;
            if (firstPerson)
                origin = {view.vieworg[0], view.vieworg[1], view.vieworg[2] - 17.0f};
        }
        if (!std::all_of(origin.begin(), origin.end(), [](float n) { return std::isfinite(n); }))
        { Retire(effect); return; }
        if (effect.worldFlags & skillfx::GroundFollow)
        {
            // Original client02A3FC31: point hull, world only, then one unit
            // above the traced foot position. Crouching uses the short hull.
            const auto* events = engine.pEventAPI;
            if (!events || !events->EV_SetTraceHull || !events->EV_PlayerTrace)
            { Retire(effect); return; }
            auto end = origin;
            end[2] -= owner->curstate.usehull ? 18.0f : 36.0f;
            pmtrace_t trace{};
            events->EV_SetTraceHull(2);
            if (!current()) return;
            events->EV_PlayerTrace(origin.data(), end.data(), PM_WORLD_ONLY, -1, &trace);
            if (!current()) return;
            if (!std::isfinite(trace.endpos[2])) { Retire(effect); return; }
            origin[2] = trace.endpos[2] + 1.0f;
        }
        for (unsigned i = 0; i < 3; ++i) temp->entity.origin[i] = origin[i] + effect.parameters.origin[i];
    }
}
int ModelFrames(model_t* model)
{
    if (model->type == mod_sprite) return std::max(1, model->numframes);
    const auto* header = studio.Mod_Extradata ? static_cast<const studiohdr_t*>(studio.Mod_Extradata(model)) : nullptr;
    if (!header || header->numbodyparts < 0 || header->bodypartindex < 0 || header->bodypartindex > header->length ||
        header->numbodyparts > (header->length - header->bodypartindex) / int(sizeof(mstudiobodyparts_t))) return 1;
    const auto* parts = reinterpret_cast<const mstudiobodyparts_t*>(reinterpret_cast<const byte*>(header) + header->bodypartindex);
    int count = 1;
    // Original HW378E3810 counts studio body variations, not sequence frames.
    for (int i = 0; i < header->numbodyparts; ++i)
    {
        if (parts[i].nummodels <= 0 || count > INT_MAX / parts[i].nummodels) return 1;
        count *= parts[i].nummodels;
    }
    return count;
}
void Model(const skillfx::ModelEffect& parameters, const skillfx::WorldEffect* world = nullptr,std::uint32_t managedId=0,std::uint16_t skin=0)
{
    if(managedId)
    {
        const auto old=std::find_if(effects.begin(),effects.end(),[&](const Effect& e){return e.managedId==managedId;});
        // Reliable replay of an existing instance must retain animation time.
        if(old!=effects.end()) { if(Owns(*old))return;*old={}; }
    }
    const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    if (parameters.excludedOwner && local && parameters.excludedOwner == local->index) return;
    if (!engine.GetClientTime || !studio.GetModelByIndex || !engine.pEfxAPI) return;
    const auto allocate = managedId ? engine.pEfxAPI->CL_TempEntAllocHigh : engine.pEfxAPI->CL_TempEntAlloc;
    if (!allocate) return;
    auto* model = studio.GetModelByIndex(parameters.model);
    if (!model || (model->type != mod_studio && model->type != mod_sprite)) return;
    const float now = static_cast<float>(engine.GetClientTime());
    if (!std::isfinite(now)) return;
    const float die = managedId || (world && (world->flags & skillfx::Indefinite)) ?
        std::numeric_limits<float>::max() : now + parameters.life;
    if (!std::isfinite(die)) return;
    const auto slot = std::find_if(effects.begin(), effects.end(), [&](const Effect& effect)
    { return !Owns(effect) || now > effect.temp->die || now < effect.birth; });
    if (slot == effects.end()) return;
    Retire(*slot);
    auto origin = parameters.origin;
    auto* temp = allocate(origin.data(), model);
    if (!temp) return;
    nextToken = nextToken == INT_MAX ? 1 : nextToken + 1;
    *slot = {temp, model, nextToken, now, die, parameters, world ? world->flags : std::uint8_t{},managedId};
    temp->callback = Animate;
    temp->flags = FTENT_CLIENTCUSTOM | FTENT_SPRANIMATE;
    if (world ? (world->flags & skillfx::Persist) != 0 : parameters.owner != 0) temp->flags |= FTENT_PERSIST;
    if (world && (world->flags & skillfx::Loop)) temp->flags |= FTENT_SPRANIMATELOOP;
    temp->frameMax = static_cast<float>(parameters.frameMax ? parameters.frameMax : ModelFrames(model));
    temp->die = die + (parameters.fadeSpeed > 0 ? 1.0f / parameters.fadeSpeed : 0);
    temp->entity.curstate.iuser4 = nextToken;
    temp->entity.curstate.fuser4 = now;
    temp->entity.curstate.sequence = parameters.sequence;
    temp->entity.curstate.skin = skin;
    temp->entity.curstate.frame = 0;
    temp->entity.curstate.animtime = now;
    temp->entity.curstate.framerate = parameters.frameRate;
    temp->entity.curstate.scale = parameters.scale;
    temp->entity.curstate.rendermode = parameters.renderMode;
    temp->entity.curstate.renderamt = std::clamp(skillfx::Alpha(parameters, parameters.life), 0, 255);
    temp->entity.baseline.renderamt = static_cast<int>(parameters.alpha);
    if (world)
    {
        for (unsigned i = 0; i < 3; ++i)
            temp->entity.angles[i] = temp->entity.curstate.angles[i] = world->angles[i];
        if (world->flags & skillfx::NoInterpolation) temp->entity.curstate.effects |= EF_NOINTERP;
    }
    Animate(temp, 0, now);
}
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size < 2 || size > 192) return 1;
    const auto bytes = std::span(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size));
    if (bytes[0] != skillfx::Version) return 1;
    if (bytes[1] == static_cast<std::uint8_t>(skillfx::Kind::Model))
    {
        skillfx::ModelEffect effect;
        if (skillfx::Decode(bytes, effect)) Model(effect);
    }
    else if (bytes[1] == static_cast<std::uint8_t>(skillfx::Kind::World))
    {
        skillfx::WorldEffect effect;
        if (skillfx::Decode(bytes, effect)) Model(effect.model, &effect);
    }
    else if (bytes[1] == static_cast<std::uint8_t>(skillfx::Kind::ManagedWorld))
    {
        skillfx::ManagedWorldEffect effect;
        if(skillfx::Decode(bytes,effect))Model(effect.world.model,&effect.world,effect.id,effect.skin);
    }
    else if (bytes[1] == static_cast<std::uint8_t>(skillfx::Kind::RemoveWorld))
    {
        std::uint32_t id{};
        if(skillfx::DecodeRemoveWorld(bytes,id))
            for(auto& effect:effects)if(effect.managedId==id)Retire(effect);
    }
    else if (bytes[1] == static_cast<std::uint8_t>(skillfx::Kind::Radio) && size >= 4 && bytes.back() == 0)
    {
        const std::string_view path(reinterpret_cast<const char*>(bytes.data() + 2), bytes.size() - 3);
        if (skillfx::ValidRadioPath(path) && engine.pfnPlaySoundByName)
            engine.pfnPlaySoundByName(reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data() + 2)), 1.0f);
    }
    else return previousEffects ? previousEffects(name, size, data) : 1;
    return 1;
}
int ReceiveMP2(const char* name, int size, void* data)
{
    SynchronizeSession();
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (!bytes || size <= 0 || bytes[0] != 0x4A) return previousMP2 ? previousMP2(name, size, data) : 1;
    if (size != 4) return 1;
    const int index = bytes[1] | (unsigned(bytes[2]) << 8);
    if (index > 0 && index <= 32 && bytes[3] <= 1) infiniteAmmo[index] = bytes[3] != 0;
    return 1;
}
int Init(const char* name, int size, void* data)
{
    ResetCharacterSkillEffects();
    return previousInit ? previousInit(name, size, data) : 1;
}
int Reset(const char* name, int size, void* data)
{
    ResetCharacterSkillEffects();
    return previousReset ? previousReset(name,size,data) : 1;
}
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    InstallChainedUserMessage(name, hook, previous);
}
}
void InitializeCharacterSkillEffects()
{
    InitializeCharacterSkillDisplay();
    Install(skillfx::MessageName, Receive, previousEffects);
    Install("MPToCL2", ReceiveMP2, previousMP2);
    Install("InitHUD", Init, previousInit);
    Install("ResetHUD", Reset, previousReset);
}
void ResetCharacterSkillEffects()
{
    for (auto& effect : effects) Retire(effect);
    infiniteAmmo = {};
    ResetCharacterSkillDisplay();
}
void ShutdownCharacterSkillEffects() { ResetCharacterSkillEffects(); ShutdownCharacterSkillDisplay(); }
bool CharacterSkillInfiniteAmmo(int index)
{ return index > 0 && index <= 32 && infiniteAmmo[index]; }
bool CharacterSkillInfiniteAmmoFor(int weaponId)
{
    const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    const auto* definition = FindWeaponDefinition(weaponId);
    const auto& current = CurrentExtendedWeapon();
    return local && view.health > 0 && !view.spectator && CharacterSkillInfiniteAmmo(local->index) &&
        current.state && current.id == weaponId && definition && definition->ammo1 >= 0 &&
        (definition->slot == 0 || definition->slot == 1);
}
void CharacterSkillEffectsFrame()
{
    if (!infiniteSprite && std::any_of(infiniteAmmo.begin(),infiniteAmmo.end(),[](bool value){return value;}))
        infiniteSprite = engine.pfnSPR_Load("sprites/csol/640hud12.spr");
}
void CharacterSkillEffectsVideoInit() { infiniteSprite = 0; }
bool DrawCharacterSkillInfiniteAmmo(int weaponId, int right, int y, int r, int g, int b)
{
    if (!infiniteSprite || !CharacterSkillInfiniteAmmoFor(weaponId)) return false;
    constexpr wrect_t rect{172,208,231,255};
    // Original CHudAmmo02A23403→02A230AA reserves two36px cells and
    // places the actual36x24glyph in the left cell, two pixels below digits.
    engine.pfnSPR_Set(infiniteSprite,r,g,b);
    engine.pfnSPR_DrawAdditive(0,right-72,y+2,&rect);
    return true;
}
}
