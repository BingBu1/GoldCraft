// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "wall_upgrade_hud.hpp"
#include "scenario_round_hud_client.hpp"
#include "../../../scenario/core/scenario_localization.hpp"
#include "../../../scenario/core/scenario_round_hud.hpp"
#include "../../../image_codec.hpp"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <VGUI/ISurface.h>
#include <VGUI/ILocalize.h>
#include <VGUI/IScheme.h>

namespace csol::client
{
namespace
{
// Original client02A5B67F/029CB650; root/parent layout is documented in the
// wall upgrade report. These are pixels, not a guessed 640x480 scaling rule.
constexpr int Left = 10, Top = 445 - 12, Width = 204, Height = 28;
constexpr char Background[] = "resource/Hud/ZBS/zbslevelupwallbg.tga";
constexpr char SchemePath[] = "resource/csol/wall/v1/WallUpgradeScheme.res";
bool enabled{}, textLoaded{}, reported{}, layoutDirty{true};
int level{1}, texture{};
vgui::HFont font{};
void* privateFont{};
vgui::ILocalize* localize{};
vgui::ISchemeManager* schemes{};
int fontScreenWidth{}, fontScreenHeight{};
std::wstring title, noticeFormat, levelText;
int titleX{}, titleY{}, levelY{};
std::uint64_t epoch{};
ULONGLONG textRetry{}, imageRetry{};

CreateInterfaceFn VGuiFactory()
{
    // The engine owns ISurface; ILocalize and ISchemeManager belong to vgui2.
    // Match the deployed MetaHook Controls/BaseUI interface initialization.
    const auto module = reinterpret_cast<HINTERFACEMODULE>(GetModuleHandleA("vgui2.dll"));
    return module ? Sys_GetFactory(module) : nullptr;
}

void Report(const std::exception& error)
{
    if (!reported) engine.Con_DPrintf("CSOLSupport: wall upgrade HUD: %s\n", error.what());
    reported = true;
}

bool LoadText()
{
    if (textLoaded) return true;
    if (GetTickCount64() < textRetry) return false;
    textRetry = GetTickCount64() + 5000;
    const auto owner = epoch;
    try
    {
        const auto bytes = ReadResource("resource/cso_chn.txt");
        if (owner != epoch) return false;
        if (bytes.size() < 2 || bytes.size() % 2 || bytes[0] != std::byte{0xff} || bytes[1] != std::byte{0xfe})
            throw std::runtime_error("wall localization must be UTF16LE");
        std::u16string content;
        content.reserve(bytes.size() / 2 - 1);
        for (std::size_t i = 2; i < bytes.size(); i += 2)
            content += char16_t(std::to_integer<unsigned>(bytes[i]) |
                (std::to_integer<unsigned>(bytes[i + 1]) << 8));
        while (!content.empty() && !content.back()) content.pop_back();
        scenario_hud::LocalizedTokens tokens;
        std::string error;
        const std::u16string titleKey = u"CSO_GuardWall", noticeKey = u"CSO_ZBS_WallUpgradeMsg";
        if (!scenario_hud::ReadLocalizedTokens(content, {titleKey, noticeKey}, tokens, error))
            throw std::runtime_error(error);
        if (!tokens.contains(titleKey) || !tokens.contains(noticeKey))
            throw std::runtime_error("missing wall upgrade localization");
        const auto& format = tokens.at(noticeKey);
        unsigned names{}, levels{};
        for (std::size_t i = 0; i < format.size(); ++i)
            if (format[i] == u'%')
            {
                if (++i == format.size()) throw std::runtime_error("invalid wall notice format");
                if (format[i] == u's') ++names;
                else if (format[i] == u'd') ++levels;
                else if (format[i] != u'%') throw std::runtime_error("invalid wall notice format");
            }
        if (names != 1 || levels != 1) throw std::runtime_error("invalid wall notice arguments");
        title.assign(tokens.at(titleKey).begin(), tokens.at(titleKey).end());
        noticeFormat.assign(format.begin(), format.end());
        textLoaded = true;
        layoutDirty = true;
        return true;
    }
    catch (const std::exception& error) { Report(error); }
    return false;
}

void ShowUpgrade(unsigned slot)
{
    if (slot < 1 || slot > 32 || !engine.pfnGetPlayerInfo || !api) return;
    const auto owner = epoch;
    const int announcedLevel = level;
    hud_player_info_t player{};
    engine.pfnGetPlayerInfo(int(slot), &player);
    if (owner != epoch || !player.name) return;
    const std::string name(player.name);
    if (!LoadText() || owner != epoch) return;
    if (!localize)
    {
        const auto factory = VGuiFactory();
        auto* candidate = factory ? static_cast<vgui::ILocalize*>(factory(VGUI_LOCALIZE_INTERFACE_VERSION, nullptr)) : nullptr;
        if (owner != epoch) return;
        localize = candidate;
    }
    if (!localize) { Report(std::runtime_error("VGUI_Localize003 unavailable from vgui2.dll")); return; }
    // HW37A6E790 uses the native localization conversion with a 0x3B-byte
    // output limit before formatting the name and the current wall level.
    wchar_t wideName[60]{};
    localize->ConvertANSIToUnicode(name.c_str(), wideName, 0x3b);
    if (owner != epoch) return;
    wideName[std::size(wideName) - 1] = 0;
    std::wstring text;
    for (std::size_t i = 0; i < noticeFormat.size(); ++i)
    {
        if (noticeFormat[i] != L'%') { text += noticeFormat[i]; continue; }
        const auto kind = noticeFormat[++i];
        if (kind == L's') text += wideName;
        else if (kind == L'd') text += std::to_wstring(announcedLevel);
        else text += L'%';
    }
    ShowScenarioRoundHudText(1, text, {220, 255, 128});
}
}

bool ConsumeWallUpgradeHud(int size, const void* data)
{
    if (!data || size < 1) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    // Original MPToCL9 ->0281A700->02A5CE60. Other consumers still receive
    // the common reset; this handler owns only its wall fields.
    if (size == 1 && bytes[0] == 9) { ResetWallUpgradeHud(); return false; }
    if (size < 2) return false;
    if (bytes[0] != 27) return false;
    if (bytes[1] == 10)
    {
        if (size == 4) { ++epoch; enabled = bytes[2] != 0; level = bytes[3]; layoutDirty = true; }
        return true;
    }
    if (bytes[1] == 11)
    {
        if (size == 3) ShowUpgrade(bytes[2]);
        return true;
    }
    return false;
}

bool WallUpgradeHudVisible() { return enabled; }

void PrepareWallUpgradeHud(vgui::ISurface* surface)
{
    if (!enabled || !surface) return;
    const auto owner = epoch;
    int screenWidth{}, screenHeight{};
    surface->GetScreenSize(screenWidth, screenHeight);
    if (owner != epoch) return;
    if (font && (screenWidth != fontScreenWidth || screenHeight != fontScreenHeight))
    { font = 0; layoutDirty = true; imageRetry = 0; }
    LoadText();
    if (owner != epoch) return;
    if ((!texture || !font) && GetTickCount64() >= imageRetry)
    {
        imageRetry = GetTickCount64() + 5000;
        try
        {
            if (!texture)
            {
                const auto bytes = ReadResource(Background);
                if (owner != epoch) return;
                const auto image = DecodeTGA(bytes);
                if (image.Width != Width || image.Height != Height)
                    throw std::runtime_error("wall background dimensions changed");
                const int candidate = surface->CreateNewTextureID(true);
                if (owner != epoch) { if (candidate) surface->DeleteTextureByID(candidate); return; }
                if (candidate) surface->DrawSetTextureRGBA(candidate, image.RGBA.data(), Width, Height, 1, true);
                if (owner != epoch) { if (candidate) surface->DeleteTextureByID(candidate); return; }
                texture = candidate;
            }
            if (!privateFont)
            {
                const auto bytes = ReadResource(scenario_hud::FontPath);
                if (owner != epoch) return;
                DWORD count{};
                privateFont = AddFontMemResourceEx(const_cast<std::byte*>(bytes.data()),
                    DWORD(bytes.size()), nullptr, &count);
                if (owner != epoch) return;
            }
            if (privateFont && !font)
            {
                if (!schemes)
                {
                    const auto factory = VGuiFactory();
                    auto* candidate = factory ? static_cast<vgui::ISchemeManager*>(factory(VGUI_SCHEME_INTERFACE_VERSION, nullptr)) : nullptr;
                    if (owner != epoch) return;
                    schemes = candidate;
                }
                if (!schemes) throw std::runtime_error("VGUI_Scheme009 unavailable from vgui2.dll");
                if (ReadResource(SchemePath).empty()) throw std::runtime_error("missing wall upgrade font scheme");
                if (owner != epoch) return;
                const auto handle = schemes->LoadSchemeFromFile(SchemePath, "CSOLWallUpgradeV1");
                if (owner != epoch) return;
                auto* scheme = handle ? schemes->GetIScheme(handle) : nullptr;
                if (owner != epoch) return;
                const auto* revision = scheme ? scheme->GetResourceString("CSOLWallUpgradeRevision") : nullptr;
                if (owner != epoch) return;
                if (!revision || std::string_view(revision) != "1")
                    throw std::runtime_error("invalid wall upgrade font scheme");
                // Original Label02C8F530 asks the scheme for Default. Resource
                // tall=9 is input to the scheme's language/resolution policy,
                // not an instruction to bypass that policy with a 9px font.
                const auto candidate = scheme->GetFont("Default", false);
                if (owner != epoch) return;
                if (!candidate) throw std::runtime_error("wall upgrade Default font unavailable");
                font = candidate; layoutDirty = true;
                fontScreenWidth = screenWidth; fontScreenHeight = screenHeight;
            }
        }
        catch (const std::exception& error) { Report(error); }
    }
    if (owner != epoch || !font || !textLoaded || !layoutDirty) return;
    levelText = level >= 28 ? L"Level MAX" : L"Level " + std::to_wstring(level);
    int width{}, height{};
    surface->GetTextSize(font, title.c_str(), width, height);
    if (owner != epoch) return;
    // Original Label aligns overwide text west; otherwise the title is
    // centered. Both resource labels vertically center inside their 22px box.
    titleX = Left + 3 + (width > 84 ? 0 : (84 - width) / 2);
    titleY = Top + 4 + (22 - height) / 2;
    surface->GetTextSize(font, levelText.c_str(), width, height);
    if (owner != epoch) return;
    levelY = Top + 4 + (22 - height) / 2;
    layoutDirty = false;
}

void PaintWallUpgradeHud(vgui::ISurface* surface)
{
    if (!enabled || !surface || !texture || !font || !textLoaded || layoutDirty) return;
    const auto root = surface->GetEmbeddedPanel();
    if (!root) return;
    surface->PushMakeCurrent(root, false);
    surface->DrawSetColor(255, 255, 255, 255);
    surface->DrawSetTexture(texture);
    surface->DrawTexturedRect(Left, Top, Left + Width, Top + Height);
    surface->DrawSetTextFont(font);
    surface->DrawSetTextColor(255, 255, 255, 255);
    surface->DrawSetTextPos(titleX, titleY);
    surface->DrawPrintText(title.c_str(), int(title.size()));
    surface->DrawSetTextPos(Left + 98, levelY);
    surface->DrawPrintText(levelText.c_str(), int(levelText.size()));
    surface->DrawFlushText();
    surface->PopMakeCurrent(root);
}

void ResetWallUpgradeHud()
{
    ++epoch;
    enabled = false; level = 1; layoutDirty = true; levelText.clear();
}
void InvalidateWallUpgradeHud(vgui::ISurface* surface)
{
    ++epoch;
    const int retired = texture; texture = 0;
    font = 0;
    textLoaded = false; reported = false; layoutDirty = true;
    title.clear(); noticeFormat.clear(); levelText.clear(); textRetry = imageRetry = 0;
    if (surface && retired) surface->DeleteTextureByID(retired);
}
void ShutdownWallUpgradeHud(vgui::ISurface* surface)
{
    ResetWallUpgradeHud(); InvalidateWallUpgradeHud(surface);
    if (privateFont) RemoveFontMemResourceEx(privateFont);
    privateFont = nullptr; font = 0; localize = nullptr; schemes = nullptr;
    fontScreenWidth = fontScreenHeight = 0;
}
}
