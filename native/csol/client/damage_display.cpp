// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "damage_display.hpp"
#include <algorithm>
#include <cmath>

namespace csol::client
{
namespace
{
damage::Numbers numbers;
std::vector<damage::Skin> skins;
cvar_t* selected{};
cvar_t* enabled{};
pfnUserMsgHook previous{};
int activeSkin{-1};

int ReceiveDamage(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size <= 0) return previous ? previous(name, size, data) : 1;
    damage::Message message;
    const auto result = damage::Decode({static_cast<const std::byte*>(data), std::size_t(size)}, message);
    if (result == damage::ParseResult::Unrelated) return previous ? previous(name, size, data) : 1;
    if (result == damage::ParseResult::Malformed || !DamageEnabled()) return 1;
    const int id = DamageSelectedSkin();
    const auto* skin = DamageSkin(id);
    if (id != 0 && (!skin || !skin->Find(damage::Style(message.style)))) return 1;
    // Port to the matched public GoldSrc entity ABI; no original-client offsets.
    const auto* entity = message.target > 0 ? engine.GetEntityByIndex(message.target) : nullptr;
    if (!entity || !entity->model) return 1;
    damage::Anchor anchor{entity->origin[0], entity->origin[1], entity->origin[2]};
    if (entity->model->type == mod_brush)
    {
        anchor.x += (entity->curstate.mins[0] + entity->curstate.maxs[0]) * .5f;
        anchor.y += (entity->curstate.mins[1] + entity->curstate.maxs[1]) * .5f;
        anchor.z += (entity->curstate.mins[2] + entity->curstate.maxs[2]) * .5f;
    }
    anchor.z += id == 0 ? 40 : 20;
    if (activeSkin != id) { numbers.Reset(); activeSkin = id; }
    const auto jitter = [](int range) {
        return range > 0 ? int(float(engine.pfnRandomLong(0, range - 1)) - range * .5f) : 0;
    };
    try
    {
        // The original text widget uses its full one-second lifetime and does
        // not apply image-number supersession skips.
        numbers.Push(message, anchor, skin ? skin->animation : damage::Animation{1,0,0}, engine.GetClientTime(),
            skin ? jitter(skin->randomWidth) : 0, skin ? jitter(skin->randomHeight) : 0);
    }
    catch (const std::exception& e) { engine.Con_DPrintf("CSOLSupport damage: %s\n", e.what()); }
    return 1;
}
}
std::span<const damage::Skin> DamageSkins() { return skins; }
const damage::Skin* DamageSkin(int id)
{
    const auto it = std::find_if(skins.begin(), skins.end(), [=](const auto& s) { return s.id == id; });
    return it == skins.end() ? nullptr : &*it;
}
int DamageSelectedSkin()
{
    const float value = selected ? selected->value : 1;
    return std::isfinite(value) && value >= 0 && value <= 256 ? int(value) : 1;
}
bool DamageEnabled() { return !enabled || enabled->value != 0; }
void ApplyDamageSettings(bool show, int id)
{
    if (id != 0 && !DamageSkin(id)) return;
    engine.Cvar_SetValue("cl_dmg_skin", float(id));
    engine.Cvar_SetValue("csol_damage_numbers", show ? 1.0f : 0.0f);
    ResetDamageDisplay();
}
void InitializeDamageDisplay()
{
    selected = engine.pfnGetCvarPointer("cl_dmg_skin");
    if (!selected) selected = engine.pfnRegisterVariable("cl_dmg_skin", "1", FCVAR_ARCHIVE);
    enabled = engine.pfnGetCvarPointer("csol_damage_numbers");
    if (!enabled) enabled = engine.pfnRegisterVariable("csol_damage_numbers", "1", FCVAR_ARCHIVE);
    try { skins = damage::ReadSkins(ReadResource(damage::StyleResource)); }
    catch (const std::exception& e) { engine.Con_Printf("CSOLSupport damage: %s\n", e.what()); }
    const auto old = api->HookUserMsg(damage::MessageName, ReceiveDamage);
    if (old != ReceiveDamage) previous = old;
    if (!old) engine.pfnHookUserMsg(damage::MessageName, ReceiveDamage);
}
void ResetDamageDisplay() { numbers.Reset(); activeSkin = -1; }
void DamageDisplayFrame()
{
    if (!DamageEnabled() || (activeSkin >= 0 && activeSkin != DamageSelectedSkin())) ResetDamageDisplay();
    numbers.Advance(engine.GetClientTime());
    if (DamageEnabled()) PrepareDamageGraphics(DamageSelectedSkin());
}
std::span<const damage::Number> DamageEntries() { return numbers.Entries(); }
void DamageDisplayVideoInit() { ResetDamageDisplay(); ResetDamageGraphics(); }
void ShutdownDamageDisplay() { ResetDamageDisplay(); ShutdownDamageOptions(); skins.clear(); }
}
