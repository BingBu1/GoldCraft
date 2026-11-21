// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "../resource_refresh.hpp"
#include "guest_cof_resources.hpp"
#include "guest_cof_effects.hpp"
#include "../../../guest/cof/resource_catalog.hpp"
#include "../../../guest/cof/arsenal_audio.hpp"
#include "../../../guest/cof/client/resource_groups.hpp"
#include "../../../guest/cof/admission.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include "../../../include/goldcraft/sound_precache_api.h"
#include "../../../include/goldcraft/precache_ready_api.h"

#include <algorithm>
#include <limits>

namespace csol::client
{
namespace
{
using namespace guest::cof::resources;
using guest::cof::client::AudioResult;
using guest::cof::client::AuroraResult;
constexpr auto maxSamples=guest::cof::client::FirearmLocalSampleCount + 1; // shared M16 switch when FAMAS is the only owner
constexpr auto fog = P345Models[static_cast<unsigned>(P345Model::Fog)].host;
const GoldCraftConnectionAPI* connection{};
const GoldCraftSoundPrecacheAPI* sounds{};
const GoldCraftPrecacheReadyAPI* notifications{};
GuestCofResourceBudget budget{};
GuestCofResourceState state{};
std::vector<std::byte> particleScript;
std::array<std::vector<std::byte>, ShotDecals.size()> decalImages;
std::uint64_t serial{1}, subscription{};
DWORD ownerThread{};
bool preparing{}; // Survives replacement while an old synchronous callback unwinds.

bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
void Advance() noexcept
{
    if (serial) serial = serial == std::numeric_limits<std::uint64_t>::max() ? 0 : serial + 1;
}
struct Lease
{
    std::uint64_t serial, generation;
    const GoldCraftConnectionAPI* connection;
    const GoldCraftSoundPrecacheAPI* sounds;
    bool Current() const noexcept
    {
        return serial && serial == ::csol::client::serial && Owner() && connection &&
            connection->generation() == generation && serial == ::csol::client::serial;
    }
};

void Invalidate(std::uint64_t expected) noexcept
{
    InvalidateGuestCofEffects();
    if (serial != expected) return;
    InvalidateGuestCofRevolverEvents();
    if (serial != expected) return;
    InvalidateGuestCofAudioResources();
    if (serial == expected) InvalidateGuestCofParticleResources();
    if (serial == expected) InvalidateGuestCofDecalResources();
}
bool NativeSound(void* context, std::string_view asset) noexcept
{
    const auto& lease = *static_cast<const Lease*>(context);
    if (!lease.Current()) return false;
    const auto result = lease.sounds->inspect(lease.generation, asset.data(), std::uint32_t(asset.size()));
    // Original PlaySFX selects independent local PCM or native playback.
    // A native SFX may be registered while its engine cache remains lazy.
    return (result & GoldCraftSoundSignonRegistered) != 0 && lease.Current();
}
AudioResult PrepareMapAudio(Lease& lease, const GuestCofResourceBudget& limits,
    std::span<const GuestCofSoundAsset> base, std::size_t availableBytes, unsigned& added) noexcept
{
    using guest::cof::arsenal::Gunshots;
    std::array<GuestCofSoundAsset, maxSamples + Gunshots.size()> plan{};
    std::array<std::vector<std::byte>, Gunshots.size()> waves;
    std::copy(base.begin(), base.end(), plan.begin());
    added = 0;
    if (!limits.voicesPerSample) return AudioResult::InvalidLimits;
    unsigned count = static_cast<unsigned>(base.size());
    for (std::size_t i = 0; i < Gunshots.size(); ++i) {
        const auto& sample = Gunshots[i];
        if (count >= 2048 / limits.voicesPerSample || !NativeSound(&lease, sample.nativeAsset)) continue;
        if (!lease.Current() || !ResourceReadsAllowed()) return AudioResult::Inactive;
        try { waves[i] = ReadResource(sample.localFile); }
        catch (const std::exception&) { continue; }
        if (!lease.Current()) return AudioResult::Inactive;
        if (waves[i].empty() || waves[i].size() > availableBytes) continue;
        availableBytes -= waves[i].size();
        plan[count++] = {sample.localKey, sample.remoteKey, sample.nativeAsset, waves[i], limits.voicesPerSample};
    }
    const auto prepare = [&](unsigned n) {
        return PrepareGuestCofAudio({{n, n * limits.voicesPerSample, limits.pcmBytes},
            {plan.data(), n}, &lease, NativeSound});
    };
    if (!count) return AudioResult::NotPrepared;
    auto result = prepare(count);
    const auto baseCount = static_cast<unsigned>(base.size());
    if (result == AudioResult::Applied) { added = count - baseCount; return result; }
    if (!lease.Current() || count == baseCount) return result;
    // Preserve usable guns if one optional waveform was corrupt in the download.
    if (count == baseCount + 2) {
        for (unsigned i = baseCount; i < count; ++i) {
            plan[baseCount] = plan[i];
            result = prepare(baseCount + 1);
            if (result == AudioResult::Applied) { added = 1; return result; }
            if (!lease.Current()) return AudioResult::Inactive;
        }
    }
    return baseCount ? prepare(baseCount) : result;
}

void Failed(const Lease& lease, GuestCofResourceState candidate) noexcept
{
    if (!lease.Current()) return;
    state = candidate;
    particleScript.clear();
    for (auto& image : decalImages) image.clear();
    Invalidate(lease.serial);
}

void Ready(void*, std::uint64_t generation) noexcept
{
    if (!Owner() || !generation || !serial || !connection || !sounds) return;
    Lease lease{serial, generation, connection, sounds};
    if (!lease.Current() || state.generation == generation) return;
    if (preparing)
    {
        state = {GuestCofResourceStatus::Cancelled, generation};
        return;
    }
    struct Preparation
    {
        Preparation() noexcept { preparing = true; }
        ~Preparation() { preparing = false; }
    } preparation;
    GuestCofResourceState candidate{GuestCofResourceStatus::Preparing, generation, 1};
    state = candidate;
    particleScript.clear();
    Invalidate(lease.serial);
    if (!lease.Current()) return;
    // The original remote shell event uses native EV_PlaySound independently
    // from local PCM. Its own signon gate does not admit a whole weapon.
    PrepareGuestCofRevolverEvents(sounds);
    if (!lease.Current()) return;

    // Registration discovery is a no-I/O query. Ordinary servers must never
    // open a CoF file, create an audio device, or load the guest sprite.
    std::array<const LocalSoundResource*, maxSamples> selected{};
    unsigned legacySamples{};
    constexpr auto famasBit = guest::cof::FirearmCapability(guest::cof::Firearm::Famas);
    for (const auto& definition : guest::cof::client::FirearmResourceGroups)
    {
        const bool famas = definition.identity == guest::cof::Firearm::Famas;
        if (famas) legacySamples = candidate.samples;
        const auto group=definition.localSounds;
        unsigned registered{};
        for (const auto& asset : group)
        {
            const auto flags = lease.sounds->inspect(generation, asset.remoteAsset.data(),
                                                   std::uint32_t(asset.remoteAsset.size()));
            if (!lease.Current()) return;
            registered += (flags & GoldCraftSoundSignonRegistered) != 0;
        }
        if (!registered) continue;
        if (registered != group.size())
        {
            if (famas) continue;
            // The FAMAS draw sequence can advertise just M16's switch sound.
            if (definition.identity == guest::cof::Firearm::M16 && registered == 1 &&
                NativeSound(&lease, M16AudioResources.back().remoteAsset)) continue;
            candidate.status = GuestCofResourceStatus::NativeSoundUnavailable;
            candidate.resource = group.front().remoteAsset.data();
            Failed(lease, candidate);
            return;
        }
        if(definition.identity==guest::cof::Firearm::Revolver&&!GuestCofRevolverEventsReady())
        {
            candidate.status=GuestCofResourceStatus::NativeSoundUnavailable;
            candidate.resource=RevolverSounds[static_cast<unsigned>(RevolverSound::Shell)].host;
            Failed(lease,candidate);return;
        }
        candidate.weapons|=guest::cof::FirearmCapability(definition.identity);
        for (const auto& asset : group) selected[candidate.samples++] = &asset;
        if (famas && !(candidate.weapons & guest::cof::FirearmCapability(guest::cof::Firearm::M16)) &&
            NativeSound(&lease, M16AudioResources.back().remoteAsset))
            selected[candidate.samples++] = &M16AudioResources.back();
        if (!lease.Current()) return;
    }
    if (!candidate.samples)
    {
        unsigned extra{};
        candidate.audio = PrepareMapAudio(lease, budget, {}, budget.pcmBytes, extra);
        if (!lease.Current()) return;
        candidate.samples = extra;
        candidate.status = extra ? GuestCofResourceStatus::Prepared : GuestCofResourceStatus::NoAssets;
        if (extra) candidate.studioAndRecoilPrepared = PrepareGuestCofEffects();
        if (lease.Current()) state = candidate;
        return;
    }
    const auto coldBudget = budget;
    const auto omitFamasAudio = [&] {
        candidate.samples = legacySamples;
        candidate.weapons &= ~famasBit;
    };
    if (legacySamples && (candidate.weapons & famasBit) && coldBudget.voicesPerSample > 2048 / candidate.samples)
        omitFamasAudio();
    if (!coldBudget.voicesPerSample || coldBudget.voicesPerSample > 2048 / candidate.samples ||
        !coldBudget.pcmBytes || coldBudget.pcmBytes > 256u * 1024u * 1024u)
    {
        candidate.status = GuestCofResourceStatus::InvalidBudget;
        Failed(lease, candidate);
        return;
    }
    if (!ResourceReadsAllowed())
    {
        candidate.status = GuestCofResourceStatus::ResourceFailed;
        Failed(lease, candidate);
        return;
    }
    try
    {
        std::array<std::vector<std::byte>, maxSamples> waves;
        std::array<GuestCofSoundAsset, maxSamples> assets;
        // Host memory policy also bounds retained cold file bytes. The real
        // audio decoder independently enforces its decoded PCM limit.
        std::size_t fileBytes{}, legacyBytes{};
        for (unsigned i = 0; i < candidate.samples; ++i)
        {
            const auto& asset = *selected[i];
            candidate.resource = asset.localFile.data();
            if (i < legacySamples) waves[i] = ReadResource(asset.localFile.data());
            else
            {
                try { waves[i] = ReadResource(asset.localFile.data()); }
                catch (const std::exception&) { waves[i].clear(); }
                if (!lease.Current()) return;
                if (legacySamples && ResourceReadsAllowed() &&
                    (waves[i].empty() || waves[i].size() > coldBudget.pcmBytes - fileBytes))
                { omitFamasAudio(); fileBytes = legacyBytes; break; }
            }
            if (!lease.Current()) return;
            if (!ResourceReadsAllowed() || waves[i].size() > coldBudget.pcmBytes - fileBytes)
            {
                candidate.status = GuestCofResourceStatus::ResourceFailed;
                Failed(lease, candidate);
                return;
            }
            fileBytes += waves[i].size();
            if (i + 1 == legacySamples) legacyBytes = fileBytes;
            assets[i] = {asset.localKey, asset.remoteKey, asset.remoteAsset, waves[i], coldBudget.voicesPerSample};
        }
        candidate.resource = P345Particle.host;
        auto script = ReadResource(P345Particle.host);
        if (!lease.Current()) return;
        if (legacySamples && (candidate.weapons & famasBit) && script.size() > coldBudget.pcmBytes - fileBytes)
        { omitFamasAudio(); fileBytes = legacyBytes; }
        if (!ResourceReadsAllowed() || script.size() > coldBudget.pcmBytes - fileBytes || !engine.pfnSPR_Load)
        {
            candidate.status = GuestCofResourceStatus::ResourceFailed;
            Failed(lease, candidate);
            return;
        }
        candidate.resource = fog;
        const auto sprite = engine.pfnSPR_Load(fog);
        if (!lease.Current()) return;
        if (!ResourceReadsAllowed() || sprite <= 0)
        {
            candidate.status = GuestCofResourceStatus::ResourceFailed;
            Failed(lease, candidate);
            return;
        }
        unsigned extraSamples{};
        const auto prepareAudio = [&] {
            return PrepareMapAudio(lease, coldBudget, {assets.data(), candidate.samples},
                coldBudget.pcmBytes - fileBytes - script.size(), extraSamples);
        };
        candidate.audio = prepareAudio();
        if (lease.Current() && candidate.audio != AudioResult::Applied && legacySamples &&
            (candidate.weapons & famasBit))
        {
            omitFamasAudio();
            fileBytes = legacyBytes;
            candidate.audio = prepareAudio();
        }
        if (!lease.Current()) return;
        if (candidate.audio != AudioResult::Applied)
        {
            candidate.status = GuestCofResourceStatus::AudioFailed;
            Failed(lease, candidate);
            return;
        }
        candidate.samples += extraSamples;
        candidate.particles = PrepareGuestCofParticles({
            {reinterpret_cast<const char*>(script.data()), script.size()}, sprite});
        if (!lease.Current()) return;
        if (candidate.particles != AuroraResult::Applied)
        {
            candidate.status = GuestCofResourceStatus::ParticleFailed;
            Failed(lease, candidate);
            return;
        }
        particleScript = std::move(script);
        candidate.studioAndRecoilPrepared = PrepareGuestCofEffects();
        if (candidate.studioAndRecoilPrepared && GuestCofFamasViewReady()) candidate.weapons |= famasBit;
        else candidate.weapons &= ~famasBit;
        if (!lease.Current()) return;
        if (candidate.weapons)
        {
            std::array<std::vector<std::byte>, ShotDecals.size()> images;
            std::array<std::span<const std::uint8_t>, ShotDecals.size()> plan;
            for (std::size_t i = 0; i < images.size(); ++i)
            {
                candidate.resource = ShotDecals[i].host;
                images[i] = ReadResource(ShotDecals[i].host);
                if (!lease.Current()) return;
                // Original shot textures are 64x64 RGB24. Bound cold bytes,
                // while the real decoder validates their layout/payload.
                if (!ResourceReadsAllowed() || images[i].size() < 18 || images[i].size() > 18 + 255 + 64*64*3)
                {
                    candidate.status = GuestCofResourceStatus::ResourceFailed;
                    Failed(lease, candidate);
                    return;
                }
                plan[i] = {reinterpret_cast<const std::uint8_t*>(images[i].data()), images[i].size()};
            }
            candidate.decalsPrepared = PrepareGuestCofDecalResources(plan);
            if (!lease.Current()) return;
            if (!candidate.decalsPrepared)
            {
                candidate.status = GuestCofResourceStatus::DecalFailed;
                Failed(lease, candidate);
                return;
            }
            decalImages = std::move(images);
        }
        candidate.status = GuestCofResourceStatus::Prepared;
        candidate.resource = nullptr;
        state = candidate;
    }
    catch (...)
    {
        candidate.status = GuestCofResourceStatus::ResourceFailed;
        Failed(lease, candidate);
    }
}

void Retired(void*, std::uint64_t generation) noexcept
{
    if (!Owner() || !generation || state.generation != generation) return;
    Advance();
    const auto retiredSerial = serial;
    state = {GuestCofResourceStatus::Cancelled, generation};
    particleScript.clear();
    for (auto& image : decalImages) image.clear();
    Invalidate(retiredSerial);
}

template<class Interface, class Getter>
const Interface* InterfaceFrom(const char* name) noexcept
{
    const auto module = GetModuleHandleA("GoldCraft.dll");
    const auto getter = module ? reinterpret_cast<Getter>(GetProcAddress(module, name)) : nullptr;
    return getter ? getter() : nullptr;
}
}

void InitializeGuestCofResources(GuestCofResourceBudget limits)
{
    InitializeGuestCofResources(limits, GoldCraftConnectionInterface(), GoldCraftSoundPrecacheInterface(),
        InterfaceFrom<GoldCraftPrecacheReadyAPI, GoldCraftGetPrecacheReadyAPI>("GoldCraft_GetPrecacheReadyAPI"));
}
void InitializeGuestCofResources(GuestCofResourceBudget limits, const GoldCraftConnectionAPI* provider,
                               const GoldCraftSoundPrecacheAPI* soundProvider,
                               const GoldCraftPrecacheReadyAPI* lifecycle)
{
    if (ownerThread && !Owner()) throw std::runtime_error("CoF resource initialization is on the wrong thread");
    const auto before = serial;
    ShutdownGuestCofResources();
    if (!serial || serial != before + 1) return;
    ownerThread = GetCurrentThreadId();
    budget = limits;
    if (!provider || provider->size < sizeof(*provider) || provider->version != 1 || !provider->generation ||
        !soundProvider || soundProvider->size < sizeof(*soundProvider) || soundProvider->version != 1 ||
        !soundProvider->inspect || !lifecycle || lifecycle->size < sizeof(*lifecycle) || lifecycle->version != 1 ||
        !lifecycle->subscribe || !lifecycle->unsubscribe)
    {
        state.status = GuestCofResourceStatus::Unavailable;
        return;
    }
    connection = provider;
    sounds = soundProvider;
    notifications = lifecycle;
    state.status = GuestCofResourceStatus::Armed;
    const auto registering = serial;
    const auto token = lifecycle->subscribe(Ready, Retired, nullptr);
    if (serial != registering)
    {
        if (token) lifecycle->unsubscribe(token);
        return;
    }
    subscription = token;
    if (!token) state.status = GuestCofResourceStatus::Unavailable;
}
void ShutdownGuestCofResources() noexcept
{
    if (ownerThread && !Owner()) return;
    Advance();
    const auto retiring = serial;
    const auto* lifecycle = notifications;
    const auto token = subscription;
    notifications = nullptr;
    subscription = 0;
    connection = nullptr;
    sounds = nullptr;
    ownerThread = 0;
    state = {};
    particleScript.clear();
    for (auto& image : decalImages) image.clear();
    if (token) lifecycle->unsubscribe(token);
    if (serial == retiring) Invalidate(retiring);
}

void GuestCofResourcesVideoInit() noexcept
{
    if (!Owner() || preparing || state.status != GuestCofResourceStatus::Prepared || !connection) return;
    const Lease lease{serial, state.generation, connection, sounds};
    if (!lease.Current()) return;
    struct Preparation
    {
        Preparation() noexcept { preparing = true; }
        ~Preparation() { preparing = false; }
    } preparation;
    auto candidate = state;
    if (!candidate.weapons && particleScript.empty()) return;
    if (!ResourceReadsAllowed() || !engine.pfnSPR_Load || particleScript.empty())
    {
        candidate.status = GuestCofResourceStatus::ResourceFailed;
        Failed(lease, candidate);
        return;
    }
    const auto sprite = engine.pfnSPR_Load(fog);
    if (!lease.Current()) return;
    if (!ResourceReadsAllowed() || sprite <= 0)
    {
        candidate.status = GuestCofResourceStatus::ResourceFailed;
        candidate.resource = fog;
        Failed(lease, candidate);
        return;
    }
    candidate.particles = PrepareGuestCofParticles({
        {reinterpret_cast<const char*>(particleScript.data()), particleScript.size()}, sprite});
    if (!lease.Current()) return;
    if (candidate.particles != AuroraResult::Applied)
    {
        candidate.status = GuestCofResourceStatus::ParticleFailed;
        Failed(lease, candidate);
        return;
    }
    candidate.studioAndRecoilPrepared = PrepareGuestCofEffects();
    if (!lease.Current()) return;
    if (state.decalsPrepared)
    {
        std::array<std::span<const std::uint8_t>, ShotDecals.size()> plan;
        for (std::size_t i = 0; i < plan.size(); ++i)
            plan[i] = {reinterpret_cast<const std::uint8_t*>(decalImages[i].data()), decalImages[i].size()};
        candidate.decalsPrepared = PrepareGuestCofDecalResources(plan);
        if (!lease.Current()) return;
        if (!candidate.decalsPrepared)
        {
            candidate.status = GuestCofResourceStatus::DecalFailed;
            Failed(lease, candidate);
            return;
        }
    }
    if (lease.Current()) state = candidate;
}
GuestCofResourceState GuestCofResourcesState() noexcept
{
    return Owner() ? state : GuestCofResourceState{};
}
}
