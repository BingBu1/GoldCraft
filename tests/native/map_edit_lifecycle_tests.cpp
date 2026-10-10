#define EXT_FUNC
#include "interface.h"
#include "map_edit_commit.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
long fail_after = -1;
bool forbid_allocation = false;
unsigned allocations = 0;
}
void *operator new(std::size_t size) {
    if (forbid_allocation || fail_after == 0) throw std::bad_alloc{};
    if (fail_after > 0) --fail_after;
    ++allocations;
    if (auto *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc{};
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
using namespace goldcraft;
struct Physics final : IHostMapPhysics {
    HostMapStats stats{};
    std::vector<HostMapCut> cuts;
    unsigned calls = 0;
    bool fail = false, fail_visibility = false, prohibit_post_commit = false;
    const edits::Ledger *ledger = nullptr;
    edits::Snapshot before;
    bool Replace(std::uint64_t epoch, std::uint64_t revision, const HostMapCut *p,
                 std::uint32_t count) override {
        ++calls;
        assert(!ledger || ledger->state() == before);
        if (fail) return false;
        std::vector<HostMapCut> next;
        if (count) next.assign(p, p + count);
        if (fail_visibility) return false;
        cuts.swap(next); stats.epoch = epoch; stats.revision = revision;
        if (prohibit_post_commit) forbid_allocation = true;
        return true;
    }
    void Reset(std::uint64_t epoch, std::uint64_t revision) override { stats = {epoch,revision,0,0,0,0}; cuts.clear(); }
    const char *LastError() const override { return "injected compile/visibility failure"; }
    HostMapStats Stats() const override { return stats; }
};
edits::Box box(float x) { return {{x,0,0},{x+16,16,16}}; }
template <class F> void rejects(F f) {
    bool threw = false; try { f(); } catch (const std::exception &) { threw = true; }
    assert(threw);
}
void background_failure_isolation(const edits::Ledger &original) {
    auto ledger = original;
    Physics physics;
    server_edits::BackgroundPruner background;
    const auto before_history = ledger.since(1);
    unsigned continued = 0, diagnostics = 0;
    auto alive = [](edits::Target target) { return target.slot == 11; };
    auto report = [&](const char *reason) { assert(reason && *reason); ++diagnostics; };
    auto frame = [&] {
        background.run(ledger, physics, alive, report);
        ++continued; // Work after MapMiningFrame/maintenance must still execute.
    };
    physics.fail = true;
    for (int i=0;i<32;++i) frame();
    assert(continued==32 && diagnostics==1 && physics.calls==32);
    assert(ledger.state()==original.state() && physics.stats.revision==0);
    const auto after_history = ledger.since(1);
    assert(after_history.size()==before_history.size());
    for (std::size_t i=0;i<after_history.size();++i)
        assert(after_history[i].payload==before_history[i].payload);
    physics.fail=false;
    frame();
    assert(continued==33 && diagnostics==1 && ledger.state().cuts.size()==2);
    assert(physics.stats.revision==ledger.state().revision);
    const auto warm_allocations=allocations, warm_calls=physics.calls;
    for (int i=0;i<4096;++i) frame();
    assert(continued==4129 && diagnostics==1);
    assert(allocations==warm_allocations && physics.calls==warm_calls);
    ledger=original; physics.fail_visibility=true;
    frame(); frame(); // A new failure episode after success emits once again.
    assert(continued==4131 && diagnostics==2 && ledger.state()==original.state());
    physics.fail_visibility=false;
    frame();
    assert(continued==4132 && diagnostics==2 && ledger.state().cuts.size()==2);
    ledger=original;
    // Allocation failure before Replace stays within the same production helper.
    fail_after=0;
    frame();
    fail_after=-1;
    assert(continued==4133 && diagnostics==3 && ledger.state()==original.state());
    frame();
    assert(continued==4134 && ledger.state().cuts.size()==2);
    ledger=original; physics.fail=true;
    unsigned throwing_diagnostics=0;
    background.run(ledger,physics,alive,[&](const char *) { ++throwing_diagnostics; throw 7; });
    ++continued;
    assert(continued==4135 && throwing_diagnostics==1 && ledger.state()==original.state());
    // Even an unexpected non-std exception at the background boundary is contained.
    background.reset();
    background.run(ledger,physics,[](edits::Target)->bool { throw 9; },report);
    ++continued;
    assert(continued==4136 && diagnostics==4 && ledger.state()==original.state());
    // Map reset re-arms reporting independently of recovery in the previous map.
    background.reset();
    background.run(ledger,physics,alive,report);
    ++continued;
    assert(continued==4137 && diagnostics==5 && ledger.state()==original.state());
    physics.fail=false;
    frame();
    assert(continued==4138 && ledger.state().cuts.size()==2);
    std::puts("background: 32 repeated failures+same-frame continuation, retry recovery, allocation/non-std/diagnostic failures contained, episode/reset diagnostics, 4096 no-op frames allocate0");
}
int main() {
    edits::Ledger original; original.reset(23);
    original.add({},box(0));
    original.add({10,100,2},box(0)); original.add({10,100,2},box(16));
    original.add({11,200,2},box(0)); // A separate instance of the same model.
    original.add({12,300,4},box(0));
    background_failure_isolation(original);
    auto alive = [](edits::Target target) { return target.slot == 11; };
    for (int failure = 0; failure < 2; ++failure) {
        auto ledger = original; Physics physics;
        physics.ledger = &ledger; physics.before = ledger.state();
        physics.fail = failure == 0; physics.fail_visibility = failure == 1;
        const auto history = ledger.since(1);
        rejects([&]{server_edits::prune(ledger,physics,alive);});
        assert(ledger.state() == original.state() && physics.stats.revision == 0);
        const auto after = ledger.since(1); assert(after.size() == history.size());
        for (std::size_t i=0; i<after.size(); ++i) assert(after[i].payload == history[i].payload);
        physics.fail = physics.fail_visibility = false;
        assert(server_edits::prune(ledger,physics,alive)); // Retry has the intact prior journal.
        assert(ledger.state().revision == original.state().revision + 2);
        assert(ledger.state().cuts.size() == 2 && physics.cuts.size() == 2);
        assert(ledger.state().cuts[1].target.slot == 11);
        const auto removed = ledger.since(original.state().revision); assert(removed.size() == 2);
        edits::Replica peer; peer.reset(23); peer.accept(original.state());
        for (const auto &message : removed) {
            const auto d = edits::delta(message.payload);
            assert(d.operation == edits::Operation::remove_target);
            assert(peer.accept(d) == edits::Applied::changed);
        }
        assert(peer.state() == ledger.state());
    }
    unsigned failures = 0;
    for (long allocation = 0; allocation < 100; ++allocation) {
        auto ledger = original; Physics physics;
        fail_after = allocation;
        bool success = false;
        try { success = server_edits::prune(ledger,physics,alive); }
        catch (const std::bad_alloc &) { ++failures; }
        fail_after = -1;
        if (success) break;
        assert(ledger.state() == original.state() && physics.stats.revision == 0);
    }
    assert(failures >= 6);
    auto ledger = original; Physics physics;
    physics.prohibit_post_commit = true;
    assert(server_edits::prune(ledger,physics,alive));
    forbid_allocation = false;
    const auto warm_allocations = allocations, calls = physics.calls;
    for (int i=0;i<4096;++i) assert(!server_edits::prune(ledger,physics,alive));
    assert(allocations == warm_allocations && physics.calls == calls);
    // Live targets (including a suspended SOLID_NOT target at the adapter) are
    // retained; same-model instances and world cuts never share removal keys.
    auto all_live = original; Physics idle;
    assert(!server_edits::prune(all_live,idle,[](auto){return true;}));
    assert(all_live.state() == original.state() && idle.calls == 0);
    edits::Ledger only_dynamic; only_dynamic.reset(23);
    only_dynamic.add({10,100,2},box(0)); only_dynamic.add({10,100,2},box(16));
    Physics empty;
    assert(server_edits::prune(only_dynamic,empty,[](auto){return false;}));
    assert(only_dynamic.state().cuts.empty() && empty.cuts.empty());
    assert(only_dynamic.state().revision==4 && empty.stats.revision==4);
    std::printf("ledger: rollback+retry, delta replay, %u allocation failures, commit noexcept, 4096 no-op frames allocate0\n",failures);
}
