// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <asio/awaitable.hpp>
#include <asio/any_completion_handler.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Host transport adaptation. Asio owns coroutine frames, scheduling, IOCP and
// overlapped-file completion; WinHTTP owns HTTP/TLS and its callback threads.
namespace goldcraft::async_download {
using Clock = std::chrono::steady_clock;
struct Failure { DWORD code; };
inline void Require(bool valid, DWORD code = 0) {
    if (!valid) throw Failure{code ? code : GetLastError()};
}
inline bool TransientHttpFailure(DWORD code, unsigned status = 0) noexcept {
    if (status) return status == 408 || status == 429 || status == 500 ||
        status == 502 || status == 503 || status == 504;
    switch (code) {
    case ERROR_TIMEOUT: case ERROR_WINHTTP_TIMEOUT: case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR: case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_RESEND_REQUEST: case ERROR_CONNECTION_ABORTED:
    case ERROR_CONNECTION_INVALID: case ERROR_CONNECTION_REFUSED:
        return true;
    default: return false;
    }
}
inline std::chrono::milliseconds RetryDelay(unsigned attempt, bool transient) noexcept {
    return std::chrono::milliseconds(transient ? std::min(4000u, 250u << std::min(attempt, 4u)) : 200u);
}
struct HttpHandle {
    HINTERNET value{};
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};
class Request;
class Session {
public:
    asio::io_context io{1};
    const unsigned operationTimeout, connectionLimit;
    explicit Session(unsigned concurrency, unsigned timeout) : operationTimeout(timeout), connectionLimit(concurrency) {}
    void Open() {
        if (http.value) return;
        unload = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(unload != nullptr);
        try {
            http.value = WinHttpOpen(L"GoldCraftAsyncDownload/2", WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
            Require(http.value != nullptr);
            // The session event fences callback code itself before DLL unload,
            // beyond the per-request HANDLE_CLOSING/context ownership fence.
            Require(WinHttpSetOption(http.value, WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT, &unload, sizeof(unload)) != FALSE);
            sessionFence = true;
            Require(WinHttpSetTimeouts(http.value, operationTimeout, operationTimeout, operationTimeout, operationTimeout) != FALSE);
            DWORD connections = connectionLimit;
            Require(WinHttpSetOption(http.value, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &connections, sizeof(connections)) != FALSE);
            Require(WinHttpSetOption(http.value, WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER, &connections, sizeof(connections)) != FALSE);
        } catch (...) {
            if (http.value) { WinHttpCloseHandle(http.value); http.value = nullptr; }
            if (sessionFence) WaitForSingleObject(unload, INFINITE);
            sessionFence = false;
            CloseHandle(unload); unload = nullptr; throw;
        }
    }
    ~Session() {
        if (http.value) {
            WinHttpCloseHandle(http.value); http.value = nullptr;
            WaitForSingleObject(unload, INFINITE);
        }
        if (unload) CloseHandle(unload);
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    HINTERNET Native() { Open(); return http.value; }
    // Only immutable data/hash/flush work may use this bounded blocking lane.
    // Network waiting and disk writes never occupy it.
    asio::thread_pool& Blocking() {
        if (!blocking) blocking = std::make_unique<asio::thread_pool>(1);
        return *blocking;
    }
    void Poll();
private:
    friend class Request;
    HttpHandle http;
    HANDLE unload{};
    bool sessionFence{};
    std::unique_ptr<asio::thread_pool> blocking;
    std::vector<Request*> requests; // Owned/changed only by the Asio thread.
};

class Request {
public:
    using Handler = asio::any_completion_handler<void(asio::error_code, DWORD)>;
    Request(Session& owner, const std::string& url, std::atomic<bool>& cancelled,
            Clock::time_point deadline) : owner(owner), cancelled(cancelled), deadline(deadline) {
        Require(!cancelled, ERROR_CANCELLED);
        Require(!url.empty() && url.size() < 2048, ERROR_INVALID_NAME);
        for (unsigned char c : url) Require(c >= 32 && c < 127, ERROR_INVALID_NAME);
        const std::wstring wide(url.begin(), url.end());
        URL_COMPONENTS p{}; p.dwStructSize = sizeof(p);
        p.dwHostNameLength = p.dwUrlPathLength = p.dwExtraInfoLength =
            p.dwUserNameLength = p.dwPasswordLength = DWORD(-1);
        Require(WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &p) && p.dwHostNameLength &&
            !p.dwUserNameLength && !p.dwPasswordLength && !p.dwExtraInfoLength &&
            (p.nScheme == INTERNET_SCHEME_HTTP || p.nScheme == INTERNET_SCHEME_HTTPS), ERROR_INVALID_NAME);
        const std::wstring host(p.lpszHostName, p.dwHostNameLength), path(p.lpszUrlPath, p.dwUrlPathLength);
        connection.value = WinHttpConnect(owner.Native(), host.c_str(), p.nPort, 0);
        Require(connection.value != nullptr);
        value = WinHttpOpenRequest(connection.value, L"GET", path.c_str(), L"HTTP/1.1", WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, p.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        Require(value != nullptr);
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        closeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!closeEvent || !WinHttpSetOption(value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
            !WinHttpSetOption(value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))) {
            const auto code = GetLastError(); WinHttpCloseHandle(value); value = nullptr;
            if (closeEvent) CloseHandle(closeEvent);
            closeEvent = nullptr; throw Failure{code};
        }
    }
    ~Request() {
        if (value) WinHttpCloseHandle(value);
        // Only exceptional unwinding uses a blocking fence. All normal success,
        // failure and cancellation paths await AsyncClose on the IO executor.
        if (armed) {
            WaitForSingleObject(closeEvent, INFINITE);
            { std::lock_guard lock(mutex); }
            std::erase(owner.requests, this);
        }
        CloseHandle(closeEvent);
    }
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    void Arm() {
        owner.requests.reserve(owner.requests.size() + 1);
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        Require(WinHttpSetOption(value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) != FALSE);
        Require(WinHttpSetStatusCallback(value, Callback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK);
        armed = true; owner.requests.push_back(this);
    }
    template<class Token> auto Send(Token&& token) {
        return Operation(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, nullptr, 0, std::forward<Token>(token));
    }
    template<class Token> auto Receive(Token&& token) {
        return Operation(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, nullptr, 0, std::forward<Token>(token));
    }
    template<class Token> auto Available(Token&& token) {
        return Operation(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE, nullptr, 0, std::forward<Token>(token));
    }
    template<class Token> auto Read(void* buffer, DWORD bytes, Token&& token) {
        return Operation(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, buffer, bytes, std::forward<Token>(token));
    }
    template<class Token> auto Close(Token&& token) {
        return asio::async_initiate<Token, void(asio::error_code, DWORD)>(
            [this](auto handler) {
                Handler finished([this, handler = std::move(handler)](asio::error_code error, DWORD count) mutable {
                    Retire(); handler(error, count);
                });
                Handler ready;
                {
                    std::lock_guard lock(mutex);
                    if (!armed || closed) ready = std::move(finished);
                    else closeHandler = std::move(finished);
                }
                if (ready) Post(std::move(ready), 0, 0);
                else Abort(0);
            }, token);
    }
    void Retire() {
        std::erase(owner.requests, this); armed = false;
    }
    unsigned Status() const {
        DWORD status{}, size = sizeof(status);
        Require(WinHttpQueryHeaders(value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE);
        return status;
    }
    bool Length(std::uint64_t& bytes, std::uint64_t maximum) const {
        wchar_t text[32]{}; DWORD length = sizeof(text);
        if (!WinHttpQueryHeaders(value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                text, &length, WINHTTP_NO_HEADER_INDEX)) {
            Require(GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND); return false;
        }
        Require(text[0] != 0, ERROR_INVALID_DATA);
        bytes = 0;
        for (const auto c : text) {
            if (!c) break;
            Require(c >= L'0' && c <= L'9', ERROR_INVALID_DATA);
            const auto digit = unsigned(c - L'0');
            Require(bytes <= maximum / 10 && (bytes < maximum / 10 || digit <= maximum % 10), ERROR_FILE_TOO_LARGE);
            bytes = bytes * 10 + digit;
        }
        Require(bytes > 0 && bytes <= maximum, ERROR_FILE_TOO_LARGE); return true;
    }
    void Poll() {
        if (cancelled.load(std::memory_order_relaxed)) Abort(ERROR_CANCELLED);
        else {
            bool expired{};
            { std::lock_guard lock(mutex); expired = Clock::now() >= std::min(deadline, operationDeadline); }
            if (expired) Abort(ERROR_TIMEOUT);
        }
    }
private:
    Session& owner;
    std::atomic<bool>& cancelled;
    Clock::time_point deadline;
    HttpHandle connection;
    HINTERNET value{};
    HANDLE closeEvent{};
    std::mutex mutex;
    Handler pending, closeHandler;
    DWORD expected{}, abortReason{};
    Clock::time_point operationDeadline{Clock::time_point::max()};
    bool armed{}, closed{};
    void Post(Handler handler, DWORD error, DWORD count) {
        asio::post(owner.io, [handler = std::move(handler), error, count]() mutable {
            handler(error ? asio::error_code(error, asio::error::get_system_category()) : asio::error_code{}, count);
        });
    }
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void* data, DWORD length) {
        if (!context) return;
        auto& self = *reinterpret_cast<Request*>(context);
        auto executor = self.owner.io.get_executor();
        Handler completion, closing;
        DWORD error{};
        {
            std::lock_guard lock(self.mutex);
            if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
                self.closed = true;
                SetEvent(self.closeEvent);
                closing = std::move(self.closeHandler);
                // Closing is the last notification. Publish any still-pending
                // operation too, without losing its cancelled/timeout reason.
                error = self.abortReason ? self.abortReason : ERROR_OPERATION_ABORTED;
                completion = std::move(self.pending);
            } else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
                error = self.abortReason ? self.abortReason :
                    data ? static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError : ERROR_GEN_FAILURE;
                completion = std::move(self.pending);
            } else if (status == self.expected) {
                error = self.abortReason;
                if (status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE && !error) {
                    if (data && length == sizeof(DWORD)) length = *static_cast<const DWORD*>(data);
                    else error = ERROR_INVALID_DATA;
                }
                completion = std::move(self.pending);
            } else return;
            self.operationDeadline = Clock::time_point::max();
        }
        // No context access follows releasing the mutex, including when an
        // exceptional-unwind destructor is waiting on the closing event.
        if (completion) asio::post(executor, [handler = std::move(completion), error, length]() mutable {
            handler(error ? asio::error_code(error, asio::error::get_system_category()) : asio::error_code{}, length);
        });
        if (closing) asio::post(executor, [handler = std::move(closing)]() mutable { handler(asio::error_code{}, DWORD(0)); });
    }
    template<class Token> auto Operation(DWORD event, void* buffer, DWORD bytes, Token&& token) {
        return asio::async_initiate<Token, void(asio::error_code, DWORD)>(
            [this, event, buffer, bytes](auto handler) {
                DWORD error = cancelled ? ERROR_CANCELLED : !value ? ERROR_OPERATION_ABORTED : 0;
                if (error) { Post(std::move(handler), error, 0); return; }
                {
                    std::lock_guard lock(mutex);
                    expected = event; pending = std::move(handler);
                    operationDeadline = std::min(deadline, Clock::now() + std::chrono::milliseconds(owner.operationTimeout));
                }
                BOOL started{};
                if (event == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
                    started = WinHttpSendRequest(value, L"Cache-Control: no-cache\r\n", DWORD(-1),
                        WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(this));
                else if (event == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
                    started = WinHttpReceiveResponse(value, nullptr);
                else if (event == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE)
                    started = WinHttpQueryDataAvailable(value, nullptr);
                else started = WinHttpReadData(value, buffer, bytes, nullptr);
                if (started || GetLastError() == ERROR_IO_PENDING) return;
                error = GetLastError();
                Handler failure;
                { std::lock_guard lock(mutex); failure = std::move(pending); operationDeadline = Clock::time_point::max(); }
                if (failure) Post(std::move(failure), error, 0);
            }, token);
    }
    void Abort(DWORD reason) {
        if (!value) return;
        if (reason) { std::lock_guard lock(mutex); abortReason = reason; }
        WinHttpCloseHandle(std::exchange(value, nullptr));
    }
};
inline void Session::Poll() { for (auto* request : requests) request->Poll(); }

// Keep synchronous SDK/setup exceptions within ordinary C++ functions. Normal
// HTTP, cancellation and integrity failures use values across suspension points.
inline std::unique_ptr<Request> Open(Session& session, const std::string& url,
        std::atomic<bool>& cancelled, Clock::time_point deadline, DWORD& error) noexcept {
    try {
        auto request = std::make_unique<Request>(session, url, cancelled, deadline);
        request->Arm(); return request;
    } catch (const Failure& failure) { error = failure.code; }
    catch (...) { error = ERROR_NOT_ENOUGH_MEMORY; }
    return {};
}
struct Headers {
    DWORD error{};
    unsigned status{};
    bool hasLength{};
    std::uint64_t length{};
};
inline Headers ReadHeaders(Request& request, std::uint64_t maximum) noexcept {
    Headers result;
    try { result.status = request.Status(); result.hasLength = request.Length(result.length, maximum); }
    catch (const Failure& failure) { result.error = failure.code; }
    catch (...) { result.error = ERROR_NOT_ENOUGH_MEMORY; }
    return result;
}

// The SDK provides no overlapped FlushFileBuffers or BCryptHash. Submit those
// bounded operations to Asio's shared blocking lane and return to the caller's
// executor, keeping request frames/buffers alive until completion.
template<class Function>
asio::awaitable<DWORD> Blocking(Session& session, Function function) {
    co_return co_await asio::async_initiate<decltype(asio::use_awaitable), void(DWORD)>(
        [&session, function = std::move(function)](auto handler) mutable {
            auto executor = asio::get_associated_executor(handler);
            asio::post(session.Blocking(), [executor, function = std::move(function), handler = std::move(handler)]() mutable {
                thread_local const bool named = [] {
                    SetThreadDescription(GetCurrentThread(), L"GoldCraft download finalization"); return true;
                }();
                (void)named;
                const DWORD code = function();
                asio::post(executor, [handler = std::move(handler), code]() mutable { handler(code); });
            });
        }, asio::use_awaitable);
}

// One Asio dispatcher regardless of concurrent file count. Pending jobs are
// data only; coroutine frames/read buffers are allocated on admission.
template<class Job, class Policy>
class Queue {
public:
    Queue(unsigned concurrency, std::size_t capacity, unsigned timeout, Policy policy = {}) :
        concurrency(std::clamp(concurrency, 1u, 16u)), capacity(capacity),
        session(this->concurrency, timeout), guard(asio::make_work_guard(session.io)), timer(session.io), policy(std::move(policy)) {}
    ~Queue() { Shutdown(); }
    bool Submit(std::shared_ptr<Job> job) {
        std::lock_guard lock(mutex);
        if (!job || stopping || pending.size() + active.size() >= capacity) return false;
        if (!thread.joinable()) thread = std::thread([this] {
            SetThreadDescription(GetCurrentThread(), L"GoldCraft download IO");
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); session.io.run();
        });
        pending.push_back(std::move(job));
        asio::post(session.io, [this] { Drain(); }); return true;
    }
    void Cancel() {
        std::lock_guard lock(mutex); CancelLocked();
        if (thread.joinable()) asio::post(session.io, [this] { CancelRunning(); session.Poll(); });
    }
    void Shutdown() {
        {
            std::lock_guard lock(mutex);
            if (stopping) return;
            stopping = true; CancelLocked();
            if (thread.joinable()) asio::post(session.io, [this] { CancelRunning(); session.Poll(); });
            guard.reset();
        }
        if (thread.joinable()) thread.join();
    }
private:
    unsigned concurrency;
    std::size_t capacity;
    Session session;
    asio::executor_work_guard<asio::io_context::executor_type> guard;
    asio::steady_timer timer;
    std::thread thread;
    std::mutex mutex;
    std::deque<std::shared_ptr<Job>> pending;
    struct Running {
        explicit Running(std::shared_ptr<Job> value) : job(std::move(value)) {}
        std::shared_ptr<Job> job;
        asio::cancellation_signal cancellation;
        bool cancelled{}; // Asio executor only.
    };
    std::vector<std::shared_ptr<Running>> active;
    bool stopping{}, ticking{};
    Policy policy;
    void CancelLocked() {
        for (auto& job : pending) { job->cancelled = true; job->done.store(true, std::memory_order_release); }
        pending.clear();
        for (auto& task : active) task->job->cancelled = true;
    }
    void CancelRunning() {
        // Cancellation signals and coroutine frames belong to the IO executor.
        // Snapshot under the queue mutex; emitting can dispatch completions.
        std::array<std::shared_ptr<Running>, 16> snapshot{};
        std::size_t count{};
        {
            std::lock_guard lock(mutex);
            for (const auto& task : active)
                if (task->job->cancelled && !task->cancelled) {
                    task->cancelled = true; snapshot[count++] = task;
                }
        }
        for (std::size_t i = 0; i < count; ++i)
            snapshot[i]->cancellation.emit(asio::cancellation_type::terminal);
    }
    void Drain() {
        std::lock_guard lock(mutex);
        while (!stopping && !pending.empty() && active.size() < concurrency) {
            auto task = std::make_shared<Running>(std::move(pending.front())); pending.pop_front(); active.push_back(task);
            Policy::Active(*task->job, true);
            asio::co_spawn(session.io, policy.Run(session, task->job),
                asio::bind_cancellation_slot(task->cancellation.slot(), [this, task](std::exception_ptr failure) {
                Policy::Finish(*task->job, failure);
                { std::lock_guard lock(mutex); std::erase(active, task); }
                Drain();
            }));
        }
        if (!active.empty() && !ticking) Tick();
    }
    void Tick() {
        ticking = true; timer.expires_after(std::chrono::milliseconds(20));
        timer.async_wait([this](asio::error_code error) {
            if (error) return;
            CancelRunning(); session.Poll();
            std::lock_guard lock(mutex);
            if (!active.empty()) Tick(); else ticking = false;
        });
    }
};
}
