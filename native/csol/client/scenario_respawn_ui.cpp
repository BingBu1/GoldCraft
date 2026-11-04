// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "overlay_layers.hpp"
#include "scenario_respawn_ui.hpp"
#include "scenario_retry_scoreboard.hpp"
#include "scenario_round_hud_client.hpp"
#include "user_message_chain.hpp"
#include "buy_menu.hpp"
#include "characters.hpp"
#include "weapon_inventory.hpp"
#include "../image_codec.hpp"
#include "../scenario_localization.hpp"
#include <cwchar>
#include <ostream>
#include <VGUI/ISurface.h>
#include <VGUI/IScheme.h>
#include <VGUI/IPanel.h>
#include <VGUI/IVGui.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
namespace hud = scenario_respawn_ui;
hud::State state;
vgui::ISurface* surface{};
vgui::ISchemeManager* schemes{};
vgui::IPanel* panels{};
vgui::IVGui* panelGui{};
hud::RetryScoreboard retryScoreboard;
IVGUI2Extension* extension{};
std::array<int, 3 + hud::RetryImages.size()> textures{};
vgui::HFont centerFont{}, hintFont{};
vgui::HFont budgetTitleFont{}, budgetCountFont{};
vgui::HScheme budgetScheme{};
int itemSprite{};
std::array<std::wstring, hud::CenterTokens.size()> centerTemplates;
std::array<std::wstring, hud::HintTokens.size()> hintTexts;
std::wstring centerText;
std::wstring reloadWaitTemplate;
std::wstring retryTemplate;
std::wstring budgetTitle, budgetUnlimited, budgetText;
constexpr std::string_view ReloadWaitToken = "#CSO_ZBSVER2_ReviveNotCooled";
constexpr std::string_view RetryToken = "#CSO_ZBS_RoundWillRestart";
bool localized{}, reported{}, requested{}, gameUI{}, console{}, intermission{};
ULONGLONG retryAt{};
double frameTime{};
int hidden{};
std::uint64_t accepted{}, malformed{}, draws{}, commands{}, budgetDraws{};
pfnUserMsgHook previousMP{}, previousIcon{}, previousText{}, previousHint{}, previousHintArgs{};
pfnUserMsgHook previousInit{}, previousReset{}, previousHide{};
pfnUserMsgHook previousWait{};

std::u16string ReadLocalization()
{
    const auto bytes = ReadResource(hud::LocalizationPath);
    if (bytes.size() < 2 || bytes.size() % 2 || bytes[0] != std::byte{0xff} || bytes[1] != std::byte{0xfe})
        throw std::runtime_error("scenario localization must be UTF16LE");
    std::u16string text;
    text.reserve(bytes.size() / 2 - 1);
    for (std::size_t at = 2; at < bytes.size(); at += 2)
        text += char16_t(hud::Byte(bytes[at]) | hud::Byte(bytes[at + 1]) << 8);
    while (!text.empty() && text.back() == u'\0') text.pop_back();
    return text;
}
std::u16string TokenKey(std::string_view token)
{ return {token.begin() + 1, token.end()}; }
void FormatCenter(double now)
{
    centerText.clear();
    const auto* message = state.Center(now);
    if (!localized || !message) return;
    centerText = centerTemplates[message->token];
    const auto marker = centerText.find(L"%s1");
    if (marker != centerText.npos) centerText.replace(marker, 3, std::to_wstring(message->seconds));
}
void FormatBudget()
{ budgetText = hud::FormatRetryBudget(state.Budget(), budgetUnlimited); }
void LoadLocalization()
{
    scenario_hud::LocalizedTokens tokens;
    std::set<std::u16string> wanted;
    for (auto token : hud::CenterTokens) wanted.insert(TokenKey(token));
    for (auto token : hud::HintTokens) wanted.insert(TokenKey(token));
    wanted.insert(TokenKey(ReloadWaitToken));
    wanted.insert(TokenKey(RetryToken));
    wanted.insert(TokenKey(hud::RetryBudgetTitleToken));
    wanted.insert(TokenKey(hud::RetryBudgetUnlimitedToken));
    std::string error;
    if (!scenario_hud::ReadLocalizedTokens(ReadLocalization(), wanted, tokens, error))
        throw std::runtime_error(error);
    for (auto token : wanted) if (!tokens.contains(token)) throw std::runtime_error("missing scenario revival localization");
    for (unsigned i = 0; i < centerTemplates.size(); ++i)
    {
        const auto& text = tokens.at(TokenKey(hud::CenterTokens[i]));
        centerTemplates[i].assign(text.begin(), text.end());
    }
    for (unsigned i = 0; i < hintTexts.size(); ++i)
    {
        const auto& text = tokens.at(TokenKey(hud::HintTokens[i]));
        hintTexts[i].assign(text.begin(), text.end());
    }
    const auto& reloadText = tokens.at(TokenKey(ReloadWaitToken));
    reloadWaitTemplate.assign(reloadText.begin(), reloadText.end());
    const auto& retryText = tokens.at(TokenKey(RetryToken));
    retryTemplate.assign(retryText.begin(), retryText.end());
    const auto& title = tokens.at(TokenKey(hud::RetryBudgetTitleToken));
    budgetTitle.assign(title.begin(), title.end());
    const auto& unlimited = tokens.at(TokenKey(hud::RetryBudgetUnlimitedToken));
    budgetUnlimited.assign(unlimited.begin(), unlimited.end());
    localized = true;
    FormatCenter(engine.GetClientTime());
    FormatBudget();
}
void ReleaseImages()
{
    if (surface) for (int texture : textures) if (texture) surface->DeleteTextureByID(texture);
    textures = {}; itemSprite = {}; centerFont = hintFont = 0; retryAt = 0;
    retryScoreboard.Reset(); budgetTitleFont = budgetCountFont = 0; budgetScheme = 0;
}
void Prepare()
{
    if (!state.Active() || !surface || GetTickCount64() < retryAt) return;
    retryAt = GetTickCount64() + 5000;
    try
    {
        if (!localized) LoadLocalization();
        if (schemes)
        {
            if (auto* scheme = schemes->GetIScheme(schemes->GetDefaultScheme()))
            {
                // 02A8F7CA and 02A1330A request these named fonts. Let the
                // installation's VGUI scheme own glyphs and yres scaling.
                centerFont = scheme->GetFont("EngineFont", true);
                hintFont = scheme->GetFont("CreditsFont", true);
            }
        }
        if (!itemSprite && engine.pfnSPR_Load) itemSprite = engine.pfnSPR_Load(hud::ItemSprite);
        for (unsigned i = 0; i < textures.size(); ++i)
        {
            if (textures[i]) continue;
            const auto& resource = i < hud::Outcomes.size() ? hud::Outcomes[i] :
                i == 2 ? hud::CenterBackground : hud::RetryImages[i - 3];
            const auto image = DecodeTGA(ReadResource(resource.path));
            if (image.Width != resource.width || image.Height != resource.height)
                throw std::runtime_error("scenario revival image dimensions changed");
            const int texture = surface->CreateNewTextureID(true);
            if (texture) surface->DrawSetTextureRGBA(texture, image.RGBA.data(), image.Width, image.Height, 1, true);
            textures[i] = texture;
        }
    }
    catch (const std::exception& error)
    {
        if (!reported) engine.Con_DPrintf("CSOLSupport: scenario revival HUD: %s\n", error.what());
        reported = true;
    }
}
bool HudVisible()
{
    return !gameUI && !console && !(hidden & 4) &&
        (!engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("hud_draw") != 0);
}
void Quad(int texture, const hud::Rect& rect, int alpha)
{
    if (!texture || rect.width <= 0 || rect.height <= 0 || alpha <= 0) return;
    surface->DrawSetTexture(texture); surface->DrawSetColor(255, 255, 255, alpha);
    const int x = rect.x, y = rect.y, right = x + rect.width, bottom = y + rect.height;
    vgui::VGuiVertex vertices[]{{x,y,0,0},{right,y,1,0},{right,bottom,1,1},{x,bottom,0,1}};
    surface->DrawTexturedPolygon(vertices, 4);
}
void PaintHint(int token, int width, int height)
{
    if (!hintFont || !localized) return;
    const auto& text = hintTexts[unsigned(token)];
    int textWidth{}, textHeight{};
    surface->GetTextSize(hintFont, text.c_str(), textWidth, textHeight);
    int x = std::max(0, (width - textWidth) / 2);
    const int y = std::clamp(int(height * .7f), 0, std::max(0, height - textHeight));
    surface->DrawSetTextFont(hintFont);
    for (unsigned i = 0; i < text.size(); ++i)
    {
        int a{}, b{}, c{};
        surface->GetCharABCwide(hintFont, text[i], a, b, c);
        const auto rgb = hud::HintColor(i, unsigned(text.size()), unsigned(token), state.HintAge(frameTime));
        if ((rgb[0] || rgb[1] || rgb[2]) && x + a + b + c <= width)
        {
            surface->DrawSetTextColor(rgb[0], rgb[1], rgb[2], 255);
            surface->DrawSetTextPos(x, y); surface->DrawPrintText(text.data() + i, 1);
        }
        x += a + b + c;
    }
}
void PaintBudgetText(vgui::HFont font, const std::wstring& text, const hud::Rect& bounds)
{
    if (!font || text.empty()) return;
    int width{}, height{};
    surface->GetTextSize(font, text.c_str(), width, height);
    surface->DrawSetTextFont(font); surface->DrawSetTextColor(255, 255, 255, 255);
    surface->DrawSetTextPos(bounds.x + (bounds.width - width) / 2, bounds.y + (bounds.height - height) / 2);
    surface->DrawPrintText(text.c_str(), int(text.size()));
}
void PaintRetryBudget(vgui::VPANEL root, int width, int height)
{
    if (!localized || !panels || !panelGui || !schemes) return;
    const auto board = retryScoreboard.Visible(*panels, *panelGui, root, GetTickCount64());
    if (!board) return;
    hud::Rect bounds;
    panels->GetAbsPos(board, bounds.x, bounds.y);
    panels->GetSize(board, bounds.width, bounds.height);
    int rootX{}, rootY{};
    panels->GetAbsPos(root, rootX, rootY);
    bounds.x -= rootX; bounds.y -= rootY;
    const auto footer = hud::RetryBudgetFooter(bounds, width, height);
    if (!footer.width) return;
    const auto schemeId = panels->GetScheme(board);
    if (schemeId != budgetScheme || !budgetTitleFont || !budgetCountFont)
    {
        budgetScheme = schemeId;
        budgetTitleFont = budgetCountFont = 0;
        if (auto* scheme = schemes->GetIScheme(schemeId ? schemeId : schemes->GetDefaultScheme()))
        {
            // Native scoreboard adaptation: these verified host fonts are not
            // claimed to be the as-yet unresolved original CSOL inherited font.
            budgetTitleFont = scheme->GetFont("Default", false);
            budgetCountFont = scheme->GetFont("DefaultSmall", false);
        }
    }
    surface->DrawSetColor(0, 0, 0, 190);
    surface->DrawFilledRect(footer.x, footer.y, footer.x + footer.width, footer.y + footer.height);
    surface->DrawSetColor(130, 130, 130, 220);
    surface->DrawOutlinedRect(footer.x, footer.y, footer.x + footer.width, footer.y + footer.height);
    const auto label = [&](const hud::Rect& child)
    { return hud::Rect{footer.x + child.x, footer.y + child.y, child.width, child.height}; };
    PaintBudgetText(budgetTitleFont, budgetTitle, label(hud::OriginalRetryTitle));
    PaintBudgetText(budgetCountFont, budgetText, label(hud::OriginalRetryCount));
    ++budgetDraws;
}
void Paint()
{
    if (!requested) return;
    requested = false;
    if (!surface || !HudVisible()) return;
    int width{}, height{};
    surface->GetScreenSize(width, height);
    const auto root = surface->GetEmbeddedPanel();
    if (!root || width <= 0 || height <= 0) return;
    surface->PushMakeCurrent(root, false);
    const int alpha = state.OutcomeAlpha(frameTime);
    if (alpha) Quad(textures[unsigned(state.Outcome())], hud::OutcomeRect(width, height), alpha);
    const int countdown = state.RetryCountdown();
    if (countdown >= 0)
    {
        const auto layout = hud::RetryRects(width, height);
        Quad(textures[3], layout.banner, 255);
        const int digits = countdown >= 10 ? 2 : 1;
        const int digitWidth = layout.numbers.width / digits;
        int value = countdown;
        // Original 029C81A0 compresses each image into its share of one
        // fixed square, starting with the least significant digit at right.
        for (int i = 1; i <= digits; ++i, value /= 10)
            Quad(textures[4 + unsigned(value % 10)],
                {layout.numbers.x + layout.numbers.width - i * digitWidth,
                    layout.numbers.y, digitWidth, layout.numbers.height}, 255);
    }
    const auto* center = state.Center(frameTime);
    if (center && centerFont && !centerText.empty())
    {
        // 02A8E940: centered background covers 60% of screen width, y-10..y+23.
        const int y = int(height * .614583), backgroundWidth = int(width * .6);
        Quad(textures[2], {(width - backgroundWidth) / 2, y - 10, backgroundWidth, 33}, 255);
        int textWidth{}, textHeight{};
        surface->GetTextSize(centerFont, centerText.c_str(), textWidth, textHeight);
        const auto rgb = hud::CenterColor(*center);
        surface->DrawSetTextFont(centerFont); surface->DrawSetTextColor(rgb[0], rgb[1], rgb[2], 255);
        surface->DrawSetTextPos(int(width * .5f - textWidth * .5f), y);
        surface->DrawPrintText(centerText.c_str(), int(centerText.size()));
    }
    const int hint = state.Hint(frameTime);
    if (hint >= 0) PaintHint(hint, width, height);
    PaintRetryBudget(root, width, height);
    surface->DrawFlushText(); surface->PopMakeCurrent(root); ++draws;
}
void Sprite(wrect_t rectangle, int x, int y, float tint)
{
    engine.pfnSPR_Set(itemSprite, int(130 * tint), int(180 * tint), int(27 * tint));
    engine.pfnSPR_DrawAdditive(0, x, y, &rectangle);
}
void DrawItems(double now)
{
    if (!itemSprite || !engine.pfnSPR_Set || !engine.pfnSPR_DrawAdditive || !HudVisible()) return;
    for (unsigned i = 0; i < state.Items().size(); ++i)
    {
        if (!(state.Mask() & (1u << i))) continue;
        const auto& item = state.Items()[i];
        const int top = hud::ItemTops[i], y = hud::ItemY[i];
        wrect_t rectangle{1, 69, top, top + 27};
        if (!item.cooldown) Sprite(rectangle, 10, y, 1);
        else
        {
            Sprite(rectangle, 10, y, .3f);
            const int rows = state.CooldownRows(i, now);
            rectangle.top += rows;
            if (rows < 27) Sprite(rectangle, 10, y + rows, .7f);
        }
        if (item.count < 0) continue;
        Sprite({1, 14, 118, 135}, 79, y + 5, .7f);
        char digits[5]{};
        const auto result = std::to_chars(digits, digits + sizeof(digits), std::clamp(item.count, 0, 9999));
        int x = 92;
        for (const char* digit = digits; digit != result.ptr; ++digit, x += 13)
        {
            const int left = 14 + (*digit - '0') * 13;
            Sprite({left, left + 13, 118, 135}, x, y + 5, .7f);
        }
    }
}
class Overlay final : public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude() const override { return 25; }
    void Initialize(CreateInterfaceFn*, int) override {}
    void Start(cl_enginefuncs_s*, int) override {}
    void Shutdown() override
    { ResetScenarioRespawnUi(); ReleaseImages(); surface = nullptr; schemes = nullptr; panels = nullptr; panelGui = nullptr; }
    void Key_Event(int&, int&, const char*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceAppProc(void*&, void*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceWndProc(void*&, unsigned int&, unsigned int&, long&, VGUI2Extension_CallbackContext*) override {}
    void Paint(int&, int&, int&, int&, VGUI2Extension_CallbackContext* context) override
    { if (context->IsPost) QueueOverlayPaint(OverlayLayer::ScenarioRespawnUi, csol::client::Paint); }
    void HideGameUI(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) gameUI = false; }
    void ActivateGameUI(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) gameUI = true; }
    void HideConsole(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) console = false; }
    void ShowConsole(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) console = true; }
} overlay;
std::wstring RetryPlayerName(unsigned index)
{
    hud_player_info_t player{};
    if (engine.pfnGetPlayerInfo) engine.pfnGetPlayerInfo(int(index), &player);
    if (!player.name) return {};
    const auto length = strnlen(player.name, 128);
    if (length == 128) return {};
    const auto convert = [&](UINT codePage)
    {
        const int size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, player.name, int(length), nullptr, 0);
        std::wstring text(size > 0 ? size : 0, L'\0');
        if (size > 0) MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, player.name, int(length), text.data(), size);
        return text;
    };
    auto name = convert(CP_UTF8);
    return name.empty() ? convert(CP_ACP) : name;
}
void ApplyRetry(const hud::Message& message)
{
    if (message.kind == hud::Kind::RetryBudget) FormatBudget();
    else if (message.kind == hud::Kind::RetryCountdown)
    {
        if (const auto* sound = hud::RetrySound(message.count); sound && engine.pfnPlaySoundByNameAtPitch)
            engine.pfnPlaySoundByNameAtPitch(sound, 1, 100);
    }
    else if (message.kind == hud::Kind::RetryAnnouncement)
    {
        try
        {
            if (!localized) LoadLocalization();
            const auto text = hud::FormatRetryAnnouncement(retryTemplate, RetryPlayerName(message.index), unsigned(message.count));
            ShowScenarioRoundHudText(1, text, {166,241,255});
        }
        catch (const std::exception& error)
        {
            if (!reported) engine.Con_DPrintf("CSOLSupport: scenario retry notice: %s\n", error.what());
            reported = true;
        }
    }
}
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (data && size >= 0)
    {
        hud::Message message;
        const auto result = hud::DecodeMP({static_cast<const std::byte*>(data), std::size_t(size)}, message);
        if (result == hud::ParseResult::Malformed) { ++malformed; return 1; }
        if (result == hud::ParseResult::Accepted && state.Apply(message, engine.GetClientTime()))
        { ++accepted; ApplyRetry(message); }
    }
    return previousMP ? previousMP(name, size, data) : 1;
}
int Icon(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (data && size == 2) state.SetMask({static_cast<const std::byte*>(data), 2});
    // HudIcon also owns unrelated bits; preserve the rest of the chain.
    return previousIcon ? previousIcon(name, size, data) : 1;
}
int Text(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (data && size >= 0)
    {
        hud::CenterMessage message;
        const std::span<const std::byte> bytes{static_cast<const std::byte*>(data), std::size_t(size)};
        const auto golden = hud::DecodeGoldenText(bytes, message);
        const auto result = golden == hud::ParseResult::Unrelated ? hud::DecodeText(bytes, message) : golden;
        if (result == hud::ParseResult::Malformed)
        {
            ++malformed;
            // Only a recognized Golden key is ours to reject outright. Keep
            // ordinary TextMsg traffic on the existing message chain.
            if (golden == hud::ParseResult::Malformed) return 1;
        }
        if (result == hud::ParseResult::Accepted)
        {
            const double now = engine.GetClientTime();
            double duration = engine.pfnGetCvarFloat("scr_centertime");
            if (!std::isfinite(duration) || duration < 0) duration = 2;
            if (state.SetCenter(message, now, duration)) { FormatCenter(now); ++accepted; }
            return 1;
        }
    }
    return previousText ? previousText(name, size, data) : 1;
}
int ReceiveHint(const char* name, int size, void* data, bool withArguments, pfnUserMsgHook previous)
{
    SynchronizeSession();
    if (data && size >= 0)
    {
        unsigned token{};
        const auto result = hud::DecodeHint({static_cast<const std::byte*>(data), std::size_t(size)}, withArguments, token);
        if (result == hud::ParseResult::Malformed) ++malformed;
        if (result == hud::ParseResult::Accepted)
        { if (state.SetHint(token, engine.GetClientTime())) ++accepted; return 1; }
    }
    return previous ? previous(name, size, data) : 1;
}
int Hint(const char* name, int size, void* data) { return ReceiveHint(name, size, data, false, previousHint); }
int HintArgs(const char* name, int size, void* data) { return ReceiveHint(name, size, data, true, previousHintArgs); }
int ReloadWait(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (data && size >= 0)
    {
        std::int32_t seconds{};
        const auto result = hud::DecodeReloadWait({static_cast<const std::byte*>(data), std::size_t(size)}, seconds);
        if (result == hud::ParseResult::Malformed) { ++malformed; return 1; }
        if (result == hud::ParseResult::Accepted)
        {
            try
            {
                if (!localized) LoadLocalization();
                auto text = reloadWaitTemplate;
                const auto marker = text.find(L"%d");
                if (marker != text.npos) text.replace(marker, 2, std::to_wstring(seconds));
                // Original 0284EF24 -> 029C52B0 channel6. Reuse the same
                // scenario text renderer and its original .1/1.1/.1 timing.
                if (ShowScenarioRoundHudText(6, text, {255,195,35})) ++accepted;
            }
            catch (const std::exception& error)
            {
                if (!reported) engine.Con_DPrintf("CSOLSupport: scenario revival hint: %s\n", error.what());
                reported = true;
            }
            return 1;
        }
    }
    return previousWait ? previousWait(name, size, data) : 1;
}
int Init(const char* name, int size, void* data)
{ ResetScenarioRespawnUi(); return previousInit ? previousInit(name, size, data) : 1; }
int ResetLife(const char* name, int size, void* data)
{ state.ResetLife(); centerText.clear(); hidden = 0; requested = false; return previousReset ? previousReset(name, size, data) : 1; }
int Hide(const char* name, int size, void* data)
{ if (data && size == 1) hidden = *static_cast<unsigned char*>(data); return previousHide ? previousHide(name, size, data) : 1; }
}
void InitializeScenarioRespawnUi()
{
    InstallChainedUserMessage("MPToCL", Receive, previousMP);
    InstallChainedUserMessage("HudIcon", Icon, previousIcon);
    InstallChainedUserMessage("TextMsg", Text, previousText);
    InstallChainedUserMessage("HudTextPro", Hint, previousHint);
    InstallChainedUserMessage("HudTextArgs", HintArgs, previousHintArgs);
    InstallChainedUserMessage("ZBSVer2", ReloadWait, previousWait);
    InstallChainedUserMessage("InitHUD", Init, previousInit);
    InstallChainedUserMessage("ResetHUD", ResetLife, previousReset);
    InstallChainedUserMessage("HideWeapon", Hide, previousHide);
    if (!extension)
    {
        const auto factory = api->GetEngineFactory();
        surface = factory ? static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION, nullptr)) : nullptr;
        // VGUI2Extension's BaseUI uses the loaded vgui2 factory for these
        // public interfaces. Do not cast the unrelated native ClientVGUI ABI.
        const auto vguiModule = reinterpret_cast<HINTERFACEMODULE>(GetModuleHandleA("vgui2.dll"));
        const auto vguiFactory = vguiModule ? Sys_GetFactory(vguiModule) : nullptr;
        schemes = vguiFactory ? static_cast<vgui::ISchemeManager*>(vguiFactory(VGUI_SCHEME_INTERFACE_VERSION, nullptr)) : nullptr;
        panels = vguiFactory ? static_cast<vgui::IPanel*>(vguiFactory(VGUI_PANEL_INTERFACE_VERSION, nullptr)) : nullptr;
        panelGui = vguiFactory ? static_cast<vgui::IVGui*>(vguiFactory(VGUI_IVGUI_INTERFACE_VERSION, nullptr)) : nullptr;
        const auto module = api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto pluginFactory = module ? Sys_GetFactory(module) : nullptr;
        extension = pluginFactory ? static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION, nullptr)) : nullptr;
        if (extension && surface) extension->RegisterBaseUICallbacks(&overlay);
    }
}
void ResetScenarioRespawnUi()
{
    state.Reset(); centerText.clear(); reloadWaitTemplate.clear(); retryTemplate.clear(); centerTemplates = {}; hintTexts = {};
    budgetTitle.clear(); budgetUnlimited.clear(); budgetText.clear(); retryScoreboard.Reset();
    budgetTitleFont = budgetCountFont = 0; budgetScheme = 0;
    localized = reported = requested = intermission = false; hidden = 0; frameTime = 0; retryAt = 0;
}
void ScenarioRespawnUiFrame() { Prepare(); }
void ScenarioRespawnUiVideoInit() { ReleaseImages(); }
void DrawScenarioRespawnUi(float time, int inIntermission)
{
    intermission = inIntermission != 0;
    frameTime = time; requested = state.Active() && std::isfinite(time);
    if (requested && !intermission) DrawItems(time);
}
bool TryScenarioHealingSlot(int oneBasedSlot)
{
    SynchronizeSession();
    const auto* command = state.HealingCommand(oneBasedSlot);
    if (!command) return false;
    const bool gameplay = !gameUI && !console && !BuyMenuVisible() && !CharacterMenuVisible() &&
        !WeaponTextMenuVisible() && !intermission && !(hidden & 4) && view.health > 0 &&
        !view.spectator && (!surface || surface->HasFocus());
    if (*command && gameplay && engine.pfnServerCmd) { engine.pfnServerCmd(command); ++commands; }
    return true;
}
void ShutdownScenarioRespawnUi()
{
    ResetScenarioRespawnUi(); ReleaseImages();
    if (extension && surface) extension->UnregisterBaseUICallbacks(&overlay);
    extension = nullptr; surface = nullptr; schemes = nullptr; gameUI = console = false;
    panels = nullptr; panelGui = nullptr;
}
void ScenarioRespawnUiDiagnostics(std::ostream& out)
{
    out << "scenarioRevival active=" << state.Active() << " mask=" << state.Mask()
        << " localized=" << localized << " sprite=" << itemSprite << " fonts=" << centerFont << ',' << hintFont
        << " retry=" << state.RetryCountdown()
        << " retryBudget=" << state.Budget().remaining << '/' << state.Budget().initial
        << " retryBudgetDraws=" << budgetDraws << " scoreboardApi=" << (panels && panelGui)
        << " accepted=" << accepted << " malformed=" << malformed << " draws=" << draws << " commands=" << commands << '\n';
}
}
