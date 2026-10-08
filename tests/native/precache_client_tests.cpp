// Exercise production routing and reversible patches, not a second parser.
#include "../../native/client/precache_client.cpp"
#include <cassert>
#include <iostream>

cl_enginefunc_t gEngfuncs{};
using namespace goldcraft::client_precache;

namespace {
std::vector<byte> packet;
size_t bit_cursor;
int native_parses, downloads, native_count;
int cursor, truncated, consistency, force;
void __cdecl parse_error(const char*, ...) { throw std::runtime_error("malformed packet"); }

void __cdecl begin_bits(void*) { bit_cursor = size_t(cursor) * 8; }
void __cdecl finish_bits(void*) { cursor = int((bit_cursor + 7) / 8); }
unsigned __cdecl bits(int count) {
    unsigned result = 0;
    for (int i = 0; i < count; ++i, ++bit_cursor) {
        if (bit_cursor / 8 >= packet.size()) { truncated = 1; return result; }
        result |= unsigned((packet[bit_cursor / 8] >> (bit_cursor % 8)) & 1) << i;
    }
    return result;
}
void load_packet(std::initializer_list<std::pair<unsigned, int>> fields) {
    packet.assign(1, 0xCC); // Verify lookahead restores a nonzero read position.
    size_t position = 8;
    for (const auto& [value, width] : fields) {
        for (int i = 0; i < width; ++i, ++position) {
            if (position / 8 >= packet.size()) packet.push_back(0);
            packet[position / 8] |= byte(((value >> i) & 1) << (position % 8));
        }
    }
    cursor = 1; truncated = 0;
}
void legacy_resources() {
    assert(!extended && cursor == 1 && !truncated);
    begin_bits(nullptr); native_count = int(bits(12)); finish_bits(nullptr);
    ++native_parses;
}
void valid_manifest() {
    load_packet({{goldcraft::precache::marker, 12}, {goldcraft::precache::magic, 32},
        {goldcraft::precache::version, 8}, {0, 32}, {0, 32}, {0, 16}, {0, 1}});
    parse_resources();
}
}

int main() {
    metahook_api_t fake_api{};
    fake_api.WriteDWORD = [](void* address, DWORD value) { std::memcpy(address, &value, 4); };
    fake_api.WriteMemory = [](void* address, void* value, DWORD size) -> DWORD {
        std::memcpy(address, value, size); return size;
    };
    api = &fake_api;
    read_count = &cursor; bad_read = &truncated;
    consistency_count = &consistency; force_consistency = &force;
    start_bits = begin_bits; end_bits = finish_bits; read_bits = bits;
    parse_resources_original = legacy_resources;
    loading_text = [](const char*) {};
    start_download = [](const char*, int) { ++downloads; };
    host_error = parse_error;
    client_media_enabled = true;

    std::array<model_t*, 512> stock_models{};
    std::array<Sound*, 512> stock_sounds{};
    legacy_model_precache = stock_models.data(); legacy_sound_precache = stock_sounds.data();
    DWORD model_operand = DWORD(stock_models.data()), sound_operand = DWORD(stock_sounds.data()) + 4;
    table_patches = {{&model_operand, Table::models, 0}, {&sound_operand, Table::sounds, 4}};
    DWORD sound_end = DWORD(stock_sounds.data() + stock_sounds.size());
    sound_end_patch = &sound_end; legacy_sound_end = sound_end;
    DWORD consistency_table = 0x12345678;
    consistency_table_patch = &consistency_table; legacy_consistency_table = consistency_table;
    byte consistency_bits = 12;
    consistency_bits_patch = &consistency_bits; legacy_consistency_bits = consistency_bits;
    std::array<DWORD, 5> limits{512, 512, 512, 512, 512};
    for (size_t i = 0; i < limits.size(); ++i) model_limits[i] = &limits[i];
    std::array<byte, 5> code{0xE8, 1, 2, 3, 4};
    const auto original_code = code;
    const std::array<byte, 4> replacement{9, 8, 7, 6};
    prepare_patch(code.data() + 1, replacement.data(), unsigned(replacement.size()));
    assert(code == original_code && !extended); // Installation stays inert.

    load_packet({{120, 12}, {0xFFFFFFFF, 32}}); parse_resources();
    assert(native_parses == 1 && native_count == 120 && !extended && downloads == 0);
    assert(code == original_code && model_operand == DWORD(stock_models.data()));
    load_packet({{4095, 12}, {0xFFFFFFFF, 32}}); parse_resources();
    assert(native_parses == 2 && native_count == 4095 && !extended);
    // A truncated lookahead must leave error handling to the original parser.
    load_packet({{4095, 12}}); parse_resources();
    assert(native_parses == 3 && native_count == 4095 && !extended);

    bool rejected = false;
    try { valid_manifest(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && !extended && code == original_code); // No mid-session switch.
    reset_client();

    load_packet({{goldcraft::precache::marker, 12}, {goldcraft::precache::magic, 32}, {255, 8}});
    rejected = false;
    try { parse_resources(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && !extended && code == original_code);
    client_media_enabled = false;
    rejected = false;
    try { valid_manifest(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && !extended && code == original_code);
    client_media_enabled = true;

    valid_manifest();
    assert(extended && !receiving && downloads == 1 && native_parses == 3);
    assert(std::equal(replacement.begin(), replacement.end(), code.begin() + 1));
    assert(model_operand == DWORD(models.data()) && consistency_bits == 32);
    models.resize(70000); sounds.resize(70001); update_tables();
    assert(model_operand == DWORD(models.data()) && sound_operand == DWORD(sounds.data()) + 4);
    assert(limits[0] == 70000 && sound_end == DWORD(sounds.data() + sounds.size()));
    clear_client_original = []() -> int {
        assert(extended); // Old connection cleanup precedes restoration.
        return 17;
    };
    assert(clear_client() == 17);
    assert(!extended && code == original_code && model_operand == DWORD(stock_models.data()));
    assert(sound_operand == DWORD(stock_sounds.data()) + 4 && sound_end == legacy_sound_end);
    assert(consistency_table == legacy_consistency_table && consistency_bits == 12);
    assert(limits == (std::array<DWORD, 5>{512, 512, 512, 512, 512}));
    assert(models.size() == 512 && sounds.size() == 512 && manifest_total == 0);

    load_packet({{180, 12}}); parse_resources();
    assert(native_parses == 4 && native_count == 180 && !extended && code == original_code);
    reset_client();
    valid_manifest(); // Ordinary -> extended -> ordinary -> extended.
    assert(extended && downloads == 2);
    disconnect_original = []() { assert(extended); };
    disconnect();
    assert(!extended && code == original_code && consistency_bits == 12 && resources.empty());
    reset_client(); // Repeated teardown is idempotent.
    assert(!extended && code == original_code && model_capacity() == 512);
    client_read_short = []() { return -1; };
    assert(read_brass_model() == -1); // Stock signed READ_SHORT semantics.
    std::cout << "Production native/extended parser routing and byte-exact teardown passed\n";
}
