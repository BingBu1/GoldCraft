#include <metahook.h>
#include <com_model.h>
#include <custom.h>
#include <entity_types.h>
#include <cvardef.h>
#include <gl/GL.h>
#include "precache_client.hpp"
#include "goldcraft/precache_protocol.hpp"
#include "goldcraft/precache_api.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <iomanip>
#include <ostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

extern cl_enginefunc_t gEngfuncs;

namespace goldcraft::client_precache {
namespace {
static_assert(sizeof(model_t) == 392);
static_assert(sizeof(resource_t) == 136 && offsetof(resource_t, pNext) == 128);
struct Sound { char name[64]{}; cache_user_t cache{}; int servercount = 0; };
struct CRC { int enabled = 0, checked = 0; unsigned initial = 0; };
struct Model { model_t model{}; CRC crc{}; };
struct Consistency {
    resource_t* resource = nullptr;
    int sound = 0, resource_index = 0;
    unsigned value = 0;
    int check_type = 0;
    float mins[3]{}, maxs[3]{};
};
static_assert(sizeof(Sound) == 72 && sizeof(Consistency) == 44);

metahook_api_t* api = nullptr;
void* engine = nullptr;
bool enabled = false, extended = false, receiving = false;
bool legacy_manifest_received = false;
bool client_media_enabled = false;
unsigned brass_reads = 0, high_brass_reads = 0, last_brass_model = 0;
int (__cdecl* client_read_short)() = nullptr;
int (__cdecl* client_read_long)() = nullptr;
int* client_bad_read = nullptr;
int* client_shadow_index = nullptr;
std::string install_failure;
unsigned manifest_total = 0, manifest_next = 0, manifests = 0, resets = 0;
unsigned high_model_reads = 0, high_sound_reads = 0, high_model_lookups = 0;
unsigned last_model_read = 0, last_sound_read = 0, last_model_lookup = 0;
std::vector<model_t*> models(512);
std::vector<Sound*> sounds(512);
std::vector<resource_t*> resources;
std::vector<Consistency> checks;
std::vector<std::unique_ptr<Model>> extra_models;
std::unordered_map<const model_t*, unsigned> extra_model_indices;
std::vector<std::unique_ptr<Sound>> extra_sounds;
std::unordered_map<std::string, model_t*> models_by_name;
std::unordered_map<std::string, Sound*> sounds_by_name;
std::vector<Model*> reusable_models;
std::vector<Sound*> reusable_sounds;
int sound_cache_epoch = -1;
std::deque<cl_entity_t> static_entities;
struct SoundObservation { unsigned index; bool ambient; std::string name; };
std::deque<SoundObservation> sound_observations;
struct BrassObservation { unsigned index; std::string name; };
std::deque<BrassObservation> brass_observations;
std::unordered_map<const model_t*, std::unique_ptr<std::array<cache_user_t, 32>>> skin_caches;

model_t* original_models = nullptr;
model_t** legacy_model_precache = nullptr;
Sound** legacy_sound_precache = nullptr;
CRC* original_crcs = nullptr;
int* original_model_count = nullptr;
Sound** original_sounds = nullptr;
int* original_sound_count = nullptr;
// HL10210 reads this scalar directly (mov ecx,[cl.servercount]); it is not
// another pointer. Keep the declaration tied to instructions, not IDA types.
int* server_count = nullptr;
int* consistency_count = nullptr;
int* force_consistency = nullptr;
int* read_count = nullptr;
int* bad_read = nullptr;
resource_t* needed = nullptr;
void* net_message = nullptr;

void (__cdecl* start_bits)(void*) = nullptr;
void (__cdecl* end_bits)(void*) = nullptr;
unsigned (__cdecl* read_bits)(int) = nullptr;
int (__cdecl* read_short)() = nullptr;
int (__cdecl* read_long)() = nullptr;
int (__cdecl* read_byte)() = nullptr;
float (__cdecl* read_coord)(void*) = nullptr;
const char* (__cdecl* read_string)() = nullptr;
int (__cdecl* read_data)(void*, int) = nullptr;
void* (__cdecl* zero_alloc)(unsigned) = nullptr;
void (__cdecl* unmunge)(void*, int, int) = nullptr;
void (__cdecl* loading_text)(const char*) = nullptr;
void (__cdecl* start_download)(const char*, int) = nullptr;
void (__cdecl* host_error)(const char*, ...) = nullptr;
void* (__cdecl* cache_check)(cache_user_t*) = nullptr;
void (__cdecl* cache_free)(cache_user_t*) = nullptr;
model_t* (__cdecl* load_model)(model_t*, int, int) = nullptr;
model_t* (__cdecl* find_model_original)(int, const char*) = nullptr;
Sound* (__cdecl* find_sound_original)(const char*, int*) = nullptr;
void (__cdecl* clear_models_original)() = nullptr;
int (__cdecl* clear_client_original)() = nullptr;
void (__cdecl* parse_resources_original)() = nullptr;
void (__cdecl* disconnect_original)() = nullptr;
model_t* (__cdecl* model_for_index_original)(int) = nullptr;
void (__cdecl* need_crc_original)(const char*, int) = nullptr;
int (__cdecl* validate_crc_original)(const char*, unsigned) = nullptr;

void* resolve(const char* name, mh_gamesymbol_kind_t kind) {
    const auto symbol = std::string("GoldCraft_") + name;
    void* address = nullptr;
    const auto status = api->ResolveGameSymbol(engine, symbol.c_str(), kind, &address);
    if (status != MH_GAMESYMBOL_OK)
        throw std::runtime_error(symbol + ": " + api->GetGameSymbolStatusString(status));
    return address;
}
template<class T> void function(T& target, const char* name) {
    target = reinterpret_cast<T>(resolve(name, MH_GAMESYMBOL_KIND_FUNCTION));
}
template<class T> void global(T& target, const char* name) {
    target = reinterpret_cast<T>(resolve(name, MH_GAMESYMBOL_KIND_GLOBAL));
}
template<class T> void hook(const char* name, T replacement, T& original) {
    if (!api->InlineHook(resolve(name, MH_GAMESYMBOL_KIND_FUNCTION),
        reinterpret_cast<void*>(replacement), reinterpret_cast<void**>(&original)))
        api->SysError("GoldCraft: failed to install %s", name);
}

enum class Table { models, sounds };
struct TablePatch { const char* name; Table table; unsigned offset; };
// This list only includes reviewed pointer operands. The adjacent event-table
// boundary and model-count global must remain at their original addresses.
#include "precache_table_patches.inc"
struct BoundPatch { void* address; Table table; unsigned offset; };
std::vector<BoundPatch> table_patches;
void* sound_end_patch = nullptr;
void* consistency_table_patch = nullptr;
void* consistency_bits_patch = nullptr;
std::array<void*, 5> model_limits{};
std::vector<std::pair<void*, void*>> wire_calls;
void* static_parser_slot = nullptr;
DWORD legacy_sound_end = 0, legacy_consistency_table = 0;
byte legacy_consistency_bits = 0;
struct ConnectionPatch {
    void* address;
    std::vector<byte> original, replacement;
};
std::vector<ConnectionPatch> connection_patches;

// Resolve/verify at plugin load, but leave stock instructions in place until
// this connection receives a matching GoldCraft manifest. Restore exact bytes
// at teardown; another server must never inherit a previous server's protocol.
void prepare_patch(void* address, const void* replacement, unsigned size) {
    const auto* before = static_cast<const byte*>(address);
    const auto* after = static_cast<const byte*>(replacement);
    connection_patches.push_back({address, {before, before + size}, {after, after + size}});
    if (extended) api->WriteMemory(address, connection_patches.back().replacement.data(), size);
}

void update_tables() {
    if (!extended) return;
    for (const auto& patch : table_patches) {
        const auto base = patch.table == Table::models ? static_cast<void*>(models.data()) : sounds.data();
        api->WriteDWORD(patch.address, DWORD(base) + patch.offset);
    }
    api->WriteDWORD(sound_end_patch, DWORD(sounds.data() + sounds.size()));
    for (auto patch : model_limits) api->WriteDWORD(patch, DWORD(models.size()));
}

void set_extension(bool active) {
    if (extended == active) return;
    extended = active;
    for (auto& patch : connection_patches) {
        auto& bytes = active ? patch.replacement : patch.original;
        api->WriteMemory(patch.address, bytes.data(), DWORD(bytes.size()));
    }
    if (active) {
        update_tables();
    } else {
        for (const auto& patch : table_patches) {
            const auto base = patch.table == Table::models ? static_cast<void*>(legacy_model_precache) : legacy_sound_precache;
            api->WriteDWORD(patch.address, DWORD(base) + patch.offset);
        }
        api->WriteDWORD(sound_end_patch, legacy_sound_end);
        for (auto patch : model_limits) api->WriteDWORD(patch, 512);
        api->WriteDWORD(consistency_table_patch, legacy_consistency_table);
        api->WriteMemory(consistency_bits_patch, &legacy_consistency_bits, 1);
    }
}

[[noreturn]] void malformed(const char* reason) {
    host_error("GoldCraft precache: %s", reason);
    // Host_Error longjmps out of the current frame. Never continue a parser
    // after a malformed resource even in a host with a different error policy.
    std::terminate();
}

int model_count() { return *original_model_count + (extended ? int(extra_models.size()) : 0); }
int model_index(const model_t* model) {
    const auto address = reinterpret_cast<uintptr_t>(model);
    const auto first = reinterpret_cast<uintptr_t>(original_models);
    if (address >= first && address < first + sizeof(model_t) * *original_model_count &&
        (address - first) % sizeof(model_t) == 0) return int((address - first) / sizeof(model_t));
    const auto found = extra_model_indices.find(model);
    return found == extra_model_indices.end() ? -1 : 1024 + int(found->second);
}
model_t* model_at(int index) {
    if (index >= 0 && index < *original_model_count) return original_models + index;
    return index >= 1024 && size_t(index - 1024) < extra_models.size() ?
        &extra_models[index - 1024]->model : nullptr;
}
int model_capacity() { return extended ? int(models.size()) : 512; }

CRC* model_crc(model_t* model) {
    const int index = model_index(model);
    if (index < 0) malformed("unregistered model cache object");
    return index < 1024 ? original_crcs + index : &extra_models[index - 1024]->crc;
}

model_t* __cdecl find_model(int crc, const char* name) {
    if (!extended) return find_model_original(crc, name);
    if (!name || !*name || std::strlen(name) >= sizeof(model_t::name))
        malformed("invalid model name");
    std::string key(name);
    for (auto& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
    const auto known = models_by_name.find(key);
    if (known != models_by_name.end()) return known->second;
    bool reusable = false;
    for (int i = 0; i < *original_model_count; ++i) {
        if (!_stricmp(original_models[i].name, name)) return models_by_name[key] = original_models + i;
        reusable |= original_models[i].needload == 2;
    }
    if (*original_model_count < 1024 || reusable) {
        // Remove the old key if Mod_FindName reuses an original cache object.
        auto* result = find_model_original(crc, name);
        for (auto it = models_by_name.begin(); it != models_by_name.end(); )
            if (it->second == result) it = models_by_name.erase(it); else ++it;
        return models_by_name[key] = result;
    }

    // Cache owns a pointer to cache_user_t, so moving vector<Model> is invalid.
    // The vector owns separately allocated, stable objects instead.
    Model* entry = nullptr;
    while (!reusable_models.empty() && !entry) {
        auto* candidate = reusable_models.back(); reusable_models.pop_back();
        if (candidate->model.needload == 2) entry = candidate;
    }
    if (!entry) {
        auto value = std::make_unique<Model>();
        entry = value.get();
        extra_model_indices.emplace(&entry->model, unsigned(extra_models.size()));
        extra_models.push_back(std::move(value));
    }
    if (entry->model.cache.data && (entry->model.type == mod_studio || entry->model.type == mod_alias))
        cache_free(&entry->model.cache);
    std::string old_key(entry->model.name);
    for (auto& c : old_key) c = char(std::tolower(static_cast<unsigned char>(c)));
    models_by_name.erase(old_key);
    *entry = Model{};
    entry->crc.enabled = crc;
    std::strcpy(entry->model.name, name);
    entry->model.needload = 1;
    return models_by_name[key] = &entry->model;
}

void __cdecl need_crc(const char* name, int required) {
    if (!extended) { need_crc_original(name, required); return; }
    model_crc(find_model(0, name))->enabled = required;
}
int __cdecl validate_crc(const char* name, unsigned value) {
    if (!extended) return validate_crc_original(name, value);
    auto* crc = model_crc(find_model(1, name));
    if (crc->checked) return crc->initial == value;
    crc->checked = 1; crc->initial = value;
    return 1;
}

unsigned __cdecl crc_operand(model_t* model) {
    // Mod_LoadModel's native CRC and altered-model checks stay intact. Its
    // indexed operand now names the stable record associated with this model.
    return (DWORD(model_crc(model)) - DWORD(original_crcs)) / sizeof(unsigned);
}
void* crc_resume = nullptr;
__declspec(naked) void crc_thunk() {
    __asm {
        push ecx
        push edx
        push edi
        call crc_operand
        add esp, 4
        pop edx
        pop ecx
        jmp dword ptr [crc_resume]
    }
}

cache_user_t* __cdecl skin_cache(model_t* model, unsigned skin) {
    if (model_index(model) < 0 || skin >= 32) malformed("invalid Studio skin cache index");
    auto& entry = skin_caches[model];
    if (!entry) entry = std::make_unique<std::array<cache_user_t, 32>>();
    return &(*entry)[skin];
}
void* skin_resume = nullptr;
__declspec(naked) void skin_thunk() {
    __asm {
        push ecx
        push edx
        push ebx
        push dword ptr [ebp-13Ch]
        call skin_cache
        add esp, 8
        mov esi, eax
        pop edx
        pop ecx
        jmp dword ptr [skin_resume]
    }
}

void __cdecl clear_models() {
    models_by_name.clear();
    reusable_models.clear();
    for (auto& [model, skins] : skin_caches)
        for (auto& cache : *skins) if (cache.data) cache_free(&cache);
    skin_caches.clear();
    for (auto& entry : extra_models) {
        auto& model = entry->model;
        if (model.type == mod_alias || model.needload == 3) continue;
        if (model.needload != 2) {
            for (int i = 0; i < model.numsurfaces; ++i) {
                // HL25 adds16 bytes to msurface_t; the SDK's92-byte stride is
                // not this engine ABI. Exact10210 disassembly verified both.
                auto* list = reinterpret_cast<GLuint*>(reinterpret_cast<byte*>(model.surfaces) + i * 108 + 92);
                if (*list) { glDeleteLists(*list, 1); *list = 0; }
            }
            model.needload = 2;
        }
        if (model.type == mod_sprite) model.cache.data = nullptr;
        if (model.needload == 2 && model.type != mod_studio && model.type != mod_alias)
            reusable_models.push_back(entry.get());
        std::string key(model.name);
        for (auto& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
        models_by_name.emplace(std::move(key), &model);
    }
    for (const auto& entry : extra_models) {
        std::string key(entry->model.name);
        for (auto& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
        models_by_name[key] = &entry->model;
    }
    clear_models_original();
}

Sound* __cdecl find_sound(const char* name, int* cached) {
    if (!extended) return find_sound_original(name, cached);
    if (!name || std::strlen(name) >= 64) malformed("invalid sound name");
    if (sound_cache_epoch != *server_count) {
        sound_cache_epoch = *server_count;
        sounds_by_name.clear(); reusable_sounds.clear();
        for (int i = 0; i < *original_sound_count; ++i)
            sounds_by_name[(*original_sounds)[i].name] = *original_sounds + i;
        for (const auto& entry : extra_sounds) {
            sounds_by_name[entry->name] = entry.get();
            if (entry->servercount > 0 && entry->servercount != sound_cache_epoch)
                reusable_sounds.push_back(entry.get());
        }
    }
    const auto known = sounds_by_name.find(name);
    if (known != sounds_by_name.end()) {
        auto* sound = known->second;
        if (cached) *cached = cache_check(&sound->cache) != nullptr;
        if (sound->servercount > 0) sound->servercount = *server_count;
        return sound;
    }
    bool reusable = false;
    for (int i = 0; i < *original_sound_count; ++i) {
        const auto& sound = (*original_sounds)[i];
        if (!std::strcmp(sound.name, name)) return find_sound_original(name, cached);
        reusable |= sound.servercount > 0 && sound.servercount != *server_count;
    }
    Sound* spare = nullptr;
    while (!reusable_sounds.empty() && !spare) {
        auto* candidate = reusable_sounds.back(); reusable_sounds.pop_back();
        if (candidate->servercount > 0 && candidate->servercount != *server_count) spare = candidate;
    }
    if (*original_sound_count < 1024 || reusable) {
        auto* result = find_sound_original(name, cached);
        for (auto it = sounds_by_name.begin(); it != sounds_by_name.end(); )
            if (it->second == result) it = sounds_by_name.erase(it); else ++it;
        return sounds_by_name[name] = result;
    }
    if (!spare) {
        auto value = std::make_unique<Sound>(); spare = value.get();
        extra_sounds.push_back(std::move(value));
    }
    if (cache_check(&spare->cache)) cache_free(&spare->cache);
    sounds_by_name.erase(spare->name);
    *spare = Sound{};
    std::strcpy(spare->name, name);
    spare->servercount = *server_count;
    if (cached) *cached = 0;
    return sounds_by_name[name] = spare;
}

model_t* __cdecl model_for_index(int index) {
    if (!extended) return model_for_index_original(index);
    if (index < 0 || size_t(index) >= models.size()) return nullptr;
    if (unsigned(index) >= precache::media_escape) { ++high_model_lookups; last_model_lookup = unsigned(index); }
    return model_for_index_original(index);
}

unsigned read_media_short() {
    std::uint32_t index = 0;
    if (!precache::read_media_index(read_short, read_long, [] { return *bad_read != 0; }, extended, index))
        malformed("invalid or truncated 32-bit media index");
    return index;
}

int __cdecl read_brass_model() {
    if (!extended) return client_read_short();
    std::uint32_t index = 0;
    if (!precache::read_media_index(client_read_short, client_read_long,
        [] { return *client_bad_read != 0; }, extended, index) || index >= models.size())
        malformed("invalid Brass model index");
    ++brass_reads;
    if (index >= precache::media_escape) ++high_brass_reads;
    last_brass_model = index;
    brass_observations.push_back({index, models[index] ? models[index]->name : ""});
    if (brass_observations.size() > 32) brass_observations.pop_front();
    return int(index);
}

int __cdecl read_model_index() {
    const auto index = read_media_short();
    if (index >= models.size()) malformed("model index outside received manifest");
    last_model_read = index;
    if (index >= precache::media_escape) ++high_model_reads;
    return int(index);
}

unsigned __cdecl read_sound_index(int bits, unsigned flags) {
    unsigned index = read_bits(bits);
    if (extended && bits == 16 && index == precache::media_escape) index = read_bits(32);
    if (*bad_read || index > precache::max_media_index || (!(flags & 16) && index >= sounds.size()))
        malformed("sound index outside received manifest");
    last_sound_read = index;
    if (index >= precache::media_escape) ++high_sound_reads;
    if (!(flags & 16) && index >= precache::media_escape) {
        sound_observations.push_back({index, false, sounds[index] ? sounds[index]->name : ""});
        if (sound_observations.size() > 16) sound_observations.pop_front();
    }
    return index;
}

__declspec(naked) void sound_index_thunk() {
    __asm {
        push ebx // CL_Parse_Sound's flags, verified for10210
        push dword ptr [esp+8]
        call read_sound_index
        add esp, 8
        ret
    }
}

int __cdecl read_static_sound_index() {
    const unsigned index = read_media_short();
    // Flags follow volume, attenuation, entity(short) and pitch. Peek only after
    // verifying the remaining byte count; entity IDs stay16-bit.
    struct Message { const char* name; unsigned flags; byte* data; int maximum, size; };
    const auto* message = static_cast<const Message*>(net_message);
    if (*read_count < 0 || message->size - *read_count < 6) malformed("truncated static sound");
    if (!(message->data[*read_count + 5] & 16) && index >= sounds.size())
        malformed("static sound index outside received manifest");
    last_sound_read = index;
    if (index >= precache::media_escape) ++high_sound_reads;
    if (!(message->data[*read_count + 5] & 16) && index >= precache::media_escape) {
        sound_observations.push_back({index, true, sounds[index] ? sounds[index]->name : ""});
        if (sound_observations.size() > 16) sound_observations.pop_front();
    }
    return int(index);
}

void __cdecl parse_static_entity() {
    cl_entity_t entity{};
    auto& state = entity.curstate;
    state.modelindex = read_model_index();
    state.sequence = read_byte(); state.frame = float(read_byte());
    state.colormap = static_cast<unsigned short>(read_short()); state.skin = read_byte();
    for (int i = 0; i < 3; ++i) {
        entity.origin[i] = state.origin[i] = read_coord(net_message);
        entity.angles[i] = state.angles[i] = float(read_byte()) * (360.f / 256.f);
    }
    state.rendermode = read_byte();
    if (state.rendermode) {
        state.renderamt = read_byte();
        state.rendercolor.r = byte(read_byte()); state.rendercolor.g = byte(read_byte());
        state.rendercolor.b = byte(read_byte()); state.renderfx = read_byte();
    }
    if (*bad_read) malformed("truncated static entity");
    entity.model = model_for_index(state.modelindex);
    if (!entity.model) malformed("static entity model not loaded");
    entity.baseline = entity.prevstate = entity.curstate;
    static_entities.push_back(entity);
}

void reset_client() {
    set_extension(false);
    resources.clear(); checks.clear();
    static_entities.clear();
    sound_observations.clear();
    brass_observations.clear();
    brass_reads = high_brass_reads = last_brass_model = 0;
    high_model_reads = high_sound_reads = high_model_lookups = 0;
    last_model_read = last_sound_read = last_model_lookup = 0;
    manifest_next = manifest_total = 0; receiving = legacy_manifest_received = false;
    models.assign(512, nullptr);
    sounds.assign(512, nullptr);
    *consistency_count = *force_consistency = 0;
    ++resets;
}
int __cdecl clear_client() {
    // The engine must release the old connection while its tables are valid.
    const int result = clear_client_original();
    reset_client();
    return result;
}
void __cdecl disconnect() {
    disconnect_original();
    reset_client();
}

void parse_consistency() {
    *force_consistency = read_bits(1) != 0;
    checks.clear();
    unsigned previous = 0, cleared = 0;
    if (*force_consistency) while (read_bits(1)) {
        const auto index = read_bits(1) ? previous + read_bits(5) : read_bits(extended ? 32 : 10);
        if (*bad_read || index < previous || index >= resources.size()) malformed("invalid consistency resource index");
        for (; cleared < index; ++cleared)
            if ((checks.empty() || checks.back().resource != resources[cleared]) &&
                std::strstr(resources[cleared]->szFileName, "models/")) need_crc(resources[cleared]->szFileName, 0);
        auto* resource = resources[index];
        if (std::strstr(resource->szFileName, "models/")) need_crc(resource->szFileName, 1);
        Consistency record;
        record.resource = resource;
        record.sound = resource->type == t_sound;
        record.resource_index = int(index);
        const std::array<byte, 32> zero{};
        if (resource->type == t_model && std::memcmp(resource->rguc_reserved, zero.data(), zero.size())) {
            unmunge(resource->rguc_reserved, 32, *server_count);
            record.check_type = resource->rguc_reserved[0];
            if (record.check_type > 3) malformed("unknown consistency type");
        }
        checks.push_back(record);
        previous = index;
    }
    if (*bad_read) malformed("truncated consistency list");
    // The sender iterates records without moving them; its original digest,
    // Studio bounds and altered-model validation paths are retained.
    *consistency_count = int(checks.size());
    api->WriteDWORD(consistency_table_patch, DWORD(checks.data()) + offsetof(Consistency, sound));
    byte bits = extended ? 32 : 12;
    api->WriteMemory(consistency_bits_patch, &bits, 1);
}

void __cdecl parse_resources() {
    const int saved_read = *read_count;
    const int saved_bad_read = *bad_read;
    start_bits(net_message);
    unsigned count = read_bits(12);
    unsigned total = count, first = 0;
    bool is_extended = false;
    if (count == precache::marker) {
        is_extended = read_bits(32) == precache::magic;
        if (is_extended) {
            if (legacy_manifest_received) malformed("mixed resource formats");
            if (read_bits(8) != precache::version) malformed("unsupported manifest version");
            if (!client_media_enabled) malformed("extended manifest requires the matched client media parser");
            total = read_bits(32); first = read_bits(32); count = read_bits(16);
            precache::ManifestChunk chunk{total, first, count};
            if (!chunk.valid(manifest_next) || (receiving && total != manifest_total))
                malformed("out-of-order resource manifest");
        }
    }
    if (!is_extended) {
        if (extended || receiving) malformed("mixed resource formats");
        // Including the valid legacy count4095: replay from the exact original
        // position using the engine's resource, download and consistency code.
        end_bits(net_message);
        *read_count = saved_read; *bad_read = saved_bad_read;
        parse_resources_original();
        manifest_total = count;
        legacy_manifest_received = true;
        ++manifests;
        return;
    }
    if (!first) {
        if (!resources.empty()) malformed("duplicate resource manifest");
        set_extension(true); manifest_total = total; receiving = true;
    } else if (!extended || !is_extended) malformed("mixed resource formats");

    size_t model_size = models.size(), sound_size = sounds.size();
    for (unsigned i = 0; i < count; ++i) {
        resource_t record{};
        record.type = static_cast<resourcetype_t>(read_bits(4));
        const char* name = read_string();
        if (!name || !*name || std::strlen(name) >= sizeof(record.szFileName)) malformed("invalid resource name");
        std::strcpy(record.szFileName, name);
        const auto index = read_bits(extended ? 32 : 12);
        if (index > 0x7fffffff || record.type > t_eventscript) malformed("invalid resource type or index");
        if ((record.type == t_model || record.type == t_sound) && index > precache::max_media_index)
            malformed("media index exceeds32-bit engine API range");
        if (record.type == t_eventscript && index >= 256) malformed("event index exceeds engine range");
        if (record.type == t_decal && index >= 512) malformed("decal index exceeds engine range");
        record.nIndex = int(index);
        record.nDownloadSize = int(read_bits(24));
        record.ucFlags = static_cast<byte>(read_bits(3) & ~RES_WASMISSING);
        if (record.ucFlags & RES_CUSTOM) read_data(record.rgucMD5_hash, 16);
        if (read_bits(1)) read_data(record.rguc_reserved, 32);
        if (*bad_read) malformed("truncated resource entry");
        if (record.type == t_model) model_size = std::max(model_size, size_t(index) + 1);
        if (record.type == t_sound) sound_size = std::max(sound_size, size_t(index) + 1);
        auto* resource = static_cast<resource_t*>(zero_alloc(sizeof(resource_t)));
        if (!resource) malformed("resource allocation failed");
        *resource = record;
        // Allocate with the engine's allocator: its normal downloader and list
        // cleanup own these nodes, including failed connection teardown.
        if (!needed->pPrev || !needed->pNext) malformed("uninitialized resource list");
        resource->pPrev = needed->pPrev;
        resource->pNext = needed;
        needed->pPrev->pNext = resource;
        needed->pPrev = resource;
        resources.push_back(resource);
    }
    if (model_size != models.size() || sound_size != sounds.size()) {
        const char* allocation_error = nullptr;
        try { models.resize(model_size); sounds.resize(sound_size); }
        catch (const std::bad_alloc&) { allocation_error = "insufficient memory for media tables"; }
        catch (const std::length_error&) { allocation_error = "media tables exceed process address space"; }
        // The first resize can succeed before the second fails. Restore every
        // engine operand even on that path before Host_Error clears the client.
        update_tables();
        if (allocation_error) malformed(allocation_error);
    }
    manifest_next = first + count;
    if (manifest_next == total) parse_consistency();
    end_bits(net_message);
    if (*bad_read) malformed("truncated resource manifest");
    if (manifest_next != total) return;
    receiving = false; ++manifests;
    loading_text("#GameUI_VerifyingResources");
    start_download("Verifying and downloading resources...\n", 0);
}

void stats() {
    gEngfuncs.Con_Printf("GoldCraft precache v%u: extended=%d receiving=%d resources=%u/%u models=%u sounds=%u cached_models=%d cached_sounds=%u consistency=%u manifests=%u resets=%u\n",
        precache::version, extended, receiving, unsigned(resources.size()), manifest_total, unsigned(models.size()), unsigned(sounds.size()),
        model_count(), unsigned(*original_sound_count + extra_sounds.size()), unsigned(checks.size()), manifests, resets);
}

void jump_patch(const char* name, void* target, unsigned size, void*& resume) {
    auto* address = static_cast<byte*>(resolve(name, MH_GAMESYMBOL_KIND_PATCH));
    resume = address + size;
    std::vector<byte> code(size, 0x90);
    code[0] = 0xe9;
    const auto relative = DWORD(target) - DWORD(address) - 5;
    std::memcpy(code.data() + 1, &relative, 4);
    prepare_patch(address, code.data(), unsigned(code.size()));
}
}

bool install(metahook_api_t* value) {
    if (enabled) return true;
    api = value; engine = api->GetEngineBase();
    uint64_t crc = 0;
    const auto identity_status = api->GetModuleCRC64(engine, &crc);
    if (identity_status != MH_GAMESYMBOL_OK || crc != 0x6ef7192cd8254e2dULL) {
        install_failure = std::string("engine identity: ") + api->GetGameSymbolStatusString(identity_status) +
            ", CRC64=" + std::to_string(crc);
        return false;
    }
    // Resolve everything before touching the engine. The catalog build also
    // checks the exact SHA256 and original operand bytes against the copy.
    try {
        global(original_models, "mod_known"); global(original_crcs, "mod_crc");
        global(original_model_count, "mod_numknown"); global(original_sounds, "known_sfx");
        global(original_sound_count, "num_sfx"); global(server_count, "servercount");
        global(consistency_count, "consistency_count"); global(force_consistency, "force_consistency");
        global(read_count, "msg_readcount"); global(bad_read, "msg_badread");
        global(needed, "resourcesneeded"); global(net_message, "net_message");
        function(start_bits, "MSG_StartBitReading"); function(end_bits, "MSG_EndBitReading");
        function(read_bits, "MSG_ReadBits"); function(read_string, "MSG_ReadBitString");
        function(read_short, "MSG_ReadShort"); function(read_long, "MSG_ReadLong");
        function(read_byte, "MSG_ReadByte"); function(read_coord, "MSG_ReadCoord");
        function(read_data, "MSG_ReadBitData"); function(zero_alloc, "Mem_ZeroMalloc");
        function(unmunge, "COM_UnMunge"); function(loading_text, "LoadingText");
        function(start_download, "CL_StartResourceDownloading"); function(host_error, "Host_Error");
        function(cache_check, "Cache_Check"); function(cache_free, "Cache_Free");
        function(load_model, "Mod_LoadModel");
        const auto old_models = resolve("model_precache", MH_GAMESYMBOL_KIND_GLOBAL);
        const auto old_sounds = resolve("sound_precache", MH_GAMESYMBOL_KIND_GLOBAL);
        legacy_model_precache = static_cast<model_t**>(old_models);
        legacy_sound_precache = static_cast<Sound**>(old_sounds);
        for (const auto& patch : patches) {
            auto* address = resolve(patch.name, MH_GAMESYMBOL_KIND_PATCH);
            const DWORD expected = DWORD(patch.table == Table::models ? old_models : old_sounds) + patch.offset;
            if (*static_cast<DWORD*>(address) != expected) throw std::runtime_error(std::string("changed operand: ") + patch.name);
            table_patches.push_back({address, patch.table, patch.offset});
        }
        sound_end_patch = resolve("sound_end", MH_GAMESYMBOL_KIND_PATCH);
        consistency_table_patch = resolve("consistency_send_table", MH_GAMESYMBOL_KIND_PATCH);
        consistency_bits_patch = resolve("consistency_send_bits", MH_GAMESYMBOL_KIND_PATCH);
        legacy_sound_end = *static_cast<DWORD*>(sound_end_patch);
        legacy_consistency_table = *static_cast<DWORD*>(consistency_table_patch);
        legacy_consistency_bits = *static_cast<byte*>(consistency_bits_patch);
        for (unsigned i = 0; i < model_limits.size(); ++i) {
            const auto name = "model_limit_" + std::to_string(i);
            model_limits[i] = resolve(name.c_str(), MH_GAMESYMBOL_KIND_PATCH);
            if (*static_cast<DWORD*>(model_limits[i]) != 512) throw std::runtime_error("changed model limit operand");
        }
        for (const auto name : {"CL_ModelForIndex", "CL_ParseResourceList", "CL_ClearState", "CL_Disconnect",
            "Mod_FindName", "Mod_ClearAll", "Mod_NeedCRC", "Mod_ValidateCRC", "S_FindName"})
            resolve(name, MH_GAMESYMBOL_KIND_FUNCTION);
        resolve("model_crc_operand", MH_GAMESYMBOL_KIND_PATCH);
        resolve("studio_skin_operand", MH_GAMESYMBOL_KIND_PATCH);
        // Each entry is a reviewed CALL of a media reader, never an entity ID,
        // decal ID, coordinate or duration. Validate all before patching any.
#define GC_MODEL_READ(name) {name, reinterpret_cast<void*>(read_model_index), reinterpret_cast<void*>(read_short)},
#define GC_SOUND_READ(name) {name, reinterpret_cast<void*>(sound_index_thunk), reinterpret_cast<void*>(read_bits)},
#define GC_STATIC_SOUND_READ(name) {name, reinterpret_cast<void*>(read_static_sound_index), reinterpret_cast<void*>(read_short)},
        struct WireCall { const char* name; void* replacement; void* original; };
        const WireCall calls[] = {
#include "precache_wire_patches.inc"
        };
#undef GC_MODEL_READ
#undef GC_SOUND_READ
#undef GC_STATIC_SOUND_READ
        for (const auto& call : calls) {
            auto* address = static_cast<byte*>(resolve(call.name, MH_GAMESYMBOL_KIND_PATCH));
            if (*address != 0xe8 || address + 5 + *reinterpret_cast<int*>(address + 1) != call.original)
                throw std::runtime_error(std::string("changed media reader: ") + call.name);
            wire_calls.emplace_back(address, call.replacement);
        }
        static_parser_slot = resolve("static_parser_slot", MH_GAMESYMBOL_KIND_PATCH);
        if (*static_cast<void**>(static_parser_slot) != resolve("CL_ParseStatic", MH_GAMESYMBOL_KIND_FUNCTION))
            throw std::runtime_error("changed static entity parser");
        std::memcpy(models.data(), old_models, models.size() * sizeof(model_t*));
        std::memcpy(sounds.data(), old_sounds, sounds.size() * sizeof(Sound*));
    } catch (const std::exception& error) {
        table_patches.clear();
        wire_calls.clear();
        install_failure = error.what();
        OutputDebugStringA(error.what());
        return false;
    }
    for (const auto& call : wire_calls) {
        const DWORD relative = DWORD(call.second) - DWORD(call.first) - 5;
        prepare_patch(static_cast<byte*>(call.first) + 1, &relative, sizeof(relative));
    }
    const DWORD static_parser = DWORD(parse_static_entity);
    prepare_patch(static_parser_slot, &static_parser, sizeof(static_parser));
    jump_patch("model_crc_operand", crc_thunk, 28, crc_resume);
    jump_patch("studio_skin_operand", skin_thunk, 52, skin_resume);
    hook("Mod_FindName", find_model, find_model_original);
    hook("Mod_ClearAll", clear_models, clear_models_original);
    hook("S_FindName", find_sound, find_sound_original);
    hook("CL_ClearState", clear_client, clear_client_original);
    hook("CL_Disconnect", disconnect, disconnect_original);
    hook("CL_ModelForIndex", model_for_index, model_for_index_original);
    hook("CL_ParseResourceList", parse_resources, parse_resources_original);
    hook("Mod_NeedCRC", need_crc, need_crc_original);
    hook("Mod_ValidateCRC", validate_crc, validate_crc_original);
    enabled = true;
    install_failure.clear();
    return true;
}

const char* install_error() { return install_failure.c_str(); }

bool install_client() {
    if (client_media_enabled) return true;
    if (!enabled) return false;
    auto* client = api->GetClientBase();
    uint64_t crc = 0;
    if (!client || api->GetModuleCRC64(client, &crc) != MH_GAMESYMBOL_OK || crc != 0x124d6034a6b28b40ULL) {
        install_failure = "unsupported client identity for 32-bit Brass";
        return false;
    }
    try {
        auto symbol = [&](const char* name, mh_gamesymbol_kind_t kind) {
            void* address = nullptr;
            const auto status = api->ResolveGameSymbol(client, name, kind, &address);
            if (status != MH_GAMESYMBOL_OK)
                throw std::runtime_error(std::string(name) + ": " + api->GetGameSymbolStatusString(status));
            return address;
        };
        client_read_short = reinterpret_cast<decltype(client_read_short)>(symbol("GoldCraft_CS_ReadShort", MH_GAMESYMBOL_KIND_FUNCTION));
        client_read_long = reinterpret_cast<decltype(client_read_long)>(symbol("GoldCraft_CS_ReadLong", MH_GAMESYMBOL_KIND_FUNCTION));
        client_bad_read = static_cast<int*>(symbol("GoldCraft_CS_BadRead", MH_GAMESYMBOL_KIND_GLOBAL));
        client_shadow_index = static_cast<int*>(symbol("GoldCraft_CS_ShadowSprite", MH_GAMESYMBOL_KIND_GLOBAL));
        auto* call = static_cast<byte*>(symbol("GoldCraft_CS_BrassModelRead", MH_GAMESYMBOL_KIND_PATCH));
        if (*call != 0xe8 || call + 5 + *reinterpret_cast<int*>(call + 1) != reinterpret_cast<void*>(client_read_short))
            throw std::runtime_error("changed Brass model reader CALL");
        const DWORD relative = DWORD(read_brass_model) - DWORD(call) - 5;
        prepare_patch(call + 1, &relative, sizeof(relative));
        client_media_enabled = true;
        return true;
    } catch (const std::exception& error) {
        install_failure = error.what();
        return false;
    }
}

void register_commands() {
    if (!enabled) return;
    gEngfuncs.pfnRegisterVariable(precache::capability, client_media_enabled ? precache::capability_value : "0", FCVAR_USERINFO);
    // Registration only stores the cvar. A real set populates GoldSrc's
    // connection userinfo, including when the value equals its default.
    gEngfuncs.Cvar_SetValue(precache::capability, client_media_enabled ? float(precache::version) : 0.f);
    gEngfuncs.pfnAddCommand("gc_precache_stats", stats);
}

void create_entities() {
    if (enabled) for (auto& entity : static_entities) gEngfuncs.CL_CreateVisibleEntity(ET_NORMAL, &entity);
}

void write_status(std::ostream& out) {
    const auto shadow_index = client_shadow_index ? *client_shadow_index : 0;
    const auto* shadow = enabled && shadow_index >= 0 && shadow_index < model_capacity() ?
        (extended ? models[shadow_index] : legacy_model_precache[shadow_index]) : nullptr;
    out << "\"precache\":{\"version\":" << precache::version << ",\"enabled\":" << enabled
        << ",\"clientMedia\":" << client_media_enabled << ",\"brassReads\":" << brass_reads
        << ",\"highBrassReads\":" << high_brass_reads << ",\"lastBrassModel\":" << last_brass_model
        << ",\"lastBrassName\":" << std::quoted(last_brass_model < models.size() && models[last_brass_model] ? models[last_brass_model]->name : "")
        << ",\"error\":" << std::quoted(install_failure) << ",\"extended\":" << extended
        << ",\"storage\":" << std::quoted(extended ? "goldcraft" : "native")
        << ",\"shadowIndex\":" << shadow_index << ",\"shadowName\":" << std::quoted(shadow ? shadow->name : "")
        << ",\"receiving\":" << receiving << ",\"resources\":" << (extended ? resources.size() : manifest_total)
        << ",\"total\":" << manifest_total << ",\"models\":" << model_capacity() << ",\"sounds\":" << (extended ? sounds.size() : 512)
        << ",\"cachedModels\":" << (enabled ? model_count() : 0)
        << ",\"cachedSounds\":" << (enabled ? *original_sound_count + (extended ? extra_sounds.size() : 0) : 0)
        << ",\"checks\":" << (enabled ? *consistency_count : 0) << ",\"manifests\":" << manifests << ",\"resets\":" << resets
        << ",\"highModelReads\":" << high_model_reads << ",\"highSoundReads\":" << high_sound_reads
        << ",\"highModelLookups\":" << high_model_lookups << ",\"lastModelRead\":" << last_model_read
        << ",\"lastSoundRead\":" << last_sound_read << ",\"lastModelLookup\":" << last_model_lookup
        << ",\"staticEntities\":" << static_entities.size() << ",\"soundObservations\":[";
    unsigned count = 0;
    for (const auto& sound : sound_observations) {
        if (count++) out << ',';
        out << "{\"index\":" << sound.index << ",\"ambient\":" << sound.ambient
            << ",\"name\":" << std::quoted(sound.name) << '}';
    }
    const auto* view = gEngfuncs.GetViewModel ? gEngfuncs.GetViewModel() : nullptr;
    out << "],\"brassObservations\":[";
    count = 0;
    for (const auto& brass : brass_observations) {
        if (count++) out << ',';
        out << "{\"index\":" << brass.index << ",\"name\":" << std::quoted(brass.name) << '}';
    }
    out << "],\"viewModelIndex\":" << (view ? view->curstate.modelindex : 0)
        << ",\"viewModelName\":" << std::quoted(view && view->model ? view->model->name : "")
        << ",\"highEntities\":[";
    count = 0;
    if (enabled && gEngfuncs.GetEntityByIndex) for (int i = 1; i < 2048; ++i) {
        const auto* entity = gEngfuncs.GetEntityByIndex(i);
        if (!entity || unsigned(entity->curstate.modelindex) < precache::media_escape ||
            entity->curstate.modelindex < 0 || !entity->model) continue;
        if (count++) out << ',';
        out << "{\"entity\":" << i << ",\"index\":" << entity->curstate.modelindex
            << ",\"name\":" << std::quoted(entity->model->name) << '}';
    }
    out << "]}";
}
}

extern "C" const GoldCraftPrecacheAPI* GoldCraft_GetPrecacheAPI() {
    using namespace goldcraft::client_precache;
    static const GoldCraftPrecacheAPI result{sizeof(GoldCraftPrecacheAPI), 1,
        model_count, model_index, model_at, model_capacity};
    return enabled ? &result : nullptr;
}
