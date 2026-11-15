// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the exact x86 compiler/Asio ABI without networking or game hooks.
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <iostream>
#include <stdexcept>
struct Pair { unsigned error{}, bytes{}; };
asio::awaitable<Pair> PairResult() {
    co_await asio::post(asio::use_awaitable);
    co_return Pair{0, 1234};
}
asio::awaitable<unsigned> ScalarResult() {
    co_await asio::post(asio::use_awaitable);
    co_return 1234;
}
asio::awaitable<void> FailureResult() {
    co_await asio::post(asio::use_awaitable);
    throw std::runtime_error("expected probe exception");
}
asio::awaitable<void> Probe(unsigned mode) {
    if (mode == 0) {
        auto result = co_await PairResult();
        if (result.error || result.bytes != 1234) throw std::runtime_error("pair result corrupted");
        result = co_await PairResult();
        if (result.error || result.bytes != 1234) throw std::runtime_error("reassigned pair result corrupted");
    } else if (mode == 1) {
        if (auto result = co_await ScalarResult(); result != 1234) throw std::runtime_error("scalar result corrupted");
    } else {
        bool caught{};
        try { co_await FailureResult(); } catch (const std::runtime_error&) { caught = true; }
        if (!caught) throw std::runtime_error("exception not caught");
    }
}
int main(int argc, char** argv) {
    static_assert(sizeof(void*) == 4);
    if (argc != 2) return 2;
    asio::io_context io;
    bool passed{};
    asio::co_spawn(io, Probe(unsigned(std::stoul(argv[1]))), [&](std::exception_ptr failure) { passed = !failure; });
    io.run();
    std::cout << "coroutine ABI " << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
