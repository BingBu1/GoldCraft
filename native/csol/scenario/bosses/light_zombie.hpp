// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Numeric stages of CSO CLightZombieBoss (31500; MP 39f962f3..., factory
// 113E5955 -> ctor 10983E90, vtable 11806968) and its lightzb_bomb.
// Evidence: docs/2026-11-08_分析-僵尸少女31500原版取证-report.md and
// docs/2026-11-08_实现-僵尸少女纯逻辑层-report.md. Pure float32 code: the host
// supplies entities, team/alive gates, engine random values and callbacks.
// The skill tree itself uses behavior_tree.hpp; skill selection reuses
// BossSkills (10A28760 matches it once the empty forced queue is removed).

#include "oberon_combat.hpp"
#include "zombi_boss.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

#pragma clang fp contract(off)

namespace csol::scenario::light_zombie
{
using Vec3 = std::array<float, 3>;

inline constexpr std::int32_t TypeId = 31500;
inline constexpr std::int16_t SkillKey = 1003;

// SK 1003 row types (ZBS_SK_zs_nightmare_lightzombie.cso) and the leaf each
// one selects in the 10983E90 SkillSelector.
enum Skill : std::int16_t
{
    SpawnEvent = 321, Attack1 = 322, Attack2 = 323, Attack3 = 324, NukeBomb = 325,
    NormalBomb1 = 326, NormalBomb2 = 327, CrazyBomb = 328, BombJump = 329, Hide = 330,
};

// Original fixed tunables: static initializers via 106DB260 with an empty
// std::function, never written afterwards (value pointer in parentheses).
inline constexpr float HideTargetAmount = 0.0f;      // 11EF6678
inline constexpr float HideFadeInSeconds = 3.0f;     // 11EF65A0
inline constexpr float CrazyFanDegrees = 180.0f;     // 11EF6B80
inline constexpr float CrazyLift = 1.0f;             // 11EF6758
inline constexpr float CrazySpeedLow = 500.0f;       // 11EF6B30
inline constexpr float CrazySpeedHigh = 1000.0f;     // 11EF6930
inline constexpr float NormalBomb1Lift = 0.5f;       // 11EF69C0
inline constexpr float NormalBomb2Lift = 0.5f;       // 11EF6480
inline constexpr float JumpRelocateMinimum = 1000.0f; // 11EF6A98
inline constexpr float JumpRelocateFan = 180.0f;     // 11EF6890
inline constexpr float JumpChancePercent = 75.0f;    // 11EF68E8
inline constexpr float JumpTurnInterval = 0.5f;      // 11EF6840
inline constexpr float NukeHeight = 1500.0f;         // 11EF67A8
inline constexpr float BombHealth = 1500.0f;         // 11EF6978
inline constexpr float BombMaxSpeed = 800.0f;        // 11EF63A8
inline constexpr float BombBounceSeconds = 2.0f;     // 11EF6630
inline constexpr float BombLifeSeconds = 10.0f;      // 11EF6AE8
inline constexpr float BombBounceDamping = 0.8f;     // 11EF6360
inline constexpr float BombBlastRadius = 200.0f;     // 11EF65E8
inline constexpr float BombBlastDamage = 300.0f;     // 11EF6A48
inline constexpr float BombBlastKickSide = 600.0f;   // 11EF6510
inline constexpr float BombBlastKickUp = 600.0f;     // 11EF63F0
inline constexpr float LandingKickSide = 600.0f;     // 11EF6558 / 11EF67F8
inline constexpr float LandingKickUp = 600.0f;       // 11EF6438 / 11EF64C8
inline constexpr float SelectionInterval = 2.0f;     // 10A28798 (+0x1DD4)
inline constexpr float FadeTick = 0.03333000093698502f; // 118073A8 (0x3D08850A)

inline constexpr char BombModel[] = "models/monster/lightzombieboss_bomb.mdl";
inline constexpr char NukeModel[] = "models/monster/lightzombieboss_nukebomb.mdl";
inline constexpr char NukeShadowModel[] = "models/monster/lightzombieboss_nukebombshadow.mdl";
inline constexpr char BombSprite[] = "sprites/ef_lightzombiebossbomb.spr";
inline constexpr char BombExplodeSound[] = "zombi/lightzombieboss/zombieboss_bomb/explode.wav";
inline constexpr char NukeExplodeSound[] = "zombi/lightzombieboss/zombieboss_bomb/explode2.wav";
inline constexpr char BombMoveSound[] = "zombi/lightzombieboss/zombieboss_bomb/move.wav";
inline constexpr std::array<const char*, 3> BombThrowSounds{
    "zombi/lightzombieboss/zombieboss_bomb/Throw1.wav",
    "zombi/lightzombieboss/zombieboss_bomb/Throw2.wav",
    "zombi/lightzombieboss/zombieboss_bomb/Throw3.wav"};

// 10985420 (vt1E0): type 330 is not selectable while rendermode == 4.
constexpr bool RowAllowed(std::int16_t type, int rendermode)
{
    return type != Hide || rendermode != 4;
}

// 11530220: one-sided wrap. Negative adds 360 until >= 0; otherwise subtracts
// 360 while strictly above 360 (360 itself stays).
inline float AngleMod(float degrees)
{
    if (0.0f > degrees)
    {
        do degrees = degrees + 360.0f; while (0.0f > degrees);
        return degrees;
    }
    while (degrees > 360.0f) degrees = degrees - 360.0f;
    return degrees;
}

// cvttss2si: out-of-range and unordered values become INT_MIN.
inline std::int32_t Truncate(float value)
{
    if (!(value > -2147483904.0f && value < 2147483648.0f)) return INT32_MIN;
    return static_cast<std::int32_t>(value);
}

// x87 under the Windows 53-bit precision control word: a float loaded with
// fld, then fadd/fmul of a float memory operand, stored with fstp dword.
inline float X87Add(float st0, float memory) { return static_cast<float>(double(st0) + double(memory)); }

// 109858F0 (SpawnEvent destination): first func_buyzone's vt280 point, else
// this constant.
inline constexpr Vec3 NoBuyzoneDestination{464.0f, 0.0f, 336.0f};

// ---------- Hide (330) ----------
// 10986760 event 0: duration = row+10 damage, but at least 0.1 (an unordered
// compare also selects 0.1); per-tick step (255 - 0) / (duration / 0.03333).
inline float HideStep(float damage)
{
    const float duration = damage > 0.1f ? damage : 0.1f;
    return (255.0f - HideTargetAmount) / (duration / FadeTick);
}

struct HideFade { float amount; bool finished; };
// 10987940 Tick while rendermode == 4: subtract a positive hide_value, then
// finish once 0 >= amount (amount becomes 0; renderfx 0; reappear time,
// timeline (0, 1001) and hide_value -1 are host steps).
inline HideFade HideFadeOut(float amount, float hideValue)
{
    if (hideValue > 0.0f) amount = amount - hideValue;
    if (HideTargetAmount >= amount) return {HideTargetAmount, true};
    return {amount, false};
}
// 10987A91..AAE: improv+1E88 = row+44 rushPower + now.
inline float ReappearTime(float rushPower, float now) { return rushPower + now; }
// 109887B0 after 10A29100, while rendermode == 4: skipped when reappear is
// exactly -1; runs when now > reappear. Adds (255 - 0) / (3.0 / 0.03333);
// renderfx 22; done (vt130 render restore, reappear = -1) once >= 255.
inline bool FadeInDue(float reappear, float now) { return reappear != -1.0f && now > reappear; }
inline HideFade HideFadeIn(float amount)
{
    const float step = (255.0f - HideTargetAmount) / (HideFadeInSeconds / FadeTick);
    amount = step + amount;
    return {amount, amount >= 255.0f};
}

// ---------- bomb throws ----------
// 10986850 / 10986990 (NormalBomb1/2 event 1000): gpGlobals->v_forward after
// UTIL_MakeVectors(pev->angles) plus (0, 0, 0.5), fast-normalized (114BD500).
inline Vec3 NormalThrowDirection(const Vec3& forward, float lift)
{
    return zombi_boss::FastNormalize({forward[0] + 0.0f, forward[1] + 0.0f, forward[2] + lift});
}

// 10987630 (CrazyBomb Tick while sequence 22). fire_rhand != 0 uses
// attachment 1 and a -90 fan, else attachment 2 and +90.
inline int CrazyAttachment(bool rightHand) { return rightHand ? 1 : 2; }
inline float CrazyFanHigh(bool rightHand)
{
    float high = CrazyFanDegrees * 0.5f;
    if (rightHand) high = high * -1.0f;
    return high;
}
// yawOffset = RANDOM_FLOAT(0, CrazyFanHigh) from the engine; then x87 add to
// pev->angles.y, 11530220, yaw-only forward (11530060) + (0, 0, 1.0).
inline Vec3 CrazyThrowDirection(float yawOffset, float yaw)
{
    const float angle = AngleMod(X87Add(yawOffset, yaw));
    const auto forward = oberon::Forward(angle);
    return zombi_boss::FastNormalize({forward[0] + 0.0f, forward[1] + 0.0f, 0.0f + CrazyLift});
}
// Speed = RANDOM_FLOAT(500, 1000); next fire = row+0C damageDelay + now.
inline float CrazyNextFire(float damageDelay, float now) { return damageDelay + now; }

// 10987BB0: velocity = speed * direction.
inline Vec3 ThrowVelocity(const Vec3& direction, float speed)
{
    return {speed * direction[0], speed * direction[1], speed * direction[2]};
}

// ---------- BombJump relocation (109859A0) ----------
// float(sqrt(double(dy*dy + dx*dx))), y term first.
inline float PlanarDistance(const Vec3& from, const Vec3& to)
{
    const float dy = to[1] - from[1], dx = to[0] - from[0];
    const float square = dy * dy + dx * dx;
    return static_cast<float>(std::sqrt(static_cast<double>(square)));
}

struct NavExtent { Vec3 lo, hi, center; }; // CNavArea +08/+14/+20

// origin and target are vt128 points; target already passed through the
// owner's vt6C adjustment. Distance: while jumping (vtA0), to the current
// jump destination (+1BE4); else max(1000, planar distance to target) -
// 1000 > d selects 1000, otherwise (including unordered) d.
inline float RelocationDistance(bool jumping, const Vec3& origin, const Vec3& jumpDestination,
    const Vec3& target)
{
    if (jumping) return PlanarDistance(origin, jumpDestination);
    const float distance = PlanarDistance(origin, target);
    return JumpRelocateMinimum > distance ? JumpRelocateMinimum : distance;
}
// yawOffset = RANDOM_FLOAT(-90, 90). Result xy = forward * distance + origin,
// z = target z. When area = GetNavArea(target, 120) exists and the result is
// outside its 3D extent (inclusive; unordered counts as outside), xy becomes
// target + FastNormalize(area.center - origin) * distance.
inline Vec3 RelocationPoint(const Vec3& origin, const Vec3& target, float distance,
    float yawOffset, float yaw, const std::optional<NavExtent>& area)
{
    const auto forward = oberon::Forward(AngleMod(X87Add(yawOffset, yaw)));
    Vec3 result{forward[0] * distance + origin[0], forward[1] * distance + origin[1], target[2]};
    if (!area) return result;
    const auto inside = [&](int i) { return result[i] >= area->lo[i] && area->hi[i] >= result[i]; };
    if (inside(0) && inside(1) && inside(2)) return result;
    const auto direction = zombi_boss::FastNormalize({area->center[0] - origin[0],
        area->center[1] - origin[1], area->center[2] - origin[2]});
    return {direction[0] * distance + target[0], direction[1] * distance + target[1], target[2]};
}

// ---------- jump flight (10A29160 vtA0 branch, 10A291F7..10A292F4) ----------
struct JumpFlight { Vec3 velocity; float progress; bool landed; };
// center = owner vt84 point. velocity = (t*dir.xy + (1-t)*0, t*dir.z + (1-t))
// * speed (+1E78); t' = min(step + t, 1) (unordered selects 1); landed when
// t' == 1 and threshold (+1E84) >= -(dest.z - center.z) before normalizing.
inline JumpFlight JumpStep(const Vec3& destination, const Vec3& center, float progress,
    float step, float speed, float threshold)
{
    const Vec3 delta{destination[0] - center[0], destination[1] - center[1], destination[2] - center[2]};
    const float drop = -delta[2];
    const auto dir = zombi_boss::FastNormalize(delta);
    const float rest = 1.0f - progress;
    const float flat = rest * 0.0f;
    JumpFlight out{};
    out.velocity = {(progress * dir[0] + flat) * speed, (progress * dir[1] + flat) * speed,
        (progress * dir[2] + rest) * speed};
    const float next = step + progress;
    out.progress = next < 1.0f ? next : 1.0f;
    out.landed = out.progress == 1.0f && threshold >= drop;
    return out;
}

// ---------- lightzb_bomb ----------
// 10984D00: horizontal speed squared x*x + y*y > 400 -> 2, > 36 -> 1, else 0.
inline int BombSequence(float vx, float vy)
{
    const float square = vx * vx + vy * vy;
    if (square > 400.0f) return 2;
    if (square > 36.0f) return 1;
    return 0;
}

// 10988900 per 0.0333 tick: turn toward target at 300 deg/s, accelerate 800.
struct BombSteer { bool turned; float yaw; Vec3 velocity; };
inline BombSteer BombTrack(float dt, float yaw, const Vec3& turnOrigin, const Vec3& origin,
    const Vec3& velocity, float maxSpeed, const Vec3& target)
{
    BombSteer out{false, yaw, velocity};
    const float dy = target[1] - turnOrigin[1], dx = target[0] - turnOrigin[0];
    const float length = static_cast<float>(std::sqrt(static_cast<double>(dy * dy + dx * dx)));
    float nx = 1.0f, ny = 0.0f;
    if (length > 0.0f) { nx = dx / length; ny = dy / length; }
    const auto forward = oberon::Forward(yaw); // cos (1133C2F0), sin (1133C3F0)
    const float cross = (-forward[1]) * nx + forward[0] * ny;
    const float dot = forward[0] * nx + ny * forward[1];
    float turn = cross;
    bool apply = true;
    if (0.0f > dot) turn = 0.0f > cross ? -1.0f : 1.0f;
    else if (0.05f > std::fabs(cross)) apply = false;
    if (apply)
    {
        out.turned = true;
        out.yaw = (dt * 300.0f) * turn + yaw; // angles.x and angles.z are zeroed
    }
    Vec3 pull{target[0] - origin[0], target[1] - origin[1], 0.0f};
    if (!(pull[0] > -0.01f && 0.01f > pull[0] && pull[1] > -0.01f && 0.01f > pull[1]))
        pull = zombi_boss::FastNormalize(pull);
    const float accel = dt * 800.0f;
    Vec3 v{accel * pull[0] + velocity[0], accel * pull[1] + velocity[1], velocity[2]};
    const float square = (v[0] * v[0] + v[1] * v[1]) + v[2] * v[2];
    const float speed = static_cast<float>(std::sqrt(static_cast<double>(square)));
    Vec3 dir{0.0f, 0.0f, 1.0f};
    if (speed > 0.0f) dir = {v[0] / speed, v[1] / speed, v[2] / speed};
    const float clamped = maxSpeed < speed ? maxSpeed : speed;
    out.velocity = {clamped * dir[0], clamped * dir[1], clamped * dir[2]};
    return out;
}

// 10984A60 Bomb_Attack gate after the host's cooldown/row/valid/alive/team
// checks (skip while nextAttack > now). row is the boss row captured at throw
// (+D8). bombBaseZ = pev+F4 + origin.z. Facing uses x87: cos(yaw)*(dx*r)
// computed in double, plus float (dy*r)*sin(yaw).
inline bool BombCanHit(const BossSkillRow& row, float now, float nextAttack, const Vec3& bomb,
    float bombMinsZ, float yaw, const Vec3& target)
{
    if (nextAttack > now) return false;
    const float dy = target[1] - bomb[1], dx = target[0] - bomb[0];
    const float square = dy * dy + dx * dx;
    if (square > row.damageDistance * row.damageDistance) return false;
    const float base = bombMinsZ + bomb[2];
    if (target[2] > (base + row.damageHeight) + 36.0f) return false;
    if ((base - 118.11000061035156f) - 36.0f > target[2]) return false;
    const float inverse = oberon::InverseLength(square);
    const auto forward = oberon::Forward(yaw);
    const float xTerm = dx * inverse;
    const float yTerm = (dy * inverse) * forward[1];
    const float facing = static_cast<float>(double(forward[0]) * double(xTerm) + double(yTerm));
    return !(row.damageCosine > facing);
}
// Hit: damage row+10 (DMG 2), next attack = row+20 forceAttackTime + now.
inline float BombNextAttack(const BossSkillRow& row, float now) { return row.forceAttackTime + now; }

// 1152D8A0: linear radius falloff with the 114BBE90 inverse length.
struct RadiusHit { float damage; Vec3 direction; };
inline RadiusHit RadiusDamage(const Vec3& source, const Vec3& point, float damage, float radius)
{
    const float dy = point[1] - source[1], dx = point[0] - source[0], dz = point[2] - source[2];
    const float square = (dy * dy + dx * dx) + dz * dz;
    if (!(radius * radius > square)) return {0.0f, {0.0f, 0.0f, 0.0f}};
    const float inverse = oberon::InverseLength(square);
    const float ratio = (radius - 1.0f / inverse) / radius;
    const float scale = 0.0f > ratio ? 0.0f : ratio;
    return {scale * damage, {dx * inverse, dy * inverse, dz * inverse}};
}

// 109885A0 mode-15 reward for a player hitting a bomb (not a "grenade"
// inflictor): P22..P26 are difficulty parameters (10823570, float via fld).
// The host draws money = RANDOM_FLOAT(low, high) from this range, then passes
// the engine's float to BombHitReward. item353 is player+20AE; option1C4 is
// the room option (800 disables the 1.5 multiplier).
struct BombRewardRange { float low, high; };
inline BombRewardRange BombRewardMoneyRange(float p25, float p26, float damage)
{
    return {static_cast<float>(double(p26) * double(damage)), p25};
}
struct BombReward { std::int32_t points; std::int32_t money; };
inline BombReward BombHitReward(float p22, float p23, float p24, float damage,
    float randomMoney, bool item353, std::int32_t option1C4)
{
    const float scaled = static_cast<float>(double(p24) * double(damage));
    const float sum = static_cast<float>(Truncate(X87Add(p22, scaled)));
    const std::int32_t points = Truncate(sum < p23 ? sum : p23);
    std::int32_t money = Truncate(randomMoney);
    if (item353 && option1C4 != 800) money = Truncate(static_cast<float>(money) * 1.5f);
    return {points, money};
}
}
