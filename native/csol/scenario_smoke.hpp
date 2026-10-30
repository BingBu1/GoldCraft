// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>

namespace csol::smoke
{
using Vec3 = std::array<float, 3>;
inline constexpr char Sprite[] = "sprites/gas_puff_01.spr";
inline constexpr int RenderMode = 4;
inline constexpr float Color = 5.0f / 255.0f;
inline constexpr std::size_t InitialParticles = 18;
inline constexpr std::size_t FillParticles = 19;

// The ordinary scenario createsmoke event (iparam2 == 1), new smoke renderer.
// Angles retain the particle roll; drawing supplies the camera pitch and yaw.
struct Particle
{
    Vec3 origin{}, previousOrigin{}, sourceOrigin{}, velocity{};
    Vec3 angles{0, 0, 1}, angularVelocity{};
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
// Call once per changed client time, including particles outside the view.
// previousTime is the preceding particle-manager time, not the birth time.
// Returns false after the original post-Think expiry check.
bool Tick(Particle& particle, double clientTime, float previousTime, Services& services);
}
