#pragma once
#include <iosfwd>
struct metahook_api_s;
namespace goldcraft::packet_client {
bool install(metahook_api_s* api);
bool enabled() noexcept;
const char* install_error();
void reset() noexcept;
void write_status(std::ostream& out);
}
