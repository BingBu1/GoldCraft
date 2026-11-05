// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include "resource_download.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace goldcraft::resource_download {
namespace {
using Clock = std::chrono::steady_clock;
void Check(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
struct Handle {
    HINTERNET value{};
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    operator HINTERNET() const { return value; }
};
struct Request {
    HINTERNET value{};
    bool armed{}, closed{}, error{};
    DWORD status{}, count{};
    std::mutex mutex;
    std::condition_variable wake;
    explicit Request(HINTERNET handle) : value(handle) {}
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD event, void*, DWORD length) {
        if (!context) return;
        auto& self = *reinterpret_cast<Request*>(context);
        std::lock_guard lock(self.mutex);
        if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) self.closed = true;
        else if (event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) self.error = true;
        else if (event == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                 event == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE || event == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            self.status = event;
            self.count = length;
        }
        self.wake.notify_all();
    }
    void Arm() {
        Check(value != nullptr, "Cannot create resource HTTP request");
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        Check(WinHttpSetOption(value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) != FALSE,
              "Cannot initialize HTTP callback");
        Check(WinHttpSetStatusCallback(value, Callback,
              WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK,
              "Cannot register HTTP callback");
        armed = true;
    }
    void Begin() { std::lock_guard lock(mutex); status = count = 0; error = false; }
    DWORD Wait(Job& job, DWORD expected, Clock::time_point deadline) {
        std::unique_lock lock(mutex);
        while (!error && !closed && status != expected && !job.cancelled && Clock::now() < deadline)
            wake.wait_for(lock, std::chrono::milliseconds(25));
        Check(!job.cancelled, "Resource update cancelled");
        Check(!error && !closed && status == expected && Clock::now() < deadline, "Resource HTTP request failed or timed out");
        return count;
    }
    ~Request() {
        if (!value) return;
        WinHttpCloseHandle(value);
        if (armed) { std::unique_lock lock(mutex); wake.wait(lock, [&] { return closed; }); }
    }
    operator HINTERNET() const { return value; }
};
void Started(BOOL value) { Check(value || GetLastError() == ERROR_IO_PENDING, "Resource HTTP operation failed"); }
void Transfer(Job& job) {
    job.transferred.store(0, std::memory_order_relaxed);
    Check(job.expected.required && job.expected.size && job.expected.size <= resource_sync::max_file_size,
          "Invalid resource update size");
    Check(!job.cancelled && job.url.size() < 2048, "Invalid resource update URL");
    for (unsigned char c : job.url) Check(c >= 32 && c < 127, "Invalid resource URL character");
    const std::wstring url(job.url.begin(), job.url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
    Check(WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts) != FALSE && parts.dwHostNameLength &&
          !parts.dwUserNameLength && !parts.dwPasswordLength && !parts.dwExtraInfoLength &&
          (parts.nScheme == INTERNET_SCHEME_HTTP || parts.nScheme == INTERNET_SCHEME_HTTPS), "Unsupported resource URL");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
    Handle session{WinHttpOpen(L"GoldCraftResourceSync/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    Check(session.value != nullptr, "Cannot open resource HTTP session");
    Check(WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000) != FALSE, "Cannot set resource timeouts");
    Handle connection{WinHttpConnect(session, host.c_str(), parts.nPort, 0)};
    Check(connection.value != nullptr, "Cannot connect resource HTTP session");
    Request request{WinHttpOpenRequest(connection, L"GET", path.c_str(), L"HTTP/1.1", WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    request.Arm();
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    Check(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) &&
          WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)), "Cannot set HTTP policy");
    const auto deadline = Clock::now() + std::chrono::minutes(3);
    request.Begin();
    Started(WinHttpSendRequest(request, L"Connection: close\r\nCache-Control: no-cache\r\n", DWORD(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&request)));
    request.Wait(job, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline);
    request.Begin();
    Started(WinHttpReceiveResponse(request, nullptr));
    request.Wait(job, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline);
    DWORD status{}, length = sizeof(status), size{};
    Check(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) && status == 200,
        "Resource server did not return HTTP 200");
    length = sizeof(size);
    Check(WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &size, &length, WINHTTP_NO_HEADER_INDEX) && size == job.expected.size,
        "Resource response length differs from manifest");
    job.bytes.resize(size);
    std::size_t offset = 0;
    while (offset < size) {
        Check(!job.cancelled, "Resource update cancelled");
        request.Begin();
        Started(WinHttpReadData(request, job.bytes.data() + offset, DWORD(std::min<std::size_t>(128 * 1024, size - offset)), nullptr));
        const auto count = request.Wait(job, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline);
        Check(count && count <= size - offset, "Resource response was truncated");
        offset += count;
        job.transferred.store(std::uint32_t(offset), std::memory_order_relaxed);
    }
    resource_sync::Digest digest{};
    Check(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, job.bytes.data(), size, digest.data(), DWORD(digest.size())) >= 0,
          "Resource SHA-256 calculation failed");
    Check(digest == job.expected.sha256, "Resource SHA-256 differs from server manifest");
    Check(!job.cancelled, "Resource update cancelled");
}
}
struct Worker::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping{};
    std::deque<std::shared_ptr<Job>> queued;
    std::vector<std::shared_ptr<Job>> active;
    std::vector<std::thread> threads;
    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        for (;;) {
            std::shared_ptr<Job> job;
            { std::unique_lock lock(mutex); wake.wait(lock, [&] { return stopping || !queued.empty(); });
                if (stopping) return;
                job = queued.front(); queued.pop_front(); active.push_back(job); }
            for (unsigned attempt = 0; attempt != 3 && !job->cancelled; ++attempt) {
                try { Transfer(*job); job->success = true; job->error.clear(); break; }
                catch (const std::exception& error) { job->error = error.what(); job->bytes.clear(); }
                if (attempt != 2) { std::unique_lock lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(200), [&] { return stopping || job->cancelled; }); }
            }
            job->done.store(true, std::memory_order_release);
            std::lock_guard lock(mutex); std::erase(active, job);
        }
    }
};
Worker::Worker() : impl(std::make_unique<Impl>()) {}
Worker::~Worker() { Shutdown(); }
bool Worker::Submit(std::shared_ptr<Job> job) {
    std::lock_guard lock(impl->mutex);
    if (!job || impl->stopping || impl->queued.size() + impl->active.size() >= 4 || job->submitted.exchange(true)) return false;
    if (impl->threads.empty()) {
        try { for (unsigned i = 0; i != 2; ++i) impl->threads.emplace_back([this] { impl->Run(); }); }
        catch (...) { if (impl->threads.empty()) { job->submitted = false; return false; } }
    }
    impl->queued.push_back(std::move(job)); impl->wake.notify_one(); return true;
}
void Worker::Cancel() {
    std::lock_guard lock(impl->mutex);
    for (auto& job : impl->active) job->cancelled = true;
    for (auto& job : impl->queued) { job->cancelled = true; job->done.store(true, std::memory_order_release); }
    impl->queued.clear(); impl->wake.notify_all();
}
void Worker::Shutdown() {
    { std::lock_guard lock(impl->mutex);
        impl->stopping = true;
        for (auto& job : impl->active) job->cancelled = true;
        for (auto& job : impl->queued) { job->cancelled = true; job->done.store(true, std::memory_order_release); }
        impl->queued.clear(); impl->wake.notify_all(); }
    for (auto& thread : impl->threads) thread.join();
    impl->threads.clear();
}
}
