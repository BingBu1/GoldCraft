#pragma once
#include <iosfwd>

struct metahook_api_s;

namespace goldcraft::input_audit {
// Optional, process-local diagnostics. Never changes a binding, cvar or button.
bool install(metahook_api_s* api, bool requested);
void shutdown();
void sample();
int mouse_look_state();
void write_status(std::ostream& out);
}
