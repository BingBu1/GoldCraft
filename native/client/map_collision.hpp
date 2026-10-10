#pragma once
#include "goldcraft/map_edits.hpp"
#include <iosfwd>
#include <memory>
struct playermove_s;
struct model_s;
struct event_api_s;
namespace goldcraft::client_map {
void reset();
// Prepare from the public loaded world model before committing a received edit.
struct Prepared;
using Candidate = std::shared_ptr<const Prepared>;
Candidate prepare(const edits::Replica &replica, model_s *world_model);
void commit(Candidate candidate) noexcept;
void move(playermove_s *movement, int server, void (*original)(playermove_s *, int));
// Keep the engine's public table untouched. Only the client receives this proxy.
event_api_s *event_api(event_api_s *source);
void movement_initialized(playermove_s *movement) noexcept;
void shutdown_events() noexcept;
void write_status(std::ostream &out);
} // namespace goldcraft::client_map
