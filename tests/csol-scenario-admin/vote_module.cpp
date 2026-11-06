// SPDX-License-Identifier: GPL-3.0-or-later
// Load the actual compiled AMXX module; only its AMXX API/provider are controlled.
#include <Windows.h>
#include <amxxmodule.h>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
int checks{}, failures{};
std::map<std::string, AMX_NATIVE> natives;
void Check(bool ok, const char* text)
{
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; }
}
int AddNatives(const AMX_NATIVE_INFO* list)
{
    for (; list && list->name; ++list) natives[list->name] = list->func;
    return 1;
}
int Unused() { throw std::runtime_error("unexpected AMXX callback"); }
void* Request(const char* name)
{
    return !std::strcmp(name, "AddNatives") ? reinterpret_cast<void*>(&AddNatives) : reinterpret_cast<void*>(&Unused);
}
template<class T> T Function(HMODULE module, const char* name)
{
    auto result = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!result) throw std::runtime_error(std::string("missing export ") + name);
    return result;
}
}
int main(int argc, char** argv)
{
    static_assert(sizeof(void*) == 4 && sizeof(cell) == 4);
    if (argc != 3) return 2;
    try {
        const auto module = LoadLibraryA(argv[1]);
        if (!module) throw std::runtime_error("AMXX module LoadLibrary failed");
        const auto attach = Function<int (*)(PFN_REQ_FNPTR)>(module, "AMXX_Attach");
        const auto detach = Function<int (*)()>(module, "AMXX_Detach");
        Check(attach(Request) == AMXX_OK, "actual module attaches and registers natives");
        auto version = natives.at("csol_scenario_map_vote_api_version");
        auto request = natives.at("csol_scenario_map_vote");
        cell noargs[]{0}, args[]{sizeof(cell), 98}, wrong[]{2 * sizeof(cell), 98, 1};
        Check(version(nullptr, noargs) == 0 && request(nullptr, args) == 0, "absent MP provider is unavailable");
        const auto provider = LoadLibraryA(argv[2]);
        if (!provider) throw std::runtime_error("provider LoadLibrary failed");
        const auto set = Function<void (*)(int)>(provider, "SetAvailable");
        const auto count = Function<int (*)()>(provider, "RequestCount");
        Check(version(nullptr, noargs) == 0, "missing interface is not cached as a successful provider");
        set(1);
        Check(version(nullptr, noargs) == 1, "exact versioned CreateInterface resolves after becoming available");
        Check(version(nullptr, nullptr) == 0 && version(nullptr, args) == 0, "version rejects null and nonzero argument count");
        Check(request(nullptr, nullptr) == 0 && request(nullptr, noargs) == 0 && request(nullptr, wrong) == 0 && count() == 0,
            "request validates exact argument size before calling provider");
        args[1] = 75;
        Check(request(nullptr, args) == 0 && count() == 1, "provider rejects stale map identity through actual native");
        args[1] = 98;
        Check(request(nullptr, args) == 1 && count() == 2, "actual module dispatches the current map request");
        Check(request(nullptr, args) == 0 && count() == 3, "provider rejection is preserved for duplicate request");
        Check(detach() == AMXX_OK, "actual module detaches");
        set(0);
        Check(attach(Request) == AMXX_OK && version(nullptr, noargs) == 0, "detach clears cached map vote interface");
        Check(detach() == AMXX_OK, "second detach succeeds");
        FreeLibrary(module);
        FreeLibrary(provider);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "EXCEPTION " << e.what() << '\n';
    }
    std::cout << "scenario-admin module " << checks << " checks " << failures << " failures\n";
    return failures ? 1 : 0;
}
