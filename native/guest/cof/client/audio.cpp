// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#pragma clang fp contract(off)

namespace guest::cof::client
{
namespace
{
constexpr std::uint32_t NoVoice = std::numeric_limits<std::uint32_t>::max();
static_assert(std::atomic<HRESULT>::is_always_lock_free);
bool ValidKey(std::string_view key) noexcept
{
    return !key.empty() && key.size() <= 255 && key.find('\0') == key.npos;
}
std::uint16_t U16(const std::byte* p) noexcept
{
    return std::uint16_t(std::to_integer<unsigned>(p[0]) | (std::to_integer<unsigned>(p[1]) << 8));
}
std::uint32_t U32(const std::byte* p) noexcept
{
    return std::uint32_t(U16(p)) | (std::uint32_t(U16(p + 2)) << 16);
}
bool Tag(const std::byte* p, const char (&text)[5]) noexcept { return std::memcmp(p, text, 4) == 0; }
float Volume(float value) noexcept
{
    // Original MINSS(1,value) followed by MAXSS(0,value): unordered returns
    // the second operand both times. Preserve NaN and signed zero behavior.
    const float upper = 1.0f < value ? 1.0f : value;
    return 0.0f > upper ? 0.0f : upper;
}
struct Pcm
{
    WAVEFORMATEX format{};
    std::span<const std::byte> data;
};
AudioResult Parse(std::span<const std::byte> wave, Pcm& pcm) noexcept
{
    if (wave.size() < 12 || !Tag(wave.data(), "RIFF") || !Tag(wave.data() + 8, "WAVE"))
        return AudioResult::InvalidWave;
    const std::uint64_t limit64 = std::uint64_t(U32(wave.data() + 4)) + 8;
    if (limit64 > wave.size() || limit64 < 12) return AudioResult::InvalidWave;
    const auto limit = static_cast<std::size_t>(limit64);
    bool format = false, data = false;
    for (std::size_t cursor = 12; cursor < limit;)
    {
        if (limit - cursor < 8) return AudioResult::InvalidWave;
        const auto size = U32(wave.data() + cursor + 4);
        const auto start = cursor + 8;
        if (size > limit - start) return AudioResult::InvalidWave;
        const auto* p = wave.data() + start;
        if (Tag(wave.data() + cursor, "fmt "))
        {
            if (format || size < 16) return AudioResult::InvalidWave;
            format = true;
            if (U16(p) != WAVE_FORMAT_PCM) return AudioResult::UnsupportedFormat;
            auto& f = pcm.format;
            f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = U16(p + 2);
            f.nSamplesPerSec = U32(p + 4); f.nAvgBytesPerSec = U32(p + 8);
            f.nBlockAlign = U16(p + 12); f.wBitsPerSample = U16(p + 14);
            if ((f.nChannels != 1 && f.nChannels != 2) ||
                (f.wBitsPerSample != 8 && f.wBitsPerSample != 16 &&
                 f.wBitsPerSample != 24 && f.wBitsPerSample != 32) ||
                f.nSamplesPerSec < XAUDIO2_MIN_SAMPLE_RATE || f.nSamplesPerSec > XAUDIO2_MAX_SAMPLE_RATE)
                return AudioResult::UnsupportedFormat;
            const auto block = f.nChannels * (f.wBitsPerSample / 8);
            if (f.nBlockAlign != block || f.nAvgBytesPerSec != f.nSamplesPerSec * block)
                return AudioResult::InvalidWave;
        }
        else if (Tag(wave.data() + cursor, "data"))
        {
            if (data || !size || size > XAUDIO2_MAX_BUFFER_BYTES) return AudioResult::InvalidWave;
            data = true; pcm.data = {p, size};
        }
        const auto padded = std::uint64_t(size) + (size & 1u);
        if (padded > limit - start) return AudioResult::InvalidWave;
        cursor = start + static_cast<std::size_t>(padded);
    }
    if (!format || !data || pcm.data.size() % pcm.format.nBlockAlign) return AudioResult::InvalidWave;
    return AudioResult::Applied;
}
}

struct LocalAudio::Impl final : IXAudio2EngineCallback
{
    struct Sample
    {
        std::array<char, 256> key{};
        std::uint16_t length{};
        std::uint32_t first{}, count{}, bytes{};
        std::unique_ptr<std::byte[]> pcm;
        std::string_view Key() const noexcept { return {key.data(), length}; }
    };
    struct Voice
    {
        IXAudio2SourceVoice* audio{};
        std::uint32_t sample{};
        float requestedVolume{}, requestedSpeed{};
        bool active{}, paused{}, looped{};
    };
    DWORD thread{};
    HMODULE module{};
    IXAudio2* engine{};
    IXAudio2MasteringVoice* master{};
    bool com{}, ready{}, paused{};
    AudioProviders providers{};
    AudioLimits limits{};
    std::vector<Sample> samples;
    std::vector<Voice> voices;
    std::vector<std::uint32_t> lookup;
    std::uint32_t sampleCount{}, voiceCount{}, pcmBytes{}, loopSlot{NoVoice};
    std::atomic<HRESULT> error{S_OK};
    bool registered{};
    void STDMETHODCALLTYPE OnProcessingPassStart() noexcept override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() noexcept override {}
    // Device loss notification touches only this lock-free status word. No
    // game, transport, voice ownership or resource state runs on this thread.
    void STDMETHODCALLTYPE OnCriticalError(HRESULT value) noexcept override { error.store(value, std::memory_order_relaxed); }
    ~Impl()
    {
        if (registered) engine->UnregisterForCallbacks(this);
        for (auto& v : voices) if (v.audio) v.audio->DestroyVoice();
        if (master) master->DestroyVoice();
        if (engine) engine->Release();
        if (module) FreeLibrary(module);
        if (com) CoUninitialize();
    }
    bool Owner() const noexcept { return thread == GetCurrentThreadId(); }
    bool Failed() const noexcept { return FAILED(error.load(std::memory_order_relaxed)); }
    void Poll(Voice& v) noexcept
    {
        if (!v.active) return;
        XAUDIO2_VOICE_STATE state{};
        v.audio->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (!state.BuffersQueued) v.active = v.paused = v.looped = false;
    }
    bool Stop(Voice& v) noexcept
    {
        if (FAILED(v.audio->Stop()) || FAILED(v.audio->FlushSourceBuffers())) return false;
        v.active = v.paused = v.looped = false;
        return true;
    }
};

LocalAudio::LocalAudio() noexcept = default;
LocalAudio::~LocalAudio() = default;

AudioResult LocalAudio::Initialize(AudioLimits limits, AudioProviders providers) noexcept
{
    if (impl_) return impl_->Owner() ? AudioResult::Inactive : AudioResult::WrongThread;
    if (!limits.samples || limits.samples > 256 || !limits.voices || limits.voices > 2048 ||
        !limits.pcmBytes || limits.pcmBytes > 256u * 1024u * 1024u || !providers.volume || !providers.randomFloat)
        return AudioResult::InvalidLimits;
    try
    {
        auto p = std::make_unique<Impl>();
        p->thread = GetCurrentThreadId(); p->limits = limits; p->providers = providers;
        p->samples.resize(limits.samples); p->voices.resize(limits.voices); p->lookup.resize(limits.samples);
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com) && com != RPC_E_CHANGED_MODE) return AudioResult::DeviceFailure;
        p->com = SUCCEEDED(com);
        p->module = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!p->module) return AudioResult::DeviceFailure;
        using Create = HRESULT (WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
        const auto create = reinterpret_cast<Create>(GetProcAddress(p->module, "XAudio2Create"));
        if (!create || FAILED(create(&p->engine, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
            FAILED(p->engine->CreateMasteringVoice(&p->master))) return AudioResult::DeviceFailure;
        if (FAILED(p->engine->RegisterForCallbacks(p.get()))) return AudioResult::DeviceFailure;
        p->registered = true;
        impl_ = std::move(p);
        return AudioResult::Applied;
    }
    catch (const std::bad_alloc&) { return AudioResult::Capacity; }
}

AudioResult LocalAudio::Prepare(std::string_view key, std::span<const std::byte> wave,
                               std::uint32_t capacity) noexcept
{
    if (!impl_) return AudioResult::Inactive;
    auto& p = *impl_;
    if (!p.Owner()) return AudioResult::WrongThread;
    if (p.Failed()) return AudioResult::DeviceFailure;
    if (p.ready) return AudioResult::Inactive;
    if (!ValidKey(key)) return AudioResult::InvalidSample;
    for (std::uint32_t i = 0; i < p.sampleCount; ++i)
        if (p.samples[i].Key() == key) return AudioResult::DuplicateSample;
    Pcm pcm;
    const auto result = Parse(wave, pcm);
    if (result != AudioResult::Applied) return result;
    if (!capacity || capacity > p.limits.voices - p.voiceCount || p.sampleCount == p.limits.samples ||
        pcm.data.size() > p.limits.pcmBytes - p.pcmBytes) return AudioResult::Capacity;
    auto memory = std::unique_ptr<std::byte[]>(new (std::nothrow) std::byte[pcm.data.size()]);
    if (!memory) return AudioResult::Capacity;
    std::memcpy(memory.get(), pcm.data.data(), pcm.data.size());
    std::uint32_t made = 0;
    for (; made < capacity; ++made)
    {
        auto& v = p.voices[p.voiceCount + made];
        if (FAILED(p.engine->CreateSourceVoice(&v.audio, &pcm.format, 0, 2.0f))) break;
        v.sample = p.sampleCount;
    }
    if (made != capacity)
    {
        while (made) { auto& v = p.voices[p.voiceCount + --made]; v.audio->DestroyVoice(); v = {}; }
        return AudioResult::DeviceFailure;
    }
    auto& s = p.samples[p.sampleCount];
    std::memcpy(s.key.data(), key.data(), key.size()); s.length = static_cast<std::uint16_t>(key.size());
    s.pcm = std::move(memory); s.bytes = static_cast<std::uint32_t>(pcm.data.size());
    s.first = p.voiceCount; s.count = capacity;
    p.lookup[p.sampleCount] = p.sampleCount;
    ++p.sampleCount; p.voiceCount += capacity; p.pcmBytes += s.bytes;
    return AudioResult::Applied;
}

AudioResult LocalAudio::Activate() noexcept
{
    if (!impl_) return AudioResult::Inactive;
    auto& p = *impl_;
    if (!p.Owner()) return AudioResult::WrongThread;
    if (p.Failed()) return AudioResult::DeviceFailure;
    if (!p.sampleCount || p.ready) return AudioResult::Inactive;
    std::sort(p.lookup.begin(), p.lookup.begin() + p.sampleCount,
              [&p](auto a, auto b) { return p.samples[a].Key() < p.samples[b].Key(); });
    p.ready = true;
    return AudioResult::Applied;
}

AudioResult LocalAudio::Play(const LocalSound& request) noexcept
{
    if (!impl_) return AudioResult::Inactive;
    auto& p = *impl_;
    if (!p.Owner()) return AudioResult::WrongThread;
    if (p.Failed()) return AudioResult::DeviceFailure;
    if (!p.ready) return AudioResult::Inactive;
    if (!ValidKey(request.sample)) return AudioResult::InvalidSample;
    if (request.mode == 2)
        return p.loopSlot == NoVoice || p.Stop(p.voices[p.loopSlot]) ? AudioResult::Applied : AudioResult::DeviceFailure;
    // Original mode1 stops the old loop before attempting to create its replacement.
    if (request.mode == 1 && p.loopSlot != NoVoice)
    {
        if (!p.Stop(p.voices[p.loopSlot])) return AudioResult::DeviceFailure;
        p.loopSlot = NoVoice;
    }
    const auto end = p.lookup.begin() + p.sampleCount;
    const auto found = std::lower_bound(p.lookup.begin(), end, request.sample,
        [&p](auto index, auto key) { return p.samples[index].Key() < key; });
    if (found == end || p.samples[*found].Key() != request.sample) return AudioResult::NotPrepared;
    const auto& s = p.samples[*found];
    std::uint32_t selected = NoVoice;
    for (std::uint32_t i = s.first; i < s.first + s.count; ++i)
    {
        auto& v = p.voices[i]; p.Poll(v);
        if (!v.active && i != p.loopSlot) { selected = i; break; }
    }
    if (selected == NoVoice) return AudioResult::Capacity;
    auto& v = p.voices[selected];
    if (request.mode == 1) p.loopSlot = selected;
    const float volume = Volume(p.providers.volume(p.providers.context));
    const float random = p.providers.randomFloat(p.providers.context, -0.1f, 0.1f);
    const float speed = static_cast<float>(static_cast<double>(random) + 1.0);
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM; buffer.AudioBytes = s.bytes;
    buffer.pAudioData = reinterpret_cast<const BYTE*>(s.pcm.get());
    buffer.LoopCount = request.mode == 1 ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(v.audio->SetVolume(volume)) || FAILED(v.audio->SetFrequencyRatio(speed)) ||
        FAILED(v.audio->SubmitSourceBuffer(&buffer)) || FAILED(v.audio->Start()))
    {
        p.Stop(v);
        return AudioResult::DeviceFailure;
    }
    v.requestedVolume = volume; v.requestedSpeed = speed;
    v.active = true; v.paused = false; v.looped = request.mode == 1;
    return AudioResult::Applied;
}

AudioResult LocalAudio::Update() noexcept
{
    if (!impl_) return AudioResult::Inactive;
    auto& p = *impl_;
    if (!p.Owner()) return AudioResult::WrongThread;
    if (p.Failed()) return AudioResult::DeviceFailure;
    if (!p.ready) return AudioResult::Inactive;
    if (p.loopSlot != NoVoice)
    {
        auto& v = p.voices[p.loopSlot];
        const float volume = Volume(p.providers.volume(p.providers.context));
        if (FAILED(v.audio->SetVolume(volume))) return AudioResult::DeviceFailure;
        v.requestedVolume = volume;
    }
    return AudioResult::Applied;
}

AudioResult LocalAudio::SetPaused(bool paused) noexcept
{
    if (!impl_) return AudioResult::Inactive;
    auto& p = *impl_;
    if (!p.Owner()) return AudioResult::WrongThread;
    if (p.Failed()) return AudioResult::DeviceFailure;
    if (!p.ready) return AudioResult::Inactive;
    for (std::uint32_t i = 0; i < p.voiceCount; ++i)
    {
        auto& v = p.voices[i]; p.Poll(v);
        if (v.active)
        {
            const auto result = paused ? v.audio->Stop() : v.audio->Start();
            if (FAILED(result)) return AudioResult::DeviceFailure;
            v.paused = paused;
        }
    }
    p.paused = paused;
    return AudioResult::Applied;
}

AudioResult LocalAudio::Shutdown() noexcept
{
    if (!impl_) return AudioResult::Applied;
    if (!impl_->Owner()) return AudioResult::WrongThread;
    impl_.reset();
    return AudioResult::Applied;
}

AudioResult LocalAudio::Inspect(AudioState& state) noexcept
{
    state = {};
    if (!impl_) return AudioResult::Inactive;
    auto& p = *impl_;
    if (!p.Owner()) return AudioResult::WrongThread;
    state.preparedSamples = p.sampleCount; state.preparedVoices = p.voiceCount;
    state.retainedPcmBytes = p.pcmBytes; state.ready = p.ready;
    state.globallyPaused = p.paused; state.hasLoopSlot = p.loopSlot != NoVoice;
    for (std::uint32_t i = 0; i < p.voiceCount; ++i)
    {
        auto& v = p.voices[i]; p.Poll(v);
        if (v.active) { ++state.activeVoices; state.pausedVoices += v.paused; state.loopingVoices += v.looped; }
    }
    return AudioResult::Applied;
}

AudioResult LocalAudio::InspectVoice(std::uint32_t index, AudioVoiceState& state) noexcept
{
    state = {};
    if (!impl_) return AudioResult::Inactive;
    auto& p = *impl_;
    if (!p.Owner()) return AudioResult::WrongThread;
    if (index >= p.voiceCount) return AudioResult::NotPrepared;
    auto& v = p.voices[index]; p.Poll(v);
    XAUDIO2_VOICE_STATE actual{}; v.audio->GetState(&actual, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    state.sample = p.samples[v.sample].Key(); state.buffersQueued = actual.BuffersQueued;
    v.audio->GetVolume(&state.volume); v.audio->GetFrequencyRatio(&state.speed);
    state.requestedVolume = v.requestedVolume; state.requestedSpeed = v.requestedSpeed;
    state.active = v.active; state.paused = v.paused; state.looped = v.looped; state.globalSlot = index == p.loopSlot;
    return AudioResult::Applied;
}
}
