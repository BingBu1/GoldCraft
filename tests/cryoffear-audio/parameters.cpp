// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/audio.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <bit>
#include <cstdio>
#include <fcntl.h>
#include <io.h>

using namespace guest::cof::client;
namespace
{
struct Input { float volume, random; } input{};
float Volume(void*) noexcept { return input.volume; }
float Random(void*, float, float) noexcept { return input.random; }
}
int main()
{
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
    // Synthetic unsigned8 mono PCM silence, one second, zero game resources.
    std::array<std::byte, 8044> wave{};
    constexpr std::array<unsigned char, 44> header{
        'R','I','F','F',0x64,0x1f,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x40,0x1f,0,0,0x40,0x1f,0,0,1,0,8,0,'d','a','t','a',0x40,0x1f,0,0};
    for (unsigned i = 0; i < header.size(); ++i) wave[i] = std::byte(header[i]);
    for (unsigned i = 44; i < wave.size(); ++i) wave[i] = std::byte{128};
    LocalAudio audio;
    if (audio.Initialize({1, 1, 8000}, {nullptr, Volume, Random}) != AudioResult::Applied ||
        audio.Prepare("silent.wav", wave, 1) != AudioResult::Applied || audio.Activate() != AudioResult::Applied) return 1;
    while (std::fread(&input, sizeof(input), 1, stdin))
    {
        if (audio.Play({"silent.wav", 1, false, {0, 0, 0}}) != AudioResult::Applied) return 2;
        AudioVoiceState state;
        // XAudio2 applies SetVolume/FrequencyRatio asynchronously. Wait for a
        // processing quantum before diagnostics; never used in production.
        Sleep(25);
        if (audio.InspectVoice(0, state) != AudioResult::Applied || !state.active || !state.looped) return 3;
        const std::array<unsigned, 4> output{std::bit_cast<unsigned>(state.requestedVolume), std::bit_cast<unsigned>(state.requestedSpeed),
                                             std::bit_cast<unsigned>(state.volume), std::bit_cast<unsigned>(state.speed)};
        if (std::fwrite(output.data(), sizeof(output), 1, stdout) != 1) return 4;
    }
    return audio.Shutdown() == AudioResult::Applied ? 0 : 5;
}
