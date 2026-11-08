// SPDX-License-Identifier: GPL-3.0-or-later
// CSO private TE133/137/138 wire format and client consumer, linked with the
// production client/cso_tent.cpp and a controlled efx/engine table.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/cso_tent.hpp"
#include "../../native/csol/cso_tent.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace csol::client { cl_enginefunc_t engine{}; }
namespace
{
int checks{}, failures{}, allocations{}, sprites{};
float now = 10.5f;
model_t boomer{}, sprite{};
TEMPENTITY temp{};
struct SpriteCall
{
    float pos[3], dir[3], scale, a, life;
    int model, rendermode, renderfx, flags;
} spriteCall{};
float allocOrigin[3]{};
model_t* allocModel{};

void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %d %s\n", checks, name); }
}
model_t* ModelByIndex(int index) { return index == 7 ? &boomer : index == 9 ? &sprite : nullptr; }
float ClientTime() { return now; }
TEMPENTITY* Alloc(float* origin, model_t* model)
{
    ++allocations;
    std::memcpy(allocOrigin, origin, sizeof(allocOrigin));
    allocModel = model;
    temp = {};
    temp.entity.curstate.animtime = -123.0f; // sentinel: TE133 must not write it
    temp.entity.curstate.frame = 55.0f;      // sentinel: TE133 must not reset it
    temp.fadeSpeed = .5f;
    return &temp;
}
TEMPENTITY* TempSprite(float* pos, float* dir, float scale, int model, int rendermode,
    int renderfx, float a, float life, int flags)
{
    ++sprites;
    std::memcpy(spriteCall.pos, pos, sizeof(spriteCall.pos));
    std::memcpy(spriteCall.dir, dir, sizeof(spriteCall.dir));
    spriteCall.scale = scale; spriteCall.model = model; spriteCall.rendermode = rendermode;
    spriteCall.renderfx = renderfx; spriteCall.a = a; spriteCall.life = life; spriteCall.flags = flags;
    temp = {};
    temp.entity.curstate.framerate = 10.0f; // stock R_TempSprite constant
    return &temp;
}
efx_api_t effects{};
event_api_t eventApi{};
std::vector<int> explosionOrder;
struct ExplosionCall { float origin[3], scale, rate; int model, flags; } explosionCall{};
struct SoundCall { float origin[3], volume, attenuation; int entity, channel, flags, pitch; const char* path; } soundCall{};
void Explode(float* origin, int model, float scale, float rate, int flags)
{
    explosionOrder.push_back(1);
    std::memcpy(explosionCall.origin, origin, sizeof(explosionCall.origin));
    explosionCall.model = model; explosionCall.scale = scale; explosionCall.rate = rate; explosionCall.flags = flags;
}
void Play(int entity, float* origin, int channel, const char* path, float volume, float attenuation, int flags, int pitch)
{
    explosionOrder.push_back(2);
    std::memcpy(soundCall.origin, origin, sizeof(soundCall.origin));
    soundCall.entity = entity; soundCall.channel = channel; soundCall.path = path;
    soundCall.volume = volume; soundCall.attenuation = attenuation; soundCall.flags = flags; soundCall.pitch = pitch;
}

void ExtendedExplosion()
{
    using namespace csol::cso_tent;
    using csol::client::ConsumeCsoTent;
    Explosion shot{{100.56f, -20.28f, 36.0f}, 0x1234, 10, 30, 0, 2};
    auto bytes = Write(shot);
    const std::uint8_t expected[]{9, 0x24, 3, 0x5e, 0xff, 0x20, 1, 0x34, 0x12, 10, 30, 0, 2};
    Check(bytes && !std::memcmp(bytes->data(), expected, sizeof(expected)), "MP ground TE3 payload with host subtype");
    Check(ConsumeCsoTent(int(bytes->size()), bytes->data()), "extended explosion consumed");
    Check(explosionOrder == std::vector<int>({1,2}), "visual then exactly one dedicated sound");
    Check(explosionCall.origin[0] == 100.5f && explosionCall.origin[1] == -20.25f && explosionCall.origin[2] == 36 &&
        explosionCall.model == 0x1234 && explosionCall.scale == 1 && explosionCall.rate == 30 &&
        explosionCall.flags == TE_EXPLFLAG_NOSOUND, "native explosion receives original visual parameters without default sound");
    Check(soundCall.entity == -1 && soundCall.channel == CHAN_AUTO && soundCall.volume == 1 &&
        soundCall.attenuation == .3f && soundCall.flags == 0 && soundCall.pitch == 100 &&
        !std::strcmp(soundCall.path, "weapons/mooncake_exp.wav") &&
        !std::memcmp(soundCall.origin, explosionCall.origin, sizeof(soundCall.origin)), "original special sound parameters");
    // The original byte flags are forwarded intact; NOSOUND only gates audio.
    for (unsigned flags = 0; flags <= 255; ++flags)
    {
        explosionOrder.clear(); shot.flags = static_cast<std::uint8_t>(flags); bytes = Write(shot);
        ConsumeCsoTent(int(bytes->size()), bytes->data());
        Check(explosionCall.flags == (flags | TE_EXPLFLAG_NOSOUND) && explosionOrder ==
            ((flags & TE_EXPLFLAG_NOSOUND) ? std::vector<int>{1} : std::vector<int>{1,2}), "all original flag combinations");
    }
    for (unsigned scale = 0; scale <= 255; ++scale)
    {
        explosionOrder.clear(); shot.flags = 0; shot.scale = static_cast<std::uint8_t>(scale); bytes = Write(shot);
        ConsumeCsoTent(int(bytes->size()), bytes->data());
        Check(explosionCall.scale == float(double(scale) * .1) && explosionOrder == std::vector<int>({1,2}),
            "original double scale conversion including sound-only zero");
    }
    explosionOrder.clear();
    for (unsigned length = 1; length < bytes->size(); ++length)
        Check(ConsumeCsoTent(length, bytes->data()) && explosionOrder.empty(), "truncated extended packet has no effects");
    auto bad = *bytes; bad.back() = 3;
    Check(ConsumeCsoTent(int(bad.size()), bad.data()) && explosionOrder.empty(), "unimplemented sound selector rejected");
    bad = *bytes; bad[7] = bad[8] = 0;
    Check(ConsumeCsoTent(int(bad.size()), bad.data()) && explosionOrder.empty(), "zero model rejected");
    bad[8] = 0x80;
    Check(ConsumeCsoTent(int(bad.size()), bad.data()) && explosionOrder.empty(), "negative model rejected");
    shot.sound = 3; Check(!Write(shot), "encoder refuses unsupported selector");
    shot.sound = 2; shot.origin[0] = NAN; Check(!Write(shot), "encoder refuses nonfinite position");
    shot.origin[0] = 4096; Check(!Write(shot), "encoder refuses overflowing coordinate");
    shot.origin[0] = 0; bytes = Write(shot);
    effects.R_Explosion = nullptr;
    ConsumeCsoTent(int(bytes->size()), bytes->data());
    Check(explosionOrder == std::vector<int>{2}, "missing visual interface preserves original sound branch");
    explosionOrder.clear(); effects.R_Explosion = Explode; eventApi.EV_PlaySound = nullptr;
    ConsumeCsoTent(int(bytes->size()), bytes->data());
    Check(explosionOrder == std::vector<int>{1}, "missing sound interface preserves visual branch");
    explosionOrder.clear(); csol::client::engine.pEfxAPI = nullptr; csol::client::engine.pEventAPI = nullptr;
    Check(ConsumeCsoTent(int(bytes->size()), bytes->data()) && explosionOrder.empty(), "missing APIs are safe");
    csol::client::engine.pEfxAPI = &effects; csol::client::engine.pEventAPI = &eventApi;
    eventApi.EV_PlaySound = Play;
}


void Wire()
{
    using namespace csol::cso_tent;
    // Boomer 1155CCF0 record (TE138).
    Model shell;
    shell.extended = true;
    shell.origin = {100.5f, -20.25f, 36.0f};
    shell.angles = {0, 100.0f, 0};
    shell.model = 0x1234;
    shell.life = 10; shell.framerate = 1; shell.fade = 1; shell.renderamt = 200;
    shell.rendermode = 4; shell.fadeSpeed = 17;
    std::size_t size{};
    const auto bytes = Write(shell, size);
    const std::uint8_t expected[]{ModelTentEx, 0x24, 0x03, 0x5E, 0xFF, 0x20, 0x01, 0, 71, 0,
        0x34, 0x12, 10, 1, 1, 200, 4, 0xFF, 0xFF, 17};
    Check(bytes && size == ModelTentExSize && !std::memcmp(bytes->data(), expected, size), "TE138 byte layout");
    const auto back = ReadModel(std::span(bytes->data(), size));
    Check(back && back->extended && back->origin[0] == 100.5f && back->origin[1] == -20.25f &&
        back->origin[2] == 36.0f && back->angles[1] == 71 * 360.0f / 256.0f && back->model == 0x1234 &&
        back->fadeSpeed == 17 && back->follow == -1, "TE138 round trip");
    Check(!ReadModel(std::span(bytes->data(), size - 1)), "TE138 short rejected");

    // Boomer 1155CA50 record (TE133): yaw only.
    Model pool;
    pool.origin = {1, 2, 3};
    pool.angles[1] = 170.0f;
    pool.model = 9; pool.life = 10; pool.framerate = 1; pool.fade = 1; pool.renderamt = 100; pool.rendermode = 5;
    const auto poolBytes = Write(pool, size);
    Check(poolBytes && size == ModelTentSize && (*poolBytes)[0] == ModelTent && (*poolBytes)[7] == 120, "TE133 layout");
    const auto poolBack = ReadModel(std::span(poolBytes->data(), size));
    Check(poolBack && !poolBack->extended && poolBack->angles[0] == 0 && poolBack->angles[2] == 0 &&
        poolBack->angles[1] == 168.75f && poolBack->fadeSpeed == 0, "TE133 round trip");
    pool.angles[0] = 1;
    Check(!Write(pool, size), "TE133 refuses pitch it cannot carry");

    // Original ReadAngle is signed: byte 142 decodes as -114 steps.
    Model negative;
    negative.extended = true;
    negative.angles = {200.0f, 0, 0};
    const auto negativeBytes = Write(negative, size);
    const auto negativeBack = ReadModel(std::span(negativeBytes->data(), size));
    Check(negativeBack && (*negativeBytes)[7] == 142 && negativeBack->angles[0] == -114 * 360.0f / 256.0f,
        "signed angle byte");

    Sprite puff;
    puff.origin = {-0.125f, 4095.875f, -4096.0f};
    puff.model = 9; puff.scale = 10; puff.brightness = 200; puff.framerate = 20;
    const auto spriteBytes = Write(puff);
    Check(spriteBytes && (*spriteBytes)[0] == SpriteTent && (*spriteBytes)[1] == 0xFF && (*spriteBytes)[2] == 0xFF &&
        (*spriteBytes)[7] == 9 && (*spriteBytes)[9] == 10 && (*spriteBytes)[10] == 200 && (*spriteBytes)[11] == 20,
        "TE137 layout");
    const auto spriteBack = ReadSprite(*spriteBytes);
    Check(spriteBack && spriteBack->origin[0] == -0.125f && spriteBack->origin[1] == 4095.875f &&
        spriteBack->origin[2] == -4096.0f, "TE137 coordinates");
    puff.origin[0] = NAN;
    Check(!Write(puff), "non-finite coordinate rejected");
    puff.origin[0] = 5000.0f;
    Check(!Write(puff), "out-of-range coordinate rejected");
}

void Consumer()
{
    using namespace csol::cso_tent;
    using csol::client::ConsumeCsoTent;
    Model shell;
    shell.extended = true;
    shell.origin = {8, 16, 24};
    shell.angles = {0, 90.0f, 0};
    shell.model = 7; shell.life = 10; shell.framerate = 1; shell.fade = 1; shell.renderamt = 200;
    shell.rendermode = kRenderTransAlpha; shell.fadeSpeed = 17;
    std::size_t size{};
    auto bytes = Write(shell, size);
    Check(ConsumeCsoTent(int(size), bytes->data()) && allocations == 1, "TE138 allocates");
    Check(allocModel == &boomer && allocOrigin[0] == 8 && allocOrigin[2] == 24, "TE138 model and origin");
    Check(temp.entity.angles[1] == 90.0f && temp.entity.angles[0] == 0 && temp.entity.curstate.rendermode == kRenderTransAlpha,
        "TE138 angles and render mode");
    Check(temp.flags == (FTENT_SPRANIMATE | FTENT_FADEOUT) && temp.frameMax == 256.0f &&
        temp.entity.curstate.framerate == 1.0f * .1f && temp.entity.curstate.renderamt == 200 &&
        temp.entity.baseline.renderamt == 200 && temp.fadeSpeed == 17.0f * .1f, "TE138 animation and fade");
    Check(temp.entity.curstate.frame == 0 && temp.entity.curstate.animtime == now &&
        temp.die == float(1.0 + double(now)), "TE138 frame, animtime and life");

    Model pool;
    pool.origin = {1, 2, 3};
    pool.angles[1] = 45.0f;
    pool.model = 9; pool.life = 10; pool.framerate = 1; pool.fade = 1; pool.renderamt = 100;
    pool.rendermode = kRenderTransAdd; pool.follow = 3;
    bytes = Write(pool, size);
    Check(ConsumeCsoTent(int(size), bytes->data()) && allocations == 2, "TE133 allocates");
    Check(temp.entity.curstate.framerate == 1.0f && temp.fadeSpeed == 1.0f && temp.entity.curstate.animtime == -123.0f &&
        temp.entity.curstate.frame == 55.0f, "TE133 raw rate, fixed fade and untouched animtime/frame");
    Check(temp.flags == (FTENT_SPRANIMATE | FTENT_FADEOUT | FTENT_PLYRATTACHMENT) && temp.clientIndex == 3 &&
        temp.tentOffset[0] == 1 && temp.tentOffset[2] == 3, "TE133 follow");

    shell.follow = 4;
    bytes = Write(shell, size);
    ConsumeCsoTent(int(size), bytes->data());
    Check((temp.flags & (FTENT_PLYRATTACHMENT | FTENT_PERSIST)) == (FTENT_PLYRATTACHMENT | FTENT_PERSIST),
        "TE138 follow adds 0xA000");

    shell.model = 8; // not loaded
    bytes = Write(shell, size);
    const int before = allocations;
    Check(ConsumeCsoTent(int(size), bytes->data()) && allocations == before, "missing model draws nothing");
    Check(ConsumeCsoTent(int(size) - 1, bytes->data()) && allocations == before, "malformed record consumed");
    const std::uint8_t other[]{3, 0, 0, 0, 0, 0, 0};
    Check(!ConsumeCsoTent(sizeof(other), other), "older CsoMonFX subtypes left to their consumer");

    Sprite puff;
    puff.origin = {5, 6, 7};
    puff.model = 9; puff.scale = 20; puff.brightness = 200; puff.framerate = 8;
    const auto spriteBytes = Write(puff);
    Check(ConsumeCsoTent(int(spriteBytes->size()), spriteBytes->data()) && sprites == 1, "TE137 sprite");
    Check(spriteCall.pos[0] == 5 && spriteCall.pos[2] == 7 && spriteCall.dir[0] == 0 && spriteCall.dir[2] == 0 &&
        spriteCall.scale == float(20 * .1) && spriteCall.model == 9 && spriteCall.rendermode == kRenderTransAdd &&
        spriteCall.renderfx == kRenderFxNone && spriteCall.life == 0 && spriteCall.flags == FTENT_SPRANIMATE,
        "TE137 R_TempSprite arguments");
    Check(spriteCall.a == float(double(200.0f) * (1.0 / 255.0)) && temp.entity.curstate.framerate == 8.0f,
        "TE137 brightness and CSO rate");
}
}

int main()
{
    effects.CL_TempEntAlloc = Alloc;
    effects.R_TempSprite = TempSprite;
    csol::client::engine.pEfxAPI = &effects;
    csol::client::engine.hudGetModelByIndex = ModelByIndex;
    csol::client::engine.GetClientTime = ClientTime;
    effects.R_Explosion = Explode;
    eventApi.EV_PlaySound = Play;
    csol::client::engine.pEventAPI = &eventApi;
    Wire();
    Consumer();
    ExtendedExplosion();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
