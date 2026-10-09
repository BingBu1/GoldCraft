#include "input_audit.hpp"
#include <metahook.h>
#include <cvardef.h>
#include <demo_api.h>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ostream>

extern cl_enginefunc_t gEngfuncs;
extern cl_exportfuncs_t gExportfuncs;

namespace goldcraft::input_audit {
namespace {
struct Snapshot {
    std::array<int, 3> mlook{};
    bool have_button = false, have_crosshair = false;
    float crosshair = 0;
    bool focused = false, playback = false, recording = false, console = false;
};
enum class Argument { unavailable, none, key, other };
struct Event {
    std::uint64_t sequence = 0, elapsed_ms = 0;
    const char* source = "sample";
    Argument argument = Argument::unavailable;
    int argc = 0, key = 0;
    Snapshot before{}, after{};
};
void press();
void release();
struct Command {
    const char* name;
    xcommand_t wrapper, original = nullptr;
};
std::array<Command, 2> commands{{{"+mlook", press}, {"-mlook", release}}};
metahook_api_t* api = nullptr;
cvar_t* crosshair = nullptr;
bool attempted = false, enabled = false;
const char* reason = "disabled";
std::array<Event, 32> events{};
std::uint64_t total = 0, sampled_changes = 0;
std::array<std::uint64_t, 2> calls{};
std::chrono::steady_clock::time_point started{};
Snapshot last{};

bool read_button(std::array<int, 3>& value) {
    // The matched public KB_Find ABI returns kbutton_s: two keys and state.
    // No private address, retained client pointer or write to the button.
    const auto* button = gExportfuncs.KB_Find ? gExportfuncs.KB_Find("in_mlook") : nullptr;
    if (!button) return false;
    std::memcpy(value.data(), button, sizeof(value));
    return true;
}

Snapshot snapshot() {
    Snapshot result;
    result.have_button = read_button(result.mlook);
    result.have_crosshair = crosshair && std::isfinite(crosshair->value);
    if (result.have_crosshair) result.crosshair = crosshair->value;
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    result.focused = foreground == GetCurrentProcessId();
    result.console = gEngfuncs.Con_IsVisible && gEngfuncs.Con_IsVisible();
    if (gEngfuncs.pDemoAPI) {
        result.playback = gEngfuncs.pDemoAPI->IsPlayingback && gEngfuncs.pDemoAPI->IsPlayingback();
        result.recording = gEngfuncs.pDemoAPI->IsRecording && gEngfuncs.pDemoAPI->IsRecording();
    }
    return result;
}

void append(Event event) {
    event.sequence = ++total;
    event.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    events[(total - 1) % events.size()] = event;
}

void observe(const Snapshot& next) {
    if (last.have_button != next.have_button || last.mlook != next.mlook ||
        last.have_crosshair != next.have_crosshair || last.crosshair != next.crosshair) {
        Event event;
        event.before = last;
        event.after = next;
        append(event);
        ++sampled_changes;
    }
    last = next;
}

void read_argument(Event& event) {
    if (!gEngfuncs.Cmd_Argc || !gEngfuncs.Cmd_Argv) return;
    event.argc = gEngfuncs.Cmd_Argc();
    const char* arg = gEngfuncs.Cmd_Argv(1);
    if (!arg) return;
    if (!*arg) { event.argument = Argument::none; return; }
    event.argument = Argument::other;
    // Only retain an ordinary key code, never arbitrary argument text.
    std::size_t size = 0;
    while (size < 12 && arg[size]) ++size;
    if (size == 12) return;
    if (*arg == '+' && (size < 2 || arg[1] < '0' || arg[1] > '9')) return;
    const char* first = arg + (*arg == '+');
    int key = 0;
    const auto parsed = std::from_chars(first, arg + size, key);
    if (parsed.ec == std::errc{} && parsed.ptr == arg + size && key >= -1 && key <= 255) {
        event.argument = Argument::key;
        event.key = key;
    }
}

void invoke(unsigned index) {
    const auto original = commands[index].original;
    if (!enabled) { if (original) original(); return; }
    Event event;
    event.source = commands[index].name;
    event.before = snapshot();
    observe(event.before);
    read_argument(event); // The original handler may change the command context.
    ++calls[index];
    original();
    event.after = snapshot();
    append(event);
    last = event.after;
}
void press() { invoke(0); }
void release() { invoke(1); }

void write_snapshot(std::ostream& out, const Snapshot& value) {
    out << "{\"mlook\":";
    if (value.have_button) out << '[' << value.mlook[0] << ',' << value.mlook[1] << ',' << value.mlook[2] << ']';
    else out << "null";
    out << ",\"crosshair\":";
    if (value.have_crosshair) out << value.crosshair;
    else out << "null";
    out << ",\"focused\":" << (value.focused ? "true" : "false")
        << ",\"playback\":" << (value.playback ? "true" : "false")
        << ",\"recording\":" << (value.recording ? "true" : "false")
        << ",\"console\":" << (value.console ? "true" : "false") << '}';
}
}

int mouse_look_state() {
    std::array<int, 3> value{};
    return read_button(value) ? value[2] : -1;
}

bool install(metahook_api_t* value, bool requested) {
    if (!requested || attempted) return enabled;
    attempted = true;
    // HUD_Init has registered both commands before the caller gets here.
    if (!value || !value->FindCmd || !value->HookCmd || !gExportfuncs.KB_Find) {
        reason = "missing_api";
        return false;
    }
    for (auto& command : commands) {
        const auto* found = value->FindCmd(command.name);
        if (!found || !found->function || found->function == command.wrapper) {
            reason = "missing_command";
            return false;
        }
        command.original = found->function;
    }
    api = value;
    for (auto& command : commands) {
        const auto previous = api->HookCmd(command.name, command.wrapper);
        if (!previous) { shutdown(); reason = "hook_failed"; return false; }
        command.original = previous;
    }
    crosshair = gEngfuncs.pfnGetCvarPointer ? gEngfuncs.pfnGetCvarPointer("crosshair") : nullptr;
    started = std::chrono::steady_clock::now();
    last = snapshot();
    enabled = true;
    reason = "active";
    return true;
}

void shutdown() {
    enabled = false;
    if (api) {
        for (const auto& command : commands) {
            const auto* found = api->FindCmd(command.name);
            if (found && found->function == command.wrapper)
                api->HookCmd(command.name, command.original);
        }
    }
    // A later plugin may still chain through our wrapper. Keep its continuation
    // callable, but never overwrite that plugin's table entry or sample after exit.
    api = nullptr;
    crosshair = nullptr;
    if (attempted) reason = "stopped";
}

void sample() { if (enabled) observe(snapshot()); }

void write_status(std::ostream& out) {
    unsigned direct = 0;
    if (api) for (const auto& command : commands) {
        const auto* found = api->FindCmd(command.name);
        if (found && found->function == command.wrapper) ++direct;
    }
    out << "\"inputAudit\":{\"enabled\":" << (enabled ? "true" : "false")
        << ",\"status\":\"" << reason << "\",\"directCommandHandlers\":" << direct
        << ",\"pressCalls\":" << calls[0] << ",\"releaseCalls\":" << calls[1]
        << ",\"sampledChanges\":" << sampled_changes << ",\"totalEvents\":" << total
        << ",\"overwrittenEvents\":" << (total > events.size() ? total - events.size() : 0)
        << ",\"latest\":";
    write_snapshot(out, last);
    out << ",\"events\":[";
    const auto first = total > events.size() ? total - events.size() : 0;
    constexpr const char* arguments[]{"unavailable", "none", "key", "other"};
    for (auto n = first; n < total; ++n) {
        if (n != first) out << ',';
        const auto& event = events[n % events.size()];
        out << "{\"sequence\":" << event.sequence << ",\"elapsedMs\":" << event.elapsed_ms
            << ",\"source\":\"" << event.source << "\",\"argumentKind\":\""
            << arguments[static_cast<unsigned>(event.argument)] << "\",\"argc\":" << event.argc
            << ",\"key\":";
        if (event.argument == Argument::key) out << event.key;
        else out << "null";
        out << ",\"before\":"; write_snapshot(out, event.before);
        out << ",\"after\":"; write_snapshot(out, event.after);
        out << '}';
    }
    out << "]}";
}
}
