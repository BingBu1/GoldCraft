// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include "../../native/client/resource_native_hud.hpp"
#include "../../native/client/hud_sprites.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace goldcraft::resource_native_hud;
namespace {
int checks{}, failures{};
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", name); }
}
void check(Result result, const char* name) { check(bool(result), name); }
template<class T> T read(std::span<std::byte> bytes, std::size_t offset) {
    T value{}; std::memcpy(&value, bytes.data() + offset, sizeof(value)); return value;
}
template<class T> void write(std::span<std::byte> bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
struct Fixture;
Fixture* active{};
constexpr DWORD NativeAbort = 0xe0474348;
// This helper also lets the same new regression fixture compile against the
// frozen validation05 header for an ABI-correct old-source counterexample.
template<class T> void bind_resolution(T& binding, int (*read_resolution)(void*) noexcept) {
    if constexpr (requires { binding.current_resolution = read_resolution; })
        binding.current_resolution = read_resolution;
}
std::unordered_set<void*> engine_sources;
void engine_free(void* pointer) noexcept {
    check(engine_sources.erase(pointer) == 1, "SPR source uses its exact engine allocator owner");
    std::free(pointer);
}

struct Fixture {
    std::array<std::byte, hud_size> bytes{};
    std::unordered_map<void*, std::size_t> arrays;
    std::unordered_set<int> textures;
    std::vector<std::string> names{"number_0", "bucket1", "selection", "flash_empty", "flash_full",
        "flash_beam", "dmg_bio", "damage_next", "cross", "radar", "radaropaque"};
    int version{}, loads{}, calls{}, allocations{}, frees{}, texture_frees{}, next_texture{100};
    int fail_allocation{}, invalid_count{}, callback_result{1};
    bool throw_after{}, reenter{}, four_textures{}, foreign_directory_after{};
    bool abort_before_textures{};
    int resolution{640}, resolution_reads{}, abort_after_texture{};
    wrect_t* retained_rectangle{};
    Refresh refresh;
    Admission admission{1,1,3,0,true,true,true,false,false,false};
    Binding binding;

    Fixture() {
        active = this;
        goldcraft::hud_sprites::bind(loader);
        binding = {bytes, release_array, this, vid_init, release_texture};
        bind_resolution(binding, current_resolution);
        write(bytes, 0x60, 640);
        vid_init(this); // Real initial native call must have completed before update.
        calls = 0; allocations = 0;
    }
    static int current_resolution(void* context) noexcept {
        auto& self = *static_cast<Fixture*>(context);
        ++self.resolution_reads;
        return self.resolution;
    }
    ~Fixture() {
        if (refresh.pending()) {
            admission.old_view_restored = true; version = 0; callback_result = 1;
            fail_allocation = 0; throw_after = false; reenter = false; foreign_directory_after = false;
            const auto result = refresh.Rollback(admission);
            check(bool(result), "fixture leaves no pending HUD transaction");
        }
        for (auto [p, size] : arrays) std::free(p);
        arrays.clear(); textures.clear();
        goldcraft::hud_sprites::bind(loader);
    }
    static client_sprite_t* loader(char*, int* count) {
        auto& self = *active; ++self.loads;
        const auto n = self.names.size() + (self.version ? 7 : 0);
        auto* result = static_cast<client_sprite_t*>(std::calloc(n * 2, sizeof(client_sprite_t)));
        engine_sources.insert(result);
        for (int res = 0; res < 2; ++res) for (std::size_t i = 0; i < n; ++i) {
            auto& item = result[res * n + i];
            const auto name = i < self.names.size() ? self.names[i] : "new_" + std::to_string(i);
            std::snprintf(item.szName, sizeof(item.szName), "%s", name.c_str());
            std::snprintf(item.szSprite, sizeof(item.szSprite), "hud%d", self.version);
            item.iRes = res ? 640 : 320; item.rc = {0, self.version ? 45 : 20, 0, 30};
        }
        *count = self.invalid_count ? self.invalid_count : static_cast<int>(n * 2);
        return result;
    }
    static void release_array(void* pointer) noexcept {
        auto& self = *active;
        check(self.arrays.erase(pointer) == 1, "native array release only frees native-owned blocks once");
        for (auto offset : {0x178,0x17c,0x180})
            check(read<void*>(self.bytes, offset) != pointer, "current HUD never references a freed array");
        ++self.frees; std::free(pointer);
    }
    static void release_texture(void* context, int id) noexcept {
        auto& self = *static_cast<Fixture*>(context);
        check(self.textures.erase(id) == 1, "native scope texture owner releases each ID once");
        for (auto offset : {0x40f0,0x40f4,0x40f8,0x40fc,0x4100})
            check(read<int>(self.bytes, offset) != id, "scope does not retain retired texture ID");
        ++self.texture_frees;
    }
    void* allocate(std::size_t size) {
        ++allocations;
        if (fail_allocation && allocations == fail_allocation) throw std::bad_alloc();
        auto* value = std::calloc(1, size); arrays[value] = size; return value;
    }
    // SDK fixture executes the observed native layout/lifecycle: list==null
    // allocates 3 arrays, nonnull reloads handles only, components retain rect
    // pointers, and scope replaces its ISurface ID. This is not a real game run.
    static int vid_init(void* context) {
        auto& self = *static_cast<Fixture*>(context); ++self.calls;
        if (self.reenter) {
            check(!self.refresh.Rebuild(self.admission), "recursive native refresh is rejected");
        }
        write(self.bytes, 0x4, 0); write(self.bytes, 0x20, 0);
        auto* list = read<client_sprite_t*>(self.bytes, 0x0c);
        int total = read<int>(self.bytes, 0x14), selected = read<int>(self.bytes, 0x10);
        if (!list) {
            char path[] = "sprites/hud.txt";
            list = goldcraft::hud_sprites::list(path, &total);
            write(self.bytes, 0x0c, list); write(self.bytes, 0x14, total);
            selected = 0;
            for (int i = 0; i < total; ++i) selected += list[i].iRes == self.resolution;
            write(self.bytes, 0x10, selected);
            write(self.bytes, 0x178, self.allocate(selected * 4));
            write(self.bytes, 0x17c, self.allocate(selected * 16));
            write(self.bytes, 0x180, self.allocate(selected * 24));
            auto* rects = read<wrect_t*>(self.bytes, 0x17c);
            auto* names = read<char*>(self.bytes, 0x180);
            int cursor = 0;
            for (int i = 0; i < total; ++i) if (list[i].iRes == self.resolution) {
                rects[cursor] = list[i].rc; std::strncpy(names + cursor * 24, list[i].szName, 24); ++cursor;
            }
        }
        write(self.bytes, 0x60, self.resolution);
        auto* handles = read<int*>(self.bytes, 0x178);
        for (int i = 0; i < selected; ++i) handles[i] = 1000 + self.version * 100 + i;
        write(self.bytes, 0x4754, 0); write(self.bytes, 0x16c, 30);
        self.retained_rectangle = read<wrect_t*>(self.bytes, 0x17c) + 3;
        write(self.bytes, 0x1b44, self.retained_rectangle);
        if (self.abort_before_textures) RaiseException(NativeAbort, 0, 0, nullptr);
        int created = 0;
        for (int i = self.four_textures ? 1 : 0; i < (self.four_textures ? 5 : 1); ++i) {
            const auto texture = self.next_texture++; self.textures.insert(texture);
            write(self.bytes, 0x40f0 + 4 * i, texture);
            if (++created == self.abort_after_texture) RaiseException(NativeAbort, 0, 0, nullptr);
        }
        if (self.foreign_directory_after)
            write(self.bytes, 0x0c, reinterpret_cast<client_sprite_t*>(std::uintptr_t{1}));
        if (self.throw_after) throw std::bad_alloc();
        return self.callback_result;
    }
    Result prepare() {
        return refresh.Prepare(admission, binding, goldcraft::hud_sprites::resource_owner());
    }
    Result initialize(std::uint64_t event = 1) {
        return refresh.Initialize({event,true,true,true,true,true,resolution}, binding,
                                  goldcraft::hud_sprites::resource_owner());
    }
};

// /EHsc leaves this SEH code outside catch(...). The outer boundary proves
// that the native call has unwound; no C++ exception or forced-success return
// stands in for an interrupted original callback.
bool escape_native(Fixture* fixture, int operation, std::uint64_t event = 1) {
    __try {
        if (operation == 0) fixture->refresh.Rebuild(fixture->admission);
        else if (operation == 1) fixture->refresh.Rollback(fixture->admission);
        else fixture->initialize(event);
        return false;
    } __except (GetExceptionCode() == NativeAbort ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
}

void recovery_regressions() {
    check(goldcraft::hud_sprites::bind_source_release(engine_free), "recovery cases use the exact source owner");
    {
        Fixture f; f.resolution = 320;
        const auto calls = f.calls, loads = f.loads;
        check(!f.prepare() && f.calls == calls && f.loads == loads,
              "ordinary preparation rejects a screen resolution newer than retained native arrays");
        f.resolution = 640;
    }
    {
        Fixture f; f.version = 1; check(f.prepare(), "resolution rebuild case prepares");
        const auto calls = f.calls;
        f.resolution = 320;
        check(!f.refresh.Rebuild(f.admission) && f.calls == calls && f.frees == 0,
              "rebuild samples actual resolution before calling native or allocating replacement arrays");
        f.resolution = 640; f.version = 0; f.admission.old_view_restored = true;
        check(f.refresh.Rollback(f.admission), "changed-resolution preflight can cancel without a native call");
    }
    {
        Fixture f; f.version = 1;
        check(f.prepare() && f.refresh.Rebuild(f.admission), "ordinary rollback resolution case builds");
        const auto calls = f.calls, reads = f.resolution_reads;
        const auto candidate = read<void*>(f.bytes, 0x17c);
        f.version = 0; f.resolution = 320; f.admission.old_view_restored = true;
        const auto result = f.refresh.Rollback(f.admission);
        check(!result && result.requires_restore && f.refresh.pending() && f.calls == calls && f.frees == 0 &&
              read<void*>(f.bytes, 0x17c) == candidate,
              "ordinary rollback rejects actual changed resolution before writing retained arrays or calling native");
        check(f.resolution_reads > reads, "ordinary rollback obtains a fresh screen observation");
        f.resolution = 640;
        if (f.refresh.pending()) check(f.refresh.Rollback(f.admission), "rollback recovers once actual resolution matches");
    }
    {
        Fixture f; f.version = 1;
        check(f.prepare() && f.refresh.Rebuild(f.admission), "commit resolution case builds");
        f.resolution = 320;
        const auto result = f.refresh.Commit(f.admission);
        check(!result && result.requires_restore && f.refresh.pending() && f.frees == 0,
              "commit cannot retire rollback arrays after actual resolution changes");
        f.resolution = 640; f.version = 0; f.admission.old_view_restored = true;
        if (f.refresh.pending()) check(f.refresh.Rollback(f.admission), "resolution-rejected commit retains recoverable old owner");
    }
    {
        Fixture f; f.resolution = 320;
        const auto calls = f.calls, loads = f.loads;
        check(!f.refresh.Initialize({1,true,true,true,true,true,640}, f.binding,
              goldcraft::hud_sprites::resource_owner()) && f.calls == calls && f.loads == loads,
              "natural event cannot substitute a stale resolution for the actual screen");
        f.resolution = 640;
    }
    for (int created : {0, 1, 2, 4}) {
        Fixture f; f.version = 1; f.four_textures = true;
        check(f.prepare(), "non-C++ initial rebuild case prepares");
        f.abort_before_textures = created == 0; f.abort_after_texture = created;
        check(escape_native(&f, 0), "initial native rebuild escapes C++ catch through the outer SEH boundary");
        check(f.refresh.pending() && f.arrays.size() == 6, "initial unwind retains both real array generations");
        f.abort_before_textures = false; f.abort_after_texture = 0; f.version = 0;
        check(f.initialize(), "proven initial unwind can capture partial replacement and recover");
        check(f.arrays.size() == 3 && f.textures.size() == 5,
              "initial unwind recovery retires all replaced arrays and partial scope IDs exactly once");
    }
    for (int created : {0, 1, 2, 4}) {
        Fixture f; f.version = 1; f.four_textures = true;
        check(f.prepare() && f.refresh.Rebuild(f.admission), "non-C++ rollback case builds candidate");
        f.version = 0; f.admission.old_view_restored = true;
        f.abort_before_textures = created == 0; f.abort_after_texture = created;
        check(escape_native(&f, 1), "rollback native call escapes without a C++ failure return");
        check(f.refresh.pending() && f.arrays.size() == 6, "interrupted rollback retains old and candidate arrays");
        check(escape_native(&f, 2, 1), "next proven boundary may encounter a second non-C++ rollback unwind");
        check(f.refresh.pending() && f.arrays.size() == 6, "repeated rollback unwind never overwrites the candidate generation");
        f.abort_before_textures = false; f.abort_after_texture = 0;
        check(f.initialize(2), "later natural boundary safely rebinds after repeated rollback unwinds");
        check(f.arrays.size() == 3, "successful recovery retires the original candidate arrays after rollback unwind");
        check(f.textures.size() == 5, "successful recovery retires all displaced and partial scope IDs after rollback unwind");
    }
    check(engine_sources.empty(), "recovery regressions leave no owned source allocations");
}

void signature_tests() {
    std::vector<std::byte> image(hud_rva + hud_size);
    auto bytes = std::span(image);
    auto put = [&](std::size_t offset, std::initializer_list<unsigned char> values) {
        for (auto v : values) image[offset++] = static_cast<std::byte>(v);
    };
    put(0x47790,{0xb9}); write(bytes,0x47791,std::uint32_t{0x20117b48});
    put(0x47795,{0xe8,6,0x99,0,0,0xb8,1,0,0,0,0xc3});
    put(0x51104,{0x8b,0x7e,0x0c,0x85,0xff,0x0f,0x85,0x57,1,0,0});
    put(0x50424,{0xff,0xb7,0x78,1,0,0,0xe8,0xea,0x44,6,0});
    put(0x5042f,{0xff,0xb7,0x7c,1,0,0,0xe8,0xdf,0x44,6,0});
    put(0x5043a,{0xff,0xb7,0x80,1,0,0,0xe8,0xd4,0x44,6,0});
    put(0xb4919,{0xe9,0xb6,0x0b,0,0}); put(0xb54d4,{0xe9,0xcb,1,0,0});
    check(!ValidateImage(image,0x20000000,client_sha256,engine_sha256), "relocated image signatures match exact HUD layout");
    check(ValidateImage(image,0x20000000,"other",engine_sha256), "different native client is rejected");
    check(ValidateImage(image,0x20000000,client_sha256,"other"), "different native engine is rejected");
    check(ValidateImage(image,0x21000000,client_sha256,engine_sha256), "relocated gHUD operand must match image base");
    check(ValidateImage(std::span(image).first(200),0x20000000,client_sha256,engine_sha256), "truncated image cannot expose writable fields");
    for (auto offset : {0x47790,0x47795,0x51104,0x50424,0x5042f,0x5043a,0xb4919,0xb54d4}) {
        const auto old = image[offset]; image[offset] ^= std::byte{1};
        check(ValidateImage(image,0x20000000,client_sha256,engine_sha256), "changed instruction rejects native HUD binding");
        image[offset] = old;
    }
    std::vector<std::byte> engine(0x2b2440);
    constexpr unsigned char allocator[] = {0x69,0xcb,0x98,0,0,0,0x6a,1,0x51,0xff,0x15};
    std::memcpy(engine.data()+0x195cb0,allocator,sizeof(allocator));
    write(std::span(engine),0x195cbb,std::uint32_t{0x202b2438});
    engine[source_free_rva]=std::byte{0xff};engine[source_free_rva+1]=std::byte{0x25};
    write(std::span(engine),source_free_rva+2,std::uint32_t{0x202b242c});
    check(!ValidateSourceAllocator(engine,0x20000000,engine_sha256),"relocated engine allocation/free instructions match");
    check(ValidateSourceAllocator(engine,0x21000000,engine_sha256),"wrong engine relocation rejects source deallocator");
    check(ValidateSourceAllocator(engine,0x20000000,"wrong"),"unknown engine cannot free SPR source");
}
}

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc >= 2 && std::strcmp(argv[1], "--recovery-regressions") == 0) {
        recovery_regressions();
        std::printf("native HUD recovery: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    signature_tests();
    if (argc >= 2) {
        auto* input = std::fopen(argv[1], "rb");
        check(input != nullptr, "exact original PE mapped-image fixture opens");
        if (!input) return 2;
        std::fseek(input, 0, SEEK_END); const auto size = std::ftell(input);
        std::fseek(input, 0, SEEK_SET); std::vector<std::byte> image(static_cast<std::size_t>(size));
        check(std::fread(image.data(), 1, image.size(), input) == image.size(), "original mapped-image fixture read completes");
        std::fclose(input);
        check(!ValidateImage(image, 0x10000000, client_sha256, engine_sha256), "actual original PE bytes match native HUD binding");
        write(std::span(image), 0x47791, std::uint32_t{0x30117b48});
        check(!ValidateImage(image, 0x30000000, client_sha256, engine_sha256), "actual original instructions support relocated HUD address");
    }
    if (argc >= 3) {
        auto* input = std::fopen(argv[2], "rb");
        check(input != nullptr,"exact original engine mapped image opens");if (!input) return 2;
        std::fseek(input,0,SEEK_END); const auto size=std::ftell(input);std::fseek(input,0,SEEK_SET);
        std::vector<std::byte> image(static_cast<std::size_t>(size));
        check(std::fread(image.data(),1,image.size(),input)==image.size(),"engine mapped image read completes");std::fclose(input);
        check(!ValidateSourceAllocator(image,0x10000000,engine_sha256),"actual engine calloc and free instructions match binding");
    }
    // This first fixture also verifies delayed owner binding after initial HUD
    // loading. No guessed plugin free is used for the engine's pending source.
    {
        Fixture f;
        check(engine_sources.size()==1,"first client source remains retained before allocator binding");
    }
    {
        Fixture f; f.version = 1;
        check(!f.prepare() && f.frees == 0, "unbound source allocator blocks update without touching original HUD");
        check(engine_sources.size() == 2, "client rebind retains both copied sources until exact engine free is bound");
        check(goldcraft::hud_sprites::bind_source_release(engine_free), "late engine allocator binding succeeds");
        check(engine_sources.empty(), "late binding releases only the actual copied engine source");
        const auto old_list = read<client_sprite_t*>(f.bytes,0x0c);
        const auto old_rect = read<wrect_t*>(f.bytes,0x17c);
        const auto old_count = read<int>(f.bytes,0x10);
        // Legacy counterexample: video init alone keeps the old directory/rects.
        Fixture::vid_init(&f);
        check(read<int>(f.bytes,0x10) == old_count && old_rect[0].right == 20,
              "old VidInit-only behavior reproduces stale size and rectangles");
        check(f.prepare() && f.arrays.contains(old_rect), "preflight stages new directory while retaining native arrays");
        check(!goldcraft::hud_sprites::bind(Fixture::loader), "rebind cannot retire an active directory transaction");
        const auto pre = f.calls;
        check(f.refresh.Rebuild(f.admission), "fresh native HUD generation is built");
        check(f.calls == pre + 1 && f.frees == 0, "rebuild uses one export and keeps rollback arrays alive");
        check(read<client_sprite_t*>(f.bytes,0x0c) != old_list && read<int>(f.bytes,0x10) == old_count + 7,
              "changed directory length reaches native selected count");
        check(f.retained_rectangle->right == 45, "native rect consumers bind new rectangle storage");
        check(f.refresh.Commit(f.admission), "native HUD transaction commits");
        check(f.frees == 3 && !f.arrays.contains(old_rect), "commit frees exactly three retired native blocks");
        check(!f.refresh.Rebuild(f.admission), "duplicate rebuild does not issue another native call");
        check(engine_sources.empty(), "both original and candidate SPR sources are released");
    }
    for (int state : {0,1,2,4,5,6}) {
        Fixture f; f.admission.state = state; const auto loads=f.loads;
        check(!f.prepare() && f.loads==loads && f.frees==0, "only connected pre-download state is allowed");
    }
    for (int invalid=0; invalid<8; ++invalid) {
        Fixture f;
        switch (invalid) {
        case 0:f.admission.connection=0;break; case 1:f.admission.initial_vidinit=0;break;
        case 2:f.admission.uploading=1;break; case 3:f.admission.manifest_complete=false;break;
        case 4:f.admission.hashes_verified=false;break;case 5:f.admission.consumers_paused=false;break;
        case 6:f.admission.download_started=true;break;case 7:f.admission.signon_started=true;break;
        }
        const auto loads=f.loads;
        check(!f.prepare() && f.loads==loads && f.frees==0, "all admission gates reject before directory I/O or release");
    }
    for (const auto* missing : {"number_0","bucket1","flash_empty","flash_full","flash_beam","cross","radar","radaropaque"}) {
        Fixture f;
        for (auto& name:f.names) if(name==missing) name="missing";
        f.version=1;
        check(!f.prepare() && f.frees==0 && f.calls==0, "required native rectangle is checked before native VidInit can index -1");
        check(engine_sources.empty(), "rejected staged catalog releases its engine source");
    }
    for (int failure=1;failure<=5;++failure) {
        Fixture f; f.version=1;
        const auto old=read<void*>(f.bytes,0x17c);
        check(f.prepare(), "failed-rebuild case prepares");
        f.fail_allocation=failure<=3?failure:0; f.callback_result=failure==4?0:1; f.throw_after=failure==5;
        const auto result=f.refresh.Rebuild(f.admission);
        check(!result && result.requires_restore && f.frees==0, "partial/full native failure preserves rollback generation");
        check(!f.refresh.Commit(f.admission), "failed native rebuild cannot commit");
        check(!f.refresh.Rollback(f.admission), "rollback requires parent to restore old resource view");
        f.admission.old_view_restored=true;f.version=0;f.callback_result=1;f.fail_allocation=0;f.throw_after=false;
        check(f.refresh.Rollback(f.admission), "rollback reruns native binding on retained directory");
        check(read<void*>(f.bytes,0x17c)==old && f.retained_rectangle==static_cast<wrect_t*>(old)+3,
              "rollback consumers point at retained original arrays before candidate release");
        check(f.arrays.size()==3 && !f.refresh.pending(), "rollback releases candidate blocks without double-free");
    }
    {
        Fixture f; f.version=1; check(f.prepare(),"late gate fixture prepares");
        f.admission.state=5;check(!f.refresh.Rebuild(f.admission)&&f.frees==0,"transition to active after prepare blocks retirement");
        f.admission.state=3;check(f.refresh.Rebuild(f.admission),"unchanged paused signon may resume");
        ++f.admission.connection;check(!f.refresh.Commit(f.admission),"different connection cannot commit stale HUD plan");
        --f.admission.connection; f.admission.old_view_restored=true;f.version=0;
        check(f.refresh.Rollback(f.admission),"same admission can restore after stale-token commit rejection");
    }
    {
        Fixture f;f.version=1;check(f.prepare(),"reentrant fixture prepares");f.reenter=true;
        check(f.refresh.Rebuild(f.admission),"outer native callback survives rejected recursion");
        f.reenter=false;check(f.refresh.Commit(f.admission),"outer refresh commits once");
        ++f.admission.connection;++f.admission.initial_vidinit;f.version=0;
        check(f.prepare()&&f.refresh.Rebuild(f.admission)&&f.refresh.Commit(f.admission),
              "later connection can rebuild a smaller ordinary-base HUD catalog");
        check(read<int>(f.bytes,0x10)==12,"base catalog retains infection compatibility entry without CSOL dependence");
    }
    {
        Fixture f; f.version=1; check(f.prepare(),"rollback retry prepares");check(f.refresh.Rebuild(f.admission),"rollback retry builds");
        f.admission.old_view_restored=true;f.version=0;f.callback_result=0;
        check(!f.refresh.Rollback(f.admission)&&f.refresh.pending(),"failed rollback keeps both array generations alive");
        check(f.textures.size()==1,"failed rollback retires superseded scope IDs while preserving current owner");
        check(!f.refresh.Rollback(f.admission)&&f.refresh.pending(),"second failed rollback retains valid current scope IDs");
        f.callback_result=1;check(f.refresh.Rollback(f.admission),"rollback can rebind after recoverable callback failure");
        check(f.arrays.size()==3,"rollback retry leaves only native current arrays");
        check(f.textures.size()==1,"rollback retries leave one live scope ID without double retirement");
    }
    {
        Fixture f; f.version=1; f.invalid_count=9000;
        check(!f.prepare()&&f.frees==0,"oversized parser result is rejected before copying");
        check(engine_sources.empty(),"invalid candidate count still releases known original source");
        char path[]="sprites/weapon_test.txt";int count=0;f.invalid_count=0;
        auto* raw=goldcraft::hud_sprites::list(path,&count);
        check(engine_sources.contains(raw),"non-HUD SPR lists preserve original caller ownership");engine_free(raw);
    }
    {
        Fixture f;f.version=1;f.four_textures=true;
        check(f.prepare()&&f.refresh.Rebuild(f.admission)&&f.refresh.Commit(f.admission),"scope four-ID branch commits");
        check(f.textures.size()==5,"four-ID branch retains unmodified single-ID owner too");
        f.version=0;
        check(f.initialize(),"actual reset boundary rebuilds base catalog without a server manifest");
        check(f.calls==2 && f.textures.size()==5,"normal natural reset delegates once and retires only replaced scope IDs");
        check(read<int>(f.bytes,0x10)==12,"overlay catalog is replaced with ordinary base directory");
        const auto calls=f.calls;
        check(!f.initialize()&&f.calls==calls,"same native reset event cannot invoke original twice");
        f.resolution=320;
        check(f.initialize(2)&&read<int>(f.bytes,0x60)==320,"new natural initialization supports changed target resolution");
    }
    for (int failure=1;failure<=5;++failure) {
        Fixture f;f.version=1;check(f.prepare(),"natural recovery fixture prepares");
        f.fail_allocation=failure<=3?failure:0;f.callback_result=failure==4?0:1;f.throw_after=failure==5;
        check(!f.refresh.Rebuild(f.admission),"natural recovery receives interrupted generation");
        f.fail_allocation=0;f.callback_result=1;f.throw_after=false;f.version=0;
        const auto calls=f.calls;
        check(f.initialize(),"next original reset recovers interrupted directory and applies current base view");
        check(f.calls==calls+2 && !f.refresh.pending() && f.arrays.size()==3,
              "only interrupted transaction uses recovery then new generation, with no retained-array leak");
        check(f.textures.size()==1,"natural failure recovery releases retired scope textures");
    }
    for(int invalid=0;invalid<8;++invalid) {
        Fixture f;f.version=1;NativeInitialization n{1,true,true,true,true,true,640};
        switch(invalid){case 0:n.event=0;break;case 1:n.engine_vidinit=false;break;case 2:n.clear_state_observed=false;break;
        case 3:n.consumers_paused=false;break;case 4:n.resource_view_ready=false;break;case 5:n.previous_callback_unwound=false;break;
        case 6:n.resolution=480;break;case 7:n.resolution=0;break;}
        const auto calls=f.calls,loads=f.loads;
        check(!f.refresh.Initialize(n,f.binding,goldcraft::hud_sprites::resource_owner())&&f.calls==calls&&f.loads==loads,
              "all natural initialization gates reject before original call and directory I/O");
    }
    {
        Fixture f;f.version=1;check(f.prepare()&&f.refresh.Rebuild(f.admission),"different-resolution interrupted case prepares");
        const auto calls=f.calls;f.resolution=320;
        check(!f.initialize()&&f.calls==calls&&f.refresh.pending(),"interrupted recovery never guesses old array sizes at another resolution");
        f.resolution=640;f.version=0;f.callback_result=0;
        check(!f.initialize()&&f.calls==calls+1&&f.refresh.pending(),"failed recovery never runs a second native initialization");
        f.callback_result=1;
        check(f.initialize(2),"later actual reset may recover the retained transaction");
    }
    {
        Fixture f;f.version=1;check(f.prepare(),"prepared cancellation on natural reset stages");f.version=0;
        check(f.initialize()&&f.calls==1,"unused prepared candidate is cancelled without an extra native call");
        f.names[0]="missing";const auto calls=f.calls;
        check(!f.initialize(2)&&f.calls==calls&&!f.refresh.pending(),"invalid base candidate preserves current directory and does not call native");
    }
    {
        Fixture f;f.version=1;check(f.prepare()&&f.refresh.Rebuild(f.admission)&&f.refresh.Commit(f.admission),"base signon case installs overlay");
        ++f.admission.connection;++f.admission.initial_vidinit;f.version=0;
        f.admission.manifest_complete=f.admission.hashes_verified=false;
        check(!f.prepare(),"ordinary server cannot masquerade as a verified manifest");
        f.admission.view_proof=ResourceViewProof::restored_base;
        check(!f.prepare(),"base restoration requires explicit view and dependent-owner verification");
        f.admission.base_view_verified=true;
        check(f.prepare()&&f.refresh.Rebuild(f.admission)&&f.refresh.Commit(f.admission),"verified base view rebuilds within delayed native download gate");
        check(read<int>(f.bytes,0x10)==12,"deferred ordinary-server reset uses actual base catalog length");
        f.admission.state=5;check(!f.prepare(),"base view proof never enables an active-game refresh");
    }
    {
        Fixture f;const auto original=read<client_sprite_t*>(f.bytes,0x0c);const auto loads=f.loads;
        for (const auto address : {std::uintptr_t{1},std::uintptr_t{0xffff0000}}) {
            write(f.bytes,0x0c,reinterpret_cast<client_sprite_t*>(address));
            check(!f.prepare()&&f.loads==loads&&f.calls==0&&f.frees==0,
                  "unknown or dangling catalog is rejected by owner identity without dereference");
        }
        write(f.bytes,0x0c,original);
        check(f.prepare(),"owner remains usable after foreign directory rejection");
        check(f.refresh.Rollback(f.admission),"foreign-directory rejection leaves no owner generation pending");
    }
    {
        Fixture f;f.version=1;const auto list=read<client_sprite_t*>(f.bytes,0x0c);
        const auto array=read<void*>(f.bytes,0x178);const auto loads=f.loads;
        write(f.bytes,0x178,static_cast<void*>(nullptr));
        check(!f.prepare()&&f.loads==loads+1&&!f.refresh.pending(),
              "invalid old array after owner staging aborts the prepared candidate");
        check(engine_sources.empty()&&f.frees==0&&read<client_sprite_t*>(f.bytes,0x0c)==list,
              "failed old-array validation releases copied source without touching old directory or arrays");
        write(f.bytes,0x178,array);
        check(f.prepare()&&f.refresh.Rebuild(f.admission)&&f.refresh.Commit(f.admission),
              "failed post-owner preflight calls finish(false) and permits a later complete update");
    }
    for (bool corrupt_restore : {false, true}) {
        Fixture f; f.version = 1;
        check(f.prepare(), "post-callback owner fixture prepares");
        f.foreign_directory_after = !corrupt_restore;
        const auto rebuilt = f.refresh.Rebuild(f.admission);
        check(bool(rebuilt) == corrupt_restore,
              "rebuild rejects a foreign post-callback directory before reading it");
        f.admission.old_view_restored = true; f.version = 0;
        f.foreign_directory_after = corrupt_restore;
        const auto restored = f.refresh.Rollback(f.admission);
        check(bool(restored) != corrupt_restore,
              "rollback rejects a foreign post-callback directory before reading it");
        if (corrupt_restore) {
            check(f.refresh.pending() && f.frees == 0,
                  "foreign rollback directory retains both array generations");
            f.foreign_directory_after = false;
            check(f.refresh.Rollback(f.admission), "post-callback directory rejection permits safe rollback retry");
        }
        check(!f.refresh.pending(), "post-callback directory owner fully restored");
    }
    check(engine_sources.empty(),"no owned engine SPR directory sources leaked");
    recovery_regressions();
    std::printf("native HUD: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
