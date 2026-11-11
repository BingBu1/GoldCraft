// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/aim_fov.hpp"

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
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    _mm_setcsr(0x1F80); // Match the oracle's default round-to-nearest environment.
    std::array<std::uint32_t, 11> words;
    while (std::fread(words.data(), sizeof(words), 1, stdin) == 1)
    {
        const auto value = [&](std::size_t index) { return std::bit_cast<float>(words[index]); };
        guest::cof::AimFovState state{value(0), value(1), value(2)};
        const guest::cof::AimFovFrame frame{
            std::bit_cast<std::int32_t>(words[3]), std::bit_cast<std::int32_t>(words[4]),
            value(5), value(6), value(7), value(8), value(9), value(10)};
        const auto output = guest::cof::UpdateAimFov(state, frame);
        const std::array<std::uint32_t, 6> result{
            std::bit_cast<std::uint32_t>(state.current),
            std::bit_cast<std::uint32_t>(state.previousTime),
            std::bit_cast<std::uint32_t>(state.settledTarget),
            std::bit_cast<std::uint32_t>(output.wholeFov),
            std::bit_cast<std::uint32_t>(output.sensitivity),
            std::bit_cast<std::uint32_t>(output.clientFov)};
        if (std::fwrite(result.data(), sizeof(result), 1, stdout) != 1)
            return 2;
    }
    return std::ferror(stdin) ? 1 : 0;
}
