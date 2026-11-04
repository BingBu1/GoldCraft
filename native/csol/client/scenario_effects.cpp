// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "user_message_chain.hpp"
#include "scenario_effects.hpp"
#include "monster_impact.hpp"
#include "oberon_hint.hpp"
#include "frozen_death.hpp"
#include "frozen_smoke.hpp"
#include "../frozen_smoke.hpp"
#include "../scenario_healing.hpp"
#include "../scenario_monster_effects.hpp"
#include "../scenario_part_effects.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <bit>
#include <cstdint>
#include <algorithm>
#include <climits>
#include <cmath>

namespace csol::client
{
namespace
{
pfnUserMsgHook previousMP{}, previousInit{}, previousHealth{}, previousHide{}, previousReset{};
pfnUserMsgHook previousFrozenSmoke{};
std::array<float, 4> pain{};
int painSprite{}, painResolution{}, health{100}, hidden{};
bool healthKnown{}, healthHookPending{};
double painTime{};
struct Owned { TEMPENTITY* entity{}; int token{}, type{}; };
std::array<Owned, 512> attached;
int nextToken{};
std::uint64_t effectEpoch{};

void Follow(TEMPENTITY* temp, float, float now)
{
    const auto generation = effectEpoch;
    const int token = temp->entity.curstate.iuser4;
    const auto* owner = engine.GetEntityByIndex ? engine.GetEntityByIndex(temp->clientIndex) : nullptr;
    if (generation != effectEpoch || temp->callback != Follow ||
        temp->entity.curstate.iuser4 != token) return;
    if (!owner || !owner->model)
    {
        temp->die = now;
        temp->flags |= FTENT_NOMODEL;
        return;
    }
    for (int i = 0; i < 3; ++i) temp->entity.origin[i] = owner->origin[i] + temp->tentOffset[i];
}
bool Owns(const Owned& effect)
{
    return effect.entity && effect.entity->callback == Follow &&
        effect.entity->entity.curstate.iuser4 == effect.token;
}
auto AvailableAttachment(double now)
{
    return std::find_if(attached.begin(), attached.end(), [&](const Owned& effect)
    { return !Owns(effect) || now > effect.entity->die + 1.0; });
}
void RetireUnclaimedSprite(TEMPENTITY* temp)
{
    if (!temp->callback && !temp->entity.curstate.iuser4)
    {
        temp->die = -1;
        temp->flags |= FTENT_NOMODEL;
    }
}
bool AcceptAllocatedSprite(TEMPENTITY* temp, std::uint64_t generation)
{
    if (!temp) return false;
    // An allocation can finish after a reset. Retire only the unclaimed
    // result; a nested consumer may already have reused this engine slot.
    if (generation == effectEpoch)
        return !temp->callback && !temp->entity.curstate.iuser4;
    RetireUnclaimedSprite(temp);
    return false;
}
void AttachedSprite(int ownerIndex, float height, float duration, int type)
{
    if (!std::isfinite(height) || !std::isfinite(duration) ||
        !engine.GetEntityByIndex || !engine.CL_LoadModel || !engine.GetClientTime ||
        !engine.pEfxAPI || !engine.pEfxAPI->CL_TempEntAllocHigh) return;
    const auto generation = effectEpoch;
    const int part = part_effects::Index(type);
    // Original43 has zero hold time.46/47/53 start their one-second fade
    // at max(0,duration-1), including a duration shorter than one second.
    if (part >= 0) duration = type == 43 ? 0.0f : std::max(0.0f, duration - 1.0f);
    else if (duration < 0) return;
    const auto* owner = engine.GetEntityByIndex(ownerIndex);
    if (generation != effectEpoch) return;
    if (!owner || !owner->model) return;
    // Original0281C1D0 clears only table79/81/82, not an earlier table90.
    // Keep ownership checks so a recycled engine temp slot is never killed.
    if (part >= 0)
        for (auto& effect : attached)
            if (Owns(effect) && effect.entity->clientIndex == ownerIndex &&
                (effect.type == 43 || effect.type == 46 || effect.type == 47))
            {
                effect.entity->die = -1;
                effect.entity->flags |= FTENT_NOMODEL;
                effect = {};
            }
    int index{};
    const char* path = part >= 0 ? part_effects::Sprites[part] :
        type == 60 ? "sprites/DoubleSideA.spr" : type == 61 ? "sprites/DoubleSideB.spr" :
        type == 112 ? "sprites/zbt_invincibility.spr" : "sprites/ef_sbmine_debuff.spr";
    auto* model = engine.CL_LoadModel(path, &index);
    if (generation != effectEpoch) return;
    if (!model || model->type != mod_sprite || model->numframes <= 0) return;
    const float now = static_cast<float>(engine.GetClientTime());
    if (generation != effectEpoch) return;
    if (!std::isfinite(now) || !std::isfinite(now + duration)) return;
    // Original199/62 first expires this owner's existing table102 effect.
    if (type == 62)
        for (auto& effect : attached)
            if (Owns(effect) && effect.entity->clientIndex == ownerIndex && effect.entity->entity.model == model)
                effect.entity->die = now;
    if (AvailableAttachment(now) == attached.end()) return;
    float position[]{owner->origin[0], owner->origin[1], owner->origin[2] + height};
    auto* temp = engine.pEfxAPI->CL_TempEntAllocHigh(position, model);
    if (!AcceptAllocatedSprite(temp, generation)) return;
    // The allocator may dispatch another effect. Select ownership only
    // after it returns so both allocations retain their reset records.
    const auto slot = AvailableAttachment(now);
    if (slot == attached.end()) { RetireUnclaimedSprite(temp); return; }
    nextToken = nextToken == INT_MAX ? 1 : nextToken + 1;
    *slot = {temp, nextToken, type};
    temp->entity.curstate.iuser4 = nextToken;
    temp->flags = FTENT_PERSIST | FTENT_CLIENTCUSTOM;
    // Original 60/61 use static frame0 (0x10A000);112 loops without a
    // fade (0x11A100). Only slow-wall62 uses the timed fade-out0x11A180.
    if (part >= 0 || type == 62 || type == 112) temp->flags |= FTENT_SPRANIMATE | FTENT_SPRANIMATELOOP;
    if (part >= 0 || type == 62) temp->flags |= FTENT_FADEOUT;
    temp->callback = Follow;
    temp->clientIndex = static_cast<short>(ownerIndex);
    temp->tentOffset[0] = temp->tentOffset[1] = 0;
    temp->tentOffset[2] = height;
    temp->die = now + duration;
    temp->fadeSpeed = 1;
    temp->frameMax = static_cast<float>(model->numframes);
    temp->entity.curstate.frame = 0;
    temp->entity.curstate.framerate = 10;
    temp->entity.curstate.rendermode = kRenderTransAdd;
    temp->entity.curstate.renderfx = 14;
    temp->entity.curstate.renderamt = temp->entity.baseline.renderamt = 254;
    temp->entity.curstate.scale = type == 112 ? .9f : type == 62 ? .3f : 1.0f;
}
void Heal(int ownerIndex, int type)
{
    if (!engine.GetEntityByIndex || !engine.CL_LoadModel || !engine.GetClientTime ||
        !engine.pEfxAPI || !engine.pEfxAPI->CL_TempEntAllocHigh) return;
    const auto generation = effectEpoch;
    const auto* owner = engine.GetEntityByIndex(ownerIndex);
    if (generation != effectEpoch) return;
    if (!owner || !owner->model) return;
    int index{};
    const bool self = type == 13;
    // Original MPToCL4/effect44 selects HW table80, then follows at z60.
    const char* path = type == 40 ? "sprites/zb_skill_hpbuff.spr" :
        type == 44 ? "sprites/zombiarmorheal_head.spr" : healing::Sprites[self ? 1 : 0];
    auto* model = engine.CL_LoadModel(path, &index);
    if (generation != effectEpoch) return;
    if (!model || model->type != mod_sprite || model->numframes <= 0) return;
    const double now = engine.GetClientTime();
    if (generation != effectEpoch || !std::isfinite(now)) return;
    if (AvailableAttachment(now) == attached.end()) return;
    const float offset = type == 40 || type == 44 ? 60.0f : self ? 25.0f : 40.0f;
    float position[]{owner->origin[0], owner->origin[1], owner->origin[2] + offset};
    auto* temp = engine.pEfxAPI->CL_TempEntAllocHigh(position, model);
    if (!AcceptAllocatedSprite(temp, generation)) return;
    const auto slot = AvailableAttachment(now);
    if (slot == attached.end()) { RetireUnclaimedSprite(temp); return; }
    nextToken = nextToken == INT_MAX ? 1 : nextToken + 1;
    *slot = {temp, nextToken};
    temp->entity.curstate.iuser4 = nextToken;
    // CSO's attachment bit accepts arbitrary entities. The stock client only
    // follows player slots, so use its custom callback for the same offset.
    temp->flags = FTENT_FADEOUT | FTENT_SPRANIMATE | FTENT_PERSIST |
        FTENT_SPRANIMATELOOP | FTENT_CLIENTCUSTOM;
    temp->callback = Follow;
    temp->clientIndex = static_cast<short>(ownerIndex);
    temp->tentOffset[0] = temp->tentOffset[1] = 0;
    temp->tentOffset[2] = offset;
    temp->die = static_cast<float>(now + 1.0);
    temp->fadeSpeed = 1.0f;
    temp->frameMax = static_cast<float>(model->numframes);
    temp->entity.curstate.frame = 0;
    temp->entity.curstate.framerate = 10;
    temp->entity.curstate.rendermode = kRenderTransAdd;
    temp->entity.curstate.renderfx = 14;
    temp->entity.curstate.renderamt = temp->entity.baseline.renderamt = 254;
    temp->entity.curstate.scale = type == 40 ? .5f : 1.0f;
}
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size <= 0) return previousMP ? previousMP(name, size, data) : 1;
    if (ConsumeOberonHint(size, data)) return 1;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0] == 112)
    {
        if (size != 5) return 1;
        const int owner = bytes[1] | (int(bytes[2]) << 8);
        const auto duration = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[3] | (int(bytes[4]) << 8)));
        if (owner > 0 && owner <= SHRT_MAX && duration >= 0)
            AttachedSprite(owner, 0, static_cast<float>(duration), 112);
        return 1;
    }
    if (bytes[0] == 199)
    {
        if (size < 4) return 1;
        if (part_effects::Index(bytes[3]) < 0 && bytes[3] != 60 && bytes[3] != 61 && bytes[3] != 62)
            return previousMP ? previousMP(name, size, data) : 1;
        if (size != 12) return 1;
        const int owner = bytes[1] | (int(bytes[2]) << 8);
        const auto number = [&](int offset)
        {
            std::uint32_t bits{};
            for (unsigned i = 0; i < 4; ++i) bits |= std::uint32_t(bytes[offset + i]) << (8 * i);
            return std::bit_cast<float>(bits);
        };
        if (owner > 0 && owner <= SHRT_MAX) AttachedSprite(owner, number(4), number(8), bytes[3]);
        return 1;
    }
    if (bytes[0] != 4) return previousMP ? previousMP(name, size, data) : 1;
    if (size < 4) return 1;
    if (bytes[3] != 9 && bytes[3] != 13 && bytes[3] != 40 && bytes[3] != 44) return previousMP ? previousMP(name, size, data) : 1;
    if (size != 4) return 1;
    const int owner = bytes[1] | (int(bytes[2]) << 8);
    if (owner > 0 && owner <= SHRT_MAX) Heal(owner, bytes[3]);
    return 1;
}
int InitHUD(const char* name, int size, void* data)
{
    ResetScenarioEffects();
    return previousInit ? previousInit(name, size, data) : 1;
}
int Health(const char* name, int size, void* data)
{
    if (data && (size == 1 || size == 4))
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        std::uint32_t value = bytes[0];
        if (size == 4)
            for (unsigned i = 1; i < 4; ++i) value |= std::uint32_t(bytes[i]) << (8 * i);
        if (value <= INT32_MAX)
        {
            health = static_cast<int>(value);
            healthKnown = true;
        }
    }
    return previousHealth ? previousHealth(name, size, data) : 1;
}
int Hide(const char* name, int size, void* data)
{
    if (size == 1 && data) hidden = *static_cast<const unsigned char*>(data);
    return previousHide ? previousHide(name, size, data) : 1;
}
int ResetHUD(const char* name, int size, void* data)
{
    pain = {};
    health = 100;
    healthKnown = false;
    return previousReset ? previousReset(name, size, data) : 1;
}
void TrapDirection(const float* origin)
{
    if (origin[0] == 0 && origin[1] == 0 && origin[2] == 0) { pain = {}; return; }
    float delta[3];
    for (int i = 0; i < 3; ++i) delta[i] = origin[i] - view.simorg[i];
    const float square = delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2];
    if (square <= 2500.0f) { pain.fill(1.0f); return; }
    const float length = std::sqrt(square);
    float front = 0, right = 0;
    for (int i = 0; i < 3; ++i)
    {
        front += delta[i] / length * view.forward[i];
        right += delta[i] / length * view.right[i];
    }
    const std::array<float, 4> strength{front, right, -front, -right};
    for (std::size_t i = 0; i < pain.size(); ++i)
        if (static_cast<double>(strength[i]) > 0.3) pain[i] = std::max(pain[i], strength[i]);
}
int ReceiveMonsterEffect(const char*, int size, void* data)
{
    SynchronizeSession();
    if (ConsumeMonsterImpact(size, data)) return 1;
    if (!data || size != 7) return 1;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0] != monster_effects::Impact && bytes[0] != monster_effects::TrapDirection &&
        bytes[0] != monster_effects::ZombieBomb) return 1;
    float origin[3];
    for (int i = 0; i < 3; ++i)
    {
        const auto raw = static_cast<std::uint16_t>(bytes[1 + i * 2] | (bytes[2 + i * 2] << 8));
        origin[i] = static_cast<float>(std::bit_cast<std::int16_t>(raw)) * 0.125f;
    }
    if (bytes[0] == monster_effects::TrapDirection) { TrapDirection(origin); return 1; }
    origin[2] += 20.0f;
    auto* fx = engine.pEfxAPI;
    auto* ev = engine.pEventAPI;
    if (fx && fx->R_DefaultSprite && fx->R_Sprite_Explode && ev && ev->EV_FindModelIndex)
    {
        // Original TE128's second byte selects the sprite: zero is the
        // zombie bomb, one is Deimos. Both use the same additive explosion.
        const int index = ev->EV_FindModelIndex(bytes[0] == monster_effects::ZombieBomb ?
            monster_effects::ZombieBombSprite : monster_effects::ImpactSprite);
        if (index > 0)
            if (auto* temp = fx->R_DefaultSprite(origin, index, 30.0f))
                fx->R_Sprite_Explode(temp, 2.5f, 0);
    }
    if (ev && ev->EV_PlaySound)
        ev->EV_PlaySound(-1, origin, CHAN_AUTO, monster_effects::ImpactSound, 1.0f, 0.3f, 0, 100);
    return 1;
}
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    if (hook != Health)
    {
        InstallChainedUserMessage(name, hook, previous);
        return;
    }
    const auto old = api->HookUserMsg(name, hook);
    if (old != hook) previous = old;
    if (!old) engine.pfnHookUserMsg(name, hook);
}
int ReceiveFrozenSmoke(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (ConsumeFrozenSmoke(size, data)) return 1;
    return previousFrozenSmoke ? previousFrozenSmoke(name, size, data) : 1;
}
}
void InitializeScenarioEffects()
{
    InitializeFrozenDeath();
    Install(csol::frozen_smoke::SmokeMessage, ReceiveFrozenSmoke, previousFrozenSmoke);
    Install("MPToCL", Receive, previousMP);
    Install("InitHUD", InitHUD, previousInit);
    Install("ResetHUD", ResetHUD, previousReset);
    // GoldCraft validates the original Health handler after the complete
    // downstream HUD_Init chain returns. Observe it only from HUD_Frame.
    healthHookPending = true;
    Install("HideWeapon", Hide, previousHide);
    engine.pfnHookUserMsg(monster_effects::Message, ReceiveMonsterEffect);
}
void ScenarioEffectsFrame()
{
    FrozenSmokeFrame();
    if (!healthHookPending) return;
    healthHookPending = false;
    Install("Health", Health, previousHealth);
}
void ResetScenarioEffects()
{
    ++effectEpoch;
    ResetFrozenDeath();
    ResetFrozenSmoke();
    ResetOberonHints();
    pain = {};
    painSprite = painResolution = hidden = 0;
    health = 100;
    healthKnown = false;
    painTime = 0;
    for (auto& effect : attached)
    {
        if (Owns(effect))
        {
            effect.entity->die = -1;
            effect.entity->flags |= FTENT_NOMODEL;
        }
        effect = {};
    }
}
void DrawScenarioEffects(float time, int intermission)
{
    const double now = engine.GetClientTime ? engine.GetClientTime() : static_cast<double>(time);
    const double delta = painTime > 0 && now >= painTime ? now - painTime : 0;
    painTime = now;
    if (intermission || !healthKnown || health <= 0 || (hidden & (4 | 8)) ||
        std::none_of(pain.begin(), pain.end(), [](float value) { return value > 0; })) return;
    if (!engine.pfnGetScreenInfo || !engine.pfnSPR_Load || !engine.pfnSPR_Width || !engine.pfnSPR_Height ||
        !engine.pfnSPR_Set || !engine.pfnSPR_DrawAdditive) return;
    SCREENINFO screen{};
    screen.iSize = sizeof(screen);
    if (!engine.pfnGetScreenInfo(&screen)) return;
    const int resolution = screen.iWidth < 640 ? 320 : 640;
    if (!painSprite || painResolution != resolution)
    {
        painSprite = engine.pfnSPR_Load(resolution == 320 ? "sprites/320_pain.spr" : "sprites/640_pain.spr");
        painResolution = resolution;
    }
    if (!painSprite) return;
    for (int frame = 0; frame < 4; ++frame)
    {
        auto& strength = pain[frame];
        if (!(strength > 0.4f)) { strength = 0; continue; }
        const int width = engine.pfnSPR_Width(painSprite, frame);
        const int height = engine.pfnSPR_Height(painSprite, frame);
        const int x = screen.iWidth / 2 + (frame == 1 ? 2*width : frame == 3 ? -3*width : -width/2);
        const int y = screen.iHeight / 2 + (frame == 0 ? -3*height : frame == 2 ? 2*height : -height/2);
        const int brightness = static_cast<int>(std::max(strength, 0.5f) * 255);
        // Match the host CS HUD's normal amber tint. Original CSO's packed
        // configurable HUD color has not yet been imported into this module.
        engine.pfnSPR_Set(painSprite, (health > 25 ? 255 : 250) * brightness / 255,
            health > 25 ? 160 * brightness / 255 : 0, 0);
        engine.pfnSPR_DrawAdditive(frame, x, y, nullptr);
        strength = std::max(0.0f, strength - static_cast<float>(delta * 2.0));
    }
}
void ShutdownScenarioEffects()
{
    healthHookPending = false;
    ResetScenarioEffects();
}
}
