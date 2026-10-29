// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "csol_sequence_guard.h"
#include "../csol/sequence_owners.hpp"
#include "goldcraft/csol_sequence_api.hpp"

namespace
{
csol::SequenceOwnerRegistry owners;
std::uintptr_t Owner(model_t* model){return reinterpret_cast<std::uintptr_t>(&model->cache);}
std::uintptr_t Base(model_t* model){return reinterpret_cast<std::uintptr_t>(model->cache.data);}
void Check(model_t* model,const char* step,csol::SequenceOwnerCheck result)
{
    if(result!=csol::SequenceOwnerCheck::Ordinary && result!=csol::SequenceOwnerCheck::Ready)
        Sys_Error("CSOL studio safety %s: %s (%u)",model?model->name:"<no model>",step,unsigned(result));
}
class SequenceService final : public csol::ISequenceService
{
public:
    bool Tagged(void* model,const void* header) const override
    {return GoldCraftStudioTagged(static_cast<model_t*>(model),static_cast<const studiohdr_t*>(header));}
    void Select(void* model,const void* header,const void* descriptor) const override
    {GoldCraftStudioSelected(static_cast<model_t*>(model),static_cast<const studiohdr_t*>(header),static_cast<const mstudioseqdesc_t*>(descriptor));}
    void Sample(void* model,const void* header,const void* descriptor,const void* animation,
        float integerFrame,float fraction,bool wholeBlend) const override
    {GoldCraftStudioSample(static_cast<model_t*>(model),static_cast<const studiohdr_t*>(header),
        static_cast<const mstudioseqdesc_t*>(descriptor),static_cast<const mstudioanim_t*>(animation),integerFrame,fraction,wholeBlend);}
};
}
using csol::ISequenceService;
EXPOSE_SINGLE_INTERFACE(SequenceService,ISequenceService,csol::SequenceServiceVersion);

void GoldCraftStudioAllocating(cache_user_t* owner,int bytes)
{
    try{owners.Allocating(reinterpret_cast<std::uintptr_t>(owner),bytes>0?std::size_t(bytes):0);}
    catch(const std::exception& e){Sys_Error("CSOL studio allocation: %s",e.what());}
}
void GoldCraftStudioMemoryReset()
{
    try{owners.Reset();}catch(const std::exception& e){Sys_Error("CSOL studio memory reset: %s",e.what());}
}
void GoldCraftLoadStudioOwned(model_t* model,std::span<std::byte> bytes)
{
    csol::SequenceOwnerRegistry::LoadToken token{};
    try
    {
        // Selection and sampling are guarded in both the engine fallback and
        // the matching GameDLL. Unselected unsupported tracks remain inert.
        auto prepared=csol::PreparedSequenceModel::Read(bytes,csol::SequenceReadPolicy::AllowUnsupportedTracks);
        const auto* header=reinterpret_cast<const studiohdr_t*>(bytes.data());
        if(header->numtextures<0 || header->numtextures>4096)
            throw std::runtime_error("invalid studio texture count");
        // ReHLDS repacks palette entries to uint16[4], unlike the GL client.
        const auto allocated=std::size_t(header->length)+1280*std::size_t(header->numtextures);
        token=owners.BeginLoad(Owner(model));
        Mod_LoadStudioModel(model,bytes.data());
        if(!owners.CompleteLoad(Owner(model),token,std::move(prepared),Base(model),allocated))
            throw std::runtime_error("studio allocation/copy receipt mismatch");
    }
    catch(const std::exception& e)
    {
        if(token)owners.CancelLoad(Owner(model),token);
        Sys_Error("CSOL studio load %s: %s",model->name,e.what());
    }
}
bool GoldCraftStudioTagged(model_t* model,const studiohdr_t* header)
{
    if(!model || model->type!=mod_studio)Sys_Error("CSOL studio missing active model");
    const auto result=owners.ModelState(Owner(model),Base(model),header);
    Check(model,"model",result);return result==csol::SequenceOwnerCheck::Ready;
}
void GoldCraftStudioSelected(model_t* model,const studiohdr_t* header,const mstudioseqdesc_t* descriptor)
{
    if(!model || model->type!=mod_studio)Sys_Error("CSOL studio missing animation owner");
    csol::SequenceSample sample;
    Check(model,"selection",owners.Select(Owner(model),Base(model),header,descriptor,sample));
}
void GoldCraftStudioSample(model_t* model,const studiohdr_t* header,const mstudioseqdesc_t* descriptor,
    const mstudioanim_t* animation,float integerFrame,float fraction,bool wholeBlend)
{
    if(!model || model->type!=mod_studio)Sys_Error("CSOL studio missing sampler owner");
    csol::SequenceSample sample;
    const auto result=owners.Select(Owner(model),Base(model),header,descriptor,sample);
    Check(model,"selection",result);
    if(result==csol::SequenceOwnerCheck::Ordinary)return;
    Check(model,"sample",owners.Sample(sample,Base(model),animation,integerFrame,fraction,wholeBlend));
}
void GoldCraftCheckStudioBounds(std::span<const std::byte> bytes)
{
    try
    {
        csol::SequenceSafetyView view;
        const auto state=csol::ReadSequenceSafety(bytes,view);
        if(state==csol::SequenceSafetyStatus::Invalid)throw std::runtime_error("invalid studio bounds metadata");
        // Bounds genuinely consumes every track. Requiring the complete strict
        // reader here must remain when loading later allows unselected tracks.
        if(state==csol::SequenceSafetyStatus::Ready)
        {
            const auto* header=reinterpret_cast<const studiohdr_t*>(bytes.data());
            if(header->numbones>128)throw std::runtime_error("studio bounds bone count exceeds host limit");
        }
    }
    catch(const std::exception& e){Sys_Error("CSOL studio bounds: %s",e.what());}
}
