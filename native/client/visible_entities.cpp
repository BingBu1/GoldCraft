#include <metahook.h>
#include "visible_entities.hpp"
#include "goldcraft/visible_entities_api.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string>

namespace goldcraft::visible_entities {
void fixture_write_status(std::ostream& out);
namespace {
metahook_api_t* api = nullptr;
bool enabled = false;
std::string error;
std::array<cl_entity_t*, capacity> entries{};
int* count = nullptr;
int* max_transparent = nullptr;
int (__cdecl* allocate_transparent_original)(int) = nullptr;
unsigned consumers = 0;

void* resolve(const char* name, mh_gamesymbol_kind_t kind) {
    void* address = nullptr;
    const auto symbol = std::string("GoldCraftVisible_") + name;
    const auto result = api->ResolveGameSymbol(api->GetEngineBase(), symbol.c_str(), kind, &address);
    if (result != MH_GAMESYMBOL_OK || !address)
        throw std::runtime_error(symbol + ": " + api->GetGameSymbolStatusString(result));
    return address;
}

int __cdecl allocate_transparent(int requested) {
    // Use the engine allocator and its normal connection lifetime. There is no
    // second owning pointer or replacement free routine.
    return allocate_transparent_original(std::max(requested, capacity));
}

void bind_consumer(unsigned consumer, cl_entity_t*** table, int** size) {
    if (!enabled || !table || !size || (consumer != 1 && consumer != 2)) return;
    *table = entries.data();
    *size = count;
    consumers |= consumer;
}
}

bool install(metahook_api_t* value) {
    if (enabled) return true;
    api = value;
    uint64_t crc = 0;
    if (api->GetModuleCRC64(api->GetEngineBase(), &crc) != MH_GAMESYMBOL_OK ||
        crc != 0x6ef7192cd8254e2dULL) {
        error = "visible queue requires the verified HL10210 engine";
        return false;
    }
    // Resolve and validate the complete change before writing any operand.
    std::array<void*, 7> arrays{};
    std::array<void*, 5> limits{};
    cl_entity_t** original_entries = nullptr;
    void* allocator = nullptr;
    try {
        original_entries = static_cast<cl_entity_t**>(resolve("entries", MH_GAMESYMBOL_KIND_GLOBAL));
        count = static_cast<int*>(resolve("count", MH_GAMESYMBOL_KIND_GLOBAL));
        max_transparent = static_cast<int*>(resolve("max_transparent", MH_GAMESYMBOL_KIND_GLOBAL));
        allocator = resolve("allocate_transparent", MH_GAMESYMBOL_KIND_FUNCTION);
        const unsigned char prologue[]{0x55, 0x8b, 0xec};
        if (std::memcmp(allocator, prologue, sizeof(prologue)))
            throw std::runtime_error("transparent allocator entry changed");
        if (*count < 0 || *count > 512 || *max_transparent != 0)
            throw std::runtime_error("visible expansion must install before client dynamic allocation");
        for (unsigned i = 0; i < arrays.size(); ++i) {
            arrays[i] = resolve(("array_" + std::to_string(i)).c_str(), MH_GAMESYMBOL_KIND_PATCH);
            DWORD operand;
            std::memcpy(&operand, arrays[i], sizeof(operand));
            if (operand != DWORD(original_entries)) throw std::runtime_error("visible array operand changed");
        }
        for (unsigned i = 0; i < limits.size(); ++i) {
            limits[i] = resolve(("limit_" + std::to_string(i)).c_str(), MH_GAMESYMBOL_KIND_PATCH);
            DWORD operand;
            std::memcpy(&operand, limits[i], sizeof(operand));
            if (operand != 512) throw std::runtime_error("visible limit operand changed");
        }
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
    if (!api->InlineHook(allocator, reinterpret_cast<void*>(allocate_transparent),
                         reinterpret_cast<void**>(&allocate_transparent_original))) {
        error = "transparent allocator hook failed";
        return false;
    }
    std::copy_n(original_entries, *count, entries.data());
    for (auto address : arrays) api->WriteDWORD(address, DWORD(entries.data()));
    for (auto address : limits) api->WriteDWORD(address, capacity);
    enabled = true;
    error.clear();
    return true;
}

const char* install_error() { return error.c_str(); }

void write_status(std::ostream& out) {
    out << "\"visibleEntities\":{\"enabled\":" << enabled
        << ",\"capacity\":" << (enabled ? capacity : 512)
        << ",\"count\":" << (enabled ? *count : 0)
        << ",\"transparentCapacity\":" << (enabled ? *max_transparent : 0)
        << ",\"rendererBound\":" << bool(consumers & 1)
        << ",\"bulletBound\":" << bool(consumers & 2)
        << ",\"error\":" << std::quoted(error) << '}';
    fixture_write_status(out);
}
}

extern "C" const GoldCraftVisibleEntitiesAPI* GoldCraft_GetVisibleEntitiesAPI() {
    using namespace goldcraft::visible_entities;
    static GoldCraftVisibleEntitiesAPI result{};
    if (!enabled) return nullptr;
    result = {sizeof(result), 1, capacity, entries.data(), count, bind_consumer, sprite_drawn};
    return &result;
}
