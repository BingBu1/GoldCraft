// CSOL format implementation carried from this project's HLAM research.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_codec.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace csol
{
namespace
{
using Word = std::uint32_t;
using Subkey = std::array<Word, 3>;
using Schedule = std::array<Subkey, 64>;
using Boxes = std::array<std::array<Word, 1024>, 4>;

// ICE-4 constants and CSOL format keys, verified against the engine's table
// initialization, key schedule and block decoder (64 rounds, 32-byte keys).
constexpr Word Permutation[32] = {
    1,128,1024,8192,524288,2097152,16777216,1073741824,
    8,32,256,16384,65536,8388608,67108864,536870912,
    4,16,512,32768,131072,4194304,134217728,268435456,
    2,64,2048,4096,262144,1048576,33554432,2147483648u};
constexpr Word Modulus[4][4] = {{333,313,505,369},{379,375,319,391},{361,445,451,397},{397,425,395,505}};
constexpr Word Xor[4][4] = {{131,133,155,205},{204,167,173,65},{75,46,212,51},{234,203,46,4}};
constexpr int Rotations[16] = {0,1,2,3,2,1,3,0,1,3,2,0,3,1,0,2};
constexpr unsigned char Keys[2][32] = {
    {0x32,0xa6,0x21,0xe0,0xab,0x6b,0xf4,0x2c,0x93,0xc6,0xf1,0x96,0xfb,0x38,0x75,0x68,0xba,0x70,0x13,0x86,0xe0,0xb3,0x71,0xf4,0xe3,0x9b,0x07,0x22,0x0c,0xfe,0x88,0x3a},
    {0x22,0x7a,0x19,0x6f,0x7b,0x86,0x7d,0xe0,0x8c,0xc6,0xf1,0x96,0xfb,0x38,0x75,0x68,0x88,0x7a,0x78,0x86,0x78,0x86,0x67,0x70,0xd9,0x91,0x07,0x3a,0x14,0x74,0xfe,0x22}};

Word Multiply(Word a, Word b, Word modulus)
{
    Word value = 0;
    while (b)
    {
        if (b & 1) value ^= a;
        a <<= 1;
        if (a & 256) a ^= modulus;
        b >>= 1;
    }
    return value;
}

Boxes MakeBoxes()
{
    Boxes boxes{};
    for (int i = 0; i < 1024; ++i)
    {
        const auto column = (i >> 1) & 255;
        const auto row = (i & 1) | ((i >> 8) & 2);
        for (int box = 0; box < 4; ++box)
        {
            const Word value = column ^ Xor[box][row];
            const auto modulus = Modulus[box][row];
            Word power = Multiply(value, value, modulus);
            power = Multiply(power, value, modulus);
            power = Multiply(power, power, modulus);
            power = Multiply(power, value, modulus) << (24 - box * 8);
            for (int bit = 0; bit < 32; ++bit)
                if (power & (Word{1} << bit)) boxes[box][i] |= Permutation[bit];
        }
    }
    return boxes;
}

Schedule MakeSchedule(int version)
{
    Schedule result{};
    const auto& key = Keys[version - 20];
    for (int group = 0; group < 4; ++group)
    {
        std::array<std::uint16_t, 4> words{};
        for (int i = 0; i < 4; ++i)
            words[3 - i] = (key[group * 8 + i * 2] << 8) | key[group * 8 + i * 2 + 1];
        const auto build = [&](int start, int rotation)
        {
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 15; ++j)
                    for (int k = 0; k < 4; ++k)
                    {
                        auto& word = words[(Rotations[rotation + i] + k) & 3];
                        const Word bit = word & 1;
                        auto& part = result[start + i][j % 3];
                        part = (part << 1) | bit;
                        word = (word >> 1) | ((bit ^ 1) << 15);
                    }
        };
        build(group * 8, 0);
        build(56 - group * 8, 8);
    }
    return result;
}

Word Round(Word value, const Subkey& key, const Boxes& boxes)
{
    Word left = ((value >> 16) & 1023) | (((value >> 14) | (value << 18)) & 0xffc00);
    Word right = (value & 1023) | ((value << 2) & 0xffc00);
    const Word mix = (left ^ right) & key[2];
    left ^= mix ^ key[0];
    right ^= mix ^ key[1];
    return boxes[0][left >> 10] | boxes[1][left & 1023] | boxes[2][right >> 10] | boxes[3][right & 1023];
}

Word BigWord(const std::byte* data)
{
    Word result = 0;
    for (int i = 0; i < 4; ++i) result = (result << 8) | std::to_integer<Word>(data[i]);
    return result;
}

void WriteBig(std::byte* data, Word value)
{
    for (int i = 3; i >= 0; --i) { data[i] = std::byte(value & 255); value >>= 8; }
}
}

void DecodeStudioPayload(std::span<std::byte> data, int version)
{
    if (version != 20 && version != 21) throw std::runtime_error("不支持的 CSOL 模型编码版本");
    static const auto boxes = MakeBoxes();
    static const std::array<Schedule, 2> schedules{MakeSchedule(20), MakeSchedule(21)};
    const auto& schedule = schedules[version - 20];
    while (!data.empty())
    {
        const auto size = std::min<std::size_t>(1024, data.size());
        // The engine leaves the entire final chunk untouched when not divisible
        // by eight, even if that chunk contains some complete cipher blocks.
        if (size % 8) break;
        for (std::size_t at = 0; at < size; at += 8)
        {
            auto left = BigWord(data.data() + at);
            auto right = BigWord(data.data() + at + 4);
            for (int round = 63; round > 0; round -= 2)
            {
                left ^= Round(right, schedule[round], boxes);
                right ^= Round(left, schedule[round - 1], boxes);
            }
            WriteBig(data.data() + at, right);
            WriteBig(data.data() + at + 4, left);
        }
        data = data.subspan(size);
    }
}

bool DecodeStudioModel(std::span<std::byte> data)
{
    const auto field = [&](std::size_t at)
    {
        if (at > data.size() || data.size() - at < 4) throw std::runtime_error("CSOL 模型文件头不完整");
        Word value = 0;
        for (int i = 0; i < 4; ++i) value |= std::to_integer<Word>(data[at + i]) << (i * 8);
        return value;
    };
    if (data.size() < 8 || field(0) != 0x54534449 || (field(4) != 20 && field(4) != 21)) return false;
    const int version = field(4);
    if (data.size() < 244 || field(72) < 244 || field(72) > data.size()) throw std::runtime_error("CSOL 模型长度无效");
    data = data.first(field(72));
    const auto range = [&](Word offset, std::uint64_t size)
    {
        if (size && (offset < 244 || offset > data.size() || size > data.size() - offset))
            throw std::runtime_error("CSOL 模型编码数据越界");
    };
    std::vector<std::pair<Word, std::size_t>> payloads;
    const auto textures = field(180), textureOffset = field(184);
    if (textures > 4096) throw std::runtime_error("CSOL 模型贴图数量无效");
    range(textureOffset, std::uint64_t(textures) * 80);
    for (Word i = 0; i < textures; ++i)
    {
        const auto at = textureOffset + i * 80;
        const auto width = field(at + 68), height = field(at + 72), offset = field(at + 76);
        if (!width || !height || width > 16384 || height > 16384) throw std::runtime_error("CSOL 模型贴图尺寸无效");
        const std::uint64_t bytes = std::uint64_t(width) * height + 768;
        range(offset, bytes);
        payloads.emplace_back(offset, std::size_t(bytes));
    }
    const auto bodies = field(204), bodyOffset = field(208);
    if (bodies > 256) throw std::runtime_error("CSOL 模型部件数量无效");
    range(bodyOffset, std::uint64_t(bodies) * 76);
    for (Word i = 0; i < bodies; ++i)
    {
        const auto body = bodyOffset + i * 76;
        const auto models = field(body + 64), modelOffset = field(body + 72);
        if (models > 4096) throw std::runtime_error("CSOL 模型子模型数量无效");
        range(modelOffset, std::uint64_t(models) * 112);
        for (Word j = 0; j < models; ++j)
        {
            const auto model = modelOffset + j * 112;
            const auto vertices = field(model + 80), offset = field(model + 88);
            const std::uint64_t bytes = std::uint64_t(vertices) * 12;
            range(offset, bytes);
            if (bytes) payloads.emplace_back(offset, std::size_t(bytes));
        }
    }
    for (const auto& [offset, size] : payloads) DecodeStudioPayload(data.subspan(offset, size), version);
    data[4] = std::byte{10};
    return true;
}
}
