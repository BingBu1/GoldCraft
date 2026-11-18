// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/famas_aim.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace { unsigned allocations{}, checks{}; }
void* operator new(std::size_t size)
{
    ++allocations;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace
{
using namespace guest::cof;
using Word = std::uint32_t;
Word Bits(float value) { return std::bit_cast<Word>(value); }
float Float(Word value) { return std::bit_cast<float>(value); }
void Check(bool value, int line)
{
    ++checks;
    if (!value) { std::cerr << "Famas aim failed at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
struct Context
{
    MagazineActionState state;
    MagazineSequenceTimes times;
    FamasAimMetadata metadata{{1.25F, -2.5F, 3.75F}, 55};
    float now{13.37F}, fov{90};
    unsigned operation{}, sequence{}, change{}, nth{}, alt{}, count{}, cancelAt{}, rejectAt{};
    bool alive{true}, pressed{true}, toggle{true};
    std::array<std::array<Word, 16>, 5> events{};
    std::array<unsigned, 5> sizes{};
    std::array<Word, 9> Snapshot() const noexcept
    {
        return {Word(state.clip), state.aiming, Bits(state.primary), Bits(state.secondary),
            Bits(state.extra), Bits(state.idle), Bits(fov), sequence, Bits(now)};
    }
    template<class... T> bool Event(unsigned kind, T... values) noexcept
    {
        CHECK(count < events.size());
        auto& event = events[count]; sizes[count++] = 2 + sizeof...(T) + 9;
        event[0] = kind; event[1] = sizeof...(T) + 9;
        const std::array<Word, sizeof...(T)> args{Word(values)...};
        std::copy(args.begin(), args.end(), event.begin() + 2);
        const auto snapshot = Snapshot();
        std::copy(snapshot.begin(), snapshot.end(), event.begin() + 2 + args.size());
        if (count == cancelAt) { alive = false; return true; }
        if (count == rejectAt) return false;
        if (count == nth)
        {
            if (change & 1) now += .125F;
            if (change & 2) state.clip = !state.clip;
            if (change & 4) sequence = alt;
        }
        return true;
    }
    static bool Current(void* p) noexcept { return static_cast<Context*>(p)->alive; }
    static float Time(void* p) noexcept { return static_cast<Context*>(p)->now; }
    static bool Effect(void* p, const FamasAimEvent& event) noexcept
    {
        auto& c = *static_cast<Context*>(p);
        switch (event.kind)
        {
        case FamasAimEventKind::Presentation:
            return c.Event(0, Bits(event.offset[0]), Bits(event.offset[1]), Bits(event.offset[2]), event.value, event.enabled);
        case FamasAimEventKind::CameraZoom: return c.Event(1, Bits(event.amount), 0, 0);
        case FamasAimEventKind::PlayerFov: c.fov = event.amount; return c.Event(2, Bits(c.fov));
        case FamasAimEventKind::ViewAnimation:
            if (!c.Event(3, event.value)) return false;
            c.sequence = event.value; return true;
        }
        return false;
    }
    static bool Duration(void* p, float& out) noexcept
    {
        auto& c = *static_cast<Context*>(p); const auto sequence = c.sequence;
        if (!c.Event(4, sequence)) return false;
        out = sequence < 24 ? c.times.duration[sequence] : 0;
        return true;
    }
    FamasAimHost Host() noexcept { return {this, Current, Time, Effect, Duration}; }
    FamasControlResult Run() noexcept
    {
        return operation == 2 ? ToggleFamasAim(state, pressed, toggle, metadata, Host()) :
            ApplyFamasAim(state, operation == 0, metadata, Host());
    }
};
std::vector<Word> Words(const std::string& text)
{
    std::istringstream in(text); std::vector<Word> result; Word value{};
    while (in >> std::hex >> value) result.push_back(value);
    if (!in.eof()) return {};
    return result;
}
Context Initial()
{
    Context c; c.state.clip = 7; c.times.duration.fill(2.5F); return c;
}
void Safety()
{
    auto c = Initial(); const auto saved = c.Snapshot();
    CHECK(ApplyFamasAim(c.state, true, c.metadata, {}) == FamasControlResult::MissingHost);
    CHECK(c.Snapshot() == saved && !c.count);
    for (unsigned i = 1; i <= 5; ++i)
    {
        c = Initial(); c.cancelAt = i;
        CHECK(c.Run() == FamasControlResult::Cancelled && c.count == i);
        c = Initial(); c.rejectAt = i; c.state.idle = 16.F;
        CHECK(c.Run() == FamasControlResult::AppliedWithoutView && c.count == 5);
        CHECK(c.state.aiming && c.state.primary > c.now && c.state.primary == c.state.secondary);
        CHECK(c.state.primary == c.state.extra);
        if (i == 5) CHECK(c.state.idle == 16.F);
    }
    for (int clip : {0, 7, 75, 511, 10000})
    {
        c = Initial(); c.state.clip = clip;
        CHECK(c.Run() == FamasControlResult::Applied && c.sequence == 15 && c.state.clip == clip);
        c.operation = 1; c.count = 0;
        CHECK(c.Run() == FamasControlResult::Applied && c.sequence == 18 && !c.state.aiming);
    }
    c = Initial(); auto host = c.Host(); host.event = nullptr;
    CHECK(ApplyFamasAim(c.state, true, c.metadata, host) == FamasControlResult::AppliedWithoutView);
    CHECK(c.state.aiming && c.state.primary > c.now);
    c = Initial(); host = c.Host(); host.duration = nullptr; c.state.idle = 16.F;
    CHECK(ApplyFamasAim(c.state, true, c.metadata, host) == FamasControlResult::AppliedWithoutView);
    CHECK(c.state.aiming && c.state.primary > c.now && c.state.idle == 16.F);
    c = Initial(); c.now = std::numeric_limits<float>::quiet_NaN();
    CHECK(c.Run() == FamasControlResult::InvalidInput && !c.count);
    c = Initial(); c.metadata.offset[0] = std::numeric_limits<float>::infinity();
    CHECK(c.Run() == FamasControlResult::InvalidInput && !c.count);
    c.operation = 2; const auto beforeInvalid = c.Snapshot();
    CHECK(c.Run() == FamasControlResult::InvalidInput && c.Snapshot() == beforeInvalid);
    const auto before = allocations;
    for (unsigned i = 0; i != 10000; ++i)
    {
        c = Initial(); c.operation = i & 1;
        CHECK(c.Run() == FamasControlResult::Applied && c.count == 5);
    }
    CHECK(allocations == before);
}

}

int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    std::ifstream cases(argv[1]), expected(argv[2]), timing(argv[3]);
    if (!cases || !expected || !timing) return 1;
    MagazineSequenceTimes times;
    for (auto& value : times.duration)
    {
        Word bits{}; if (!(timing >> std::hex >> bits)) return 3; value = Float(bits);
    }
    std::string input, answer; unsigned count{};
    while (std::getline(cases, input))
    {
        const auto row = Words(input);
        if (row.size() != 18 || row[0] > 2) return 3;
        if (!std::getline(expected, answer)) return 4;
        auto c = Initial(); c.times = times; c.operation = row[0]; c.now = Float(row[1]);
        c.state.clip = int(row[2]); c.state.aiming = row[3] != 0;
        c.state.primary = Float(row[4]); c.state.secondary = Float(row[5]); c.state.extra = Float(row[6]);
        c.state.action = int(row[7]); c.state.special = int(row[8]);
        c.pressed = (row[9] & 0x40U) != 0; c.toggle = row[10] != 0;
        c.metadata = {{Float(row[11]), Float(row[12]), Float(row[13])}, int(row[14])};
        c.change = row[15]; c.nth = row[16]; c.alt = row[17];
        const auto result = c.Run();
        CHECK(result == FamasControlResult::Applied || result == FamasControlResult::Ineligible);
        const auto snapshot = c.Snapshot(); std::vector<Word> actual(snapshot.begin(), snapshot.end());
        for (unsigned i = 0; i < c.count; ++i)
            actual.insert(actual.end(), c.events[i].begin(), c.events[i].begin() + c.sizes[i]);
        const auto wanted = Words(answer);
        if (actual != wanted)
        {
            std::cerr << "Original Famas aim mismatch at case " << count << '\n';
            for (unsigned i = 0; i < std::min(actual.size(), wanted.size()); ++i)
                if (actual[i] != wanted[i]) std::cerr << i << ":" << std::hex << actual[i] << " != " << wanted[i] << '\n';
            return 5;
        }
        checks += unsigned(actual.size()); ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, answer)) return 7;
    Safety();
    std::cout << "originalCases=" << count << " checks=" << checks << " failures=0\n";
    return 0;
}
