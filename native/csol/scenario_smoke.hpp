// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>

namespace csol::smoke
{
using Vec3 = std::array<float, 3>;
inline constexpr char Sprite[] = "sprites/gas_puff_01.spr";
// Stock smoke grenades also send iparam2=1/4. A separate event keeps their
// original client callback and prediction indices intact in every mode.
inline constexpr char EventPath[] = "events/csol_scenario_smoke.sc";
inline constexpr int RenderMode = 4;
inline constexpr float Color = 5.0f / 255.0f;
inline constexpr std::size_t InitialParticles = 18;
inline constexpr std::size_t FillParticles = 19;
inline constexpr std::size_t CharacterParticles = 17;

// The ordinary scenario createsmoke event (iparam2 == 1), new smoke renderer.
// Angles retain the particle roll; drawing supplies the camera pitch and yaw.
struct Particle
{
    Vec3 origin{}, previousOrigin{}, sourceOrigin{}, velocity{};
    Vec3 angles{0, 0, 1}, angularVelocity{};
    // Final RGB for Color4f. Type 1 retains its original dark grey.
    Vec3 color{Color, Color, Color};
    float size{}, initialSize{}, brightness{190}, birth{}, die{}, deadline{};
    float expansion{0.1f}, mass{}, sourceBrightness{190};
    int frame{};
    bool stationary{}, emitFill{}, started{}, collide{true};
};

struct Trace
{
    float fraction{1};
    Vec3 end{}, normal{};
};

class Services
{
public:
    virtual ~Services() = default;
    virtual float RandomFloat(float low, float high) = 0;
    virtual int RandomInt(int low, int high) = 0;
    virtual void AngleVectors(float yaw, Vec3& forward, Vec3& up) = 0;
    // EV_SetTraceHull(2); EV_PlayerTrace(from, to, 0xA, -1, &trace).
    virtual Trace TraceWorld(const Vec3& from, const Vec3& to) = 0;
    // Copy into stable storage. Emission during Tick must not invalidate its
    // Particle reference, or update the new entries again in the same pass.
    virtual void Emit(const Particle& particle) = 0;
};

void CreateCloud(const Vec3& origin, double clientTime, Services& services);
// Original createsmoke type 10 (character skills 22/23). Color is the event's
// 0..255 RGB, life/brightness are fparam1/2 and radius is iparam1. The caller
// validates finite event parameters before entering either cloud generator.
void CreateCharacterCloud(const Vec3& origin, const Vec3& color, float life,
    float brightness, int radius, double clientTime, Services& services);
// Call once per changed client time, including particles outside the view.
// previousTime is the preceding particle-manager time, not the birth time.
// Returns false after the original post-Think expiry check.
bool Tick(Particle& particle, double clientTime, float previousTime, Services& services);
}
