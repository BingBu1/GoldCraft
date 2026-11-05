// SPDX-License-Identifier: GPL-3.0-or-later
// Executes the actual Windows HTTP worker against an independently served body.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "resource_download.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using goldcraft::resource_download::Job;
using goldcraft::resource_download::Worker;
namespace {
int checks{};
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void Wait(const std::shared_ptr<Job>& job) {
    auto until = std::chrono::steady_clock::now() + 12s;
    while (!job->done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(2ms);
    Check(job->done.load(std::memory_order_acquire), "job completion timeout");
}
std::shared_ptr<Job> Make(std::string url, std::uint32_t size, const std::string& sha) {
    auto job = std::make_shared<Job>(); job->url = std::move(url);
    job->expected.required = true; job->expected.size = size;
    Check(sha.size() == 64, "digest argument length");
    for (unsigned i = 0; i != 32; ++i) job->expected.sha256[i] = std::uint8_t(std::stoul(sha.substr(i * 2, 2), nullptr, 16));
    return job;
}
void Started(const std::shared_ptr<Job>& job) {
    auto until = std::chrono::steady_clock::now() + 5s;
    while (!job->transferred.load() && !job->done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(2ms);
    Check(job->transferred.load() > 0 && !job->done.load(std::memory_order_acquire), "expected in-flight body");
}
}
int main(int argc, char** argv) {
    static_assert(sizeof(void*) == 4);
    if (argc != 6) return 2;
    const std::string mode = argv[1], url = argv[2], sha = argv[4];
    const auto size = std::uint32_t(std::stoul(argv[3]));
    try {
        if (mode == "good" || mode == "bad") {
            Worker worker; auto job = Make(url, size, sha);
            Check(worker.Submit(job), "submit accepted"); Wait(job);
            Check(job->success == (mode == "good"), "transfer outcome");
            if (mode == "good") {
                std::ifstream f(argv[5], std::ios::binary);
                const std::vector<std::uint8_t> expected{std::istreambuf_iterator<char>(f), {}};
                Check(expected.size() == size && job->bytes == expected, "actual bytes equal independent served body");
                Check(job->error.empty() && job->transferred == size, "success published completely");
            } else {
                Check(job->bytes.empty() && !job->error.empty(), "failure discards unverified bytes");
                std::cout << "ERROR " << job->error << '\n';
            }
            worker.Shutdown(); worker.Shutdown();
            Check(!worker.Submit(Make(url, size, sha)), "submit after shutdown rejected");
        } else if (mode == "cancel") {
            Worker worker; auto job = Make(url, size, sha); Check(worker.Submit(job), "cancel setup submit"); Started(job);
            const auto start = std::chrono::steady_clock::now(); worker.Cancel(); Wait(job);
            Check(!job->success && job->cancelled && job->bytes.empty(), "active cancellation publishes failure and discards buffer");
            Check(std::chrono::steady_clock::now() - start < 2s, "cancellation bounded");
            auto fresh = Make(url.substr(0, url.rfind('/')) + "/good-after-cancel", size, sha);
            Check(worker.Submit(fresh), "worker accepts fresh job after Cancel"); Wait(fresh); Check(fresh->success, "post-cancel transfer succeeds");
        } else if (mode == "lifetime") {
            for (unsigned iteration = 0; iteration != 16; ++iteration) {
                std::weak_ptr<Job> retained;
                {
                    Worker worker; std::vector<std::shared_ptr<Job>> jobs;
                    for (unsigned i = 0; i != 4; ++i) {
                        auto job = Make(url + "-" + std::to_string(iteration) + "-" + std::to_string(i), size, sha);
                        Check(worker.Submit(job), "bounded queue accepts four jobs"); jobs.push_back(job);
                    }
                    Check(!worker.Submit(Make(url, size, sha)), "fifth job rejected");
                    Started(jobs[0]); Started(jobs[1]);
                    retained = jobs[0]; const auto start = std::chrono::steady_clock::now(); worker.Shutdown();
                    Check(std::chrono::steady_clock::now() - start < 2s, "shutdown joins async callbacks promptly");
                    for (const auto& job : jobs) {
                        Check(job->done.load(std::memory_order_acquire) && job->cancelled && !job->success,
                              "shutdown publishes active and queued cancellation");
                        Check(job->bytes.empty(), "shutdown leaves no transfer buffer");
                    }
                    Check(!worker.Submit(Make(url, size, sha)), "closed worker rejects queued work");
                }
                Check(retained.expired(), "worker/callback ownership released after shutdown");
            }
        } else if (mode == "destructor") {
            auto job = Make(url, size, sha); std::weak_ptr<Job> retained = job;
            { Worker worker; Check(worker.Submit(job), "destructor submit"); Started(job); }
            Check(job->done.load(std::memory_order_acquire) && job->cancelled && !job->success && job->bytes.empty(),
                  "destructor waits for pending request and publishes cancelled completion");
            job.reset(); Check(retained.expired(), "destructor releases shared job");
        } else if (mode == "reuse") {
            Worker worker; auto job = Make(url, size, sha); Check(worker.Submit(job), "first submission accepted"); Wait(job);
            Check(job->success, "first use succeeds");
            Check(!worker.Submit(job), "completed Job must not be resubmitted with stale done/success");
        } else if (mode == "pre-cancel") {
            Worker worker; auto job = Make(url, size, sha); job->cancelled = true;
            Check(worker.Submit(job), "pre-cancel submission accepted for completion"); Wait(job);
            Check(!job->success && job->bytes.empty(), "pre-cancelled job performs no transfer");
        } else throw std::runtime_error("unknown mode");
        std::cout << "PASS " << mode << ' ' << checks << " checks\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << mode << ' ' << checks << " checks: " << e.what() << '\n'; return 1;
    }
}
