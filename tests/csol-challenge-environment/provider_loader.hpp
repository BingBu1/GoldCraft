// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <windows.h>

namespace environment_test
{
inline void LoadProvider()
{
    const auto* path = std::getenv("GOLDCRAFT_TEST_LIGHT_PROVIDER");
    sdk_test::Require(path && *path, "explicit actual engine lightstyle provider supplied");
    const auto module = LoadLibraryA(path);
    sdk_test::Require(module != nullptr, "actual x86 engine lightstyle provider loads");
    using Map = void (*)(const char*);
    const auto map = reinterpret_cast<Map>(GetProcAddress(module, "EnvironmentTestMap"));
    sdk_test::Require(map != nullptr, "actual provider exposes controlled map boundary");
    map("m");
    // Keep the production provider loaded through fixture teardown.
}
}
