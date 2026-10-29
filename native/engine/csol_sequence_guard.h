// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <span>

void GoldCraftLoadStudioOwned(model_t* model,std::span<std::byte> bytes);
void GoldCraftStudioAllocating(cache_user_t* owner,int bytes);
void GoldCraftStudioMemoryReset();
bool GoldCraftStudioTagged(model_t* model,const studiohdr_t* header);
void GoldCraftStudioSelected(model_t* model,const studiohdr_t* header,const mstudioseqdesc_t* descriptor);
void GoldCraftStudioSample(model_t* model,const studiohdr_t* header,const mstudioseqdesc_t* descriptor,
    const mstudioanim_t* animation,float integerFrame,float fraction,bool wholeBlend);
void GoldCraftCheckStudioBounds(std::span<const std::byte> bytes);
