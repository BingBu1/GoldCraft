// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace csol::cannon {
inline constexpr int Id = 144, Capacity = 20;
inline constexpr float Interval = 3.2f, EmptyInterval = .4f, Distance = 405.f, Angle = 33.f;
inline constexpr const char* EffectMessage = "CsoCannon";
inline constexpr int EffectBytes = 25; // version, owner, ducking, nine coordinates, command seed
using Point = std::array<float, 3>;

struct SmokeFrame { float width{}, alpha{}; int frame{}; };
inline SmokeFrame SampleSmoke(float age, int frames)
{
    if (!std::isfinite(age) || age < 0 || age >= 1.5f || frames <= 0) return {};
    // CSO client particle update: width is a world-space diameter; scale speed
    // uses 30 units/second. A zero fade speed means fade over the whole life.
    const float alpha = 250.f * (1.f - age / 1.5f);
    if (alpha < 1.f) return {};
    return {22.f + age * 60.f, alpha / 255.f, int(age * 30.f) % frames};
}

inline float RelativeAngle(float value)
{
    if (value >= 180) value -= 360;
    if (value <= -180) value += 360;
    return value;
}
inline Point Angles(Point delta)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    constexpr double pi = 3.14159265358979323846;
    const float horizontal = float(std::sqrt(double(delta[0]*delta[0]+delta[1]*delta[1])));
    // Match the engine's double atan2, then round once to the stored float.
    float yaw = horizontal ? float(std::atan2(double(delta[1]),double(delta[0])) * 180.0 / pi) : 0;
    float pitch = horizontal ? float(std::atan2(double(delta[2]),double(horizontal)) * 180.0 / pi) : delta[2] > 0 ? 90.f : 270.f;
    if (yaw < 0) yaw += 360;
    if (pitch < 0) pitch += 360;
    return {pitch, yaw, 0};
}
inline float Length(Point delta)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    return float(std::sqrt(double((delta[1]*delta[1]+delta[0]*delta[0])+delta[2]*delta[2])));
}
inline Point Normalize(Point delta)
{
    const float length=Length(delta);
    if(length==0)return {0,0,1};
    const float inverse=1.f/length;
    for(auto& value:delta)value*=inverse;
    return delta;
}
inline bool AnglePair(float pitch1, float yaw1, float pitch2, float yaw2)
{
    // Original MP 11421c3e: angular envelopes, including the asymmetric
    // vertical bound. Testing only the target centre loses edge hits.
    return !(yaw1 < -Angle && yaw2 < -Angle) && !(yaw1 > Angle && yaw2 > Angle) &&
        !(pitch1 < -(Angle - 5) && pitch2 < -(Angle - 5)) && !(pitch1 > Angle && pitch2 > Angle);
}
inline bool Intersects(Point source, Point forward, Point minimum, Point maximum)
{
    const auto aim = Angles(forward);
    // Preserve the four corner pairs used by the original implementation.
    // The last corner deliberately repeats min/max/min (MP 114218b9).
    const Point corners[]{minimum, maximum,
        {minimum[0],minimum[1],maximum[2]}, {minimum[0],maximum[1],minimum[2]},
        {minimum[0],maximum[1],maximum[2]}, {maximum[0],minimum[1],minimum[2]},
        {maximum[0],minimum[1],maximum[2]}, {minimum[0],maximum[1],minimum[2]}};
    for (int i=0; i<8; i+=2) {
        Point a{}, b{};
        for (int j=0; j<3; ++j) {a[j]=corners[i][j]-source[j]; b[j]=corners[i+1][j]-source[j];}
        a=Angles(Normalize(a)); b=Angles(Normalize(b));
        if (AnglePair(RelativeAngle(a[0]-aim[0]),RelativeAngle(a[1]-aim[1]),
            RelativeAngle(b[0]-aim[0]),RelativeAngle(b[1]-aim[1]))) return true;
    }
    return false;
}
inline float Damage(float base, float distance)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    // Fire converts the property to an integer before the area-damage call.
    // Keep MP's separate mulss/subss: FMA can turn exact zero at max range
    // into a tiny negative value, which the original lower bound maps to 1.
    base = float(int(base));
    const float value=base-distance*(base/Distance);
    return value<0 ? 1.f : value;
}
inline int AddAmmo(int& clip, int count)
{
    const int added=std::clamp(count,0,std::max(0,Capacity-clip));
    clip+=added; return added;
}
}
