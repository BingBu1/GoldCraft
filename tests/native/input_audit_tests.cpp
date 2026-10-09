// Exercise the actual adapter against a command table with independent handlers.
// These tests make no claim about reproducing the user's original game trigger.
#include "../../native/client/input_audit.cpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

cl_enginefunc_t gEngfuncs{};
cl_exportfuncs_t gExportfuncs{};

namespace {
std::array<int, 3> button{-1, 0, 3};
cvar_t test_crosshair{};
std::array<cmd_function_t, 2> table{};
std::string argument, original_argument;
int argument_count = 1, lookups = 0, hook_calls = 0;
int base_presses = 0, base_releases = 0, earlier_calls = 0, later_calls = 0;
bool fail_second_hook = false, expose_button = true;
xcommand_t later_next = nullptr;

void native_press() {
    ++base_presses;
    original_argument = argument;
    button = {-1, 23, 3};
    // A handler can change command context: the observer must capture it first.
    argument = "changed by the original handler";
}
void native_release() {
    ++base_releases;
    original_argument = argument;
    button = {0, 0, 4};
}
void earlier_press() { ++earlier_calls; native_press(); }
void later_release() { ++later_calls; later_next(); }

void setup(metahook_api_t& engine) {
    table[0].function = earlier_press;
    table[1].function = native_release;
    test_crosshair.value = 1;
    gExportfuncs.KB_Find = [](const char* name) -> kbutton_s* {
        assert(std::strcmp(name, "in_mlook") == 0);
        return expose_button ? reinterpret_cast<kbutton_s*>(button.data()) : nullptr;
    };
    gEngfuncs.Cmd_Argc = [] { return argument_count; };
    gEngfuncs.Cmd_Argv = [](int index) -> char* { assert(index == 1); return argument.data(); };
    gEngfuncs.pfnGetCvarPointer = [](const char* name) -> cvar_t* {
        assert(std::strcmp(name, "crosshair") == 0); return &test_crosshair;
    };
    engine.FindCmd = [](const char* name) -> cmd_function_t* {
        ++lookups;
        if (std::strcmp(name, "+mlook") == 0) return &table[0];
        if (std::strcmp(name, "-mlook") == 0) return &table[1];
        assert(false); return nullptr;
    };
    engine.HookCmd = [](const char* name, xcommand_t next) -> xcommand_t {
        ++hook_calls;
        auto& entry = table[std::strcmp(name, "+mlook") == 0 ? 0 : 1];
        const auto old = std::exchange(entry.function, next);
        return fail_second_hook && hook_calls == 2 ? nullptr : old;
    };
}

void output(const char* scenario) {
    std::cout << "{\"scenario\":\"" << scenario << "\",\"passed\":true,";
    goldcraft::input_audit::write_status(std::cout);
    std::cout << "}\n";
}
}

int main(int argc, char** argv) {
    using namespace goldcraft::input_audit;
    const std::string scenario = argc > 1 ? argv[1] : "normal";
    metahook_api_t engine{};
    setup(engine);
    assert(!install(&engine, false));
    sample();
    assert(lookups == 0 && hook_calls == 0 && mouse_look_state() == 3);
    assert((button == std::array<int, 3>{-1, 0, 3}));

    if (scenario == "missing_api") {
        engine.FindCmd = nullptr;
        assert(!install(&engine, true));
        assert(hook_calls == 0 && table[0].function == earlier_press);
        output(scenario.c_str()); return 0;
    }
    if (scenario == "missing_command") {
        table[1].function = nullptr;
        assert(!install(&engine, true));
        assert(hook_calls == 0 && table[0].function == earlier_press);
        output(scenario.c_str()); return 0;
    }
    if (scenario == "hook_failure") {
        fail_second_hook = true;
        assert(!install(&engine, true));
        assert(!enabled && table[0].function == earlier_press && table[1].function == native_release);
        assert(base_presses == 0 && base_releases == 0);
        const auto before_shutdown = hook_calls;
        shutdown();
        assert(hook_calls == before_shutdown);
        output(scenario.c_str()); return 0;
    }
    if (scenario == "missing_button") {
        expose_button = false;
        assert(install(&engine, true));
        assert(mouse_look_state() == -1);
        table[0].function(); table[1].function();
        assert(base_presses == 1 && base_releases == 1 && !last.have_button);
        shutdown();
        assert(table[0].function == earlier_press && table[1].function == native_release);
        output(scenario.c_str()); return 0;
    }
    assert(scenario == "normal");
    assert(install(&engine, true));
    assert(install(&engine, true) && hook_calls == 2);
    assert(base_presses == 0 && base_releases == 0 && last.crosshair == 1);

    argument = "+59"; argument_count = 2;
    table[0].function();
    assert(base_presses == 1 && earlier_calls == 1 && original_argument == "+59");
    assert(events[0].argument == Argument::key && events[0].key == 59);
    assert((events[0].before.mlook == std::array<int, 3>{-1, 0, 3}));
    assert(events[0].after.mlook == button && test_crosshair.value == 1);
    argument.clear(); argument_count = 1;
    table[1].function();
    assert(base_releases == 1 && original_argument.empty());
    assert(events[1].argument == Argument::none && events[1].after.mlook[2] == 4);

    argument = "private text must never be retained"; argument_count = 2;
    table[1].function();
    assert(original_argument == argument && events[2].argument == Argument::other);
    std::ostringstream redacted;
    write_status(redacted);
    assert(redacted.str().find(argument) == std::string::npos);
    for (const char* value : {"99999", "59suffix", "-2", "+-1", "+", "12345678901234567890"}) {
        argument = value;
        table[1].function();
        assert(events[(total - 1) % events.size()].argument == Argument::other);
    }
    argument = "-1";
    table[0].function();
    assert(events[(total - 1) % events.size()].key == -1);
    const auto known_total = total;
    sample(); sample();
    assert(total == known_total); // Stable frames add no records or writes.

    test_crosshair.value = 0;
    sample();
    const auto& crosshair_event = events[(total - 1) % events.size()];
    assert(total == known_total + 1 && sampled_changes == 1);
    assert(std::strcmp(crosshair_event.source, "sample") == 0);
    assert(crosshair_event.before.crosshair == 1 && crosshair_event.after.crosshair == 0);
    assert(test_crosshair.value == 0); // Observation does not restore a preference.
    button = {0, 0, 4};
    sample();
    assert(sampled_changes == 2 && total == known_total + 2);
    test_crosshair.value = std::numeric_limits<float>::quiet_NaN();
    sample(); sample();
    assert(sampled_changes == 3 && !last.have_crosshair);
    std::ostringstream finite_json;
    write_status(finite_json);
    assert(finite_json.str().find("nan") == std::string::npos);

    argument.clear(); argument_count = 1;
    for (int i = 0; i < 70; ++i) table[1].function();
    assert(total > events.size());
    for (auto sequence = total - events.size() + 1; sequence <= total; ++sequence)
        assert(events[(sequence - 1) % events.size()].sequence == sequence);
    assert(calls[0] == static_cast<unsigned>(base_presses));
    assert(calls[1] == static_cast<unsigned>(base_releases));

    // Preserve a plugin installed later, including its continuation after stop.
    later_next = table[1].function;
    table[1].function = later_release;
    assert(install(&engine, true) && hook_calls == 2);
    const auto releases_before = base_releases;
    table[1].function();
    assert(later_calls == 1 && base_releases == releases_before + 1);
    const auto trace_count = total;
    shutdown();
    assert(table[0].function == earlier_press && table[1].function == later_release);
    const auto lookups_after_stop = lookups;
    gExportfuncs.KB_Find = nullptr; // Teardown must not read a dead client export.
    gEngfuncs.Cmd_Argv = nullptr;
    table[1].function();
    shutdown(); sample();
    assert(later_calls == 2 && base_releases == releases_before + 2);
    assert(total == trace_count && lookups == lookups_after_stop);
    assert(!install(&engine, true) && hook_calls == 3);
    output(scenario.c_str());
}
