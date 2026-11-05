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
unsigned sentinel_checks = 0, sentinel_failures = 0;
unsigned sync_checks{}, sync_begins{}, sync_cancels{};
bool sync_available = true;
std::vector<goldcraft::resource_update::Resource> captured;
std::uint64_t captured_generation{};
std::vector<std::unique_ptr<resource_t>> allocated_resources;
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

void check_sentinel(bool condition, const char* label) {
    ++sentinel_checks;
    if (!condition) {
        ++sentinel_failures;
        std::cerr << "model terminator: " << label << '\n';
    }
}

void resource_chunk(unsigned total, unsigned first, const std::vector<unsigned>& indices) {
    packet.assign(1, 0xCC);
    size_t position = 8;
    const auto put = [&](unsigned value, unsigned width) {
        for (unsigned i = 0; i < width; ++i, ++position) {
            if (position / 8 >= packet.size()) packet.push_back(0);
            packet[position / 8] |= byte(((value >> i) & 1) << (position % 8));
        }
    };
    put(goldcraft::precache::marker, 12); put(goldcraft::precache::magic, 32);
    put(goldcraft::precache::version, 8); put(total, 32); put(first, 32);
    put(unsigned(indices.size()), 16);
    for (const auto index : indices) {
        put(t_model, 4);
        const auto name = "models/terminator_" + std::to_string(index) + ".mdl";
        for (const auto character : name) put(byte(character), 8);
        put(0, 8); put(index, 32); put(32, 24); put(0, 3); put(0, 1);
    }
    if (first + indices.size() == total) put(0, 1); // No consistency list.
    cursor = 1; truncated = 0;
    parse_resources();
}

// Match the documented native name scan in C++, with an instrumented storage
// boundary. An old table fails safely instead of dereferencing unowned memory.
int native_name_lookup(model_t* const* slot_one, const char* name, unsigned& compared) {
    compared = 0;
    for (unsigned index = 1; index < models.size(); ++index) {
        const auto* model = slot_one[index - 1];
        if (!model) {
            check_sentinel(true, "native missing-name search reached NULL within physical storage");
            return -1;
        }
        ++compared;
        if (!_stricmp(model->name, name)) return int(index);
    }
    check_sentinel(false, "native missing-name search exhausted physical storage without NULL");
    return -2;
}

void model_terminator_tests(DWORD& model_operand, const std::array<DWORD, 5>& limits) {
    resource_t list{};
    needed = &list;
    read_string = []() -> const char* {
        static std::string value;
        value.clear();
        for (unsigned count = 0; count < 64; ++count) {
            const auto character = bits(8);
            if (truncated || !character) return value.c_str();
            value.push_back(char(character));
        }
        throw std::runtime_error("fixture resource name overflow");
    };
    zero_alloc = [](unsigned size) -> void* {
        assert(size == sizeof(resource_t));
        allocated_resources.push_back(std::make_unique<resource_t>());
        return allocated_resources.back().get();
    };
    model_for_index_original = [](int index) -> model_t* { return models.at(index); };
    int client_bad = 0;
    client_bad_read = &client_bad;
    client_read_short = []() { return int(bits(16)); };
    client_read_long = []() { return int(bits(32)); };
    read_short = client_read_short;
    read_long = client_read_long;

    model_t ordinary{}, final{};
    std::strcpy(ordinary.name, "models/ordinary.mdl");
    std::strcpy(final.name, "models/final.mdl");
    for (const unsigned slots : {512u, 513u, 879u, 4096u, 65535u, 65536u, 70001u}) {
        reset_client();
        list.pNext = list.pPrev = &list;
        allocated_resources.clear();
        check_sentinel(model_capacity() == 512, "reset preserves the stock API capacity");
        // Actual resource parser grows the production vector; no second parser
        // or manually resized replacement table participates in these cases.
        resource_chunk(2, 0, {1});
        check_sentinel(receiving && manifest_next == 1 && resources.size() == 1,
            "first chunk keeps the download pending");
        resource_chunk(2, 1, {slots - 1});
        check_sentinel(!receiving && resources.size() == 2 && model_capacity() == int(slots),
            "final chunk exposes only the advertised model slots");
        check_sentinel(models.size() == size_t(slots) + 1,
            "one extra physical slot exists outside the public index range");
        check_sentinel(std::all_of(limits.begin(), limits.end(), [slots](DWORD value) { return value == slots; }),
            "every engine index limit excludes the terminator");
        check_sentinel(model_operand == DWORD(models.data()), "growth refreshes the engine table operand");
        // The native loader populates model handles through that operand.
        auto* const storage = reinterpret_cast<model_t**>(model_operand);
        for (unsigned index = 1; index < slots; ++index) storage[index] = &ordinary;
        storage[slots - 1] = &final;
        unsigned compared = 0;
        check_sentinel(native_name_lookup(storage + 1, "sprites/not_precached.spr", compared) == -1 && compared == slots - 1,
            "a missing name stops after all loaded entries");
        check_sentinel(native_name_lookup(storage + 1, "MODELS/FINAL.MDL", compared) == int(slots - 1),
            "the last legal model remains case-insensitively findable");
        check_sentinel(model_for_index(int(slots - 1)) == &final && model_for_index(int(slots)) == nullptr,
            "native index lookup rejects the terminator");
        for (const auto index : {slots - 1, slots}) {
            const auto load_index = [&] {
                if (index < goldcraft::precache::media_escape) load_packet({{index, 16}});
                else load_packet({{goldcraft::precache::media_escape, 16}, {index, 32}});
                begin_bits(nullptr);
            };
            bool rejected = false;
            load_index();
            try { check_sentinel(read_model_index() == index, "wire reader preserves the last valid index"); }
            catch (const std::runtime_error&) { rejected = true; }
            check_sentinel(rejected == (index == slots), "wire reader rejects exactly the public upper bound");
            rejected = false;
            load_index();
            try { check_sentinel(read_brass_model() == int(index), "Brass reader preserves the last valid index"); }
            catch (const std::runtime_error&) { rejected = true; }
            check_sentinel(rejected == (index == slots), "Brass reader rejects exactly the public upper bound");
        }
        // Append a further chunk in a fresh connection after populating the
        // first table, so its old terminator becomes a legal model slot.
        reset_client();
        list.pNext = list.pPrev = &list;
        allocated_resources.clear();
        resource_chunk(2, 0, {slots - 1});
        auto* const before_growth = reinterpret_cast<model_t**>(model_operand);
        for (unsigned index = 1; index < slots; ++index) before_growth[index] = &ordinary;
        resource_chunk(2, 1, {slots + 7});
        auto* const grown = reinterpret_cast<model_t**>(model_operand);
        check_sentinel(model_capacity() == int(slots + 8) && grown[slots - 1] == &ordinary,
            "later chunk preserves old loaded handles");
        for (unsigned index = slots; index < slots + 8; ++index) grown[index] = &ordinary;
        check_sentinel(native_name_lookup(grown + 1, "sprites/not_precached.spr", compared) == -1 && compared == slots + 7,
            "growing a populated table moves the NULL terminator");
    }
    reset_client();
    allocated_resources.clear();
    check_sentinel(model_capacity() == 512 && models.size() == 513 && models.back() == nullptr,
        "teardown shrinks logical storage and clears the terminator");
    reset_client();
    check_sentinel(models.size() == 513 && models.back() == nullptr, "repeated teardown preserves the terminator");
}

void resource_sync_tests() {
    using goldcraft::resource_sync::manifest_version;
    resource_t list{};
    needed = &list;
    const auto clean = [&] {
        reset_client(); list.pPrev = list.pNext = &list; allocated_resources.clear(); captured.clear();
    };
    set_resource_sync({[] { return sync_available; },
        [](std::uint64_t generation, const std::vector<goldcraft::resource_update::Resource>& items) -> const char* {
            // Parsing must finish before the asynchronous receiver can inspect
            // the manifest, and it must not have started native downloading.
            assert(!receiving && sync_waiting && bit_cursor <= std::size_t(cursor) * 8);
            captured = items; captured_generation = generation; ++sync_begins; return nullptr;
        }, [] { ++sync_cancels; }});
    read_data = [](void* output, int count) { for (int i=0;i<count;++i) static_cast<byte*>(output)[i]=byte(bits(8)); return count; };
    const auto make = [&](unsigned total, unsigned first, unsigned count, std::uint64_t generation,
                          unsigned version=manifest_version, bool required=true, unsigned size=22000000,
                          unsigned type=t_generic, unsigned flags=0) {
        packet.assign(1, 0xcc); std::size_t position=8;
        const auto put = [&](unsigned value, unsigned width) {
            for(unsigned i=0;i<width;++i,++position) {
                if(position/8>=packet.size())packet.push_back(0);
                packet[position/8]|=byte(((value>>i)&1)<<(position%8));
            }
        };
        put(goldcraft::precache::marker,12);put(goldcraft::precache::magic,32);put(version,8);
        put(total,32);put(first,32);put(count,16);
        if(version==manifest_version){put(unsigned(generation),32);put(unsigned(generation>>32),32);}
        for(unsigned i=0;i<count;++i) {
            put(type,4);const std::string name=type==t_sound?"effect.wav":"models/update.mdl";
            for(unsigned char c:name)put(c,8);put(0,8);put(first+i+1,32);put(size,24);put(flags,3);
            if(flags&RES_CUSTOM)for(int j=0;j<16;++j)put(0,8);
            put(0,1);
            if(version==manifest_version) {
                put(required,1);if(required){put(size,32);for(unsigned j=0;j<32;++j)put(j+first+i,8);}
            }
        }
        if(first+count==total)put(0,1);
        cursor=1;truncated=0;
    };
    const auto check=[&](bool value){++sync_checks;assert(value);};
    clean();const int before=downloads;
    make(2,0,1,0x1234567800000001ULL);parse_resources();
    check(receiving && !sync_waiting && downloads==before && sync_begins==0);
    make(2,1,1,0x1234567800000001ULL);parse_resources();
    check(!receiving && sync_waiting && downloads==before && sync_begins==1);
    check(captured.size()==2 && captured_generation==0x1234567800000001ULL);
    check(captured[0].expected.size==22000000 && resources[0]->nDownloadSize==22000000);
    check(captured[1].expected.sha256[31]==32 && captured[0].name=="models/update.mdl");
    check(resume_resource_sync() && downloads==before+1);
    check(!resume_resource_sync() && downloads==before+1);
    clean();check(!sync_waiting && sync_resources.empty() && !resume_resource_sync());
    make(1,0,1,1,manifest_version,true,32,t_sound);parse_resources();
    check(captured.size()==1 && captured[0].name=="sound/effect.wav");
    clean();make(1,0,1,1,manifest_version,false,32,t_decal);parse_resources();
    check(captured.empty() && sync_waiting);
    for(unsigned scenario=0;scenario<9;++scenario) {
        clean();bool rejected=false;
        if(scenario==0)sync_available=false;
        if(scenario==1 || scenario==2) {make(2,0,1,1);parse_resources();}
        make(scenario==1||scenario==2?2:1,scenario==1||scenario==2?1:0,1,
             scenario==3?0:scenario==1?2:1,scenario==2?goldcraft::precache::version:manifest_version,
             true,scenario==4?0:scenario==5?goldcraft::resource_sync::max_file_size+1:32,
             scenario==6?t_decal:t_generic,scenario==7?RES_CUSTOM:0);
        if(scenario==8)packet.resize(packet.size()-8);
        try {parse_resources();}catch(const std::runtime_error&){rejected=true;}
        check(rejected);sync_available=true;
    }
    clean();set_resource_sync({});
    check(sync_cancels>0);
    valid_manifest();check(!sync_waiting && downloads==before+2); // v3 still native.
    clean();needed=nullptr;
    std::cout << "resource sync parser: " << sync_checks << " checks, 0 failures\n";
}
}

void model_owner_tests() {
    std::array<model_t, 2> originals{};
    int count = int(originals.size());
    original_models = originals.data(); original_model_count = &count;
    extra_models.clear(); extra_model_indices.clear();
    enabled = false;
    check_sentinel(!GoldCraft_GetModelOwnersAPI(), "uninstalled owner API unavailable");
    enabled = true;
    const auto* api = GoldCraft_GetModelOwnersAPI();
    check_sentinel(api && api->version == 1 && api->size == sizeof(*api), "owner ABI identity");
    check_sentinel(api->index_limit() == count, "original-only owner range");
    for (int i = 0; i < 2; ++i) {
        auto value = std::make_unique<Model>();
        value->model.type = i == 0 ? mod_studio : mod_sprite;
        extra_model_indices.emplace(&value->model, unsigned(extra_models.size()));
        extra_models.push_back(std::move(value));
    }
    for (bool connected : {true, false}) {
        extended = connected;
        check_sentinel(model_count() == count + (connected ? 2 : 0), "connection API retains old range");
        check_sentinel(api->index_limit() == 1026, "owner range survives disconnect");
        check_sentinel(api->model_at(2) == nullptr && api->model_at(1023) == nullptr,
                       "sparse unused original slots are not synthetic owners");
        for (int i = 0; i < 2; ++i) {
            check_sentinel(api->model_at(i) == &originals[i], "original owner retained");
            auto* model = &extra_models[i]->model;
            check_sentinel(api->model_at(1024 + i) == model && api->model_index(model) == 1024 + i,
                           "extra owner round trip after disconnect");
        }
        check_sentinel(!api->model_at(-1) && !api->model_at(1026), "owner bounds rejected");
    }
    model_t foreign{};
    check_sentinel(api->model_index(&foreign) == -1, "foreign owner rejected");
    extra_models.clear(); extra_model_indices.clear();
    original_models = nullptr; original_model_count = nullptr; enabled = false;
}

int main(int argc, char**) {
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
    if (argc > 1) {
        model_terminator_tests(model_operand, limits);
        std::cout << "model terminator: " << sentinel_checks << " checks, " << sentinel_failures << " failures\n";
        return sentinel_failures ? 1 : 0;
    }

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
    models.resize(70001); sounds.resize(70001); update_tables();
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
    assert(models.size() == 513 && sounds.size() == 512 && manifest_total == 0);

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
    model_terminator_tests(model_operand, limits);
    std::cout << "model terminator: " << sentinel_checks << " checks, " << sentinel_failures << " failures\n";
    resource_sync_tests();
    model_owner_tests();
    std::cout << "model ownership: total " << sentinel_checks << " checks, " << sentinel_failures << " failures\n";
    return sentinel_failures ? 1 : 0;
}
