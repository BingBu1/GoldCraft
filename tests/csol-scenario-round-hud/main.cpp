// SPDX-License-Identifier: GPL-3.0-or-later
// Production parsers/state/layout are linked separately. No SDK or GL claim.
#include "native/csol/scenario/core/scenario_round_hud.hpp"
#include "native/csol/scenario/core/scenario_localization.hpp"
#include "native/csol/scenario/core/dialogue_resources.hpp"
#include "native/csol/scenario/core/round_clock.hpp"
#include "native/csol/config_codec.hpp"
#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <vector>

namespace hud = csol::scenario_hud;
namespace scenario = csol::scenario;
namespace test
{
unsigned checks{}, failures{};
void Check(bool condition, const char* label, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << label << " line=" << where.line() << '\n'; }
}
std::vector<std::byte> Bytes(std::initializer_list<unsigned> values)
{
    std::vector<std::byte> result;
    for (auto value : values) result.push_back(std::byte(value));
    return result;
}
std::vector<std::byte> Round(unsigned round, unsigned total, float elapsed)
{
    auto result = Bytes({27, 3, round, total});
    for (unsigned shift : {0u, 8u, 16u, 24u}) result.push_back(std::byte(std::bit_cast<std::uint32_t>(elapsed) >> shift));
    return result;
}
std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("fixture input unavailable");
    std::vector<std::byte> result;
    for (char value; file.get(value);) result.push_back(std::byte(static_cast<unsigned char>(value)));
    return result;
}
struct Expected
{
    std::vector<std::byte> data;
    std::size_t at{};
    std::uint32_t Word()
    {
        if (at + 4 > data.size()) throw std::runtime_error("short independent expectation");
        std::uint32_t value{};
        for (unsigned shift : {0u, 8u, 16u, 24u}) value |= std::to_integer<std::uint32_t>(data[at++]) << shift;
        return value;
    }
    std::u16string String()
    {
        const auto size = Word();
        if (size % 2 || size > data.size() - at) throw std::runtime_error("bad independent string");
        std::u16string result;
        for (std::size_t end = at + size; at < end; at += 2)
            result += char16_t(std::to_integer<unsigned>(data[at]) | std::to_integer<unsigned>(data[at + 1]) << 8);
        return result;
    }
};
std::string Ascii(std::u16string_view value)
{
    std::string result;
    for (auto c : value) { if (c > 127) throw std::runtime_error("fixture expected ASCII"); result += char(c); }
    return result;
}
void Protocol()
{
    const hud::Message sentinel{hud::Kind::Round, 31, 251, 252, 17, -9};
    const auto unchanged = [&](const hud::Message& message)
    { return message.kind == sentinel.kind && message.player == 31 && message.value == 251 && message.total == 252 && message.elapsed == 17 && message.secondsOffset == -9; };
    struct Case { std::vector<std::byte> bytes; hud::Kind kind; unsigned player, value, total; float elapsed; int offset{}; };
    const std::vector<Case> valid{
        {Round(1, 2, 0), hud::Kind::Round, 0, 1, 2, 0},
        {Round(2, 2, 3599.75f), hud::Kind::Round, 0, 2, 2, 3599.75f},
        {Round(2, 2, -2), hud::Kind::Round, 0, 2, 2, -2},
        {Bytes({27, 25, 0xff, 0xff}), hud::Kind::TimerOffset, 0, 0, 0, 0, -1},
        {Bytes({27, 25, 0x00, 0x80}), hud::Kind::TimerOffset, 0, 0, 0, 0, -32768},
        {Bytes({27, 25, 0xff, 0x7f}), hud::Kind::TimerOffset, 0, 0, 0, 0, 32767},
        {Bytes({27, 7, 0xff, 0x80}), hud::Kind::Dialogue, 0, 33023, 0, 0},
        {Bytes({27, 2, 32, 0xff, 0xff}), hud::Kind::Frags, 32, 65535, 0, 0},
        {Bytes({27, 8, 1}), hud::Kind::ResetFrags, 1, 0, 0, 0}};
    for (const auto& item : valid)
    {
        auto decoded = sentinel;
        Check(hud::DecodeMP(item.bytes, decoded) == hud::ParseResult::Accepted, "original 27 message accepted");
        Check(decoded.kind == item.kind && decoded.player == item.player && decoded.value == item.value &&
            decoded.total == item.total && decoded.elapsed == item.elapsed && decoded.secondsOffset == item.offset, "exact message values");
        for (unsigned length = 1; length < 12; ++length)
        {
            auto bytes = item.bytes; bytes.resize(length); decoded = sentinel;
            const auto result = hud::DecodeMP(bytes, decoded);
            Check(result == (length == item.bytes.size() ? hud::ParseResult::Accepted : hud::ParseResult::Malformed), "exact wire length");
            if (result != hud::ParseResult::Accepted) Check(unchanged(decoded), "rejected length preserves output");
        }
    }
    for (unsigned opcode = 0; opcode < 256; ++opcode)
    {
        if (opcode == 2 || opcode == 3 || opcode == 7 || opcode == 8 || opcode == 25) continue;
        auto decoded = sentinel;
        Check(hud::DecodeMP(Bytes({27, opcode}), decoded) == hud::ParseResult::Unrelated && unchanged(decoded), "unrelated 27 opcode remains available to other consumers");
    }
    for (float bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        auto decoded = sentinel;
        Check(hud::DecodeMP(Round(1, 2, bad), decoded) == hud::ParseResult::Malformed && unchanged(decoded), "invalid clock is transactional");
    }
    for (unsigned player : {0u, 33u, 255u}) for (unsigned opcode : {2u, 8u})
    {
        auto bytes = Bytes({27, opcode, player, 1, 0}); if (opcode == 8) bytes.resize(3);
        auto decoded = sentinel;
        Check(hud::DecodeMP(bytes, decoded) == hud::ParseResult::Malformed && unchanged(decoded), "32-slot player bounds");
    }
    auto decoded = sentinel;
    Check(hud::DecodeValueSync(Bytes({6, 0, 0x34, 0x12}), decoded) == hud::ParseResult::Accepted &&
        decoded.kind == hud::Kind::TeamKills && decoded.value == 0x1234, "team counter uses original key6/player0");
    for (unsigned length : {0u, 1u, 2u, 3u, 5u, 9u})
    {
        auto bytes = Bytes({6, 0, 2, 0}); bytes.resize(length); decoded = sentinel;
        Check(hud::DecodeValueSync(bytes, decoded) == (length ? hud::ParseResult::Malformed : hud::ParseResult::Unrelated) && unchanged(decoded), "ValueSync length preserves prior state");
    }
}
void OriginalClockSamples()
{
    // These observations are the saved original machine-code results in
    // ui-layout-audit/clock-original.json, not recalculated expected formulas.
    struct Case { float received, elapsed, now; int carry, seconds; };
    const Case cases[]{
        {100,12.75f,101.25f,500,514}, {5,10,8,4,7}, {100,0,100.75f,0,0},
        {100,-2,101,0,-1}, {6000.25f,3600.75f,6001.5f,-32768,-29166},
        {10,9.9999f,11,32767,32777}};
    for (const auto& item : cases)
    {
        hud::State state; hud::Message message;
        Check(hud::DecodeMP(Bytes({27,25,unsigned(item.carry)&255,(unsigned(item.carry)>>8)&255}), message) == hud::ParseResult::Accepted &&
            state.Apply(message, item.received), "original signed-short carry accepted before round replay");
        Check(!state.Active(), "carry alone does not invent a round");
        Check(hud::DecodeMP(Round(7,12,item.elapsed), message) == hud::ParseResult::Accepted && state.Apply(message,item.received), "original signed finite round sample accepted");
        auto frame = state.Layout(1024,1,item.now);
        Check(frame.clock && frame.seconds == item.seconds, "clock matches direct original instruction observation");
        for (unsigned i=0;i<frame.count;++i) Check(frame.quads[i].alpha == 255, "mode15 original alpha remains255");
        state.ClearPresentation(); Check(state.Layout(1024,1,item.now).seconds == item.seconds, "life reset preserves signed carry");
        state.Reset();
        Check(state.Apply({hud::Kind::Round,0,1,2,0},100) && state.Layout(1024,1,101).seconds == 1, "new session clears carry");
    }
    hud::State negative; negative.Apply({hud::Kind::Round,0,1,2,-2},100);
    const auto frame = negative.Layout(1024,1,101);
    Check(frame.seconds == -1, "negative timer value preserved");
    const auto minus = std::find_if(frame.quads.begin(), frame.quads.begin()+frame.count,
        [](const auto& q) { return q.image == hud::Image::TimerDigits && q.u0 < 0; });
    Check(minus != frame.quads.begin()+frame.count && minus->u0 == -.3f && minus->u1 == -.2f,
        "original formatted minus maps to negative UV without invented glyph");
    for (int carry : {-32769,32768})
        Check(!negative.Apply({hud::Kind::TimerOffset,0,0,0,0,carry},100) && negative.Layout(1024,1,101).seconds == -1,
            "outside signed-short carry is rejected transactionally");
    hud::State overflow; overflow.Apply({hud::Kind::Round,0,1,2,0},0);
    overflow.Apply({hud::Kind::TimerOffset,0,0,0,0,1},0);
    Check(overflow.Layout(1024,1,2147483648.0f).seconds == -2147483647,
        "SSE out-of-range conversion sentinel and integer wrap avoid C++ undefined overflow");
}
void ClockAndLifecycle()
{
    hud::State client;
    Check(!client.Layout(1024, 1, 0).count, "no round before authority");
    Check(client.Apply({hud::Kind::Round, 0, 2, 2, 3600.75f}, 0), "original receiver accepts elapsed larger than client time");
    Check(client.Layout(1024, 1, 0).seconds == 0 && client.Layout(1024, 1, 1.25f).seconds == 1, "original negative epoch falls back to receive time");
    Check(client.Apply({hud::Kind::Round, 0, 2, 2, 3600.75f}, 4000), "normal reconnect replays authority against synchronized engine time");
    Check(client.Layout(1024, 1, 4000).seconds == 3600 && client.Layout(1024, 1, 4001.25f).seconds == 3602, "replayed positive epoch continues");
    client.Apply({hud::Kind::TeamKills, 0, 65535}, 0);
    client.Apply({hud::Kind::Frags, 32, 12345}, 0);
    const auto frame = client.Layout(1024, 32, 4000);
    scenario::ScenarioMessage row; row.id = 193; row.text = "notice";
    Check(client.Show(row, 5), "text notice accepted");
    row.image = "resource/Hud/ZBS/BonusRound"; Check(client.ShowImage(row, 5), "image notice accepted");
    client.ClearPresentation();
    Check(client.Active() && client.Layout(1024, 32, 4000).count == frame.count &&
        client.Layout(1024, 32, 4000).seconds == frame.seconds, "life reset retains map clock and counters");
    Check(std::none_of(client.Notices().begin(), client.Notices().end(), [](const auto& n) { return n.active; }) &&
        std::none_of(client.ImageNotices().begin(), client.ImageNotices().end(), [](const auto& n) { return n.active; }), "life reset clears presentation only");
    for (float bad : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        Check(!client.Apply({hud::Kind::Round, 0, 1, 2, 10}, bad) && client.Layout(1024, 32, 4000).seconds == 3600, "bad update time preserves clock");
    client.Reset(); Check(!client.Active() && !client.Layout(1024, 32, 10).count, "session reset clears HUD");
    scenario::RoundClock server;
    Check(!server.Snapshot(10), "server clock initially absent");
    Check(server.BeginRound(0, 2, 100) && server.Snapshot(120)->elapsed == 20, "preparation counts in original game clock");
    Check(server.BeginRound(1, 2, 150) && server.Snapshot(190)->elapsed == 90 && server.Snapshot(190)->round == 2, "stage change retains first start");
    Check(server.BeginRound(1, 2, 200) && server.Snapshot(230)->elapsed == 130, "continuation preserves cumulative clock");
    Check(!server.BeginRound(2, 2, 300) && server.Snapshot(230)->elapsed == 130, "invalid stage preserves authority");
    server.Reset(); Check(!server.Snapshot(500), "new game has no old clock");
    Check(server.BeginRound(0, 2, 0) && server.BeginRound(1, 2, 10) && server.Snapshot(11)->elapsed == 1, "original zero sentinel retained");
}
void LayoutAndFades()
{
    // Fixed original-image coordinates, independently transcribed from
    // 029ADEA4 constructor and 029C7A90 integer centering + SSE scaling.
    struct RectCase { int width, height; std::array<hud::ImageRect, 4> expected; };
    const RectCase cases[]{
        {1024, 768, {{{256,100,512,128},{384,94,256,256},{384,112,256,128},{227,147,571,142}}}},
        {640, 480, {{{160,62,320,80},{240,58,160,160},{240,70,160,80},{141,91,356,88}}}},
        {1920, 1080, {{{480,140,960,180},{720,132,480,360},{720,157,480,180},{425,206,1070,199}}}}};
    for (const auto& item : cases) for (unsigned channel = 0; channel < 4; ++channel)
    {
        const auto actual = hud::DialogueImageRect(channel, item.width, item.height);
        const auto expected = item.expected[channel];
        Check(actual.x == expected.x && actual.y == expected.y && actual.width == expected.width && actual.height == expected.height,
            "original four-image coordinate fixture");
    }
    for (const auto& args : {std::array<int,3>{4,1024,768},{0,0,768},{0,1024,0},{0,-1,768}})
    {
        const auto result = hud::DialogueImageRect(unsigned(args[0]), args[1], args[2]);
        Check(result.x == 0 && result.y == 0 && result.width == 0 && result.height == 0, "invalid image viewport has no rectangle");
    }
    hud::State state;
    scenario::ScenarioMessage row; row.id = 193; row.text = "notice";
    Check(state.Show(row, 10), "default text timeline begins");
    const auto initial = state.Notices()[1];
    Check(initial.born == 10 && initial.fadeEnd == 10.5f && initial.showEnd == 13.5f && initial.die == 14,
        "original half-second fades and three-second show");
    for (const auto& pair : {std::pair{9.0f,0}, {10.0f,0}, {10.25f,127}, {10.5f,255}, {13.5f,255}, {13.75f,127}, {14.0f,0}})
        Check(hud::NoticeAlpha(initial, pair.first) == pair.second, "original fade alpha at exact fixture time");
    row.id = 194; row.textType = 2; row.fadeIn = -1;
    Check(state.Show(row, 10) && state.Notices()[1].id == 193 && state.Notices()[2].id == 194, "distinct text channels coexist");
    const auto flicker = state.Notices()[2];
    Check(flicker.fadeIn == .2f && flicker.fadeOut == .2f && flicker.flicker == .6f && flicker.y == .7f,
        "negative fade sentinel selects original channel defaults");
    // 029B8C60 uses fractional fmod ramp plus one quarter period, not a blink.
    auto synthetic = initial; synthetic.fadeEnd = 10; synthetic.showEnd = 20; synthetic.die = 21; synthetic.flicker = .5f;
    Check(hud::NoticeAlpha(synthetic, 10) == 31 && hud::NoticeAlpha(synthetic, 10.25f) == 95 && hud::NoticeAlpha(synthetic, 10.5f) == 31,
        "original flicker ramp retains fractional intensity");
    row = {}; row.id = 195; row.text = "instant"; row.fadeIn = 0;
    Check(state.Show(row, 20) && state.Notices()[1].fadeIn == .001f, "zero fade uses original minimum");
    row.fadeIn = -2;
    Check(!state.Show(row, 21) && state.Notices()[1].born == 20, "bad fade does not replace visible notice");

    // Timer positions are original fixed-pixel MM:SS/HH:MM:SS fixtures from
    // 0282337F, independent of score-background position and screen height.
    struct Timer { unsigned seconds; const char* digits; std::vector<int> x; std::vector<int> colons; };
    const Timer timers[]{
        {0,"0000",{490,500,515,525},{510}}, {59,"0059",{490,500,515,525},{510}},
        {60,"0100",{490,500,515,525},{510}}, {3599,"5959",{490,500,515,525},{510}},
        {3600,"010000",{478,488,503,513,528,538},{498,523}},
        {3661,"010101",{478,488,503,513,528,538},{498,523}}};
    for (const auto& fixture : timers)
    {
        hud::State clock; clock.Apply({hud::Kind::Round,0,1,2,float(fixture.seconds)}, 10000);
        const auto frame = clock.Layout(1024,1,10000);
        unsigned digit{}, colon{};
        for (unsigned i = 0; i < frame.count; ++i)
        {
            const auto& q = frame.quads[i];
            if (q.image == hud::Image::TimerDigits)
            {
                Check(digit < fixture.x.size() && q.x == fixture.x[digit] && q.y == 59 && q.width == 8 && q.height == 12 &&
                    q.u0 == float(fixture.digits[digit]-'0') / 10 && q.u1 == float(fixture.digits[digit]-'0'+1) / 10,
                    "timer digit uses original atlas cell and fixed coordinate");
                ++digit;
            }
            if (q.image == hud::Image::TimerColon)
            {
                Check(colon < fixture.colons.size() && q.x == fixture.colons[colon] && q.y == 60 && q.width == 3 && q.height == 10,
                    "timer colon uses original fixed-pixel image");
                ++colon;
            }
        }
        Check(frame.seconds == fixture.seconds && digit == fixture.x.size() && colon == fixture.colons.size(), "timer has complete expected image groups");
    }
}
void Localization()
{
    const std::set<std::u16string> wanted{u"中文", u"Escape", u"Last", u"Bracket", u"Path"};
    hud::LocalizedTokens tokens{{u"old", u"preserved"}}; std::string error;
    const auto valid = u"\ufeff\"lang\" { // comment\n \"Tokens\" { \"中文\" \"正确\" \"Escape\" \"line\\nnext\\t\\\"quoted\\\"\" \"Last\" \"first\" \"Last\" \"second\" \"[english]Ignored\" \"English\" \"Bracket\" \"{\" \"Path\" \"resource\\Hud\\unknown\" } }";
    Check(hud::ReadLocalizedTokens(valid, wanted, tokens, error), "original localization syntax parsed");
    Check(tokens.size() == 5 && tokens.at(u"中文") == u"正确" && tokens.at(u"Last") == u"second" && tokens.at(u"Bracket") == u"{", "requested Chinese and last duplicate retained");
    Check(tokens.at(u"Escape") == u"line\nnext\t\"quoted\"" && tokens.at(u"Path") == u"resource\\Hud\\unknown", "known escapes decoded and unknown path escapes retained");
    const auto saved = tokens;
    for (const auto* invalid : {u"\"Tokens\" \"{\"", u"Tokens { key }", u"Tokens { key \"unclosed", u"Tokens { key value", u"Tokens { key value [$WIN32] }", u"lang { Language chinese }"})
        Check(!hud::ReadLocalizedTokens(invalid, wanted, tokens, error) && !error.empty() && tokens == saved, "malformed localization preserves previous catalog");
    const std::u16string nul{u'T', u'o', u'k', u'e', u'n', u's', 0, u'{', u'}'};
    Check(!hud::ReadLocalizedTokens(nul, wanted, tokens, error) && tokens == saved, "embedded NUL rejected transactionally");
}
void ActualResources(const std::filesystem::path& common, const std::filesystem::path& expectedPath)
{
    Expected expected{Read(expectedPath)};
    if (expected.Word() != 0x31474c44) throw std::runtime_error("wrong independent expectation magic");
    scenario::ConfigError error; scenario::MessageTable messages;
    Check(scenario::ReadMessageTableResource(Read(common / "maps/ZBS_message.cso"), messages, error), "actual encrypted message table uses production decoder and parser");
    auto waves = Read(common / "maps/zbs_wave_zs_deadend.cso");
    Check(csol::DecodeConfiguration(waves), "actual deadend wave decrypted by production codec");
    while (!waves.empty() && waves.back() == std::byte{}) waves.pop_back();
    scenario::WaveTable table;
    Check(scenario::ReadWaves({reinterpret_cast<const char*>(waves.data()), waves.size()}, table, error), "actual wave uses production CSV parser");
    const auto eventCount = expected.Word();
    Check(eventCount == 20 && std::count_if(table.rows.begin(), table.rows.end(), [](const auto& row) { return row.IsEvent(); }) == 20, "actual20 event rows retained");
    for (unsigned i = 0; i < eventCount; ++i)
    {
        const auto record = expected.Word(), number = expected.Word();
        const auto found = std::find_if(table.rows.begin(), table.rows.end(), [&](const auto& row) { return row.recordIndex == record; });
        Check(found != table.rows.end() && found->eventMessage == std::to_string(number), "event source row matches independent CSV");
    }
    scenario::DialogueResources resources;
    Check(scenario::CollectDialogueResources(table, messages, resources, error), "actual map resource closure accepted");
    Check(resources.images.empty() && resources.sounds == std::set<std::string>{"events/tutor_msg.wav"}, "deadend exact resource closure");
    const auto localizedBytes = Read(common / "resource/cso_chn.txt");
    Check(localizedBytes.size() % 2 == 0 && localizedBytes[0] == std::byte{0xff} && localizedBytes[1] == std::byte{0xfe}, "actual Chinese file UTF16LE");
    std::u16string localized;
    for (std::size_t i = 2; i + 1 < localizedBytes.size(); i += 2)
        localized += char16_t(std::to_integer<unsigned>(localizedBytes[i]) | std::to_integer<unsigned>(localizedBytes[i + 1]) << 8);
    while (!localized.empty() && !localized.back()) localized.pop_back();
    struct Row { unsigned id, type; std::u16string text, key, value, image, sound; };
    std::vector<Row> rows; std::set<std::u16string> wanted;
    for (unsigned count = expected.Word(); count; --count)
    {
        Row row; row.id = expected.Word(); row.text = expected.String(); row.key = expected.String();
        row.value = expected.String(); row.image = expected.String(); row.sound = expected.String(); row.type = expected.Word();
        if (!row.key.empty()) wanted.insert(row.key);
        rows.push_back(std::move(row));
    }
    Check(expected.at == expected.data.size(), "all independent expectations consumed");
    hud::LocalizedTokens tokens; std::string localizationError;
    Check(hud::ReadLocalizedTokens(localized, wanted, tokens, localizationError), "actual Chinese catalog uses production parser");
    scenario::WaveTable imageTable; std::set<std::string> selectedImages, selectedSounds;
    for (const auto& row : rows)
    {
        const auto* actual = messages.Find(static_cast<std::uint16_t>(row.id));
        Check(actual && actual->text == Ascii(row.text) && actual->type == int(row.type), "selected message text and image channel match independent CSV");
        if (!actual) continue;
        if (!row.key.empty()) Check(tokens.contains(row.key) && tokens.at(row.key) == row.value, "actual localized Chinese exact UTF16 value");
        if (!row.image.empty())
        {
            const auto path = scenario::DialogueImagePath(actual->image);
            Check(path && *path == Ascii(row.image), "actual backslash image becomes downloadable TGA");
            selectedImages.insert(Ascii(row.image));
            if (!row.sound.empty()) selectedSounds.insert(Ascii(row.sound));
            scenario::WaveRow event; event.recordIndex = row.id; event.spawnPoint = 9999;
            event.types = {40}; event.eventMessage = std::to_string(row.id); imageTable.rows.push_back(event);
            hud::State state; Check(state.ShowImage(*actual, 10), "each actual image type has a consumer channel");
            const auto at = std::find_if(hud::ImageChannels.begin(), hud::ImageChannels.end(), [&](const auto& channel) { return channel.channel == int(row.type) + 10; });
            Check(at != hud::ImageChannels.end() && state.ImageNotices()[at - hud::ImageChannels.begin()].id == row.id, "actual type uses original image slot");
        }
    }
    Check(scenario::CollectDialogueResources(imageTable, messages, resources, error) && resources.images == selectedImages && resources.sounds == selectedSounds,
        "resource closure includes all four actual image channels without executing live monster predicates");
    const auto saved = resources;
    imageTable.rows.front().eventMessage = "2147483648";
    Check(!scenario::CollectDialogueResources(imageTable, messages, resources, error) && resources.images == saved.images && resources.sounds == saved.sounds,
        "invalid event resource closure preserves complete previous output");
}
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("expected common and expectation paths");
        test::Protocol(); test::OriginalClockSamples(); test::ClockAndLifecycle(); test::LayoutAndFades(); test::Localization(); test::ActualResources(argv[1], argv[2]);
        std::cout << "scenario_round_hud checks=" << test::checks << " failures=" << test::failures << '\n';
        return test::failures ? 1 : 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
