// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit MP CreateInterface boundary for the actual AMXX module DLL test.
#define EXT_FUNC
#include <interface.h>
#include "goldcraft/csol_gameplay_settings_api.hpp"
#include <cmath>
#include <cstring>

namespace
{
bool available{}, statusAvailable{true};
int userid{71};
int counters[9]{};
double lastValue{};
void (*onUserid)(){};
class Provider final : public csol::IGameplaySettingsService
{
public:
    bool Status(csol::GameplaySettingsStatus& output) override
    {
        ++counters[0]; output = {};
        output.revision = 17; output.count = 3; output.busy = 1;
        output.result = csol::GameplayEditResult::Queued;
        std::strcpy(output.error, statusAvailable ? "pending controlled save" : "controlled store unavailable");
        return statusAvailable;
    }
    bool Read(int id, csol::GameplaySetting& output) override
    {
        ++counters[1];
        if (id < 0 || id > 2) return false;
        output = {}; output.id = id; output.nextMap = id != 0; output.available = id != 2;
        output.type = id == 0 ? csol::GameplayValueType::Boolean :
            id == 1 ? csol::GameplayValueType::Integer : csol::GameplayValueType::Real;
        output.value = id == 0 ? 1 : id == 1 ? 180 : 0;
        output.minimum = id == 0 ? 0 : id == 1 ? 1 : .001;
        output.maximum = id == 0 ? 1 : id == 1 ? 86400 : 1000000;
        std::strcpy(output.key, id == 0 ? "kick" : id == 1 ? "midday_seconds" : "sprint_maximum");
        std::strcpy(output.comment, "持久配置受控说明");
        return true;
    }
    int UserId(int player) const override
    {
        ++counters[2];
        if (player < 1 || player > 32) return 0;
        const int result = userid;
        const auto callback = onUserid; if (callback) callback();
        return result;
    }
    csol::GameplayEditResult Edit(int actor, int expectedUserid, std::uint32_t revision, int id, double value) override
    {
        ++counters[3]; counters[5] = actor; counters[6] = expectedUserid;
        counters[7] = static_cast<int>(revision); counters[8] = id; lastValue = value;
        if (actor < 1 || actor > 32 || expectedUserid <= 0 || expectedUserid != userid)
            return csol::GameplayEditResult::Unavailable;
        if (!std::isfinite(value)) return csol::GameplayEditResult::Failed;
        if (revision != 17) return csol::GameplayEditResult::Stale;
        if (id < 0 || id > 2) return csol::GameplayEditResult::Failed;
        return csol::GameplayEditResult::Queued;
    }
} provider;
}
extern "C" __declspec(dllexport) IBaseInterface* CreateInterface(const char* name, int*)
{
    ++counters[4];
    return available && name && !std::strcmp(name, csol::GameplaySettingsServiceVersion) ? &provider : nullptr;
}
extern "C" __declspec(dllexport) void SetAvailable(int value) { available = value != 0; }
extern "C" __declspec(dllexport) void SetStatusAvailable(int value) { statusAvailable = value != 0; }
extern "C" __declspec(dllexport) void SetUserId(int value) { userid = value; }
extern "C" __declspec(dllexport) void SetUserIdCallback(void (*callback)()) { onUserid = callback; }
extern "C" __declspec(dllexport) void ResetCounters() { for (auto& count : counters) count = 0; }
extern "C" __declspec(dllexport) int Counter(int index) { return index >= 0 && index < 9 ? counters[index] : -1; }
extern "C" __declspec(dllexport) double LastValue() { return lastValue; }
