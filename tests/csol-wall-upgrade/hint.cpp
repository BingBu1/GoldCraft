// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/wall_upgrade_hint.hpp"
#include "../../native/csol/wall_upgrade_hint.hpp"
#include <r_efx.h>
#include <cstdio>
#include <cstring>
#include <limits>

namespace csol::client { cl_enginefunc_t engine{}; }
namespace
{
int checks{}, failures{}, allocations{};
float now = 10;
bool resetOnLoad{}, resetOnLookup{}, resetOnAllocate{};
bool deathOnLoad{}, deathOnAllocate{};
bool removeOnLoad{}, removeOnAllocate{}, attachOnLookup{};
const std::uint8_t* activePacket{};
model_t sprite{}, boss{}, replacement{};
cl_entity_t owner{};
std::array<TEMPENTITY, 80> temps{};
efx_api_t effects{};
void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %d %s\n", checks, name); }
}
cl_entity_t* Owner(int index)
{
    if (resetOnLookup) { resetOnLookup = false; csol::client::ResetWallUpgradeHints(); }
    if (attachOnLookup)
    {
        attachOnLookup = false;
        csol::client::ConsumeWallUpgradeHint(20, activePacket);
    }
    return index == owner.index ? &owner : nullptr;
}
model_t* Load(const char* path, int* index)
{
    Check(std::strcmp(path, csol::wall_upgrade::Sprite) == 0, "original sprite resource");
    if (resetOnLoad) { resetOnLoad = false; csol::client::ResetWallUpgradeHints(); }
    if (deathOnLoad)
    {
        deathOnLoad = false;
        const std::array<std::uint8_t, 3> death{200,9,0};
        csol::client::ConsumeWallUpgradeHint(3, death.data());
    }
    if (removeOnLoad)
    {
        removeOnLoad = false;
        const std::array<std::uint8_t, 3> remove{96,9,0};
        csol::client::ConsumeWallUpgradeHint(3, remove.data());
    }
    *index = 3;
    return &sprite;
}
TEMPENTITY* Allocate(float* origin, model_t* model)
{
    if (resetOnAllocate) { resetOnAllocate = false; csol::client::ResetWallUpgradeHints(); }
    if (deathOnAllocate)
    {
        deathOnAllocate = false;
        const std::array<std::uint8_t, 3> death{200,9,0};
        csol::client::ConsumeWallUpgradeHint(3, death.data());
    }
    if (removeOnAllocate)
    {
        removeOnAllocate = false;
        const std::array<std::uint8_t, 3> remove{96,9,0};
        csol::client::ConsumeWallUpgradeHint(3, remove.data());
    }
    if (allocations == static_cast<int>(temps.size())) return nullptr;
    auto& temp = temps[allocations++];
    temp = {};
    temp.entity.model = model;
    for (unsigned i = 0; i < 3; ++i) temp.entity.origin[i] = origin[i];
    return &temp;
}
void Setup()
{
    csol::client::ResetWallUpgradeHints();
    temps = {};
    allocations = 0;
    now = 10;
    resetOnLoad = resetOnLookup = resetOnAllocate = false;
    deathOnLoad = deathOnAllocate = false;
    removeOnLoad = removeOnAllocate = attachOnLookup = false;
    sprite = {};
    sprite.type = mod_sprite;
    sprite.numframes = 1;
    owner = {};
    owner.index = 9;
    owner.model = &boss;
    owner.baseline.modelindex = 10;
    owner.curstate.messagenum = 100;
    owner.origin[0] = 30;
    owner.origin[1] = 40;
    owner.origin[2] = 50;
    effects.CL_TempEntAllocHigh = Allocate;
    csol::client::engine.GetEntityByIndex = Owner;
    csol::client::engine.CL_LoadModel = Load;
    csol::client::engine.GetClientTime = [] { return now; };
    csol::client::engine.pEfxAPI = &effects;
}
}
int main()
{
    using namespace csol;
    // Actual MP wall packet: owner9, resource51, z20, duration10, scale.2.
    constexpr std::array<std::uint8_t, 20> golden{
        90,0,9,0,51, 0,0,0,0,160,0, 0,0,32,65, 1, 205,204,76,62};
    activePacket = golden.data();
    auto decoded = wall_upgrade_hint::Read(golden);
    Check(decoded && decoded->owner == 9 && decoded->duration == 10 && decoded->scale == .2f &&
        decoded->offset == std::array<float,3>{0,0,20}, "original20-byte decoding");
    for (std::size_t size = 0; size < golden.size(); ++size)
        Check(!wall_upgrade_hint::Read(std::span(golden).first(size)), "truncation rejected");
    for (std::size_t field : {std::size_t(11), std::size_t(16)})
    {
        auto invalid = golden;
        const auto bits = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity());
        for (unsigned i=0;i<4;++i) invalid[field+i] = bits >> (8*i);
        Check(!wall_upgrade_hint::Read(invalid), "infinite duration or scale rejected");
    }
    Setup();
    Check(client::ConsumeWallUpgradeHint(20, golden.data()), "recognized hint consumed");
    Check(allocations == 1, "actual allocation");
    auto& effect = temps[0];
    Check(effect.clientIndex == 9 && effect.entity.origin[2] == 70, "general entity attachment at world offset");
    Check(effect.die == 20 && effect.fadeSpeed == 10, "original hold and fade speed");
    Check(effect.entity.curstate.renderamt == 254 && effect.entity.baseline.renderamt == 254 &&
        effect.entity.curstate.rendermode == kRenderTransAdd && effect.entity.curstate.renderfx == 14 &&
        effect.entity.curstate.scale == .2f, "original visual setup");
    Check(effect.flags == (FTENT_FADEOUT | FTENT_SPRANIMATE | FTENT_PERSIST |
        FTENT_SPRANIMATELOOP | FTENT_CLIENTCUSTOM), "mapped stock flags");
    owner.origin[0] = 60;
    effect.callback(&effect, 0, now);
    Check(effect.entity.origin[0] == 60, "follow current owner");
    attachOnLookup = true;
    owner.origin[0] = 70;
    effect.callback(&effect, 0, now);
    Check(allocations == 2 && effect.entity.origin[0] == 70 && temps[1].entity.origin[0] == 70,
        "reentrant attachment grows records without invalidating follow owner");
    const std::array<std::uint8_t, 3> death{200,9,0};
    Check(!client::ConsumeWallUpgradeHint(3, death.data()), "death remains downstream");
    Check(effect.die < 0 && (effect.flags & FTENT_NOMODEL), "death retires before pool reuse");

    Setup();
    client::ConsumeWallUpgradeHint(20, golden.data());
    const std::array<std::uint8_t, 3> remove{96,9,0};
    Check(!client::ConsumeWallUpgradeHint(3, remove.data()), "removal remains downstream");
    Check(temps[0].die == now && !(temps[0].flags & FTENT_NOMODEL), "message96 starts original fade instead of immediate kill");
    owner.origin[1] = 90;
    temps[0].callback(&temps[0], 0, now);
    Check(temps[0].entity.origin[1] == 90, "expiring indicator keeps following during fade");
    Setup();
    client::ConsumeWallUpgradeHint(20, golden.data());
    temps[0].entity.curstate.iuser4 += 1;
    temps[0].die = 99;
    client::ResetWallUpgradeHints();
    Check(temps[0].die == 99, "reused temp not retired by old owner");
    Setup();
    client::ConsumeWallUpgradeHint(20, golden.data());
    owner.model = &replacement;
    temps[0].callback(&temps[0], 0, now);
    Check(temps[0].die < 0, "changed owner model rejected");
    Setup();
    client::ConsumeWallUpgradeHint(20, golden.data());
    owner.curstate.messagenum = 1;
    temps[0].callback(&temps[0], 0, now);
    Check(temps[0].die < 0, "state rollback rejected");
    Setup();
    resetOnLoad = true;
    client::ConsumeWallUpgradeHint(20, golden.data());
    Check(allocations == 0, "reset in model load cancels old attach");
    Setup();
    resetOnAllocate = true;
    client::ConsumeWallUpgradeHint(20, golden.data());
    Check(allocations == 1 && !temps[0].callback, "retired engine pool not touched after allocation callback");
    Setup();
    deathOnLoad = true;
    client::ConsumeWallUpgradeHint(20, golden.data());
    Check(allocations == 0, "death during load cancels pending attachment");
    Setup();
    deathOnAllocate = true;
    client::ConsumeWallUpgradeHint(20, golden.data());
    Check(allocations == 1 && temps[0].die < 0, "death during allocation retires returned attachment");
    Setup();
    removeOnLoad = true;
    client::ConsumeWallUpgradeHint(20, golden.data());
    Check(allocations == 0, "removal96 during load cancels pending attachment");
    Setup();
    removeOnAllocate = true;
    client::ConsumeWallUpgradeHint(20, golden.data());
    Check(allocations == 1 && temps[0].die < 0, "removal96 during allocation cancels returned attachment");
    Setup();
    client::ConsumeWallUpgradeHint(20, golden.data());
    resetOnLookup = true;
    temps[0].callback(&temps[0], 0, now);
    Check(temps[0].die < 0, "reset during follow safely retires");
    Setup();
    auto other = golden;
    other[4] = 22;
    Check(!client::ConsumeWallUpgradeHint(20, other.data()) && allocations == 0, "other resources remain downstream");
    other = golden;
    other[1] = 2;
    Check(client::ConsumeWallUpgradeHint(20, other.data()) && allocations == 0, "unsupported secondary flags not interpreted as stock layout");
    Setup();
    for (unsigned i=0;i<70;++i) client::ConsumeWallUpgradeHint(20,golden.data());
    Check(allocations==70,"native engine capacity is not truncated to64 attachments");
    client::ConsumeWallUpgradeHint(3,remove.data());
    for (int i=0;i<allocations;++i) Check(temps[i].die==now&&!(temps[i].flags&FTENT_NOMODEL),"owner removal fades every attachment");
    Setup();
    deathOnLoad=true;
    client::ConsumeWallUpgradeHint(20,golden.data());
    Check(allocations==0,"owner retirement during load cancels creation");
    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
