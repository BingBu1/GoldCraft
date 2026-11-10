// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>

namespace csol::menu {

struct Rect {
    int x{}, y{}, w{}, h{};

    bool Contains(int px, int py) const noexcept
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

// CSO client 01DE6070/01DE5FE0: panel-local coordinates use separate X/Y
// factors. Only a root panel receives the horizontal widescreen margin.
// Position and size are truncated separately, including at each parent.
class Viewport {
public:
    Viewport(int width, int height) noexcept
        : yScale_(float(height) * (1.0f / 480.0f))
    {
        const int aspectWidth = int(float(height) / 0.75f);
        const bool wide = width > aspectWidth;
        xScale_ = float(wide ? aspectWidth : width) * (1.0f / 640.0f);
        margin_ = wide ? float(width - aspectWidth) * 0.5f : 0.0f;
    }

    Rect Root(float x, float y, float w, float h) const noexcept
    {
        return {int(float(int(x * xScale_)) + margin_), int(y * yScale_),
                int(w * xScale_), int(h * yScale_)};
    }

    Rect Child(const Rect& parent, float x, float y, float w, float h) const noexcept
    {
        return {parent.x + int(x * xScale_), parent.y + int(y * yScale_),
                int(w * xScale_), int(h * yScale_)};
    }

    float XScale() const noexcept { return xScale_; }
    float YScale() const noexcept { return yScale_; }

private:
    float xScale_{}, yScale_{}, margin_{};
};

inline Rect CloseButton(const Rect& root, int screenHeight) noexcept
{
    // Frame::PerformLayout / ApplyScheme use literal proportional pixels,
    // independently of the menu's X scale and its resource coordinates.
    const float q = float(screenHeight) / 480.0f;
    const int inset = int(12.0f * q), side = int(18.0f * q);
    return {root.x + root.w - inset - int(20.0f * q), root.y + inset, side, side};
}

inline std::array<Rect,2> TeamTabs(const Viewport& viewport, const Rect& root,
                                 int rawX, int rawY, int rawHeight,
                                 const std::array<int,2>& textWidths) noexcept
{
    const auto first = viewport.Child(root,rawX,rawY,0,rawHeight);
    const int firstWidth = textWidths[0] + 14;
    return {{{first.x,first.y,firstWidth,first.h},
             {first.x+firstWidth+1,first.y,textWidths[1]+14,first.h}}};
}

// TabButton::PerformLayout 02CA7790 -> 02CA7E70 uses the native
// image cap width even when the panel's height is proportional.
inline std::array<Rect,3> TabSlices(const Rect& panel, int capWidth) noexcept
{
    return {{{panel.x,panel.y,capWidth,panel.h},
             {panel.x+capWidth,panel.y,panel.w-2*capWidth,panel.h},
             {panel.x+panel.w-capWidth,panel.y,capWidth,panel.h}}};
}

// UnitImageProgressBarMirror 02C958F0 stores each float operation before
// converting to double, adding 0.0001 and truncating. It is not rounding.
inline int ProgressMirror(float progress, float valueScale) noexcept
{
    const float percent = progress * 100.0f;
    const float value = percent * valueScale;
    return int(double(value) + 0.0001);
}

struct SearchPlacement { Rect title, search, help; };

inline SearchPlacement BuySearch(const Viewport& viewport, const Rect& parent,
                                int titleWidth, int titleHeight,
                                float controlScale) noexcept
{
    auto title = viewport.Child(parent,26*controlScale,40*controlScale,0,0);
    auto search = viewport.Child(parent,0,40*controlScale,145*controlScale,23*controlScale);
    search.x = title.x + titleWidth + 9;
    search.h = (std::min)(search.h,24);
    title.y = search.y + (search.h-titleHeight)/2;
    title.w = titleWidth;title.h = titleHeight;
    auto help = viewport.Child(parent,0,40*controlScale,0,0);
    help.x = search.x+search.w+4;++help.y;help.w=help.h=25;
    return {title,search,help};
}

} // namespace csol::menu
