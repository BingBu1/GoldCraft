// SPDX-License-Identifier: GPL-3.0-or-later
// Exact production client TU and matching GoldSrc SDK declarations. Engine
// callback registration/dispatch is controlled; stock graphics is an observer.
#include "native/csol/client/scenario/core/scenario_smoke.cpp"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace csol::client
{
cl_enginefunc_t engine{};
ref_params_t view{};
void SynchronizeSession() {}
}
namespace
{
using Callback = void (*)(event_args_t*);
using namespace csol::client;
constexpr char NativeEvent[] = "events/createsmoke.sc";
constexpr char ScenarioEvent[] = "events/csol_scenario_smoke.sc";
std::map<std::string, Callback> callbacks;
std::vector<event_args_t> nativeCalls;
int checks{}, failures{}, modelLoads{};
float now = 100;
model_t model{};

void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
void Stock(event_args_t* args) { if (args) nativeCalls.push_back(*args); }
void Hook(const char* name, Callback function)
{
    if (!name || !*name || !function) throw std::runtime_error("invalid HookEvent input");
    callbacks[name] = function; // matched engine replaces the same-name callback
}
void Fire(const char* name, event_args_t& event)
{
    const auto found = callbacks.find(name);
    if (found == callbacks.end()) { Check(false, "event has an explicitly registered callback"); return; }
    found->second(&event);
}
std::size_t Active()
{ return std::count_if(particles.begin(), particles.end(), [](const Slot& item) { return item.active; }); }

void NativeSmoke()
{
    const auto count = nativeCalls.size(), particlesBefore = Active();
    const auto eventsBefore = events, invalidBefore = invalidEvents;
    event_args_t detonate{};
    detonate.origin[0] = 12; detonate.origin[1] = 34; detonate.origin[2] = 56;
    detonate.iparam2 = 1; detonate.bparam1 = 1;
    Fire(NativeEvent, detonate);
    event_args_t puff = detonate;
    puff.angles[0] = 4; puff.angles[1] = 5; puff.angles[2] = 6;
    puff.fparam1 = 3.25f; puff.fparam2 = -5.25f;
    puff.iparam1 = 250; puff.iparam2 = 4; puff.bparam2 = 6;
    Fire(NativeEvent, puff);
    Check(nativeCalls.size() == count + 2, "stock detonation 1 and continuing puff 4 both reach native callback");
    if (nativeCalls.size() == count + 2)
    {
        Check(std::memcmp(&nativeCalls[count], &detonate, sizeof(detonate)) == 0,
            "stock detonation event reaches callback without parameter mutation");
        Check(std::memcmp(&nativeCalls[count + 1], &puff, sizeof(puff)) == 0,
            "stock continuing smoke event reaches callback without parameter mutation");
    }
    Check(Active() == particlesBefore && events == eventsBefore && invalidEvents == invalidBefore,
        "native smoke neither emits nor rejects a scenario cloud");
}
}

int main()
{
    using namespace csol::client;
    try
    {
        event_api_t api{};
        engine.pfnHookEvent = Hook;
        engine.GetClientTime = [] { return now; };
        engine.pfnRandomFloat = [](float low, float high) { return low + (high - low) * .25f; };
        engine.pfnRandomLong = [](int, int high) { return high; };
        engine.pfnAngleVectors = [](const float*, float* forward, float* right, float* up)
        {
            std::fill_n(forward, 3, 0); forward[0] = 1;
            std::fill_n(right, 3, 0); right[1] = 1;
            std::fill_n(up, 3, 0); up[2] = 1;
        };
        model.type = mod_sprite; model.numframes = 2;
        engine.CL_LoadModel = [](const char* path, int*) -> model_t*
        {
            ++modelLoads; Check(std::strcmp(path, csol::smoke::Sprite) == 0, "scenario loads its actual sprite path");
            return &model;
        };
        engine.pEventAPI = &api;
        api.EV_SetUpPlayerPrediction = [](int, int) {};
        api.EV_PushPMStates = api.EV_PopPMStates = [] {};
        api.EV_SetTraceHull = [](int) {};
        api.EV_PlayerTrace = [](float*, float* end, int, int, pmtrace_t* result)
        { *result = {}; result->fraction = 1; std::copy_n(end, 3, result->endpos); };

        // The real plugin invokes downstream HUD_Init before this producer.
        Hook(NativeEvent, Stock);
        InitializeScenarioSmoke();
        Check(callbacks[NativeEvent] == Stock, "HUD initialization preserves stock registration");
        Check(callbacks.contains(ScenarioEvent), "scenario registers only its dedicated event name");
        Check(callbacks.size() == 2, "stock and scenario callback records coexist");
        NativeSmoke();

        event_args_t scenario{}; scenario.iparam2 = 1;
        scenario.origin[0] = 32; scenario.origin[1] = 64; scenario.origin[2] = 96;
        const auto stockBefore = nativeCalls.size();
        Fire(ScenarioEvent, scenario);
        Check(Active() == csol::smoke::InitialParticles && events == 1,
            "dedicated event reaches actual cloud producer");
        Check(nativeCalls.size() == stockBefore && modelLoads == 1,
            "scenario cloud does not invoke native smoke callback");
        NativeSmoke();

        for (int reset = 0; reset != 5; ++reset)
        {
            ResetScenarioSmoke();
            Check(Active() == 0 && callbacks[NativeEvent] == Stock,
                "map/session reset clears only scenario particles and preserves native registration");
            NativeSmoke();
        }
        InitializeScenarioSmoke();
        Check(callbacks.size() == 2 && callbacks[NativeEvent] == Stock,
            "repeated HUD initialization does not replace native smoke");
        NativeSmoke();
        ShutdownScenarioSmoke();
        Check(Active() == 0 && callbacks[NativeEvent] == Stock, "shutdown does not alter native callback");
        NativeSmoke();
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "scenario-smoke-routing " << checks << " checks " << failures << " failures\n";
    return failures ? 1 : 0;
}
