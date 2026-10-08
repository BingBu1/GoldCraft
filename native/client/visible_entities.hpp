#pragma once
#include <iosfwd>
struct metahook_api_s;
struct cl_entity_s;
namespace goldcraft::visible_entities {
constexpr int capacity = 4096;
bool install(metahook_api_s* api);
const char* install_error();
void write_status(std::ostream& out);
void fixture_start(int target_count, double seconds);
void fixture_clear();
void create_entities(const float* origin, const float* angles);
void sprite_drawn(const cl_entity_s* entity);
}
