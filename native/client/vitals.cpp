#include <metahook.h>
#include <cvardef.h>
#include "vitals.hpp"
#include "goldcraft/vitals_protocol.hpp"
#include <array>
#include <charconv>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

extern cl_enginefunc_t gEngfuncs;

namespace goldcraft::client_vitals {
namespace {
bool enabled = false, wide_read = false;
std::string failure;
int (__cdecl* read_byte)() = nullptr;
int (__cdecl* read_short)() = nullptr;
int (__cdecl* read_long)() = nullptr;
using DrawNumber = int (__thiscall*)(void*, int, int, int, int, int, int, int);
DrawNumber draw_number = nullptr;
int* native_health = nullptr;
int* native_armor = nullptr;
std::array<pfnUserMsgHook, 4> previous{};
std::array<unsigned, 4> wide_messages{}, legacy_messages{};
unsigned rejected = 0;
int drawn_health = -1, drawn_armor = -1;
unsigned health_draws = 0, armor_draws = 0;
constexpr const char* names[]{"Health", "Battery", "SpecHealth", "SpecHealth2"};

int __cdecl read_health() { return wide_read ? read_long() : read_byte(); }
int __cdecl read_armor() { return wide_read ? read_long() : read_short(); }

template<unsigned Index>
int message(const char* name, int size, void* data) {
    const unsigned old_size = Index == 1 || Index == 3 ? 2 : 1;
    if (!data || size < 0 || !vitals::valid_payload(
            static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size), old_size, Index == 3)) {
        ++rejected;
        return 1;
    }
    // Only this message's native scalar read changes width. For SpecHealth2,
    // the following player index remains a byte. Nested callbacks restore it.
    const bool wide = size == (Index == 3 ? 5 : 4);
    const bool saved = std::exchange(wide_read, wide);
    ++(wide ? wide_messages : legacy_messages)[Index];
    const int result = previous[Index](name, size, data);
    wide_read = saved;
    return result;
}

int digits(void* hud, int x, int y, int flags, int value, int r, int g, int b) {
    if (value < 1000) return draw_number(hud, x, y, flags, value, r, g, b);
    // The native routine uses value/100 as a sprite index, so passing four or
    // more digits would read outside its digit sprites. Reuse it one digit at
    // a time, retaining the game's font, colors and fade. DHN_DRAWZERO is 1.
    char text[11];
    const auto end = std::to_chars(text, text + sizeof(text), value).ptr;
    for (auto p = text; p != end; ++p)
        x = draw_number(hud, x, y, 1, *p - '0', r, g, b);
    return x;
}
int __fastcall draw_health(void* hud, void*, int x, int y, int flags, int value, int r, int g, int b) {
    const auto result = digits(hud, x, y, flags, value, r, g, b);
    drawn_health = value; ++health_draws;
    return result;
}
int __fastcall draw_armor(void* hud, void*, int x, int y, int flags, int value, int r, int g, int b) {
    const auto result = digits(hud, x, y, flags, value, r, g, b);
    drawn_armor = value; ++armor_draws;
    return result;
}
}

bool install(metahook_api_t* api) {
    if (enabled) return true;
    try {
        uint64_t crc = 0;
        auto* client = api->GetClientBase();
        if (!client || api->GetModuleCRC64(client, &crc) != MH_GAMESYMBOL_OK || crc != 0x124d6034a6b28b40ULL)
            throw std::runtime_error("integer vitals require the verified CS client");
        auto resolve = [&](const char* name, mh_gamesymbol_kind_t kind) {
            void* value = nullptr;
            const std::string symbol = std::string("GoldCraftVitals_") + name;
            const auto status = api->ResolveGameSymbol(client, symbol.c_str(), kind, &value);
            if (status != MH_GAMESYMBOL_OK || !value)
                throw std::runtime_error(symbol + ": " + api->GetGameSymbolStatusString(status));
            return value;
        };
        read_byte = reinterpret_cast<decltype(read_byte)>(resolve("ReadByte", MH_GAMESYMBOL_KIND_FUNCTION));
        read_short = reinterpret_cast<decltype(read_short)>(resolve("ReadShort", MH_GAMESYMBOL_KIND_FUNCTION));
        read_long = reinterpret_cast<decltype(read_long)>(resolve("ReadLong", MH_GAMESYMBOL_KIND_FUNCTION));
        draw_number = reinterpret_cast<DrawNumber>(resolve("DrawNumber", MH_GAMESYMBOL_KIND_FUNCTION));
        native_health = static_cast<int*>(resolve("Health", MH_GAMESYMBOL_KIND_GLOBAL));
        native_armor = static_cast<int*>(resolve("Armor", MH_GAMESYMBOL_KIND_GLOBAL));
        const char* patches[]{"HealthRead", "ArmorRead", "SpecHealthRead", "SpecHealth2Read", "HealthDraw", "ArmorDraw"};
        void* expected[]{reinterpret_cast<void*>(read_byte), reinterpret_cast<void*>(read_short),
                         reinterpret_cast<void*>(read_byte), reinterpret_cast<void*>(read_byte),
                         reinterpret_cast<void*>(draw_number), reinterpret_cast<void*>(draw_number)};
        void* replacement[]{reinterpret_cast<void*>(read_health), reinterpret_cast<void*>(read_armor),
                            reinterpret_cast<void*>(read_health), reinterpret_cast<void*>(read_health),
                            reinterpret_cast<void*>(draw_health), reinterpret_cast<void*>(draw_armor)};
        std::array<byte*, 6> calls{};
        for (unsigned i = 0; i < calls.size(); ++i) {
            calls[i] = static_cast<byte*>(resolve(patches[i], MH_GAMESYMBOL_KIND_PATCH));
            std::int32_t relative;
            std::memcpy(&relative, calls[i] + 1, sizeof(relative));
            if (*calls[i] != 0xe8 || calls[i] + 5 + relative != expected[i])
                throw std::runtime_error(std::string(patches[i]) + ": native CALL changed");
        }
        const char* native_handlers[]{"HealthMessage", "ArmorMessage", "SpecHealthMessage", "SpecHealth2Message"};
        for (unsigned i = 0; i < previous.size(); ++i) {
            const auto entry = api->FindUserMsgHook(names[i]);
            if (!entry || reinterpret_cast<void*>(entry->function) != resolve(native_handlers[i], MH_GAMESYMBOL_KIND_FUNCTION))
                throw std::runtime_error(std::string(names[i]) + ": native HUD handler changed");
        }
        // Preflight everything before editing any instruction or advertising.
        for (unsigned i = 0; i < calls.size(); ++i)
            api->WriteDWORD(calls[i] + 1, DWORD(replacement[i]) - DWORD(calls[i]) - 5);
        const pfnUserMsgHook handlers[]{message<0>, message<1>, message<2>, message<3>};
        for (unsigned i = 0; i < previous.size(); ++i)
            previous[i] = api->HookUserMsg(names[i], handlers[i]);
        enabled = true;
        failure.clear();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    gEngfuncs.pfnRegisterVariable(vitals::capability, enabled ? vitals::version : "0", FCVAR_USERINFO);
    gEngfuncs.Cvar_SetValue(vitals::capability, enabled ? 1.f : 0.f);
    return enabled;
}

void reset() {
    wide_read = false;
    wide_messages = {}; legacy_messages = {}; rejected = 0;
    drawn_health = drawn_armor = -1; health_draws = armor_draws = 0;
}
const char* error() { return failure.c_str(); }
void write_status(std::ostream& out) {
    out << "\"nativeVitals\":{\"enabled\":" << enabled << ",\"health\":" << (enabled ? *native_health : -1)
        << ",\"armor\":" << (enabled ? *native_armor : -1) << ",\"drawnHealth\":" << drawn_health
        << ",\"drawnArmor\":" << drawn_armor << ",\"healthDraws\":" << health_draws << ",\"armorDraws\":" << armor_draws
        << ",\"rejected\":" << rejected << ",\"wideMessages\":[";
    for (unsigned i = 0; i < 4; ++i) out << (i ? "," : "") << wide_messages[i];
    out << "],\"legacyMessages\":[";
    for (unsigned i = 0; i < 4; ++i) out << (i ? "," : "") << legacy_messages[i];
    out << "],\"error\":" << std::quoted(failure) << '}';
}
}
