// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

struct metahook_api_s;
struct cl_enginefuncs_s;

namespace goldcraft::scenario_keys {
void apply_on_join(metahook_api_s* api, const cl_enginefuncs_s& engine, std::uint64_t connection);
}
