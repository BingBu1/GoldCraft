#pragma once
struct metahook_api_s;
struct playermove_s;
namespace goldcraft::client_motion {
bool install(metahook_api_s* api);
void shutdown();
bool ready();
const char* error();
}
