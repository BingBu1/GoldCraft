#pragma once
#include <cstddef>
#include <span>

// Called after CRC validation, with the actual loaded byte count.
void GoldCraftDecodeStudio(std::span<std::byte> bytes, const char* name);
bool GoldCraftLoadCsolSprite(model_t* model, std::span<const std::byte> bytes);
void GoldCraftPrecacheCsolResources(const char* name,studiohdr_t* header);
