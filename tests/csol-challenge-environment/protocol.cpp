// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/environment_config.hpp"
#include "../../native/csol/scenario/custom/fog_owner.hpp"
#include "../../native/csol/scenario/custom/sky_lighting.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace custom = csol::scenario::custom;
namespace
{
unsigned checks{};
void Check(bool value, const char* why)
{ ++checks; if (!value) { std::printf("FAIL %s\n", why); std::exit(1); } }
}
int main()
{
    custom::Draft draft; custom::Error error; custom::SnapshotPtr settings;
    Check(draft.Set(custom::Feature::TimeCycle, true, error) && draft.Finish(settings, error), "existing grouped configuration enables time defaults");
    const auto config = custom::ReadEnvironmentConfig(*settings);
    Check(config && config->dusk == 'i' && config->night == 'b' && config->stepSeconds == 2.5f, "requested darker night with recovered cadence");
    Check(std::bit_cast<std::uint32_t>(config->fog.density) == 0x3a378034 && config->fog.color == std::array<std::uint8_t, 3>{1, 1, 1}, "exact original fog bits and unnormalized RGB");
    const auto json = custom::Serialize(*settings);
    custom::SnapshotPtr roundtrip;
    Check(custom::Parse(json, roundtrip, error) && custom::ReadEnvironmentConfig(*roundtrip)->fog == config->fog, "one-file grouped JSON roundtrip");
    custom::Draft invalid(*settings);
    Check(invalid.Set(custom::Setting::NightLightLevel, 106, error) && !invalid.Finish(roundtrip, error), "night cannot be brighter than dusk");
    invalid = custom::Draft(*settings);
    Check(invalid.Set(custom::Setting::NightFogStart, 2000, error) && !invalid.Finish(roundtrip, error), "fog interval must be ordered");
    custom::EnvironmentFade fade;
    Check(!fade.Advance(1, custom::Phase::Noon, 0), "uninitialized fade cannot run");
    Check(fade.Begin(1, *config), "begin actual fade");
    Check(!fade.Advance(2, custom::Phase::Noon, 0), "foreign challenge cannot advance");
    Check(fade.Advance(1, custom::Phase::Noon, 0) == custom::EnvironmentView{}, "noon preserves map baseline");
    Check(fade.Advance(1, custom::Phase::Dusk, 180) == custom::EnvironmentView{true, false, 'i'}, "dusk applies the recovered i level");
    Check(!fade.Advance(1, custom::Phase::Noon, 180), "same challenge cannot rewind");
    Check(fade.Advance(1, custom::Phase::Night, 300) == custom::EnvironmentView{true, false, 'i'}, "night starts its transition at the dusk level");
    Check(fade.Advance(1, custom::Phase::Night, 302.5f)->level == 'i', "strict original deadline equality does not step");
    Check(fade.Advance(1, custom::Phase::Night, 302.51f)->level == 'h', "first accepted step darkens exactly once");
    Check(fade.Advance(1, custom::Phase::Night, 1000)->level == 'g', "long stall does not catch up skipped steps");
    Check(!fade.Advance(1, custom::Phase::Night, 999), "time reversal cannot mutate state");
    for (const auto [now, level] : {std::pair{1003.f, 'f'}, {1006.f, 'e'}, {1009.f, 'd'}, {1012.f, 'c'}})
        Check(fade.Advance(1, custom::Phase::Night, now)->level == level, "bounded original light sequence reaches terminal level");
    const auto final = fade.Advance(1, custom::Phase::Night, 1015);
    Check(final->level == 'b' && final->fog, "requested darker terminal level owns fog");
    Check(fade.Advance(1, custom::Phase::Ended, 1020) == custom::EnvironmentView{}, "end restores map presentation");
    Check(!fade.Advance(1, custom::Phase::Night, 1021), "ended owner cannot restart");
    Check(fade.Begin(2, *config) && fade.Advance(2, custom::Phase::Noon, 1021) == custom::EnvironmentView{}, "committed replay gets a fresh noon");
    auto malformed = *config; malformed.stepSeconds = std::numeric_limits<float>::quiet_NaN();
    Check(!fade.Begin(3, malformed), "invalid config preserves prior owner");
    custom::EnvironmentSnapshot snapshot{{true, true, 'c'}, config->fog, 8, 3};
    const auto wire = custom::EncodeEnvironment(snapshot);
    Check(wire && wire->size() == 32, "fixed-width environment payload");
    const auto decoded = custom::DecodeEnvironment(*wire);
    Check(decoded && decoded->owner == 8 && decoded->revision == 3 && decoded->profile == config->fog && decoded->view == snapshot.view, "actual wire roundtrip");
    for (unsigned i : {0u, 1u, 2u, 3u, 4u})
    { auto bad = *wire; bad[i] = std::byte{255}; Check(!custom::DecodeEnvironment(bad), "invalid protocol or flag/level rejected"); }
    Check(!custom::DecodeEnvironment(std::span(wire->data(), wire->size() - 1)), "truncated packet rejected");
    custom::EnvironmentPresentation client;
    Check(client.Receive(*wire) && !client.Receive(*wire), "duplicates do not replay an effect");
    snapshot.revision = 4; snapshot.view = {};
    Check(client.Receive(*custom::EncodeEnvironment(snapshot)), "authoritative end accepted");
    snapshot.revision = 5; snapshot.view = {true, true, 'c'};
    Check(!client.Receive(*custom::EncodeEnvironment(snapshot)), "old ended owner cannot return");
    snapshot.owner = 9;
    Check(client.Receive(*custom::EncodeEnvironment(snapshot)), "new replay owner accepted");
    snapshot.revision = 6; snapshot.owner = 8;
    Check(!client.Receive(*custom::EncodeEnvironment(snapshot)), "old challenge cannot overwrite replay");
    custom::FogOwner fog;
    Check(!fog.Desired(client.View(), 0, true), "no fabricated baseline");
    fog.Observe(custom::FogParameters{.001f, 0});
    Check(!fog.Known(), "both original API calls must be observed");
    fog.Observe(custom::FogDraw{{25, 50, 75}, 50, 1500, 1});
    const auto baseline = fog.Baseline();
    Check(fog.Desired(nullptr, 0, true) == baseline, "ordinary server keeps original fog");
    Check(fog.Desired(client.View(), 0, false) == baseline, "view must exist before overriding");
    const auto night = fog.Desired(client.View(), 2, true);
    Check(night && night->parameters.density == config->fog.density && night->parameters.sky == 1 && night->draw.on == 1, "original CSOL fog stays on at waterlevel2");
    Check(fog.Desired(client.View(), 3, true)->draw.on == 0, "fully submerged fog switches off");
    Check(night->draw.color == std::array<float, 3>{1, 1, 1} && night->draw.start == 100 && night->draw.end == 2000, "original unnormalized color and distances retained");
    fog.Observe(custom::FogDraw{{80, 90, 100}, 20, 800, 1});
    Check(fog.Baseline().draw.color[0] == 80 && fog.Desired(nullptr, 0, true)->draw.color[0] == 80, "later external fog retained for restoration");
    fog.Reset(); client.Reset();
    Check(!fog.Known() && !client.View(), "map transition forgets prior baseline and owner");
    Check(custom::ScaleSkyLight({35, 68, 84}, 'c') == custom::SkyColor{6, 11, 14}, "original sky multiplier uses integer channels and truncates");
    Check(custom::ScaleSkyLight({35.9f, 68.9f, 84.9f}, 'b') == custom::SkyColor{3, 5, 7}, "fractional input truncates before recovered scale");
    Check(custom::ScaleSkyLight({35, 68, 84}, 'a') == custom::SkyColor{}, "darkest level has zero sky contribution");
    Check(!custom::ScaleSkyLight({-1, 0, 0}, 'c') && !custom::ScaleSkyLight({0, 0, std::numeric_limits<float>::infinity()}, 'c'), "unsupported sky input is never invented or clamped");
    custom::NumericCvarOwner owner;
    float cvar = 0;
    owner.Force(cvar, "0", 1); Check(cvar == 1, "temporary renderer flag applies");
    owner.Release(cvar, "0"); Check(cvar == 0, "unchanged archived preference restores");
    owner.Force(cvar, "0", 1); owner.Release(cvar, "1"); Check(cvar == 1, "native edit equal to forced value is retained");
    cvar = .5f; owner.Force(cvar, "0", 1); cvar = .25f; owner.Force(cvar, "0", 1); owner.Release(cvar, "0");
    Check(cvar == .25f, "external direct numeric edits survive release");
    std::printf("environment protocol/config: %u checks passed\n", checks);
}
