// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/g43_control.hpp"

#include <algorithm>
#include <array>
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
    if (!value) { std::cerr << "G43 control failed at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
struct Context
{
    MagazineActionState state;
    MagazineSequenceTimes times;
    float now{}, stamina{}, delta{};
    unsigned operation{}, count{}, cancelAt{}, rejectAt{};
    bool mutate{}, alive{true};
    std::array<std::array<Word, 18>, 3> events{};
    std::array<Word, 15> Snapshot() const noexcept
    {
        return {Word(state.clip), Word(state.reserve), Word(state.action), Word(state.special),
            Word(state.shotPhase), Word(state.burstShots), state.aiming, state.pendingReload,
            state.loading, Bits(state.primary), Bits(state.secondary), Bits(state.extra),
            Bits(state.idle), Bits(state.ownerAttack), Bits(now)};
    }
    static bool Current(void* p) noexcept { return static_cast<Context*>(p)->alive; }
    static float Time(void* p) noexcept { return static_cast<Context*>(p)->now; }
    static bool Event(void* p, const G43ControlEvent& e) noexcept
    {
        auto& c = *static_cast<Context*>(p);
        CHECK(c.count < c.events.size());
        auto& event = c.events[c.count++];
        event[0] = Word(e.kind); event[1] = Word(e.argument); event[2] = Bits(e.delay);
        const auto snapshot = c.Snapshot();
        std::copy(snapshot.begin(), snapshot.end(), event.begin() + 3);
        if (c.count == c.cancelAt) { c.alive = false; return true; }
        if (c.count == c.rejectAt) return false;
        if (c.mutate)
        {
            c.now += c.delta;
            if (e.kind == G43ControlEventKind::SchedulePushback) c.state.clip = !c.state.clip;
        }
        return true;
    }
    G43ControlHost Host() noexcept { return {this, Current, Time, Event}; }
    G43ControlResult Run() noexcept
    {
        return operation ? StartG43MagazineReload(state, times, Host()) :
            StartG43Pushback(state, stamina, Host());
    }
};
std::vector<Word> Words(const std::string& text)
{
    std::istringstream in(text);
    std::vector<Word> values; Word value{};
    while (in >> std::hex >> value) values.push_back(value);
    if (!in.eof()) return {};
    return values;
}
Context Initial()
{
    Context c; c.now = 13.37F; c.stamina = 100;
    c.state.clip = 7; c.state.reserve = 60;
    c.times.present = (std::uint64_t{1} << 38) - 1;
    c.times.duration.fill(2.5F); return c;
}
void Safety()
{
    auto c = Initial(); auto saved = c.Snapshot();
    CHECK(StartG43Pushback(c.state, c.stamina, {}) == G43ControlResult::MissingHost);
    CHECK(c.Snapshot() == saved && !c.count);
    for (unsigned op = 0; op != 2; ++op)
    {
        c = Initial(); c.operation = op; c.alive = false; saved = c.Snapshot();
        CHECK(c.Run() == G43ControlResult::Cancelled && c.Snapshot() == saved && !c.count);
        c.alive = true; c.now = std::numeric_limits<float>::quiet_NaN();
        CHECK(c.Run() == G43ControlResult::InvalidInput);
        for (unsigned stop = 1; stop <= (op ? 2U : 3U); ++stop)
        {
            c = Initial(); c.operation = op; c.cancelAt = stop;
            CHECK(c.Run() == G43ControlResult::Cancelled && c.count == stop);
            if (!op) CHECK(c.state.primary == 0);
            else if (stop == 1) CHECK(!c.state.loading);
            c = Initial(); c.operation = op; c.rejectAt = stop;
            CHECK(c.Run() == G43ControlResult::ConsumerRejected && c.count == stop);
        }
    }
    for (int clip : {11, 75, 511, 10000})
    {
        c = Initial(); c.operation = 1; c.state.clip = clip; saved = c.Snapshot();
        CHECK(c.Run() == G43ControlResult::Ineligible && c.Snapshot() == saved && !c.count);
        c.operation = 0; CHECK(c.Run() == G43ControlResult::Applied);
        CHECK(c.state.clip == clip); // Pushback never spends cartridges.
    }
    c = Initial(); c.operation = 1; c.state.aiming = true; c.state.clip = 10;
    c.state.secondary = c.now + 10;
    CHECK(c.Run() == G43ControlResult::Deferred && c.state.pendingReload && !c.count);
    c = Initial(); c.operation = 1; c.times.present = 0; saved = c.Snapshot();
    CHECK(c.Run() == G43ControlResult::MissingSequence && c.Snapshot() == saved);
    c = Initial(); c.stamina = std::numeric_limits<float>::infinity();
    CHECK(c.Run() == G43ControlResult::InvalidInput && !c.count);
    const unsigned before = allocations;
    for (unsigned i = 0; i != 10000; ++i)
    {
        c = Initial(); c.operation = i & 1;
        CHECK(c.Run() == G43ControlResult::Applied);
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
    times.present = (std::uint64_t{1} << 38) - 1;
    std::string input, wanted; unsigned count{};
    while (std::getline(cases, input))
    {
        if (!std::getline(expected, wanted)) return 4;
        const auto row = Words(input), answer = Words(wanted);
        if (row.size() != 19 || row[0] > 1 || answer.size() < 16) return 3;
        Context c; c.times = times; c.operation = row[0];
        c.state = {int(row[1]), int(row[2]), int(row[3]), int(row[4]), int(row[5]), int(row[6]),
            bool(row[7]), bool(row[8]), bool(row[9]), Float(row[10]), Float(row[11]),
            Float(row[12]), Float(row[13]), Float(row[14])};
        c.now = Float(row[15]); c.stamina = Float(row[16]); c.mutate = row[17] != 0; c.delta = Float(row[18]);
        const auto result = c.Run();
        CHECK(result == G43ControlResult::Applied || result == G43ControlResult::Deferred ||
            result == G43ControlResult::Ineligible);
        const auto snapshot = c.Snapshot();
        std::vector<Word> actual(snapshot.begin(), snapshot.end()); actual.push_back(c.count);
        for (unsigned i = 0; i < c.count; ++i) actual.insert(actual.end(), c.events[i].begin(), c.events[i].end());
        if (actual != answer)
        {
            std::cerr << "Original G43 mismatch at case " << count << '\n';
            for (unsigned i = 0; i < std::min(actual.size(), answer.size()); ++i)
                if (actual[i] != answer[i]) std::cerr << i << ":" << std::hex << actual[i] << " != " << answer[i] << '\n';
            return 5;
        }
        checks += unsigned(actual.size()); ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, wanted)) return 7;
    Safety();
    std::cout << "originalCases=" << count << " checks=" << checks << " failures=0\n";
    return 0;
}
