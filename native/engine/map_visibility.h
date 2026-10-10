#pragma once
#include "goldcraft/world_carving.hpp"
#include <memory>

struct model_s;
struct mleaf_s;
struct edict_s;
namespace goldcraft::engine_map {
struct Visibility;
using VisibilityCandidate = std::shared_ptr<Visibility>;
VisibilityCandidate prepare_visibility(model_s *world, std::span<const carving::Box> cuts);
void commit_visibility(VisibilityCandidate candidate) noexcept;
} // namespace goldcraft::engine_map

void GoldCraft_MapBeginVisibility(const float *eye, unsigned char *mask, int bytes) noexcept;
bool GoldCraft_MapMergeVisibility(const mleaf_s *leaf, unsigned char *mask, int bytes) noexcept;
bool GoldCraft_MapEntityVisible(const edict_s *entity, const unsigned char *mask) noexcept;
