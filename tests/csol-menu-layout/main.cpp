// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/menu_layout.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks{};
void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL %s\n",name); std::exit(1); }
}
void Equal(csol::menu::Rect actual, csol::menu::Rect expected, const char* name)
{
    Check(actual.x==expected.x && actual.y==expected.y && actual.w==expected.w && actual.h==expected.h,name);
}
}

int main()
{
    using namespace csol::menu;
    // Literal samples of the original per-panel conversion (01DE6070), not
    // a second implementation used as an oracle. Nonintegral scales expose
    // flattening, position/size rounding and widescreen-margin regressions.
    struct Case { int width,height; Rect buy,characters; };
    constexpr Case cases[]{
        {640,480,{17,22,606,444},{17,30,606,420}},
        {800,600,{21,27,757,555},{21,37,757,525}},
        {1024,768,{27,35,969,710},{27,48,969,672}},
        {1280,720,{185,33,909,666},{185,45,909,630}},
        {1920,1080,{278,49,1363,999},{278,67,1363,945}},
        {1116,780,{65,35,984,721},{65,48,984,682}}
    };
    for (const auto& sample:cases) {
        const Viewport viewport(sample.width,sample.height);
        Equal(viewport.Root(17,22,606,444),sample.buy,"buy root");
        Equal(viewport.Root(17,30,606,420),sample.characters,"class root");
    }
    const Viewport viewport(1116,780);
    const auto buy=viewport.Root(17,22,606,444);
    const auto favorites=viewport.Child(buy,372,30,215,162);
    Equal(favorites,{669,83,349,263},"favorites parent");
    const auto first=viewport.Child(favorites,0,26,106,31);
    Equal(first,{669,125,172,50},"favorites child truncates at each parent");
    Check(first.y!=viewport.Child(buy,372,56,106,31).y,"flattened favorite position is wrong");
    Equal(viewport.Child(favorites,109,26,106,31),{846,125,172,50},"second favorite");
    Check(first.Contains(669,125) && first.Contains(840,174),"inclusive leading pixel");
    Check(!first.Contains(841,174) && !first.Contains(840,175),"half-open trailing edge");

    Equal(CloseButton(buy,780),{998,54,29,29},"close has independent proportional inset");
    const auto search=BuySearch(viewport,buy,100,17,0.625f);
    Equal(search.title,{91,78,100,17},"measured inner title");
    Equal(search.search,{200,75,147,23},"search follows measured title");
    Equal(search.help,{351,76,25,25},"help has actual pixel dimensions");
    const auto tallSearch=BuySearch(Viewport(1920,1080),cases[4].buy,160,29,0.625f);
    Check(tallSearch.search.h==24,"search height cap");
    const auto tabs=TeamTabs(viewport,buy,16,40,17,{90,80});
    Equal(tabs[0],{91,100,104,27},"tab width is measured text plus fourteen actual pixels");
    Equal(tabs[1],{196,100,94,27},"tab gap is one actual pixel");
    const auto slices=TabSlices(tabs[0],7);
    Equal(slices[0],{91,100,7,27},"native left cap");
    Equal(slices[1],{98,100,90,27},"stretched center");
    Equal(slices[2],{188,100,7,27},"native right cap");
    Check(slices[0].w!=int(7*viewport.YScale()),"caps must not be proportionally enlarged");

    for(int value=0;value<=255;++value)
        Check(ProgressMirror(float(value)/30.0f,0.3f)==value,"ClassMod integer caps");
    Check(ProgressMirror(0.765f,0.3f)==22,"mirror truncates instead of rounding");
    Check(ProgressMirror(0.333f,0.3f)==9,"mirror is not a percentage");
    Check(ProgressMirror(1.5f,0.3f)==45,"mirror does not clamp at thirty");
    Check(ProgressMirror(-0.1f,0.3f)==-2,"original positive epsilon and truncate order");
    std::printf("%u layout checks passed\n",checks);
}
