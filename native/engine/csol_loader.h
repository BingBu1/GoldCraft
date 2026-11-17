#pragma once
#include <cstddef>
#include <span>
#include <vector>
#include "../guest/cof/studio_sound.hpp"

// Called after CRC validation, with the actual loaded byte count. Decoding and
// complete safety preparation stay inside the same load boundary.
void GoldCraftDecodeAndLoadStudio(model_t* model,std::span<std::byte> bytes);
bool GoldCraftLoadCsolSprite(model_t* model, std::span<const std::byte> bytes);
void GoldCraftPrecacheCsolResources(const char* name,studiohdr_t* header);
void GoldCraftBeginCsolResources();
void GoldCraftEndCsolResources();
guest::cof::StudioSoundDependency GoldCraftStudioSoundResource(const char* model,int event,const char* sample);
void GoldCraftCsolSequenceDelta(delta_t* delta);
