// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/magazine_model.hpp"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

using vec3_t = float[3];
using byte = unsigned char;
using int32 = std::int32_t;
#include "external/ReGameDLL_CS/regamedll/engine/studio.h"

using namespace guest::cof;
static_assert(sizeof(void*) == 4 && sizeof(studiohdr_t) == 244 && sizeof(mstudioseqdesc_t) == 176);
static_assert(offsetof(studiohdr_t, length) == 72 && offsetof(studiohdr_t, numseq) == 164 &&
    offsetof(studiohdr_t, seqindex) == 168 && offsetof(mstudioseqdesc_t, fps) == 32 &&
    offsetof(mstudioseqdesc_t, numframes) == 56 && MAXSTUDIOSEQUENCES == 2048);

namespace
{
unsigned checks;
void Check(bool value, int line)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL line%d\n", line); std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
using Blob = std::array<std::byte, sizeof(studiohdr_t) + sizeof(mstudioseqdesc_t)>;
Blob Model(int frames = 31, float fps = 30)
{
    Blob data{};
    studiohdr_t header{};
    header.id = 0x54534449; header.version = 10; header.length = data.size();
    header.numseq = 1; header.seqindex = sizeof(header);
    mstudioseqdesc_t sequence{}; sequence.numframes = frames; sequence.fps = fps;
    std::memcpy(data.data(), &header, sizeof(header));
    std::memcpy(data.data() + sizeof(header), &sequence, sizeof(sequence));
    return data;
}
void Word(Blob& data, unsigned at, std::uint32_t word) { std::memcpy(data.data() + at, &word, 4); }
void Safety()
{
    MagazineSequenceTimes times{};
    const auto valid = Model();
    CHECK(PrepareMagazineSequenceTimes(valid, 1, times) == MagazineModelResult::Prepared);
    CHECK(times.present == 1 && times.duration[0] == 1 && times.duration[42] == 0);
    const auto previous = times;
    const auto reject = [&](const auto& bytes, unsigned count, MagazineModelResult wanted) {
        CHECK(PrepareMagazineSequenceTimes(bytes, count, times) == wanted);
        CHECK(times.present == previous.present && times.duration == previous.duration);
    };
    reject(valid, 0, MagazineModelResult::InvalidInput);
    reject(valid, 44, MagazineModelResult::InvalidInput);
    reject(valid, 2, MagazineModelResult::MissingSequence);
    reject(std::span<const std::byte>{}, 1, MagazineModelResult::InvalidModel);
    reject(std::span(valid).first(243), 1, MagazineModelResult::InvalidModel);
    auto bad = valid;
    for (const auto [at, word] : std::array<std::pair<unsigned, std::uint32_t>, 10>{{
        {0, 0}, {4, 9}, {72, 0}, {164, 0}, {164, 2049}, {164, 0x80000000u},
        {168, 0}, {168, 243}, {168, 0x7fffffffu}, {168, 245}}})
    { bad = valid; Word(bad, at, word); reject(bad, 1, MagazineModelResult::InvalidModel); }
    for (const float fps : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::denorm_min()})
        reject(Model(31, fps), 1, MagazineModelResult::InvalidTiming);
    reject(Model(0), 1, MagazineModelResult::InvalidTiming);
    reject(Model(-1), 1, MagazineModelResult::InvalidTiming);
    CHECK(PrepareMagazineSequenceTimes(Model(1), 1, times) == MagazineModelResult::Prepared);
    CHECK(times.present == 1 && std::bit_cast<std::uint32_t>(times.duration[0]) == 0);
    std::printf("checks=%u\n", checks);
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--safety") == 0) { Safety(); return 0; }
    if (argc >= 3 && std::strcmp(argv[1], "--models") == 0)
    {
        for (int m = 2; m < argc; ++m)
        {
            std::ifstream file(argv[m], std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
            CHECK(bytes.size() >= sizeof(studiohdr_t));
            studiohdr_t header; std::memcpy(&header, bytes.data(), sizeof(header));
            MagazineSequenceTimes times;
            CHECK(header.numseq > 0 && header.numseq <= int(times.duration.size()));
            const auto data = std::as_bytes(std::span(bytes));
            CHECK(PrepareMagazineSequenceTimes(data, header.numseq, times) == MagazineModelResult::Prepared);
            CHECK(times.present == (std::uint64_t{1} << header.numseq) - 1);
            for (int i = 0; i < header.numseq; ++i)
                std::printf("%d %d %08x\n", m - 2, i, std::bit_cast<unsigned>(times.duration[i]));
        }
        return 0;
    }
    if (argc != 2 || std::strcmp(argv[1], "--oracle") != 0) return 3;
    int frames; unsigned fps;
    while (std::scanf("%d %x", &frames, &fps) == 2)
    {
        const auto data = Model(frames, std::bit_cast<float>(fps));
        MagazineSequenceTimes times;
        const auto result = PrepareMagazineSequenceTimes(data, 1, times);
        std::printf("%u %08x\n", unsigned(result), std::bit_cast<unsigned>(times.duration[0]));
    }
}
