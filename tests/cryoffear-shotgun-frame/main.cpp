// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/shotgun_aim.hpp"
#include "native/guest/cof/shotgun_frame.hpp"
#include "native/guest/cof/shotgun_shot.hpp"

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

namespace { unsigned checks{}, allocations{}; }
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
Word Bits(float value) noexcept { return std::bit_cast<Word>(value); }
float Float(Word value) noexcept { return std::bit_cast<float>(value); }
void Check(bool value, int line)
{
    ++checks;
    if (!value) { std::cerr << "Shotgun frame failure at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
struct Context
{
    ShotgunShotState shot;
    ShotgunFrameOwner owner;
    ShotgunAimMetadata metadata{{1.25F, -2.5F, 3.75F}, 55};
    std::array<float, 20> times{};
    float now{13.37F}, deadline{}, fov{90}, delta{.375F};
    unsigned sequence{}, operation{}, change{}, nth{}, alt{}, ordinal{}, calls{}, cancelAt{}, rejectAt{};
    bool current{true}, toggle{true}, recording{true};
    std::vector<Word> events;
    std::array<Word, 24> Snapshot() const noexcept
    {
        const auto& a = shot.action;
        return {Word(a.clip), Word(a.reserve), Word(a.action), Word(a.special), Word(a.reloadStage),
            Word(a.emptyStart), Word(a.reloadAbort), a.aiming, a.pendingReload,
            Bits(a.primary), Bits(a.secondary), Bits(a.extra), Bits(a.idle), Bits(a.ownerAttack),
            Bits(a.playerAnimationDeadline), Bits(a.insertDeadline), Word(shot.shotGate), Bits(deadline),
            Bits(now), Bits(fov), sequence, owner.flags, Bits(owner.velocityX), Bits(owner.velocityY)};
    }
    template<class... T> bool Event(unsigned kind, T... values) noexcept
    {
        if (recording)
        {
            events.insert(events.end(), {kind, sizeof...(T) + 24, Word(values)...});
            const auto snapshot = Snapshot(); events.insert(events.end(), snapshot.begin(), snapshot.end());
        }
        ++ordinal;
        if (ordinal == nth)
        {
            if (change & 1) now += delta;
            if (change & 2) owner.flags ^= 0x40000U;
            if (change & 4) owner.velocityX = owner.velocityY = 0;
            if (change & 8) sequence = alt;
            if (change & 16) shot.action.clip = !shot.action.clip;
            if (change & 32) shot.action.emptyStart = !shot.action.emptyStart;
            if (change & 64) shot.action.reloadAbort = !shot.action.reloadAbort;
        }
        return Step();
    }
    bool Step() noexcept
    {
        ++calls;
        if (calls == cancelAt) current = false;
        return calls != rejectAt;
    }
    static bool Current(void* p) noexcept { return static_cast<Context*>(p)->current; }
    static float Time(void* p) noexcept { return static_cast<Context*>(p)->now; }
    static bool Owner(void* p, ShotgunFrameOwner& out) noexcept
    { auto& c = *static_cast<Context*>(p); out = c.owner; return c.Step(); }
    static bool AimEvent(void* p, const ShotgunAimEvent& event) noexcept
    {
        auto& c = *static_cast<Context*>(p);
        switch (event.kind)
        {
        case ShotgunAimEventKind::Presentation:
            return c.Event(1, Bits(event.offset[0]), Bits(event.offset[1]), Bits(event.offset[2]), event.value, event.enabled);
        case ShotgunAimEventKind::CameraZoom: return c.Event(2, Bits(event.amount), 0, 0);
        case ShotgunAimEventKind::PlayerFov: c.fov = event.amount; return c.Event(3, Bits(c.fov));
        case ShotgunAimEventKind::ViewAnimation:
            if (!c.Event(0, event.value)) return false;
            c.sequence = unsigned(event.value); return true;
        }
        return false;
    }
    static bool Duration(void* p, float& out) noexcept
    {
        auto& c = *static_cast<Context*>(p); const unsigned index = c.sequence;
        if (!c.Event(4, index)) return false;
        out = index < c.times.size() ? c.times[index] : 0; return true;
    }
    static bool AutoAim(void* p, float slope) noexcept { return static_cast<Context*>(p)->Event(5, Bits(slope)); }
    ShotgunAimHost AimHost() noexcept { return {this, Current, Time, AimEvent, Duration, AutoAim}; }
    static bool Control(void* p, const ShotgunControlEvent& event) noexcept
    {
        auto& c = *static_cast<Context*>(p);
        if (event.kind != ShotgunControlEventKind::ViewAnimation)
            return c.Event(10 + unsigned(event.kind), event.sequence, Bits(event.delay));
        ShotgunAimEvent view{ShotgunAimEventKind::ViewAnimation}; view.value = event.sequence;
        return AimEvent(p, view);
    }
    static bool FrameEvent(void* p, ShotgunFrameEvent event, int sequence) noexcept
    {
        auto& c = *static_cast<Context*>(p);
        switch (event)
        {
        case ShotgunFrameEvent::ViewAnimation:
        {
            ShotgunAimEvent view{ShotgunAimEventKind::ViewAnimation}; view.value = sequence;
            return AimEvent(p, view);
        }
        case ShotgunFrameEvent::ExitAim:
            if (!c.Event(6)) return false;
            return ApplyShotgunAim(c.shot.action, false, c.metadata, c.AimHost()) == ShotgunControlResult::Applied;
        case ShotgunFrameEvent::Reload:
        {
            if (!c.Event(7)) return false;
            const auto r = StartShotgunReload(c.shot.action, {p, Current, Time, Control});
            return r == ShotgunControlResult::Applied || r == ShotgunControlResult::Deferred || r == ShotgunControlResult::Ineligible;
        }
        case ShotgunFrameEvent::CommonFrame: return c.Event(8);
        }
        return false;
    }
    ShotgunFrameHost FrameHost() noexcept { return {this, Current, Time, Owner, FrameEvent, Duration, Control}; }
    ShotgunControlResult Run() noexcept
    {
        switch (operation)
        {
        case 0: case 1: return ApplyShotgunAim(shot.action, operation == 0, metadata, AimHost());
        case 2: return ToggleShotgunAim(shot.action, (owner.pressed & 0x40U) != 0, toggle, metadata, AimHost());
        case 3: return ShotgunIdle(shot.action, AimHost());
        case 4: return AdvanceShotgunDerivedFrame(shot, deadline, FrameHost());
        default: return ShotgunControlResult::InvalidInput;
        }
    }
};
std::vector<Word> Words(const std::string& text)
{
    std::istringstream stream(text); std::vector<Word> values; Word value{};
    while (stream >> std::hex >> value) values.push_back(value);
    return stream.eof() ? values : std::vector<Word>{};
}
Context Initial()
{
    Context c; c.shot.action.clip = 1; c.shot.action.reserve = 50; c.times.fill(2.5F);
    c.events.reserve(2048); return c;
}
void Safety()
{
    auto c = Initial();
    CHECK(ApplyShotgunAim(c.shot.action, true, c.metadata, {}) == ShotgunControlResult::MissingHost);
    CHECK(AdvanceShotgunDerivedFrame(c.shot, c.deadline, {}) == ShotgunControlResult::MissingHost);
    for (unsigned scenario = 0; scenario != 6; ++scenario)
    {
        const auto setup = [scenario]()
        {
            auto state = Initial(); state.operation = scenario < 3 ? scenario : 4;
            state.owner.pressed = 0x40;
            if (scenario == 3) state.shot.action.pendingReload = state.shot.action.aiming = true;
            if (scenario == 4) { state.shot.action.action = 2; state.shot.action.reloadStage = 1; state.shot.action.clip = 5; }
            if (scenario == 5) { state.owner.flags = 0x40000; state.owner.velocityX = 3; state.owner.velocityY = 4; }
            return state;
        };
        c = setup(); CHECK(c.Run() == ShotgunControlResult::Applied); const unsigned calls = c.calls;
        for (unsigned edge = 1; edge <= calls; ++edge)
        {
            c = setup(); c.cancelAt = edge; CHECK(c.Run() == ShotgunControlResult::Cancelled); CHECK(c.calls == edge);
            c = setup(); c.rejectAt = edge; CHECK(c.Run() == ShotgunControlResult::ConsumerRejected); CHECK(c.calls == edge);
        }
    }
    c = Initial(); c.operation = 3; c.shot.action.primary = c.now + 1;
    CHECK(c.Run() == ShotgunControlResult::Ineligible && c.ordinal == 1);
    c = Initial(); c.current = false; CHECK(c.Run() == ShotgunControlResult::Cancelled && !c.calls);
    c = Initial(); c.metadata.offset[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(c.Run() == ShotgunControlResult::InvalidInput && !c.calls);
    c = Initial(); c.shot.action.special = 1; c.operation = 4;
    CHECK(c.Run() == ShotgunControlResult::UnsupportedState && !c.calls);
    c = Initial(); c.recording = false; const auto state = c.shot; const unsigned before = allocations;
    for (unsigned i = 0; i < 10000; ++i)
    {
        c.shot = state; c.operation = i % 2 ? 0 : 4;
        CHECK(c.Run() == ShotgunControlResult::Applied);
    }
    CHECK(allocations == before);
}
}

int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    std::ifstream cases(argv[1]), expected(argv[2]), timing(argv[3]); if (!cases || !expected || !timing) return 1;
    std::array<float, 20> times{}; Word word{};
    for (auto& value : times) { if (!(timing >> std::hex >> word)) return 3; value = Float(word); }
    std::string line, answer; unsigned count{};
    while (std::getline(cases, line))
    {
        const auto row = Words(line); if (row.size() != 38 || row[0] > 4) return 3;
        if (!std::getline(expected, answer)) return 4;
        auto c = Initial(); auto& s = c.shot.action; c.times = times;
        c.operation = row[0]; c.now = Float(row[1]); s.clip = int(row[2]); s.reserve = int(row[3]);
        s.action = int(row[4]); s.special = int(row[5]); s.reloadStage = int(row[6]);
        s.emptyStart = int(row[7]); s.reloadAbort = int(row[8]); s.aiming = row[9] != 0; s.pendingReload = row[10] != 0;
        s.primary = Float(row[11]); s.secondary = Float(row[12]); s.extra = Float(row[13]); s.idle = Float(row[14]);
        s.ownerAttack = Float(row[15]); s.playerAnimationDeadline = Float(row[16]); s.insertDeadline = Float(row[17]);
        c.shot.shotGate = int(row[18]); c.deadline = Float(row[19]); c.fov = Float(row[20]); c.sequence = row[21];
        c.owner.flags = row[22]; c.owner.velocityX = Float(row[23]); c.owner.velocityY = Float(row[24]);
        c.owner.condition3AA = row[25] != 0; c.owner.pendingGate = row[26] != 0; c.owner.pendingGateTime = Float(row[27]);
        c.owner.pressed = row[28]; c.toggle = row[29] != 0;
        c.metadata = {{Float(row[30]), Float(row[31]), Float(row[32])}, int(row[33])};
        c.change = row[34]; c.nth = row[35]; c.delta = Float(row[36]); c.alt = row[37];
        const auto result = c.Run();
        CHECK(result == ShotgunControlResult::Applied || result == ShotgunControlResult::Ineligible);
        const auto snapshot = c.Snapshot(); std::vector<Word> actual(snapshot.begin(), snapshot.end());
        actual.insert(actual.end(), c.events.begin(), c.events.end()); const auto wanted = Words(answer);
        if (actual != wanted)
        {
            std::cerr << "Original 870 frame mismatch case " << count << " op=" << row[0] << '\n';
            for (std::size_t i = 0; i < std::min(actual.size(), wanted.size()); ++i)
                if (actual[i] != wanted[i]) std::cerr << std::dec << i << ':' << std::hex << actual[i] << " != " << wanted[i] << '\n';
            return 5;
        }
        checks += unsigned(actual.size()); ++count;
    }
    if (!count) return 6; if (std::getline(expected, answer)) return 7;
    Safety(); std::cout << "originalCases=" << count << " checks=" << checks << " failures=0\n";
}
