#pragma once
#include "goldcraft/map_edits.hpp"
#include "work_history.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <ostream>

namespace goldcraft::client_map {
enum class EditStage : std::size_t {
    identity, copy, physics_prepare, renderer_begin, physics_poll,
    renderer_poll, renderer_commit, collision_commit, cancel, count
};
inline constexpr std::array<const char*, static_cast<std::size_t>(EditStage::count)> edit_stage_names{
    "identity", "copy", "physicsPrepare", "rendererBegin", "physicsPoll",
    "rendererPoll", "rendererCommit", "collisionCommit", "cancel"};
struct EditWorkIdentity {
    std::uint64_t attempt{}, epoch{}, revision{}, ticket{}, cuts_hash{};
    std::uint32_t cuts{};
};
inline EditWorkIdentity edit_work_identity(const edits::Snapshot& snapshot, std::uint64_t attempt) noexcept {
    // Stable little-endian field hash, independent of struct padding/pointers.
    // Diagnostic correlation only; not an authenticity or collision proof.
    std::uint64_t hash = 14695981039346656037ull;
    const auto field = [&](std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) { hash ^= (value >> (i * 8)) & 255; hash *= 1099511628211ull; }
    };
    for (const auto& cut : snapshot.cuts) {
        field(cut.id, 8); field(cut.target.slot, 4); field(cut.target.serial, 4); field(cut.target.model, 4);
        for (const auto value : cut.box.min) field(std::bit_cast<std::uint32_t>(value), 4);
        for (const auto value : cut.box.max) field(std::bit_cast<std::uint32_t>(value), 4);
    }
    return {attempt, snapshot.epoch, snapshot.revision, 0, hash, static_cast<std::uint32_t>(snapshot.cuts.size())};
}
struct EditWorkHistory {
    static constexpr double threshold_ms = 2;
    static constexpr std::size_t capacity = 64;
    struct Counter { std::uint64_t count{}, failed{}; double last_ms{}, max_ms{}; };
    struct Event { EditWorkIdentity identity; EditStage stage{}; double elapsed_ms{}, finished{}; bool failed{}; };
    std::array<Counter, edit_stage_names.size()> counters{};
    std::array<Event, capacity> events{};
    std::uint64_t event_count{};

    void record(EditStage stage, const EditWorkIdentity& identity, double elapsed_ms,
                double finished, bool failed = false) noexcept {
        auto& counter = counters[static_cast<std::size_t>(stage)];
        ++counter.count; counter.failed += failed; counter.last_ms = elapsed_ms;
        counter.max_ms = (std::max)(counter.max_ms, elapsed_ms);
        // Quiet polls cannot erase a slow operation. Begin/commit delimit the
        // exact attempted revision, even when their own CPU cost is small.
        if (elapsed_ms < threshold_ms && !failed && stage != EditStage::renderer_begin &&
            stage != EditStage::collision_commit) return;
        events[event_count++ % capacity] = {identity, stage, elapsed_ms, finished, failed};
    }
    void write(std::ostream& out) const {
        // Uptime-sized steady timestamps need more than the stream's default
        // six significant digits to preserve the order of millisecond events.
        const auto precision = out.precision(std::numeric_limits<double>::max_digits10);
        out << "{\"schema\":1,\"clock\":\"steady\",\"thresholdMs\":" << threshold_ms
            << ",\"capacity\":" << capacity << ",\"eventCount\":" << event_count
            << ",\"overwritten\":" << (event_count > capacity ? event_count - capacity : 0) << ",\"stages\":{";
        for (std::size_t i = 0; i < counters.size(); ++i) {
            if (i) out << ',';
            const auto& c = counters[i];
            out << '"' << edit_stage_names[i] << "\":{\"count\":" << c.count << ",\"failed\":" << c.failed
                << ",\"lastMs\":" << c.last_ms << ",\"maxMs\":" << c.max_ms << '}';
        }
        out << "},\"events\":[";
        const auto count = (std::min<std::uint64_t>)(event_count, capacity);
        for (std::uint64_t i = 0; i < count; ++i) {
            if (i) out << ',';
            const auto& e = events[(event_count - count + i) % capacity];
            const auto& id = e.identity;
            out << "{\"stage\":\"" << edit_stage_names[static_cast<std::size_t>(e.stage)]
                << "\",\"attempt\":\"" << id.attempt << "\",\"epoch\":\"" << id.epoch
                << "\",\"revision\":\"" << id.revision << "\",\"ticket\":\"" << id.ticket
                << "\",\"cutsHash\":\"" << id.cuts_hash << "\",\"cuts\":" << id.cuts
                << ",\"elapsedMs\":" << e.elapsed_ms << ",\"completedSteadySeconds\":" << e.finished
                << ",\"failed\":" << (e.failed ? "true" : "false") << '}';
        }
        out << "]}";
        out.precision(precision);
    }
};
class EditWorkScope {
public:
    EditWorkScope(EditWorkHistory& history, EditStage stage, const EditWorkIdentity& identity) noexcept
        : history_(history), stage_(stage), identity_(identity), started_(timing::seconds()), exceptions_(std::uncaught_exceptions()) {}
    ~EditWorkScope() {
        const double now = timing::seconds();
        history_.record(stage_, identity_, (std::max)(0.0, (now - started_) * 1000), now,
                        failed_ || std::uncaught_exceptions() > exceptions_);
    }
    void fail() noexcept { failed_ = true; }
    EditWorkScope(const EditWorkScope&) = delete;
    EditWorkScope& operator=(const EditWorkScope&) = delete;
private:
    EditWorkHistory& history_;
    EditStage stage_;
    const EditWorkIdentity& identity_;
    double started_;
    int exceptions_;
    bool failed_ = false;
};
} // namespace goldcraft::client_map
