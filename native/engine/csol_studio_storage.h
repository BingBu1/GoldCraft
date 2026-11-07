// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol/sequence_safety.hpp"
#include <limits>
#include <stdexcept>

namespace csol
{
struct StudioStorage
{
    int bytes;
    bool prefix;
};

// CSO hw c7bddfda, VA 378B08EC/378B090D/378B0D46: cache the
// non-pixel prefix; VA 378B03E3 skips textures on the dedicated server.
// Eligibility below is a host adapter, not an alteration of the MDL header.
inline StudioStorage PlanStudioStorage(std::span<const std::byte> bytes,bool dedicated)
{
    if(bytes.size()<sizeof(studiohdr_t))throw std::runtime_error("truncated studio storage header");
    studiohdr_t h;
    std::memcpy(&h,bytes.data(),sizeof(h));
    if(h.length<int(sizeof(h)) || std::size_t(h.length)>bytes.size() ||
        h.numtextures<0 || h.numtextures>4096 ||
        h.length>std::numeric_limits<int>::max()-1280*h.numtextures)
        throw std::runtime_error("invalid studio storage size");
    const StudioStorage legacy{h.length+1280*h.numtextures,false};
    if(!dedicated || !h.textureindex)return legacy;
    if(h.texturedataindex<int(sizeof(h)) || h.texturedataindex>h.length)return legacy;
    const auto prefix=bytes.first(std::size_t(h.texturedataindex));
    const auto array=[&](int offset,int count,std::size_t size)
    {
        if(count<0 || offset<0)return false;
        if(!count)return true;
        return offset>=int(sizeof(h)) && std::size_t(offset)<=prefix.size() &&
            std::size_t(count)<=(prefix.size()-std::size_t(offset))/size;
    };
    if(!array(h.boneindex,h.numbones,sizeof(mstudiobone_t)) ||
        !array(h.bonecontrollerindex,h.numbonecontrollers,sizeof(mstudiobonecontroller_t)) ||
        !array(h.hitboxindex,h.numhitboxes,sizeof(mstudiobbox_t)) ||
        !array(h.seqindex,h.numseq,sizeof(mstudioseqdesc_t)) ||
        !array(h.seqgroupindex,h.numseqgroups,sizeof(mstudioseqgroup_t)) ||
        !array(h.attachmentindex,h.numattachments,sizeof(mstudioattachment_t)) ||
        !array(h.bodypartindex,h.numbodyparts,sizeof(mstudiobodyparts_t)) ||
        !array(h.textureindex,h.numtextures,sizeof(mstudiotexture_t)) ||
        h.numtransitions<0 || (h.numtransitions &&
            !array(h.transitionindex,h.numtransitions,std::size_t(h.numtransitions))) ||
        h.numskinfamilies<0 || h.numskinref<0 || (h.numskinfamilies &&
            !array(h.skinindex,h.numskinref,std::size_t(h.numskinfamilies)*2)))return legacy;

    SequenceSafetyView safety;
    const auto tagged=ReadSequenceSafety(prefix,safety,SequenceReadPolicy::AllowUnsupportedTracks);
    if(tagged==SequenceSafetyStatus::Invalid)return legacy;
    for(int index=0;index<h.numseq;++index)
    {
        mstudioseqdesc_t seq;
        std::memcpy(&seq,prefix.data()+h.seqindex+std::size_t(index)*sizeof(seq),sizeof(seq));
        if(!array(seq.eventindex,seq.numevents,sizeof(mstudioevent_t)) ||
            !array(seq.pivotindex,seq.numpivots,sizeof(mstudiopivot_t)) ||
            seq.seqgroup<0 || seq.seqgroup>=MAXSTUDIOGROUPS ||
            (seq.seqgroup && seq.seqgroup>=h.numseqgroups))return legacy;
        // External group animations live in their own cache; animindex is
        // relative to that file. Tagged local tracks were checked above.
        if(seq.seqgroup || tagged==SequenceSafetyStatus::Ready)continue;
        if(h.numbones<1 || h.numbones>MAXSTUDIOBONES ||
            seq.numblends<1 || seq.numblends>16 || seq.numframes<1 || seq.numframes>65536 ||
            seq.animindex<int(sizeof(h)) || std::size_t(seq.animindex)>=prefix.size() ||
            !sequence_safety_detail::Animation(prefix.subspan(std::size_t(seq.animindex)),
                h.numbones,seq.numblends,seq.numframes))return legacy;
    }
    return {h.texturedataindex,true};
}
}

struct model_s;
// Internal engine entry for a plan computed from this exact, validated buffer.
// Callers with registered loader hooks must use Mod_LoadStudioModel instead.
void Mod_LoadStudioModelPlanned(model_s* model,void* buffer,const csol::StudioStorage& storage);
