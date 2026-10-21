#pragma once
#include <cstddef>
#include <span>
#include <vector>

// Called after CRC validation, with the actual loaded byte count.
std::vector<std::byte> GoldCraftDecodeStudio(std::span<std::byte> bytes, const char* name);
bool GoldCraftLoadCsolSprite(model_t* model, std::span<const std::byte> bytes);
void GoldCraftPrecacheCsolResources(const char* name,studiohdr_t* header);
void GoldCraftCsolSequenceDelta(delta_t* delta);
