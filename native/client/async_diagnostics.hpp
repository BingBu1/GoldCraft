#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

#ifdef GOLDCRAFT_ASYNC_DIAGNOSTICS_TESTING
#include <functional>
#endif

namespace goldcraft::diagnostics {

inline constexpr std::size_t log_capacity = 256;
inline constexpr std::size_t snapshot_capacity = 1;
inline constexpr std::size_t max_log_bytes = 16 * 1024;
inline constexpr std::size_t max_snapshot_bytes = 4 * 1024 * 1024;

struct Config {
    std::string log_path;
    std::string status_path;
    bool debugger_output = true;
#ifdef GOLDCRAFT_ASYNC_DIAGNOSTICS_TESTING
    // Runs on the real spdlog worker, immediately before its real file I/O.
    std::function<void(bool snapshot)> before_write{};
    // Observes actual sink errors after spdlog catches them, on the worker.
    std::function<void(bool snapshot, const std::string&)> on_error{};
#endif
};

struct Stats {
    std::uint64_t log_submitted = 0, log_written = 0, log_overwritten = 0;
    std::uint64_t snapshot_submitted = 0, snapshot_written = 0, snapshot_overwritten = 0;
    std::uint64_t log_errors = 0, snapshot_errors = 0;
    std::uint64_t oversized_logs = 0, oversized_snapshots = 0;
    std::uint64_t rejected = 0, start_errors = 0;
    std::size_t log_pending = 0, snapshot_pending = 0;
    std::uint32_t active_workers = 0, log_worker_id = 0, snapshot_worker_id = 0;
    bool running = false, status_enabled = false;
};

// The game thread owns start/submit/stats/stop. Workers receive copied bytes,
// paths and atomic counters only; they never access engine, VGUI or GL objects.
// Queue submission uses spdlog's short queue mutex, never its capacity wait or
// a sink mutex. Allocation/copy and main-thread serialization still cost time.
class Dispatcher {
public:
    Dispatcher() noexcept;
    ~Dispatcher();
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    // Starts workers without opening any files. Repeated start is rejected.
    bool start(Config config) noexcept;
    void log(std::string_view line) noexcept;
    void snapshot(std::string_view json) noexcept;
    bool status_enabled() const noexcept;
    Stats stats() const noexcept;
    void write_status(std::ostream& out) const;

    // Drain and join outside DllMain, before plugin unload. This final teardown
    // intentionally waits for outstanding I/O; no unsafe detached worker.
    void stop() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    Stats stopped_{};
};

} // namespace goldcraft::diagnostics
