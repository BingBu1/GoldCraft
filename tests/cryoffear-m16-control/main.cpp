// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/m16_control.hpp"

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
    if (!value) { std::cerr << "M16 control failed at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
struct Context
{
    M16ControlState state;
    MagazineSequenceTimes times;
    float now{}, delta{};
    unsigned operation{}, count{}, cancelAt{}, rejectAt{};
    bool pressed{}, empty{}, mutate{}, alive{true};
    std::array<std::array<Word, 19>, 4> events{};
    std::array<Word, 16> Snapshot() const noexcept
    {
        const auto& s = state.action;
        return {Word(s.clip), Word(s.reserve), Word(s.action), Word(s.special), Word(s.shotPhase),
            Word(s.burstShots), s.aiming, s.pendingReload, s.loading, Bits(s.primary), Bits(s.secondary),
            Bits(s.extra), Bits(s.idle), Bits(s.ownerAttack), state.burst, Bits(now)};
    }
    static bool Current(void* p) noexcept { return static_cast<Context*>(p)->alive; }
    static float Time(void* p) noexcept { return static_cast<Context*>(p)->now; }
    static bool Event(void* p, const M16ControlEvent& e) noexcept
    {
        auto& c = *static_cast<Context*>(p);
        CHECK(c.count < c.events.size());
        auto& event = c.events[c.count++];
        event[0] = Word(e.kind);
        event[1] = e.kind == M16ControlEventKind::ViewAnimation ? Word(e.sequence) :
            e.kind == M16ControlEventKind::PlayerReloadLoaded || e.kind == M16ControlEventKind::PlayerReloadEmpty ? 9 : 0;
        const auto snapshot = c.Snapshot();
        std::copy(snapshot.begin(), snapshot.end(), event.begin() + 3);
        if (c.count == c.cancelAt) { c.alive = false; return true; }
        if (c.count == c.rejectAt) return false;
        if (e.kind == M16ControlEventKind::Shoot)
        {
            --c.state.action.clip;
            ++c.state.action.burstShots;
            if (c.mutate) { c.state.burst = !c.state.burst; c.now += c.delta; }
        }
        else if (c.mutate && (e.kind == M16ControlEventKind::EmptyClick ||
                 (c.operation == 1 && e.kind == M16ControlEventKind::ViewAnimation))) c.now += c.delta;
        return true;
    }
    M16ControlHost Host() noexcept { return {this, Current, Time, Event}; }
    M16ControlResult Run() noexcept
    {
        if (operation == 0) return M16PrimaryTrigger(state, pressed, empty, Host());
        if (operation == 1) return M16ToggleFireMode(state, Host());
        return StartM16MagazineReload(state, times, Host());
    }
};
std::vector<Word> Words(const std::string& text)
{
    std::istringstream in(text);
    std::vector<Word> values;
    Word value{};
    while (in >> std::hex >> value) values.push_back(value);
    if (!in.eof()) return {};
    return values;
}
Context Initial()
{
    Context c;
    c.now = 13.37F; c.pressed = true;
    c.state.action.clip = 7; c.state.action.reserve = 60;
    c.times.present = (std::uint64_t{1} << 38) - 1;
    c.times.duration.fill(2.5F);
    return c;
}
void Safety()
{
    auto c = Initial();
    auto saved = c.Snapshot();
    CHECK(M16PrimaryTrigger(c.state, true, false, {}) == M16ControlResult::MissingHost);
    CHECK(c.Snapshot() == saved && c.count == 0);
    for (unsigned op = 0; op != 3; ++op)
    {
        c = Initial(); c.operation = op; c.alive = false;
        saved = c.Snapshot();
        CHECK(c.Run() == M16ControlResult::Cancelled);
        CHECK(c.Snapshot() == saved && !c.count);
        c.alive = true; c.now = std::numeric_limits<float>::quiet_NaN();
        CHECK(c.Run() == M16ControlResult::InvalidInput);
        for (unsigned stop = 1; stop <= (op == 2 ? 2U : 1U); ++stop)
        {
            c = Initial(); c.operation = op; c.cancelAt = stop;
            CHECK(c.Run() == M16ControlResult::Cancelled);
            CHECK(c.count == stop);
            if (op == 1) CHECK(c.state.burst && c.state.action.primary == 0);
            if (op == 2 && stop == 1) CHECK(!c.state.action.loading);
        }
    }
    for (unsigned op : {0U, 1U})
    {
        c = Initial(); c.operation = op; c.rejectAt = 1;
        CHECK(c.Run() == M16ControlResult::ConsumerRejected);
        CHECK(c.state.action.primary == 0 && c.state.burst);
        c.rejectAt = 0;
        CHECK(c.Run() == M16ControlResult::Applied);
    }
    c = Initial(); c.times.present = 0; c.operation = 2;
    saved = c.Snapshot();
    CHECK(c.Run() == M16ControlResult::MissingSequence);
    CHECK(c.Snapshot() == saved && !c.count);
    for (int clip : {21, 75, 511, 10000})
    {
        c = Initial(); c.operation = 2; c.state.action.clip = clip;
        saved = c.Snapshot();
        CHECK(c.Run() == M16ControlResult::Ineligible);
        CHECK(c.Snapshot() == saved && !c.count);
        c.operation = 0;
        CHECK(c.Run() == M16ControlResult::Applied);
        CHECK(c.state.action.clip == clip - 1);
    }
}
}

int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    std::ifstream cases(argv[1]), expected(argv[2]), timing(argv[3]);
    if (!cases || !expected || !timing) return 1;
    MagazineSequenceTimes times;
    for (auto& value : times.duration) { Word bits{}; if (!(timing >> std::hex >> bits)) return 3; value = Float(bits); }
    times.present = (std::uint64_t{1} << 38) - 1;
    std::string input, wanted;
    unsigned count{};
    while (std::getline(cases, input))
    {
        const auto row = Words(input);
        if (row.size() != 21 || row[0] > 2) return 3;
        if (!std::getline(expected, wanted)) return 4;
        Context c; c.times = times; c.operation = row[0];
        auto& a = c.state.action;
        a = {int(row[1]), int(row[2]), int(row[3]), int(row[4]), int(row[5]), int(row[6]),
            row[7] != 0, row[8] != 0, row[9] != 0, Float(row[10]), Float(row[11]), Float(row[12]),
            Float(row[13]), Float(row[14])};
        c.state.burst = row[15] != 0; c.now = Float(row[16]); c.pressed = row[17] != 0;
        c.empty = row[18] != 0; c.mutate = row[19] != 0; c.delta = Float(row[20]);
        const auto before = allocations;
        const auto result = c.Run();
        CHECK(allocations == before);
        CHECK(result == M16ControlResult::Applied || result == M16ControlResult::Deferred || result == M16ControlResult::Ineligible);
        const auto snapshot = c.Snapshot();
        std::vector<Word> actual(snapshot.begin(), snapshot.end());
        actual.push_back(c.count);
        for (unsigned i = 0; i != c.count; ++i) actual.insert(actual.end(), c.events[i].begin(), c.events[i].end());
        if (actual != Words(wanted))
        {
            std::cerr << "case " << count << " op=" << c.operation << '\n';
            for (auto v : actual) std::cerr << std::hex << v << ' ';
            std::cerr << "\nexpected " << wanted << '\n';
            return 5;
        }
        ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, wanted)) return 7;
    Safety();
    std::cout << "cases=" << count << " checks=" << checks << " failures=0\n";
}
