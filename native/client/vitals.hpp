#pragma once
#include <iosfwd>
struct metahook_api_s;
namespace goldcraft::client_vitals {
bool install(metahook_api_s* api);
void reset();
void write_status(std::ostream& out);
const char* error();
}
