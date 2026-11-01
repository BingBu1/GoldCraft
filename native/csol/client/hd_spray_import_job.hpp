// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hd_spray.hpp"
#include <Windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace csol::client::spray_import {
struct Result {
    SpraySettings settings;
    spray::Animation preview;
    std::string error;
    bool selected{};
};
class Job {
public:
    std::atomic<bool> cancelled{}, done{};
    Result result; // Published by done(release), read only after done(acquire).
    void Cancel() {
        cancelled = true;
        std::lock_guard lock(mutex);
        if (dialog) PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }
    void Dialog(HWND window) {
        std::lock_guard lock(mutex);
        dialog = window;
        if (dialog && cancelled) PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }
private:
    std::mutex mutex;
    HWND dialog{};
};
// One local operation at a time. No engine/VGUI calls, detached threads or
// panel pointers may be captured by work. A closed page cancels its lease.
class Worker {
public:
    using Work = std::function<Result(Job&)>;
    ~Worker() { Shutdown(); }
    bool Available() { Reap();return !active; }
    std::shared_ptr<Job> Start(Work work) {
        Reap();
        if (active) return {};
        auto job = std::make_shared<Job>();
        thread = std::thread([job, work = std::move(work)] {
            try { if (!job->cancelled) job->result = work(*job); }
            catch (const std::exception& error) { job->result.error = error.what(); }
            catch (...) { job->result.error = "图片导入失败"; }
            job->done.store(true, std::memory_order_release);
        });
        active = job;
        return job;
    }
    void Reap() {
        if (!active || !active->done.load(std::memory_order_acquire)) return;
        if (thread.joinable()) thread.join();
        active.reset();
    }
    void Shutdown() {
        if (active) active->Cancel();
        if (thread.joinable()) thread.join();
        active.reset();
    }
private:
    std::shared_ptr<Job> active;
    std::thread thread;
};
}
