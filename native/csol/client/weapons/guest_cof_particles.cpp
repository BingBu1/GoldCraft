// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "../user_message_chain.hpp"
#include "guest_cof_particles.hpp"
#include "../../../guest/cof/catalog.hpp"
#include "../../../guest/cof/particle_protocol.hpp"
#include "../../../include/goldcraft/connection_api.h"

#include <cmath>
#include <limits>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
using guest::cof::client::AuroraResult;
cl_enginefunc_t particleEngine{};
guest::cof::client::Aurora particles{&particleEngine};
const GoldCraftConnectionAPI* connection{};
DWORD ownerThread{};
std::uint64_t observedGeneration{}, preparationSerial{1};
int fogSprite{}, viewEntity{};
float drawTime{}; // Original transparent clock globals persist across HUD clears.
cvar_t *cheapSmoke{}, *particleLimit{};
bool ready{}, preparing{}, skipTransparent{};
AuroraResult status{AuroraResult::NotPrepared};
pfnUserMsgHook previousReset{};

bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
std::uint64_t Generation() noexcept { return connection ? connection->generation() : 0; }

void Retire(bool resetView = false) noexcept
{
    if (preparationSerial)
        preparationSerial = preparationSerial == std::numeric_limits<std::uint64_t>::max() ?
            0 : preparationSerial + 1;
    ready = false;
    fogSprite = 0;
    if (resetView) viewEntity = 0;
    particles.VideoInit();
    status = AuroraResult::NotPrepared;
}

bool Synchronize() noexcept
{
    if (!Owner()) return false;
    const auto generation = Generation();
    if (generation != observedGeneration)
    {
        Retire(true);
        observedGeneration = generation;
    }
    return generation != 0;
}

int PreparedSprite(const char* name)
{
    return name && std::string_view(name) == "sprites/fog2.spr" ? fogSprite : 0;
}

std::string_view ViewModel(std::uint8_t original) noexcept
{
    // The codec has authenticated the original identity; placement and resource
    // namespaces are host adaptations, not original CS inventory IDs.
    for (const auto& entry : guest::cof::Pistols)
        if (entry.originalId == original) return entry.viewModel;
    return {};
}

int ReceiveParticle(const char*, int size, void* data)
{
    if (!Synchronize() || !ready || !data || size < 0) return 1;
    guest::cof::ParticleMessageData value{};
    if (!guest::cof::DecodeParticle({static_cast<const std::byte*>(data), std::size_t(size)}, value))
    {
        status = AuroraResult::InvalidInput;
        return 1;
    }
    switch (value.operation)
    {
    case guest::cof::ParticleOperation::Add:
    {
        // Original unordered/nonzero comparison also skips creation for NaN.
        if (cheapSmoke->value != 0) return 1;
        const auto* model = engine.GetViewModel();
        if (!model) { status = AuroraResult::MissingBackend; return 1; }
        status = particles.Add(model->index, "aurora/gunsmoke.aur", 1,
            ViewModel(value.originalWeapon), 0, particleLimit->value);
        break;
    }
    case guest::cof::ParticleOperation::DeleteOwner:
        // An absent owner is a real original no-op; a match removes only the
        // first linked system, including an already empty one.
        particles.DeleteOwner(value.owner);
        status = AuroraResult::Applied;
        break;
    case guest::cof::ParticleOperation::KillLocal:
        particles.KillLocal(value.immediate);
        status = AuroraResult::Applied;
        break;
    }
    return 1;
}

int ReceiveViewEntity(const char*, int size, void* data)
{
    if (!Synchronize() || !data || size < 0) return 1;
    std::int16_t value{};
    if (guest::cof::DecodeViewEntity({static_cast<const std::byte*>(data), std::size_t(size)}, value))
        viewEntity = value;
    return 1;
}

int ResetHud(const char* name, int size, void* data)
{
    // Original CHud ResetHUD clears ViewEntity. Aurora's virtual Reset is empty:
    // no particle deletion, sprite invalidation or audio reset belongs here.
    if (Owner()) viewEntity = 0;
    return previousReset ? previousReset(name, size, data) : 1;
}

void Dedicated(const char* name, pfnUserMsgHook callback)
{
    const auto old = api->HookUserMsg(name, callback);
    if (!old) engine.pfnHookUserMsg(name, callback);
    else if (old != callback)
    {
        api->HookUserMsg(name, old);
        throw std::runtime_error("CoF particle message already has a consumer");
    }
}
}

void InitializeGuestCofParticles() { InitializeGuestCofParticles(GoldCraftConnectionInterface()); }

void InitializeGuestCofParticles(const GoldCraftConnectionAPI* provider)
{
    if (ownerThread && !Owner()) throw std::runtime_error("CoF particle initialization is on the wrong thread");
    ownerThread = GetCurrentThreadId();
    Retire(true);
    connection = provider && provider->size >= sizeof(GoldCraftConnectionAPI) &&
        provider->version == 1 && provider->generation ? provider : nullptr;
    observedGeneration = Generation();
    particleEngine = engine;
    particleEngine.pfnSPR_Load = PreparedSprite;
    cheapSmoke = engine.pfnGetCvarPointer("cl_cheapweaponsmoke");
    if (!cheapSmoke) cheapSmoke = engine.pfnRegisterVariable("cl_cheapweaponsmoke", "0", FCVAR_ARCHIVE);
    particleLimit = engine.pfnGetCvarPointer("cl_smokeparticlelimit");
    if (!particleLimit) particleLimit = engine.pfnRegisterVariable("cl_smokeparticlelimit", "5", FCVAR_ARCHIVE);
    Dedicated(guest::cof::ParticleMessage, ReceiveParticle);
    Dedicated(guest::cof::ViewEntityMessage, ReceiveViewEntity);
    InstallChainedUserMessage("ResetHUD", ResetHud, previousReset);
}

AuroraResult PrepareGuestCofParticles(const GuestCofParticlePlan& plan) noexcept
{
    if (!Owner() || preparing || !Synchronize() || !preparationSerial) return AuroraResult::NotPrepared;
    struct Preparation
    {
        Preparation() noexcept { preparing = true; }
        ~Preparation() { preparing = false; }
    } preparation;
    Retire();
    const auto serial = preparationSerial;
    const auto generation = observedGeneration;
    if (!cheapSmoke || !particleLimit || plan.fogSprite <= 0 || !engine.GetViewAngles || !engine.pfnAngleVectors)
        return status = AuroraResult::MissingBackend;
    fogSprite = plan.fogSprite;
    const auto result = particles.Prepare(plan.script);
    if (result != AuroraResult::Applied || serial != preparationSerial || generation != Generation())
    {
        Retire();
        return status = result == AuroraResult::Applied ? AuroraResult::NotPrepared : result;
    }
    ready = true;
    return status = AuroraResult::Applied;
}

bool GuestCofParticlesReady() noexcept { return Synchronize() && ready; }
bool KillGuestCofLocalParticles(bool immediate) noexcept
{
    if (!Synchronize()) return false;
    particles.KillLocal(immediate);
    return true;
}
AuroraResult GuestCofParticleStatus() noexcept { return status; }

void GuestCofParticlesVideoInit() noexcept { if (Owner()) Retire(true); }
void InvalidateGuestCofParticleResources() noexcept { if (Owner()) Retire(true); }

void GuestCofParticlesViewStage(const ref_params_s& params) noexcept
{
    if (Owner()) skipTransparent = params.nextView == 1;
}

void DrawGuestCofParticles(bool resourceReadsAllowed) noexcept
{
    if (!Owner() || skipTransparent || !engine.GetClientTime) return;
    const float previous = drawTime;
    drawTime = engine.GetClientTime();
    const float difference = drawTime - previous;
    const float positive = 0.0f > difference ? 0.0f : difference;
    const float elapsed = 0.5f < positive ? 0.5f : positive;
    if (!Synchronize() || !ready || !resourceReadsAllowed) return;
    const auto* local = engine.GetLocalPlayer();
    if (!local) { status = AuroraResult::MissingBackend; return; }
    if (viewEntity && viewEntity != local->index)
    {
        // This original branch feeds AngleVectors an uninitialized local angle.
        // The human's zero-init authorization did not include that camera bug.
        status = AuroraResult::Unsupported;
        return;
    }
    std::array<float, 3> angles{}, forward{}, right{}, up{};
    engine.GetViewAngles(angles.data());
    engine.pfnAngleVectors(angles.data(), forward.data(), right.data(), up.data());
    status = particles.Frame(elapsed, 0, right, up);
}

void ShutdownGuestCofParticles() noexcept
{
    if (ownerThread && !Owner()) return;
    Retire(true);
    observedGeneration = 0;
    connection = nullptr;
    ownerThread = 0;
}
}
