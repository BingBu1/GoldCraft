#pragma once
#include "goldcraft/map_edits.hpp"
#include "goldcraft/host_map_api.hpp"
#include <algorithm>
#include <tuple>

namespace goldcraft::server_edits {
// The caller owns candidate. All allocation (including the removal journal)
// precedes Replace; the ledger publication after physics/PVS/PAS cannot fail.
inline void publish(edits::Ledger &current, edits::Ledger &candidate, IHostMapPhysics &physics) {
    std::vector<HostMapCut> cuts;
    cuts.reserve(candidate.state().cuts.size());
    for (const auto &cut : candidate.state().cuts) {
        HostMapCut copy{cut.target.slot, cut.target.serial, cut.target.model, {}, {}};
        for (int axis = 0; axis < 3; ++axis) {
            copy.min[axis] = cut.box.min[axis];
            copy.max[axis] = cut.box.max[axis];
        }
        cuts.push_back(copy);
    }
    if (!physics.Replace(candidate.state().epoch, candidate.state().revision, cuts.data(),
                         static_cast<std::uint32_t>(cuts.size())))
        throw ProtocolError(physics.LastError());
    current.swap(candidate);
}

template <class Alive> bool prune(edits::Ledger &current, IHostMapPhysics &physics, Alive alive) {
    std::vector<edits::Target> stale;
    for (const auto &cut : current.state().cuts)
        if (cut.target.slot && !alive(cut.target)) stale.push_back(cut.target);
    if (stale.empty()) return false;
    std::ranges::sort(stale, [](const auto &a, const auto &b) {
        return std::tie(a.slot, a.serial, a.model) < std::tie(b.slot, b.serial, b.model);
    });
    stale.erase(std::unique(stale.begin(), stale.end()), stale.end());
    auto candidate = current;
    for (const auto &target : stale) candidate.remove(target);
    publish(current, candidate, physics);
    return true;
}

// Background maintenance must not escape into the bridge's StartFrame catch:
// that would skip unrelated input, pose, damage, timeout and snapshot work.
// Request-time prune remains throwing so its current cut can be rejected.
class BackgroundPruner {
public:
    void reset() noexcept { failed_ = false; }
    template <class Alive, class Report>
    void run(edits::Ledger &current, IHostMapPhysics &physics, Alive alive, Report report) noexcept {
        try {
            prune(current, physics, alive);
            failed_ = false;
        } catch (const std::exception &failure) {
            report_failure(report, failure.what());
        } catch (...) {
            report_failure(report, "Unknown map edit cleanup failure");
        }
    }
private:
    template <class Report> void report_failure(Report &report, const char *reason) noexcept {
        // One diagnostic per consecutive failure episode; retry every frame.
        if (failed_) return;
        failed_ = true;
        try { report(reason); } catch (...) {} // Diagnostics cannot stop the bridge either.
    }
    bool failed_ = false;
};
} // namespace goldcraft::server_edits
