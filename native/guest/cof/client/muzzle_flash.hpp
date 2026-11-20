// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
struct cl_enginefuncs_s;
struct cl_entity_s;
struct event_args_s;
namespace guest::cof::client
{
// Original custom renderer light record; this is not the engine dlight_t.
struct PointLight
{
    std::array<float, 3> origin;
    float radius;
    std::array<float, 3> color;
    float die;
    float decay;
    std::int32_t key;
    std::array<std::byte, 84> rendererFields;
};
static_assert(sizeof(PointLight) == 124);
struct PointLightState
{
    std::array<PointLight, 32> lights{};
    float lastTime{};
};
PointLight& AllocatePointLight(PointLightState&, int key, float now) noexcept;
void AdvancePointLights(PointLightState&, float now) noexcept;
bool HasPointLights(const PointLightState&, float now) noexcept;
// Original VidInit clears the pool only, retaining the independent lastTime.
void ClearPointLights(PointLightState&) noexcept;

struct MuzzleLatch
{
    bool pending{};
    int type{};
    int owner{};
    std::array<float, 3> eventVector{};
};
enum class MuzzleResult { RequestsIssued, Ignored, Unsupported, InvalidInput, MissingBackend, BackendFailed, Cancelled };
struct MuzzleHost
{
    cl_enginefuncs_s* engine{};
    // Persistent storage mirrors the two original static cl_entity_t records.
    // It must outlive all engine references, through the frame/scene lifecycle.
    cl_entity_s* localVisibleEntity{};
    cl_entity_s* worldVisibleEntity{};
    void* context{};
    // Must represent an actual drawable custom pointlight backend. A pool-only
    // implementation must return false; the emitter then issues no requests.
    bool (*customPointLightsReady)(void*) noexcept {};
    // Called once per original allocation; owns its own GetClientTime call.
    PointLight* (*allocatePointLight)(void*, int key) noexcept {};
    // Host lifetime guard; optional only for standalone original replay.
    bool (*current)(void*) noexcept {};
};
// Host authenticates P345 ownership/model, owner thread, and original Studio
// stage. This component does not make the entire weapon or renderer Ready.
class P345Muzzle
{
public:
    explicit P345Muzzle(MuzzleHost host) noexcept : host_(host) {}
    MuzzleResult Local(const cl_entity_s& viewModel, unsigned attachment,
                       float scale, float cheapSmoke) const noexcept;
    // attachment is the original bone-transformed attachment at world Studio
    // drawing time, not a guessed origin or a saved network attachment.
    MuzzleResult World(const std::array<float, 3>& attachment, float scale) const noexcept;
    MuzzleResult Latch(MuzzleLatch&, const event_args_s&) const noexcept;
    // Original single latch clears before world emission. Session reset belongs
    // to the host adapter; no weapon/death timer is invented here.
    static bool Take(MuzzleLatch&, int currentEntityIndex) noexcept;
private:
    friend class GlockMuzzle;
    friend class RevolverMuzzle;
    friend class TMPMuzzle;
    friend class MP5Muzzle;
    friend class M16Muzzle;
    friend class G43Muzzle;
    friend class FamasMuzzle;
    friend class ShotgunMuzzle;
    MuzzleResult LocalWithSize(const cl_entity_s&,unsigned,float,float,float baseSize,
        const char* secondFlashModel = nullptr) const noexcept;
    MuzzleResult WorldWithSize(const std::array<float,3>&, float scale, float baseSize,
        bool repeatFlashLookup, const char* secondFlashModel = "models/flash1.mdl") const noexcept;
    bool Ready(bool cheapSmoke) const noexcept;
    bool Current() const noexcept;
    MuzzleResult Lights(const float* origin) const noexcept;
    MuzzleHost host_;
};
// Original21 selects base6.0 in the local generic emitter. Its world default
// stays base3.0 and has no type3 shell tail.
class RevolverMuzzle
{
public:
    explicit RevolverMuzzle(MuzzleHost host) noexcept : base_(host) {}
    MuzzleResult Local(const cl_entity_s& view,unsigned attachment,float scale,float cheapSmoke) const noexcept;
    MuzzleResult World(const std::array<float,3>& attachment,float scale) const noexcept
    {return base_.World(attachment,scale);}
private:
    P345Muzzle base_;
};
// Original14 uses local base4 and exact TMP P-model world base2.
// Both branches issue the original second flash1 model callback before RNG.
class TMPMuzzle
{
public:
    explicit TMPMuzzle(MuzzleHost host) noexcept : base_(host) {}
    MuzzleResult Local(const cl_entity_s&, unsigned attachment, float scale, float cheapSmoke) const noexcept;
    MuzzleResult World(const std::array<float,3>& attachment, float scale) const noexcept;
private:
    P345Muzzle base_;
};
// Original27 uses local flash2/base2; its cooperative borrowed TMP P selects
// world flash1/base2. Both paths preserve the second model callback before RNG.
class MP5Muzzle
{
public:
    explicit MP5Muzzle(MuzzleHost host) noexcept : base_(host) {}
    MuzzleResult Local(const cl_entity_s&, unsigned attachment, float scale, float cheapSmoke) const noexcept;
    MuzzleResult World(const std::array<float,3>& attachment, float scale) const noexcept;
private:
    P345Muzzle base_;
};
// Original9 selects local flash2/base6. Exact cooperative M16 P selects
// world flash2/base3; both retain the second model callback before RNG.
class M16Muzzle
{
public:
    explicit M16Muzzle(MuzzleHost host) noexcept : base_(host) {}
    MuzzleResult Local(const cl_entity_s&, unsigned attachment, float scale, float cheapSmoke) const noexcept;
    MuzzleResult World(const std::array<float,3>& attachment, float scale) const noexcept;
private:
    P345Muzzle base_;
};
// Original23 selects flash2 with base size 9 in the local emitter.
class FamasMuzzle
{
public:
    explicit FamasMuzzle(MuzzleHost host) noexcept : base_(host) {}
    MuzzleResult Local(const cl_entity_s& view, unsigned attachment, float scale, float cheapSmoke) const noexcept
    { return base_.LocalWithSize(view, attachment, scale, cheapSmoke, 9.F, "models/flash2.mdl"); }
private:
    P345Muzzle base_;
};
// Original26 uses local flash2/base6 and the default world flash1/base3.
class G43Muzzle
{
public:
    explicit G43Muzzle(MuzzleHost host) noexcept : base_(host) {}
    MuzzleResult Local(const cl_entity_s&, unsigned attachment, float scale, float cheapSmoke) const noexcept;
    MuzzleResult World(const std::array<float,3>& attachment, float scale) const noexcept
    {return base_.World(attachment,scale);}
private:
    P345Muzzle base_;
};
// Original4 uses local flash1/base6; exact cooperative shotgun P retains the
// default world flash1/base3 and does not execute the type3 shell tail.
class ShotgunMuzzle
{
public:
    explicit ShotgunMuzzle(MuzzleHost host) noexcept : base_(host) {}
    MuzzleResult Local(const cl_entity_s&, unsigned attachment, float scale, float cheapSmoke) const noexcept;
    MuzzleResult World(const std::array<float,3>& attachment, float scale) const noexcept
    { return base_.World(attachment, scale); }
private:
    P345Muzzle base_;
};
// Glock type3 has the same proved local default as P345, but a distinct world
// shell consumer. Its event vector is divided by20, never normalized.
class GlockMuzzle
{
public:
    explicit GlockMuzzle(MuzzleHost host) noexcept : base_(host), host_(host) {}
    MuzzleResult Local(const cl_entity_s& view, unsigned attachment, float scale, float cheapSmoke) const noexcept;
    MuzzleResult World(const std::array<float,3>& flashAttachment,
        const std::array<float,3>& shellAttachment, const std::array<float,3>& eventVector, float scale) const noexcept;
    // Original secondary-entity field path. The host playback-event adapter uses
    // explicit authoritative transport instead of treating modelindex as entity.
    MuzzleResult Latch(MuzzleLatch&, const event_args_s&) const noexcept;
private:
    P345Muzzle base_;
    MuzzleHost host_;
};
}
