// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_audio.hpp"
#include "../../native/guest/cof/presentation_protocol.hpp"
#include "../../native/include/goldcraft/connection_api.h"

#include <event_api.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>

namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
}

namespace
{
using namespace csol::client;
using namespace guest::cof;
using guest::cof::client::AudioResult;
unsigned checks{}, failures{}, remoteCalls{}, randomCalls{}, volumeCalls{}, precacheCalls{};
std::uint64_t generation = 1;
bool precached = true;
bool cancelPreparation{}, changeConnection{};
bool cancelInVolume{}, cancelInRandom{}, replaceInVolume{};
const GuestCofAudioPlan* callbackPlan{};
const GoldCraftConnectionAPI* callbackProvider{};
pfnUserMsgHook installed{};
std::array<char, 64> remotePath{};
std::array<float, 3> remoteOrigin{};
int remoteEntity{}, remoteChannel{}, remoteFlags{}, remotePitch{};
float remoteVolume{}, remoteAttenuation{};

void Check(bool condition, int line)
{
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %d\n", line); }
}
#define CHECK(x) Check((x), __LINE__)

pfnUserMsgHook Hook(const char* name, pfnUserMsgHook replacement)
{
    CHECK(!std::strcmp(name, SoundMessage));
    const auto old = installed;
    if (old) installed = replacement;
    return old;
}
int Register(const char* name, pfnUserMsgHook replacement)
{
    CHECK(!std::strcmp(name, SoundMessage));
    CHECK(!installed);
    installed = replacement;
    return 1;
}
std::uint64_t Generation() { return generation; }
float Volume(const char* name)
{
    CHECK(!std::strcmp(name, "volume"));
    ++volumeCalls;
    if (cancelInVolume || replaceInVolume)
    {
        cancelInVolume = false;
        ShutdownGuestCofAudio();
        if (replaceInVolume)
        {
            replaceInVolume = false;
            InitializeGuestCofAudio(callbackProvider);
            CHECK(PrepareGuestCofAudio(*callbackPlan) == AudioResult::Applied);
        }
    }
    return 0; // Real XAudio2 without audible output.
}
float Random(float low, float high)
{
    CHECK(low == -.1f && high == .1f);
    ++randomCalls;
    if (cancelInRandom)
    {
        cancelInRandom = false;
        ShutdownGuestCofAudio();
    }
    return 0;
}
bool Precached(void*, std::string_view asset) noexcept
{
    ++precacheCalls;
    CHECK(asset == "guest/cof/p345/fire.wav");
    if (cancelPreparation) ShutdownGuestCofAudio();
    if (changeConnection) ++generation;
    return precached;
}
void Remote(int entity, float* origin, int channel, const char* path, float volume,
            float attenuation, int flags, int pitch)
{
    ++remoteCalls;
    std::strcpy(remotePath.data(), path);
    std::copy_n(origin, 3, remoteOrigin.begin());
    remoteEntity = entity; remoteChannel = channel; remoteFlags = flags; remotePitch = pitch;
    remoteVolume = volume; remoteAttenuation = attenuation;
}
int Other(const char*, int, void*) { return 1; }

// Two seconds of generated PCM keeps source buffers alive while exercising the
// real service. No original WAV or mock audio implementation is used.
std::array<std::byte, 44 + 88200> Wave()
{
    std::array<std::byte, 44 + 88200> out{};
    auto tag = [&](unsigned at, const char* text) { std::memcpy(out.data() + at, text, 4); };
    auto u16 = [&](unsigned at, unsigned n) {
        out[at] = std::byte(n & 255); out[at + 1] = std::byte(n >> 8);
    };
    auto u32 = [&](unsigned at, unsigned n) { u16(at, n & 65535); u16(at + 2, n >> 16); };
    tag(0, "RIFF"); u32(4, unsigned(out.size()) - 8); tag(8, "WAVE");
    tag(12, "fmt "); u32(16, 16); u16(20, 1); u16(22, 1);
    u32(24, 22050); u32(28, 44100); u16(32, 2); u16(34, 16);
    tag(36, "data"); u32(40, 88200);
    return out;
}

void Deliver(std::uint8_t mode, std::uint8_t remote,
             std::string_view sample = "weapons/p345/fire.wav")
{
    SoundPacket packet{};
    CHECK(EncodeSound({sample, mode, 2, remote, {-32768, -9, 32767}, -19}, packet));
    CHECK(installed && installed(SoundMessage, packet.size, packet.bytes.data()) == 1);
}
}

int main()
{
    using namespace csol::client;
    metahook_api_t host{};
    host.HookUserMsg = Hook;
    api = &host;
    event_api_t event{};
    event.EV_PlaySound = Remote;
    engine.pEventAPI = &event;
    engine.pfnHookUserMsg = Register;
    engine.pfnGetCvarFloat = Volume;
    engine.pfnRandomFloat = Random;
    GoldCraftConnectionAPI provider{sizeof(GoldCraftConnectionAPI), 1, Generation};
    const auto wave = Wave();
    const GuestCofSoundAsset asset{"weapons_new/p345/fire.wav", "weapons/p345/fire.wav",
        "guest/cof/p345/fire.wav", wave, 4};
    GuestCofAudioPlan plan{{1, 4, 1024 * 1024}, {&asset, 1}, nullptr, Precached};

    CHECK(!GuestCofAudioReady());
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Inactive);
    InitializeGuestCofAudio(nullptr);
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Inactive);
    InitializeGuestCofAudio(&provider);
    CHECK(!GuestCofAudioReady());
    Deliver(0, 0);
    CHECK(randomCalls == 0 && remoteCalls == 0);
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Applied);
    CHECK(GuestCofAudioReady() && precacheCalls == 1);
    CHECK(installed(SoundMessage, 0, nullptr) == 1);
    std::array<std::byte, 192> malformed{};
    CHECK(installed(SoundMessage, int(malformed.size()), malformed.data()) == 1);
    CHECK(randomCalls == 0 && remoteCalls == 0);

    Deliver(0, 3, "weapons_new/p345/fire.wav");
    CHECK(remoteCalls == 1 && randomCalls == 0);
    CHECK(!std::strcmp(remotePath.data(), "guest/cof/p345/fire.wav"));
    CHECK(remoteEntity == 0 && remoteChannel == 1 && remoteFlags == 0 && remotePitch == 100);
    CHECK(remoteVolume == 1 && remoteAttenuation == .8f);
    CHECK(remoteOrigin[0] == -4096 && remoteOrigin[1] == -1.125f && remoteOrigin[2] == 4095.875f);
    Deliver(0, 1, "weapons/unknown/fire.wav");
    CHECK(remoteCalls == 1);
    Deliver(0, 0);
    CHECK(randomCalls == 1 && volumeCalls == 1);
    Deliver(1, 0);
    CHECK(randomCalls == 2 && volumeCalls == 2);
    GuestCofAudioFrame();
    CHECK(volumeCalls == 3);
    GuestCofAudioPause(true);
    GuestCofAudioPause(true); // Original changes-only transition.
    Deliver(0, 0); // New one-shot unpauses independently of stored pause state.
    CHECK(randomCalls == 3);
    GuestCofAudioPause(false);
    Deliver(2, 0, "ignored/still-consumed");
    CHECK(randomCalls == 3);

    AudioResult wrongThread{};
    bool wrongReady = true;
    std::thread worker([&] {
        wrongThread = PrepareGuestCofAudio(plan);
        wrongReady = GuestCofAudioReady();
        ShutdownGuestCofAudio();
    });
    worker.join();
    CHECK(wrongThread == AudioResult::WrongThread && !wrongReady);
    CHECK(GuestCofAudioReady());
    ++generation;
    Deliver(0, 0);
    CHECK(!GuestCofAudioReady() && randomCalls == 3);
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Applied);
    auto bad = asset;
    bad.remoteAsset = "guest/cof/../escape.wav";
    auto invalid = plan;
    invalid.assets = {&bad, 1};
    CHECK(PrepareGuestCofAudio(invalid) == AudioResult::InvalidSample);
    CHECK(!GuestCofAudioReady());
    bad = asset; bad.remoteAsset = "weapons/p345/fire.wav";
    CHECK(PrepareGuestCofAudio(invalid) == AudioResult::InvalidSample);
    auto duplicate = std::array{asset, asset};
    invalid = plan; invalid.limits.samples = 2; invalid.assets = duplicate;
    CHECK(PrepareGuestCofAudio(invalid) == AudioResult::DuplicateSample);
    precached = false;
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::NotPrepared && !GuestCofAudioReady());
    precached = true;
    changeConnection = true;
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::NotPrepared && !GuestCofAudioReady());
    changeConnection = false;
    cancelPreparation = true;
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::NotPrepared && !GuestCofAudioReady());
    cancelPreparation = false;
    InitializeGuestCofAudio(&provider);
    generation = 0;
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Inactive);
    generation = 8;
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Applied);
    callbackPlan = &plan;
    callbackProvider = &provider;
    auto beforeRandom = randomCalls;
    cancelInVolume = true;
    Deliver(0, 0);
    CHECK(!GuestCofAudioReady() && randomCalls == beforeRandom);
    InitializeGuestCofAudio(&provider);
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Applied);
    cancelInRandom = true;
    Deliver(0, 0);
    CHECK(!GuestCofAudioReady());
    InitializeGuestCofAudio(&provider);
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Applied);
    Deliver(1, 0);
    cancelInVolume = true;
    GuestCofAudioFrame();
    CHECK(!GuestCofAudioReady());
    InitializeGuestCofAudio(&provider);
    CHECK(PrepareGuestCofAudio(plan) == AudioResult::Applied);
    Deliver(1, 0);
    replaceInVolume = true;
    GuestCofAudioFrame();
    CHECK(GuestCofAudioReady()); // An old Update must not retire the replacement.
    beforeRandom = randomCalls;
    replaceInVolume = true;
    Deliver(0, 0);
    CHECK(GuestCofAudioReady() && randomCalls == beforeRandom);
    InitializeGuestCofAudio(&provider); // Duplicate registration is safe; cold plan retires.
    CHECK(!GuestCofAudioReady());
    installed = Other;
    bool collision{};
    try { InitializeGuestCofAudio(&provider); } catch (const std::runtime_error&) { collision = true; }
    CHECK(collision && installed == Other);
    ShutdownGuestCofAudio();
    ShutdownGuestCofAudio();
    CHECK(!GuestCofAudioReady());
    std::printf("checks=%u failures=%u realXAudio2=yes audible=no\n", checks, failures);
    return failures ? 1 : 0;
}
