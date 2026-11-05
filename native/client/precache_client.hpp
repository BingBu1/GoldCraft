#pragma once
#include <iosfwd>
#include "resource_update.hpp"
struct metahook_api_s;
namespace goldcraft::client_precache {
struct ResourceSyncCallbacks {
    bool (*available)(){};
    // Returns null on success. The receiver copies values before returning;
    // it never borrows engine resource nodes across network or frame calls.
    const char* (*begin)(std::uint64_t, const std::vector<resource_update::Resource>&){};
    void (*cancel)(){};
};
void set_resource_sync(ResourceSyncCallbacks callbacks);
bool resume_resource_sync();
void set_reset_observer(void (*callback)());
bool install(metahook_api_s* api);
bool install_client();
const char* install_error();
void register_commands();
void create_entities();
void write_status(std::ostream& out);
}
