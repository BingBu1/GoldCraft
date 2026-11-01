// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_round_hud_client.hpp"
#include "../scenario_round_hud.hpp"
#include "../scenario_localization.hpp"
#include "../scenario/dialogue_resources.hpp"
#include "../image_codec.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <VGUI/ISurface.h>
#include <VGUI/ISystem.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
namespace hud = scenario_hud;
hud::State state;
scenario::MessageTable messages;
std::map<std::uint16_t, std::vector<std::wstring>> lines;
vgui::ISurface* surface{};
vgui::ISystem* guiSystem{};
IVGUI2Extension* extension{};
std::array<int, hud::Resources.size()> textures{};
std::array<bool, hud::Resources.size()> imageReported{};
std::array<vgui::HFont, unsigned(hud::Font::Count)> fonts{};
struct DialogueTexture { int id{}; float retryAfter{}; bool reported{}; };
std::map<std::uint16_t, DialogueTexture> dialogueTextures;
HANDLE privateFont{};
pfnUserMsgHook previousMP{}, previousSync{}, previousInit{}, previousReset{}, previousHide{};
bool prepared{}, catalogLoaded{}, requested{}, gameUI{}, console{};
bool catalogReported{}, fontReported{};
int hidden{};
float frameTime{};
float catalogRetryAfter{}, imageRetryAfter{};
std::uint64_t accepted{}, malformed{}, missingMessages{}, sounds{};

float RealTime()
{
    if (guiSystem) return float(guiSystem->GetCurrentTime());
    static const auto start = GetTickCount64();
    return float(double(GetTickCount64() - start) / 1000.0);
}
std::wstring Wide(std::string_view text)
{
    if (text.empty()) return {};
    const auto convert = [&](UINT codePage)
    {
        const int size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
            text.data(), int(text.size()), nullptr, 0);
        std::wstring result(size > 0 ? size : 0, L'\0');
        if (size > 0) MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
            text.data(), int(text.size()), result.data(), size);
        return result;
    };
    auto result = convert(CP_UTF8);
    // The original CSO message table retains CP949 literal strings. The
    // localized Chinese resource itself is UTF16LE and does not use this.
    return result.empty() ? convert(949) : result;
}
std::u16string Utf16(std::span<const std::byte> bytes)
{
    if (bytes.size() >= 2 && bytes[0] == std::byte{0xff} && bytes[1] == std::byte{0xfe})
    {
        if (bytes.size() % 2) throw std::runtime_error("odd UTF16 localization length");
        std::u16string result;
        result.reserve(bytes.size() / 2 - 1);
        for (std::size_t at = 2; at < bytes.size(); at += 2)
            result += char16_t(std::to_integer<unsigned>(bytes[at]) |
                std::to_integer<unsigned>(bytes[at + 1]) << 8);
        while (!result.empty() && result.back() == u'\0') result.pop_back();
        return result;
    }
    const auto wide = Wide({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    return {wide.begin(), wide.end()};
}
void LoadCatalog()
{
    if (catalogLoaded || RealTime() < catalogRetryAfter) return;
    catalogRetryAfter = RealTime() + 5;
    try
    {
        scenario::MessageTable candidate;
        scenario::ConfigError error;
        if (!scenario::ReadMessageTableResource(ReadResource(hud::MessagePath), candidate, error))
            throw std::runtime_error(error.message);
        std::set<std::u16string> wanted;
        for (const auto& row : candidate.rows)
            if (row.text.starts_with('#'))
            {
                const auto key = Wide(std::string_view(row.text).substr(1));
                wanted.emplace(key.begin(), key.end());
            }
        hud::LocalizedTokens tokens;
        std::string localizationError;
        if (!hud::ReadLocalizedTokens(Utf16(ReadResource(hud::LocalizationPath)), wanted,
            tokens, localizationError)) throw std::runtime_error(localizationError);
        std::map<std::uint16_t, std::vector<std::wstring>> candidateLines;
        for (const auto& [id, index] : candidate.byId)
        {
            const auto& row = candidate.rows[index];
            auto text = Wide(row.text);
            if (row.text.starts_with('#'))
            {
                const auto key = text.substr(1);
                const auto token = tokens.find(std::u16string(key.begin(), key.end()));
                if (token != tokens.end()) text.assign(token->second.begin(), token->second.end());
            }
            auto& output = candidateLines[id];
            std::size_t first{};
            while (first < text.size())
            {
                const auto end = text.find(L'\n', first);
                auto line = text.substr(first, end == text.npos ? text.npos : end - first);
                std::erase(line, L'\r');
                output.push_back(std::move(line));
                if (end == text.npos) break;
                first = end + 1;
            }
        }
        messages = std::move(candidate);
        lines = std::move(candidateLines);
        catalogLoaded = true;
    }
    catch (const std::exception& error)
    {
        if (!catalogReported) engine.Con_DPrintf("CSOLSupport: scenario dialogue: %s\n", error.what());
        catalogReported = true;
    }
}
void ReleaseDialogueImages()
{
    if (surface) for (const auto& [id, texture] : dialogueTextures)
        if (texture.id > 0) surface->DeleteTextureByID(texture.id);
    dialogueTextures.clear();
}
void ReleaseImages()
{
    if (surface) for (int texture : textures) if (texture) surface->DeleteTextureByID(texture);
    textures = {};
    imageReported = {};
    prepared = false;
    imageRetryAfter = 0;
    ReleaseDialogueImages();
}
void Prepare()
{
    if (prepared || !surface || RealTime() < imageRetryAfter) return;
    imageRetryAfter = RealTime() + 5;
    for (std::size_t i = 0; i < textures.size(); ++i)
    {
        if (textures[i]) continue;
        try
        {
            const auto& resource = hud::Resources[i];
            const auto image = DecodeTGA(ReadResource(resource.path));
            if (image.Width != resource.width || image.Height != resource.height)
                throw std::runtime_error("scenario HUD image dimensions changed");
            const int texture = surface->CreateNewTextureID(true);
            if (texture) surface->DrawSetTextureRGBA(texture, image.RGBA.data(),
                image.Width, image.Height, 1, true);
            textures[i] = texture;
        }
        catch (const std::exception& error)
        {
            if (!imageReported[i]) engine.Con_DPrintf("CSOLSupport: %s: %s\n", hud::Resources[i].path, error.what());
            imageReported[i] = true;
        }
    }
    if (!privateFont)
    {
        try
        {
            const auto bytes = ReadResource(hud::FontPath);
            DWORD count{};
            privateFont = AddFontMemResourceEx(const_cast<std::byte*>(bytes.data()), DWORD(bytes.size()), nullptr, &count);
        }
        catch (const std::exception& error)
        {
            if (!fontReported) engine.Con_DPrintf("CSOLSupport: scenario font: %s\n", error.what());
            fontReported = true;
        }
    }
    // The actual Chinese TrackerScheme font definitions, not Arial fallbacks.
    constexpr int heights[]{33, 20, 24, 16, 14};
    for (std::size_t i = 0; privateFont && i < fonts.size(); ++i)
        if (!fonts[i])
        {
            const auto font = surface->CreateFont();
            if (font && surface->AddGlyphSetToFont(font, "DFYuanW9-GB", heights[i],
                i == unsigned(hud::Font::DefaultTitle) ? 500 : 0, 0, 0,
                vgui::FONTFLAG_ANTIALIAS, 0, 0xffff)) fonts[i] = font;
        }
    prepared = std::all_of(textures.begin(), textures.end(), [](int value) { return value != 0; }) &&
        std::all_of(fonts.begin(), fonts.end(), [](auto value) { return value != 0; });
}
void PrepareDialogueImages()
{
    if (!surface || !catalogLoaded) return;
    const float now = engine.GetClientTime();
    for (const auto& notice : state.ImageNotices())
    {
        if (!notice.active || now >= notice.die) continue;
        auto& texture = dialogueTextures[notice.id];
        if (texture.id || now < texture.retryAfter) continue;
        texture.retryAfter = now + 1;
        const auto* row = messages.Find(notice.id);
        const auto path = row ? scenario::DialogueImagePath(row->image) : std::nullopt;
        if (!path) { texture.id = -1; continue; }
        try
        {
            const auto image = DecodeTGA(ReadResource(path->c_str()));
            texture.id = surface->CreateNewTextureID(true);
            if (texture.id) surface->DrawSetTextureRGBA(texture.id, image.RGBA.data(),
                image.Width, image.Height, 1, true);
        }
        catch (const std::exception& error)
        {
            if (!texture.reported) engine.Con_DPrintf("CSOLSupport: %s: %s\n", path->c_str(), error.what());
            texture.reported = true;
        }
    }
}
void TextureQuad(int texture, int x, int y, int width, int height, int alpha,
    float u0 = 0, float u1 = 1)
{
    if (texture <= 0 || width <= 0 || height <= 0 || alpha <= 0) return;
    surface->DrawSetColor(255, 255, 255, alpha);
    surface->DrawSetTexture(texture);
    vgui::VGuiVertex vertices[]{{x,y,u0,0},{x+width,y,u1,0},
        {x+width,y+height,u1,1},{x,y+height,u0,1}};
    surface->DrawTexturedPolygon(vertices, 4);
}
void Quad(hud::Image image, int x, int y, int width, int height, int alpha = 255,
    float u0 = 0, float u1 = 1)
{
    TextureQuad(textures[unsigned(image)], x, y, width, height, alpha, u0, u1);
}
void Text(vgui::HFont font, const wchar_t* text, int centerX, int y,
    const std::array<std::uint8_t, 3>& color, int alpha)
{
    if (!font || !*text) return;
    int width{}, height{};
    surface->GetTextSize(font, text, width, height);
    surface->DrawSetTextFont(font);
    surface->DrawSetTextColor(color[0], color[1], color[2], alpha);
    surface->DrawSetTextPos(centerX - width / 2, y);
    surface->DrawPrintText(text, int(std::wcslen(text)));
    surface->DrawFlushText();
}
void Paint()
{
    if (!requested) return;
    requested = false;
    if (!surface || gameUI || console || (hidden & 4) ||
        engine.pfnGetCvarFloat("hud_draw") == 0) return;
    int width{}, height{};
    surface->GetScreenSize(width, height);
    const auto root = surface->GetEmbeddedPanel();
    if (!root || width <= 0 || height <= 0) return;
    const auto* local = engine.GetLocalPlayer();
    const auto frame = state.Layout(width, local ? local->index : 0, frameTime);
    surface->PushMakeCurrent(root, false);
    for (unsigned i = 0; i < frame.count; ++i)
    {
        const auto& q = frame.quads[i];
        Quad(q.image, q.x, q.y, q.width, q.height, q.alpha, q.u0, q.u1);
    }
    for (unsigned channel = 0; channel < state.ImageNotices().size(); ++channel)
    {
        const auto& notice = state.ImageNotices()[channel];
        const int alpha = hud::NoticeAlpha(notice, frameTime);
        const auto texture = dialogueTextures.find(notice.id);
        if (!alpha || texture == dialogueTextures.end()) continue;
        const auto rect = hud::DialogueImageRect(channel, width, height);
        TextureQuad(texture->second.id, rect.x, rect.y, rect.width, rect.height, alpha);
    }
    const float now = RealTime();
    for (unsigned channel = 0; channel < state.Notices().size(); ++channel)
    {
        const auto& notice = state.Notices()[channel];
        const int alpha = hud::NoticeAlpha(notice, now);
        if (!alpha) continue;
        const auto found = lines.find(notice.id);
        if (found == lines.end() || found->second.empty()) continue;
        const auto font = fonts[unsigned(hud::TextChannels[channel].font)];
        if (!font) continue;
        const float x = float(width) * notice.x, y = float(height) * notice.y;
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 100000 || std::abs(y) > 100000) continue;
        // The original mode15/17 root enables the 824x32 background. Channel
        // 18 uses its own 444x32 background; a leading space clears it.
        if (!found->second.front().empty() && found->second.front().front() != L' ')
        {
            const int backgroundWidth = channel == 18 ? 444 : 824;
            const int left = int(x - backgroundWidth / 2), top = int(y - 16);
            Quad(hud::Image::TextLeft, left, top, 128, 32, alpha);
            Quad(hud::Image::TextCenter, left + 128, top, backgroundWidth - 256, 32, alpha);
            Quad(hud::Image::TextRight, left + backgroundWidth - 128, top, 128, 32, alpha);
        }
        const int lineHeight = surface->GetFontTall(font);
        int top = int(y) - int(found->second.size()) * lineHeight / 2;
        for (const auto& line : found->second)
        {
            Text(font, line.c_str(), int(x), top, notice.color, alpha);
            top += lineHeight;
        }
    }
    surface->PopMakeCurrent(root);
}
class Overlay final : public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude() const override { return 24; }
    void Initialize(CreateInterfaceFn*, int) override {}
    void Start(cl_enginefuncs_s*, int) override {}
    void Shutdown() override
    { ResetScenarioRoundHud(); ReleaseImages(); surface = nullptr; guiSystem = nullptr; fonts = {}; }
    void Key_Event(int&, int&, const char*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceAppProc(void*&, void*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceWndProc(void*&, unsigned int&, unsigned int&, long&, VGUI2Extension_CallbackContext*) override {}
    void Paint(int&, int&, int&, int&, VGUI2Extension_CallbackContext* context) override
    { if (context->IsPost) csol::client::Paint(); }
    void HideGameUI(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) gameUI = false; }
    void ActivateGameUI(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) gameUI = true; }
    void HideConsole(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) console = false; }
    void ShowConsole(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) console = true; }
} overlay;
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (data && size >= 0)
    {
        hud::Message message;
        const auto result = hud::DecodeMP({static_cast<const std::byte*>(data), std::size_t(size)}, message);
        if (result == hud::ParseResult::Malformed) { ++malformed; return 1; }
        if (result == hud::ParseResult::Accepted)
        {
            ++accepted;
            if (message.kind != hud::Kind::Dialogue) state.Apply(message, engine.GetClientTime());
            else
            {
                LoadCatalog();
                if (const auto* row = messages.Find(std::uint16_t(message.value)))
                {
                    if (!row->text.empty()) state.Show(*row, RealTime());
                    if (!row->image.empty()) state.ShowImage(*row, engine.GetClientTime());
                    const auto sound = scenario::DialogueSoundPath(row->sound);
                    if (sound && engine.pfnPlaySoundByNameAtPitch)
                    { engine.pfnPlaySoundByNameAtPitch(sound->c_str(), 1, 100); ++sounds; }
                }
                else ++missingMessages;
            }
        }
    }
    return previousMP ? previousMP(name, size, data) : 1;
}
int Sync(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (data && size >= 0)
    {
        hud::Message message;
        const auto result = hud::DecodeValueSync({static_cast<const std::byte*>(data), std::size_t(size)}, message);
        if (result == hud::ParseResult::Malformed) { ++malformed; return 1; }
        if (result == hud::ParseResult::Accepted) { state.Apply(message, engine.GetClientTime()); ++accepted; }
    }
    return previousSync ? previousSync(name, size, data) : 1;
}
int Init(const char* name, int size, void* data)
{ ResetScenarioRoundHud(); return previousInit ? previousInit(name, size, data) : 1; }
int ResetLife(const char* name, int size, void* data)
{ state.ClearPresentation(); hidden = 0; requested = false; return previousReset ? previousReset(name, size, data) : 1; }
int Hide(const char* name, int size, void* data)
{ if (data && size == 1) hidden = *static_cast<unsigned char*>(data); return previousHide ? previousHide(name, size, data) : 1; }
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    const auto old = api->HookUserMsg(name, hook);
    if (old != hook) previous = old;
    if (!old) engine.pfnHookUserMsg(name, hook);
}
}
void InitializeScenarioRoundHud()
{
    Install("MPToCL", Receive, previousMP); Install("ValueSync", Sync, previousSync);
    Install("InitHUD", Init, previousInit); Install("ResetHUD", ResetLife, previousReset);
    Install("HideWeapon", Hide, previousHide);
    if (!extension)
    {
        const auto factory = api->GetEngineFactory();
        surface = factory ? static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION, nullptr)) : nullptr;
        guiSystem = factory ? static_cast<vgui::ISystem*>(factory(VGUI_SYSTEM_INTERFACE_VERSION, nullptr)) : nullptr;
        const auto module = api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto pluginFactory = module ? Sys_GetFactory(module) : nullptr;
        extension = pluginFactory ? static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION, nullptr)) : nullptr;
        if (extension && surface) extension->RegisterBaseUICallbacks(&overlay);
    }
}
void ResetScenarioRoundHud()
{
    state.Reset(); messages = {}; lines.clear(); catalogLoaded = false;
    ReleaseDialogueImages(); catalogRetryAfter = 0; catalogReported = false;
    requested = false; hidden = 0; frameTime = 0;
}
void ScenarioRoundHudFrame()
{
    if (state.Active()) { LoadCatalog(); Prepare(); }
    else if (catalogLoaded) Prepare();
    PrepareDialogueImages();
}
void ScenarioRoundHudVideoInit() { ReleaseImages(); }
void DrawScenarioRoundHud(float time, int intermission)
{ frameTime = time; requested = !intermission && (state.Active() || catalogLoaded); }
void ShutdownScenarioRoundHud()
{
    ResetScenarioRoundHud(); ReleaseImages();
    if (extension && surface) extension->UnregisterBaseUICallbacks(&overlay);
    if (privateFont) RemoveFontMemResourceEx(privateFont);
    privateFont = nullptr; fonts = {}; extension = nullptr; surface = nullptr; guiSystem = nullptr;
    gameUI = console = fontReported = false;
}
void ScenarioRoundHudDiagnostics(std::ostream& out)
{
    out << "scenarioRound active=" << state.Active() << " prepared=" << prepared
        << " messageRows=" << messages.rows.size() << " localizedRows=" << lines.size()
        << " accepted=" << accepted << " malformed=" << malformed
        << " unknownMessages=" << missingMessages << " sounds=" << sounds << '\n';
}
}
