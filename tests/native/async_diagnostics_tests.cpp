#include "async_diagnostics.hpp"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace goldcraft::diagnostics;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
unsigned checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class Predicate> void until(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    check(predicate(), message);
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    check(static_cast<bool>(file), "fixture write");
}
struct Gates {
    std::mutex mutex;
    std::condition_variable cv;
    bool log_entered = false, status_entered = false, log_open = false, status_open = false;
    bool expired = false;
    void before(bool snapshot) {
        std::unique_lock lock(mutex);
        (snapshot ? status_entered : log_entered) = true;
        cv.notify_all();
        if (!cv.wait_for(lock, 8s, [&] { return snapshot ? status_open : log_open; })) expired = true;
    }
    void wait() {
        std::unique_lock lock(mutex);
        check(cv.wait_for(lock, 3s, [&] { return log_entered && status_entered; }), "both real workers entered gate");
    }
    void release(bool snapshot) {
        { std::lock_guard lock(mutex); (snapshot ? status_open : log_open) = true; }
        cv.notify_all();
    }
    void release_all() { release(false); release(true); }
};
struct ReleaseOnExit { Gates& gates; ~ReleaseOnExit() { gates.release_all(); } };

void bounded_blocking(const fs::path& root) {
    Gates gates;
    Dispatcher dispatcher;
    ReleaseOnExit release{gates};
    Config config{(root / "bounded.log").string(), (root / "bounded.json").string(), false};
    config.before_write = [&](bool snapshot) { gates.before(snapshot); };
    write(root / "bounded.log", "old log\n");
    check(dispatcher.start(config), "start blocked-sink dispatcher");
    check(!dispatcher.start(config), "repeated start rejected without restart/drain");
    check(!fs::exists(root / "bounded.json"), "start performs no status I/O");
    check(read(root / "bounded.log") == "old log\n", "start does not touch log");
    std::string copied = "{\"seq\":0}";
    dispatcher.snapshot(copied);
    copied.assign("mutated input");
    dispatcher.log("first\n");
    gates.wait();
    constexpr unsigned logs = 4096, snapshots = 2048;
    const auto begin = std::chrono::steady_clock::now();
    for (unsigned i = 1; i <= logs; ++i) dispatcher.log("log " + std::to_string(i) + "\n");
    for (unsigned i = 1; i <= snapshots; ++i) dispatcher.snapshot("{\"seq\":" + std::to_string(i) + "}");
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    auto s = dispatcher.stats();
    check(s.log_written == 0 && s.snapshot_written == 0, "producer finished while both sinks remained blocked");
    check(s.log_pending == log_capacity && s.snapshot_pending == snapshot_capacity, "bounded independent queues");
    check(s.log_overwritten == logs - log_capacity && s.snapshot_overwritten == snapshots - 1, "exact overwrite counts");
    check(s.active_workers == 2 && s.log_worker_id != s.snapshot_worker_id, "independent real worker identities");
    check(s.log_worker_id != GetCurrentThreadId() && s.snapshot_worker_id != GetCurrentThreadId(), "no file sink runs on owner thread");
    dispatcher.log(std::string(max_log_bytes + 1, 'x'));
    dispatcher.snapshot(std::string(max_snapshot_bytes + 1, 'x'));
    s = dispatcher.stats();
    check(s.oversized_logs == 1 && s.oversized_snapshots == 1, "byte limits observable");
    check(s.log_pending == log_capacity && s.snapshot_pending == 1, "oversized data cannot grow queues");
    gates.release(true);
    until([&] { return dispatcher.stats().snapshot_written == 2; }, "status drains while log sink still blocked");
    check(read(root / "bounded.json") == "{\"seq\":2048}", "latest snapshot retained under saturation");
    check(dispatcher.stats().log_written == 0, "status does not wait for log disk");
    HANDLE log_thread = OpenThread(SYNCHRONIZE, FALSE, s.log_worker_id);
    HANDLE status_thread = OpenThread(SYNCHRONIZE, FALSE, s.snapshot_worker_id);
    check(log_thread && status_thread, "open actual worker lifetime handles");
    gates.release(false);
    dispatcher.stop();
    s = dispatcher.stats();
    check(s.active_workers == 0 && !s.running, "stop joins both workers");
    check(WaitForSingleObject(log_thread, 0) == WAIT_OBJECT_0 && WaitForSingleObject(status_thread, 0) == WAIT_OBJECT_0,
          "Windows confirms both worker threads terminated");
    CloseHandle(log_thread); CloseHandle(status_thread);
    check(s.log_written == log_capacity + 1 && s.log_errors == 0 && s.snapshot_errors == 0, "surviving log queue drained without errors");
    check(s.log_overwritten == logs - log_capacity && s.snapshot_overwritten == snapshots - 1, "stop preserves overwrite counters");
    const auto lines = read(root / "bounded.log");
    check(lines.starts_with("old log\nfirst\nlog 3841\n") && lines.ends_with("log 4096\n"), "append semantics and final log retained");
    dispatcher.stop(); dispatcher.log("ignored"); dispatcher.snapshot("ignored");
    check(dispatcher.stats().rejected == 2 && dispatcher.stats().active_workers == 0, "idempotent stop and post-stop rejection");
    check(!gates.expired, "blocking test gate never timed out");
    std::cout << "blocked_sink_producer_ms=" << ms << " log_overwritten=" << s.log_overwritten
              << " status_overwritten=" << s.snapshot_overwritten << '\n';
}

void copying_and_shutdown(const fs::path& root) {
    Gates gates;
    Dispatcher dispatcher;
    ReleaseOnExit release{gates};
    Config config{(root / "copy.log").string(), (root / "copy.json").string(), false};
    config.before_write = [&](bool snapshot) { gates.before(snapshot); };
    check(dispatcher.start(config), "copy test start");
    std::string log = "owned log\n", status = "{\"owned\":true}";
    dispatcher.log(log); dispatcher.snapshot(status);
    log.assign("bad"); status.assign("bad");
    gates.wait();
    std::atomic<bool> stop_entered{}, stop_returned{};
    // Ownership is transferred for this phase: the original owner makes no
    // Dispatcher calls until the shutdown thread has joined.
    std::thread stopper([&] { stop_entered = true; dispatcher.stop(); stop_returned = true; });
    until([&] { return stop_entered.load(); }, "stop thread started");
    std::this_thread::sleep_for(30ms);
    const bool stopped_early = stop_returned.load();
    gates.release_all();
    stopper.join();
    check(!stopped_early, "shutdown waits for in-flight sink instead of detaching");
    check(stop_returned && dispatcher.stats().active_workers == 0, "shutdown completed after disk released");
    check(read(root / "copy.json") == "{\"owned\":true}", "snapshot bytes copied before caller mutation");
    check(read(root / "copy.log") == "owned log\n", "log bytes copied before caller mutation");
    check(!gates.expired, "copy gate did not time out");
}

void errors_and_restart(const fs::path& root) {
    Dispatcher dispatcher;
    Config config{(root / "missing" / "error.log").string(), (root / "missing" / "error.json").string(), false};
    check(dispatcher.start(config), "invalid output path does not perform blocking init I/O");
    dispatcher.log("retry\n"); dispatcher.snapshot("{\"retry\":0}");
    until([&] { const auto s = dispatcher.stats(); return s.log_errors == 1 && s.snapshot_errors == 1; }, "worker file errors recorded");
    check(dispatcher.stats().log_written == 0 && dispatcher.stats().snapshot_written == 0, "failed writes are not successful writes");
    fs::create_directory(root / "missing");
    dispatcher.log("recovered\n"); dispatcher.snapshot("{\"retry\":1}");
    dispatcher.stop();
    check(read(root / "missing" / "error.log") == "recovered\n", "log can retry failed lazy open");
    check(read(root / "missing" / "error.json") == "{\"retry\":1}", "status can retry failed lazy open");
    check(dispatcher.stats().log_errors == 1 && dispatcher.stats().snapshot_errors == 1, "errors survive drain");
    config.log_path = config.status_path;
    check(!dispatcher.start(config) && dispatcher.stats().start_errors == 1, "same-path configuration rejected");
    config.log_path = (root / "restart.log").string(); config.status_path.clear();
    check(dispatcher.start(config) && !dispatcher.status_enabled(), "restart without status starts log worker only");
    dispatcher.log("restart\n"); dispatcher.stop();
    check(dispatcher.stats().active_workers == 0 && read(root / "restart.log") == "restart\n", "restart has no surviving worker");
    for (const auto& entry : fs::recursive_directory_iterator(root))
        check(entry.path().extension() != ".tmp", "no orphan status temporary files");
}

void atomic_readers(const fs::path& root) {
    std::mutex error_mutex;
    std::vector<std::string> errors;
    Dispatcher dispatcher;
    Config config{(root / "atomic.log").string(), (root / "atomic.json").string(), false};
    config.on_error = [&](bool snapshot, const std::string& error) {
        std::lock_guard lock(error_mutex);
        errors.push_back(std::string(snapshot ? "snapshot: " : "log: ") + error);
    };
    check(dispatcher.start(config), "atomic-reader start");
    const std::string prefix = "{\"pad\":\"" + std::string(60000, 'x') + "\",\"seq\":";
    dispatcher.snapshot(prefix + "0}");
    until([&] { return dispatcher.stats().snapshot_written == 1; }, "initial atomic file written");
    std::atomic<bool> done{};
    std::atomic<unsigned> reads{}, bad{}, open_errors{};
    std::atomic<DWORD> last_open_error{};
    std::thread reader([&] {
        while (!done) {
            HANDLE file = CreateFileW((root / "atomic.json").c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file == INVALID_HANDLE_VALUE) { last_open_error = GetLastError(); ++open_errors; continue; }
            char buffer[61000]; DWORD count = 0;
            const auto ok = ReadFile(file, buffer, sizeof(buffer), &count, nullptr);
            CloseHandle(file);
            const std::string_view value(buffer, count);
            if (!ok || !value.starts_with(prefix) || !value.ends_with('}') || count <= prefix.size() + 1) ++bad;
            ++reads;
        }
    });
    bool drained = true;
    unsigned failed_sequence = 0;
    for (unsigned i = 1; i <= 64; ++i) {
        dispatcher.snapshot(prefix + std::to_string(i) + "}");
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (dispatcher.stats().snapshot_written != i + 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        if (dispatcher.stats().snapshot_written != i + 1) { drained = false; failed_sequence = i; break; }
    }
    done = true; reader.join(); dispatcher.stop();
    const auto final = dispatcher.stats();
    std::cout << "atomic_failed_sequence=" << failed_sequence << " submitted=" << final.snapshot_submitted
              << " written=" << final.snapshot_written << " errors=" << final.snapshot_errors
              << " overwritten=" << final.snapshot_overwritten << " reads=" << reads
              << " bad=" << bad << " open_errors=" << open_errors
              << " last_open_error=" << last_open_error << '\n';
    for (const auto& error : errors) std::cout << "atomic_sink_error=" << error << '\n';
    check(drained, "all replacements succeeded alongside active reader");
    check(reads > 0 && bad == 0 && open_errors == 0, "readers observe complete JSON with no missing target");
    check(read(root / "atomic.json") == prefix + "64}", "last atomic replacement exact bytes");
    std::cout << "atomic_file_reader_samples=" << reads << '\n';
}

void held_readers_and_recovery(const fs::path& root) {
    const auto path = root / "held.json";
    // Only the single snapshot worker appends; read after stop joins it.
    std::vector<std::string> sink_errors;
    Dispatcher dispatcher;
    Config config{"", path.string(), false};
    config.on_error = [&](bool, const std::string& error) { sink_errors.push_back(error); };
    check(dispatcher.start(config), "held-reader start");
    dispatcher.snapshot("{\"seq\":0}");
    until([&] { return dispatcher.stats().snapshot_written == 1; }, "first creation succeeds");
    auto open_reader = [&](DWORD share) {
        return std::unique_ptr<void, decltype(&CloseHandle)>(
            CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, 0, nullptr), CloseHandle);
    };
    auto held_bytes = [](HANDLE handle) {
        char bytes[32]{}; DWORD count{};
        LARGE_INTEGER begin{};
        check(SetFilePointerEx(handle, begin, nullptr, FILE_BEGIN) != FALSE, "rewind held original handle");
        check(ReadFile(handle, bytes, sizeof(bytes), &count, nullptr) != FALSE, "read held original handle");
        return std::string(bytes, count);
    };
    auto no_temporaries = [&] {
        unsigned temporary_files = 0;
        for (const auto& entry : fs::directory_iterator(root))
            if (entry.path().extension() == ".tmp") ++temporary_files;
        check(temporary_files == 0, "no temporary files after completed snapshot attempt");
    };
    auto held = open_reader(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    check(held.get() != INVALID_HANDLE_VALUE, "open reader with delete sharing");
    dispatcher.snapshot("{\"seq\":1}");
    until([&] { return dispatcher.stats().snapshot_written == 2; }, "replace while original reader remains open");
    check(held_bytes(held.get()) == "{\"seq\":0}", "held reader retains original complete bytes");
    check(read(path) == "{\"seq\":1}", "new opens observe complete replacement");
    held.reset();
    held = open_reader(FILE_SHARE_READ | FILE_SHARE_WRITE);
    check(held.get() != INVALID_HANDLE_VALUE, "open reader denying delete sharing");
    try {
        dispatcher.snapshot("{\"seq\":2}");
        until([&] { return dispatcher.stats().snapshot_errors == 1; },
              "denied delete sharing is an observable worker error on this platform");
        const auto denied = dispatcher.stats();
        std::cout << "held_share3 submitted=" << denied.snapshot_submitted
                  << " written=" << denied.snapshot_written << " errors=" << denied.snapshot_errors
                  << " overwritten=" << denied.snapshot_overwritten << '\n';
        check(denied.snapshot_submitted == 3 && denied.snapshot_written == 2 &&
              denied.snapshot_errors == 1 && denied.snapshot_overwritten == 0,
              "share3 rejection preserves exact accounting");
        check(read(path) == "{\"seq\":1}" && held_bytes(held.get()) == "{\"seq\":1}",
              "share3 rejection preserves both complete target and held reader");
        no_temporaries();
        held.reset();
        dispatcher.snapshot("{\"seq\":3}");
        until([&] { return dispatcher.stats().snapshot_written == 3; },
              "next snapshot recovers after incompatible reader closes");
        check(read(path) == "{\"seq\":3}", "reader-close recovery publishes exact next bytes");
        std::cout << "held_reader_close_recovered_bytes=" << read(path) << '\n';
        check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "set target readonly for error fixture");
        dispatcher.snapshot("{\"seq\":4}");
        until([&] { return dispatcher.stats().snapshot_errors == 2; }, "readonly rejection is an observable worker error");
        check(dispatcher.stats().snapshot_written == 3 && read(path) == "{\"seq\":3}",
              "readonly rejection retains previous target and success count");
        std::cout << "held_readonly_rejected_bytes=" << read(path) << '\n';
        no_temporaries();
        check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE, "restore target attributes");
        dispatcher.snapshot("{\"seq\":5}");
        dispatcher.stop();
        const auto final = dispatcher.stats();
        std::cout << "held_recovery submitted=" << final.snapshot_submitted
                  << " written=" << final.snapshot_written << " errors=" << final.snapshot_errors
                  << " overwritten=" << final.snapshot_overwritten
                  << " workers_after_stop=" << final.active_workers
                  << " target_bytes=" << read(path) << '\n';
        for (const auto& error : sink_errors) std::cout << "held_sink_error=" << error << '\n';
        check(read(path) == "{\"seq\":5}" && final.snapshot_written == 4,
              "next submitted snapshot recovers after readonly cleared");
        check(final.snapshot_submitted == 6 && final.snapshot_errors == 2 &&
              final.snapshot_overwritten == 0 && final.active_workers == 0,
              "two observable errors without retry loss or worker leak");
        check(sink_errors.size() == 2, "both rejected snapshots report their errors");
        check(sink_errors[0].find("SetFileInformationByHandle(FileRenameInfoEx) error=32:") != std::string::npos,
              "sink reports immediate numeric sharing error");
        check(sink_errors[1].find("SetFileInformationByHandle(FileRenameInfoEx) error=5:") != std::string::npos,
              "sink reports immediate numeric readonly error");
        no_temporaries();
    } catch (...) {
        dispatcher.stop();
        const auto final = dispatcher.stats();
        std::cout << "held_recovery_failure submitted=" << final.snapshot_submitted
                  << " written=" << final.snapshot_written << " errors=" << final.snapshot_errors
                  << " overwritten=" << final.snapshot_overwritten
                  << " workers_after_stop=" << final.active_workers
                  << " target_attributes=" << GetFileAttributesW(path.c_str())
                  << " target_bytes=" << read(path) << '\n';
        for (const auto& error : sink_errors) std::cout << "held_sink_error=" << error << '\n';
        throw;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "fixture path argument");
        const fs::path root = fs::absolute(argv[1]);
        check(!fs::exists(root), "fresh isolated output directory required");
        fs::create_directories(root);
        static_assert(sizeof(wchar_t) == 2 && sizeof(DWORD) == 4);
        static_assert(offsetof(FILE_RENAME_INFO, FileName) == (sizeof(void*) == 8 ? 20 : 12));
        static_assert(FILE_RENAME_FLAG_REPLACE_IF_EXISTS == 1 && FILE_RENAME_FLAG_POSIX_SEMANTICS == 2);
        std::cout << "native_rename_layout pointer=" << sizeof(void*) << " struct=" << sizeof(FILE_RENAME_INFO)
                  << " offset=" << offsetof(FILE_RENAME_INFO, FileName) << " wchar=" << sizeof(wchar_t) << '\n';
        bounded_blocking(root);
        copying_and_shutdown(root);
        errors_and_restart(root);
        atomic_readers(root);
        held_readers_and_recovery(root);
        std::cout << "PASS async diagnostics " << checks << " checks; offline I/O/lifetime only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL check " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
