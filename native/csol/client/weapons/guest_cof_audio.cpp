// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof_audio.hpp"
#include "../../../guest/cof/presentation_protocol.hpp"
#include "../../../include/goldcraft/connection_api.h"

#include <event_api.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace csol::client
{
namespace
{
using namespace guest::cof::client;
struct RemoteAsset
{
    std::array<char, 256> key{};
    std::array<char, 64> path{};
    std::uint16_t length{};
    std::string_view Key() const noexcept { return {key.data(), length}; }
};

LocalAudio audio;
std::array<RemoteAsset, 256> remoteAssets;
std::size_t remoteCount{};
const GoldCraftConnectionAPI* connection{};
std::uint64_t preparedGeneration{};
bool ready{};
DWORD ownerThread{};
bool paused{};
bool preparing{};
std::uint64_t preparationSerial{1};

void Retire() noexcept
{
    if (preparationSerial)
        preparationSerial = preparationSerial == std::numeric_limits<std::uint64_t>::max() ?
            0 : preparationSerial + 1;
    ready = false;
    preparedGeneration = 0;
    remoteCount = 0;
    paused = false;
    audio.Shutdown();
}

std::uint64_t Generation() noexcept { return connection ? connection->generation() : 0; }

bool Synchronize() noexcept
{
    if (!ready) return false;
    if (!ownerThread || ownerThread != GetCurrentThreadId()) return false;
    const auto current = Generation();
    if (ready && (!current || current != preparedGeneration)) Retire();
    return ready;
}

bool Local(void*, const LocalSound& sound) noexcept
{
    return Synchronize() && audio.Play(sound) == AudioResult::Applied;
}

bool Remote(void*, const RemoteSound& sound) noexcept
{
    if (!Synchronize() || !engine.pEventAPI || !engine.pEventAPI->EV_PlaySound) return false;
    const auto end = remoteAssets.begin() + remoteCount;
    const auto found = std::lower_bound(remoteAssets.begin(), end, sound.sample,
        [](const RemoteAsset& a, std::string_view b) { return a.Key() < b; });
    if (found == end || found->Key() != sound.sample) return false;
    auto position = sound.origin;
    engine.pEventAPI->EV_PlaySound(sound.entity, position.data(), sound.channel,
        found->path.data(), sound.volume, sound.attenuation, sound.flags, sound.pitch);
    return true;
}

int Receive(const char*, int size, void* data)
{
    if (!Synchronize() || !data || size < 0) return 1;
    guest::cof::SoundMessageData sound{};
    if (guest::cof::DecodeSound({static_cast<const std::byte*>(data), std::size_t(size)}, sound))
        DispatchSound({nullptr, Remote, Local, nullptr}, sound);
    return 1;
}

bool SafeKey(std::string_view key, std::size_t limit) noexcept
{
    return !key.empty() && key.size() <= limit && key.find('\0') == key.npos;
}

bool SafeAsset(std::string_view path) noexcept
{
    // Only the authored guest namespace can reach the native engine. No
    // untrusted message path becomes a filesystem lookup or another mod's sound.
    if (!path.starts_with("guest/cof/") || !path.ends_with(".wav") || path.size() >= 64) return false;
    for (const auto c : path)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.' || c == '/')) return false;
    for (std::size_t start = 0; start < path.size();)
    {
        const auto slash = path.find('/', start);
        const auto part = path.substr(start, slash == path.npos ? path.size() - start : slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == path.npos) break;
        start = slash + 1;
    }
    return true;
}

float Volume(void*) noexcept { return engine.pfnGetCvarFloat("volume"); }
float Random(void*, float low, float high) noexcept { return engine.pfnRandomFloat(low, high); }
}

void InitializeGuestCofAudio() { InitializeGuestCofAudio(GoldCraftConnectionInterface()); }

void InitializeGuestCofAudio(const GoldCraftConnectionAPI* provider)
{
    if (ownerThread && ownerThread != GetCurrentThreadId())
        throw std::runtime_error("CoF audio initialization is on the wrong thread");
    ownerThread = GetCurrentThreadId();
    Retire();
    connection = provider && provider->size >= sizeof(GoldCraftConnectionAPI) &&
        provider->version == 1 && provider->generation ? provider : nullptr;
    const auto old = api->HookUserMsg(guest::cof::SoundMessage, Receive);
    if (!old) engine.pfnHookUserMsg(guest::cof::SoundMessage, Receive);
    else if (old != Receive)
    {
        api->HookUserMsg(guest::cof::SoundMessage, old);
        throw std::runtime_error("CoF sound message already has a consumer");
    }
}

AudioResult PrepareGuestCofAudio(const GuestCofAudioPlan& plan) noexcept
{
    if (!ownerThread) return AudioResult::Inactive;
    if (ownerThread != GetCurrentThreadId()) return AudioResult::WrongThread;
    if (preparing) return AudioResult::Inactive;
    struct Preparing
    {
        Preparing() noexcept { preparing = true; }
        ~Preparing() { preparing = false; }
    } preparingScope;
    Retire();
    const auto serial = preparationSerial;
    const auto generation = Generation();
    if (!generation || !serial) return AudioResult::Inactive;
    if (!plan.remotePrecached || !engine.pEventAPI || !engine.pEventAPI->EV_PlaySound ||
        !engine.pfnGetCvarFloat || !engine.pfnRandomFloat)
        return AudioResult::NotPrepared;
    if (plan.assets.empty() || plan.assets.size() > remoteAssets.size() ||
        plan.limits.samples != plan.assets.size()) return AudioResult::InvalidLimits;

    // Validate the complete plan before creating a device or voices. Keys and
    // native paths are copied; the asynchronous mixer retains only owned PCM.
    for (std::size_t i = 0; i < plan.assets.size(); ++i)
    {
        const auto& a = plan.assets[i];
        if (!SafeKey(a.localSample, 255) || !SafeKey(a.remoteSample, 255) ||
            !SafeAsset(a.remoteAsset)) return AudioResult::InvalidSample;
        if (!a.voices || !plan.remotePrecached(plan.context, a.remoteAsset) ||
            Generation() != generation || preparationSerial != serial)
            return AudioResult::NotPrepared;
        for (std::size_t j = 0; j < i; ++j)
            if (plan.assets[j].localSample == a.localSample || plan.assets[j].remoteSample == a.remoteSample)
                return AudioResult::DuplicateSample;
    }
    auto result = audio.Initialize(plan.limits, {nullptr, Volume, Random});
    if (result != AudioResult::Applied) return result;
    for (const auto& a : plan.assets)
    {
        result = audio.Prepare(a.localSample, a.wave, a.voices);
        if (result != AudioResult::Applied || Generation() != generation || preparationSerial != serial)
        {
            Retire();
            return result == AudioResult::Applied ? AudioResult::Inactive : result;
        }
        auto& remote = remoteAssets[remoteCount++];
        remote.length = static_cast<std::uint16_t>(a.remoteSample.size());
        std::memcpy(remote.key.data(), a.remoteSample.data(), a.remoteSample.size());
        std::memcpy(remote.path.data(), a.remoteAsset.data(), a.remoteAsset.size());
        remote.path[a.remoteAsset.size()] = 0;
    }
    std::sort(remoteAssets.begin(), remoteAssets.begin() + remoteCount,
        [](const RemoteAsset& a, const RemoteAsset& b) { return a.Key() < b.Key(); });
    result = audio.Activate();
    if (result != AudioResult::Applied || Generation() != generation || preparationSerial != serial)
    {
        Retire();
        return result == AudioResult::Applied ? AudioResult::Inactive : result;
    }
    preparedGeneration = generation;
    ready = true;
    return AudioResult::Applied;
}

bool GuestCofAudioReady() noexcept { return Synchronize(); }

void GuestCofAudioFrame() noexcept
{
    if (!Synchronize()) return;
    const auto serial = preparationSerial;
    if (audio.Update() != AudioResult::Applied && preparationSerial == serial) Retire();
}

void GuestCofAudioPause(bool next) noexcept
{
    if (!Synchronize() || next == paused) return;
    if (audio.SetPaused(next) != AudioResult::Applied) Retire();
    else paused = next;
}

void ShutdownGuestCofAudio() noexcept
{
    if (ownerThread && ownerThread != GetCurrentThreadId()) return;
    Retire();
    connection = nullptr;
    ownerThread = 0;
}
}
