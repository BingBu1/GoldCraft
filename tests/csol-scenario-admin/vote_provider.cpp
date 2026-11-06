// SPDX-License-Identifier: GPL-3.0-or-later
// Controlled CreateInterface boundary for testing the real AMXX module DLL.
#include <cstring>
#define EXT_FUNC
#include <interface.h>
#include "goldcraft/csol_scenario_admin_api.hpp"

namespace {
bool available{}, pending{};
int calls{};
class Provider final : public csol::IScenarioMapVoteService
{
public:
    bool RequestMapVote(int mapId) override
    {
        ++calls;
        if (pending || mapId != 98) return false;
        pending = true;
        return true;
    }
} provider;
}
extern "C" __declspec(dllexport) IBaseInterface* CreateInterface(const char* name, int*)
{
    return available && name && !std::strcmp(name, csol::ScenarioMapVoteServiceVersion) ? &provider : nullptr;
}
extern "C" __declspec(dllexport) void SetAvailable(int value) { available = value != 0; }
extern "C" __declspec(dllexport) int RequestCount() { return calls; }
