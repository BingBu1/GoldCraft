// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/scenario/core/emitter_runtime.hpp"
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace runtime = csol::particles;
using runtime::EmitterResult;
struct Payload { unsigned id{}, kind{}, retained{}; };
using Pool = runtime::TemporaryPool<Payload, unsigned>;
using Record = Pool::Record;
unsigned checks{};
void Check(bool value, const char *message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
unsigned Bits(float value) { return std::bit_cast<unsigned>(value); }
float Real()
{
    unsigned bits;
    if (!(std::cin >> bits)) throw std::runtime_error("incomplete float input");
    return std::bit_cast<float>(bits);
}
// Fixture seeding alone permits states not produced by the supported messages.
// The production owner intentionally has no public mutable-record accessor.
Record &Seed(Pool &pool, std::size_t index)
{
    return const_cast<Record &>(pool.Records()[index]);
}
struct Host
{
    Pool &pool;
    std::vector<std::vector<unsigned>> events;
    float now{10}, cached{9}, afterLoad{9};
    unsigned model{1}, created{};
    int failAt{-1}, invalidate{-1}, recurseAt{-1};
    void Boundary(int where)
    {
        if (invalidate == where) pool.ResetVideoRecords();
        if (recurseAt == where)
        {
            Check(pool.Frame(*this) == EmitterResult::Reentrant, "recursive Frame");
            if (where < 3)
                Check(pool.Add(Record{}, *this) == EmitterResult::Reentrant, "recursive Add");
        }
    }
    unsigned LoadSprite(const Record &record)
    {
        events.push_back({0, record.payload.id, static_cast<unsigned>(record.remaining), record.model});
        cached = afterLoad;
        Boundary(0);
        return 2;
    }
    unsigned GetModel(unsigned handle)
    {
        events.push_back({1, handle});
        Boundary(1);
        return model;
    }
    float HudTime()
    {
        events.push_back({2, Bits(cached)});
        Boundary(2);
        return cached;
    }
    void Overflow(const Record &record)
    {
        events.push_back({3, record.payload.id});
        Boundary(3);
    }
    float Time()
    {
        events.push_back({4, Bits(now)});
        Boundary(4);
        return now;
    }
    void BadSprite(const Record &record)
    {
        events.push_back({5, record.payload.id, static_cast<unsigned>(record.remaining), Bits(record.next)});
        Boundary(5);
    }
    runtime::EmitterPrepare Prepare(Record &, float)
    {
        Boundary(6);
        return runtime::EmitterPrepare::Emit; // Explicit world-owner fixture.
    }
    bool Create(const Record &record, float time)
    {
        events.push_back({7, record.payload.id, Bits(time),
                          static_cast<unsigned>(record.remaining), Bits(record.next)});
        Boundary(7);
        return static_cast<int>(created++) != failAt;
    }
};
Record Read()
{
    Record record;
    std::cin >> record.tag >> record.total >> record.burst >> record.remaining >> record.active;
    record.interval = Real(); record.next = Real();
    std::cin >> record.model >> record.payload.id >> record.payload.kind >> record.payload.retained;
    return record;
}
void PrintEvents(const Host &host)
{
    std::cout << '[';
    bool first = true;
    for (const auto &row : host.events)
    {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[';
        for (std::size_t i = 0; i != row.size(); ++i)
        {
            if (i) std::cout << ',';
            std::cout << row[i];
        }
        std::cout << ']';
    }
    std::cout << ']';
}
void Print(const Pool &pool, const Host &host)
{
    std::cout << "{\"events\":";
    PrintEvents(host);
    std::cout << ",\"records\":[";
    bool first = true;
    for (const auto &r : pool.Records())
    {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[' << r.tag << ',' << static_cast<unsigned>(r.total) << ','
                  << static_cast<unsigned>(r.burst) << ',' << static_cast<unsigned>(r.remaining)
                  << ',' << static_cast<unsigned>(r.active) << ',' << Bits(r.interval) << ','
                  << Bits(r.next) << ',' << (r.model != 0) << ',' << r.payload.id << ','
                  << r.payload.kind << ',' << r.payload.retained << ']';
    }
    std::cout << "]}\n";
}
void Input(const std::string &mode)
{
    unsigned count;
    while (std::cin >> count)
    {
        Pool pool;
        Host host{pool};
        host.now = Real(); host.cached = Real(); host.afterLoad = Real();
        std::cin >> host.model >> host.failAt;
        Check(count <= 128, "fixture capacity");
        for (unsigned i = 0; i != count; ++i) Seed(pool, i) = Read();
        if (mode == "add") pool.Add(Read(), host);
        else if (mode == "frame") pool.Frame(host);
        else if (mode == "video") pool.ResetVideoRecords();
        else if (mode == "reset") pool.ResetHud();
        else throw std::runtime_error("unknown mode");
        Print(pool, host);
    }
}
Record Example(unsigned id = 1)
{
    Record record;
    record.tag = 43;
    record.total = record.remaining = 3;
    record.burst = 1;
    record.active = 1;
    record.interval = .1f;
    record.next = 9;
    record.model = 1;
    record.payload = {id, id % 3, 17};
    return record;
}
void Units()
{
    Pool pool;
    Host host{pool};
    for (unsigned i = 0; i != 128; ++i)
        Check(pool.Add(Example(i + 1), host) == EmitterResult::Complete, "shared producer slot");
    Check(pool.Add(Example(), host) == EmitterResult::Full, "shared 128 cap");
    Check(host.events.back() == std::vector<unsigned>({3, 1}), "overflow callback");
    for (unsigned i = 0; i != 128; ++i)
    {
        const auto &record = pool.Records()[i];
        Check(record.payload.id == i + 1 && record.payload.kind == (i + 1) % 3,
              "all producers share insertion order");
        Check(record.total == 3 && record.remaining == 3 && record.active == 1, "Add state");
    }
    Seed(pool, 5).remaining = 0;
    Seed(pool, 5).active = 2;
    Check(pool.Add(Example(999), host) == EmitterResult::Complete, "remaining controls reuse");
    Check(pool.Records()[5].payload.id == 999 && pool.Records()[5].active == 1, "first free slot");
    pool.ResetHud();
    Check(pool.Records()[5].payload.id == 999 && pool.Records()[5].remaining == 3, "ResetHUD preserves");
    Seed(pool, 1).active = 0; Seed(pool, 2).active = 2;
    pool.ResetVideoRecords();
    for (unsigned i = 0; i != 128; ++i)
    {
        const auto &record = pool.Records()[i];
        Check(record.remaining == (i == 1 || i == 2 ? 3 : 0), "conditional video reset");
        Check(record.next == 9 && record.burst == 1 && record.model == 1 &&
              record.payload.retained == 17, "video retains nonreset fields");
    }
    pool.Initialize();
    for (const auto &record : pool.Records())
        Check(record.tag == 0 && record.total == 0 && record.burst == 0 && record.model == 0 &&
              record.payload.id == 0 && record.next == 0, "Init clears records");

    for (int where = 0; where != 3; ++where)
    {
        pool.Initialize(); Host cb{pool}; cb.invalidate = where;
        Check(pool.Add(Example(), cb) == EmitterResult::Invalidated, "Add video invalidation");
        cb.invalidate = -1;
        Check(pool.Add(Example(), cb) == EmitterResult::Complete, "Add guard released");
    }
    for (int where = 4; where != 8; ++where)
    {
        pool.Initialize(); Host cb{pool}; Seed(pool, 0) = Example();
        if (where == 5) Seed(pool, 0).model = 0;
        cb.invalidate = where;
        Check(pool.Frame(cb) == EmitterResult::Invalidated, "Frame video invalidation");
        cb.invalidate = -1;
        Check(pool.Frame(cb) == EmitterResult::Complete, "Frame guard released");
    }
    pool.Initialize();
    for (unsigned i = 0; i != 128; ++i) Seed(pool, i) = Example(i + 1);
    Host overflow{pool}; overflow.invalidate = 3;
    Check(pool.Add(Example(), overflow) == EmitterResult::Invalidated, "overflow invalidation");
    overflow.invalidate = -1;
    Check(pool.Add(Example(), overflow) == EmitterResult::Complete, "overflow guard released");
    for (int where = 0; where != 8; ++where)
    {
        pool.Initialize(); Host cb{pool}; cb.recurseAt = where;
        if (where < 3) Check(pool.Add(Example(), cb) == EmitterResult::Complete, "Add recursion protection");
        else if (where >= 4)
        {
            Seed(pool, 0) = Example();
            if (where == 5) Seed(pool, 0).model = 0;
            Check(pool.Frame(cb) == EmitterResult::Complete, "Frame recursion protection");
        }
    }
    pool.Initialize(); Seed(pool, 0) = Example(); Seed(pool, 1) = Example(2);
    Host cb{pool}; cb.failAt = 0;
    Check(pool.Frame(cb) == EmitterResult::FactoryFailed, "failed factory aborts phase");
    Check(pool.Records()[0].remaining == 2 && pool.Records()[1].remaining == 3,
          "failure consumes only visited record");

    struct PrepareHost : Host
    {
        runtime::EmitterPrepare action{runtime::EmitterPrepare::Emit};
        runtime::EmitterPrepare Prepare(Record &r, float)
        {
            if (action == runtime::EmitterPrepare::StopFrame) r.total = r.remaining = 0;
            return action;
        }
    };
    for (auto action : {runtime::EmitterPrepare::NextRecord, runtime::EmitterPrepare::StopFrame})
    {
        pool.Initialize(); Seed(pool, 0) = Example(); Seed(pool, 1) = Example(2);
        PrepareHost prep{{pool}}; prep.action = action;
        Check(pool.Frame(prep) == (action == runtime::EmitterPrepare::StopFrame ?
              EmitterResult::Stopped : EmitterResult::Complete), "explicit prepare outcome");
        Check(prep.created == 0 && pool.Records()[0].next == 10.1f, "preparation follows consumption");
        Check(pool.Records()[1].remaining == (action == runtime::EmitterPrepare::StopFrame ? 3 : 2),
              "preparation traversal outcome");
    }
    struct AddingHost : Host
    {
        bool added{};
        bool Create(const Record &record, float now)
        {
            const bool result = Host::Create(record, now);
            if (!added)
            {
                added = true;
                Check(pool.Add(Example(2), *this) == EmitterResult::Complete, "Add from factory");
            }
            return result;
        }
    };
    pool.Initialize(); Seed(pool, 0) = Example();
    AddingHost adding{{pool}};
    Check(pool.Frame(adding) == EmitterResult::Complete && adding.created == 2,
          "new later record participates in current traversal");
    std::cout << "checks=" << checks << " failed=0\n";
}
int main(int argc, char **argv)
{
    try { if (argc == 1) Units(); else Input(argv[1]); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
