// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/audio.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace guest::cof::client;
namespace
{
float Volume(void*) noexcept { return 0; }
float Random(void*, float, float) noexcept { return 0; }
}
int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    LocalAudio audio;
    if (audio.Initialize({64, 128, 32 * 1024 * 1024}, {nullptr, Volume, Random}) != AudioResult::Applied) return 3;
    std::vector<std::string> keys;
    for (const auto* weapon : {L"glock", L"p345", L"revolver", L"vp70"})
    {
        const auto folder = std::filesystem::path(argv[1]) / weapon;
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            if (!entry.is_regular_file()) continue;
            std::ifstream file(entry.path(), std::ios::binary | std::ios::ate);
            if (!file) return 4;
            const auto length = file.tellg(); if (length <= 0) return 5;
            std::vector<std::byte> bytes(static_cast<std::size_t>(length));
            file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()), length);
            const auto key = "weapons_new/" + entry.path().parent_path().filename().string() + "/" + entry.path().filename().string();
            if (audio.Prepare(key, bytes, 4) != AudioResult::Applied) { std::fprintf(stderr, "prepare failed: %s\n", key.c_str()); return 6; }
            keys.push_back(key);
        }
    }
    if (audio.Activate() != AudioResult::Applied) return 7;
    for (const auto& key : keys)
        if (audio.Play({key, 0, false, {0, 0, 0}}) != AudioResult::Applied) return 8;
    AudioState state;
    if (audio.Inspect(state) != AudioResult::Applied || state.preparedSamples != keys.size() || state.activeVoices != keys.size()) return 9;
    std::printf("%u private target PCM WAVs prepared, %u voices precreated, %u independent real source buffers submitted, %u retained PCM bytes; volume zero\n",
                state.preparedSamples, state.preparedVoices, state.activeVoices, state.retainedPcmBytes);
    return audio.Shutdown() == AudioResult::Applied ? 0 : 10;
}
