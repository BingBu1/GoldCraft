// SPDX-License-Identifier: GPL-3.0-or-later
#include <Windows.h>
#include "../../native/include/goldcraft/connection_api.h"
#include <cstdio>
#include <initializer_list>
int main(int argc, char** argv) {
    if (argc != 2 || GoldCraftConnectionInterface()) return 1;
    const auto module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) return 2;
    const auto configure = reinterpret_cast<void (*)(unsigned)>(GetProcAddress(module, "ConfigureConnectionFixture"));
    const auto queries = reinterpret_cast<unsigned (*)()>(GetProcAddress(module, "ConnectionFixtureQueries"));
    if (!configure || !queries) return 3;
    for (unsigned mode : {1u, 2u, 3u}) {
        configure(mode);
        if (GoldCraftConnectionInterface()) return 4;
    }
    configure(0);
    const auto* api = GoldCraftConnectionInterface();
    if (!api || api->generation() != 17 || queries() != 4) return 5;
    for (unsigned i = 0; i < 5; ++i)
        if (GoldCraftConnectionInterface() != api || api->generation() != 17) return 6;
    if (queries() != 4) return 7;
    std::printf("actual Windows lookup: missing module, size/version/function rejection, valid cached provider passed\n");
    // Provider pointers are process-lifetime API; do not unload while cached.
    return 0;
}
