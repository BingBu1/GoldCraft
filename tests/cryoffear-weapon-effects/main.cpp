// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/weapon_effects.hpp"
#include <metahook.h>
#include <com_model.h>
#include <event_api.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <fcntl.h>
#include <io.h>

using namespace guest::cof::client;
static_assert(sizeof(void*) == 4);
static_assert(offsetof(cl_entity_t, attachment) == 0xB60);
static_assert(offsetof(cl_entity_t, model) == 0xB94);
static_assert(offsetof(mstudioevent_t, event) == 4);
static_assert(offsetof(mstudioevent_t, options) == 0xC);
static_assert(offsetof(event_api_t, EV_FindModelIndex) == 0xC);
static_assert(offsetof(efx_api_t, R_TempModel) == 0xC0);

namespace
{
unsigned allocations{}, localCount{}, spatialCount{}, modelCount{}, shellCount{}, commandCount{};
bool countAllocations{}, backendSuccess{true};
char sample[100]{}, command[40]{};
std::array<float, 3> position{}, speed{}, rotation{};
float life{}, volume{};
int shellModel{}, shellSound{};

void Check(bool value, int line)
{
    if (!value) { std::fprintf(stderr, "FAIL line%d\n", line); std::abort(); }
}
#define CHECK(value) Check((value), __LINE__)
bool Local(void*, const LocalSound& sound) noexcept
{
    ++localCount;
    CHECK(sound.sample.size() < sizeof(sample));
    std::memcpy(sample, sound.sample.data(), sound.sample.size());
    sample[sound.sample.size()] = 0;
    CHECK((sound.mode == 0 && !sound.enabled && sound.origin == std::array<float,3>{}));
    return backendSuccess;
}
void Spatial(const char* path, float gain, float* origin)
{
    ++spatialCount;
    std::strcpy(sample, path);
    volume = gain;
    std::memcpy(position.data(), origin, sizeof(position));
}
int Model(const char* path)
{
    ++modelCount;
    CHECK(std::strcmp(path, "models/shell.mdl") == 0);
    return 37;
}
TEMPENTITY* Shell(float* origin, float* direction, float* angles, float seconds, int model, int sound)
{
    ++shellCount;
    std::memcpy(position.data(), origin, sizeof(position));
    std::memcpy(speed.data(), direction, sizeof(speed));
    std::memcpy(rotation.data(), angles, sizeof(rotation));
    life = seconds; shellModel = model; shellSound = sound;
    static TEMPENTITY result{};
    return backendSuccess ? &result : nullptr;
}
int Command(const char* text)
{
    ++commandCount;
    std::strcpy(command, text);
    return 0; // Original ignores SDK return; Dispatched never means server accepted.
}

// Execute original scalar SSE operation order on this machine as an independent
// check of emulator NaN propagation. IDA1004CE8C..1004CEF2:tip-origin,then*10.
void OriginalSse(const unsigned* input, unsigned* output)
{
    const float multiplier = 10.0F;
    const float* factor = &multiplier;
    __asm
    {
        mov eax, input
        mov ecx, output
        mov edx, factor
        movss xmm1, [edx]
        movss xmm0, [eax+12]
        subss xmm0, [eax]
        mulss xmm0, xmm1
        movss [ecx], xmm0
        movss xmm0, [eax+16]
        subss xmm0, [eax+4]
        mulss xmm0, xmm1
        movss [ecx+4], xmm0
        movss xmm0, [eax+20]
        subss xmm0, [eax+8]
        mulss xmm0, xmm1
        movss [ecx+8], xmm0
    }
}
}

void* operator new(std::size_t size)
{
    if (countAllocations) ++allocations;
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }

int main(int argc, char** argv)
{
    model_t model{};
    std::strcpy(model.name, "models/v_p345.mdl");
    cl_entity_t entity{};
    entity.model = &model;
    const float attachments[3][3]{{91,92,93},{1,-2,3},{4,-6,8}};
    std::memcpy(entity.attachment, attachments, sizeof(attachments));
    event_api_t events{}; events.EV_FindModelIndex = Model;
    efx_api_t effects{}; effects.R_TempModel = Shell;
    cl_enginefunc_t engine{};
    engine.pEventAPI = &events; engine.pEfxAPI = &effects;
    engine.pfnPlaySoundByNameAtLocation = Spatial; engine.pfnServerCmd = Command;
    WeaponEffects service({&engine, nullptr, Local});
    mstudioevent_t event{};
    event.event = 5006;
    if (argc > 1)
    {
        CHECK(_setmode(_fileno(stdin), _O_BINARY) != -1);
        CHECK(_setmode(_fileno(stdout), _O_BINARY) != -1);
        std::array<unsigned, 6> input;
        while (std::fread(input.data(), sizeof(input), 1, stdin) == 1)
        {
            for (std::size_t i = 0; i < 3; ++i)
            {
                entity.attachment[1][i] = std::bit_cast<float>(input[i]);
                entity.attachment[2][i] = std::bit_cast<float>(input[i+3]);
            }
            CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
            std::array<unsigned, 3> words{std::bit_cast<unsigned>(speed[0]),
                std::bit_cast<unsigned>(speed[1]), std::bit_cast<unsigned>(speed[2])};
            if (argc > 2 && std::strcmp(argv[2], "--original-sse") == 0)
                OriginalSse(input.data(), words.data());
            CHECK(std::fwrite(words.data(), sizeof(words), 1, stdout) == 1);
        }
        return 0;
    }
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
    CHECK((position == std::array<float,3>{1,-2,3}));
    CHECK((speed == std::array<float,3>{30,-40,50}));
    CHECK((rotation == std::array<float,3>{}));
    CHECK(life == 2.5F && shellModel == 37 && shellSound == 1 && modelCount == 1 && shellCount == 1);
    backendSuccess = false;
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::BackendFailed);
    backendSuccess = true;
    engine.pEfxAPI = nullptr;
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::MissingBackend);
    engine.pEfxAPI = &effects;
    event.event = 5004;
    std::strcpy(event.options, "weapons/p345_shoot.wav");
    for (const auto weapon : {Weapon::Glock,Weapon::P345,Weapon::Vp70,Weapon::Revolver})
    {
        CHECK(service.StudioEvent(event, entity, weapon) == EffectResult::Dispatched);
        CHECK(std::strcmp(sample,"weapons_new/p345_shoot.wav") == 0);
    }
    std::strcpy(event.options,"prefix\\weapons/p345.wav");
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
    CHECK(std::strcmp(sample,"weapons_new/weapons/p345.wav") == 0);
    backendSuccess = false;
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::BackendFailed);
    CHECK(spatialCount == 0);
    backendSuccess = true;
    WeaponEffects missing({&engine});
    CHECK(missing.StudioEvent(event, entity, Weapon::P345) == EffectResult::MissingBackend);
    std::strcpy(event.options, "ambient/test.wav");
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
    CHECK(std::strcmp(sample, "ambient/test.wav") == 0 && volume == 1.0F);
    CHECK((position == std::array<float,3>{91,92,93}));
    std::strcpy(model.name,"prefix/models/cs/v_p345.mdl");
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
    CHECK(std::strcmp(sample, "cs/ambient/test.wav") == 0);
    std::strcpy(event.options, "weapons/test.wav");
    CHECK(service.StudioEvent(event, entity, Weapon::None) == EffectResult::Dispatched);
    CHECK(std::strcmp(sample, "cs/weapons/test.wav") == 0);
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
    CHECK(std::strcmp(sample, "weapons_new/test.wav") == 0);
    event.event = 5080;
    struct CommandCase { const char* options; const char* expected; };
    for (const auto& item : {CommandCase{"0","zoominmdl 0"}, {"2","zoominmdl 2"},
                            {"-7","zoominmdl -7"}, {" \t+21tail","zoominmdl 21"},
                            {"nonnumeric","zoominmdl 0"}, {"2147483647","zoominmdl 2147483647"},
                            {"-2147483648","zoominmdl -2147483648"}})
    {
        std::strcpy(event.options, item.options);
        CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
        CHECK(std::strcmp(command,item.expected) == 0);
    }
    CHECK(commandCount == 7);
    engine.pfnServerCmd = nullptr;
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::MissingBackend);
    engine.pfnServerCmd = Command;
    event.event = 5099;
    entity.model = nullptr;
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Unhandled);
    event.event = 5004;
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::InvalidEntity);
    entity.model = &model;
    std::memset(event.options,'a',sizeof(event.options));
    CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::InvalidOptions);
    std::strcpy(event.options,"weapons/test.wav");
    countAllocations = true;
    for (int i = 0; i < 10000; ++i)
        CHECK(service.StudioEvent(event, entity, Weapon::P345) == EffectResult::Dispatched);
    countAllocations = false;
    CHECK(allocations == 0);
    std::puts("PASS realSDK x86 layouts;5004 four pistols/local/spatial/no fallback;5006 attachment1/2*10,life2.5;5080 command;missing backends;hot allocations0");
}
