// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/audio.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

using namespace guest::cof::client;
namespace
{
std::atomic<unsigned> allocations{};
void Check(bool pass, int line)
{
    if (!pass) { std::fprintf(stderr, "audio failure at line %d\n", line); std::exit(1); }
}
#define CHECK(...) Check((__VA_ARGS__), __LINE__)
struct Providers
{
    float volume{}, random{};
    int volumeCalls{}, randomCalls{};
    static float Volume(void* context) noexcept
    { auto& p = *static_cast<Providers*>(context); ++p.volumeCalls; return p.volume; }
    static float Random(void* context, float low, float high) noexcept
    {
        auto& p = *static_cast<Providers*>(context); ++p.randomCalls;
        CHECK(low == -0.1f && high == 0.1f); return p.random;
    }
    AudioProviders Bind() noexcept { return {this, Volume, Random}; }
};
void Put16(std::vector<std::byte>& wave, std::size_t at, unsigned value)
{ wave[at] = std::byte(value & 255u); wave[at + 1] = std::byte((value >> 8) & 255u); }
void Put32(std::vector<std::byte>& wave, std::size_t at, unsigned value)
{ Put16(wave, at, value & 65535u); Put16(wave, at + 2, value >> 16); }
std::vector<std::byte> Wave(unsigned bits = 24, unsigned channels = 2, unsigned rate = 96000)
{
    const auto bytes = rate * channels * (bits / 8);
    std::vector<std::byte> wave(44 + bytes);
    std::memcpy(wave.data(), "RIFF", 4); Put32(wave, 4, 36 + bytes);
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8); Put32(wave, 16, 16);
    Put16(wave, 20, 1); Put16(wave, 22, channels); Put32(wave, 24, rate);
    Put32(wave, 28, bytes); Put16(wave, 32, channels * (bits / 8)); Put16(wave, 34, bits);
    std::memcpy(wave.data() + 36, "data", 4); Put32(wave, 40, bytes);
    if (bits == 8) std::memset(wave.data() + 44, 128, bytes);
    return wave;
}
void Lifecycle()
{
    Providers providers;
    LocalAudio audio;
    LocalSound request{"weapons_new/test.wav", 0, false, {999, -999, 444}};
    CHECK(audio.Play(request) == AudioResult::Inactive);
    CHECK(audio.Initialize({0, 1, 100}, providers.Bind()) == AudioResult::InvalidLimits);
    CHECK(audio.Initialize({4, 6, 4 * 1024 * 1024}, providers.Bind()) == AudioResult::Applied);
    auto wave = Wave();
    auto bad = wave; bad[0] = std::byte{};
    CHECK(audio.Prepare(request.sample, bad, 1) == AudioResult::InvalidWave);
    bad = wave; Put32(bad, 16, 0xFFFFFFFFu);
    CHECK(audio.Prepare(request.sample, bad, 1) == AudioResult::InvalidWave);
    bad = wave; Put16(bad, 20, 3);
    CHECK(audio.Prepare(request.sample, bad, 1) == AudioResult::UnsupportedFormat);
    bad = wave; Put16(bad, 32, 1);
    CHECK(audio.Prepare(request.sample, bad, 1) == AudioResult::InvalidWave);
    CHECK(audio.Prepare({}, wave, 1) == AudioResult::InvalidSample);
    CHECK(audio.Prepare(std::string_view("a\0b", 3), wave, 1) == AudioResult::InvalidSample);
    CHECK(audio.Prepare(request.sample, wave, 0) == AudioResult::Capacity);
    CHECK(audio.Prepare(request.sample, wave, 3) == AudioResult::Applied);
    CHECK(audio.Prepare(request.sample, wave, 1) == AudioResult::DuplicateSample);
    CHECK(audio.Prepare("weapons_new/other.wav", Wave(16, 1, 22050), 3) == AudioResult::Applied);
    CHECK(audio.Prepare("weapons_new/overflow.wav", wave, 1) == AudioResult::Capacity);
    CHECK(audio.Activate() == AudioResult::Applied);
    CHECK(audio.Prepare("cold-only.wav", wave, 1) == AudioResult::Inactive);
    const auto baseline = allocations.load();
    // No local weapon/player state is ever established. The service is global
    // to its recipient process, including a listener in ordinary CS form.
    CHECK(audio.Play(request) == AudioResult::Applied);
    CHECK(audio.Play(request) == AudioResult::Applied);
    AudioState state;
    CHECK(audio.Inspect(state) == AudioResult::Applied && state.activeVoices == 2);
    CHECK(!state.hasLoopSlot && !state.loopingVoices);
    AudioVoiceState voice;
    CHECK(audio.InspectVoice(0, voice) == AudioResult::Applied && voice.buffersQueued == 1);
    CHECK(voice.volume == 0 && voice.speed == 1 && voice.active && !voice.looped && !voice.paused);
    CHECK(audio.SetPaused(true) == AudioResult::Applied);
    CHECK(audio.Inspect(state) == AudioResult::Applied && state.pausedVoices == 2);
    request.mode = 1; providers.random = 0.1f;
    CHECK(audio.Play(request) == AudioResult::Applied);
    CHECK(audio.Inspect(state) == AudioResult::Applied && state.activeVoices == 3 && state.loopingVoices == 1);
    // Original engine pause-all visits existing voices. A subsequently created
    // PlaySFX is explicitly unpaused by100806E5, even while the pause flag is set.
    CHECK(state.pausedVoices == 2 && state.globallyPaused && state.hasLoopSlot);
    CHECK(audio.InspectVoice(2, voice) == AudioResult::Applied && voice.globalSlot && !voice.paused);
    const auto parameterDeadline = GetTickCount64() + 500;
    while (voice.speed == 1 && GetTickCount64() < parameterDeadline)
    {
        Sleep(1);
        CHECK(audio.InspectVoice(2, voice) == AudioResult::Applied);
    }
    CHECK(std::bit_cast<unsigned>(voice.speed) == std::bit_cast<unsigned>(static_cast<float>(static_cast<double>(0.1f) + 1.0)));
    request.mode = 0;
    CHECK(audio.Play(request) == AudioResult::Capacity);
    CHECK(audio.SetPaused(false) == AudioResult::Applied);
    CHECK(audio.Inspect(state) == AudioResult::Applied && state.pausedVoices == 0);
    // Mode2 uses only the global loop slot: no path lookup or RNG/volume read.
    const auto randomBefore = providers.randomCalls, volumeBefore = providers.volumeCalls;
    request.mode = 2; request.sample = "not-prepared.wav";
    CHECK(audio.Play(request) == AudioResult::Applied);
    CHECK(audio.Play(request) == AudioResult::Applied);
    CHECK(providers.randomCalls == randomBefore && providers.volumeCalls == volumeBefore);
    CHECK(audio.Inspect(state) == AudioResult::Applied && state.activeVoices == 2 && state.hasLoopSlot);
    request.mode = 1; request.sample = "weapons_new/other.wav";
    CHECK(audio.Play(request) == AudioResult::Applied);
    CHECK(audio.Inspect(state) == AudioResult::Applied && state.activeVoices == 3 && state.loopingVoices == 1);
    // Only the loop slot gets live cvar volume updates; keep all samples silent.
    providers.volume = -1.0f; CHECK(audio.Update() == AudioResult::Applied);
    CHECK(audio.InspectVoice(3, voice) == AudioResult::Applied && voice.globalSlot && voice.volume == 0);
    request.mode = 1; request.sample = "not-prepared.wav";
    CHECK(audio.Play(request) == AudioResult::NotPrepared);
    CHECK(audio.Inspect(state) == AudioResult::Applied && !state.hasLoopSlot && state.loopingVoices == 0);
    CHECK(allocations.load() == baseline);
    // Wrong-thread requests are rejected before touching XAudio2 or providers.
    AudioResult otherThread = AudioResult::Applied;
    std::thread thread([&] { otherThread = audio.Play(request); }); thread.join();
    CHECK(otherThread == AudioResult::WrongThread);
    CHECK(audio.Shutdown() == AudioResult::Applied);
    CHECK(audio.Inspect(state) == AudioResult::Inactive && !state.preparedSamples);
    CHECK(audio.Shutdown() == AudioResult::Applied);
    CHECK(audio.Initialize({2, 2, 1024 * 1024}, providers.Bind()) == AudioResult::Applied);
    CHECK(audio.Prepare("pcm32.wav", Wave(32, 2, 48000), 1) == AudioResult::Applied);
    CHECK(audio.Prepare("reuse.wav", Wave(8, 1, 8000), 1) == AudioResult::Applied);
    CHECK(audio.Activate() == AudioResult::Applied);
    request = {"reuse.wav", 0, true, {0, 0, 0}};
    CHECK(audio.Play(request) == AudioResult::Applied);
    // Real completion, not a forced finished flag. PCM is one second of silence.
    Sleep(1200);
    CHECK(audio.Inspect(state) == AudioResult::Applied && state.activeVoices == 0);
    CHECK(audio.Play(request) == AudioResult::Applied);
    CHECK(audio.Shutdown() == AudioResult::Applied);
}
}

void* operator new(std::size_t size)
{
    ++allocations;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    Lifecycle();
    std::puts("real XAudio2 lifecycle, PCM formats, parallel/loop/global stop/pause, completion, capacity, thread and application allocation checks passed");
}
