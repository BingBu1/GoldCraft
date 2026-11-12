// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/scenario/core/particle_runtime.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace runtime = csol::particles;
using runtime::Vec3;
struct Particle
{
    unsigned id{};
    bool visible{true}, changeDie{};
    float die{}, afterDie{};
    Vec3 origin{}, move{};
};
using Pool = runtime::OrdinaryPool<Particle>;
unsigned checks{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float Real()
{
    std::uint32_t bits{};
    if (!(std::cin >> bits)) throw std::runtime_error("incomplete input");
    return std::bit_cast<float>(bits);
}
struct Callbacks
{
    Pool& pool;
    std::vector<std::vector<unsigned>> events;
    std::vector<float> clocks;
    Vec3 camera{};
    std::size_t clockAt{};
    int clearAt{-1};
    bool recurse{};
    void Boundary(int at)
    {
        if (clearAt == at) pool.Clear();
        if (recurse)
        {
            Check(!pool.Allocate(true), "recursive allocation changes active traversal");
            Check(!pool.Frame(*this), "recursive frame");
        }
    }
    float Time()
    {
        Check(!clocks.empty(), "missing clock fixture");
        float now = clocks[std::min(clockAt++, clocks.size() - 1)];
        events.push_back({0, Bits(now)});
        Boundary(0);
        return now;
    }
    bool Visible(Particle& p)
    {
        events.push_back({1, p.id});
        Boundary(1);
        return p.visible;
    }
    Vec3 SortOrigin()
    {
        events.push_back({2});
        Boundary(2);
        return camera;
    }
    Vec3 Position(Particle& p) { Boundary(3); return p.origin; }
    void Update(Particle& p, float now)
    {
        events.push_back({3, p.id, Bits(now)});
        for (unsigned i = 0; i != 3; ++i) p.origin[i] += p.move[i];
        if (p.changeDie) p.die = p.afterDie;
        Boundary(4);
    }
    float Die(Particle& p) { Boundary(5); return p.die; }
    void Retire(Particle& p) { events.push_back({4, p.id}); Boundary(6); }
    void Draw(Particle& p) { events.push_back({5, p.id}); Boundary(7); }
};
void PrintEvents(const std::vector<std::vector<unsigned>>& events)
{
    std::cout << '[';
    bool first = true;
    for (const auto& row : events)
    {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[';
        for (unsigned i = 0; i != row.size(); ++i)
        {
            if (i) std::cout << ',';
            std::cout << row[i];
        }
        std::cout << ']';
    }
    std::cout << ']';
}
void SetPrevious(Pool& pool, float previous)
{
    pool.Clear();
    Callbacks callbacks{pool};
    callbacks.clocks = {previous};
    Check(pool.Frame(callbacks), "seed previous time");
}
void FrameInput()
{
    auto pool = std::make_unique<Pool>();
    std::size_t count{};
    while (std::cin >> count)
    {
        const float previous = Real();
        SetPrevious(*pool, previous);
        Callbacks callbacks{*pool};
        for (float& v : callbacks.camera) v = Real();
        std::size_t clockCount{};
        std::cin >> clockCount;
        for (std::size_t i = 0; i != clockCount; ++i) callbacks.clocks.push_back(Real());
        std::vector<Particle*> particles;
        for (std::size_t i = 0; i != count; ++i)
        {
            auto* p = pool->Allocate(true);
            Check(p != nullptr, "fixture allocation");
            *p = {};
            unsigned visible{}, changeDie{};
            std::cin >> p->id >> visible;
            p->visible = visible != 0;
            p->die = Real();
            for (float& v : p->origin) v = Real();
            for (float& v : p->move) v = Real();
            std::cin >> changeDie;
            p->changeDie = changeDie != 0;
            p->afterDie = Real();
            particles.push_back(p);
        }
        Check(pool->Frame(callbacks), "ordinary frame");
        std::cout << "{\"events\":";
        PrintEvents(callbacks.events);
        std::cout << ",\"active\":[";
        bool first = true;
        auto visit = [&](const Particle& p)
        {
            if (!first) std::cout << ',';
            first = false;
            std::cout << p.id;
        };
        pool->VisitActive(visit);
        std::cout << "],\"count\":" << pool->ActiveCount() << ",\"drawCount\":" << pool->DrawCount()
                  << ",\"previous\":" << Bits(pool->PreviousTime()) << ",\"particles\":[";
        for (unsigned i = 0; i != particles.size(); ++i)
        {
            if (i) std::cout << ',';
            auto& p = *particles[i];
            std::cout << '[' << p.id << ',' << Bits(p.die);
            for (float v : p.origin) std::cout << ',' << Bits(v);
            std::cout << ']';
        }
        std::cout << "]}\n";
    }
}
void CameraInput()
{
    auto pool = std::make_unique<Pool>();
    std::uint32_t previousBits{};
    while (std::cin >> previousBits)
    {
        const float previous = std::bit_cast<float>(previousBits);
        SetPrevious(*pool, previous);
        auto initial = [&] { return previous == 0.f ? 1.f : 0.f; };
        Check(pool->SetEnvironment(1, {1, 2, 3}, initial), "initial environment");
        const float sample = Real(), gravity = Real();
        Vec3 camera;
        for (float& v : camera) v = Real();
        auto clock = [&] { return sample; };
        Check(pool->SetEnvironment(gravity, camera, clock), "environment");
        std::cout << '[' << Bits(pool->Gravity());
        for (float v : pool->Camera()) std::cout << ',' << Bits(v);
        std::cout << ',' << Bits(pool->PreviousTime()) << "]\n";
    }
}
void Units()
{
    auto pool = std::make_unique<Pool>();
    Check(!pool->Allocate(true), "unprepared allocation");
    pool->Prepare();
    Check(!pool->Allocate(false), "disabled gate");
    Check(pool->ActiveCount() == 0, "disabled gate side effect");
    std::vector<Particle*> initial;
    for (unsigned i = 0; i != runtime::OrdinaryCapacity; ++i)
    {
        auto* p = pool->Allocate(true);
        Check(p != nullptr, "ordinary slot");
        *p = {};
        p->id = i;
        p->die = 10;
        initial.push_back(p);
    }
    Check(!pool->Allocate(true) && pool->ActiveCount() == 1200, "ordinary cap");
    Callbacks cb{*pool};
    cb.clocks = {10};
    Check(pool->Frame(cb), "full frame");
    Check(pool->ActiveCount() == 0 && pool->DrawCount() == 1200, "retired visible draw queue");
    unsigned retired{}, drawn{};
    for (const auto& row : cb.events)
    {
        retired += row[0] == 4;
        drawn += row[0] == 5;
    }
    Check(retired == 1200 && drawn == 1200, "queued payloads survive retirement");
    Check(pool->Allocate(true) == initial[0], "free-head reuse order");
    pool->Clear();
    Check(pool->Allocate(true) == initial[0], "cold vector rebuild order");
    Check(initial[0]->id == 0 && initial[0]->die == 10, "clear overwrote payload");
    Check(pool->PreviousTime() == 10, "clear reset independent clock");

    for (int boundary = 0; boundary != 8; ++boundary)
    {
        SetPrevious(*pool, 9);
        auto* p = pool->Allocate(true);
        *p = {};
        p->die = 10;
        Callbacks callbacks{*pool};
        callbacks.clocks = {10};
        callbacks.clearAt = boundary;
        Check(!pool->Frame(callbacks), "callback Clear did not abort frame");
        Check(pool->ActiveCount() == 0 && pool->PreviousTime() == 9, "cleared frame continued");
        Check(pool->Allocate(true) != nullptr, "busy guard not restored");
    }
    SetPrevious(*pool, 9);
    *pool->Allocate(true) = {};
    Callbacks nested{*pool};
    nested.clocks = {10};
    nested.recurse = true;
    Check(pool->Frame(nested), "nonrecursive owner frame");
    auto clock = [&] { pool->Clear(); return 20.f; };
    const Vec3 before = pool->Camera();
    Check(!pool->SetEnvironment(700, {8, 9, 10}, clock), "camera callback clear");
    Check(pool->Camera() == before && pool->Gravity() == 700, "environment ordering");
    std::cout << "{\"checks\":" << checks << ",\"failures\":0}\n";
}
int main(int argc, char** argv)
{
    try
    {
        const std::string command = argc > 1 ? argv[1] : "unit";
        if (command == "frame") FrameInput();
        else if (command == "camera") CameraInput();
        else if (command == "compare")
        {
            std::uint32_t a{}, b{};
            while (std::cin >> a >> b)
                std::cout << runtime::CompareDistance(std::bit_cast<float>(a), std::bit_cast<float>(b)) << '\n';
        }
        else Units();
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
