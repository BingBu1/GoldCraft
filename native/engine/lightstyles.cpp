// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "lightstyles.h"
#include "goldcraft/lightstyle_owner.hpp"
#include "goldcraft/lightstyle_api.hpp"

namespace
{
static_assert(MAX_LIGHTSTYLE_SIZE == goldcraft::LightStylePatternBytes);
goldcraft::LightStyleOwner lights;
bool writing{};

bool CurrentMap()
{
    if (g_psv.state != ss_loading && g_psv.state != ss_active)
    { lights.ForgetMap(); return false; }
    lights.EnterMap(static_cast<std::uint32_t>(g_psvs.spawncount));
    return g_psv.state == ss_active;
}
struct WriteScope
{
    WriteScope() { writing = true; }
    ~WriteScope() { writing = false; }
};
class MapLightStyleService final : public goldcraft::IMapLightStyleService
{
public:
    std::uint64_t Acquire() override
    { return !writing && CurrentMap() ? lights.Acquire() : 0; }
    bool Set(std::uint64_t owner, char level) override
    {
        if (writing || !CurrentMap() || !lights.Owns(owner)) return false;
        if (lights.Applied() && lights.Level()[0] == level) return true;
        if (!lights.Set(owner, level)) return false;
        WriteScope scope; PF_lightstyle_I(0, lights.Level()); return true;
    }
    bool Release(std::uint64_t owner) override
    {
        if (writing || !CurrentMap() || !lights.Owns(owner)) return false;
        const bool restore = lights.Applied();
        if (!lights.Release(owner)) return false;
        if (restore)
        { WriteScope scope; PF_lightstyle_I(0, lights.Baseline()); }
        return true;
    }
    bool Status(goldcraft::MapLightStyleStatus& output) override
    {
        const bool active = CurrentMap();
        output = {}; output.owner = lights.Owner(); output.map = lights.Map();
        output.known = lights.Known(); output.applied = lights.Applied();
        output.level = lights.Level()[0];
        std::memcpy(output.baseline, lights.Baseline(), sizeof(output.baseline));
        return active;
    }
};
}
using goldcraft::IMapLightStyleService;
EXPOSE_SINGLE_INTERFACE(MapLightStyleService, IMapLightStyleService, goldcraft::MapLightStyleServiceVersion);

const char* GoldCraftLightStyleWrite(int style, const char* pattern)
{
    if (style || writing) return pattern;
    CurrentMap();
    return lights.Observe(pattern);
}
