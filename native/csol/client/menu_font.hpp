// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <VGUI/ISurface.h>
#include <VGUI/IScheme.h>
#include <algorithm>
#include <string_view>
#include <string>
#include <vector>

namespace csol::client {

// A configured HFont cannot be resized with AddGlyphSetToFont. Keep immutable
// font handles by recipe, and use the measured height when positioning text.
// This is a host-lifetime adapter, not a replacement for CSO's font manager.
struct MeasuredMenuFont {
    vgui::HFont handle{};
    int tall{};
    int recipeTall{};
};

class MenuFontCache {
public:
    // Current x64 CSO vgui2 LoadFonts (180030B10) fixes its font viewport
    // at 1024x768. Its no-yres proportional recipes use double tall*768/480,
    // truncate toward zero, cap at 128, and apply no language minimum.
    // HW forwards that recipe unchanged to CreateFontA; its measured height
    // can differ. This evidence does not establish the old x86 CSO scheme.
    MeasuredMenuFont GetCSOScaled(vgui::ISurface& surface,
                                 const std::string& family,
                                 int normalizedTall, int weight, int flags,
                                 bool proportional = true)
    {
        const int tall = (std::min)(proportional
            ? static_cast<int>((768.0 / 480.0) * normalizedTall)
            : normalizedTall, 128);
        const auto handle = Get(surface, family, tall, weight, flags);
        return {handle, handle ? surface.GetFontTall(handle) : 0, tall};
    }

    // Host adaptation of Scheme2::BuildFontGlyphs for the evidenced menu
    // recipes without yres. Old x86 CSO vgui2.dll is unavailable: this does
    // not establish its scaling algorithm. Language comes from the same
    // IVGUI2Extension::GetCurrentLanguage source used by the host scheme.
    MeasuredMenuFont GetHostScaled(vgui::ISurface& surface,
                                  vgui::ISchemeManager& schemes,
                                  const char* language, const std::string& family,
                                  int normalizedTall, int weight, int flags,
                                  bool proportional = true)
    {
        int tall = proportional ? schemes.GetProportionalScaledValue(normalizedTall)
                                : normalizedTall;
        tall = (std::max)((std::min)(tall, 127), MinimumHeight(language));
        const auto handle = Get(surface, family, tall, weight, flags);
        return {handle, handle ? surface.GetFontTall(handle) : 0, tall};
    }

    vgui::HFont Get(vgui::ISurface& surface, const std::string& family,
                    int tall, int weight, int flags)
    {
        for (const auto& font : fonts_)
            if (font.family == family && font.tall == tall &&
                font.weight == weight && font.flags == flags)
                return font.handle;

        const auto handle = surface.CreateFont();
        const bool configured = handle && surface.AddGlyphSetToFont(
            handle, family.c_str(), tall, weight, 0, 0, flags, 0, 0xffff);
        fonts_.push_back({family, tall, weight, flags, configured ? handle : 0});
        return configured ? handle : 0;
    }

    // The resource transaction rebuilds an existing HFont in place. Publish
    // its new family only after resource reads resume, including on rollback.
    // Forgetting the recipe here would allocate another permanent host HFont.
    void RebindFamily(vgui::HFont handle, const std::string& family)
    {
        if (!handle) return;
        for (auto& font : fonts_)
            if (font.handle == handle) {
                font.family = family;
                return;
            }
    }

    void InvalidateFailures()
    {
        std::erase_if(fonts_, [](const auto& font) { return !font.handle; });
    }

    void Invalidate() { fonts_.clear(); }

private:
    static int MinimumHeight(const char* language)
    {
        const std::string_view value = language ? language : "";
        const auto equal = [value](std::string_view expected) {
            if (value.size() != expected.size()) return false;
            for (std::size_t i = 0; i < value.size(); ++i) {
                const char c = value[i] >= 'A' && value[i] <= 'Z'
                    ? static_cast<char>(value[i] + ('a' - 'A')) : value[i];
                if (c != expected[i]) return false;
            }
            return true;
        };
        if (equal("korean") || equal("tchinese") || equal("schinese") || equal("japanese"))
            return 13;
        return equal("thai") ? 18 : 0;
    }

    struct Font {
        std::string family;
        int tall{}, weight{}, flags{};
        vgui::HFont handle{};
    };
    std::vector<Font> fonts_;
};

} // namespace csol::client
