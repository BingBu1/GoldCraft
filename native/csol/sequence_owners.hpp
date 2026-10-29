// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sequence_safety.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace csol
{
// Engine-local ownership only; no pointer or STL object crosses a process or
// DLL boundary. Model/cache callbacks supply live owners, never saved pointers
// which this registry could dereference after model storage has been retired.
enum class SequenceOwnerCheck
{
    Ordinary, Ready, MissingOwner, WrongHeader, InvalidDescriptor,
    UnknownTrack, InvalidTrack, InvalidFrame, InvalidAnimation
};

// Matched B client/hw CalcRotations normalization, before integer conversion.
// Callers pass the original raw frame to stock code; these values only describe
// the indices/fraction it will actually consume for the safety check.
inline bool ClientSequenceFrame(float raw,std::uint32_t frames,float& integer,float& fraction)
{
    if(!frames || frames>65536 || !std::isfinite(raw))return false;
    if(raw>float(frames-1))raw=0;
    else if(double(raw)<-0.01)raw=-0.01f;
    integer=std::trunc(raw);fraction=raw-integer;return true;
}

class PreparedSequenceModel
{
public:
    static PreparedSequenceModel Read(std::span<const std::byte> bytes,
        SequenceReadPolicy policy=SequenceReadPolicy::StrictAllTracks)
    {
        using namespace sequence_safety_detail;
        SequenceSafetyView view;
        const auto status=ReadSequenceSafety(bytes,view,policy);
        if(status==SequenceSafetyStatus::Invalid || !Range(bytes,0,244) ||
            Word(bytes,0)!=0x54534449 || Word(bytes,4)!=10)
            throw std::runtime_error("invalid studio model for sequence ownership");
        PreparedSequenceModel result;
        result.tagged_=status==SequenceSafetyStatus::Ready;
        result.length_=Word(bytes,72);
        result.textureIndex_=Word(bytes,184);
        result.retained_=result.textureIndex_?Word(bytes,188):result.length_;
        if(result.length_<244 || result.length_>bytes.size() ||
            result.retained_<244 || result.retained_>result.length_)
            throw std::runtime_error("studio retained prefix is outside the actual file");
        result.bones_=Word(bytes,140);result.boneIndex_=Word(bytes,144);
        result.count_=Word(bytes,164);result.descriptors_=Word(bytes,168);
        if(result.tagged_)
        {
            // Both matched stock samplers have fixed 128-bone scratch arrays.
            if(result.bones_>128)throw std::runtime_error("studio exceeds stock sampler bone capacity");
            const auto footer=result.descriptors_-SequenceSafetyFooterBytes;
            result.records_=Word(bytes,footer+20);
            result.tracks_.resize(result.count_);
            for(std::size_t i=0;i<result.tracks_.size();++i)
                for(std::size_t word=0;word<8;++word)
                    result.tracks_[i][word]=Word(bytes,result.records_+i*SequenceSafetyRecordBytes+word*4);
        }
        return result;
    }

    std::uint32_t RetainedBytes() const{return retained_;}
    bool Tagged() const{return tagged_;}

private:
    friend class SequenceOwnerRegistry;
    // Owned values only. Copy/move this object without borrowing input bytes.
    std::vector<std::array<std::uint32_t,8>> tracks_;
    std::uint32_t length_{},retained_{},bones_{},boneIndex_{},count_{},descriptors_{},records_{},textureIndex_{};
    bool tagged_{};

    bool HeaderMatches(std::span<const std::byte> loaded) const
    {
        using namespace sequence_safety_detail;
        return loaded.size()==retained_ && Range(loaded,0,244) &&
            Word(loaded,0)==0x54534449 && Word(loaded,4)==10 && Word(loaded,72)==length_ &&
            Word(loaded,140)==bones_ && Word(loaded,144)==boneIndex_ &&
            Word(loaded,164)==count_ && Word(loaded,168)==descriptors_ &&
            Word(loaded,184)==textureIndex_ && (!textureIndex_ || Word(loaded,188)==retained_);
    }
    bool CopiedMetadataMatches(std::span<const std::byte> loaded) const
    {
        using namespace sequence_safety_detail;
        if(!HeaderMatches(loaded))return false;
        if(!tagged_)return true;
        const auto footer=descriptors_-SequenceSafetyFooterBytes;
        const std::array<std::uint32_t,8> expected{SequenceSafetyMagic0,SequenceSafetyMagic1,
            SequenceSafetyVersion,count_,bones_,records_,std::uint32_t(SequenceSafetyRecordBytes),0};
        for(std::size_t word=0;word<8;++word)if(Word(loaded,footer+word*4)!=expected[word])return false;
        for(std::size_t i=0;i<tracks_.size();++i)for(std::size_t word=0;word<8;++word)
            if(Word(loaded,records_+i*SequenceSafetyRecordBytes+word*4)!=tracks_[i][word])return false;
        return true;
    }
};

struct SequenceSample
{
    // A short-lived value ticket, never a dereferenceable saved view.
    std::uintptr_t owner{},base{};
    std::uint64_t generation{};
    SequenceSelection selection{};
};

class SequenceOwnerRegistry
{
    struct Entry
    {
        PreparedSequenceModel model;
        std::uint64_t load{},allocation{},allocationLoad{},content{};
        std::size_t allocatedBytes{};
        bool loaded{},allocationConflict{};
    };
    std::unordered_map<std::uintptr_t,Entry> owners_;
    std::uint64_t serial_{};
    std::uint64_t Next()
    {
        if(serial_==std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("studio ownership generation exhausted");
        return ++serial_;
    }
    static bool SpanAddress(std::uintptr_t base,std::size_t length)
    {return base && length<=std::numeric_limits<std::uintptr_t>::max()-base;}
public:
    using LoadToken=std::uint64_t;
    LoadToken BeginLoad(std::uintptr_t owner)
    {
        if(!owner)throw std::runtime_error("null studio cache owner");
        Entry candidate;candidate.load=Next();
        owners_.insert_or_assign(owner,std::move(candidate));
        return serial_;
    }
    // Observe every Cache_Alloc before it can overwrite this owner's storage.
    // Unrelated texture caches need no entries or allocations in this registry.
    void Allocating(std::uintptr_t owner,std::size_t bytes)
    {
        const auto found=owners_.find(owner);if(found==owners_.end())return;
        auto& entry=found->second;
        entry.loaded=false;entry.model={};entry.content=0;
        // A second allocation during the same load cannot reuse the first
        // source's RLE proof, even if header/metadata happen to match. Latch
        // the conflict until a genuinely new BeginLoad replaces this token.
        if(entry.load && entry.allocation)entry.allocationConflict=true;
        entry.allocation=Next();entry.allocationLoad=entry.load;entry.allocatedBytes=bytes;
    }
    bool CompleteLoad(std::uintptr_t owner,LoadToken token,PreparedSequenceModel model,
        std::uintptr_t liveBase,std::size_t expectedAllocation)
    {
        const auto found=owners_.find(owner);if(found==owners_.end())return false;
        auto& entry=found->second;
        if(!token || entry.load!=token || entry.allocationLoad!=token || !entry.allocation || entry.allocationConflict ||
            entry.allocatedBytes!=expectedAllocation || expectedAllocation<model.RetainedBytes() ||
            !SpanAddress(liveBase,model.RetainedBytes()))return false;
        // The pinned loader's matching allocation and copy establish this
        // actual prefix. Never derive its length from an unchecked live header.
        const std::span<const std::byte> prefix(reinterpret_cast<const std::byte*>(liveBase),model.RetainedBytes());
        if(!model.CopiedMetadataMatches(prefix))return false;
        entry.model=std::move(model);entry.content=Next();entry.load=0;entry.loaded=true;
        return true;
    }
    void CancelLoad(std::uintptr_t owner,LoadToken token)
    {
        const auto found=owners_.find(owner);
        if(found!=owners_.end() && found->second.load==token)owners_.erase(found);
    }
    void Reset()
    {
        // Do not reset serial: an old ticket must not become valid after ABA.
        Next();owners_.clear();
    }
    SequenceOwnerCheck ModelState(std::uintptr_t owner,std::uintptr_t liveBase,const void* actualHeader) const
    {
        const auto found=owners_.find(owner);
        if(found==owners_.end() || !found->second.loaded)return SequenceOwnerCheck::MissingOwner;
        const auto& model=found->second.model;
        if(!SpanAddress(liveBase,model.retained_) || liveBase!=reinterpret_cast<std::uintptr_t>(actualHeader))
            return SequenceOwnerCheck::WrongHeader;
        const std::span<const std::byte> prefix(reinterpret_cast<const std::byte*>(liveBase),model.retained_);
        if(!model.HeaderMatches(prefix))return SequenceOwnerCheck::WrongHeader;
        return model.tagged_?SequenceOwnerCheck::Ready:SequenceOwnerCheck::Ordinary;
    }
    SequenceOwnerCheck Select(std::uintptr_t owner,std::uintptr_t liveBase,
        const void* actualHeader,const void* descriptor,SequenceSample& output) const
    {
        using namespace sequence_safety_detail;
        output={};
        const auto found=owners_.find(owner);
        if(found==owners_.end() || !found->second.loaded)return SequenceOwnerCheck::MissingOwner;
        const auto& entry=found->second;const auto& model=entry.model;
        if(!SpanAddress(liveBase,model.retained_) || liveBase!=reinterpret_cast<std::uintptr_t>(actualHeader))
            return SequenceOwnerCheck::WrongHeader;
        const std::span<const std::byte> prefix(reinterpret_cast<const std::byte*>(liveBase),model.retained_);
        if(!model.HeaderMatches(prefix))return SequenceOwnerCheck::WrongHeader;
        if(!model.tagged_)return SequenceOwnerCheck::Ordinary;
        const auto address=reinterpret_cast<std::uintptr_t>(descriptor);
        if(address<liveBase)return SequenceOwnerCheck::InvalidDescriptor;
        const auto offset=address-liveBase;
        if(offset<model.descriptors_ || !Range(prefix,offset,176) ||
            (offset-model.descriptors_)%176 || (offset-model.descriptors_)/176>=model.count_)
            return SequenceOwnerCheck::InvalidDescriptor;
        SequenceSelection selected{Word(prefix,offset+124),Word(prefix,offset+56),
            Word(prefix,offset+120),Word(prefix,offset+156),model.bones_};
        const auto record=std::lower_bound(model.tracks_.begin(),model.tracks_.end(),selected.animation,
            [](const auto& row,std::uint32_t animation){return row[0]<animation;});
        if(record==model.tracks_.end() || (*record)[0]!=selected.animation ||
            (*record)[2]!=selected.frames || (*record)[3]!=selected.blends ||
            (*record)[4]!=selected.group || (*record)[5]!=selected.bones)return SequenceOwnerCheck::UnknownTrack;
        if((*record)[6]!=1)return SequenceOwnerCheck::InvalidTrack;
        output={owner,liveBase,entry.content,selected};
        return SequenceOwnerCheck::Ready;
    }
    SequenceOwnerCheck Sample(const SequenceSample& sample,std::uintptr_t liveBase,
        const void* animation,float integerFrame,float fraction,bool wholeBlend) const
    {
        const auto found=owners_.find(sample.owner);
        if(!sample.generation || found==owners_.end() || !found->second.loaded ||
            found->second.content!=sample.generation)return SequenceOwnerCheck::MissingOwner;
        if(!liveBase || sample.base!=liveBase)return SequenceOwnerCheck::WrongHeader;
        const auto& selected=sample.selection;
        if(!selected.bones || !selected.blends || !selected.frames)return SequenceOwnerCheck::InvalidTrack;
        // Client negative normalization truncates toward zero, keeping a small
        // negative fraction. Preserve that original extrapolation; never pass
        // a substituted integral frame to the actual stock sampler.
        if(!std::isfinite(integerFrame) || integerFrame<0 || integerFrame!=std::trunc(integerFrame) ||
            integerFrame>float(selected.frames-1) || !std::isfinite(fraction) ||
            fraction < -0.01f || fraction>=1 || (integerFrame>0 && fraction<0) ||
            (integerFrame==float(selected.frames-1) && fraction>0))return SequenceOwnerCheck::InvalidFrame;
        const auto address=reinterpret_cast<std::uintptr_t>(animation);
        if(address<liveBase || address-liveBase<selected.animation)return SequenceOwnerCheck::InvalidAnimation;
        const auto offset=address-liveBase-selected.animation;
        const auto stride=std::size_t(selected.bones)*12;
        if(offset>=stride*selected.blends || offset%(wholeBlend?stride:12))return SequenceOwnerCheck::InvalidAnimation;
        return SequenceOwnerCheck::Ready;
    }
};
}
