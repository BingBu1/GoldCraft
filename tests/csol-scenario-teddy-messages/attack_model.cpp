// SPDX-License-Identifier: GPL-3.0-or-later
// Compare the actual allocation-free builder with packets observed while
// executing original MP events. This reader follows the audited HW3785D920
// field order, independently of the production builder/compact encoders.
#include "../../native/csol/scenario/bosses/teddy_model_effect.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
namespace teddy = csol::scenario::teddy;
unsigned checks{}, cases{};
void Check(bool value, const char* why)
{ ++checks; if (!value) throw std::runtime_error(why); }
void Same(float a, float b)
{ Check(std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b), "float bits differ from original packet"); }
void Compare(const csol::skillfx::WorldEffect& actual, const csol::skillfx::WorldEffect& expected)
{
    for (unsigned axis = 0; axis != 3; ++axis)
    { Same(actual.model.origin[axis], expected.model.origin[axis]); Same(actual.angles[axis], expected.angles[axis]); }
    Check(actual.model.model == expected.model.model, "model identity");
    Check(actual.model.owner == expected.model.owner && actual.model.excludedOwner == expected.model.excludedOwner, "owner fields");
    Check(actual.model.sequence == expected.model.sequence && actual.model.frameMax == expected.model.frameMax, "animation fields");
    Same(actual.model.life, expected.model.life); Same(actual.model.frameRate, expected.model.frameRate);
    Same(actual.model.alpha, expected.model.alpha); Same(actual.model.fadeSpeed, expected.model.fadeSpeed);
    Same(actual.model.scale, expected.model.scale); Same(actual.model.fadeIn, expected.model.fadeIn);
    Check(actual.model.renderMode == expected.model.renderMode && actual.flags == expected.flags, "render fields");
}
struct Reader
{
    std::vector<std::uint8_t> bytes;
    std::size_t at{};
    explicit Reader(const std::string& hex)
    {
        Check(hex.size() == 96, "expected complete 48-byte original TE145 packet");
        for (std::size_t i = 0; i < hex.size(); i += 2)
        {
            unsigned value{};
            const auto parsed = std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
            Check(parsed.ec == std::errc{} && parsed.ptr == hex.data() + i + 2, "invalid packet hex");
            bytes.push_back(static_cast<std::uint8_t>(value));
        }
    }
    std::uint8_t Byte() { Check(at < bytes.size(), "packet truncated"); return bytes[at++]; }
    std::uint16_t Word() { const auto lo = Byte(); return static_cast<std::uint16_t>(lo | unsigned(Byte()) << 8); }
    std::uint32_t Long() { const auto lo = Word(); return lo | std::uint32_t(Word()) << 16; }
    float Coord() { return float(std::bit_cast<std::int16_t>(Word())) * .125f; }
    float Angle() { return float(std::bit_cast<std::int8_t>(Byte())) * (360.0f / 256.0f); }
    csol::skillfx::WorldEffect Read()
    {
        csol::skillfx::WorldEffect effect;
        Check(Byte() == 145, "wrong original tempentity");
        for (auto& v : effect.model.origin) v = Coord();
        for (auto& v : effect.angles) v = Angle();
        for (unsigned axis = 0; axis != 3; ++axis) Same(Coord(), 0);
        effect.model.model = Word();
        effect.model.life = static_cast<float>(double(std::bit_cast<std::int8_t>(Byte())) * .1);
        effect.model.sequence = Word();
        effect.model.frameRate = float(std::bit_cast<std::int16_t>(Word())) * .1f;
        Check(Byte() == 1, "original fade enabled");
        effect.model.alpha = Byte(); effect.model.renderMode = Byte();
        Check(Word() == 65535, "original owner absent");
        effect.model.owner = effect.model.excludedOwner = 0;
        effect.model.fadeSpeed = float(Byte()) * .1f;
        Check(Byte() == 0, "original fade-in disabled");
        effect.model.fadeIn = float(Byte()) * .1f;
        effect.model.scale = float(Byte()) * .1f;
        effect.model.frameMax = Word();
        Check(Long() == 0 && Long() == 0, "original additional flags absent");
        Check(Byte() == 0, "original ordinary allocator");
        Check(Long() == 0 && Byte() == 0, "original auxiliary and excluded-player fields absent");
        Check(at == bytes.size(), "unread original fields");
        effect.flags = 0;
        return effect;
    }
};
void Original(const char* path)
{
    std::ifstream input(path); Check(bool(input), "open original fixture");
    std::size_t count{}; std::string line;
    Check(bool(std::getline(input, line)), "fixture header missing");
    std::istringstream header(line);
    Check(bool(header >> count) && count > 0 && count <= 100000, "invalid original case count");
    header >> std::ws; Check(header.eof(), "extra header fields");
    unsigned kinds[2]{};
    for (std::size_t row = 0; row != count; ++row)
    {
        Check(bool(std::getline(input, line)), "fixture row missing");
        std::istringstream fields(line);
        int kind{}, model{}; std::array<float, 3> origin{}, angles{}; std::string hex;
        Check(bool(fields >> kind >> origin[0] >> origin[1] >> origin[2] >> angles[0] >> angles[1] >> angles[2] >> model >> hex), "partial original row");
        fields >> std::ws; Check(fields.eof(), "extra original row fields");
        Check(kind == 0 || kind == 1, "unknown original event");
        const auto effect = teddy::AttackModelEffect(kind == 0 ? teddy::AttackModelVisual::Attack2 : teddy::AttackModelVisual::Blackhole2, origin, angles, model);
        Check(effect.has_value(), "valid original call rejected");
        const auto expected = Reader(hex).Read();
        Compare(*effect, expected);
        // Catch the common TE145 first-byte/sequence confusion explicitly.
        Check(effect->model.sequence == 1 && effect->model.life == .2f && effect->model.fadeSpeed == (kind == 0 ? 1 : 3), "event-specific life/sequence/fade");
        const auto encoded = csol::skillfx::Encode(*effect);
        Check(encoded.size() == csol::skillfx::WorldBytes, "portable message size");
        csol::skillfx::WorldEffect decoded;
        Check(csol::skillfx::Decode(encoded, decoded), "portable message rejected");
        Compare(decoded, expected);
        ++kinds[kind]; ++cases;
    }
    input >> std::ws; Check(input.eof(), "extra original rows");
    Check(kinds[0] > 0 && kinds[1] > 0, "both original event paths required");
}
void Boundaries()
{
    using enum teddy::AttackModelVisual;
    const std::array<float, 3> zero{};
    for (const auto visual : {Attack2, Blackhole2})
    {
        for (const int model : {-1, 0, 32768, std::numeric_limits<int>::max()})
            Check(!teddy::AttackModelEffect(visual, zero, zero, model), "invalid model accepted");
        for (unsigned axis = 0; axis != 3; ++axis)
        {
            for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()})
            {
                auto value = zero; value[axis] = invalid;
                Check(!teddy::AttackModelEffect(visual, value, zero, 1), "invalid coordinate accepted");
                Check(!teddy::AttackModelEffect(visual, zero, value, 1), "invalid angle accepted");
            }
            auto position = zero; position[axis] = 268435456.0f;
            Check(!teddy::AttackModelEffect(visual, position, zero, 1), "int32 coordinate overflow accepted");
            position[axis] = -268435456.0f;
            const auto edge = teddy::AttackModelEffect(visual, position, zero, 1);
            Check(edge && edge->model.origin[axis] == 0, "valid int32 minimum did not retain low16 wrap");
        }
    }
    Check(!teddy::AttackModelEffect(static_cast<teddy::AttackModelVisual>(-1), zero, zero, 1), "invalid visual accepted");
}
}
int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "original packet fixture required");
        Original(argv[1]); Boundaries();
        std::cout << "originalCases=" << cases << " checks=" << checks << " failures=0\n";
        return 0;
    }
    catch (const std::exception& e)
    { std::cerr << "ERROR " << e.what() << " checks=" << checks << " failures=1\n"; return 1; }
}
