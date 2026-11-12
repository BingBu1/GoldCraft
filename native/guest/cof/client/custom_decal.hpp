// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
struct model_s;
namespace guest::cof::client
{
using DecalVector = std::array<float, 3>;
struct CustomDecalBasis { DecalVector s{}, t{}; };
CustomDecalBasis MakeCustomDecalBasis(const DecalVector&) noexcept;
DecalVector CustomDecalIntersection(const DecalVector& a, const DecalVector& b,
    const DecalVector& normal, const DecalVector& plane) noexcept;
// Original convex BSP polygon clip, including cyclic vertex order and strict
// positive-side test. Empty output also represents insufficient output capacity.
std::size_t ClipCustomDecal(std::span<const DecalVector>, const DecalVector& normal,
    const DecalVector& plane, std::span<DecalVector>) noexcept;
// Seven RGB levels: 64,32,16,8,4,2,1. Cold resource preparation only.
using ShotDecalTexture = std::array<std::uint8_t, 16383>;
bool BuildShotDecalTexture(std::span<const std::uint8_t> tga, ShotDecalTexture&) noexcept;
struct CustomDecalRandom
{
    void* context{};
    int (*range)(void*, int minimum, int maximum){};
};
struct CustomDecalVertex { DecalVector position{}; std::array<float, 2> uv{}; };
struct CustomDecalView
{
    std::span<const CustomDecalVertex> vertices{};
    DecalVector origin{}, normal{};
    std::size_t surface{};
    unsigned variant{};
};
enum class CustomDecalResult
{
    Added, Queued, Disabled, NoSurface, Malformed, Unsupported, MissingRandom, QueueFull
};
struct CustomDecalFrame
{
    // The scene owner must establish these against its real depth/visibility
    // lifecycle, before transparency, in the original multiplicative color space.
    bool matchingOpaqueDepth{}, visibilityEstablished{}, originalColorSpace{};
    std::int32_t surfaceVisframe{};
    // Optional host PVS bitset, indexed by actual world model surface index.
    // Renderer maintains this directly; it need not mutate engine visframe.
    std::span<const std::uint64_t> visibleSurfaceWords{};
};
enum class CustomDecalDrawResult { Drawn, Empty, Disabled, MissingResources, UnsupportedScene };
// Dynamic "shot" only: original client 10059450 -> 10057DA0, 100586C0.
// Resource/geometry preparation is cold; Receive/Draw perform no I/O/allocation.
class CustomDecals
{
public:
    CustomDecals();
    ~CustomDecals();
    CustomDecals(const CustomDecals&) = delete;
    CustomDecals& operator=(const CustomDecals&) = delete;
    bool PrepareMap(model_s&, bool hl25);
    // Call before freeing/replacing the world. Host session isolation also
    // drops pending messages; ClearMarks alone follows original 100586A0.
    void ClearMap() noexcept;
    void ClearMarks() noexcept;
    CustomDecalResult Receive(std::span<const std::uint8_t>, float enabled, CustomDecalRandom) noexcept;
    void FlushPending(float enabled, CustomDecalRandom) noexcept;
    std::size_t Count() const noexcept;
    std::size_t Pending() const noexcept;
    CustomDecalView Inspect(std::size_t slot) const noexcept;
    // Supply actual shot1..shot5 TGA bytes after resource preparation. Owns GL
    // textures; call on the render thread, outside draw/message callbacks.
    bool InitializeGL(const std::array<std::span<const std::uint8_t>, 5>&);
    bool ReleaseGL() noexcept;
    void AbandonGL() noexcept;
    CustomDecalDrawResult Draw(const CustomDecalFrame&, float enabled) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
