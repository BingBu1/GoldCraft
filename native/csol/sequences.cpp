// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequences.hpp"
#include "sequence_safety.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace csol
{
namespace
{
constexpr size_t DescriptorSize = 176;
constexpr size_t MaxBytes = 128 * 1024 * 1024;
constexpr int MaxSequences = 2048;
constexpr int FixedUnitFirstSequence = 198;
constexpr std::array<float,6> FixedScale{1.0f/256,1.0f/256,1.0f/256,
    1.0f/10240,1.0f/10240,1.0f/10240};
using BoneScales=std::vector<std::array<float,6>>;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Range(std::span<const std::byte> bytes, size_t offset, size_t size)
{
    Require(offset <= bytes.size() && size <= bytes.size() - offset, "SEQ range outside payload");
}
int Word(std::span<const std::byte> bytes, size_t offset)
{
    Range(bytes, offset, 4);
    int value; std::memcpy(&value, bytes.data() + offset, 4); return value;
}
void Put(std::vector<std::byte>& bytes, size_t offset, int value)
{
    Range(bytes, offset, 4); std::memcpy(bytes.data() + offset, &value, 4);
}
void Grow(std::vector<std::byte>& out, std::span<const std::byte> bytes)
{
    Require(bytes.size() <= MaxBytes - out.size(), "merged studio model exceeds budget");
    out.insert(out.end(), bytes.begin(), bytes.end());
}
float Float(std::span<const std::byte> bytes,size_t offset)
{
    Range(bytes,offset,4);float value;std::memcpy(&value,bytes.data()+offset,4);return value;
}
// In normal gameplay CSO selects channel units by the final sequence number,
// not the MDL/SEQ source. A stock studio model has only one scale per bone.
// Requantize both unit systems into a common representable scale when packing.
// Keep the original RLE layout and verify any shared value is consistent.
std::vector<std::byte> RescaleAnimation(std::span<const std::byte> block,int blends,int frames,
    const BoneScales& source,const BoneScales& target)
{
    std::vector<std::byte> result(block.begin(),block.end());
    const bool rescale=source!=target;
    // Occupancy is separate from the value: zero and odd/shared bytes are valid.
    std::vector<unsigned char> written(rescale?block.size():0);
    if(rescale)for(int blend=0;blend<blends;++blend)for(size_t bone=0;bone<source.size();++bone)
        for(int axis=0;axis<6;++axis)
        {
            const size_t table=(blend*source.size()+bone)*12;
            unsigned short offset;std::memcpy(&offset,block.data()+table+axis*2,2);
            if(!offset)continue;
            size_t cursor=table+offset;int remaining=frames;
            while(remaining>0)
            {
                const auto valid=std::to_integer<unsigned>(block[cursor]);
                const auto total=std::to_integer<unsigned>(block[cursor+1]);
                for(unsigned i=1;i<=valid;++i)
                {
                    const size_t at=cursor+i*2;short value;std::memcpy(&value,block.data()+at,2);
                    const double converted=std::round(double(value)*source[bone][axis]/target[bone][axis]);
                    Require(converted>=-32768 && converted<=32767,"animation rescale overflow");
                    const auto quantized=static_cast<short>(converted);
                    std::array<std::byte,2> encoded;
                    std::memcpy(encoded.data(),&quantized,2);
                    for(size_t byte=0;byte<encoded.size();++byte)
                    {
                        Require(!written[at+byte] || result[at+byte]==encoded[byte],"animation shares incompatible channel scales");
                        written[at+byte]=1;
                        result[at+byte]=encoded[byte];
                    }
                }
                cursor+=(valid+1)*2;remaining-=total;
            }
        }
    // GoldSrc reads a second endpoint even at the exact final frame, where
    // the interpolation fraction is zero. Original tracks need not contain
    // a next run: their last channel can end at the block boundary. Preserve
    // all real runs and provide bounded storage for that otherwise unused read.
    // This is padding, not another authored animation frame.
    Require(result.size()<=MaxBytes-4,"animation padding exceeds budget");
    const std::array<std::byte,4> terminal{std::byte{1},std::byte{1},
        result[result.size()-2],result[result.size()-1]};
    result.insert(result.end(),terminal.begin(),terminal.end());
    return result;
}
// Studio animation offsets are relative to each bone's 12-byte channel table.
// Validate the RLE too: a zero-length run would hang the original renderer.
size_t Animation(std::span<const std::byte> block, int bones, int blends, int frames)
{
    Require(blends >= 1 && blends <= 16 && frames >= 1 && frames <= 65536, "invalid SEQ animation dimensions");
    const size_t tables = size_t(bones) * blends * 12;
    Range(block, 0, tables);
    size_t end=tables;
    // A stream may share samples, but a sample must never alias any run
    // header. Requantization would otherwise change another channel's counts.
    // Classify bytes rather than words so odd and partially shared offsets
    // cannot evade the check. This temporary storage exists only at loading.
    std::vector<unsigned char> roles;
    const auto mark=[&](size_t begin,size_t length,unsigned char role)
    {
        Range(block,begin,length);
        if(roles.size()<begin+length)roles.resize(begin+length);
        for(size_t at=begin;at<begin+length;++at)
        {
            Require(!roles[at] || roles[at]==role,"SEQ animation sample overlaps run header");
            roles[at]=role;
        }
    };
    for (size_t table = 0; table < tables; table += 12)
    {
        for (int axis = 0; axis < 6; ++axis)
        {
            unsigned short offset; std::memcpy(&offset, block.data() + table + axis * 2, 2);
            if (!offset) continue;
            size_t cursor = table + offset;
            Require(cursor >= tables, "SEQ animation overlaps channel tables");
            int remaining = frames;
            while (remaining > 0)
            {
                Range(block, cursor, 2);
                const auto valid = std::to_integer<unsigned>(block[cursor]);
                const auto total = std::to_integer<unsigned>(block[cursor + 1]);
                Require(valid > 0 && total >= valid, "invalid SEQ animation run");
                Range(block, cursor, (valid + 1) * 2);
                mark(cursor,2,1);
                mark(cursor+2,valid*2,2);
                cursor += (valid + 1) * 2;
                end=std::max(end,cursor);
                remaining -= total;
            }
        }
    }
    return end;
}

struct AnimationArena { size_t begin{},end{}; };

// A packing optimization, not a new model format. Reuse dead animation and
// descriptor bytes only after proving every other v10 consumer is disjoint.
// See docs/2026-11-07_分析-角色动画前缀重复与原槽复用边界-report.md.
bool CanReuseAnimationArena(std::span<const std::byte> model,AnimationArena arena,
    const std::vector<std::vector<std::byte>>& sequences)
{
    if(arena.begin<244 || arena.end>model.size() || arena.begin>=arena.end)return false;
    size_t budget=model.size()*2;
    const auto live=[&](int offset,size_t bytes)
    {
        if(!bytes)return true;
        if(offset<0)return false;
        const auto at=size_t(offset);
        if(at>model.size() || bytes>model.size()-at || bytes>budget)return false;
        budget-=bytes;
        return at+bytes<=arena.begin || at>=arena.end;
    };
    const auto array=[&](int offset,int count,size_t stride)
    {
        return count>=0 && size_t(count)<=model.size()/stride && live(offset,size_t(count)*stride);
    };
    if(!live(0,244))return false;
    for(const auto entry:{std::array{140,144,112},std::array{148,152,24},std::array{156,160,32},
                         std::array{172,176,104},std::array{180,184,80},std::array{212,216,88}})
        if(!array(Word(model,entry[1]),Word(model,entry[0]),size_t(entry[2])))return false;
    // These optional encodings are not covered by the reference closure.
    // Runtime Renderer writes soundtable as a cache slot; inspect disk input.
    for(size_t at=220;at<236;at+=4)if(Word(model,at)!=0)return false;
    const int groups=Word(model,172),groupIndex=Word(model,176);
    if(groups!=1 || Word(model,size_t(groupIndex)+96)!=0 || Word(model,size_t(groupIndex)+100)!=0)return false;
    const int skins=Word(model,192),families=Word(model,196),transitions=Word(model,236);
    if(skins<0 || families<0 || transitions<0)return false;
    const auto skinBytes=std::uint64_t(skins)*std::uint64_t(families)*2;
    const auto transitionBytes=std::uint64_t(transitions)*std::uint64_t(transitions);
    if(skinBytes>model.size() || transitionBytes>model.size() ||
       !live(Word(model,200),size_t(skinBytes)) || !live(Word(model,240),size_t(transitionBytes)))return false;
    const int textures=Word(model,180),textureData=Word(model,188);
    if(textures && (textureData<0 || size_t(textureData)>model.size() ||
       !live(textureData,model.size()-size_t(textureData))))return false;
    const int count=Word(model,164),index=Word(model,168);
    const auto descriptorAllowsReuse=[&](std::span<const std::byte> bytes,size_t at)
    {
        // FindTransition indexes the matrix using either endpoint. Inspect
        // appended descriptors too: they share the original model's graph.
        const int entry=Word(bytes,at+160),exit=Word(bytes,at+164);
        return Word(bytes,at+88)==0 && Word(bytes,at+92)==0 && Word(bytes,at+156)==0 &&
            entry>=0 && entry<=transitions && exit>=0 && exit<=transitions &&
            (entry==0 || exit>0);
    };
    for(int i=0;i<count;++i)
    {
        const auto at=size_t(index)+size_t(i)*DescriptorSize;
        if(!descriptorAllowsReuse(model,at) ||
           !array(Word(model,at+52),Word(model,at+48),76) ||
           !array(Word(model,at+64),Word(model,at+60),20))return false;
    }
    for(const auto& file:sequences)
    {
        if(file.size()<8)return false;
        const int added=Word(file,0),start=Word(file,4);
        if(added<1 || start<8 || size_t(start)>file.size() ||
           size_t(added)>(file.size()-size_t(start))/DescriptorSize)return false;
        for(int i=0;i<added;++i)
            if(!descriptorAllowsReuse(file,size_t(start)+size_t(i)*DescriptorSize))return false;
    }
    const int bodies=Word(model,204),bodyIndex=Word(model,208);
    if(!array(bodyIndex,bodies,76))return false;
    for(int body=0;body<bodies;++body)
    {
        const auto at=size_t(bodyIndex)+size_t(body)*76;
        const int models=Word(model,at+64),modelIndex=Word(model,at+72);
        if(!array(modelIndex,models,112))return false;
        for(int sub=0;sub<models;++sub)
        {
            const auto m=size_t(modelIndex)+size_t(sub)*112;
            if(Word(model,m+64)!=0 || Word(model,m+104)!=0 || Word(model,m+108)!=0)return false;
            const int vertices=Word(model,m+80),normals=Word(model,m+92);
            if(!array(Word(model,m+84),vertices,1) || !array(Word(model,m+88),vertices,12) ||
               !array(Word(model,m+96),normals,1) || !array(Word(model,m+100),normals,12))return false;
            const int meshes=Word(model,m+72),meshIndex=Word(model,m+76);
            if(!array(meshIndex,meshes,20))return false;
            for(int mesh=0;mesh<meshes;++mesh)
            {
                const auto record=size_t(meshIndex)+size_t(mesh)*20;
                // The active Renderer uses the submodel normal array, not this
                // legacy mesh field. Do not reinterpret an unknown nonzero one.
                if(Word(model,record+16)!=0)return false;
                const int commands=Word(model,record+4);
                if(commands<0)return false;
                size_t cursor=size_t(commands);
                for(;;)
                {
                    if(cursor>model.size() || 2>model.size()-cursor || !live(int(cursor),2))return false;
                    short command;std::memcpy(&command,model.data()+cursor,2);cursor+=2;
                    if(!command)break;
                    const int n=command<0?-int(command):int(command);
                    const size_t bytes=size_t(n)*8;
                    if(n<3 || !live(int(cursor),bytes))return false;
                    for(int v=0;v<n;++v)
                    {
                        short vertex,normal;
                        std::memcpy(&vertex,model.data()+cursor+size_t(v)*8,2);
                        std::memcpy(&normal,model.data()+cursor+size_t(v)*8+2,2);
                        if(vertex<0 || vertex>=vertices || normal<0 || normal>=normals)return false;
                    }
                    cursor+=bytes;
                }
            }
        }
    }
    return true;
}
}

std::vector<std::string> SequenceFiles(const std::string& model, const ResourceExists& exists)
{
    std::vector<std::string> files;
    if (model.size() < 4 || model.substr(model.size() - 4) != ".mdl") return files;
    const auto stem = model.substr(0, model.size() - 4);
    for (int i = 1; i <= 256; ++i)
    {
        auto path = stem + std::to_string(i) + ".seq";
        if (!exists(path)) break;
        files.push_back(std::move(path));
    }
    return files;
}

std::vector<std::byte> AppendSequences(std::span<const std::byte> model,
    const std::vector<std::vector<std::byte>>& sequences,SequenceReadPolicy policy)
{
    Require(model.size() >= 244 && model.size() <= MaxBytes, "invalid studio size for SEQ append");
    Require(Word(model, 0) == 0x54534449 && Word(model, 4) == 10, "SEQ requires a decoded IDST model");
    const int length = Word(model, 72), bones = Word(model, 140);
    int count = Word(model, 164);
    Require(length >= 244 && size_t(length) <= model.size() && bones > 0 && bones <= 256,
        "invalid studio header for SEQ append");
    Require(count >= 1 && count <= MaxSequences && sequences.size() <= 256, "SEQ count exceeds budget");
    model = model.first(length);
    SequenceSafetyView safetyView;
    const auto stored=ReadSequenceSafety(model,safetyView,policy);
    Require(stored!=SequenceSafetyStatus::Invalid,"invalid packed sequence safety data");
    // Exported models may still have their original SEQ files alongside them.
    // All consumers, including dependency collection, share this idempotence.
    if(stored==SequenceSafetyStatus::Ready)return {model.begin(),model.end()};
    if(sequences.empty())return {model.begin(),model.end()};
    int totalCount=count;
    for(const auto& file:sequences)
    {
        const int added=Word(file,0);
        Require(added>0 && added<=MaxSequences-totalCount,"SEQ count exceeds budget");
        totalCount+=added;
    }
    const int oldIndex = Word(model, 168);
    Range(model, size_t(oldIndex), size_t(count) * DescriptorSize);
    // Both stock loaders copy only the prefix before texturedataindex, then
    // upload/repack textures. Keep every animation before that boundary.
    const int textures=Word(model,180),textureIndex=Word(model,184);
    Require(textures>=0 && textures<=4096,"invalid texture count for SEQ append");
    const int textureData=textures>0?Word(model,188):length;
    Require(textureData>=244 && textureData<=length,"invalid texture boundary for SEQ append");
    Range(model,size_t(textureIndex),size_t(textures)*80);
    Require(size_t(textureIndex)+size_t(textures)*80<=size_t(textureData),"texture records overlap image payload");
    Require(oldIndex+size_t(count)*DescriptorSize<=size_t(textureData),"original sequences overlap texture payload");
    std::vector<std::byte> out(model.begin(), model.begin()+textureData);
    std::vector<std::byte> descriptors(model.begin() + oldIndex, model.begin() + oldIndex + count * DescriptorSize);
    std::vector<std::array<std::uint32_t,8>> safety;
    safety.reserve(totalCount);
    const auto appendAnimation=[&](std::vector<std::byte> animation,int frames,int blends,bool sampleable=true)
    {
        const auto offset=out.size(),size=animation.size();
        Grow(out,animation);
        safety.push_back({std::uint32_t(offset),std::uint32_t(size),std::uint32_t(frames),
            std::uint32_t(blends),0,std::uint32_t(bones),sampleable?1u:0u,0});
    };
    const int boneIndex=Word(model,144);
    Range(model,size_t(boneIndex),size_t(bones)*112);
    Require(size_t(boneIndex)+size_t(bones)*112<=size_t(textureData),"bones overlap texture payload");
    BoneScales original(bones),fixed(bones,FixedScale),target(bones);
    for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)
    {
        const size_t at=size_t(boneIndex)+bone*112+88+axis*4;
        const float scale=Float(model,at);
        Require(std::isfinite(scale) && scale>=0,"invalid studio bone scale");
        original[bone][axis]=scale;
        target[bone][axis]=totalCount>FixedUnitFirstSequence?std::max(scale,FixedScale[axis]):scale;
        std::memcpy(out.data()+at,&target[bone][axis],4);
    }
    // The original GameUI state6 / non-preview sampler switches at index198.
    // MDL tracks can be above that threshold and appended tracks can be below.
    std::vector<int> nativeOffsets;
    nativeOffsets.reserve(count+1);
    nativeOffsets.push_back(textureData);
    nativeOffsets.push_back(oldIndex);
    for(int i=0;i<count;++i)nativeOffsets.push_back(Word(descriptors,i*DescriptorSize+124));
    std::sort(nativeOffsets.begin(),nativeOffsets.end());
    std::vector<std::vector<std::byte>> nativeAnimations;
    nativeAnimations.reserve(count);
    AnimationArena arena{size_t(oldIndex),size_t(oldIndex)+size_t(count)*DescriptorSize};
    size_t packedNativeBytes{};
    for(int i=0;i<count;++i)
    {
        const size_t at=i*DescriptorSize;
        Require(Word(descriptors,at+156)==0,"external native sequence groups are unsupported");
        const int anim=Word(descriptors,at+124),blends=Word(descriptors,at+120),frames=Word(descriptors,at+56);
        Require(anim>=244 && anim<textureData,"native animation outside studio payload");
        const auto end=std::upper_bound(nativeOffsets.begin(),nativeOffsets.end(),anim);
        Require(end!=nativeOffsets.end(),"native animation has no payload boundary");
        const auto block=model.subspan(anim,*end-anim);
        const auto used=Animation(block,bones,blends,frames);
        arena.begin=std::min(arena.begin,size_t(anim));
        arena.end=std::max(arena.end,size_t(anim)+used);
        nativeAnimations.push_back(RescaleAnimation(block.first(used),blends,frames,
            i>=FixedUnitFirstSequence?fixed:original,target));
        const auto aligned=(nativeAnimations.back().size()+3)&~size_t{3};
        Require(aligned<=MaxBytes-packedNativeBytes,"native animation packing exceeds budget");
        packedNativeBytes+=aligned;
    }
    // All conversions read immutable model bytes before any original slot is
    // reused. Independent output tracks retain padding and monotonic metadata.
    const auto alignedBegin=(arena.begin+3)&~size_t{3};
    const bool reuse=alignedBegin<=arena.end && packedNativeBytes<=arena.end-alignedBegin &&
        CanReuseAnimationArena(model,arena,sequences);
    size_t cursor=alignedBegin;
    for(int i=0;i<count;++i)
    {
        const size_t at=size_t(i)*DescriptorSize;
        auto& animation=nativeAnimations[i];
        const auto offset=reuse?cursor:out.size();
        Put(descriptors,at+124,int(offset));
        const int frames=Word(descriptors,at+56),blends=Word(descriptors,at+120);
        if(reuse)
        {
            std::copy(animation.begin(),animation.end(),out.begin()+offset);
            safety.push_back({std::uint32_t(offset),std::uint32_t(animation.size()),std::uint32_t(frames),
                std::uint32_t(blends),0,std::uint32_t(bones),1,0});
            cursor+=(animation.size()+3)&~size_t{3};
        }
        else
        {
            appendAnimation(std::move(animation),frames,blends);
            while(out.size()%4)out.push_back(std::byte{});
        }
    }

    for (const auto& file : sequences)
    {
        const auto seq = std::span<const std::byte>(file);
        const int added = Word(seq, 0), index = Word(seq, 4);
        Require(added > 0 && added <= MaxSequences - count && index >= 8, "invalid SEQ descriptor count");
        Range(seq, size_t(index), size_t(added) * DescriptorSize);
        Require(size_t(index) + size_t(added) * DescriptorSize == seq.size(), "SEQ descriptor trailer mismatch");
        std::vector<std::byte> records(seq.begin() + index, seq.end());
        size_t cursor = 8;
        // CSO stores all events/pivots before any bone animation data.
        for (int i = 0; i < added; ++i)
        {
            const size_t at = i * DescriptorSize;
            const int events = Word(records, at + 48), pivots = Word(records, at + 60);
            Require(events >= 0 && events <= 4096 && pivots >= 0 && pivots <= 4096, "invalid SEQ event count");
            Put(records, at + 52, int(out.size()));
            Range(seq,cursor,size_t(events)*76);Grow(out,seq.subspan(cursor,size_t(events)*76));
            cursor += size_t(events) * 76;
            Put(records, at + 64, int(out.size()));
            Range(seq,cursor,size_t(pivots)*20);Grow(out,seq.subspan(cursor,size_t(pivots)*20));
            cursor += size_t(pivots) * 20;
            Require(cursor <= size_t(index), "SEQ events overlap descriptors");
            Require(Word(records, at + 156) == 0, "external sequence groups in SEQ are unsupported");
        }
        for (int i = 0; i < added; ++i)
        {
            const size_t at = i * DescriptorSize;
            // Legacy absolute offsets carry only the spacing between animations.
            const int old = Word(seq, index + at + 124);
            const auto next = i + 1 == added ? size_t(index) : cursor +
                size_t(Word(seq, index + at + DescriptorSize + 124) - int64_t(old));
            Require(next >= cursor && next <= size_t(index), "invalid SEQ animation spacing");
            const auto block=seq.subspan(cursor,next-cursor);
            const int blends=Word(records,at+120),frames=Word(records,at+56);
            Require(blends>=1 && blends<=16 && frames>=1 && frames<=65536 && !block.empty(),
                "invalid SEQ animation dimensions");
            bool sampleable=true;
            try { Animation(block,bones,blends,frames); }
            catch(const std::runtime_error&)
            {
                if(policy!=SequenceReadPolicy::AllowUnsupportedTracks)throw;
                sampleable=false;
            }
            Put(records, at + 124, int(out.size()));
            if(sampleable)
                appendAnimation(RescaleAnimation(block,blends,frames,
                    count+i>=FixedUnitFirstSequence?fixed:original,target),frames,blends);
            else
                // Keep the exact authored block and descriptor number. Its
                // encoding does not match this model's bones; do not remap it.
                appendAnimation({block.begin(),block.end()},frames,blends,false);
            cursor = next;
        }
        Grow(descriptors, records);
        count += added;
    }
    // Relative metadata stays before the descriptors and texture image boundary,
    // so both stock loaders retain it when repacking texture data. It does not
    // borrow reserved studio-header fields or alter authored sequence indices.
    const auto safetyIndex=std::uint32_t(out.size());
    for(const auto& record:safety)Grow(out,std::as_bytes(std::span(record)));
    const std::array<std::uint32_t,8> footer{SequenceSafetyMagic0,SequenceSafetyMagic1,
        SequenceSafetyVersion,std::uint32_t(safety.size()),std::uint32_t(bones),safetyIndex,
        std::uint32_t(SequenceSafetyRecordBytes),0};
    Grow(out,std::as_bytes(std::span(footer)));
    Put(out, 164, count);
    Put(out, 168, int(out.size()));
    Grow(out, descriptors);
    if(textures>0)
    {
        const int shift=int(out.size())-textureData;
        Put(out,188,int(out.size()));
        for(int i=0;i<textures;++i)
        {
            const size_t at=size_t(textureIndex)+i*80+76;
            const int image=Word(model,at);
            Require(image>=textureData && image<length,"invalid texture image offset");
            Put(out,at,image+shift);
        }
        Grow(out,model.subspan(textureData));
    }
    Put(out, 72, int(out.size()));
    return out;
}
}
