// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the real client TU and existing engine edges, but run only the changed
// center-notice contract. Surface records API calls; it is not a pixel oracle.
#define wmain OriginalRespawnClientMain
#include "../csol-scenario-respawn-ui/client_main.cpp"
#undef wmain
#include "../csol-wall-status/surface_stub.hpp"
#include <limits>

namespace golden_test
{
using namespace fixture;
float centerSeconds = 2;
struct TextDraw { std::wstring text; std::array<int, 4> color; int x{}, y{}; vgui::HFont font{}; };
struct Background { int left{}, top{}, right{}, bottom{}, texture{}, alpha{}; };
struct Surface final : vgui::TestSurface
{
    int width{1024}, height{768}, x{}, y{}, texture{}, alpha{}, pushes{}, pops{};
    vgui::HFont font{};
    std::array<int, 4> color{};
    std::vector<TextDraw> texts;
    std::vector<Background> backgrounds;
    vgui::VPANEL GetEmbeddedPanel() override { return 1; }
    void GetScreenSize(int& w, int& h) override { w = width; h = height; }
    void PushMakeCurrent(vgui::VPANEL, bool) override { ++pushes; }
    void PopMakeCurrent(vgui::VPANEL) override { ++pops; }
    void GetTextSize(vgui::HFont, const wchar_t* text, int& w, int& h) override
    { w = int(std::wcslen(text)) * 14; h = 14; }
    void DrawSetTextFont(vgui::HFont value) override { font = value; }
    void DrawSetTextColor(int r, int g, int b, int a) override { color = {r, g, b, a}; }
    void DrawSetTextPos(int a, int b) override { x = a; y = b; }
    void DrawPrintText(const wchar_t* text, int size) override
    { texts.push_back({std::wstring(text, std::size_t(size)), color, x, y, font}); }
    void DrawSetTexture(int id) override { texture = id; }
    void DrawSetColor(int, int, int, int a) override { alpha = a; }
    void DrawTexturedPolygon(vgui::VGuiVertex* vertices, int count) override
    {
        Check(count == 4, "center background has four vertices");
        backgrounds.push_back({vertices[0].GetX(), vertices[0].GetY(), vertices[2].GetX(),
            vertices[2].GetY(), texture, alpha});
    }
} capture;
constexpr std::array<std::string_view, 2> keys{
    "#CSO_ZBS_SpawnedGoldenZB", "#CSO_ZBS_KilledGoldenZB"};
constexpr std::array<std::wstring_view, 2> chinese{
    L"不好！黄金僵尸出现了！快消灭它们！", L"恭喜！成功消灭了黄金僵尸！"};
std::vector<std::byte> Notice(unsigned which, bool original)
{
    auto packet = original ? Bytes({4, 0, 7, 0}) : Bytes({4});
    String(packet, keys[which]);
    return packet;
}
void Render()
{
    capture.texts.clear(); capture.backgrounds.clear();
    csol::client::DrawScenarioRespawnUi(now, 0);
    csol::client::Paint();
    Check(capture.pushes == capture.pops, "real paint balances surface context");
}
void SupplyGraphics()
{
    csol::client::surface = &capture;
    csol::client::centerFont = 11;
    csol::client::textures[2] = 17;
}
void CheckGolden(unsigned which)
{
    using namespace csol::client;
    Check(state.Center(now) && state.Center(now)->token == 5 + which,
        "real receiver keeps the authoritative Golden token");
    Check(centerText == chinese[which], "actual full Chinese catalog resolves Golden text");
    Render();
    Check(capture.texts.size() == 1 && capture.texts[0].text == chinese[which] &&
        capture.texts[0].color == std::array<int, 4>{255, 215, 0, 255} && capture.texts[0].font == 11,
        "real paint uses golden RGB through the existing center font");
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        using namespace fixture;
        using namespace csol::client;
        using namespace golden_test;
        if (argc != 2) throw std::runtime_error("expected actual unpacked common directory");
        common = argv[1];
        metahook_api_t storage{}; api = &storage;
        storage.GetEngineFactory = []()->CreateInterfaceFn { return nullptr; };
        storage.GetPluginModuleHandleByBaseFileName = [](const char*)->HINTERFACEMODULE { return nullptr; };
        storage.HookUserMsg = [](const char* name, pfnUserMsgHook hook)->pfnUserMsgHook
        { const auto prior = hooks.contains(name) ? hooks.at(name) : Previous; hooks[name] = hook; return prior; };
        engine.GetClientTime = [] { return now; };
        engine.pfnGetCvarFloat = [](const char* name)
        { return std::string_view(name) == "scr_centertime" ? centerSeconds : 1.f; };
        engine.Con_DPrintf = Log;
        view.health = 100;
        InitializeScenarioRespawnUi(); InitializeScenarioRespawnUi();
        Check(hooks.size() == 9, "Golden reuses the existing message hook owner");
        LoadLocalization(); SupplyGraphics();
        Check(reads == 1 && localized, "one actual full catalog load supplies both Golden keys");

        for (bool original : {true, false}) for (unsigned which : {0u, 1u})
        {
            auto packet = Notice(which, original);
            if (original) Check(packet.size() == 29 - which, "original packet has exact documented wire length");
            const auto oldForwards = forwards;
            Check(Send("TextMsg", packet) == 1 && forwards == oldForwards,
                "Golden original or explicit stock adapter is consumed exactly once");
            CheckGolden(which);
        }
        for (const auto dimensions : {std::array<int, 3>{800, 600, 368}, {1024, 768, 471}, {1920, 1080, 663}})
        {
            capture.width = dimensions[0]; capture.height = dimensions[1]; Render();
            Check(capture.texts.size() == 1 && capture.texts[0].y == dimensions[2] &&
                capture.texts[0].x == (dimensions[0] - int(chinese[1].size()) * 14) / 2,
                "mode15 center position follows documented truncation across resolutions");
            const int width = int(dimensions[0] * .6), left = (dimensions[0] - width) / 2;
            Check(capture.backgrounds.size() == 1 && capture.backgrounds[0].left == left &&
                capture.backgrounds[0].right == left + width && capture.backgrounds[0].top == dimensions[2] - 10 &&
                capture.backgrounds[0].bottom == dimensions[2] + 23 && capture.backgrounds[0].texture == 17 &&
                capture.backgrounds[0].alpha == 255, "same center background covers sixty percent width and thirty-three pixels");
        }
        for (const float duration : {-1.f, 0.f, 3.f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
        {
            centerSeconds = duration;
            Send("TextMsg", Notice(0, true));
            const float expected = std::isfinite(duration) && duration >= 0 ? duration : 2.f;
            Check(bool(state.Center(now)) == (expected > 0), "zero cvar expires immediately; invalid cvar uses host protection");
            Check(!state.Center(now + expected), "Golden deadline follows cvar instead of metadata color seven");
            if (expected > 0) Check(state.Center(now + expected - .001), "Golden remains visible until its precise deadline");
        }
        centerSeconds = 2;
        Send("TextMsg", Center("#CSO_ZBS_AutoRespawnWait", "30")); Render();
        Check(capture.texts.size() == 1 && capture.texts[0].color == std::array<int, 4>{255, 180, 30, 255} &&
            capture.texts[0].text == L"复活剩余时间 (30秒)", "later revival notice replaces Golden and restores ordinary color");
        Send("TextMsg", Notice(1, true)); CheckGolden(1);
        const auto oldAccepted = accepted, oldMalformed = malformed;
        const auto oldForwards = forwards;
        unsigned rejected{};
        const auto reject = [&](std::vector<std::byte> packet)
        {
            ++rejected;
            csol::scenario_respawn_ui::CenterMessage decoded{99, 77};
            Check(csol::scenario_respawn_ui::DecodeText(packet, decoded) == csol::scenario_respawn_ui::ParseResult::Malformed &&
                decoded.token == 99 && decoded.seconds == 77, "bad known Golden packet does not modify decode output");
            Check(Send("TextMsg", packet) == 1 && accepted == oldAccepted && forwards == oldForwards &&
                malformed == oldMalformed + rejected && centerText == chinese[1] && state.Center(now)->token == 6,
                "bad known Golden packet neither reaches stock chain nor replaces valid center state");
        };
        for (unsigned which : {0u, 1u}) for (bool original : {false, true})
        {
            auto packet = Notice(which, original); packet.pop_back(); reject(packet);
            packet = Notice(which, original); packet.push_back(std::byte{}); reject(packet);
            packet = Notice(which, original); String(packet, "player"); reject(packet);
            if (original) for (unsigned index : {1u, 2u, 3u})
            { packet = Notice(which, true); packet[index] = std::byte{65}; reject(packet); }
        }
        for (auto packet : {Center("#Buy"), Center("#CSO_ZBS_SpawnedGoldenZBX"), Bytes({4, 0, 7, 0})})
        {
            const auto before = forwards;
            Check(Send("TextMsg", packet) == 73 && forwards == before + 1 && centerText == chinese[1],
                "unowned center keys and incomplete unidentifiable headers retain ordinary message chain");
        }
        const auto forwardUnrelated = [&](std::vector<std::byte> packet, const char* label)
        {
            csol::scenario_respawn_ui::CenterMessage decoded{99, 77};
            Check(csol::scenario_respawn_ui::DecodeText(packet, decoded) == csol::scenario_respawn_ui::ParseResult::Unrelated &&
                decoded.token == 99 && decoded.seconds == 77, "unrelated text cannot enter center through the legacy decoder");
            const auto before = forwards;
            const auto rejectedBefore = malformed;
            const auto acceptedBefore = accepted;
            Check(Send("TextMsg", packet) == 73 && forwards == before + 1 && malformed == rejectedBefore &&
                accepted == acceptedBefore && centerText == chinese[1] && state.Center(now)->token == 6, label);
        };
        for (const auto key : keys)
        {
            for (const std::string_view prefix : {"abc", "123", "#--", "中"})
                forwardUnrelated(Center(std::string(prefix) + std::string(key)),
                    "ordinary ASCII or UTF8 text with Golden key at offset four remains on stock chain");
            for (const auto metadata : {std::array<unsigned, 3>{1, 6, 1}, {1, 0, 1}, {9, 'a', 'b'}})
            {
                auto packet = Bytes({4, metadata[0], metadata[1], metadata[2]}); String(packet, key);
                forwardUnrelated(packet, "ambiguous control prefix is never reaccepted as Golden by the legacy parser");
            }
            for (const auto text : {"before " + std::string(key), std::string(key) + " after",
                "before " + std::string(key) + " after"})
            {
                forwardUnrelated(Center(text), "embedded key in ordinary stock text remains ordinary text");
                auto packet = Bytes({4, 0, 7, 0}); String(packet, text);
                forwardUnrelated(packet, "embedded key with recognizable metadata is not a fixed Golden token");
            }
        }
        auto nonCenter = Notice(0, false); nonCenter[0] = std::byte{3};
        Check(Send("TextMsg", nonCenter) == 73 && centerText == chinese[1], "other destinations are not claimed");
        for (bool* gate : {&gameUI, &console})
        { *gate = true; Render(); Check(capture.texts.empty(), "existing game or console visibility gate hides center"); *gate = false; }
        hidden = 4; Render(); Check(capture.texts.empty(), "HUD hidden gate applies to Golden"); hidden = 0;
        centerFont = 0; Render(); Check(capture.texts.empty(), "missing EngineFont does not invent a fallback font");
        SupplyGraphics(); CheckGolden(1);
        ScenarioRespawnUiVideoInit();
        Check(state.Center(now) && centerText == chinese[1] && centerFont == 0,
            "video reset retires graphics without replaying or clearing active authoritative notice");
        Render(); Check(capture.texts.empty(), "retired video resources cannot draw stale handles");
        SupplyGraphics(); CheckGolden(1);
        Send("ResetHUD", Bytes({0})); Render();
        Check(!state.Center(now) && centerText.empty() && capture.texts.empty(), "ResetHUD clears Golden for the old life");
        Send("TextMsg", Notice(0, true)); Send("InitHUD", Bytes({0}));
        Check(!state.Active() && !state.Center(now) && centerText.empty() && !localized,
            "InitHUD retires prior session Golden and localization");
        LoadLocalization(); Send("TextMsg", Notice(1, false)); newSession = true;
        Send("HudIcon", Bytes({2, 0}));
        Check(!state.Center(now) && centerText.empty() && state.Mask() == 2, "session synchronization clears center before accepting next packet");
        const auto originalCommon = common; common /= "__golden_missing_fixture__";
        bool missing{}; try { LoadLocalization(); } catch (const std::exception&) { missing = true; }
        common = originalCommon;
        Check(missing && !localized, "missing actual catalog is a failure rather than invented translated text");
        LoadLocalization(); Send("TextMsg", Notice(0, true));
        ShutdownScenarioRespawnUi();
        Check(!state.Active() && !state.Center(now) && !surface && centerText.empty(), "shutdown releases Golden state and surface");
        std::cout << "golden_notice_client checks=" << checks << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
