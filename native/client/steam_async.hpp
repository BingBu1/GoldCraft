#pragma once
#include <iosfwd>
struct metahook_api_s;
namespace goldcraft::steam_async {
// Called by the main thread after client load / once per HUD_Frame / before
// plugin teardown respectively. No Steam IPC occurs in these functions.
void install(metahook_api_s*);
void frame();
void shutdown();
void diagnostics(std::ostream&);
}
