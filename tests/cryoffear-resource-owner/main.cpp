// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/resource_refresh.hpp"
#include "../../native/csol/client/weapons/guest_cof_resources.hpp"
#include "../../native/csol/client/weapons/guest_cof_effects.hpp"
#include "../../native/guest/cof/resource_catalog.hpp"
#include "../../native/guest/cof/presentation_protocol.hpp"
#include "../../native/guest/cof/particle_protocol.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include "../../native/include/goldcraft/sound_precache_api.h"
#include "../../native/include/goldcraft/precache_ready_api.h"
#include <event_api.h>
#include <triangleapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace csol::client { metahook_api_t* api{}; cl_enginefunc_t engine{}; engine_studio_api_t studio{}; }
namespace
{
using namespace csol::client;
using namespace guest::cof;
using namespace guest::cof::resources;
using guest::cof::client::AuroraResult;
unsigned checks{}, failures{}, reads{}, loads{}, queries{}, sprites{}, volumeCalls{}, randomCalls{}, remoteCalls{};
std::filesystem::path original;
std::array<unsigned, 14> registrations{};
std::array<pfnUserMsgHook, 4> messages{};
GoldCraftPrecacheReadyCallback ready{}, retired{};
void* listener{};
std::uint64_t generation{1}, nextToken{}, token{};
bool rejectSubscription{}, failRead{}, badWave{}, badScript{}, failLoad{}, failSprite{};
enum class Edge { None, Query, Read, Load, Sprite, Model };
Edge cancelAt{}, replaceAt{};
bool nestedReady{};
unsigned edgeSkip{};
GuestCofResourceBudget limits{4, 8u * 1024u * 1024u}; // Fixture policy, not an original constant.
GoldCraftConnectionAPI connection;
GoldCraftSoundPrecacheAPI soundApi;
GoldCraftPrecacheReadyAPI lifecycle;
model_t sprite{}, glockModel{}, p345Model{}, shellModel{}, flashModel{}, cheapSmokeModel{}, ordinaryModel{};
bool missingEffectModel{};
unsigned effectModelQueries{}, effectShells{};
void (*viewPunch)(event_args_s*){};
unsigned char nativePunchPatch[]{0x83,0xbe,0xc4,0,0,0,0};
cl_entity_t viewModel{}, local{};
cvar_t cheap{}, smokeLimit{};
triangleapi_t triangles{};

void Check(bool value, int line)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %d\n", line); }
}
#define CHECK(x) Check((x), __LINE__)
void Initialize() { InitializeGuestCofResources(limits, &connection, &soundApi, &lifecycle); }
void Complete() { CHECK(ready); if (ready) ready(listener, generation); }
void Disconnect()
{
    const auto old = generation++;
    if (retired) retired(listener, old);
}
void EdgeAction(Edge edge)
{
    if (cancelAt != edge && replaceAt != edge) return;
    if (edgeSkip) { --edgeSkip; return; }
    const auto replace = replaceAt == edge;
    cancelAt = replaceAt = Edge::None;
    Disconnect();
    if (replace)
    {
        Initialize();
        if (nestedReady) Complete();
    }
}
std::uint64_t Generation() { return generation; }
std::uint64_t Subscribe(GoldCraftPrecacheReadyCallback a, GoldCraftPrecacheReadyCallback b, void* context)
{
    CHECK(a && b);
    if (rejectSubscription) return 0;
    ready = a; retired = b; listener = context;
    return token = ++nextToken;
}
std::uint32_t Unsubscribe(std::uint64_t value)
{
    if (!value || value != token) return 0;
    token = 0; ready = retired = nullptr; listener = nullptr;
    return 1;
}
std::uint32_t Inspect(std::uint64_t expected, const char* name, std::uint32_t size) noexcept
{
    ++queries;
    EdgeAction(Edge::Query);
    if (expected != generation) return 0;
    unsigned index{};
    for (const auto group : {std::span<const LocalSoundResource>(P345AudioResources),
                             std::span<const LocalSoundResource>(GlockAudioResources)})
        for (const auto& asset : group)
        {
            if (asset.remoteAsset == std::string_view(name, size)) return registrations[index];
            ++index;
        }
    CHECK(false);
    return 0;
}
unsigned Message(const char* name)
{
    if (!std::strcmp(name, SoundMessage)) return 0;
    if (!std::strcmp(name, ParticleMessage)) return 1;
    if (!std::strcmp(name, ViewEntityMessage)) return 2;
    CHECK(!std::strcmp(name, "ResetHUD"));
    return 3;
}
pfnUserMsgHook Hook(const char* name, pfnUserMsgHook callback)
{
    auto& entry = messages[Message(name)];
    const auto previous = entry;
    if (previous) entry = callback;
    return previous;
}
int Register(const char* name, pfnUserMsgHook callback)
{
    auto& entry = messages[Message(name)];
    CHECK(!entry);
    entry = callback;
    return 1;
}
int Load(const char* name)
{
    ++loads;
    CHECK(std::string_view(name) == "sprites/guest/cof/fog2.spr");
    EdgeAction(Edge::Load);
    return failLoad ? 0 : 7;
}
const model_t* Sprite(int handle)
{
    ++sprites;
    EdgeAction(Edge::Sprite);
    return handle == 7 && !failSprite ? &sprite : nullptr;
}
float Volume(const char* name) { CHECK(!std::strcmp(name, "volume")); ++volumeCalls; return 0; }
float Random(float low, float high) { ++randomCalls; return (low + high) * .5f; }
void Remote(int, float*, int, const char*, float, float, int, int) { ++remoteCalls; }
float Time() { return 0; }
cl_entity_t* View() { return &viewModel; }
cl_entity_t* Local() { return &local; }
cl_entity_t* Entity(int) { return &viewModel; }
void Angles(float* out) { std::fill_n(out, 3, 0); }
void Vectors(const float*, float* f, float* r, float* u)
{
    for (unsigned i = 0; i < 3; ++i) { f[i] = i == 0; r[i] = i == 1; u[i] = i == 2; }
}
cvar_t* Cvar(const char* name)
{
    if (!std::strcmp(name, "cl_cheapweaponsmoke")) return &cheap;
    CHECK(!std::strcmp(name, "cl_smokeparticlelimit"));
    return &smokeLimit;
}
void Sound(std::uint8_t mode, std::uint8_t remote = 0)
{
    SoundPacket packet{};
    CHECK(EncodeSound({"weapons/p345/p345_fire.wav", mode, 0, remote, {}, 0}, packet));
    CHECK(messages[0] && messages[0](SoundMessage, packet.size, packet.bytes.data()) == 1);
}
void* Client() { return reinterpret_cast<void*>(0x10000000); }
mh_gamesymbol_status_t Resolve(void* base,const char* name,mh_gamesymbol_kind_t kind,void** out)
{
    CHECK(base==Client() && !std::strcmp(name,"GoldCraftCof_AfterNativePunch") && kind==MH_GAMESYMBOL_KIND_PATCH);
    *out=nativePunchPatch;return MH_GAMESYMBOL_OK;
}
int MaxClients() { return 32; }
void Event(const char* path,void (*callback)(event_args_s*))
{
    CHECK(!std::strcmp(path,GlockEvents[1].host));viewPunch=callback;
}
int ModelIndex(const char* path)
{
    ++effectModelQueries;EdgeAction(Edge::Model);
    if(missingEffectModel)return 0;
    if(!std::strcmp(path,GlockModels[0].host))return 17;
    if(!std::strcmp(path,P345Models[0].host))return 18;
    if(!std::strcmp(path,GlockModels[unsigned(GlockModel::Flash)].host))return 20;
    if(!std::strcmp(path,GlockModels[unsigned(GlockModel::CheapSmoke)].host))return 21;
    CHECK(!std::strcmp(path,GlockModels[unsigned(GlockModel::Shell)].host));return 19;
}
model_t* Model(int index){return index==17?&glockModel:index==18?&p345Model:index==19?&shellModel:index==20?&flashModel:index==21?&cheapSmokeModel:nullptr;}
TEMPENTITY* Shell(float*,float*,float*,float,int index,int sound)
{
    CHECK(index==19 && sound==1);++effectShells;static TEMPENTITY result;return &result;
}
int Command(const char*) { return 0; }
void Begin()
{
    Disconnect();
    failRead = badWave = badScript = failLoad = failSprite = missingEffectModel = false;
    cancelAt = replaceAt = Edge::None; edgeSkip = 0; nestedReady = false;
    registrations.fill(3);
    limits = {4, 8u * 1024u * 1024u};
#if CSOL_RESOURCE_REFRESH
    resource_refresh_detail::readsAllowed = true;
#endif
    Initialize();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Armed);
}
void Prepared(unsigned samples, bool effects = true)
{
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Prepared);
    CHECK(GuestCofResourcesState().generation == generation);
    CHECK(GuestCofResourcesState().samples == samples);
    CHECK(GuestCofResourcesState().attempts == 1);
    CHECK(GuestCofAudioReady() && GuestCofParticlesReady());
    CHECK(GuestCofResourcesState().studioAndRecoilPrepared == effects);
    CHECK(GuestCofEffectsReady() == effects);
}
}
namespace csol::client
{
// This suite keeps its existing audio/Aurora/Studio scope. The real GL decal
// provider and its cold owner closure are tested in cryoffear-custom-decal.
bool PrepareGuestCofDecalResources(const std::array<std::span<const std::uint8_t>,5>& images) noexcept
{
    for (const auto image : images) CHECK(image.size() == 12306 && image[2] == 2 && image[16] == 24);
    return true;
}
void InvalidateGuestCofDecalResources() noexcept {}
// This fixture observes actual adapter installation; it never fakes effects readiness.
hook_t* Hook(void* address,void*,void** original)
{
    CHECK(address==nativePunchPatch);*original=address;return reinterpret_cast<hook_t*>(1);
}
void RemoveHook(hook_t* hook){CHECK(hook==reinterpret_cast<hook_t*>(1));}
std::vector<std::byte> ReadResource(const char* name)
{
    ++reads;
    EdgeAction(Edge::Read);
    if (failRead) throw std::runtime_error("fixture read failure");
    std::filesystem::path file;
    if (std::string_view(name) == P345Particle.host) file = original / P345Particle.original;
    for (const auto& resource : ShotDecals)
        if (std::string_view(name) == resource.host) file = original / resource.original;
    for (const auto group : {std::span<const LocalSoundResource>(P345AudioResources),
                             std::span<const LocalSoundResource>(GlockAudioResources)})
        for (const auto& asset : group)
            if (asset.localFile == name) file = original / "sound" / asset.localKey;
    CHECK(!file.empty());
    std::ifstream input(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    CHECK(!bytes.empty());
    if (badScript && std::string_view(name) == P345Particle.host) return {std::byte{'?'}};
    if (badWave && std::string_view(name) != P345Particle.host) return {std::byte{0}};
    return {reinterpret_cast<const std::byte*>(bytes.data()),
            reinterpret_cast<const std::byte*>(bytes.data() + bytes.size())};
}
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    original = argv[1];
    connection = {sizeof(connection), 1, Generation};
    soundApi = {sizeof(soundApi), 1, Inspect};
    lifecycle = {sizeof(lifecycle), 1, Subscribe, Unsubscribe};
    metahook_api_t host{}; host.HookUserMsg = Hook; host.GetClientBase=Client;host.ResolveGameSymbol=Resolve;api = &host;
    event_api_t event{}; event.EV_PlaySound = Remote;event.EV_FindModelIndex=ModelIndex; engine.pEventAPI = &event;
    efx_api_t efx{};efx.R_TempModel=Shell;engine.pEfxAPI=&efx;
    engine.GetMaxClients=MaxClients;engine.pfnHookEvent=Event;engine.pfnServerCmd=Command;studio.GetModelByIndex=Model;
    glockModel.type=p345Model.type=shellModel.type=flashModel.type=ordinaryModel.type=mod_studio;
    cheapSmokeModel.type=mod_sprite;
    std::strcpy(glockModel.name,GlockModels[0].host);std::strcpy(p345Model.name,P345Models[0].host);
    std::strcpy(shellModel.name,GlockModels[unsigned(GlockModel::Shell)].host);
    std::strcpy(flashModel.name,GlockModels[unsigned(GlockModel::Flash)].host);
    std::strcpy(cheapSmokeModel.name,GlockModels[unsigned(GlockModel::CheapSmoke)].host);
    std::strcpy(ordinaryModel.name,"models/v_usp.mdl");viewModel.model=&glockModel;
    engine.pfnHookUserMsg = Register; engine.pfnSPR_Load = Load;
    engine.pfnGetCvarFloat = Volume; engine.pfnRandomFloat = Random;
    engine.GetSpritePointer = Sprite; engine.GetClientTime = Time;
    engine.GetViewModel = View; engine.GetLocalPlayer = Local; engine.GetEntityByIndex = Entity;
    engine.GetViewAngles = Angles; engine.pfnAngleVectors = Vectors; engine.pfnGetCvarPointer = Cvar;
    engine.pTriAPI = &triangles; sprite.numframes = 1; smokeLimit.value = 5;
    InitializeGuestCofAudio(&connection); InitializeGuestCofParticles(&connection);
    CHECK(InitializeGuestCofEffects(&connection) && viewPunch);

    InitializeGuestCofResources(limits, nullptr, &soundApi, &lifecycle);
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Unavailable && !token);
    rejectSubscription = true; Initialize();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Unavailable && !token);
    rejectSubscription = false;
    Begin(); registrations.fill(0); Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::NoAssets);
    CHECK(!reads && !loads && !GuestCofAudioReady() && !GuestCofParticlesReady());
    registrations.fill(3); Complete();
    CHECK(!reads && !loads); // No frame/repeated-ready retry after an incomplete plan.
    Begin(); registrations[0] = 0; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::NativeSoundUnavailable && !reads);
    Begin(); limits.voicesPerSample = 0; Initialize(); Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::InvalidBudget && !reads);
    Begin(); limits.voicesPerSample = 2048; Initialize(); Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::InvalidBudget && !reads);
    Begin(); limits.pcmBytes = 1; Initialize(); Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::ResourceFailed && reads == 1);
    Begin(); unsigned before = reads; failRead = true; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::ResourceFailed && reads == before + 1);
    Begin(); badWave = true; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::AudioFailed && !GuestCofAudioReady());
    Begin(); badScript = true; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::ParticleFailed && !GuestCofAudioReady());
    Begin(); failLoad = true; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::ResourceFailed);
    Begin(); failSprite = true; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::ParticleFailed);
    for (unsigned group = 0; group < 2; ++group)
    {
        Begin(); registrations.fill(0); std::fill_n(registrations.begin() + group * 7, 7, 3);
        before = reads; Complete(); Prepared(7); CHECK(reads == before + 13);
        CHECK(GuestCofResourcesState().weapons==(group?1u:2u));
    }
    Begin(); registrations.fill(GoldCraftSoundSignonRegistered); before = reads; Complete();
    Prepared(14); CHECK(reads == before + 20); // Local PCM is prepared even when native PCM is lazy.
    Begin(); before = reads; Complete(); Prepared(14); CHECK(reads == before + 20);
    auto beforeLoads = loads, beforeQueries = queries;
    Complete(); Prepared(14); CHECK(reads == before + 20 && loads == beforeLoads && queries == beforeQueries);
    SetGuestCofEffectsWeapon(guest::cof::client::Weapon::Glock);
    mstudioevent_t studioEvent{};studioEvent.event=5006;
    const auto modelQueriesBefore=effectModelQueries;
    CHECK(GuestCofStudioEvent(&studioEvent,&viewModel));CHECK(effectShells==1 && effectModelQueries==modelQueriesBefore);
    viewModel.model=&ordinaryModel;CHECK(!GuestCofStudioEvent(&studioEvent,&viewModel));viewModel.model=&glockModel;
    Sound(1); CHECK(volumeCalls == 1 && randomCalls == 1);
    Sound(0, 1); CHECK(remoteCalls == 1);
    GuestCofParticlesVideoInit(); CHECK(!GuestCofParticlesReady());
    GuestCofResourcesVideoInit(); Prepared(14);
    CHECK(reads == before + 20 && loads == beforeLoads + 1);
    GuestCofAudioFrame(); CHECK(volumeCalls == 2); // VideoInit retained the real loop/device.
    studioEvent.event=5004;std::strcpy(studioEvent.options,"weapons/glock/glock_magin.wav");
    const auto priorVolume=volumeCalls,priorRandom=randomCalls,priorModels=effectModelQueries;
    CHECK(GuestCofStudioEvent(&studioEvent,&viewModel));
    CHECK(GuestCofLastStudioEffect()==guest::cof::client::EffectResult::Dispatched);
    CHECK(volumeCalls==priorVolume+1 && randomCalls==priorRandom+1 && effectModelQueries==priorModels);
    CHECK(reads==before+20 && loads==beforeLoads+1); // Real prepared PCM, zero hot resource work.
    auto oldRetired = retired; auto oldListener = listener; auto oldGeneration = generation;
    Disconnect(); CHECK(!GuestCofAudioReady() && !GuestCofParticlesReady());
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Cancelled);
    before = volumeCalls; GuestCofAudioFrame(); CHECK(volumeCalls == before);
    Complete(); Prepared(14);
    oldRetired(oldListener, oldGeneration); Prepared(14); // A stale old callback cannot retire the new plan.
    bool rejected{}, wrongState{};
    const auto foreignReady = ready; const auto foreignListener = listener;
    std::thread worker([&] {
        try { Initialize(); } catch (const std::runtime_error&) { rejected = true; }
        wrongState = GuestCofResourcesState().status == GuestCofResourceStatus::Uninitialized;
        foreignReady(foreignListener, generation); ShutdownGuestCofResources();
    });
    worker.join(); CHECK(rejected && wrongState); Prepared(14);
    for (const auto edge : {Edge::Query, Edge::Read, Edge::Load, Edge::Sprite, Edge::Model})
    {
        Begin(); cancelAt = edge; Complete();
        CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Cancelled);
        CHECK(!GuestCofAudioReady() && !GuestCofParticlesReady());
        Begin(); replaceAt = edge; Complete();
        CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Armed);
        CHECK(!GuestCofAudioReady() && !GuestCofParticlesReady());
        Complete(); Prepared(14);
    }
    Begin(); replaceAt = Edge::Read; nestedReady = true; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Cancelled);
    CHECK(!GuestCofAudioReady() && !GuestCofParticlesReady());
    Begin(); cancelAt = Edge::Query; edgeSkip = 14; Complete(); // The real audio admission queries again.
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Cancelled && !GuestCofAudioReady());
#if CSOL_RESOURCE_REFRESH
    Begin(); resource_refresh_detail::readsAllowed = false; before = reads; Complete();
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::ResourceFailed && reads == before);
    resource_refresh_detail::readsAllowed = true;
#endif
    Begin();missingEffectModel=true;Complete();Prepared(14,false);
    CHECK(GuestCofAudioReady() && GuestCofParticlesReady());
    ShutdownGuestCofEffects();Begin();Complete();Prepared(14,false);
    CHECK(InitializeGuestCofEffects(&connection));Begin();Complete();Prepared(14);
    Disconnect();CHECK(!GuestCofEffectsReady() && !GuestCofResourcesState().studioAndRecoilPrepared);
    ShutdownGuestCofResources(); ShutdownGuestCofEffects(); ShutdownGuestCofAudio(); ShutdownGuestCofParticles();
    CHECK(!token && !ready && !retired && !GuestCofAudioReady() && !GuestCofParticlesReady());
    CHECK(GuestCofResourcesState().status == GuestCofResourceStatus::Uninitialized);
    std::printf("checks=%u failures=%u realWaves=14 realXAudio2=yes actualSdk=yes live=no performance=no\n", checks, failures);
    return failures ? 1 : 0;
}
