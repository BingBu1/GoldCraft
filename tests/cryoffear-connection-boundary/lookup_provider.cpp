// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/include/goldcraft/connection_api.h"
namespace {
uint64_t Generation() { return 17; }
GoldCraftConnectionAPI value{sizeof(GoldCraftConnectionAPI), 1, Generation};
unsigned queries{};
}
extern "C" const GoldCraftConnectionAPI* GoldCraft_GetConnectionAPI() { ++queries; return &value; }
extern "C" void ConfigureConnectionFixture(unsigned mode) {
    value = {sizeof(GoldCraftConnectionAPI), 1, Generation};
    if (mode == 1) value.size = sizeof(GoldCraftConnectionAPI)-1;
    if (mode == 2) value.version = 2;
    if (mode == 3) value.generation = nullptr;
}
extern "C" unsigned ConnectionFixtureQueries() { return queries; }
