#include "async_diagnostics.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace goldcraft::diagnostics {
namespace {
using Pool = spdlog::details::thread_pool;

struct Counters {
    std::atomic<std::uint64_t> log_written{}, snapshot_written{};
    std::atomic<std::uint64_t> log_errors{}, snapshot_errors{};
    std::atomic<std::uint32_t> workers{}, log_thread{}, snapshot_thread{};
};

// One sink per single-worker pool. The null sink mutex cannot hold up a
// producer: no producer calls log/flush/set_pattern on this sink directly.
class FileSink final : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
public:
    FileSink(const Config& config, bool snapshot, std::shared_ptr<Counters> counters)
        : path_(snapshot ? config.status_path : config.log_path),
          snapshot_(snapshot), debugger_(config.debugger_output), counters_(std::move(counters))
#ifdef GOLDCRAFT_ASYNC_DIAGNOSTICS_TESTING
        , before_write_(config.before_write)
#endif
    {}

protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
#ifdef GOLDCRAFT_ASYNC_DIAGNOSTICS_TESTING
        if (before_write_) before_write_(snapshot_);
#endif
        if (snapshot_) {
            replace_snapshot(message.payload);
            counters_->snapshot_written.fetch_add(1, std::memory_order_relaxed);
        } else {
            if (debugger_) {
                const std::string line(message.payload.data(), message.payload.size());
                OutputDebugStringA(line.c_str());
            }
            if (!path_.empty()) {
                if (!log_.is_open()) {
                    log_.clear();
                    log_.exceptions(std::ios::failbit | std::ios::badbit);
                    log_.open(path_, std::ios::binary | std::ios::app);
                }
                log_.write(message.payload.data(), static_cast<std::streamsize>(message.payload.size()));
                log_.flush();
            }
            counters_->log_written.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void flush_() override {
        if (log_.is_open()) log_.flush();
    }

private:
    void replace_snapshot(spdlog::string_view_t bytes) {
        // Same-directory replacement keeps readers from seeing a truncated
        // JSON. No parent creation/retry/fsync on a producer or draw callback.
        if (temporary_.empty()) {
            static std::atomic<std::uint64_t> next{};
            temporary_ = path_;
            temporary_ += L".goldcraft-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(next.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
        }
        try {
            std::ofstream file;
            file.exceptions(std::ios::failbit | std::ios::badbit);
            file.open(temporary_, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            file.close();
            // ReplaceFile opens the replacement without sharing. That handle
            // can reject readers after the new name becomes visible. Rename
            // our closed, complete file through a fully shared DELETE handle.
            // POSIX semantics retain old handles and direct new opens to the
            // new file. Requires Windows10 extended rename and filesystem
            // support; rejection is observable, with no weaker fallback/retry.
            const auto name = std::filesystem::absolute(path_).native();
            constexpr auto offset = offsetof(FILE_RENAME_INFO, FileName);
            constexpr auto max_name_units =
                ((std::numeric_limits<DWORD>::max)() - offset - sizeof(wchar_t)) / sizeof(wchar_t);
            if (name.size() > max_name_units || name.find(L'\0') != std::wstring::npos)
                throw std::invalid_argument("GoldCraft status invalid rename path");
            const auto name_bytes = name.size() * sizeof(wchar_t);
            const auto buffer_bytes = (std::max)(sizeof(FILE_RENAME_INFO), offset + name_bytes + sizeof(wchar_t));
            // calloc provides alignment and implicit object creation in C++20.
            std::unique_ptr<void, decltype(&std::free)> buffer(std::calloc(1, buffer_bytes), std::free);
            if (!buffer) throw std::bad_alloc();
            auto* info = static_cast<FILE_RENAME_INFO*>(buffer.get());
            info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
            info->FileNameLength = static_cast<DWORD>(name_bytes); // UTF-16 bytes, excludes NUL
            std::memcpy(info->FileName, name.c_str(), name_bytes + sizeof(wchar_t));
            auto fail = [](const char* operation, DWORD error) {
                throw std::system_error(static_cast<int>(error), std::system_category(),
                    std::string("GoldCraft status ") + operation + " error=" + std::to_string(error));
            };
            const auto raw = CreateFileW(temporary_.c_str(), DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (raw == INVALID_HANDLE_VALUE) fail("CreateFileW(rename source)", GetLastError());
            const std::unique_ptr<void, decltype(&CloseHandle)> source(raw, CloseHandle);
            if (!SetFileInformationByHandle(source.get(), FileRenameInfoEx, info, static_cast<DWORD>(buffer_bytes)))
                fail("SetFileInformationByHandle(FileRenameInfoEx)", GetLastError());
        } catch (...) {
            DeleteFileW(temporary_.c_str());
            throw;
        }
    }

    std::filesystem::path path_, temporary_;
    bool snapshot_, debugger_;
    std::shared_ptr<Counters> counters_;
    std::ofstream log_;
#ifdef GOLDCRAFT_ASYNC_DIAGNOSTICS_TESTING
    std::function<void(bool)> before_write_;
#endif
};

std::shared_ptr<Pool> make_pool(std::size_t capacity, bool snapshot,
                               const std::shared_ptr<Counters>& counters) {
    return std::make_shared<Pool>(capacity, 1, [counters, snapshot] {
        (snapshot ? counters->snapshot_thread : counters->log_thread)
            .store(GetCurrentThreadId(), std::memory_order_relaxed);
        counters->workers.fetch_add(1, std::memory_order_relaxed);
    }, [counters] { counters->workers.fetch_sub(1, std::memory_order_relaxed); });
}

std::shared_ptr<spdlog::async_logger> make_logger(const Config& config, bool snapshot,
                                                const std::shared_ptr<Pool>& pool,
                                                const std::shared_ptr<Counters>& counters) {
    auto sink = std::make_shared<FileSink>(config, snapshot, counters);
    auto logger = std::make_shared<spdlog::async_logger>(snapshot ? "gc-status" : "gc-log",
        std::move(sink), pool, spdlog::async_overflow_policy::overrun_oldest);
    logger->set_error_handler([counters, snapshot
#ifdef GOLDCRAFT_ASYNC_DIAGNOSTICS_TESTING
        , on_error = config.on_error
#endif
    ](const std::string& error) {
        (snapshot ? counters->snapshot_errors : counters->log_errors)
            .fetch_add(1, std::memory_order_relaxed);
#ifdef GOLDCRAFT_ASYNC_DIAGNOSTICS_TESTING
        if (on_error) on_error(snapshot, error);
#else
        (void)error;
#endif
    });
    return logger;
}
} // namespace

struct Dispatcher::State {
    std::shared_ptr<Counters> counters = std::make_shared<Counters>();
    std::shared_ptr<Pool> log_pool, snapshot_pool;
    std::shared_ptr<spdlog::async_logger> logger, snapshots;
    Stats submitted{};

    explicit State(const Config& config) {
        log_pool = make_pool(log_capacity, false, counters);
        logger = make_logger(config, false, log_pool, counters);
        if (!config.status_path.empty()) {
            snapshot_pool = make_pool(snapshot_capacity, true, counters);
            snapshots = make_logger(config, true, snapshot_pool, counters);
        }
    }
};

Dispatcher::Dispatcher() noexcept = default;
Dispatcher::~Dispatcher() { stop(); }

bool Dispatcher::start(Config config) noexcept {
    if (state_) { ++state_->submitted.start_errors; return false; }
    try {
        if (!config.log_path.empty() && config.log_path == config.status_path)
            throw std::invalid_argument("Log and status require different paths");
        state_ = std::make_unique<State>(config);
        stopped_ = {};
        return true;
    } catch (...) { ++stopped_.start_errors; return false; }
}

void Dispatcher::log(std::string_view line) noexcept {
    if (!state_) { ++stopped_.rejected; return; }
    if (line.size() > max_log_bytes) { ++state_->submitted.oversized_logs; return; }
    ++state_->submitted.log_submitted;
    try { state_->logger->log(spdlog::level::info, spdlog::string_view_t(line.data(), line.size())); }
    catch (...) { state_->counters->log_errors.fetch_add(1, std::memory_order_relaxed); }
}

void Dispatcher::snapshot(std::string_view json) noexcept {
    if (!state_ || !state_->snapshots) {
        if (state_) ++state_->submitted.rejected;
        else ++stopped_.rejected;
        return;
    }
    if (json.size() > max_snapshot_bytes) { ++state_->submitted.oversized_snapshots; return; }
    ++state_->submitted.snapshot_submitted;
    try { state_->snapshots->log(spdlog::level::info, spdlog::string_view_t(json.data(), json.size())); }
    catch (...) { state_->counters->snapshot_errors.fetch_add(1, std::memory_order_relaxed); }
}

bool Dispatcher::status_enabled() const noexcept { return state_ && state_->snapshots; }

Stats Dispatcher::stats() const noexcept {
    if (!state_) return stopped_;
    auto out = state_->submitted;
    const auto& counters = *state_->counters;
    out.log_written = counters.log_written.load(std::memory_order_relaxed);
    out.snapshot_written = counters.snapshot_written.load(std::memory_order_relaxed);
    out.log_errors = counters.log_errors.load(std::memory_order_relaxed);
    out.snapshot_errors = counters.snapshot_errors.load(std::memory_order_relaxed);
    out.active_workers = counters.workers.load(std::memory_order_relaxed);
    out.log_worker_id = counters.log_thread.load(std::memory_order_relaxed);
    out.snapshot_worker_id = counters.snapshot_thread.load(std::memory_order_relaxed);
    out.running = true;
    out.status_enabled = static_cast<bool>(state_->snapshots);
    if (state_->log_pool) {
        out.log_overwritten = state_->log_pool->overrun_counter();
        out.log_pending = state_->log_pool->queue_size();
    }
    if (state_->snapshot_pool) {
        out.snapshot_overwritten = state_->snapshot_pool->overrun_counter();
        out.snapshot_pending = state_->snapshot_pool->queue_size();
    }
    return out;
}

void Dispatcher::write_status(std::ostream& out) const {
    const auto s = stats();
    out << "\"asyncDiagnostics\":{\"running\":" << (s.running ? "true" : "false")
        << ",\"logSubmitted\":" << s.log_submitted << ",\"logWritten\":" << s.log_written
        << ",\"logOverwritten\":" << s.log_overwritten << ",\"logErrors\":" << s.log_errors
        << ",\"snapshotSubmitted\":" << s.snapshot_submitted << ",\"snapshotWritten\":" << s.snapshot_written
        << ",\"snapshotOverwritten\":" << s.snapshot_overwritten << ",\"snapshotErrors\":" << s.snapshot_errors
        << ",\"oversizedLogs\":" << s.oversized_logs << ",\"oversizedSnapshots\":" << s.oversized_snapshots
        << ",\"rejected\":" << s.rejected << ",\"startErrors\":" << s.start_errors
        << ",\"logPending\":" << s.log_pending << ",\"snapshotPending\":" << s.snapshot_pending
        << ",\"activeWorkers\":" << s.active_workers << ",\"logWorkerId\":" << s.log_worker_id
        << ",\"snapshotWorkerId\":" << s.snapshot_worker_id << '}';
}

void Dispatcher::stop() noexcept {
    if (!state_) return;
    const auto before = stats();
    // No producer remains. Pool destructors enqueue a non-droppable terminate
    // after pending messages and join; logger->flush() here would itself be a
    // droppable queue entry. Every file sink operation already closes/flushes.
    state_->snapshot_pool.reset();
    state_->log_pool.reset();
    stopped_ = stats();
    stopped_.log_overwritten = before.log_overwritten;
    stopped_.snapshot_overwritten = before.snapshot_overwritten;
    stopped_.running = stopped_.status_enabled = false;
    state_.reset();
}

} // namespace goldcraft::diagnostics
