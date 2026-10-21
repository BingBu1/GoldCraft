// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequences.hpp"
#include <cstring>
#include <stdexcept>

namespace csol
{
namespace
{
constexpr size_t DescriptorSize = 176;
constexpr size_t MaxBytes = 128 * 1024 * 1024;
constexpr int MaxSequences = 2048;

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
// Studio animation offsets are relative to each bone's 12-byte channel table.
// Validate the RLE too: a zero-length run would hang the original renderer.
void Animation(std::span<const std::byte> block, int bones, int blends, int frames)
{
    Require(blends >= 1 && blends <= 16 && frames >= 1 && frames <= 65536, "invalid SEQ animation dimensions");
    const size_t tables = size_t(bones) * blends * 12;
    Range(block, 0, tables);
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
                remaining -= total;
            }
        }
    }
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

    for (const auto& file : sequences)
    {
        const auto seq = std::span<const std::byte>(file);
        const int added = Word(seq, 0), index = Word(seq, 4);
        Require(added > 0 && added <= MaxSequences - count && index >= 8, "invalid SEQ descriptor count");
        Range(seq, size_t(index), size_t(added) * DescriptorSize);
        Require(size_t(index) + size_t(added) * DescriptorSize == seq.size(), "SEQ descriptor trailer mismatch");
        const size_t base = out.size();
        std::vector<std::byte> records(seq.begin() + index, seq.end());
        size_t cursor = 8;
        // CSO stores all events/pivots before any bone animation data.
        for (int i = 0; i < added; ++i)
        {
            const size_t at = i * DescriptorSize;
            const int events = Word(records, at + 48), pivots = Word(records, at + 60);
            Require(events >= 0 && events <= 4096 && pivots >= 0 && pivots <= 4096, "invalid SEQ event count");
            Put(records, at + 52, int(base + cursor));
            cursor += size_t(events) * 76;
            Put(records, at + 64, int(base + cursor));
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
            Animation(seq.subspan(cursor, next - cursor), bones, Word(records, at + 120), Word(records, at + 56));
            Put(records, at + 124, int(base + cursor));
            cursor = next;
        }
        Grow(out, seq.first(index));
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
