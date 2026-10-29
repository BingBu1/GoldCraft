// SPDX-License-Identifier: GPL-3.0-or-later
// Source preparation only. Real Feature3 owner, controlled clone side effects.
#include "../../native/csol/scenario/feature3.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
using namespace csol::scenario;
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
void Require(bool value, const char* label)
{ Check(value, label); if (!value) throw std::runtime_error(label); }
struct State { bool current{true}, clockAvailable{true}; float now{10}; };
bool Current(const void* p) { return static_cast<const State*>(p)->current; }
bool Clock(const void* p, float& now)
{ const auto& s = *static_cast<const State*>(p); now = s.now; return s.clockAvailable; }
struct Harness
{
    std::vector<std::string> trace;
    State state;
    std::shared_ptr<Feature3> owner = std::make_shared<Feature3>();
    Feature3Query Query() const { return {Current, &state, Clock, &state}; }
};
struct Plan
{
    std::vector<std::string>* trace{};
    std::string name;
    bool empty{}, fail{}, emptyError{};
    std::function<void()> clone, destroy, sourceDestroy;
};
class Condition final : public Feature3Condition
{
public:
    explicit Condition(std::shared_ptr<Plan> p, bool temporary = false) : plan(std::move(p)), copy(temporary) {}
    ~Condition() override
    {
        if (copy) { plan->trace->push_back("d" + plan->name); if (plan->destroy) plan->destroy(); }
        else if (plan->sourceDestroy) { plan->trace->push_back("s" + plan->name); plan->sourceDestroy(); }
    }
    bool Clone(std::unique_ptr<Feature3Condition>& out, ConfigError& error) const override
    {
        Check(!copy, "temporary callable is never evaluated or cloned again");
        plan->trace->push_back("c" + plan->name);
        if (!plan->empty) out = std::make_unique<Condition>(plan, true);
        if (plan->clone) plan->clone();
        if (plan->fail && !plan->emptyError) error = {17, "fixture clone failure"};
        return !plan->fail;
    }
private:
    std::shared_ptr<Plan> plan;
    bool copy;
};
struct Probe { std::shared_ptr<Plan> plan; std::shared_ptr<Condition> source; };
Probe Make(Harness& h, std::string name)
{
    auto p = std::make_shared<Plan>(); p->trace = &h.trace; p->name = std::move(name);
    return {p, std::make_shared<Condition>(p)};
}
void Put(Harness& h, std::int32_t key, Feature3Timing t,
    std::shared_ptr<const Feature3Condition> a = {}, std::shared_ptr<const Feature3Condition> b = {})
{ Require(h.owner->Put(key, {t, std::move(a), std::move(b)}), "install authored live record"); }

void Classification()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity(), max = std::numeric_limits<float>::max();
    const float above = std::nextafter(1.0f, 2.0f);
    struct Row { Feature3Timing t; float now; int category; };
    const Row rows[]{
        {{2,2,9,20},10,0}, {{0,2,10,20},10,1}, {{0,10,0,20},10,2}, {{0,0,0,10},10,3},
        {{1,0,16777216.0f,0},16777216.0f,3}, {{0,-1,0,16777216.0f},16777216.0f,3},
        {{0,0,above,0},1,0}, {{0,0,0,above},1,1}, {{0,above-1,0,above},1,2},
        {{nan,0,0,20},10,1}, {{0,0,nan,20},10,1}, {{0,nan,0,20},10,2},
        {{0,0,0,nan},10,3}, {{0,0,100,200},nan,3},
        {{max,0,max,0},max,0}, {{0,-max,0,max},max,1},
        {{inf,0,-inf,20},10,1}, {{0,inf,0,inf},10,2}, {{-0.0f,0,0,0},0,3}};
    for (const auto& row : rows)
    {
        Harness h; h.state.now = row.now; Put(h, -70000, row.t);
        int category = 87; ConfigError error;
        Check(h.owner->Classify(-70000, h.Query(), category, error) && category == row.category,
            "ordered float32 classification matches independent boundary table");
        bool active = row.category == 3;
        Check(h.owner->AnyActive(h.Query(), active, error) && active == (row.category != 3),
            "every non-three category is active, including zero");
    }
}
void MissingAndClock()
{
    Harness h; auto query = h.Query(); query.clock = nullptr; ConfigError error;
    int category = 87; bool active = true;
    Check(h.owner->Classify(INT32_MIN, query, category, error) && category == 3,
        "missing signed key needs no clock and classifies as host-defined absent");
    Check(h.owner->AnyActive(query, active, error) && !active, "empty tree needs no clock");
    Feature3 unowned; category = 87; active = true;
    Check(!unowned.Put(1, {}) && !unowned.Classify(1, query, category, error) && category == 87 &&
        !unowned.AnyActive(query, active, error) && active, "queries and insertion require shared ownership");
    query.current = nullptr;
    Check(!h.owner->AnyActive(query, active, error) && active, "missing owner check is not treated as an empty tree");
    for (bool absentCallback : {true, false})
    {
        auto a = Make(h, "A"), b = Make(h, "B"); Put(h, 7, {0,0,0,20}, a.source, b.source);
        query = h.Query(); if (absentCallback) query.clock = nullptr; else h.state.clockAvailable = false;
        h.trace.clear(); category = 87; error = {};
        Check(!h.owner->Classify(7, query, category, error) && category == 87 && !error.message.empty() &&
            h.trace == std::vector<std::string>({"cA","cB","dB","dA"}),
            "unavailable clock after two clones cancels output and destroys both in reverse order");
    }
}
void StagedSourcesAndTime()
{
    Harness h; h.state.now = 10;
    auto a = Make(h, "A"), old = Make(h, "old"), next = Make(h, "new");
    Put(h, 7, {0,10,200,100}, a.source, old.source);
    a.plan->clone = [&] {
        Check(h.owner->SetTiming(7, {100,-100,100,100}), "first clone can change live timing");
        Check(h.owner->SetConditions(7, {}, next.source), "first clone replaces second source before its lookup");
    };
    next.plan->clone = [&] { h.owner->SetTiming(7, {100,-100,-1,30}); h.state.now = 20; };
    next.plan->destroy = [&] { h.owner->SetTiming(7, {1000,0,1000,1000}); h.state.now = -1000; };
    int category = 87; ConfigError error;
    Check(h.owner->Classify(7, h.Query(), category, error) && category == 2 &&
        h.trace == std::vector<std::string>({"cA","cnew","dnew","dA"}),
        "A/B are early, second source and C/D/time are late, cleanup does not recompute the saved category");

    Harness pinned; auto first = Make(pinned, "A"), middle = Make(pinned, "B"), last = Make(pinned, "C");
    Put(pinned, 7, {0,0,0,20}, first.source, middle.source);
    first.plan->clone = [&] { pinned.owner->SetConditions(7, {}, middle.source); };
    first.plan->sourceDestroy = [&] { pinned.owner->SetConditions(7, {}, last.source); };
    first.source.reset();
    Check(pinned.owner->Classify(7, pinned.Query(), category, error) && category == 1 &&
        pinned.trace == std::vector<std::string>({"cA","sA","cC","dC","dA"}),
        "source1 stays alive through Clone and its destructor can change source2 before acquisition");
}
void SignedOrderAndLiveSuccessor()
{
    Harness ordered;
    for (auto key : {INT32_MAX,65536,0,-1,INT32_MIN})
    { auto p = Make(ordered, std::to_string(key)); Put(ordered, key, {}, p.source); }
    ConfigError error; bool active = true;
    Check(ordered.owner->AnyActive(ordered.Query(), active, error) && !active &&
        ordered.trace == std::vector<std::string>({"c-2147483648","d-2147483648","c-1","d-1","c0","d0","c65536","d65536","c2147483647","d2147483647"}),
        "tree traversal uses signed full-width keys, not insertion order or low16 aliases");
    ordered.trace.clear(); ordered.owner->SetTiming(0, {0,0,0,20});
    Check(ordered.owner->AnyActive(ordered.Query(), active, error) && active &&
        ordered.trace == std::vector<std::string>({"c-2147483648","d-2147483648","c-1","d-1","c0","d0"}),
        "first live non-three node ends traversal immediately");

    Harness live; auto start = Make(live, "start"), removed = Make(live, "removed"), tail = Make(live, "tail");
    auto early = Make(live, "early"), inserted = Make(live, "inserted");
    Put(live, -2, {}, start.source); Put(live, -1, {0,0,0,20}, removed.source); Put(live, 10, {}, tail.source);
    start.plan->destroy = [&] {
        Check(live.owner->Remove(-1), "remove upcoming active node during clone cleanup");
        Put(live, -3, {0,0,0,20}, early.source); Put(live, 0, {}, inserted.source);
    };
    active = true;
    Check(live.owner->AnyActive(live.Query(), active, error) && !active &&
        live.trace == std::vector<std::string>({"cstart","dstart","cinserted","dinserted","ctail","dtail"}),
        "post-cleanup live successor sees inserted later key, skips removed next key and never restarts at earlier insertion");
}
void PerNodeTime()
{
    Harness h; h.state.now = 20; auto a = Make(h, "A"), b = Make(h, "B");
    Put(h, 1, {}, a.source); Put(h, 2, {0,0,0,10}, b.source);
    b.plan->clone = [&] { h.state.now = 5; };
    bool active = false; ConfigError error;
    Check(h.owner->AnyActive(h.Query(), active, error) && active &&
        h.trace == std::vector<std::string>({"cA","dA","cB","dB"}),
        "time is sampled after each node's clones, not once for the tree");
}
void Cancellation()
{
    // Mutations occur only in clone/destructor callbacks. Current and Clock
    // remain pure state reads, including their failure outcomes.
    for (int phase = 0; phase != 4; ++phase) for (int mutation = 0; mutation != 4; ++mutation)
        for (bool wholeTree : {false, true})
    {
        Harness h; auto a = Make(h, "A"), b = Make(h, "B"); Put(h, 7, {0,0,0,20}, a.source, b.source);
        const auto cancel = [&] {
            if (mutation == 0) h.state.current = false;
            if (mutation == 1) Check(h.owner->Remove(7), "current node removal succeeds");
            if (mutation == 2) Put(h, 7, {0,0,0,50});
            if (mutation == 3) h.owner->Invalidate();
        };
        if (phase == 0) a.plan->clone = cancel;
        if (phase == 1) b.plan->clone = cancel;
        if (phase == 2) b.plan->destroy = cancel;
        if (phase == 3) a.plan->destroy = cancel;
        ConfigError error; int category = 87; bool active = false;
        const bool ok = wholeTree ? h.owner->AnyActive(h.Query(), active, error) :
            h.owner->Classify(7, h.Query(), category, error);
        Check(!ok && category == 87 && !active, "owner or current-node retirement cancels without publishing category or aggregate");
        Check(h.trace == (phase == 0 ? std::vector<std::string>{"cA","dA"} :
            std::vector<std::string>{"cA","cB","dB","dA"}),
            "cancellation at either clone or destructor still releases established clones in reverse order");
        if (mutation == 3) Check(!h.owner->Valid() && !h.owner->Put(9,{}) && !h.owner->SetTiming(9,{}) &&
            !h.owner->SetConditions(9,{},{}) && !h.owner->Remove(9), "invalidation is terminal for record writes");
    }
}
void CloneResults()
{
    for (int failing = 0; failing != 2; ++failing) for (bool empty : {false, true})
        for (bool emptyError : {false, true})
    {
        Harness h; auto a = Make(h, "A"), b = Make(h, "B"); Put(h, 7, {0,0,0,20}, a.source, b.source);
        auto failure = failing ? b.plan : a.plan; failure->fail = true; failure->empty = empty; failure->emptyError = emptyError;
        int category = 87; ConfigError error;
        Check(!h.owner->Classify(7, h.Query(), category, error) && category == 87 &&
            error.message == (emptyError ? "feature3 condition clone failed" : "fixture clone failure"),
            "Clone false preserves output and retains diagnostic or supplies fallback");
        const std::vector<std::string> expected = failing ?
            (empty ? std::vector<std::string>{"cA","cB","dA"} : std::vector<std::string>{"cA","cB","dB","dA"}) :
            (empty ? std::vector<std::string>{"cA"} : std::vector<std::string>{"cA","dA"});
        Check(h.trace == expected, "Clone failure cleans any returned partial clone and prior clone without publishing");
        h.trace.clear(); bool active = false; error = {};
        Check(!h.owner->AnyActive(h.Query(), active, error) && !active && h.trace == expected && !error.message.empty(),
            "failed node classification also preserves aggregate output and cleanup order");
    }
    Harness h; auto a = Make(h, "A"), b = Make(h, "B"); a.plan->empty = b.plan->empty = true;
    Put(h, 7, {0,0,0,20}, a.source, b.source); int category = 87; ConfigError error;
    Check(h.owner->Classify(7, h.Query(), category, error) && category == 1 &&
        h.trace == std::vector<std::string>({"cA","cB"}), "successful null clones are valid and need no destruction");
    Harness reentrant; auto first = Make(reentrant, "A"), second = Make(reentrant, "B");
    Put(reentrant, 7, {0,0,0,20}, first.source, second.source); Put(reentrant, 8, {});
    second.plan->fail = true; int nestedCategory = 91; error = {}; category = 87;
    second.plan->destroy = [&] {
        auto query = reentrant.Query(); query.clock = nullptr;
        Check(!reentrant.owner->Classify(8, query, nestedCategory, error) && nestedCategory == 91 &&
            error.message == "feature3 clock is unavailable", "clone cleanup can reenter using caller-owned diagnostic");
    };
    Check(!reentrant.owner->Classify(7, reentrant.Query(), category, error) && category == 87 &&
        error.byteOffset == 17 && error.message == "fixture clone failure" &&
        reentrant.trace == std::vector<std::string>({"cA","cB","dB","dA"}),
        "outer Clone failure publishes its own diagnostic after both cleanup callbacks return");
}
void KeepAlive()
{
    for (bool wholeTree : {false, true})
    {
        Harness h; auto a = Make(h, "A"), b = Make(h, "B"); Put(h, 7, {0,0,0,20}, a.source, b.source);
        std::weak_ptr<Feature3> weak = h.owner; auto* raw = h.owner.get();
        a.plan->clone = [&] { h.owner.reset(); Check(!weak.expired(), "query pins last owner before callbacks"); };
        a.plan->destroy = [&] { Check(!weak.expired(), "owner remains alive through final clone destruction"); };
        int category = 87; bool active = false; ConfigError error;
        const bool ok = wholeTree ? raw->AnyActive(h.Query(), active, error) : raw->Classify(7, h.Query(), category, error);
        Check(ok && (wholeTree ? active : category == 1) && weak.expired() &&
            h.trace == std::vector<std::string>({"cA","cB","dB","dA"}),
            "query keeps the actual object alive until return without leaking the removed last external owner");
    }
}
void CloneExceptions()
{
    // Clone writes its optional output before invoking this throwing callback.
    // Both standard and non-standard exceptions must take the production catch.
    for (int source = 0; source != 2; ++source) for (bool empty : {false, true})
        for (bool nonStandard : {false, true}) for (bool wholeTree : {false, true})
    {
        Harness h; auto a = Make(h, "A"), b = Make(h, "B"); Put(h, 7, {0,0,0,20}, a.source, b.source);
        const auto throwing = source ? b.plan : a.plan; throwing->empty = empty;
        int throws{};
        throwing->clone = [&] { ++throws; if (nonStandard) throw 73; throw std::runtime_error("authored Clone throw"); };
        const auto expected = source ?
            (empty ? std::vector<std::string>{"cA","cB","dA"} : std::vector<std::string>{"cA","cB","dB","dA"}) :
            (empty ? std::vector<std::string>{"cA"} : std::vector<std::string>{"cA","dA"});
        // Exceed the production depth limit on the same owner. One successful
        // retry would not detect a leaked depth increment from a caught throw.
        for (int attempt = 0; attempt != 33; ++attempt)
        {
            ConfigError error; int category = 87; bool active = true; h.trace.clear();
            const bool ok = wholeTree ? h.owner->AnyActive(h.Query(), active, error) : h.owner->Classify(7, h.Query(), category, error);
            Check(!ok && category == 87 && active && throws == attempt + 1 &&
                error.message == "feature3 condition clone threw",
                "each real Clone exception preserves both output sentinels and reaches Clone again on the same owner");
            Check(h.trace == expected, "thrown Clone releases partial output and earlier clone in reverse order");
        }
        throwing->clone = {}; h.trace.clear(); ConfigError error; int category = 87; bool active = false;
        const bool recovered = wholeTree ? h.owner->AnyActive(h.Query(), active, error) : h.owner->Classify(7, h.Query(), category, error);
        Check(recovered && (wholeTree ? active : category == 1) && error.message.empty() && throws == 33,
            "same owner and query interface succeed after more caught exceptions than the nested depth limit");
    }
    // A cancellation can occur before the throw or during cleanup after the
    // exception was caught. Neither is a diagnostic failure of a live owner.
    for (int source = 0; source != 2; ++source) for (int mutation = 0; mutation != 4; ++mutation)
        for (bool inCleanup : {false, true}) for (bool wholeTree : {false, true})
    {
        Harness h; auto a = Make(h, "A"), b = Make(h, "B"); Put(h, 7, {0,0,0,20}, a.source, b.source);
        const auto cancel = [&] {
            if (mutation == 0) h.state.current = false;
            if (mutation == 1) Check(h.owner->Remove(7), "throw case removes current record");
            if (mutation == 2) Put(h, 7, {0,0,0,50});
            if (mutation == 3) h.owner->Invalidate();
        };
        const auto throwing = source ? b.plan : a.plan;
        throwing->clone = [&] { if (!inCleanup) cancel(); throw std::runtime_error("cancel then throw"); };
        if (inCleanup) throwing->destroy = cancel;
        const auto original = h.owner;
        ConfigError error; int category = 87; bool active = true;
        const bool ok = wholeTree ? h.owner->AnyActive(h.Query(), active, error) : h.owner->Classify(7, h.Query(), category, error);
        Check(!ok && category == 87 && active && error.message.empty() && error.byteOffset == 0,
            "owner/node cancellation takes precedence over caught Clone exception and emits no diagnostic");
        Check(h.trace == (source ? std::vector<std::string>{"cA","cB","dB","dA"} :
            std::vector<std::string>{"cA","dA"}), "cancelled exception still cleans all established clone outputs");
        throwing->clone = {}; throwing->destroy = {}; h.trace.clear(); error = {}; category = 87;
        if (mutation == 0) h.state.current = true;
        if (mutation == 1) Put(h, 7, {}); // Recreate the removed key as an ended node.
        if (mutation == 3)
        {
            h.owner = std::make_shared<Feature3>(); Put(h, 7, {0,0,0,20});
            bool oldActive = true; int oldCategory = 87; ConfigError oldError;
            const bool oldOk = wholeTree ? original->AnyActive(h.Query(), oldActive, oldError) :
                original->Classify(7, h.Query(), oldCategory, oldError);
            Check(!oldOk && oldActive && oldCategory == 87 && oldError.message.empty(),
                "invalidated owner remains retired while the replacement is a distinct owner");
        }
        active = mutation == 1;
        const bool recovered = wholeTree ? h.owner->AnyActive(h.Query(), active, error) : h.owner->Classify(7, h.Query(), category, error);
        Check(recovered && (wholeTree ? active == (mutation != 1) : category == (mutation == 1 ? 3 : 1)) &&
            error.message.empty() && (h.owner == original) == (mutation != 3) &&
            h.trace == (mutation == 0 ? std::vector<std::string>{"cA","cB","dB","dA"} : std::vector<std::string>{}),
            "later query uses restored current state or the replacement node; only Invalidate needs an independent owner");
    }
    for (int source = 0; source != 2; ++source)
    {
        Harness h; auto a = Make(h, "A"), b = Make(h, "B"); Put(h, 7, {0,0,0,20}, a.source, b.source); Put(h, 8, {});
        const auto throwing = source ? b.plan : a.plan;
        throwing->clone = [] { throw std::runtime_error("outer Clone throw"); };
        ConfigError error; int category = 87, cleanups{};
        const auto reenter = [&] {
            ++cleanups; auto query = h.Query(); query.clock = nullptr; int nested = 91;
            Check(!h.owner->Classify(8, query, nested, error) && nested == 91 &&
                error.message == "feature3 clock is unavailable", "exception cleanup reenters with same caller diagnostic");
        };
        a.plan->destroy = reenter; b.plan->destroy = reenter;
        Check(!h.owner->Classify(7, h.Query(), category, error) && category == 87 && cleanups == source + 1 &&
            error.message == "feature3 condition clone threw" &&
            h.trace == (source ? std::vector<std::string>{"cA","cB","dB","dA"} : std::vector<std::string>{"cA","dA"}),
            "outer caught exception restores its diagnostic after reverse cleanup and nested query returns");
    }
}
}
namespace
{
struct StopClock
{
    mutable int reads{};
    int failAt{};
    static bool Read(const void* context, float& value)
    {
        const auto& clock = *static_cast<const StopClock*>(context);
        const int read = ++clock.reads;
        value = read == 1 ? 10.0f : read == 2 ? 20.0f : 30.0f;
        return read != clock.failAt;
    }
};
int StopCategory(Harness& h, float now)
{
    h.state.now = now; int category = -1; ConfigError error;
    Require(h.owner->Classify(14, h.Query(), category, error), "observe actual stopped record through classification");
    return category;
}
void StopClassificationAndFade()
{
    const Feature3Timing timings[]{{4,5,8,40}, {0,5,0,40}, {0,5,0,12}, {0,5,0,10}};
    for (int initial = 0; initial != 5; ++initial)
    {
        Harness h; if (initial != 4) Put(h, 14, timings[initial]);
        StopClock clock; auto query = h.Query(); query.clock = StopClock::Read; query.clockOwner = &clock;
        bool changed = true; ConfigError error;
        Check(h.owner->Stop(14, false, query, changed, error) && changed == (initial < 2) &&
            clock.reads == (initial < 2 ? 3 : initial == 4 ? 0 : 1) && error.message.empty(),
            "only categories zero and one reset; missing key and categories two/three do not consume reset clocks");
        Check(StopCategory(h, 10) == (initial < 2 ? 0 : initial == 4 ? 3 : initial),
            "Stop leaves inactive/fading records unchanged and never inserts an absent key");
    }
    for (bool clearFade : {false, true})
    {
        Harness h; Require(h.owner->Put(14, {{4,5,8,40}, {}, {}, 77}), "seed the real record including intermediate duration");
        StopClock clock; auto query = h.Query(); query.clock = StopClock::Read; query.clockOwner = &clock;
        bool changed{}; ConfigError error;
        Check(h.owner->Stop(14, clearFade, query, changed, error) && changed && clock.reads == 3 &&
            StopCategory(h, 19) == 0 && StopCategory(h, 21) == 1 &&
            StopCategory(h, 30) == (clearFade ? 3 : 2) && StopCategory(h, 35) == 3,
            "two separate reset clocks set c=20 and d=30+fade; a is zero and false preserves b");
    }
}
void StopFailurePrefix()
{
    for (int failure : {2,3})
    {
        Harness h; Put(h, 14, {10,5,9,100});
        StopClock clock; clock.failAt = failure; auto query = h.Query();
        query.clock = StopClock::Read; query.clockOwner = &clock; bool changed = true; ConfigError error;
        Check(!h.owner->Stop(14, false, query, changed, error) && !changed && clock.reads == failure &&
            error.message == "feature3 stop clock is unavailable", "failed reset clock leaves changed false and reports real failure");
        Check(StopCategory(h, 10) == (failure == 2 ? 1 : 0) && StopCategory(h, 21) == 1 && StopCategory(h, 96) == 2,
            "failed reset preserves the completed a/c writes while old d and fade remain observable");
    }
}
void StopAfterCloneCleanup()
{
    Harness h; auto condition = Make(h, "A"); Put(h, 14, {4,5,8,40}, condition.source);
    condition.plan->destroy = [&] { Require(h.owner->SetTiming(14, {99,7,8,100}), "clone cleanup changes live fade before Stop writes"); };
    bool changed{}; ConfigError error;
    Check(h.owner->Stop(14, false, h.Query(), changed, error) && changed && h.trace == std::vector<std::string>({"cA","dA"}),
        "Stop waits for original classifier clone cleanup before sampling current fade");
    condition.plan->destroy = {};
    Check(StopCategory(h, 16) == 2 && StopCategory(h, 17) == 3,
        "reset retains cleanup-mutated b=7, clears a and uses the actual same record");
}
}
int main()
{
    try { Classification(); MissingAndClock(); StagedSourcesAndTime(); SignedOrderAndLiveSuccessor(); PerNodeTime(); Cancellation(); CloneResults(); KeepAlive(); CloneExceptions();
        StopClassificationAndFade(); StopFailurePrefix(); StopAfterCloneCleanup(); }
    catch (const std::exception& error) { ++failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
