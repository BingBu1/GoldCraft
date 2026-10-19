#pragma once
#include <iosfwd>
struct metahook_api_s;
namespace goldcraft::client_userinfo {
bool install(metahook_api_s* api);
void initialize();
void frame();
void reset();
void shutdown();
const char* error();
void diagnostics(std::ostream& out);
}
