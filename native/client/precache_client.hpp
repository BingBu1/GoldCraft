#pragma once
#include <iosfwd>
struct metahook_api_s;
namespace goldcraft::client_precache {
bool install(metahook_api_s* api);
bool install_client();
const char* install_error();
void register_commands();
void create_entities();
void write_status(std::ostream& out);
}
