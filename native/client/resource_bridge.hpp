// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
struct metahook_api_s;
struct mh_interface_s;
struct mh_enginesave_s;
struct cl_enginefuncs_s;

namespace goldcraft::resource_bridge {
// Installs the exact filesystem and original-client lifecycle hooks. Capability
// advertisement remains off unless every owner provider is available.
const char* install(metahook_api_s*, mh_interface_s*, mh_enginesave_s*, cl_enginefuncs_s*);
bool paused() noexcept;
void write_status(std::ostream&);
void shutdown();
}
