// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/settings.hpp"
#include <rapidjson/document.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
thread_local bool observeAllocations{};
thread_local std::size_t allocations{};
int checks{}, failures{};
void Check(bool value, const char* text)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << text << '\n'; }
}
void Require(bool value, const char* text)
{ Check(value, text); if (!value) throw std::runtime_error(text); }
}
void* operator new(std::size_t bytes)
{
    if (observeAllocations) ++allocations;
    if (auto* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
namespace settings = csol::scenario::custom;
using namespace settings;

std::string Document(std::string_view features = {}, std::string_view values = {},
    std::string_view comments = R"({"features":{},"values":{}})")
{
    return "{\"version\":1,\"features\":{" + std::string(features) +
        "},\"values\":{" + std::string(values) + "},\"comments\":" + std::string(comments) + "}";
}
SnapshotPtr Read(std::string_view text)
{
    SnapshotPtr output; Error error;
    if (!Parse(text, output, error))
        throw std::runtime_error(error.field + ": " + error.message);
    return output;
}
void Reject(std::string_view text, const char* reason)
{
    auto old = Read(Document()); auto output = old; Error error;
    Check(!Parse(text, output, error), reason);
    Check(output == old && !error.message.empty(), "parse rejection preserves immutable published snapshot and diagnostic");
}
void Defaults()
{
    auto value = Read(Document());
    Check(value && value->Enabled(Feature::Kick), "kick defaults enabled");
    for (const auto& descriptor : Features())
    {
        Check(value->Enabled(descriptor.id) == descriptor.defaultValue, "feature getter matches documented transition default");
        Check(descriptor.id == Feature::Kick || !descriptor.defaultValue, "new incomplete feature defaults disabled");
        Check(!descriptor.key.empty() && descriptor.key.size() <= 63 && descriptor.cvar.starts_with("csol_custom_"), "feature descriptor has bounded key and uniform cvar namespace");
        Check(FindFeature(descriptor.key) == descriptor.id, "feature key finds stable typed identity");
        Check(!value->Comment(descriptor.id).empty(), "default feature explanation is retained");
    }
    Check(value->Integer(Setting::MiddaySeconds) == 180 && value->Integer(Setting::DuskSeconds) == 120,
        "authorized phase defaults are 180 and 120 seconds");
    Check(value->Integer(Setting::MasteryThreshold1) == 100 && value->Integer(Setting::MasteryThreshold2) == 200 &&
        value->Integer(Setting::MasteryThreshold3) == 300, "authorized mastery thresholds preserved");
    Check(value->Number(Setting::MasteryReload1) == .85 && value->Integer(Setting::DamageRefundMaximumPerMonster) == 1000 &&
        value->Comment(Setting::MasteryReload1).find("GoldCraft自定义默认") != std::string_view::npos,
        "explicit custom balance defaults retain their provenance");
    Check(!value->Number(Setting::SprintMaximum), "unverified CoF base stamina remains absent");
    for (const auto& descriptor : Settings())
    {
        Check(!descriptor.key.empty() && descriptor.key.size() <= 63 && descriptor.cvar.starts_with("csol_custom_"), "numeric descriptor has bounded key and matching cvar namespace");
        Check(std::isfinite(descriptor.minimum) && std::isfinite(descriptor.maximum) && descriptor.minimum <= descriptor.maximum,
            "every numeric menu range is finite and ordered");
        Check(FindSetting(descriptor.key) == descriptor.id, "numeric key finds stable typed identity");
        Check(!value->Comment(descriptor.id).empty(), "default numeric explanation is retained");
    }
    Check(!FindFeature("time_cycle;quit") && !FindSetting("../../config"), "unknown key/path/command strings are not identities");
    Check(!value->Enabled(static_cast<Feature>(255)) && !value->Number(static_cast<Setting>(255)), "invalid enum reads are bounded");
}
void InputValidation()
{
    Reject("", "empty JSON rejected");
    Reject(std::string(MaximumBytes + 1, ' '), "oversized JSON rejected");
    Reject(std::string("{\"version\":1,\0}", 16), "embedded NUL rejected");
    Reject("{}", "required envelope rejected when missing");
    Reject(R"({"version":1,"features":{},"values":{}})", "persistent comments object is required");
    Reject(R"({"version":2,"features":{},"values":{},"comments":{"features":{},"values":{}}})", "unsupported version rejected");
    Reject(R"({"version":1.0,"features":{},"values":{},"comments":{"features":{},"values":{}}})", "floating version does not truncate to integer");
    Reject(R"({"version":1,"version":1,"features":{},"values":{},"comments":{"features":{},"values":{}}})", "duplicate root field rejected");
    Reject(Document(R"("kick":true,"kick":false)"), "duplicate feature rejected");
    Reject(Document(R"("kick":1)"), "numeric boolean rejected");
    Reject(Document(R"("bogus":true)"), "unknown feature rejected");
    Reject(Document({}, R"("midday_seconds":180,"midday_seconds":181)"), "duplicate numeric field rejected");
    Reject(Document({}, R"("midday_seconds":1.5)"), "fractional integer rejected");
    Reject(Document({}, R"("midday_seconds":true)"), "boolean numeric field rejected");
    Reject(Document({}, R"("midday_seconds":"180")"), "string numeric field rejected");
    Reject(Document({}, R"("midday_seconds":null)"), "null does not erase optional numeric field");
    Reject(Document({}, R"("midday_seconds":1e309)"), "overflowing real rejected");
    Reject(Document({}, R"("midday_seconds":NaN)"), "NaN rejected");
    Reject(Document({}, R"("midday_seconds":Infinity)"), "infinite value rejected");
    Reject(Document({}, R"("horde_probability":1.01)"), "probability cannot exceed one");
    Reject(Document({}, R"("unknown_value":1)"), "unknown numeric setting rejected");
    Reject(Document({}, {}, R"({"features":{"kick":"a","kick":"b"},"values":{}})"), "duplicate explanatory key rejected");
    Reject(Document({}, {}, R"({"features":{"bogus":"a"},"values":{}})"), "unknown explanatory identity rejected");
    Reject(Document({}, {}, R"({"features":{"kick":3},"values":{}})"), "numeric explanation rejected");
    Reject(Document({}, {}, R"({"features":{},"values":{},"other":{}})"), "unknown comments container rejected");
    std::string invalidUtf8 = Document(); invalidUtf8.insert(1, std::string(1, char(0xff)));
    Reject(invalidUtf8, "invalid UTF8 JSON rejected");
    Check(Read("//说明\n" + Document(R"("time_cycle":true)") + "\n/*结束*/")->Enabled(Feature::TimeCycle),
        "line and block comments parse while standalone time cycle uses authorized defaults");
}
void RoundTrip()
{
    const auto source = Read(Document(R"("kick":false)", R"("blind_box_price_1":10,"blind_box_price_2":20,"blind_box_price_3":30)",
        R"({"features":{"kick":"踢击可再次开启；不是永久删除"},"values":{"blind_box_price_1":"CS金钱\n第一档"}})"));
    const auto serialized = Serialize(*source);
    const auto restored = Read(serialized);
    Check(!restored->Enabled(Feature::Kick) && restored->Integer(Setting::BlindBoxPrice1) == 10,
        "disabled feature retains authored numeric parameters across serialization");
    Check(restored->Comment(Feature::Kick) == "踢击可再次开启；不是永久删除" &&
        restored->Comment(Setting::BlindBoxPrice1) == "CS金钱\n第一档", "persistent Chinese explanatory strings roundtrip exactly");
    Draft edit(*restored); Error error;
    Require(edit.Set(Feature::Kick, true, error), "typed feature can be re-enabled");
    Require(edit.Set(Feature::BlindBox, true, error), "blind box draft activation accepted pending whole validation");
    SnapshotPtr next; Require(edit.Finish(next, error), "complete increasing three-price draft publishes");
    Check(next->Enabled(Feature::Kick) && next->Enabled(Feature::BlindBox) && !restored->Enabled(Feature::BlindBox),
        "published snapshot is immutable after cold draft edits");
    Require(edit.Set(Feature::BlindBox, false, error), "feature toggle can be disabled again");
    Require(edit.Finish(next, error), "disabled complete draft publishes");
    Check(next->Integer(Setting::BlindBoxPrice3) == 30 && !next->Enabled(Feature::BlindBox), "disable preserves prior price and remains reversible");
}
std::string Grouped(std::string_view groups)
{ return "{\"version\":2,\"features\":{" + std::string(groups) + "}}"; }
void GroupedVersion2()
{
    const auto legacy = Read(Document(R"("kick":false)", R"("blind_box_price_1":10,"blind_box_price_2":20,"blind_box_price_3":30)",
        R"({"features":{"kick":"踢击说明就在开关旁"},"values":{"sprint_maximum":"暂未设置；保留说明","blind_box_price_1":"第一档价格"}})"));
    const auto text = Serialize(*legacy);
    rapidjson::Document document;
    document.Parse(text.data(), text.size());
    Require(!document.HasParseError() && document.IsObject(), "generated single JSON is valid UTF8 JSON");
    Require(document.MemberCount() == 2 && document.HasMember("version") && document["version"].IsInt() &&
        document["version"].GetInt() == 2 && document.HasMember("features") && document["features"].IsObject(),
        "canonical file has only version2 and grouped features root");
    const auto restored = Read(text);
    Check(document["features"].MemberCount() == FeatureCount, "single generated file includes every feature once");
    for (const auto& feature : Features())
    {
        const std::string key(feature.key);
        Require(document["features"].HasMember(key.c_str()), "generated group uses existing stable feature key");
        const auto& group = document["features"][key.c_str()];
        Require(group.IsObject() && group.MemberCount() == 3 && group.HasMember("enabled") &&
            group.HasMember("comment") && group.HasMember("values"), "feature toggle explanation and values are colocated");
        Check(group["enabled"].IsBool() && group["comment"].IsString() && group["values"].IsObject(),
            "group uses editable boolean and adjacent prose without a comments lookup");
        Check(restored->Enabled(feature.id) == legacy->Enabled(feature.id) &&
            restored->Comment(feature.id) == legacy->Comment(feature.id), "version1 feature and Chinese explanation migrate without loss");
        unsigned expected{};
        for (const auto& setting : Settings())
        {
            if (setting.feature != feature.id) continue;
            ++expected;
            const std::string name(setting.key);
            Require(group["values"].HasMember(name.c_str()), "numeric setting is emitted inside its descriptor feature");
            const auto& leaf = group["values"][name.c_str()];
            Check(leaf.IsObject() && leaf.MemberCount() == 2 && leaf.HasMember("value") && leaf.HasMember("comment") &&
                leaf["comment"].IsString(), "numeric value and explanation are adjacent in one object");
            Check(restored->Number(setting.id) == legacy->Number(setting.id) &&
                restored->Comment(setting.id) == legacy->Comment(setting.id), "version1 numbers absence and prose roundtrip through grouped version2");
            Check(legacy->Number(setting.id).has_value() ? leaf["value"].IsNumber() : leaf["value"].IsNull(),
                "optional absence is visible as null without inventing a numeric default");
        }
        Check(group["values"].MemberCount() == expected, "group contains only parameters belonging to this feature");
    }
    Check(text.find("GoldCraft自定义默认") != std::string::npos && text.find("\"comments\"") == std::string::npos,
        "custom-default provenance remains local and no second explanation mapping is emitted");
    Check(Serialize(*restored) == text, "canonical grouped serialization is stable after readback");
    Check(Read(Grouped(""))->Integer(Setting::MiddaySeconds) == 180, "omitted groups keep declared defaults");
    Check(Read(Grouped(R"("time_cycle":{"enabled":true,"comment":"昼夜时长","values":{"midday_seconds":{"value":240,"comment":"正午改为四分钟"}}})"))->Integer(Setting::MiddaySeconds) == 240,
        "direct single-file grouped edit changes its actual typed setting");
    Check(!Read(Grouped(R"("cof_sprint":{"enabled":false,"comment":"体力","values":{"sprint_maximum":{"value":null,"comment":"未填体力"}}})"))->Number(Setting::SprintMaximum),
        "disabled optional null remains absent on readback");
    Reject(R"({"version":2,"features":{},"comments":{}})", "grouped root rejects separate comments mapping");
    Reject(R"({"version":2,"features":{},"values":{}})", "grouped root rejects separate values mapping");
    Reject(R"({"version":2,"version":2,"features":{}})", "grouped duplicate version rejected");
    Reject(R"({"version":2,"features":{},"features":{}})", "grouped duplicate features root rejected");
    Reject(R"({"version":2.0,"features":{}})", "grouped version must be integer2");
    Reject(R"({"version":3,"features":{}})", "future unsupported format rejected");
    Reject(Grouped(R"("unknown":{"enabled":false,"comment":"x","values":{}})"), "unknown grouped feature rejected");
    Reject(Grouped(R"("kick":{"enabled":true,"comment":"x","values":{}},"kick":{"enabled":false,"comment":"y","values":{}})"), "duplicate grouped feature rejected");
    Reject(Grouped(R"("kick":true)"), "flat feature boolean rejected in version2");
    Reject(Grouped(R"("kick":{"enabled":1,"comment":"x","values":{}})"), "grouped numeric enabled rejected");
    Reject(Grouped(R"("kick":{"enabled":true,"comment":1,"values":{}})"), "grouped nonstring feature prose rejected");
    Reject(Grouped(R"("kick":{"enabled":true,"comment":"x","values":[]})"), "grouped values array rejected");
    Reject(Grouped(R"("kick":{"enabled":true,"comment":"x"})"), "present group requires its values container");
    Reject(Grouped(R"("kick":{"enabled":true,"comment":"x","values":{},"other":0})"), "unknown group field rejected");
    Reject(Grouped(R"("kick":{"enabled":true,"enabled":false,"comment":"x","values":{}})"), "duplicate enabled group field rejected");
    Reject(Grouped(R"("kick":{"enabled":true,"comment":"x","values":{"midday_seconds":{"value":180,"comment":"wrong group"}}})"), "known parameter in a different feature group rejected");
    Reject(Grouped(R"("kick":{"enabled":true,"comment":"x","values":{"unknown":{"value":1,"comment":"x"}}})"), "unknown grouped parameter rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":180}})"), "flat grouped numeric value rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":180,"comment":"a"},"midday_seconds":{"value":181,"comment":"b"}}})"), "duplicate grouped parameter rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":180,"value":181,"comment":"a"}}})"), "duplicate numeric leaf field rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":180,"comment":"a","other":0}}})"), "unknown numeric leaf field rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":180}}})"), "present numeric object requires colocated explanation");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":1.5,"comment":"a"}}})"), "grouped fractional integer rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":true,"comment":"a"}}})"), "grouped boolean numeric value rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":null,"comment":"a"}}})"), "null cannot erase a declared numeric default");
    Reject(Grouped(R"("cof_sprint":{"enabled":true,"comment":"x","values":{"sprint_maximum":{"value":null,"comment":"a"}}})"), "enabled grouped feature still rejects missing optional parameters");
    Reject(Grouped(R"("cof_sprint":{"enabled":false,"comment":"x","values":{"sprint_maximum":{"value":"100","comment":"a"}}})"), "grouped numeric string rejected");
    Reject(Grouped(R"("cof_sprint":{"enabled":false,"comment":"x","values":{"sprint_maximum":{"value":100,"comment":false}}})"), "grouped nonstring numeric explanation rejected");
    Reject(Grouped(R"("time_cycle":{"enabled":false,"comment":"x","values":{"midday_seconds":{"value":0,"comment":"a"}}})"), "grouped range validation remains strict");
}
void FeatureRequirements()
{
    for (const auto& descriptor : Features())
    {
        if (descriptor.id != Feature::CofSprint && descriptor.id != Feature::Blood &&
            descriptor.id != Feature::ZombiePopulation && descriptor.id != Feature::RandomHorde) continue;
        const std::string enabled = "\"" + std::string(descriptor.key) + "\":true";
        Reject(Document(enabled), "enabling feature with unknown required parameters is explicit failure");
    }
    Reject(Document(R"("blind_box":true)", R"("blind_box_price_1":30,"blind_box_price_2":20,"blind_box_price_3":40)"),
        "three blind box prices must be strictly increasing");
    Reject(Document({}, R"("mastery_threshold_1":100,"mastery_threshold_2":100)"), "mastery thresholds remain strictly increasing even while disabled");
    Check(Read(Document(R"("damage_refund_cap":true)", R"("damage_refund_max_per_monster":500)"))->Enabled(Feature::DamageRefundCap),
        "explicit per-player-victim cap enables without a fabricated scope field");
    Check(Read(Document(R"("mastery":true)", R"("mastery_reload_1":0.9,"mastery_reload_2":0.8,"mastery_damage_2":1.1,"mastery_damage_3":1.2,"mastery_fire_rate_3":1.1)"))->Enabled(Feature::Mastery),
        "complete exact per-stage mastery fields enable with authored thresholds");
    Reject(Document({}, R"("mastery_multiplier_1":2)"), "obsolete one-multiplier mastery shortcut rejected");
    Reject(Document({}, R"("refund_cap_scope":1)"), "obsolete speculative refund scope rejected");
}
void DraftValidation()
{
    auto old = Read(Document()); Draft draft(*old); Error error;
    Check(!draft.Set(static_cast<Feature>(255), true, error), "invalid feature enum edit rejected");
    Check(!draft.Set(static_cast<Setting>(255), 1, error), "invalid setting enum edit rejected");
    Check(!draft.Set(Setting::MiddaySeconds, 1.5, error), "typed draft integer remains strict");
    Check(!draft.Set(Setting::SprintMaximum, std::numeric_limits<double>::quiet_NaN(), error), "draft rejects NaN");
    Check(!draft.Set(Setting::SprintMaximum, std::numeric_limits<double>::infinity(), error), "draft rejects infinity");
    Check(!draft.SetComment(Feature::Kick, std::string(513, 'x'), error), "oversized explanations rejected");
    Check(!draft.SetComment(Feature::Kick, std::string("x\0y", 3), error), "NUL explanatory text rejected");
    Require(draft.Set(Feature::CofSprint, true, error), "incomplete draft can hold pending feature activation");
    auto target = old;
    Check(!draft.Finish(target, error) && target == old, "whole draft rejection does not replace published snapshot");
    Require(draft.Set(Feature::CofSprint, false, error), "incomplete disabled feature can be retained");
    Require(draft.Finish(target, error), "valid restored draft publishes independently");
    Require(draft.Set(Setting::MiddaySeconds, 50, error), "cold edit changes known default");
    Require(draft.Set(Setting::MiddaySeconds, std::nullopt, error), "clearing known default restores declared default");
    Require(draft.Finish(target, error), "reset default draft publishes");
    Check(target->Integer(Setting::MiddaySeconds) == 180 &&
        Read(Serialize(*target))->Integer(Setting::MiddaySeconds) == 180, "default reset remains stable across reload");
    for (const auto& descriptor : Settings())
    {
        Draft ranges; Error rangeError;
        Check(ranges.Set(descriptor.id, descriptor.minimum, rangeError), "descriptor minimum is accepted by typed edit");
        Check(ranges.Set(descriptor.id, descriptor.maximum, rangeError), "descriptor maximum is accepted by typed edit");
        Check(!ranges.Set(descriptor.id, descriptor.minimum - 1, rangeError), "below descriptor minimum is rejected");
        Check(!ranges.Set(descriptor.id, descriptor.maximum + 1, rangeError), "above descriptor maximum is rejected");
    }
    Draft prose; const std::string maximumEscaping(512, '"');
    for (const auto& descriptor : Features()) Require(prose.SetComment(descriptor.id, maximumEscaping, error), "maximum feature explanation accepted");
    for (const auto& descriptor : Settings()) Require(prose.SetComment(descriptor.id, maximumEscaping, error), "maximum setting explanation accepted");
    Require(prose.Finish(target, error), "bounded complete comments snapshot publishes");
    const auto text = Serialize(*target);
    Check(text.size() <= MaximumBytes && Read(text)->Comment(Feature::Kick) == maximumEscaping,
        "worst-case escaping of every allowed explanation still fits parser byte cap and roundtrips");
}
void HotReads()
{
    const auto value = Read(Document());
    allocations = 0; observeAllocations = true;
    std::uint64_t sum{};
    for (unsigned i = 0; i < 100000; ++i)
    {
        sum += value->Enabled(Feature::Kick);
        sum += static_cast<std::uint64_t>(*value->Integer(Setting::MiddaySeconds));
        sum += value->Number(Setting::SprintMaximum).has_value();
        sum += value->Comment(Feature::Kick).size();
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    observeAllocations = false;
    Check(sum > 0 && allocations == 0, "published fixed-array hot reads allocate nothing (not live performance acceptance)");
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--example")
        {
            Draft defaults; SnapshotPtr value; Error error;
            if (!defaults.Finish(value, error)) throw std::runtime_error(error.message);
            std::cout << Serialize(*value); return 0;
        }
        Defaults(); InputValidation(); RoundTrip(); FeatureRequirements(); DraftValidation(); HotReads(); GroupedVersion2();
    }
    catch (const std::exception& error) { ++failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
