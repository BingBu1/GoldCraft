// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/scenario/bosses/teddy_sliding.hpp"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sliding = csol::teddy_sliding;
namespace
{
unsigned checks{};
void Check(bool value, const char* description)
{
    ++checks;
    if (!value) throw std::runtime_error(description);
}
struct Random
{
    unsigned calls{};
    float operator()(float low, float high)
    {
        Check(calls < 4, "unexpected RNG call");
        Check(low == (calls == 3 ? 1.f : -1.f) && high == (calls == 3 ? 1.5f : 1.f), "original RNG bounds/order");
        constexpr float values[]{-1, -.5f, .75f, 1.25f};
        return values[calls++];
    }
};
std::vector<std::uint8_t> Decode(const std::string& raw)
{
    if (raw.size() % 2) throw std::runtime_error("odd hex packet");
    std::vector<std::uint8_t> bytes;
    for (std::size_t at = 0; at != raw.size(); at += 2)
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(raw.substr(at, 2), nullptr, 16)));
    return bytes;
}
std::vector<std::uint8_t> Packet()
{
    std::vector<std::uint8_t> bytes(sliding::Sprite.begin(), sliding::Sprite.end());
    bytes.push_back(0);
    const auto integer = [&](std::uint32_t value, unsigned width)
    { for (unsigned n = 0; n != width; ++n) bytes.push_back(static_cast<std::uint8_t>(value >> (n * 8))); };
    const auto real = [&](float value) { integer(std::bit_cast<std::uint32_t>(value), 4); };
    integer(7, 2);
    for (int v : {32, -40, 48, 56, -64, 72}) integer(static_cast<std::uint16_t>(v), 2);
    integer(3, 2); real(.1f); real(.5f); integer(0x30, 4);
    for (unsigned v : {0, 30, 5, 255, 255, 255, 255}) integer(v, 1);
    for (float v : {0.f, 50.f, 0.f, .2f, .2f, 0.f, 0.f}) real(v);
    return bytes;
}
void Cases()
{
    auto bytes = Packet();
    const auto event = sliding::Read(bytes);
    Check(event && event->owner == 7 && event->offset == sliding::Vec3{4, -5, 6} &&
        event->velocity == sliding::Vec3{7, -8, 9}, "original signed eighth coordinates");
    for (std::size_t n = 0; n != bytes.size(); ++n)
        Check(!sliding::Read(std::span(bytes).first(n)), "truncated packet accepted");
    bytes.push_back(0); Check(!sliding::Read(bytes), "trailing byte accepted"); bytes.pop_back();
    for (std::size_t at : {std::size_t{0}, sliding::Sprite.size(), sliding::Sprite.size() + 1, std::size_t{33}, std::size_t{50}})
    {
        auto changed = bytes; changed[at] ^= 7;
        if (at == sliding::Sprite.size() + 1) changed[at] = changed[at + 1] = 0;
        Check(!sliding::Read(changed), "unsupported emitter accepted");
    }
    Random random;
    auto p = sliding::Spawn(*event, {100, 200, 300}, 10.01f, random);
    Check(random.calls == 4, "zero spread skipped RNG");
    Check(p.origin == sliding::Vec3{104, 195, 306} && p.velocity == event->velocity, "owner offset initialization");
    Check(p.alpha == 250 && p.size == 50 && p.die == 10.01f + .5f, "original parser clamp and factory fields");
    Check(sliding::Tick(p, 10.31f, 10.01f, 800, 30, 15), "loop particle died before life");
    Check(p.frame == 9 && p.born == 10.01f && p.growth == .2f, "loop animation changed birth/growth");
    Check(!sliding::Tick(p, p.die, 10.31f, 800, 30, 15), "manager strict lifetime equality");
    Check(!sliding::Tick(p, p.die, p.die, 800, 30, 0), "invalid frame count accepted");
}
void EmitterOracle()
{
    std::string raw;
    while (std::cin >> raw)
    {
        auto event = sliding::Read(Decode(raw));
        Check(bool(event), "oracle packet outside Sliding contract");
        Random random;
        const auto p = sliding::Spawn(*event, {}, 10.01f, random);
        std::cout << "{\"owner\":" << event->owner << ",\"offset\":[" << event->offset[0] << ',' << event->offset[1] << ',' << event->offset[2]
            << "],\"velocity\":[" << p.velocity[0] << ',' << p.velocity[1] << ',' << p.velocity[2] << "],\"born\":" << p.born
            << ",\"die\":" << p.die << ",\"size\":" << p.size << ",\"growth\":" << p.growth << ",\"alpha\":" << p.alpha << ",\"roll\":" << p.roll << "}\n";
    }
}
void ParticleOracle()
{
    float a[19];
    while (std::cin >> a[0])
    {
        for (unsigned i = 1; i != 19; ++i) if (!(std::cin >> a[i])) throw std::runtime_error("incomplete particle row");
        sliding::Particle p;
        p.born = a[0]; p.die = a[1]; p.size = a[2]; p.growth = a[3]; p.alpha = a[4];
        for (unsigned i = 0; i != 3; ++i) { p.origin[i] = a[5 + i]; p.velocity[i] = a[8 + i]; }
        p.roll = a[14]; p.rotationVelocity = a[15]; p.frame = static_cast<int>(a[18]);
        const bool alive = sliding::Tick(p, a[11], a[12], a[13], static_cast<int>(a[16]), static_cast<int>(a[17]));
        std::cout << '[' << (alive ? 1 : 0) << ',' << p.size << ',' << p.alpha << ',' << p.roll << ',' << p.die;
        for (float v : p.origin) std::cout << ',' << v;
        for (float v : p.velocity) std::cout << ',' << v;
        std::cout << ',' << p.frame << ',' << p.born << ',' << p.growth << ',' << p.rotationVelocity << "]\n";
    }
}
void GeometryOracle()
{
    float a[10];
    while (std::cin >> a[0])
    {
        for (unsigned i = 1; i != 10; ++i) if (!(std::cin >> a[i])) throw std::runtime_error("incomplete geometry row");
        sliding::Particle p; p.origin = {a[0], a[1], a[2]}; p.size = a[3];
        const auto quad = sliding::Corners(p, {a[4], a[5], a[6]}, {a[7], a[8], a[9]});
        std::cout << '[';
        for (unsigned i = 0; i != 12; ++i) std::cout << (i ? "," : "") << quad[i / 3][i % 3];
        std::cout << "]\n";
    }
}
}
int main(int argc, char** argv)
{
    std::cout << std::setprecision(9);
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--emitter-oracle") EmitterOracle();
        else if (argc == 2 && std::string(argv[1]) == "--particle-oracle") ParticleOracle();
        else if (argc == 2 && std::string(argv[1]) == "--geometry-oracle") GeometryOracle();
        else { Cases(); std::cout << "{\"success\":true,\"checks\":" << checks << "}\n"; }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
