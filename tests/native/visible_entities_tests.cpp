// Exercise the production patch preflight and consumer binding without a game.
#include "../../native/client/visible_entities.cpp"
#include <cassert>
#include <iostream>
#include <map>

namespace goldcraft::visible_entities {
void fixture_write_status(std::ostream&) {}
void sprite_drawn(const cl_entity_s*) {}
}

namespace {
std::map<std::string, void*> symbols;
unsigned writes = 0, hooks = 0;
uint64_t identity = 0;
int allocation = 0;
int allocated(int requested) { allocation = requested; return 73; }
}

int main() {
    using namespace goldcraft::visible_entities;
    metahook_api_t host{};
    host.GetEngineBase = []() -> void* { return &symbols; };
    host.GetModuleCRC64 = [](void*, uint64_t* result) { *result = identity; return MH_GAMESYMBOL_OK; };
    host.ResolveGameSymbol = [](void*, const char* name, mh_gamesymbol_kind_t, void** result) {
        *result = symbols[name]; return MH_GAMESYMBOL_OK;
    };
    host.GetGameSymbolStatusString = [](mh_gamesymbol_status_t) -> const char* { return "test resolver"; };
    host.WriteDWORD = [](void* destination, DWORD value) { ++writes; std::memcpy(destination, &value, 4); };
    host.InlineHook = [](void*, void*, void** original) -> hook_t* {
        ++hooks; *original = reinterpret_cast<void*>(allocated); return reinterpret_cast<hook_t*>(1);
    };
    std::array<cl_entity_t*, 512> stock{};
    stock[0] = reinterpret_cast<cl_entity_t*>(0x12340000);
    int native_count = 1, transparent_capacity = 0;
    unsigned char allocator[]{0x55, 0x8b, 0xec, 0xc3};
    symbols["GoldCraftVisible_entries"] = stock.data();
    symbols["GoldCraftVisible_count"] = &native_count;
    symbols["GoldCraftVisible_max_transparent"] = &transparent_capacity;
    symbols["GoldCraftVisible_allocate_transparent"] = allocator;
    std::array<DWORD, 7> pointers{};
    std::array<DWORD, 5> bounds{};
    pointers.fill(DWORD(stock.data())); bounds.fill(512);
    for (unsigned i = 0; i < pointers.size(); ++i)
        symbols["GoldCraftVisible_array_" + std::to_string(i)] = &pointers[i];
    for (unsigned i = 0; i < bounds.size(); ++i)
        symbols["GoldCraftVisible_limit_" + std::to_string(i)] = &bounds[i];
    assert(!install(&host) && writes == 0 && hooks == 0);
    identity = 0x6ef7192cd8254e2dULL;
    bounds.back() = 1024;
    assert(!install(&host) && writes == 0 && hooks == 0);
    bounds.back() = 512;
    pointers.back() += 4;
    assert(!install(&host) && writes == 0 && hooks == 0);
    pointers.back() -= 4;
    transparent_capacity = 2048;
    assert(!install(&host) && writes == 0 && hooks == 0);
    transparent_capacity = 0;
    assert(install(&host) && writes == 12 && hooks == 1);
    assert(install(&host) && writes == 12 && hooks == 1); // Idempotent installation.
    const auto shared = GoldCraft_GetVisibleEntitiesAPI();
    assert(shared && shared->capacity == 4096 && shared->count == &native_count);
    assert(shared->entries[0] == stock[0] && shared->entries[4095] == nullptr);
    for (auto pointer : pointers) assert(pointer == DWORD(shared->entries));
    for (auto bound : bounds) assert(bound == 4096);
    cl_entity_t** renderer = stock.data(); int* renderer_count = nullptr;
    cl_entity_t** bullet = stock.data(); int* bullet_count = nullptr;
    shared->bind_consumer(1, &renderer, &renderer_count);
    shared->bind_consumer(2, &bullet, &bullet_count);
    assert(renderer == bullet && renderer == shared->entries && renderer_count == bullet_count);
    assert(consumers == 3 && *renderer_count == 1);
    native_count = 0; // Engine frame/disconnect reset reaches both consumers.
    assert(*renderer_count == 0 && *bullet_count == 0);
    assert(allocate_transparent(512) == 73 && allocation == 4096);
    assert(allocate_transparent(8192) == 73 && allocation == 8192);
    std::cout << "Visible queue: preflight rejection, 12 operands, shared consumers, reset and transparent allocation passed\n";
}
