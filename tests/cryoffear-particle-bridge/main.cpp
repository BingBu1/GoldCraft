// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_particles.hpp"
#include "../../native/guest/cof/particle_protocol.hpp"
#include "../../native/guest/cof/catalog.hpp"
#include "../../native/include/goldcraft/connection_api.h"

#include <triangleapi.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

namespace csol::client { metahook_api_t* api{}; cl_enginefunc_t engine{}; }
namespace
{
using namespace csol::client;
using namespace guest::cof;
using guest::cof::client::AuroraResult;
unsigned checks{}, failures{}, loads{}, randoms{}, quads{}, angleCalls{}, spriteQueries{}, nativeResets{};
bool hasView{true}, hasLocal{true}, cancelSprite{}, cancelConnection{};
std::uint64_t generation{1};
float now{};
int lastMode{-1};
cl_entity_t viewModel{}, owner{}, local{};
model_t model{}, sprite{};
cvar_t cheap{}, limit{};
triangleapi_t triangles{};
std::array<pfnUserMsgHook, 3> handlers{};

void Check(bool value, int line)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %d\n", line); }
}
#define CHECK(x) Check((x),__LINE__)
unsigned Message(const char* name)
{
    if (!std::strcmp(name, ParticleMessage)) return 0;
    if (!std::strcmp(name, ViewEntityMessage)) return 1;
    CHECK(!std::strcmp(name, "ResetHUD"));
    return 2;
}
pfnUserMsgHook Hook(const char* name, pfnUserMsgHook next)
{
    auto& entry = handlers[Message(name)];
    const auto previous = entry;
    if (previous) entry = next;
    return previous;
}
int Register(const char* name, pfnUserMsgHook next)
{
    auto& entry = handlers[Message(name)];
    CHECK(!entry);
    entry = next;
    return 1;
}
int NativeReset(const char*, int, void*) { ++nativeResets; return 17; }
int Other(const char*, int, void*) { return 5; }
std::uint64_t Generation() { return generation; }
float Time() { return now; }
float Random(float low, float high) { ++randoms; return (low + high) * .5f; }
int Load(const char*) { ++loads; return 0; } // The message/draw adapter must never load a file.
const model_t* Sprite(int handle)
{
    ++spriteQueries;
    if (cancelSprite) ShutdownGuestCofParticles();
    if (cancelConnection) ++generation;
    return handle == 7 ? &sprite : nullptr;
}
cl_entity_t* View() { return hasView ? &viewModel : nullptr; }
cl_entity_t* Local() { return hasLocal ? &local : nullptr; }
cl_entity_t* Entity(int index) { return index == 0 || index == 5 ? &owner : nullptr; }
void Angles(float* out) { ++angleCalls; out[0] = 10; out[1] = 20; out[2] = 30; }
void Vectors(const float* angles, float* forward, float* right, float* up)
{
    CHECK(angles[0] == 10 && angles[1] == 20 && angles[2] == 30);
    for (unsigned i = 0; i < 3; ++i) { forward[i] = i == 0; right[i] = i == 1; up[i] = i == 2; }
}
cvar_t* Cvar(const char* name)
{
    if (!std::strcmp(name, "cl_cheapweaponsmoke")) return &cheap;
    CHECK(!std::strcmp(name, "cl_smokeparticlelimit"));
    return &limit;
}
void Mode(int value) { lastMode = value; }
void Begin(int) { ++quads; }
void End() {}
void Color(float,float,float,float) {}
void UV(float,float) {}
void Vertex(float*) {}
int Texture(model_t*, int) { return 1; }
void Deliver(const ParticleMessageData& value)
{
    ParticlePacket bytes{};
    CHECK(EncodeParticle(value, bytes));
    CHECK(handlers[0](ParticleMessage, int(bytes.size()), bytes.data()) == 1);
}
void ViewEntity(int value)
{
    auto bytes = EncodeViewEntity(std::int16_t(value));
    CHECK(handlers[1](ViewEntityMessage, int(bytes.size()), bytes.data()) == 1);
}
void Draw(float time)
{
    now = time;
    DrawGuestCofParticles(true);
}
}

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    const std::string script((std::istreambuf_iterator<char>(file)), {});
    CHECK(!script.empty());
    metahook_api_t host{};
    host.HookUserMsg = Hook;
    api = &host;
    handlers[2] = NativeReset;
    engine.pfnHookUserMsg = Register;
    engine.GetClientTime = Time;
    engine.GetLocalPlayer = Local;
    engine.GetViewModel = View;
    engine.GetEntityByIndex = Entity;
    engine.pfnSPR_Load = Load;
    engine.GetSpritePointer = Sprite;
    engine.pfnRandomFloat = Random;
    engine.GetViewAngles = Angles;
    engine.pfnAngleVectors = Vectors;
    engine.pfnGetCvarPointer = Cvar;
    triangles.RenderMode = Mode; triangles.Begin = Begin; triangles.End = End;
    triangles.Color4f = Color; triangles.TexCoord2f = UV;
    triangles.Vertex3fv = Vertex; triangles.SpriteTexture = Texture;
    engine.pTriAPI = &triangles;
    viewModel.index = 5;
    viewModel.model = &model;
    std::strcpy(model.name, Pistols[1].viewModel.data());
    sprite.numframes = 1;
    local.index = 1;
    limit.value = 5;
    GoldCraftConnectionAPI provider{sizeof(GoldCraftConnectionAPI), 1, Generation};
    const GuestCofParticlePlan plan{script, 7};

    CHECK(!GuestCofParticlesReady());
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::NotPrepared);
    InitializeGuestCofParticles(nullptr);
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::NotPrepared);
    InitializeGuestCofParticles(&provider);
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::Applied && loads == 0 && spriteQueries == 1);
    ParticlePacket malformed{};
    CHECK(handlers[0](ParticleMessage, int(malformed.size()), malformed.data()) == 1);
    CHECK(GuestCofParticleStatus() == AuroraResult::InvalidInput);
    cheap.value = 1;
    Deliver({ParticleOperation::Add,7,false,100});
    Draw(0);
    CHECK(randoms == 0 && quads == 0);
    cheap.value = 0;
    hasView = false;
    Deliver({ParticleOperation::Add,7,false,100});
    CHECK(GuestCofParticleStatus() == AuroraResult::MissingBackend);
    hasView = true;
    Deliver({ParticleOperation::Add,7,false,100});
    Draw(.04f); Draw(.08f);
    CHECK(randoms > 0 && quads > 0 && lastMode == 0);
    const auto beforeAngles = angleCalls;
    ViewEntity(2);
    Draw(.12f);
    CHECK(GuestCofParticleStatus() == AuroraResult::Unsupported && angleCalls == beforeAngles);
    CHECK(handlers[2]("ResetHUD", 0, nullptr) == 17 && nativeResets == 1);
    Draw(.16f);
    CHECK(GuestCofParticleStatus() == AuroraResult::Applied && quads > 0);
    ref_params_t params{};
    params.nextView = 1;
    GuestCofParticlesViewStage(params);
    Draw(.20f);
    CHECK(angleCalls == beforeAngles + 1);
    params.nextView = 0;
    GuestCofParticlesViewStage(params);
    const auto beforeDelete = quads;
    Deliver({ParticleOperation::DeleteOwner,0,false,100}); // Packet owner is replaced by actual VM index.
    Draw(.24f);
    CHECK(quads > beforeDelete);
    Deliver({ParticleOperation::DeleteOwner,0,false,5});
    const auto deleted = quads;
    Draw(.28f);
    CHECK(quads == deleted);
    for (unsigned i = 0; i < guest::cof::client::Aurora::MaxSystems; ++i)
        Deliver({ParticleOperation::Add,7,false,100});
    Deliver({ParticleOperation::Add,7,false,100});
    CHECK(GuestCofParticleStatus() == AuroraResult::CapacityExceeded);
    GuestCofParticlesVideoInit();
    CHECK(!GuestCofParticlesReady());
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::Applied);
    viewModel.index = 0;
    Deliver({ParticleOperation::Add,7,false,100});
    Draw(.32f); Draw(.36f);
    CHECK(GuestCofParticleStatus() == AuroraResult::Applied);
    Deliver({ParticleOperation::DeleteOwner,0,false,0});
    ++generation;
    Draw(.40f);
    CHECK(!GuestCofParticlesReady());
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::Applied);
    bool wrongReady = true;
    std::thread worker([&] { wrongReady = GuestCofParticlesReady(); ShutdownGuestCofParticles(); });
    worker.join();
    CHECK(!wrongReady && GuestCofParticlesReady());
    ViewEntity(2);
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::Applied);
    Draw(.44f);
    CHECK(GuestCofParticleStatus() == AuroraResult::Unsupported); // Cold reprepare is not ResetHUD.
    cancelSprite = true;
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::NotPrepared && !GuestCofParticlesReady());
    cancelSprite = false;
    InitializeGuestCofParticles(&provider);
    cancelConnection = true;
    CHECK(PrepareGuestCofParticles(plan) == AuroraResult::NotPrepared && !GuestCofParticlesReady());
    cancelConnection = false;
    InitializeGuestCofParticles(&provider);
    auto bad = plan; bad.fogSprite = 8;
    CHECK(PrepareGuestCofParticles(bad) == AuroraResult::ResourceFailed && !GuestCofParticlesReady());
    CHECK(loads == 0);
    handlers[0] = Other;
    bool collision{};
    try { InitializeGuestCofParticles(&provider); } catch (const std::runtime_error&) { collision = true; }
    CHECK(collision && handlers[0] == Other);
    ShutdownGuestCofParticles();
    CHECK(!GuestCofParticlesReady());
    std::printf("checks=%u failures=%u actualSdk=yes gameplay=no\n", checks, failures);
    return failures ? 1 : 0;
}
