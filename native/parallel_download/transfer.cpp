// SPDX-License-Identifier: GPL-3.0-or-later
#include "goldcraft/async_download.hpp"
#include <asio/write_at.hpp>
#include <asio/windows/random_access_handle.hpp>
#include "transfer.hpp"
#include <array>
#include <span>

namespace parallel_download {
namespace transport = goldcraft::async_download;
using transport::Require;
using transport::Failure;
using Clock = transport::Clock;
Job::~Job() { if (!temporary.empty()) { std::error_code ec; std::filesystem::remove(temporary, ec); } }
bool ValidPath(const std::string& path) {
    if (path.empty() || path.size() > 240 || path.front() == '/' || path.back() == '/') return false;
    for (unsigned char c : path) if (c < 32 || c >= 127 || std::string_view("\\:%?#\"<>|*").find(c) != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        auto stem = part.substr(0, part.find('.'));
        for (char& c : stem) if (c >= 'a' && c <= 'z') c -= 'a'-'A';
        if (stem == "CON" || stem == "NUL" || stem == "AUX" || stem == "PRN" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9')) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
namespace {
std::unique_ptr<asio::windows::random_access_handle> OpenFile(transport::Session& session,
        const std::filesystem::path& path, DWORD& error) noexcept {
    try {
        auto file = std::make_unique<asio::windows::random_access_handle>(session.io);
        const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { error = GetLastError(); return {}; }
        asio::error_code result; file->assign(handle, result);
        if (result) { CloseHandle(handle); error = result.value(); return {}; }
        return file;
    } catch (...) { error = ERROR_NOT_ENOUGH_MEMORY; return {}; }
}
asio::awaitable<DWORD> Body(transport::Session& session, transport::Request& request, Job& job, const Limits& limits, std::span<char> buffer) {
    asio::error_code ioError;
    co_await request.Send(asio::redirect_error(asio::use_awaitable, ioError));
    if (ioError) co_return DWORD(ioError.value());
    co_await request.Receive(asio::redirect_error(asio::use_awaitable, ioError));
    if (ioError) co_return DWORD(ioError.value());
    const auto headers = transport::ReadHeaders(request, limits.max_bytes);
    job.status = headers.status;
    if (headers.status != 200) co_return DWORD(ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
    if (headers.error) co_return headers.error;
    job.length = headers.length; job.received = 0;
    DWORD error{};
    auto file = OpenFile(session, job.temporary, error);
    if (!file) co_return error;
    std::uint64_t received = 0;
    for (;;) {
        if (job.cancelled) co_return DWORD(ERROR_CANCELLED);
        const auto read = co_await request.Read(buffer.data(), DWORD(buffer.size()),
            asio::redirect_error(asio::use_awaitable, ioError));
        if (ioError) co_return DWORD(ioError.value());
        if (!read) break;
        if (read > limits.max_bytes - received || (headers.hasLength && read > headers.length - received))
            co_return DWORD(ERROR_FILE_TOO_LARGE);
        asio::error_code writeError;
        const auto written = co_await asio::async_write_at(*file, received, asio::buffer(buffer.data(), read),
            asio::redirect_error(asio::use_awaitable, writeError));
        if (writeError) co_return DWORD(writeError.value());
        if (written != read) co_return DWORD(ERROR_WRITE_FAULT);
        received += read; job.received = received;
    }
    if (!received || (headers.hasLength && received != headers.length)) co_return DWORD(ERROR_HANDLE_EOF);
    const auto handle = file->native_handle();
    const auto code = co_await transport::Blocking(session, [handle]() -> DWORD {
        return FlushFileBuffers(handle) ? 0 : GetLastError();
    });
    co_return code ? code : job.cancelled ? DWORD(ERROR_CANCELLED) : DWORD(0);
}
asio::awaitable<DWORD> Transfer(transport::Session& session, Job& job, const Limits& limits, Clock::time_point deadline) {
    DWORD error{};
    auto request = transport::Open(session, job.url, job.cancelled, deadline, error);
    if (!request) co_return error;
    // WinHTTP owns an in-flight ReadData destination until HANDLE_CLOSING,
    // including when cancellation wakes the coroutine before READ_COMPLETE.
    std::array<char, 64 * 1024> buffer;
    const auto code = co_await Body(session, *request, job, limits, buffer);
    asio::error_code closeError;
    co_await request->Close(asio::redirect_error(asio::use_awaitable, closeError));
    co_return code;
}
struct Policy {
    Limits limits;
    static void Active(Job& job, bool value) { job.active = value; }
    asio::awaitable<void> Run(transport::Session& session, std::shared_ptr<Job> job) const {
        const auto deadline = Clock::now() + std::chrono::milliseconds(limits.total_ms);
        auto retryEnd = deadline;
        for (unsigned attempt = 0; !job->cancelled && Clock::now() < retryEnd; ++attempt) {
            job->attempts = attempt + 1; job->status = 0;
            job->error = co_await Transfer(session, *job, limits, retryEnd);
            if (!job->error) { job->success = true; break; }
            const bool transient = transport::TransientHttpFailure(job->error, job->status);
            if ((!transient && job->status >= 400 && job->status < 500) ||
                attempt >= (transient ? 11u : limits.retries) || job->cancelled) break;
            const auto now = Clock::now();
            if (!attempt) retryEnd = std::min(deadline, now + std::chrono::seconds(60));
            if (now >= retryEnd) break;
            asio::steady_timer retry(session.io, std::min(retryEnd, now + transport::RetryDelay(attempt, transient)));
            asio::error_code error;
            co_await retry.async_wait(asio::redirect_error(asio::use_awaitable, error));
            if (error) break;
        }
    }
    static void Finish(Job& job, std::exception_ptr failure) {
        if (failure) { job.success = false; job.error = ERROR_NOT_ENOUGH_MEMORY; }
        if (job.cancelled) { job.success = false; job.error = ERROR_CANCELLED; }
        job.active = false; job.done.store(true, std::memory_order_release);
    }
};
}
struct Pool::Impl {
    transport::Queue<Job, Policy> queue;
    explicit Impl(Limits value) : queue(value.workers, 32768, value.timeout_ms, Policy{value}) {}
};
Pool::Pool(Limits limits) : impl(std::make_unique<Impl>(limits)) {}
Pool::~Pool() = default;
bool Pool::Submit(const std::shared_ptr<Job>& job) { return impl->queue.Submit(job); }
void Pool::Cancel() { impl->queue.Cancel(); }
}
