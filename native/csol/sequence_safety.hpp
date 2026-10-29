// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace csol
{
// Local converted-model metadata, before the relocated sequence descriptors.
// The stock loaders preserve this prefix when they repack texture images.
// These constants are a disk/memory format, not pointers or an engine ABI.
inline constexpr std::uint32_t SequenceSafetyMagic0 = 0x51455343;
inline constexpr std::uint32_t SequenceSafetyMagic1 = 0x31415444;
inline constexpr std::uint32_t SequenceSafetyVersion = 1;
inline constexpr std::size_t SequenceSafetyRecordBytes = 32;
inline constexpr std::size_t SequenceSafetyFooterBytes = 32;
enum class SequenceSafetyStatus { Absent, Ready, Invalid };
enum class SequenceCheck { Ready, UnknownTrack, InvalidTrack, InvalidFrame };
// The permissive loading policy requires a guard at every actual selection
// and sampler. Bounds and other whole-model consumers must keep the default.
enum class SequenceReadPolicy { StrictAllTracks, AllowUnsupportedTracks };
struct SequenceSelection
{
    std::uint32_t animation{}, frames{}, blends{}, group{}, bones{};
};

namespace sequence_safety_detail
{
static_assert(std::endian::native == std::endian::little);
inline bool Range(std::span<const std::byte> bytes,std::size_t at,std::size_t size)
{ return at<=bytes.size() && size<=bytes.size()-at; }
inline std::uint32_t Word(std::span<const std::byte> bytes,std::size_t at)
{
    std::uint32_t value;
    std::memcpy(&value,bytes.data()+at,sizeof(value));
    return value;
}

// Loading-only validation. Status words in a file are not a trust boundary.
// Reserve four readable bytes after every real run, including a terminal run
// ending exactly at the last frame. Stock samplers read that second endpoint
// even when its interpolation coefficient is zero.
inline bool Animation(std::span<const std::byte> bytes,std::uint32_t bones,
    std::uint32_t blends,std::uint32_t frames)
{
    const auto tables=std::size_t(bones)*blends*12;
    if(bytes.size()<tables+4)return false;
    std::vector<unsigned char> roles;
    const auto mark=[&](std::size_t begin,std::size_t length,unsigned char role)
    {
        if(roles.size()<begin+length)roles.resize(begin+length);
        for(auto at=begin;at<begin+length;++at)
        {
            if(roles[at] && roles[at]!=role)return false;
            roles[at]=role;
        }
        return true;
    };
    for(std::size_t table=0;table<tables;table+=12)for(int axis=0;axis<6;++axis)
    {
        std::uint16_t offset;
        std::memcpy(&offset,bytes.data()+table+axis*2,2);
        if(!offset)continue;
        auto cursor=table+offset;
        if(cursor<tables)return false;
        auto remaining=std::int32_t(frames);
        while(remaining>0)
        {
            if(!Range(bytes,cursor,2))return false;
            const auto valid=std::to_integer<unsigned>(bytes[cursor]);
            const auto total=std::to_integer<unsigned>(bytes[cursor+1]);
            const auto runBytes=(valid+1)*2;
            if(!valid || total<valid || !Range(bytes,cursor,runBytes+4))return false;
            if(!mark(cursor,2,1) || !mark(cursor+2,valid*2,2))return false;
            cursor+=runBytes;
            remaining-=total;
        }
    }
    return true;
}
}

// Borrowed from one validated, immutable loaded-model generation. The caller
// must discard this view before unloading/replacing that model. No global
// registry or cache keyed by a mutable descriptor slot is involved.
class SequenceSafetyView
{
public:
    SequenceCheck Check(const SequenceSelection& selected,float frame) const
    {
        using namespace sequence_safety_detail;
        if(bytes_.empty() || selected.bones!=bones_)return SequenceCheck::UnknownTrack;
        std::size_t first=0,last=count_;
        while(first<last)
        {
            const auto mid=first+(last-first)/2;
            if(Word(bytes_,records_+mid*SequenceSafetyRecordBytes)<selected.animation)first=mid+1;
            else last=mid;
        }
        if(first==count_)return SequenceCheck::UnknownTrack;
        const auto at=records_+first*SequenceSafetyRecordBytes;
        if(Word(bytes_,at)!=selected.animation || Word(bytes_,at+8)!=selected.frames ||
            Word(bytes_,at+12)!=selected.blends || Word(bytes_,at+16)!=selected.group ||
            Word(bytes_,at+20)!=selected.bones)return SequenceCheck::UnknownTrack;
        if(Word(bytes_,at+24)!=1)return SequenceCheck::InvalidTrack;
        if(!std::isfinite(frame) || frame<0 || frame>float(selected.frames-1))
            return SequenceCheck::InvalidFrame;
        return SequenceCheck::Ready;
    }

private:
    friend SequenceSafetyStatus ReadSequenceSafety(std::span<const std::byte>,SequenceSafetyView&,SequenceReadPolicy);
    std::span<const std::byte> bytes_;
    std::uint32_t records_{},count_{},bones_{};
};

// Call at model-load/ownership boundaries using the actual retained byte span.
// Validates both the metadata and actual RLE bytes before publishing a view.
// Runtime users must not manufacture a span from an unchecked header length.
// This is a loading operation, not a draw-path or per-query operation.
inline SequenceSafetyStatus ReadSequenceSafety(std::span<const std::byte> bytes,SequenceSafetyView& output,
    SequenceReadPolicy policy=SequenceReadPolicy::StrictAllTracks)
{
    using namespace sequence_safety_detail;
    output={};
    if(!Range(bytes,0,244))return SequenceSafetyStatus::Invalid;
    const auto descriptors=Word(bytes,168);
    if(descriptors<SequenceSafetyFooterBytes || !Range(bytes,descriptors-SequenceSafetyFooterBytes,SequenceSafetyFooterBytes))
        return SequenceSafetyStatus::Absent;
    const auto footer=descriptors-SequenceSafetyFooterBytes;
    const auto magic0=Word(bytes,footer),magic1=Word(bytes,footer+4);
    if(magic0!=SequenceSafetyMagic0 && magic1!=SequenceSafetyMagic1)return SequenceSafetyStatus::Absent;
    if(magic0!=SequenceSafetyMagic0 || magic1!=SequenceSafetyMagic1 ||
        Word(bytes,footer+8)!=SequenceSafetyVersion || Word(bytes,footer+24)!=SequenceSafetyRecordBytes ||
        Word(bytes,footer+28)!=0 || Word(bytes,0)!=0x54534449 || Word(bytes,4)!=10)
        return SequenceSafetyStatus::Invalid;
    const auto count=Word(bytes,footer+12),bones=Word(bytes,footer+16),records=Word(bytes,footer+20);
    if(count<1 || count>2048 || count!=Word(bytes,164) || bones<1 || bones>256 || bones!=Word(bytes,140) ||
        records<244 || records>footer || std::size_t(count)*SequenceSafetyRecordBytes!=footer-records ||
        !Range(bytes,descriptors,std::size_t(count)*176))return SequenceSafetyStatus::Invalid;
    const auto length=Word(bytes,72),textureCount=Word(bytes,180),textureIndex=Word(bytes,184);
    const auto retained=textureIndex?Word(bytes,188):length;
    const auto boneIndex=Word(bytes,144);
    // The supplied span can be a retained prefix smaller than file length.
    // Verify the declared prefix is actually present, rather than constructing
    // a view over memory merely because header.length says it exists.
    if(length<244 || length>128*1024*1024 || retained>length || retained>bytes.size() ||
        descriptors>retained || std::size_t(count)*176>retained-descriptors ||
        boneIndex<244 || boneIndex>retained || std::size_t(bones)*112>retained-boneIndex ||
        textureCount>4096 || (!textureIndex && textureCount) ||
        (textureIndex && (textureIndex<244 || textureIndex>retained ||
            std::size_t(textureCount)*80>retained-textureIndex)))return SequenceSafetyStatus::Invalid;
    std::size_t previousEnd=244;
    for(std::size_t i=0;i<count;++i)
    {
        const auto at=records+i*SequenceSafetyRecordBytes;
        const auto animation=Word(bytes,at),size=Word(bytes,at+4),frames=Word(bytes,at+8),blends=Word(bytes,at+12);
        const auto status=Word(bytes,at+24);
        if(animation<previousEnd || animation>=records || size>records-animation ||
            frames<1 || frames>65536 || blends<1 || blends>16 ||
            Word(bytes,at+16)!=0 || Word(bytes,at+20)!=bones || status>1 || Word(bytes,at+28)!=0 || !size ||
            (!status && policy!=SequenceReadPolicy::AllowUnsupportedTracks))return SequenceSafetyStatus::Invalid;
        const auto descriptor=descriptors+i*176;
        if(Word(bytes,descriptor+124)!=animation || Word(bytes,descriptor+56)!=frames ||
            Word(bytes,descriptor+120)!=blends || Word(bytes,descriptor+156)!=0)return SequenceSafetyStatus::Invalid;
        // A ready word never certifies disk bytes. An unsupported word is only
        // a quarantine marker for a preserved incompatible block, not a way to
        // suppress validation of a valid track or bypass the default policy.
        const bool sampleable=Animation(bytes.subspan(animation,size),bones,blends,frames);
        if(sampleable!=(status==1))return SequenceSafetyStatus::Invalid;
        previousEnd=std::size_t(animation)+size;
    }
    SequenceSafetyView candidate;
    candidate.bytes_=bytes.first(descriptors);
    candidate.records_=records;candidate.count_=count;candidate.bones_=bones;
    output=candidate;
    return SequenceSafetyStatus::Ready;
}
}
