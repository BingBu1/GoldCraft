// Compile the production adapters; verify failure atomicity and x86 callbacks.
#include "../../native/client/vitals.cpp"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <limits>

cl_enginefunc_t gEngfuncs{};
namespace {
std::map<std::string, void*> symbols;
std::map<std::string, usermsg_t> messages;
uint64_t identity = 0;
int writes = 0, hooks = 0, advertised = -1;
const unsigned char* cursor;
int hp = 0, armor = 0, spectator = 0, slot = 0;
int byte_read() { return *cursor++; }
int short_read() { short n; std::memcpy(&n, cursor, 2); cursor += 2; return n; }
int long_read() { int n; std::memcpy(&n, cursor, 4); cursor += 4; return n; }
template<unsigned I> int handler(const char*, int, void* bytes) {
    using namespace goldcraft::client_vitals;
    cursor = static_cast<const unsigned char*>(bytes);
    const auto n = I == 1 ? read_armor() : read_health();
    if constexpr (I == 0) hp = n;
    else if constexpr (I == 1) armor = n;
    else spectator = n;
    if constexpr (I == 3) slot = byte_read();
    return 73;
}
std::vector<int> rendered;
struct MockHud { int draw(int x, int, int flags, int value, int, int, int) {
    assert(value < 1000);
    if (value < 10) assert(flags == 1);
    rendered.push_back(value); return x + 12;
}};
MockHud hud;
}

int main() {
    using namespace goldcraft::client_vitals;
    using goldcraft::vitals::integer;
    for (int n : {0, 1, 255, 256, 999, 1000, 32767, 32768, 65535, 65536, 1000000}) {
        assert(integer(float(n), true) == n && integer(float(n), false) == n);
    }
    assert(integer(.5f, true) == 1 && integer(.5f, false) == 0);
    assert(integer(-1, true) == 0 && integer(std::numeric_limits<float>::quiet_NaN(), true) == 0);
    assert(integer(std::numeric_limits<float>::infinity(), true) == INT_MAX);
    assert(integer(float(INT_MAX), true) == INT_MAX);
    assert(goldcraft::vitals::prediction_health(float(INT_MAX)) == 2147483520.0f);
    assert(goldcraft::vitals::prediction_health(65536) == 65536);
    assert(goldcraft::vitals::prediction_health(.5f) == .5f);
    assert(goldcraft::vitals::prediction_health(-1) == 0);
    metahook_api_t api{};
    api.GetClientBase = []() -> void* { return &symbols; };
    api.GetModuleCRC64 = [](void*, uint64_t* n) { *n = identity; return MH_GAMESYMBOL_OK; };
    api.ResolveGameSymbol = [](void*, const char* name, mh_gamesymbol_kind_t, void** out) {
        *out = symbols[name]; return MH_GAMESYMBOL_OK;
    };
    api.GetGameSymbolStatusString = [](mh_gamesymbol_status_t) -> const char* { return "test resolver"; };
    api.WriteDWORD = [](void* p, DWORD n) { ++writes; std::memcpy(p, &n, 4); };
    api.FindUserMsgHook = [](const char* n) -> usermsg_t* { return &messages[n]; };
    api.HookUserMsg = [](const char* n, pfnUserMsgHook hook) {
        ++hooks; return std::exchange(messages[n].function, hook);
    };
    gEngfuncs.pfnRegisterVariable = [](const char*, const char*, int) -> cvar_t* { return nullptr; };
    gEngfuncs.Cvar_SetValue = [](const char*, float v) { advertised = int(v); };
    symbols["GoldCraftVitals_ReadByte"] = reinterpret_cast<void*>(byte_read);
    symbols["GoldCraftVitals_ReadShort"] = reinterpret_cast<void*>(short_read);
    symbols["GoldCraftVitals_ReadLong"] = reinterpret_cast<void*>(long_read);
    void* mock_draw;
    auto method = &MockHud::draw;
    static_assert(sizeof(method) == sizeof(mock_draw));
    std::memcpy(&mock_draw, &method, sizeof(mock_draw));
    symbols["GoldCraftVitals_DrawNumber"] = mock_draw;
    symbols["GoldCraftVitals_Health"] = &hp; symbols["GoldCraftVitals_Armor"] = &armor;
    pfnUserMsgHook native[]{handler<0>, handler<1>, handler<2>, handler<3>};
    const char* suffix[]{"HealthMessage", "ArmorMessage", "SpecHealthMessage", "SpecHealth2Message"};
    for (unsigned i = 0; i < 4; ++i) {
        symbols[std::string("GoldCraftVitals_") + suffix[i]] = reinterpret_cast<void*>(native[i]);
        messages[names[i]].function = native[i];
    }
    std::array<std::array<byte, 5>, 6> calls{};
    void* targets[]{reinterpret_cast<void*>(byte_read), reinterpret_cast<void*>(short_read),
                    reinterpret_cast<void*>(byte_read), reinterpret_cast<void*>(byte_read),
                    mock_draw, mock_draw};
    const char* patches[]{"HealthRead", "ArmorRead", "SpecHealthRead", "SpecHealth2Read", "HealthDraw", "ArmorDraw"};
    for (unsigned i = 0; i < 6; ++i) {
        calls[i][0] = 0xe8;
        DWORD rel = DWORD(targets[i]) - DWORD(calls[i].data()) - 5;
        std::memcpy(calls[i].data() + 1, &rel, 4);
        symbols[std::string("GoldCraftVitals_") + patches[i]] = calls[i].data();
    }
    assert(!install(&api) && !writes && !hooks && advertised == 0);
    identity = 0x124d6034a6b28b40ULL;
    calls[5][0] = 0x90;
    assert(!install(&api) && !writes && !hooks && advertised == 0);
    calls[5][0] = 0xe8;
    messages["Health"].function = nullptr;
    assert(!install(&api) && !writes && !hooks && advertised == 0);
    messages["Health"].function = native[0];
    assert(install(&api) && writes == 6 && hooks == 4 && advertised == 1);
    assert(install(&api) && writes == 6 && hooks == 4);
    for (int n : {1, 255, 256, 999, 1000, 32768, 65536, 1000000, INT_MAX}) {
        unsigned char bytes[5]; std::memcpy(bytes, &n, 4); bytes[4] = 32;
        for (int i = 0; i < 4; ++i) {
            assert(messages[names[i]].function(names[i], i == 3 ? 5 : 4, bytes) == 73);
            assert(!wide_read);
        }
        if (hp != n || armor != n || spectator != n || slot != 32)
            std::cerr << "expected=" << n << " hp=" << hp << " armor=" << armor << " spec=" << spectator << " slot=" << slot << '\n';
        assert(hp == n && armor == n && spectator == n && slot == 32);
        rendered.clear();
        int end = draw_health(&hud, nullptr, 10, 20, 1, n, 255, 200, 10);
        if (n < 1000) assert(rendered.size() == 1 && rendered[0] == n && end == 22);
        else {
            std::string actual; for (auto digit : rendered) actual += char('0' + digit);
            assert(actual == std::to_string(n) && end == 10 + int(actual.size()) * 12);
        }
    }
    unsigned char bytes[]{255, 127, 0, 128, 0};
    assert(message<0>("Health", 1, bytes) == 73 && hp == 255);
    assert(message<1>("Battery", 2, bytes) == 73 && armor == 32767);
    bytes[1] = 1;
    assert(message<3>("SpecHealth2", 2, bytes) == 73 && spectator == 255 && slot == 1);
    const auto prior = hp;
    for (int size : {-1, 0, 2, 3, 5, 6}) message<0>("Health", size, bytes);
    message<0>("Health", 4, bytes); // Negative LONG.
    message<0>("Health", 1, nullptr);
    message<3>("SpecHealth2", 5, bytes); // Invalid target.
    assert(rejected == 9 && hp == prior);
    wide_read = true;
    message<0>("Health", 1, bytes);
    assert(wide_read); // Nested callback restores the caller's scalar width.
    reset(); assert(!wide_read && !rejected && drawn_health == -1 && health_draws == 0);
    std::cout << "Vitals: quantization, atomic preflight, legacy/LONG parsing, malformed rejection, x86 draw ABI and decimal digits passed\n";
}
