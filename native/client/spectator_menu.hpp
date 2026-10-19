#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>

struct metahook_api_s;
namespace goldcraft::spectator_menu {
bool install(metahook_api_s* api);
const char* error();
void shutdown();
void diagnostics(std::ostream& out);
namespace detail {
// Verified CS client CRC64 124d6034a6b28b40: Menu's CUtlLinkedList
// has 12-byte nodes, and mouse mode is zero. Not a cross-build SDK layout.
struct Node { void* item; std::int32_t previous, next; };
struct Menu {
    std::byte before[0x94];
    Node* nodes;
    std::int32_t capacity, grow, head;
    std::byte rest[0x34];
    std::int32_t input_mode;
};
struct Calls {
    void* (*resolve)(Menu*,std::uint32_t);
    void (*arm)(void*);
    void (*open)(void*);
    void (*select)(Menu*,int);
    void (*disarm)(void*);
};
// Return false for an orphaned/replaced item, without dereferencing it.
bool dispatch(Menu* menu,std::uint32_t panel,bool enter,const Calls& calls);
}
}
