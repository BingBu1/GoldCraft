// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/shotgun_control.hpp"

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
    if (!value) { std::cerr << "Shotgun control failure at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
struct Context
{
    ShotgunActionState state;
    float now{}, stamina{}, delta{};
    unsigned operation{}, mutation{}, count{}, cancelAt{}, rejectAt{};
    bool pressed{}, alive{true};
    std::array<std::array<Word, 20>, 6> events{};
    std::array<Word, 17> Snapshot() const noexcept
    {
        return {Word(state.clip), Word(state.reserve), Word(state.action), Word(state.special),
            Word(state.reloadStage), Word(state.emptyStart), Word(state.reloadAbort), state.aiming, state.pendingReload,
            Bits(state.primary), Bits(state.secondary), Bits(state.extra), Bits(state.idle), Bits(state.ownerAttack),
            Bits(state.playerAnimationDeadline), Bits(state.insertDeadline), Bits(now)};
    }
    static bool Current(void* p) noexcept { return static_cast<Context*>(p)->alive; }
    static float Time(void* p) noexcept { return static_cast<Context*>(p)->now; }
    static bool Event(void* p, const ShotgunControlEvent& e) noexcept
    {
        auto& c = *static_cast<Context*>(p);
        CHECK(c.count < c.events.size());
        auto& event = c.events[c.count++];
        event[0] = Word(e.kind); event[1] = Word(e.sequence); event[2] = Bits(e.delay);
        const auto snapshot = c.Snapshot();
        std::copy(snapshot.begin(), snapshot.end(), event.begin() + 3);
        if (c.count == c.cancelAt) { c.alive = false; return true; }
        if (c.count == c.rejectAt) return false;
        if (c.mutation) c.now += c.delta;
        if (c.mutation == 2 && e.kind == ShotgunControlEventKind::ViewAnimation)
        {
            c.state.reloadStage = 9;
            c.state.emptyStart = !c.state.emptyStart;
            c.state.reloadAbort = !c.state.reloadAbort;
        }
        return true;
    }
    ShotgunControlHost Host() noexcept { return {this, Current, Time, Event}; }
    ShotgunControlResult Run() noexcept
    {
        switch (operation)
        {
        case 0: return StartShotgunReload(state, Host());
        case 1: InsertShotgunShell(state); return ShotgunControlResult::Applied;
        case 2: return StartShotgunPushback(state, stamina, Host());
        case 3: return AdvanceShotgunReload(state, Host());
        case 4: ObserveShotgunReloadAttack(state, pressed); return ShotgunControlResult::Applied;
        default: return ShotgunControlResult::InvalidInput;
        }
    }
};
std::vector<Word> Words(const std::string& text)
{
    std::istringstream in(text);
    std::vector<Word> values; Word value{};
    while (in >> std::hex >> value) values.push_back(value);
    return in.eof() ? values : std::vector<Word>{};
}
Context Initial()
{
    Context c; c.now = 13.37F; c.stamina = 100;
    c.state.clip = 1; c.state.reserve = 50;
    return c;
}
void Safety()
{
    auto c = Initial(); const auto saved = c.Snapshot();
    CHECK(StartShotgunReload(c.state, {}) == ShotgunControlResult::MissingHost && c.Snapshot() == saved);
    for (unsigned op : {0U, 2U, 3U})
    {
        c = Initial(); c.operation = op; c.alive = false;
        CHECK(c.Run() == ShotgunControlResult::Cancelled && !c.count);
        c.alive = true; c.now = std::numeric_limits<float>::infinity();
        CHECK(c.Run() == ShotgunControlResult::InvalidInput && !c.count);
    }
    for (bool stop : {false, true})
        for (unsigned op : {0U, 2U, 3U})
            for (unsigned event = 1; event <= (op == 2 ? 3U : 2U); ++event)
            {
                c = Initial(); c.operation = op;
                if (op == 3) { c.state.reloadStage = 1; c.state.reloadAbort = 1; }
                if (stop) c.cancelAt = event; else c.rejectAt = event;
                CHECK(c.Run() == (stop ? ShotgunControlResult::Cancelled : ShotgunControlResult::ConsumerRejected));
                CHECK(c.count == event);
            }
    c = Initial(); c.state.clip = 0; c.state.action = 1;
    CHECK(c.Run() == ShotgunControlResult::Ineligible && c.state.emptyStart == 1 && !c.count);
    c = Initial(); c.state.clip = 0; c.state.aiming = true;
    CHECK(c.Run() == ShotgunControlResult::Deferred && c.state.emptyStart == 1 && c.state.pendingReload);
    c = Initial(); c.state.reloadStage = 1; c.state.idle = c.now;
    c.operation = 3; CHECK(c.Run() == ShotgunControlResult::Deferred && !c.count);
    c.operation = 0; CHECK(c.Run() == ShotgunControlResult::Applied && c.state.reloadStage == 2);
    const auto before = allocations;
    for (unsigned i = 0; i < 10000; ++i)
    {
        c = Initial(); c.operation = i & 1 ? 0 : 2;
        CHECK(c.Run() == ShotgunControlResult::Applied);
    }
    CHECK(allocations == before);
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    std::ifstream cases(argv[1]), expected(argv[2]);
    if (!cases || !expected) return 1;
    std::string input, wanted; unsigned count{};
    while (std::getline(cases, input))
    {
        if (!std::getline(expected, wanted)) return 4;
        const auto row = Words(input), answer = Words(wanted);
        if (row.size() != 22 || row[0] > 4 || answer.size() < 18) return 3;
        Context c; c.operation = row[0];
        c.state = {int(row[1]), int(row[2]), int(row[3]), int(row[4]), int(row[5]), int(row[6]), int(row[7]),
            bool(row[8]), bool(row[9]), Float(row[10]), Float(row[11]), Float(row[12]), Float(row[13]), Float(row[14]),
            Float(row[15]), Float(row[16])};
        c.now = Float(row[17]); c.stamina = Float(row[18]); c.delta = Float(row[19]); c.mutation = row[20]; c.pressed = row[21] != 0;
        const auto result = c.Run();
        CHECK(result == ShotgunControlResult::Applied || result == ShotgunControlResult::Deferred || result == ShotgunControlResult::Ineligible);
        const auto snapshot = c.Snapshot(); std::vector<Word> actual(snapshot.begin(), snapshot.end()); actual.push_back(c.count);
        for (unsigned i = 0; i < c.count; ++i) actual.insert(actual.end(), c.events[i].begin(), c.events[i].end());
        if (actual != answer)
        {
            std::cerr << "Original shotgun mismatch case " << count << " op=" << c.operation << '\n';
            for (unsigned i = 0; i < std::min(actual.size(), answer.size()); ++i)
                if (actual[i] != answer[i]) std::cerr << i << ':' << std::hex << actual[i] << " != " << answer[i] << '\n';
            return 5;
        }
        checks += unsigned(actual.size()); ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, wanted)) return 7;
    Safety();
    std::cout << "originalCases=" << count << " checks=" << checks << " failures=0\n";
}
