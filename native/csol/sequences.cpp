// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequences.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace csol
{
namespace
{
constexpr size_t DescriptorSize = 176;
constexpr size_t MaxBytes = 128 * 1024 * 1024;
constexpr int MaxSequences = 2048;
constexpr std::array<float,6> ExternalScale{1.0f/256,1.0f/256,1.0f/256,
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
// CSO's external player tracks use fixed channel units, independently of
// mstudiobone_t::scale. A stock studio model has only one scale per bone.
// Requantize both sources into a common representable scale when packing.
// Keep the original RLE layout and verify any shared value is consistent.
std::vector<std::byte> RescaleAnimation(std::span<const std::byte> block,int blends,int frames,
    const BoneScales& source,const BoneScales& target)
{
    std::vector<std::byte> result(block.begin(),block.end());
    std::unordered_map<size_t,short> written;
    for(int blend=0;blend<blends;++blend)for(size_t bone=0;bone<source.size();++bone)
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
                    const auto [entry,inserted]=written.emplace(at,quantized);
                    Require(inserted || entry->second==quantized,"animation shares incompatible channel scales");
                    std::memcpy(result.data()+at,&quantized,2);
                }
                cursor+=(valid+1)*2;remaining-=total;
            }
        }
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
                cursor += (valid + 1) * 2;
                end=std::max(end,cursor);
                remaining -= total;
            }
        }
    }
    return end;
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
    const std::vector<std::vector<std::byte>>& sequences)
{
    Require(model.size() >= 244 && model.size() <= MaxBytes, "invalid studio size for SEQ append");
    Require(Word(model, 0) == 0x54534449 && Word(model, 4) == 10, "SEQ requires a decoded IDST model");
    const int length = Word(model, 72), bones = Word(model, 140);
    int count = Word(model, 164);
    Require(length >= 244 && size_t(length) <= model.size() && bones > 0 && bones <= 256,
        "invalid studio header for SEQ append");
    Require(count >= 1 && count <= MaxSequences && sequences.size() <= 256, "SEQ count exceeds budget");
    model = model.first(length);
    if(sequences.empty())return {model.begin(),model.end()};
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
    const int boneIndex=Word(model,144);
    Range(model,size_t(boneIndex),size_t(bones)*112);
    Require(size_t(boneIndex)+size_t(bones)*112<=size_t(textureData),"bones overlap texture payload");
    BoneScales original(bones),fixed(bones,ExternalScale),target(bones);
    for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)
    {
        const size_t at=size_t(boneIndex)+bone*112+88+axis*4;
        const float scale=Float(model,at);
        Require(std::isfinite(scale) && scale>=0,"invalid studio bone scale");
        original[bone][axis]=scale;
        target[bone][axis]=std::max(scale,ExternalScale[axis]);
        std::memcpy(out.data()+at,&target[bone][axis],4);
    }
    // Native MDL tracks retain their original units. Only the external SEQ
    // tracks use CSO's fixed units; changing bone scales alone breaks gait.
    for(int i=0;i<count;++i)
    {
        const size_t at=i*DescriptorSize;
        Require(Word(descriptors,at+156)==0,"external native sequence groups are unsupported");
        const int anim=Word(descriptors,at+124),blends=Word(descriptors,at+120),frames=Word(descriptors,at+56);
        Require(anim>=244 && anim<textureData,"native animation outside studio payload");
        const auto block=model.subspan(anim,textureData-anim);
        const auto used=Animation(block,bones,blends,frames);
        Put(descriptors,at+124,int(out.size()));
        Grow(out,RescaleAnimation(block.first(used),blends,frames,original,target));
        while(out.size()%4)out.push_back(std::byte{});
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
            Animation(block,bones,blends,frames);
            Put(records, at + 124, int(out.size()));
            Grow(out,RescaleAnimation(block,blends,frames,fixed,target));
            cursor = next;
        }
        Grow(descriptors, records);
        count += added;
    }
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
