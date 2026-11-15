// SPDX-License-Identifier: GPL-3.0-or-later
#include "goldcraft/async_download.hpp"
#include <bcrypt.h>
#include "resource_download.hpp"
#include "resource_cache.hpp"

#pragma comment(lib, "bcrypt.lib")
namespace goldcraft::resource_download {
namespace {
namespace transport = async_download;
struct Result { DWORD code{}; const char* message{}; };
bool Resize(Job& job, std::size_t size) noexcept {
    try { job.bytes.resize(size); return true; } catch (...) { return false; }
}
asio::awaitable<Result> Body(transport::Session& session, transport::Request& request, Job& job) {
    asio::error_code ioError;
    co_await request.Send(asio::redirect_error(asio::use_awaitable, ioError));
    if (ioError) co_return Result{DWORD(ioError.value())};
    co_await request.Receive(asio::redirect_error(asio::use_awaitable, ioError));
    if (ioError) co_return Result{DWORD(ioError.value())};
    const auto headers = transport::ReadHeaders(request, resource_sync::max_file_size);
    if (headers.error) co_return Result{headers.error};
    if (headers.status != 200) co_return Result{ERROR_WINHTTP_INVALID_SERVER_RESPONSE, "Resource server did not return HTTP 200"};
    if (!headers.hasLength || headers.length != job.expected.size)
        co_return Result{ERROR_INVALID_DATA, "Resource response length differs from manifest"};
    if (!Resize(job, job.expected.size)) co_return Result{ERROR_NOT_ENOUGH_MEMORY};
    std::size_t offset = 0;
    while (offset < job.bytes.size()) {
        const auto read = co_await request.Read(job.bytes.data() + offset,
            DWORD(std::min<std::size_t>(128 * 1024, job.bytes.size() - offset)),
            asio::redirect_error(asio::use_awaitable, ioError));
        if (ioError) co_return Result{DWORD(ioError.value())};
        if (!read || read > job.bytes.size() - offset)
            co_return Result{ERROR_HANDLE_EOF, "Resource response was truncated"};
        offset += read; job.transferred.store(std::uint32_t(offset), std::memory_order_relaxed);
    }
    const auto code = co_await transport::Blocking(session, [&job]() -> DWORD {
        resource_sync::Digest digest{};
        if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, job.bytes.data(), DWORD(job.bytes.size()),
                digest.data(), DWORD(digest.size())) < 0) return ERROR_INVALID_DATA;
        return digest == job.expected.sha256 ? 0 : ERROR_CRC;
    });
    if (code) co_return Result{code, "Resource SHA-256 differs from server manifest"};
    co_return Result{job.cancelled ? ERROR_CANCELLED : DWORD(0)};
}
asio::awaitable<Result> Transfer(transport::Session& session, Job& job) {
    job.transferred.store(0, std::memory_order_relaxed);
    if (!job.expected.required || !job.expected.size || job.expected.size > resource_sync::max_file_size)
        co_return Result{ERROR_INVALID_DATA, "Invalid resource update size"};
    DWORD error{};
    auto request = transport::Open(session, job.url, job.cancelled,
        transport::Clock::now() + std::chrono::minutes(3), error);
    if (!request) co_return Result{error};
    const auto result = co_await Body(session, *request, job);
    asio::error_code closeError;
    co_await request->Close(asio::redirect_error(asio::use_awaitable, closeError));
    co_return result;
}
struct Policy {
    static void Active(Job&, bool) {}
    static asio::awaitable<void> Run(transport::Session& session, std::shared_ptr<Job> job) {
        for (unsigned attempt = 0; attempt != 3 && !job->cancelled; ++attempt) {
            const auto result = co_await Transfer(session, *job);
            if (!result.code) {
                if (!job->cacheRoot.empty()) {
                    const auto stored = co_await transport::Blocking(session, [&job]() -> DWORD {
                        const auto start = transport::Clock::now();
                        try {
                            resource_cache::Cache cache(job->cacheRoot);
                            job->cacheStored = !job->cancelled &&
                                cache.Store(job->cacheName, job->expected, job->bytes);
                            if (!job->cacheStored) job->error = cache.Error();
                        } catch (const std::exception& failure) { job->error = failure.what(); }
                        job->cacheStoreMs = std::chrono::duration<double, std::milli>(
                            transport::Clock::now() - start).count();
                        return job->cacheStored ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
                    });
                    if (stored) { job->bytes.clear(); break; }
                    std::vector<std::uint8_t>().swap(job->bytes);
                }
                job->success = !job->cancelled;
                job->error.clear();
                break;
            }
            job->error = result.message ? result.message : "Resource HTTP error " + std::to_string(result.code);
            job->bytes.clear();
            if (attempt != 2 && !job->cancelled) {
                asio::steady_timer retry(session.io, std::chrono::milliseconds(200));
                asio::error_code error;
                co_await retry.async_wait(asio::redirect_error(asio::use_awaitable, error));
            }
        }
    }
    static void Finish(Job& job, std::exception_ptr failure) {
        if (failure) { job.success = false; job.bytes.clear(); job.error = "Resource download failed"; }
        job.done.store(true, std::memory_order_release);
    }
};
}
struct Worker::Impl { transport::Queue<Job, Policy> queue{Concurrency, Concurrency, 5000}; };
Worker::Worker() : impl(std::make_unique<Impl>()) {}
Worker::~Worker() = default;
bool Worker::Submit(std::shared_ptr<Job> job) {
    if (!job || job->submitted.exchange(true)) return false;
    if (impl->queue.Submit(job)) return true;
    job->submitted = false; return false;
}
void Worker::Cancel() { impl->queue.Cancel(); }
void Worker::Shutdown() { impl->queue.Shutdown(); }
}
