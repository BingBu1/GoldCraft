// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "muzzle_flash.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
struct model_s;
struct cl_entity_s;
namespace guest::cof::client
{
// These are original gamma cvars cached by 1005A110, not host display gamma.
struct PointLightGamma { float exponent, brightness, contrast; };
struct PointLightBasis
{
    int first{};
    std::array<float, 3> normal{}, tangent{}, bitangent{};
};
static_assert(sizeof(PointLightBasis) == 40);
float PointLightColor(float input, PointLightGamma) noexcept;
std::array<float, 16> PointLightMatrix(const PointLightBasis&, const std::array<float, 3>&) noexcept;
std::array<float, 4> PointLightPlane(unsigned axis, const PointLight&) noexcept;
// Cold, deterministic resource builders. Output spans must have exact sizes.
void BuildPointLightAttenuation1D(std::span<std::uint8_t, 128>) noexcept;
void BuildPointLightAttenuation2D(std::span<std::uint8_t, 128 * 128>) noexcept;
void BuildPointLightAttenuation3D(std::span<std::uint8_t, 32 * 32 * 32 * 3>) noexcept;
void BuildPointLightCube(std::span<std::uint8_t, 6 * 32 * 32 * 3>) noexcept;

enum class PointLightStage { Unsupported, OpaqueBeforeTransparency };
enum class PointLightSpace { Unsupported, OriginalGamma };
// Same-frame host brush submission. The model/matrix and resolved angle basis
// come from the actual opaque draw, never from a later network interpolation.
struct PointLightBrush
{
    const model_s* model{};
    std::array<float,16> modelToWorld{};
    std::array<float,3> origin{}, forward{}, right{}, up{}, localView{};
    int renderfx{};
    bool rotated{};
};
// Original1005ADA0: subtract first, then the three ordered dot products.
std::array<float,3> PointLightBrushOrigin(const PointLightBrush&,
                                        const std::array<float,3>&) noexcept;
struct PointLightFrame
{
    PointLightStage stage{};
    PointLightSpace space{};
    // Host must establish real opaque depth, projection/modelview/depth-range
    // correspondence and visibility stamps; these are not self-certification.
    bool matchingOpaqueDepth{}, visibilityEstablished{}, brushLifecycleEstablished{};
    std::int32_t nodeVisframe{}, surfaceVisframe{};
    // Main1005B300 only: transformed/portal brushes have a separate original
    // light stage. Their presence refuses readiness until that adapter exists.
    std::span<cl_entity_s* const> visibleBrushes{};
    // Authenticated opaque submissions replace the legacy entity/stamp path.
    // Nonempty snapshots require the host lifecycle proof above.
    std::span<const PointLightBrush> drawnBrushes{};
    float time{}, glBmodels{}, forceTwoPass{}, disableNvCombiners{};
    PointLightGamma gamma{};
    // Actual host PVS membership, indexed by model global index. Prefer these
    // read-only masks to engine stamps when the host owns its visibility cache.
    std::span<const std::uint64_t> visibleSurfaceWords{}, visibleNodeWords{};
    bool authenticatedMatrices{};
    std::array<float,16> worldMatrix{},projectionMatrix{};
};
enum class PointLightDrawResult
{
    Drawn, NoLights, MissingResources, UnsupportedScene, UnsupportedLight,
    UnsupportedBrushLifecycle, CapacityExceeded
};
// Standalone real compatibility-GL backend. No engine dlight allocation, scene
// callback registration, extension spoof, or pool-only readiness lives here.
// PrepareMap/InitializeGL are cold owner-thread calls, never draw-time work.
class PointLightRenderer
{
public:
    PointLightRenderer();
    ~PointLightRenderer();
    PointLightRenderer(const PointLightRenderer&) = delete;
    PointLightRenderer& operator=(const PointLightRenderer&) = delete;
    bool PrepareMap(model_s&, bool hl25, std::size_t maximumSurfaceDraws);
    void ClearMap() noexcept;
    bool InitializeGL() noexcept;
    // Must run before destroying the owning GL context. Wrong-thread/context
    // release refuses and retains ownership; context loss uses AbandonGL only.
    bool ReleaseGL() noexcept;
    void AbandonGL() noexcept;
    bool Ready(const PointLightFrame&) const noexcept;
    PointLightDrawResult Draw(const PointLightFrame&, const PointLightState&) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
