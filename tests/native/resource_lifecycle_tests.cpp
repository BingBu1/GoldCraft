// SPDX-License-Identifier: GPL-3.0-or-later
// Actual lifecycle/Host TUs plus the real precache clear/disconnect wrappers.
// Each scenario runs in a fresh process; production one-shot state is never reset.
#include "../../native/client/precache_client.cpp"
#include "../../native/client/resource_lifecycle.hpp"
#include "../csol-wall-status/surface_stub.hpp"
#include <bit>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <setjmp.h>

cl_enginefunc_t gEngfuncs{};
namespace life = goldcraft::resource_lifecycle;
namespace hud = goldcraft::resource_native_hud;
namespace pc = goldcraft::client_precache;
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
// Same mapped-original/relocation boundary used by resource_native_hud_host_tests.
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
        if (!data) throw std::runtime_error("cannot allocate mapped fixture");
        std::memcpy(data, bytes.data(), size);
        const auto delta = reinterpret_cast<std::uintptr_t>(data) - Load<std::uint32_t>(data, opt + 28);
        const auto start = Load<std::uint32_t>(data, opt + 96 + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8);
        const auto length = Load<std::uint32_t>(data, opt + 100 + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8);
        if (!start || start > size || length > size - start) throw std::runtime_error("bad relocation directory");
        for (std::size_t cursor = start; cursor < start + length;) {
            const auto page = Load<std::uint32_t>(data, cursor), count = Load<std::uint32_t>(data, cursor + 4);
            if (count < 8 || count > start + length - cursor || count % 2) throw std::runtime_error("bad relocation block");
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
    void EnableGetter() {
        DWORD old{};
        if (!VirtualProtect(data + 0x78320, 6, PAGE_EXECUTE_READWRITE, &old))
            throw std::runtime_error("cannot execute original Surface getter");
        FlushInstructionCache(GetCurrentProcess(), data + 0x78320, 6);
    }
};
struct Surface final : vgui::TestSurface {
    std::vector<int> deleted;
    bool DeleteTextureByID(int id) override { deleted.push_back(id); return true; }
};
using Frame = int (__cdecl*)(float, int, int*);
enum class Action { none, natural, supplement, nested_frame };
enum class NativeMode { normal, abort_frame, abort_outside, nested_export, swap_owner };
enum class HandlerMode { once, twice, fail_after, fail_before, positive_without_native };
struct Fixture;
Fixture* test{};
struct Fixture {
    Image client, engine;
    metahook_api_t api{};
    Surface surface, replacement;
    Frame frame{};
    hud::Host::VidInit entry{};
    void **frame_original_slot{}, **native_original_slot{};
    unsigned hooks{}, unhooks{}, hook_failure{};
    bool deferred{true}, remove_success{true}, wrong_frame{}, original_clear_jumps{}, disconnect_clears{};
    unsigned native_calls{}, native_after{}, handler_calls{}, frame_calls{}, frame_depth{}, frames_normal{}, jumped{};
    unsigned clear_calls{}, cancel_calls{}, reset_calls{}, array_frees{};
    int native_result{17}, frame_result{-191}, output_value{0x12345678}, nested_result{}, width{1024};
    int consistency{}, force{};
    std::uint32_t time_bits{};
    int state_seen{};
    int* pointer_seen{};
    Action action{Action::none};
    NativeMode native_mode{NativeMode::normal};
    HandlerMode handler_mode{HandlerMode::once};
    hud::Binding saved;
    life::Entry latest{}, first{};
    std::vector<life::Entry> entries;
    std::vector<life::Observation> natives;
    life::Observation before_clear{}, during_clear{}, after_clear{}, cancel_observation{}, inside_jump{};
    jmp_buf frame_jump, outside_jump;

    Fixture(const char* c, const char* e) : client(c), engine(e) {
        test = this;
        api.GetClientBase = []()->void* { return test->client.data; };
        api.GetEngineBase = []()->void* { return test->engine.data; };
        api.GetClientSize = [] { return DWORD(test->client.size); };
        api.GetEngineSize = [] { return DWORD(test->engine.size); };
        api.GetModuleCRC64 = [](void* base, std::uint64_t* value) {
            Check(base == test->client.data || base == test->engine.data, "identity uses owning image");
            *value = base == test->client.data ? 0x124d6034a6b28b40ULL : 0x6ef7192cd8254e2dULL;
            return MH_GAMESYMBOL_OK;
        };
        api.ResolveGameSymbol = [](void* base, const char* name, mh_gamesymbol_kind_t kind, void** output) {
            struct Symbol { const char* name; bool engine; mh_gamesymbol_kind_t kind; unsigned rva; };
            constexpr Symbol symbols[]{
                {"GoldCraftNativeHud_State",false,MH_GAMESYMBOL_KIND_GLOBAL,0x117b48},
                {"GoldCraftNativeHud_VidInit",false,MH_GAMESYMBOL_KIND_FUNCTION,0x47790},
                {"GoldCraftNativeHud_DeleteArray",false,MH_GAMESYMBOL_KIND_FUNCTION,0xb4919},
                {"GoldCraftNativeHud_GetSurface",false,MH_GAMESYMBOL_KIND_FUNCTION,0x78320},
                {"GoldCraftNativeHud_SourceFree",true,MH_GAMESYMBOL_KIND_FUNCTION,0x1da3e0},
                {"GoldCraftNativeHud_HostFrame",true,MH_GAMESYMBOL_KIND_FUNCTION,0x1d28b0}};
            for (const auto& symbol : symbols) if (!std::strcmp(name, symbol.name)) {
                auto* owner = symbol.engine ? test->engine.data : test->client.data;
                Check(base == owner && kind == symbol.kind && output, "real API receives exact symbol module and kind");
                *output = owner + (test->wrong_frame && symbol.rva == 0x1d28b0 ? 0x1d43d0 : symbol.rva);
                return MH_GAMESYMBOL_OK;
            }
            Check(false, "unexpected lifecycle symbol"); return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
        };
        api.InlineHook = [](void* original, void* wrapper, void** trampoline)->hook_t* {
            auto& f = *test; ++f.hooks;
            if (f.hooks == f.hook_failure) return nullptr;
            if (original == f.engine.data + 0x1d28b0) {
                f.frame = reinterpret_cast<Frame>(wrapper); f.frame_original_slot = trampoline;
            } else if (original == f.client.data + 0x47790) {
                f.entry = reinterpret_cast<hud::Host::VidInit>(wrapper); f.native_original_slot = trampoline;
            } else Check(false, "hooks only exact outer frame and native export");
            Check(wrapper != original && trampoline, "actual installation provides separate wrapper and trampoline slot");
            if (!f.deferred) *trampoline = original == f.engine.data + 0x1d28b0 ?
                reinterpret_cast<void*>(&OriginalFrame) : reinterpret_cast<void*>(&OriginalNative);
            return reinterpret_cast<hook_t*>(f.hooks);
        };
        api.UnHook = [](hook_t* hook)->BOOL {
            Check(hook == reinterpret_cast<hook_t*>(1), "partial install removes exact first hook");
            ++test->unhooks; return test->remove_success;
        };
        pc::api = &api;
        pc::consistency_count = &consistency; pc::force_consistency = &force;
        pc::clear_client_original = OriginalClear;
        pc::disconnect_original = [] { if (test->disconnect_clears) pc::clear_client(); };
        pc::set_resource_sync({nullptr,nullptr,[] {
            ++test->cancel_calls; test->cancel_observation = life::observe();
        }});
        pc::set_reset_observer([] { ++test->reset_calls; });
        SetSurface(nullptr); client.EnableGetter();
    }
    void SetSurface(vgui::ISurface* pointer) { Store(client.data, 0x1355e4, pointer); }
    static int Screen(SCREENINFO* info) {
        Check(info && info->iSize == sizeof(*info) && info->iWidth == 0, "fresh SDK screen structure");
        info->iWidth = test->width; return 1;
    }
    static int OriginalClear() {
        auto& f = *test; ++f.clear_calls; f.during_clear = life::observe();
        if (f.original_clear_jumps) longjmp(f.outside_jump, 8);
        return -37;
    }
    void Clear() {
        before_clear = life::observe();
        Check(pc::clear_client() == -37, "actual clear wrapper preserves native return");
        after_clear = life::observe();
        Check(during_clear.clear_epoch == before_clear.clear_epoch, "observer never runs before original clear returns");
        Check(after_clear.valid && after_clear.clear_epoch > before_clear.clear_epoch, "returned original clear advances its own epoch");
        Check(cancel_observation.clear_epoch == after_clear.clear_epoch, "new clear observer precedes cancellation/reset callbacks");
    }
    void Install(bool managed = true) {
        Check(!life::observe().valid && !life::failure(), "preinstall observation is inert");
        const auto* failure = managed ? life::install(&api, Screen, {this, Handle}) : life::install(&api, Screen);
        if (failure) throw std::runtime_error(failure);
        Check(hooks == 2 && frame && entry, "actual install hooks outer frame and original export");
        Check(!native_calls && !handler_calls && life::binding().hud.empty(), "deferred install invokes no native code or surface capture");
        *frame_original_slot = reinterpret_cast<void*>(&OriginalFrame);
        *native_original_slot = reinterpret_cast<void*>(&OriginalNative);
        SetSurface(&surface);
        Check(life::observe().valid && !life::observe().clear_epoch, "fresh installed lifecycle has no synthetic clear epoch");
        Check(life::install(&api, Screen) != nullptr && hooks == 2, "installation is one-shot without replacing active hooks");
    }
    static int Handle(void* context, const life::Entry& entry) {
        auto& f = *static_cast<Fixture*>(context); ++f.handler_calls;
        f.latest = entry; if (!f.first.event) f.first = entry; f.entries.push_back(entry);
        Check(entry.event && life::current(entry), "actual handler receives current dispatch token");
        f.saved = life::binding();
        Check(!f.saved.hud.empty() && f.saved.vid_init && f.saved.current_resolution, "managed binding captures original owner");
        if (f.handler_mode == HandlerMode::fail_before) return -81;
        if (f.handler_mode == HandlerMode::positive_without_native) return 7;
        const int first = f.saved.vid_init(f.saved.context);
        if (f.handler_mode == HandlerMode::twice) {
            const auto returned = life::observe();
            Check(returned.completion == life::Completion::returned, "first supplemental call returned before second begins");
            const int second = f.saved.vid_init(f.saved.context);
            Check(second == first && life::observe().callback > returned.callback, "two sequential native calls use distinct legal invocation tokens");
        }
        return f.handler_mode == HandlerMode::fail_after ? -83 : first;
    }
    static int OriginalNative() {
        auto& f = *test; ++f.native_calls;
        f.natives.push_back(life::observe());
        if (f.native_mode == NativeMode::abort_frame || f.native_mode == NativeMode::abort_outside) {
            const auto active = life::observe(); f.Clear();
            Check(f.after_clear.callback == active.callback && f.after_clear.callback_frame == active.callback_frame &&
                  f.after_clear.completion == life::Completion::none, "clear-before-longjmp retains active native invocation without return proof");
            if (f.latest.event) Check(!life::current(f.latest), "clear invalidates old dispatch epoch before longjmp");
            if (f.native_mode == NativeMode::abort_frame) longjmp(f.frame_jump, 7);
            longjmp(f.outside_jump, 9);
        }
        if (f.native_mode == NativeMode::nested_export) {
            f.native_mode = NativeMode::normal;
            f.nested_result = f.entry();
        }
        if (f.native_mode == NativeMode::swap_owner) f.SetSurface(&f.replacement);
        ++f.native_after;
        return f.native_result;
    }
    static int __cdecl OriginalFrame(float time, int state, int* output) {
        auto& f = *test; ++f.frame_calls; ++f.frame_depth;
        f.time_bits = std::bit_cast<std::uint32_t>(time); f.state_seen = state; f.pointer_seen = output;
        // The jump target is in the original outer frame fixture, below the
        // installed wrapper, exactly the boundary this observer must recognize.
        if (setjmp(f.frame_jump) == 0) {
            if (f.frame_depth == 1) switch (f.action) {
            case Action::natural: f.nested_result = f.entry(); break;
            case Action::supplement: f.nested_result = f.saved.vid_init(f.saved.context); break;
            case Action::nested_frame: f.nested_result = f.frame(time, state, output); break;
            default: break;
            }
            ++f.frames_normal;
        } else {
            ++f.jumped; f.inside_jump = life::observe();
        }
        --f.frame_depth;
        if (output) *output = f.output_value;
        return f.frame_result;
    }
    void RunFrame() {
        int output = -1;
        constexpr std::uint32_t bits = 0x3eaaaaab; // non-round float, passed as original 32 bits
        Check(frame(std::bit_cast<float>(bits), -19, &output) == frame_result, "outer wrapper preserves exact integer return");
        Check(time_bits == bits && state_seen == -19 && pointer_seen == &output && output == output_value,
              "outer wrapper preserves float bits state output pointer and native writes");
    }
};

void Basic(Fixture& f, bool managed) {
    f.Install(managed);
    Check(f.entry() == f.native_result, "normal export preserves original return");
    Check(life::observe().completion == life::Completion::returned && !life::observe().initial_vidinit,
          "normal no-frame return is proven but no clear epoch means no admission initialization");
    f.native_result = -101;
    Check(f.entry() == -101, "negative native return is preserved without fallback");
    f.native_result = 17;
    Check(f.frame(std::bit_cast<float>(0x80000000u), -2147483647, nullptr) == f.frame_result &&
          f.time_bits == 0x80000000u && f.state_seen == -2147483647 && !f.pointer_seen,
          "outer wrapper preserves negative zero, signed state, and null output");
    f.Clear();
    if (managed) f.handler_mode = HandlerMode::twice;
    f.action = Action::natural; f.RunFrame();
    const auto complete = life::observe();
    Check(complete.valid && complete.completion == life::Completion::returned && complete.initial_vidinit,
          "successful native call after real clear records initialization");
    if (managed) {
        Check(f.entries.back().new_clear_state && !f.entries.front().new_clear_state, "clear epoch becomes fresh exactly once");
        Check(!life::current(f.latest) && !life::current(f.first), "returned and stale dispatch tokens cannot be reused");
        const auto entry = f.latest; const auto count = f.handler_calls;
        f.saved.vid_init(f.saved.context);
        Check(f.handler_calls == count && !life::current(entry), "supplemental call does not synthesize export entry");
        f.handler_mode = HandlerMode::once; f.entry();
        Check(!f.latest.new_clear_state && f.latest.event > entry.event, "next export cannot reuse consumed clear as new");
        f.width = 300; Check(f.saved.current_resolution(f.saved.context) == 320, "binding forwards fresh small-screen resolution");
        f.width = 1920; Check(f.saved.current_resolution(f.saved.context) == 640, "binding forwards fresh wide-screen resolution");
        f.SetSurface(&f.replacement);
        f.saved.delete_texture(f.saved.context, 41);
        Check(f.surface.deleted == std::vector<int>{41} && f.replacement.deleted.empty(), "retirement uses captured Surface despite new current owner");
        Check(f.saved.vid_init(f.saved.context) == 0 && life::binding().hud.empty(), "changed owner refuses managed native call");
    }
    Check(!life::failure(), "legal sequential paths do not invalidate lifecycle");
}
void Aborted(Fixture& f, bool supplement, bool outside) {
    f.Install(); f.Clear(); f.entry(); // establish real captured binding
    const auto prior_calls = f.native_calls, prior_handlers = f.handler_calls;
    f.native_mode = outside ? NativeMode::abort_outside : NativeMode::abort_frame;
    if (outside) {
        if (setjmp(f.outside_jump) == 0) { f.saved.vid_init(f.saved.context); Check(false, "aborted callback cannot return"); }
        Check(life::observe().callback_frame == 0 && life::observe().completion == life::Completion::none,
              "unowned interrupted supplemental callback has no fabricated frame");
        f.action = Action::none; f.RunFrame();
        Check(life::observe().completion == life::Completion::none && !life::observe().initial_vidinit,
              "later unrelated frame does not certify unowned callback");
        Check(!life::failure(), "absence of proof remains explicit without inventing completion");
        return;
    }
    f.action = supplement ? Action::supplement : Action::natural; f.RunFrame();
    const auto abandoned = life::observe();
    Check(f.native_calls == prior_calls + 1 && f.native_after == prior_calls, "longjmp skipped native after-call path");
    Check(f.handler_calls == prior_handlers + (supplement ? 0 : 1), "supplemental abort is tracked without synthetic handler entry");
    Check(f.jumped == 1 && !f.frames_normal, "jump returns through original outer frame error arm");
    Check(f.inside_jump.completion == life::Completion::none && f.inside_jump.callback == abandoned.callback,
          "setjmp error arm is still before actual outer trampoline return proof");
    Check(abandoned.valid && abandoned.callback_frame && abandoned.completion == life::Completion::frame_returned &&
          !abandoned.initial_vidinit, "only returned owning outer frame proves old callback unwound without success");
    f.native_mode = NativeMode::normal; f.handler_mode = HandlerMode::twice;
    f.action = Action::natural; f.RunFrame();
    Check(f.latest.previous.callback == abandoned.callback && f.latest.previous.completion == life::Completion::frame_returned &&
          f.latest.new_clear_state, "next native entry receives exact prior callback frame proof and fresh clear");
    Check(life::observe().initial_vidinit == f.latest.event && !life::failure(), "two-call recovery dispatch is legal after matching frame return");
}
void ClearWrappers(Fixture& f) {
    f.Install(); f.Clear();
    auto epoch = life::observe().clear_epoch;
    pc::reset_client(); Check(life::observe().clear_epoch == epoch, "plain reset does not report ClearState");
    pc::disconnect(); Check(life::observe().clear_epoch == epoch, "disconnect without actual clear does not report ClearState");
    const auto clear_calls = f.clear_calls, resets = f.reset_calls;
    f.disconnect_clears = true; pc::disconnect();
    Check(f.clear_calls == clear_calls + 1 && f.reset_calls == resets + 2, "disconnect may contain real clear plus independent generic reset");
    Check(life::observe().clear_epoch > epoch && life::observe().clear_epoch == f.cancel_observation.clear_epoch,
          "only nested real clear generates epoch despite two resets");
    epoch = life::observe().clear_epoch;
    const auto before_resets = f.reset_calls, before_cancels = f.cancel_calls;
    f.original_clear_jumps = true;
    if (setjmp(f.outside_jump) == 0) { pc::clear_client(); Check(false, "nonreturned clear cannot resume wrapper"); }
    Check(life::observe().clear_epoch == epoch && f.reset_calls == before_resets && f.cancel_calls == before_cancels,
          "original clear longjmp skips observer and reset callbacks");
}
void HandlerFailure(Fixture& f) {
    f.Install(); f.Clear(); f.handler_mode = HandlerMode::fail_before;
    Check(f.entry() == -81 && !f.native_calls && !life::observe().initial_vidinit, "handler refusal never invokes fallback native");
    f.handler_mode = HandlerMode::positive_without_native;
    Check(f.entry() == 7 && !f.native_calls && !life::observe().initial_vidinit, "positive handler without native is not initialization proof");
    f.handler_mode = HandlerMode::fail_after;
    Check(f.entry() == -83 && f.native_calls == 1 && !life::observe().initial_vidinit,
          "handler failure after one native call never invokes second fallback");
    Check(life::observe().completion == life::Completion::returned && !life::failure(), "failed dispatch still records actual native return");
}
void Invalid(Fixture& f, const std::string& scenario) {
    f.Install(); f.Clear(); f.entry();
    const bool held = scenario.find("held") != std::string::npos;
    Check(life::block_untracked_calls(held), "valid owner can select explicit transaction gate");
    auto calls = f.native_calls;
    if (scenario.starts_with("nested-native")) {
        f.native_mode = NativeMode::nested_export; f.entry();
        Check(f.native_calls == calls + (held ? 1 : 2) && f.nested_result == (held ? 0 : f.native_result),
              "reentered export uses explicit gate rather than handler-presence heuristic");
    } else if (scenario == "nested-frame") {
        f.action = Action::nested_frame; f.RunFrame();
        Check(f.frame_calls == 2, "nested frame preserves original calls while invalidating observation");
    } else if (scenario.starts_with("capture")) {
        f.SetSurface(&f.replacement);
        Check(f.entry() == (held ? 0 : f.native_result) && f.native_calls == calls + (held ? 0 : 1),
              "capture failure passes ordinary native once or refuses held transaction");
    } else if (scenario.starts_with("foreign-thread")) {
        // Worker completion is joined before the main thread reads fixture data.
        const auto worker = CreateThread(nullptr, 0, [](void*)->DWORD { test->nested_result = test->entry(); return 0; }, nullptr, 0, nullptr);
        Check(worker && WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "foreign entry finishes without waiting across native calls");
        if (worker) CloseHandle(worker);
        Check(f.nested_result == (held ? 0 : f.native_result) && f.native_calls == calls + (held ? 0 : 1),
              "foreign-thread export obeys explicit held gate and never calls handler");
    } else if (scenario == "foreign-frame" || scenario == "foreign-clear") {
        f.action = Action::none;
        const bool clear = scenario == "foreign-clear";
        const auto worker = CreateThread(nullptr, 0, [](void* clear)->DWORD {
            if (clear) pc::clear_client();
            else test->RunFrame();
            return 0;
        }, reinterpret_cast<void*>(clear), 0, nullptr);
        Check(worker && WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "foreign frame or clear original finishes");
        if (worker) CloseHandle(worker);
        Check(clear ? f.clear_calls == 2 : f.frame_calls == 1, "foreign observation still preserves original call");
    } else {
        throw std::runtime_error("unknown scenario");
    }
    Check(life::failure() && !life::observe().valid && life::binding().hud.empty(), "nesting or foreign ownership invalidates observations persistently");
    Check(!life::block_untracked_calls(false), "invalid lifecycle cannot release an explicit gate");
    calls = f.native_calls;
    Check(f.entry() == (held ? 0 : f.native_result) && f.native_calls == calls + (held ? 0 : 1),
          "invalid lifecycle retains ordinary passthrough or held refusal on later entries");
}
void InstallFailure(Fixture& f, const std::string& scenario) {
    if (scenario == "wrong-frame") f.wrong_frame = true;
    if (scenario == "frame-bytes") f.engine.data[0x1d28ca] ^= std::byte{1};
    if (scenario == "second-hook" || scenario == "unhook-failure") f.hook_failure = 2;
    if (scenario == "unhook-failure") f.remove_success = false;
    Check(life::install(&f.api, Fixture::Screen) != nullptr && life::failure(), "incomplete installation is rejected");
    Check(!pc::clear_state_observer && !life::observe().valid, "failed installation publishes no clear observer or valid proof");
    if (f.hook_failure) Check(f.hooks == 2 && f.unhooks == 1, "second-hook failure attempts removal of first hook exactly once");
    else Check(f.hooks == 0 && !f.unhooks, "wrong outer address or bytes rejected before mutation");
}
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const std::string scenario = argv[3];
    try {
        Fixture fixture(argv[1], argv[2]);
        if (scenario == "default") Basic(fixture, false);
        else if (scenario == "sequential") Basic(fixture, true);
        else if (scenario == "natural-longjmp") Aborted(fixture, false, false);
        else if (scenario == "supplemental-longjmp") Aborted(fixture, true, false);
        else if (scenario == "unowned-longjmp") Aborted(fixture, true, true);
        else if (scenario == "clear-observer") ClearWrappers(fixture);
        else if (scenario == "handler-failure") HandlerFailure(fixture);
        else if (scenario == "wrong-frame" || scenario == "frame-bytes" || scenario == "second-hook" || scenario == "unhook-failure") InstallFailure(fixture, scenario);
        else Invalid(fixture, scenario);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fixture error: %s\n", error.what()); return 2;
    }
    std::printf("resource lifecycle %s: %d checks, %d failures\n", scenario.c_str(), checks, failures);
    return failures ? 1 : 0;
}
