// SPDX-License-Identifier: GPL-3.0-or-later
// Actual local-import worker. Controlled blocking checks caller liveness;
// a real hidden Win32 window checks cancellation delivery. Live engine ACKs
// and VGUI rendering still require a separate in-game run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../../native/csol/client/hd_spray_import_job.hpp"
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace importing = csol::client::spray_import;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
unsigned checks{};
void Require(bool value, int line) {
    ++checks;
    if (!value) throw std::runtime_error("import worker check failed at line " + std::to_string(line));
}
#define REQUIRE(value) Require(bool(value), __LINE__)
struct Event {
    HANDLE value{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Event() { REQUIRE(value); }
    ~Event() { CloseHandle(value); }
    void Set() const { SetEvent(value); }
    bool Wait(DWORD timeout = 3000) const { return WaitForSingleObject(value, timeout) == WAIT_OBJECT_0; }
};
void WaitDone(const std::shared_ptr<importing::Job>& job) {
    const auto end = Clock::now() + 3s;
    while (!job->done.load(std::memory_order_acquire) && Clock::now() < end) Sleep(1);
    REQUIRE(job->done.load(std::memory_order_acquire));
}
importing::Result Value(unsigned marker) {
    importing::Result result;
    result.selected = true;
    result.settings.enabled = true;
    result.settings.original = {marker, marker + 1};
    result.settings.bytes.assign(4096, static_cast<unsigned char>(marker));
    return result;
}
struct WindowWork {
    Event entered, ready, create;
    std::atomic<unsigned> cancellationMessages{}, destroyed{}, threadId{};
    std::atomic<HWND> window{};
    bool delayCreate{};
};
LRESULT CALLBACK TestWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* state = static_cast<WindowWork*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    auto* state = reinterpret_cast<WindowWork*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) {
        if (state) ++state->cancellationMessages;
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (state) ++state->destroyed;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
constexpr auto WindowClass = L"GoldCraftImportWorkerCancellationTest";
importing::Result WindowOperation(importing::Job& job, WindowWork& state) {
    state.threadId = GetCurrentThreadId();
    state.entered.Set();
    if (state.delayCreate && !state.create.Wait()) throw std::runtime_error("window creation gate timed out");
    // An ordinary invisible window, with its own actual Windows message queue.
    // No game window owner, synthetic VGUI panel, or user keyboard input.
    const HWND window = CreateWindowExW(0, WindowClass, L"Import cancellation test", WS_OVERLAPPED,
        0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), &state);
    if (!window) throw std::runtime_error("CreateWindowExW failed");
    state.window = window;
    job.Dialog(window);
    state.ready.Set();
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    job.Dialog(nullptr);
    return {};
}

int main() {
    try {
        const auto mainThread = GetCurrentThreadId();
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = WindowClass;
        wc.lpfnWndProc = TestWindow;
        REQUIRE(RegisterClassW(&wc));
        {
            importing::Worker worker;
            Event entered, release;
            std::atomic<unsigned> workThread{};
            const auto started = Clock::now();
            auto job = worker.Start([&](importing::Job&) {
                workThread = GetCurrentThreadId(); entered.Set();
                if (!release.Wait()) throw std::runtime_error("decode gate timed out");
                return Value(7);
            });
            REQUIRE(job && Clock::now() - started < 1s && entered.Wait());
            REQUIRE(workThread != mainThread && !job->done && !worker.Available());
            REQUIRE(!worker.Start([](importing::Job&) { return Value(99); }));
            unsigned callerPumps{};
            for (unsigned i = 0; i < 1000; ++i) { worker.Reap(); ++callerPumps; }
            REQUIRE(callerPumps == 1000 && !job->done);
            const auto cancelled = Clock::now(); job->Cancel();
            REQUIRE(Clock::now() - cancelled < 1s && job->cancelled && !job->done);
            // Closing the page drops its shared lease. Worker ownership lasts
            // through completion, so no freed panel/result storage is needed.
            std::weak_ptr<importing::Job> old = job;
            job.reset(); REQUIRE(!old.expired());
            release.Set(); auto held = old.lock(); REQUIRE(held); WaitDone(held);
            REQUIRE(held->cancelled && held->result.selected && held->result.settings.bytes[2000] == 7);
            worker.Reap(); worker.Reap(); held.reset(); REQUIRE(old.expired() && worker.Available());
            auto next = worker.Start([](importing::Job&) { return Value(11); }); WaitDone(next);
            REQUIRE(!next->cancelled && next->result.settings.original.width == 11);
            REQUIRE(next->result.settings.bytes.size() == 4096 && next->result.settings.bytes.back() == 11);
            worker.Reap(); REQUIRE(worker.Available());
            auto failure = worker.Start([](importing::Job&) -> importing::Result { throw std::runtime_error("decode failed"); });
            WaitDone(failure); REQUIRE(failure->result.error == "decode failed" && !failure->result.selected);
            worker.Reap(); worker.Shutdown(); worker.Shutdown();
            auto reused = worker.Start([](importing::Job&) { return Value(13); }); WaitDone(reused);
            REQUIRE(reused->result.settings.original.width == 13);
        }
        for (unsigned mode = 0; mode < 3; ++mode) {
            WindowWork state; state.delayCreate = mode == 1;
            importing::Worker worker;
            auto job = worker.Start([&](importing::Job& job) { return WindowOperation(job, state); });
            REQUIRE(state.entered.Wait());
            if (state.delayCreate) { job->Cancel(); state.create.Set(); }
            REQUIRE(state.ready.Wait());
            REQUIRE(state.threadId != mainThread && state.window && !IsWindowVisible(state.window));
            const auto started = Clock::now();
            if (mode == 2) worker.Shutdown(); else job->Cancel();
            WaitDone(job); worker.Reap();
            REQUIRE(Clock::now() - started < 2s);
            REQUIRE(job->cancelled && state.cancellationMessages >= 1 && state.destroyed == 1);
            REQUIRE(!IsWindow(state.window) && job->result.error.empty());
            const auto delivered = state.cancellationMessages.load();
            job->Cancel(); worker.Shutdown(); REQUIRE(state.cancellationMessages == delivered);
        }
        {
            importing::Worker worker;
            Event entered, release;
            auto job = worker.Start([&](importing::Job&) {
                entered.Set(); if (!release.Wait()) throw std::runtime_error("shutdown gate timed out");
                return Value(17);
            });
            REQUIRE(entered.Wait());
            std::thread finisher([&] { Sleep(75); release.Set(); });
            worker.Shutdown(); finisher.join();
            REQUIRE(job->done && job->cancelled && worker.Available());
            REQUIRE(job->result.settings.bytes.back() == 17);
        }
        REQUIRE(UnregisterClassW(WindowClass, GetModuleHandleW(nullptr)));
        std::printf("import worker: %u checks passed; real Win32 cancel windows, nonblocking caller and joined shutdown verified; live engine/VGUI pending\n", checks);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
