// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/scenario/bosses/teddy_blackhole_smoke.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace bh = csol::teddy_blackhole_smoke;
unsigned checks{};
void Check(bool value, const char *text)
{
    ++checks;
    if (!value)
        throw std::runtime_error(text);
}
std::vector<std::uint8_t> Decode(const std::string &text)
{
    std::vector<std::uint8_t> out;
    Check(text.size() % 2 == 0, "odd hex");
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return out;
}
constexpr const char *Packet = "58737072697465732f736d6f6b655f69612e737072000000c842c0b8323e0000204"
                               "205000000ec51b83d0000803f32000000460000000ad7a33c0000003f0200000005"
                               "0000000a000000010000003400000024000000000048429696962103bff96009";
struct Random
{
    unsigned calls{};
    bool ring{};
    float operator()(float low, float high)
    {
        unsigned n = calls++ % (ring ? 3 : 4);
        if (ring)
        {
            constexpr float lo[]{-32, 50, 2}, hi[]{32, 70, 5}, values[]{0, 60, 3.5f};
            Check(low == lo[n] && high == hi[n], "ring RNG order");
            return values[n];
        }
        constexpr float values[]{-1, -.5f, .75f, 1.25f};
        Check(low == (n == 3 ? 1.f : -1.f) && high == (n == 3 ? 1.5f : 1.f), "spawn RNG order");
        return values[n];
    }
};
void Bit(float value) { std::cout << std::bit_cast<std::uint32_t>(value); }
void State(const bh::Particle &p)
{
    std::cout << '[';
    bool first = true;
    auto value = [&](float v)
    {
        if (!first)
            std::cout << ',';
        first = false;
        Bit(v);
    };
    for (float v : {p.born, p.die, p.size, p.initialSize, p.growth, p.alpha, p.mass})
        value(v);
    for (const auto *vec :
         {&p.origin, &p.velocity, &p.lastPosition, &p.angles, &p.rotationVelocity})
        for (float v : *vec)
            value(v);
    value(p.lastUpdate);
    std::cout << ',' << p.internalFlags << ',' << p.frame << ',' << p.Mode << ']';
}
struct World
{
    bh::Trace trace{};
    unsigned calls{};
    bh::Vec3 start{}, end{};
    bh::Trace operator()(int hull, const bh::Vec3 &a, const bh::Vec3 &b, int flags, int ignore)
    {
        Check(hull == 2 && flags == 10 && ignore == -1, "trace API arguments");
        ++calls;
        start = a;
        end = b;
        return trace;
    }
};
struct Camera
{
    unsigned calls{};
    bh::Vec3 operator()()
    {
        ++calls;
        return {27, -49, 0};
    }
};
struct Angles
{
    unsigned calls{};
    void operator()(const bh::Vec3 &a)
    {
        ++calls;
        Check(a == bh::Vec3{27, -49, 0}, "camera aggregate/order");
    }
};
void Updates()
{
    float a[35];
    while (std::cin >> a[0])
    {
        for (unsigned i = 1; i < 35; ++i)
            Check(bool(std::cin >> a[i]), "incomplete state");
        bh::Particle p;
        p.born = a[0];
        p.die = a[1];
        p.size = a[2];
        p.initialSize = a[3];
        p.growth = a[4];
        p.alpha = a[5];
        p.mass = a[6];
        for (unsigned i = 0; i < 3; ++i)
        {
            p.origin[i] = a[7 + i];
            p.velocity[i] = a[10 + i];
            p.lastPosition[i] = a[13 + i];
            p.angles[i] = a[16 + i];
            p.rotationVelocity[i] = a[19 + i];
        }
        p.lastUpdate = a[22];
        p.internalFlags = static_cast<int>(a[23]);
        p.frame = static_cast<int>(a[24]);
        World w;
        w.trace.fraction = a[28];
        for (unsigned i = 0; i < 3; ++i)
        {
            w.trace.normal[i] = a[29 + i];
            w.trace.endPosition[i] = a[32 + i];
        }
        Camera c;
        Angles av;
        const bool alive = bh::Tick(p, a[25], a[26], a[27], w, c, av);
        std::cout << "{\"state\":";
        State(p);
        std::cout << ",\"alive\":" << (alive ? 1 : 0) << ",\"calls\":[" << w.calls << ',' << c.calls
                  << ',' << av.calls << "],\"trace\":[";
        bool first = true;
        for (const auto *v : {&w.start, &w.end})
            for (float n : *v)
            {
                if (!first)
                    std::cout << ',';
                first = false;
                Bit(n);
            }
        std::cout << "]}\n";
    }
}
void Ring()
{
    std::string raw;
    while (std::cin >> raw)
    {
        auto ring = bh::Read(Decode(raw));
        Check(bool(ring), "outside fixed smoke contract");
        struct Trig
        {
            double input[72]{}, output[72]{};
            unsigned at{};
            double next(double value, bool sine)
            {
                Check((at % 2 == 1) == sine && at < 72 && input[at] == value, "trig input/order");
                return output[at++];
            }
            double cosine(double v) { return next(v, false); }
            double sine(double v) { return next(v, true); }
        } trig;
        for (unsigned i = 0; i < 72; ++i)
            Check(bool(std::cin >> trig.input[i] >> trig.output[i]), "missing numeric boundary");
        Random random;
        random.ring = true;
        bh::Emitters emitters;
        auto add = [&](const bh::Emitter &e) { Check(emitters.Add(e), "ring pool capacity"); };
        bh::Prepare(*ring, 10.f, trig, random, add);
        Check(trig.at == 72 && random.calls == 108, "ring callback count");
        std::cout << "{\"emitters\":[";
        for (unsigned i = 0; i < 36; ++i)
        {
            if (i)
                std::cout << ',';
            const auto &e = emitters.slots[i];
            std::cout << '[';
            bool first = true;
            for (float v : {e.origin[0], e.origin[1], e.origin[2], e.velocity[0], e.velocity[1],
                            e.velocity[2], e.spread, e.size, e.growth, e.next})
            {
                if (!first)
                    std::cout << ',';
                first = false;
                Bit(v);
            }
            std::cout << ',' << e.remaining << ']';
        }
        std::cout << "],\"frames\":[";
        unsigned total{};
        bh::Particle first, last;
        for (float now : {10.f, 10.01f, 10.2f, 10.4f, 10.6f, 10.8f, 11.f})
        {
            unsigned made{};
            Random spawn;
            auto factory = [&](const bh::Emitter &e, float time)
            {
                last = bh::Spawn(e, time, spawn);
                if (total++ == 0)
                    first = last;
                ++made;
                return true;
            };
            emitters.Frame(now, factory);
            if (now != 10.f)
                std::cout << ',';
            std::cout << '[' << made << ',' << spawn.calls << ',' << emitters.slots[0].remaining
                      << ',' << emitters.slots[35].remaining << ']';
        }
        std::cout << "],\"first\":";
        State(first);
        std::cout << ",\"last\":";
        State(last);
        std::cout << "}\n";
    }
}
void Geometry()
{
    float a[13];
    while (std::cin >> a[0])
    {
        for (unsigned i = 1; i < 13; ++i)
            Check(bool(std::cin >> a[i]), "incomplete geometry");
        bh::Particle p;
        p.origin = {a[0], a[1], a[2]};
        p.size = a[3];
        p.alpha = a[4];
        p.die = a[5];
        auto draw = bh::PrepareDraw(p, a[6], {a[7], a[8], a[9]}, {a[10], a[11], a[12]});
        std::cout << '[';
        if (draw)
        {
            std::cout << draw->Mode;
            for (float v : draw->color)
            {
                std::cout << ',';
                Bit(v);
            }
            for (auto &corner : draw->corners)
                for (float v : corner)
                {
                    std::cout << ',';
                    Bit(v);
                }
        }
        std::cout << "]\n";
    }
}
void Manager()
{
    std::cout << "{\"failures\":[";
    for (unsigned fail : {1u, 20u, 36u})
    {
        bh::Emitters pool;
        bh::Emitter e;
        e.remaining = 5;
        e.next = 10;
        e.size = 60;
        e.growth = 3.5f;
        for (unsigned i = 0; i < 36; ++i)
            pool.Add(e);
        unsigned attempted{}, created{};
        Random random;
        auto factory = [&](const bh::Emitter &input, float now)
        {
            if (++attempted == fail)
                return false;
            bh::Spawn(input, now, random);
            ++created;
            return true;
        };
        pool.Frame(10.01f, factory);
        if (fail != 1)
            std::cout << ',';
        std::cout << '[' << fail;
        for (unsigned i = 0; i < 36; ++i)
            std::cout << ',' << pool.slots[i].remaining;
        for (unsigned i = 0; i < 36; ++i)
        {
            std::cout << ',';
            Bit(pool.slots[i].next);
        }
        std::cout << ',' << created << ',' << random.calls << ']';
    }
    bh::Emitters pool;
    bh::Emitter e;
    e.remaining = 5;
    e.next = 10;
    unsigned count{};
    for (; count < 128 && pool.Add(e); ++count)
    {
    }
    const bool fullAccepted = pool.Add(e);
    pool.slots[3].remaining = pool.slots[7].remaining = 0;
    e.origin = {1000, 2000, 3000};
    pool.Add(e);
    unsigned first = 128;
    for (unsigned i = 0; i < 128; ++i)
        if (pool.slots[i].origin == e.origin)
        {
            first = i;
            break;
        }
    std::cout << "],\"capacity\":[" << count << ',' << (fullAccepted ? 1 : 0) << ',' << first << ','
              << pool.slots[7].remaining << "]}\n";
}
void Cases()
{
    auto bytes = Decode(Packet);
    auto ring = bh::Read(bytes);
    Check(bool(ring), "actual99B packet");
    for (std::size_t i = 0; i < bytes.size(); ++i)
        Check(!bh::Read(std::span(bytes).first(i)), "truncated packet");
    auto extra = bytes;
    extra.push_back(0);
    Check(!bh::Read(extra), "trailing byte");
    for (std::size_t i = 0; i < 93; ++i)
    {
        auto changed = bytes;
        changed[i] ^= 1;
        Check(!bh::Read(changed), "changed fixed field");
    }
    bh::Emitters pool;
    bh::Emitter e;
    e.remaining = 5;
    e.next = 10;
    for (unsigned i = 0; i < 128; ++i)
        Check(pool.Add(e), "128 slots");
    Check(!pool.Add(e), "full pool");
    pool.slots[3].remaining = pool.slots[7].remaining = 0;
    e.size = 61;
    Check(pool.Add(e) && pool.slots[3].size == 61 && pool.slots[7].remaining == 0, "first free");
    unsigned called{};
    auto fail = [&](const bh::Emitter &, float)
    {
        ++called;
        return false;
    };
    pool.Frame(10, fail);
    Check(called == 0, "strict equality");
    pool.Frame(10.01f, fail);
    Check(called == 1 && pool.slots[0].remaining == 4 && pool.slots[1].remaining == 5,
          "factory failure ends pass");
    Check(pool.slots[0].next == 10.01f + .09f, "advance before factory");
    bh::Emitters zero;
    e.next = 0;
    zero.Add(e);
    zero.Frame(20, fail);
    Check(called == 1, "zero next skipped");
    bh::Particle p;
    p.die = 11;
    Check(!bh::Expired(p, 10) && bh::Expired(p, 11), "expiry equality");
}
int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--manager-oracle")
            Manager();
        else if (argc == 2 && std::string(argv[1]) == "--particle-oracle")
            Updates();
        else if (argc == 2 && std::string(argv[1]) == "--ring-oracle")
            Ring();
        else if (argc == 2 && std::string(argv[1]) == "--geometry-oracle")
            Geometry();
        else
        {
            Cases();
            std::cout << "{\"success\":true,\"checks\":" << checks << "}\n";
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
