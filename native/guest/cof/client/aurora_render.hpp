// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
struct model_s;
struct triangleapi_s;
namespace guest::cof::client
{
struct AuroraDegreeTable
{
    std::array<float, 450> values{};
    bool prepared{};
};
// Cold preparation, never called from DrawAuroraParticle.
void PrepareAuroraDegreeTable(AuroraDegreeTable&) noexcept;
struct AuroraRenderParticle
{
    std::array<float, 3> position{};
    std::array<float, 4> color{};
    float size{}, angle{}, frame{};
};
struct AuroraRenderSeed
{
    // Unresolved is the safe host boundary, not an original default value.
    bool resolved{};
    int drawCondition{}, waterOnly{};
};
struct AuroraRenderFade { int state{}; float now{}, start{}, duration{}; };
enum class AuroraDrawResult
{
    Drawn, SizeZero, TextureUnavailable, UnresolvedSeed, UnsupportedControl,
    InvalidInput, MissingBackend, FrameRangeExceeded
};
// Explicit snapshot consumer only. No particle creation/position/control defaults.
// Caller supplies the original camera's right/up vectors and prepared real sprite.
AuroraDrawResult DrawAuroraParticle(AuroraRenderParticle&, const AuroraDegreeTable&,
    const AuroraRenderSeed&, const AuroraRenderFade&, const std::array<float,3>& right,
    const std::array<float,3>& up, const model_s* sprite, triangleapi_s* triangles) noexcept;
// Original manager restores normal mode once after all systems, including no draws.
bool FinishAuroraFrame(triangleapi_s*) noexcept;
}
