// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../scenario/custom/body_stains.hpp"

struct cl_entity_s;
struct entity_state_s;
struct clientdata_s;
struct model_s;
struct MetaRendererStudioStain;
namespace goldcraft::studio_stain { struct Contact; }

namespace csol::client
{
struct BodyStainSurface
{
    unsigned body{}, weapon{};
    scenario::custom::BodyStainSurface surface{};
    explicit operator bool() const noexcept { return body || weapon; }
};
class BodyStainViewScope
{
public:
    explicit BodyStainViewScope(const cl_entity_s* entity) noexcept;
    ~BodyStainViewScope();
    BodyStainViewScope(const BodyStainViewScope&) = delete;
    BodyStainViewScope& operator=(const BodyStainViewScope&) = delete;

private:
    const model_s* previous_;
};
void InitializeBodyStains() noexcept;
void ResetBodyStains() noexcept;
void ResetMonsterStains() noexcept;
bool ConsumeMonsterStains(int size, const void* payload) noexcept;
unsigned ProjectMonsterStains(const cl_entity_s* entity, const model_s* model, const float (*bones)[3][4],
    unsigned boneCount, goldcraft::studio_stain::Contact* output) noexcept;
void ShutdownBodyStains() noexcept;
void ReadLocalBodyStain(const entity_state_s* state, const clientdata_s* client) noexcept;
void BodyStainsFrame() noexcept;
BodyStainSurface BodyStainForModel(const cl_entity_s* entity, const model_s* model) noexcept;
bool BodyStainMaterial(BodyStainSurface surface, unsigned material, unsigned armMaterial, MetaRendererStudioStain& output) noexcept;
}
