// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the production Host against relocated original mapped images.
#include <metahook.h>
#include "../../native/client/resource_native_hud_host.hpp"
#include "../csol-wall-status/surface_stub.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace hud = goldcraft::resource_native_hud;
namespace {
int checks{}, failures{};
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
template<class T> T Load(const std::byte* memory, std::size_t offset) {
    T value; std::memcpy(&value, memory + offset, sizeof value); return value;
}
template<class T> void Store(std::byte* memory, std::size_t offset, T value) {
    std::memcpy(memory + offset, &value, sizeof value);
}
struct Image {
    std::byte* data{};
    std::size_t size{};
    explicit Image(const char* path) {
        std::ifstream file(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        if (bytes.size() < 0x100) throw std::runtime_error("missing mapped PE");
        auto* first = reinterpret_cast<const std::byte*>(bytes.data());
        const auto pe = Load<std::uint32_t>(first, 0x3c), opt = pe + 24;
        if (Load<std::uint32_t>(first, pe) != IMAGE_NT_SIGNATURE ||
            Load<std::uint16_t>(first, pe + 4) != IMAGE_FILE_MACHINE_I386 ||
            Load<std::uint16_t>(first, opt) != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            throw std::runtime_error("fixture must be PE32 x86");
        size = Load<std::uint32_t>(first, opt + 56);
        if (size != bytes.size()) throw std::runtime_error("incomplete mapped image");
        data = static_cast<std::byte*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!data) throw std::runtime_error("cannot allocate relocated fixture");
        std::memcpy(data, bytes.data(), size);
        const auto delta = reinterpret_cast<std::uintptr_t>(data) - Load<std::uint32_t>(data, opt + 28);
        const auto start = Load<std::uint32_t>(data, opt + 96 + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8);
        const auto length = Load<std::uint32_t>(data, opt + 100 + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8);
        if (!start || start > size || length > size - start) throw std::runtime_error("invalid relocation directory");
        std::size_t cursor = start, end = start + length;
        while (cursor < end) {
            if (end - cursor < 8) throw std::runtime_error("truncated relocation block");
            const auto page = Load<std::uint32_t>(data, cursor), count = Load<std::uint32_t>(data, cursor + 4);
            if (count < 8 || count > end - cursor || count % 2) throw std::runtime_error("bad relocation block");
            for (std::size_t offset = 8; offset < count; offset += 2) {
                const auto item = Load<std::uint16_t>(data, cursor + offset);
                if (!(item >> 12)) continue;
                if ((item >> 12) != IMAGE_REL_BASED_HIGHLOW) throw std::runtime_error("unexpected relocation kind");
                const std::size_t target = page + (item & 0xfff);
                if (target > size - 4) throw std::runtime_error("relocation outside image");
                Store(data, target, std::uint32_t(Load<std::uint32_t>(data, target) + delta));
            }
            cursor += count;
        }
    }
    ~Image() { if (data) VirtualFree(data, 0, MEM_RELEASE); }
    void Executable(std::size_t offset, std::size_t bytes) {
        DWORD old{};
        if (!VirtualProtect(data + offset, bytes, PAGE_EXECUTE_READWRITE, &old))
            throw std::runtime_error("cannot execute fixture boundary");
        FlushInstructionCache(GetCurrentProcess(), data + offset, bytes);
    }
};
struct Surface final : vgui::TestSurface {
    std::vector<int> deleted;
    bool DeleteTextureByID(int id) override { deleted.push_back(id); return true; }
};
struct Symbol { const char* name; bool engine; mh_gamesymbol_kind_t kind; std::uint32_t rva; };
constexpr std::array symbols{
    Symbol{"GoldCraftNativeHud_State", false, MH_GAMESYMBOL_KIND_GLOBAL, 0x117b48},
    Symbol{"GoldCraftNativeHud_VidInit", false, MH_GAMESYMBOL_KIND_FUNCTION, 0x47790},
    Symbol{"GoldCraftNativeHud_DeleteArray", false, MH_GAMESYMBOL_KIND_FUNCTION, 0xb4919},
    Symbol{"GoldCraftNativeHud_GetSurface", false, MH_GAMESYMBOL_KIND_FUNCTION, 0x78320},
    Symbol{"GoldCraftNativeHud_SourceFree", true, MH_GAMESYMBOL_KIND_FUNCTION, 0x1da3e0}};
struct Fixture;
Fixture* current{};
struct Fixture {
    Image client, engine;
    metahook_api_t api{};
    Surface surface, replacement;
    DWORD client_size{}, engine_size{};
    bool null_client{}, null_engine{};
    int crc_failure{-1}, crc_wrong{-1}, fault_symbol{-1}, fault_mode{};
    int symbol_calls{}, screen_calls{}, width{800}, screen_result{1};
    int native_calls{}, native_result{1}, alternate_calls{};
    bool replace_during_native{};
    std::vector<void*> arrays, sources;
    Fixture(const char* c, const char* e) : client(c), engine(e) {
        current = this; client_size = DWORD(client.size); engine_size = DWORD(engine.size);
        api.GetClientBase = []()->void* { return current->null_client ? nullptr : current->client.data; };
        api.GetEngineBase = []()->void* { return current->null_engine ? nullptr : current->engine.data; };
        api.GetClientSize = [] { return current->client_size; };
        api.GetEngineSize = [] { return current->engine_size; };
        api.GetModuleCRC64 = [](void* base, std::uint64_t* out) {
            auto& f = *current; const int which = base == f.client.data ? 0 : base == f.engine.data ? 1 : 2;
            Check(which < 2 && out, "CRC API receives owning module and output");
            if (which == f.crc_failure) return MH_GAMESYMBOL_MODULE_HASH_FAILED;
            *out = which == 0 ? 0x124d6034a6b28b40ULL : 0x6ef7192cd8254e2dULL;
            if (which == f.crc_wrong) *out ^= 1;
            return MH_GAMESYMBOL_OK;
        };
        api.ResolveGameSymbol = [](void* base, const char* name, mh_gamesymbol_kind_t kind, void** output) {
            auto& f = *current; ++f.symbol_calls;
            for (std::size_t index = 0; index < symbols.size(); ++index) {
                const auto& symbol = symbols[index];
                if (std::strcmp(name, symbol.name)) continue;
                auto* owner = symbol.engine ? f.engine.data : f.client.data;
                Check(base == owner && kind == symbol.kind && output, "symbol request uses exact module, name and kind");
                if (base != owner) return MH_GAMESYMBOL_MODULE_NOT_FOUND;
                if (kind != symbol.kind) return MH_GAMESYMBOL_KIND_MISMATCH;
                *output = owner + symbol.rva;
                if (int(index) == f.fault_symbol) {
                    switch (f.fault_mode) {
                    case 0: return MH_GAMESYMBOL_MODULE_NOT_FOUND;
                    case 1: return MH_GAMESYMBOL_KIND_MISMATCH;
                    case 2: *output = owner + symbol.rva + 1; break;
                    case 3: *output = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(
                        symbol.engine ? f.client.data : f.engine.data) + symbol.rva); break;
                    case 4: *output = nullptr; break;
                    case 5: return MH_GAMESYMBOL_SYMBOL_NOT_FOUND; // Reject even with populated output.
                    }
                }
                return MH_GAMESYMBOL_OK;
            }
            Check(false, "unexpected symbol requested"); return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
        };
        SetSurface(nullptr);
    }
    void SetSurface(vgui::ISurface* value) { Store(client.data, 0x1355e4, value); }
    static int Screen(SCREENINFO* info) {
        auto& f = *current; ++f.screen_calls;
        Check(info && info->iSize == sizeof *info && info->iWidth == 0 && info->iHeight == 0,
              "screen callback receives initialized real SCREENINFO");
        info->iWidth = f.width; return f.screen_result;
    }
    static int Native() {
        auto& f = *current; ++f.native_calls;
        if (f.replace_during_native) f.SetSurface(&f.replacement);
        return f.native_result;
    }
    static int Alternate() { ++current->alternate_calls; return 1; }
    static void ArrayFree(void* value) noexcept { current->arrays.push_back(value); }
    static void SourceFree(void* value) noexcept { current->sources.push_back(value); }
    void EnableBoundaries() {
        // Keep original getter and two relative delete[] jumps unchanged.
        // Replace only their ultimate imported allocator targets in this copy.
        Check(Load<std::uint16_t>(client.data, 0xb56a4) == 0x25ff,
              "client final deallocator is original indirect jump");
        const auto client_iat = Load<std::uint32_t>(client.data, 0xb56a6);
        Check(client_iat == reinterpret_cast<std::uintptr_t>(client.data) + 0xbf128,
              "client deallocator import operand was actually relocated");
        Store(client.data, 0xbf128, &ArrayFree);
        Store(engine.data, 0x2b242c, &SourceFree);
        client.Executable(0x78320, 6);
        client.Executable(0xb4919, 5);
        client.Executable(0xb54d4, 5);
        client.Executable(0xb56a4, 6);
        engine.Executable(0x1da3e0, 6);
    }
};
void Empty(hud::Host& host) {
    const auto binding = host.GetBinding();
    Check(!host.Entry() && !host.SourceRelease() && !host.OwnerCurrent(), "failed Resolve publishes no owner/address");
    Check(binding.hud.empty() && !binding.context && !binding.delete_array && !binding.vid_init &&
          !binding.delete_texture && !binding.current_resolution, "failed Resolve publishes no partial binding");
}
void ResolveTests(Fixture& f) {
    { hud::Host h; Check(h.Capture(Fixture::Native) != nullptr, "unresolved Capture rejected"); Empty(h); }
    for (int missing = 0; missing < 9; ++missing) {
        hud::Host host; auto api = f.api; auto screen = &Fixture::Screen;
        switch (missing) {
        case 1: api.GetClientBase = nullptr; break; case 2: api.GetEngineBase = nullptr; break;
        case 3: api.GetClientSize = nullptr; break; case 4: api.GetEngineSize = nullptr; break;
        case 5: api.GetModuleCRC64 = nullptr; break; case 6: api.ResolveGameSymbol = nullptr; break;
        case 7: screen = nullptr; break; case 8: api = {}; break;
        }
        Check(host.Resolve(missing ? &api : nullptr, screen) != nullptr, "missing API rejected"); Empty(host);
        Check(!host.Resolve(&f.api, Fixture::Screen), "rejected API leaves retry possible");
    }
    for (int which = 0; which < 2; ++which) for (int fault = 0; fault < 3; ++fault) {
        hud::Host host;
        if (!fault) (which ? f.null_engine : f.null_client) = true;
        if (fault == 1) f.crc_failure = which;
        if (fault == 2) f.crc_wrong = which;
        Check(host.Resolve(&f.api, Fixture::Screen) != nullptr, "module/null/CRC identity rejected"); Empty(host);
        f.null_client = f.null_engine = false; f.crc_failure = f.crc_wrong = -1;
        Check(!host.Resolve(&f.api, Fixture::Screen), "identity rejection leaves retry possible");
    }
    for (int range = 0; range < 4; ++range) {
        hud::Host host;
        if (range == 0) f.client_size = 0;
        if (range == 1) f.client_size = hud::hud_rva + hud::hud_size - 1;
        if (range == 2) f.client_size = 0x1355e4 + sizeof(void*) - 1;
        if (range == 3) f.engine_size = 0x2b2440 - 1;
        Check(host.Resolve(&f.api, Fixture::Screen) != nullptr, "truncated image rejected"); Empty(host);
        f.client_size = DWORD(f.client.size); f.engine_size = DWORD(f.engine.size);
        Check(!host.Resolve(&f.api, Fixture::Screen), "range rejection leaves retry possible");
    }
    const std::array client_changes{0x47790u,0x47791u,0x47795u,0x51104u,0x50424u,0x5042fu,
        0x5043au,0xb4919u,0xb54d4u,0x78320u,0x78321u,0x78325u};
    const std::array engine_changes{0x195cb0u,0x195cbbu,0x1da3e0u,0x1da3e2u};
    for (bool engine : {false, true}) {
        const std::span<const std::uint32_t> changes = engine ?
            std::span<const std::uint32_t>(engine_changes) : std::span<const std::uint32_t>(client_changes);
        auto* data = engine ? f.engine.data : f.client.data;
        for (auto offset : changes) {
            hud::Host host; const auto before = data[offset]; data[offset] ^= std::byte{1};
            Check(host.Resolve(&f.api, Fixture::Screen) != nullptr, "changed verified instruction/operand rejected"); Empty(host);
            data[offset] = before;
            Check(!host.Resolve(&f.api, Fixture::Screen), "instruction rejection leaves retry possible");
        }
    }
    for (int symbol = 0; symbol < int(symbols.size()); ++symbol) for (int fault = 0; fault < 6; ++fault) {
        hud::Host host; f.fault_symbol = symbol; f.fault_mode = fault;
        Check(host.Resolve(&f.api, Fixture::Screen) != nullptr, "bad symbol status/module/kind/address rejected"); Empty(host);
        f.fault_symbol = -1;
        Check(!host.Resolve(&f.api, Fixture::Screen), "symbol rejection leaves retry possible");
    }
}
void BindingTests(Fixture& f) {
    f.EnableBoundaries(); hud::Host host;
    Check(!host.Resolve(&f.api, Fixture::Screen), "relocated exact images resolve");
    Check(host.Entry() == f.client.data + 0x47790 && host.SourceRelease(), "resolve exposes exact entry/source owner");
    Check(host.GetBinding().hud.empty(), "resolved but uncaptured owner has no binding");
    Check(host.Resolve(&f.api, Fixture::Screen) != nullptr, "resolved host cannot be rebound");
    Check(host.Capture(nullptr) != nullptr, "null trampoline rejected");
    Check(host.Capture(reinterpret_cast<hud::Host::VidInit>(host.Entry())) != nullptr, "original entry cannot replace trampoline");
    Check(host.Capture(Fixture::Native) != nullptr && !host.OwnerCurrent(), "uninitialized original surface defers Capture");
    f.SetSurface(&f.surface);
    Check(!host.Capture(Fixture::Native) && host.OwnerCurrent(), "same original owner can be captured after initialization");
    Check(!host.Capture(Fixture::Native), "same owner and trampoline repeated Capture is idempotent");
    Check(host.Capture(Fixture::Alternate) != nullptr, "replacement trampoline rejected");
    auto binding = host.GetBinding();
    Check(binding.hud.data() == f.client.data + hud::hud_rva && binding.hud.size() == hud::hud_size &&
          binding.context == &host, "binding owns actual relocated HUD range");
    for (const auto pair : {std::pair{1,320}, {639,320}, {640,640}, {1920,640}, {320,320}, {0,0}, {-1,0}}) {
        f.width = pair.first; const auto before = f.screen_calls;
        Check(binding.current_resolution(binding.context) == pair.second && f.screen_calls == before + 1,
              "each query reads current screen and applies exact width threshold");
    }
    f.width = 800;
    for (int result : {0, -1}) {
        f.screen_result = result;
        Check(binding.current_resolution(binding.context) == 0, "screen failure cannot produce a resolution");
    }
    f.screen_result = 1;
    for (int result : {1, 0, -7, 23}) {
        f.native_result = result; const auto before = f.native_calls;
        Check(binding.vid_init(binding.context) == result && f.native_calls == before + 1,
              "native trampoline called exactly once and exact result preserved");
    }
    int array_tag{}, source_tag{};
    binding.delete_array(&array_tag); host.SourceRelease()(&source_tag);
    Check(f.arrays == std::vector<void*>{&array_tag} && f.sources == std::vector<void*>{&source_tag},
          "original client jumps and engine thunk reach distinct correct allocator owners");
    binding.delete_texture(binding.context, 37);
    Check(f.surface.deleted == std::vector<int>{37} && f.replacement.deleted.empty(), "texture goes through captured original Surface026");
    f.SetSurface(&f.replacement);
    Check(!host.OwnerCurrent() && host.GetBinding().hud.empty(), "different surface invalidates binding");
    Check(host.Capture(Fixture::Native) != nullptr, "replacement surface cannot inherit captured IDs");
    const auto calls = f.native_calls, screens = f.screen_calls;
    Check(binding.vid_init(binding.context) == 0 && f.native_calls == calls, "stale binding cannot call native on another owner");
    Check(binding.current_resolution(binding.context) == 0 && f.screen_calls == screens, "stale binding cannot report screen success");
    // The release callback deliberately retains its captured owner; callers
    // must check OwnerCurrent before retirement, as documented by Host.
    binding.delete_texture(binding.context, 38);
    Check(f.surface.deleted == std::vector<int>({37,38}) && f.replacement.deleted.empty(), "retained release never switches texture owner");
    f.SetSurface(nullptr); Check(!host.OwnerCurrent() && host.Capture(Fixture::Native) != nullptr, "disappeared owner stays invalid");
    f.SetSurface(&f.surface); Check(!host.Capture(Fixture::Native), "restored original owner remains capturable");
    f.native_result = 1; f.replace_during_native = true;
    Check(binding.vid_init(binding.context) == 0 && f.native_calls == calls + 1 && !host.OwnerCurrent(),
          "owner replaced during native callback cannot force success");
    Check(f.alternate_calls == 0, "rejected alternate trampoline never ran");
    f.replace_during_native = false; f.SetSurface(&f.surface);
}
}
int main(int argc, char** argv) {
    static_assert(sizeof(void*) == 4);
    if (argc != 3) return 2;
    try {
        Fixture fixture(argv[1], argv[2]);
        ResolveTests(fixture); BindingTests(fixture);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "fixture error: %s\n", failure.what()); return 2;
    }
    std::printf("native HUD host: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
