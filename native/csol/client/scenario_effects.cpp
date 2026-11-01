// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_effects.hpp"
#include "../scenario_healing.hpp"
#include "../scenario_monster_effects.hpp"
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
std::array<float, 4> pain{};
int painSprite{}, painResolution{}, health{100}, hidden{};
bool healthKnown{}, healthHookPending{};
double painTime{};
struct Owned { TEMPENTITY* entity{}; int token{}; };
std::array<Owned, 512> attached;
int nextToken{};

void Follow(TEMPENTITY* temp, float, float now)
{
    const auto* owner = engine.GetEntityByIndex ? engine.GetEntityByIndex(temp->clientIndex) : nullptr;
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
void Heal(int ownerIndex, bool self)
{
    if (!engine.GetEntityByIndex || !engine.CL_LoadModel || !engine.GetClientTime ||
        !engine.pEfxAPI || !engine.pEfxAPI->CL_TempEntAlloc) return;
    const auto* owner = engine.GetEntityByIndex(ownerIndex);
    if (!owner || !owner->model) return;
    int index{};
    auto* model = engine.CL_LoadModel(healing::Sprites[self ? 1 : 0], &index);
    if (!model || model->type != mod_sprite || model->numframes <= 0) return;
    const double now = engine.GetClientTime();
    const auto slot = std::find_if(attached.begin(), attached.end(), [&](const Owned& effect)
    { return !Owns(effect) || now > effect.entity->die + 1.0; });
    if (slot == attached.end()) return;
    float position[]{owner->origin[0], owner->origin[1], owner->origin[2] + (self ? 25.0f : 40.0f)};
    auto* temp = engine.pEfxAPI->CL_TempEntAlloc(position, model);
    if (!temp) return;
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
    temp->tentOffset[2] = self ? 25.0f : 40.0f;
    temp->die = static_cast<float>(now + 1.0);
    temp->fadeSpeed = 1.0f;
    temp->frameMax = static_cast<float>(model->numframes);
    temp->entity.curstate.frame = 0;
    temp->entity.curstate.framerate = 10;
    temp->entity.curstate.rendermode = kRenderTransAdd;
    temp->entity.curstate.renderfx = 14;
    temp->entity.curstate.renderamt = temp->entity.baseline.renderamt = 254;
    temp->entity.curstate.scale = 1;
}
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size <= 0) return previousMP ? previousMP(name, size, data) : 1;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0] != 4) return previousMP ? previousMP(name, size, data) : 1;
    if (size < 4) return 1;
    if (bytes[3] != 9 && bytes[3] != 13) return previousMP ? previousMP(name, size, data) : 1;
    if (size != 4) return 1;
    const int owner = bytes[1] | (int(bytes[2]) << 8);
    if (owner > 0 && owner <= SHRT_MAX) Heal(owner, bytes[3] == 13);
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
    if (!data || size != 7) return 1;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0] != monster_effects::Impact && bytes[0] != monster_effects::TrapDirection) return 1;
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
        const int index = ev->EV_FindModelIndex(monster_effects::ImpactSprite);
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
    const auto old = api->HookUserMsg(name, hook);
    if (old != hook) previous = old;
    if (!old) engine.pfnHookUserMsg(name, hook);
}
}
void InitializeScenarioEffects()
{
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
    if (!healthHookPending) return;
    healthHookPending = false;
    Install("Health", Health, previousHealth);
}
void ResetScenarioEffects()
{
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
