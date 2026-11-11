// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/presentation.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <xmmintrin.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    _mm_setcsr(0x1F80);
    using namespace guest::cof;
    using namespace guest::cof::client;
    std::array<std::uint32_t, 11> words;
    Presentation service({});
    constexpr AuthorityKey key{1, 1, 1};
    while (std::fread(words.data(), sizeof(words), 1, stdin) == 1)
    {
        const auto f = [&](std::size_t index) { return std::bit_cast<float>(words[index]); };
        service.BeginAuthority(key, {f(0), f(1), f(2)});
        if (service.Receive({key, Weapon::Glock, true, std::bit_cast<std::int32_t>(words[3]), f(6)}) != Result::Applied)
            return 3;
        AimFovOutput output;
        if (!service.UpdateClientData({std::bit_cast<std::int32_t>(words[4]), f(5), f(7), f(8), f(9), f(10)}, output))
            return 4;
        const auto& state = service.FovState();
        const std::array<std::uint32_t, 6> result{
            std::bit_cast<std::uint32_t>(state.current), std::bit_cast<std::uint32_t>(state.previousTime),
            std::bit_cast<std::uint32_t>(state.settledTarget), std::bit_cast<std::uint32_t>(output.wholeFov),
            std::bit_cast<std::uint32_t>(output.sensitivity), std::bit_cast<std::uint32_t>(output.clientFov)};
        if (std::fwrite(result.data(), sizeof(result), 1, stdout) != 1) return 2;
    }
    return std::ferror(stdin) ? 1 : 0;
}
