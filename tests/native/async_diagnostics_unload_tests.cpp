#include "async_diagnostics.hpp"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using goldcraft::diagnostics::Stats;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Function> Function symbol(HMODULE dll, const char* name) {
    auto function = reinterpret_cast<Function>(GetProcAddress(dll, name));
    check(function != nullptr, "fixture export");
    return function;
}
int main(int argc, char** argv) {
    try {
        check(argc == 3, "dll and fresh output arguments");
        const auto dll_path = fs::absolute(argv[1]);
        const auto root = fs::absolute(argv[2]) / ("run-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "fresh unload output");
        fs::create_directories(root);
        for (unsigned iteration = 0; iteration < 8; ++iteration) {
            HMODULE dll = LoadLibraryW(dll_path.c_str());
            check(dll != nullptr, "load production-mode diagnostics DLL");
            const auto start = symbol<bool(*)(const char*, const char*)>(dll, "AsyncStart");
            const auto submit = symbol<void(*)()>(dll, "AsyncSubmit");
            const auto stats = symbol<void(*)(Stats*)>(dll, "AsyncStats");
            const auto stop = symbol<void(*)()>(dll, "AsyncStop");
            const auto log = (root / (std::to_string(iteration) + ".log")).string();
            const auto status = (root / (std::to_string(iteration) + ".json")).string();
            check(start(log.c_str(), status.c_str()), "start production workers");
            submit();
            Stats s;
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            do { stats(&s); std::this_thread::sleep_for(1ms); }
            while ((s.active_workers != 2 || !s.snapshot_written || !s.log_written) && std::chrono::steady_clock::now() < deadline);
            check(s.active_workers == 2 && s.snapshot_written == 1 && s.log_written == 1, "actual production file sinks executed");
            HANDLE log_thread = OpenThread(SYNCHRONIZE, FALSE, s.log_worker_id);
            HANDLE status_thread = OpenThread(SYNCHRONIZE, FALSE, s.snapshot_worker_id);
            check(log_thread && status_thread, "hold actual thread handles across unload");
            stop(); stats(&s);
            check(s.active_workers == 0 && !s.running && s.log_errors == 0 && s.snapshot_errors == 0, "Shutdown drains before unload");
            check(WaitForSingleObject(log_thread, 0) == WAIT_OBJECT_0 && WaitForSingleObject(status_thread, 0) == WAIT_OBJECT_0,
                  "workers terminated before FreeLibrary");
            check(FreeLibrary(dll) != FALSE, "FreeLibrary succeeds after explicit stop");
            check(GetModuleHandleW(dll_path.filename().c_str()) == nullptr, "fixture DLL actually unloaded");
            check(WaitForSingleObject(log_thread, 0) == WAIT_OBJECT_0 && WaitForSingleObject(status_thread, 0) == WAIT_OBJECT_0,
                  "no surviving workers after FreeLibrary");
            CloseHandle(log_thread); CloseHandle(status_thread);
        }
        std::cout << "PASS 8 actual DLL load/start/write/stop/thread-exit/unload cycles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
