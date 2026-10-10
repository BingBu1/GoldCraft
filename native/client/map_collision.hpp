#pragma once
#include "goldcraft/map_edits.hpp"
#include <iosfwd>
struct playermove_s;
struct model_s;
namespace goldcraft::client_map {
void reset();
// Prepare from the public loaded world model before committing a received edit.
void prepare(const edits::Replica &replica, model_s *world_model);
void move(playermove_s *movement, int server, void (*original)(playermove_s *, int));
void write_status(std::ostream &out);
} // namespace goldcraft::client_map
