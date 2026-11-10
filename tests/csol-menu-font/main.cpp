// SPDX-License-Identifier: GPL-3.0-or-later
#include "tests/csol-hud-style/allocation_probe.hpp"
#include "native/csol/client/menu_font.hpp"
#include "tests/csol-hud-style/surface_stub.hpp"
#include <cstdlib>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

unsigned checks{};
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
struct Surface : vgui::HudTestSurface {
    vgui::HFont next{1};
    int added{}, rejected{}, measured{}, metricDelta{},screenReads{};
    int width=640,height=480;
    int expectedFlags{vgui::FONTFLAG_ANTIALIAS};
    bool fail{};
    std::map<vgui::HFont,int> heights;
    vgui::HFont CreateFont() override { return next++; }
    bool AddGlyphSetToFont(vgui::HFont h,const char*,int tall,int weight,int blur,
                           int scanlines,int flags,int low,int high) override {
        ++added;
        Check(weight==0 && blur==0 && scanlines==0 && flags==expectedFlags
              && low==0 && high==0xffff,"original recipe changed");
        if (fail || heights.contains(h)) { ++rejected; return false; }
        heights[h]=tall+metricDelta;
        return true;
    }
    int GetFontTall(vgui::HFont h) override { ++measured; return heights.at(h); }
    void GetScreenSize(int& w,int& h) override {++screenReads;w=width;h=height;}
};
struct Schemes : vgui::ISchemeManager {
    int screenTall{480}, calls{};
    std::vector<int> requested;
    vgui::HScheme LoadSchemeFromFile(const char*,const char*) override {return 0;}
    void ReloadSchemes() override {}
    vgui::HScheme GetDefaultScheme() override {return 0;}
    vgui::HScheme GetScheme(const char*) override {return 0;}
    vgui::IImage* GetImage(const char*,bool) override {return nullptr;}
    vgui::HTexture GetImageID(const char*,bool) override {return 0;}
    vgui::IScheme* GetIScheme(vgui::HScheme) override {return nullptr;}
    void Shutdown(bool) override {}
    int GetProportionalScaledValue(int v) override {
        ++calls; requested.push_back(v); return static_cast<int>(v*(screenTall/480.0f));
    }
    int GetProportionalNormalizedValue(int v) override {return v;}
};
int main() {
    constexpr auto aa=vgui::FONTFLAG_ANTIALIAS;
    Surface surface; Schemes schemes; csol::client::MenuFontCache cache;
    const auto get=[&](int tall,const char* lang="english",bool proportional=true) {
        return cache.GetHostScaled(surface,schemes,lang,"DFYuanW9-GB",tall,0,aa,proportional);
    };
    auto initial=get(9);
    Check(initial.handle && initial.tall==9,"initial recipe");
    Check(get(9).handle==initial.handle && surface.added==1,"same recipe reconfigured");
    schemes.screenTall=960;
    auto resized=get(9);
    Check(resized.handle!=initial.handle && resized.tall==18,"resize reused frozen HFont");
    schemes.screenTall=480;
    Check(get(9).handle==initial.handle && surface.added==2,"resize back did not reuse original");
    Check(get(9,"SCHINESE").tall==13,"Chinese minimum");
    Check(get(12,"japanese").tall==13,"Title minimum");
    Check(get(9,"thai").tall==18,"Thai minimum");
    Check(get(9,"english",false).tall==9,"nonproportional recipe scaled");
    schemes.screenTall=720;
    Check(get(9).tall==13,"fractional scaling did not truncate");
    Check(schemes.requested.back()==9,"normalized tall was pre-scaled");
    Check(get(120).tall==127,"maximum font height");
    surface.metricDelta=2;
    auto measured=get(20);
    Check(measured.tall==32 && measured.recipeTall==30,"requested height substituted for font metric");
    // Resource-refresh may replace glyph resources behind a retained HFont.
    surface.heights[measured.handle]=35;
    Check(get(20).tall==35,"measured height was cached across resource replacement");
    surface.expectedFlags=vgui::FONTFLAG_SYMBOL;
    auto marlett=cache.GetHostScaled(surface,schemes,"english","Marlett",11,0,vgui::FONTFLAG_SYMBOL,true);
    Check(marlett.handle && marlett.recipeTall==16 && marlett.tall==18,"Marlett SYMBOL recipe/metric");
    surface.expectedFlags=aa;
    const auto retainedBeforeFailure=get(9).handle;
    surface.fail=true;
    Check(!get(23).handle && get(23).tall==0,"configuration failure hidden");
    surface.fail=false; cache.InvalidateFailures();
    Check(get(9).handle==retainedBeforeFailure,"failure invalidation discarded a successful permanent HFont");
    Check(get(23).handle!=0,"resource refresh did not permit retry");
    // Counterexample for the withdrawn caller lifecycle: AddGlyphSet on an
    // already configured handle fails and leaves the original height intact.
    const auto old=surface.CreateFont(); surface.metricDelta=0;
    Check(surface.AddGlyphSetToFont(old,"DFYuanW9-GB",9,0,0,0,aa,0,0xffff),"old setup");
    Check(!surface.AddGlyphSetToFont(old,"DFYuanW9-GB",18,0,0,0,aa,0,0xffff)
          && surface.GetFontTall(old)==9,"old-code counterexample did not reproduce");
    // Current original CSO and the host policy intentionally differ.
    // Only the complete current x64 vgui2 -> HW chain establishes this path.
    Surface cso;csol::client::MenuFontCache originalCache;
    constexpr int normalized[]{9,12,8,11};
    constexpr int expected[]{14,19,12,17};
    std::array<vgui::HFont,4> handles{};
    for(int i=0;i<4;++i){
        cso.expectedFlags=i==3?vgui::FONTFLAG_SYMBOL:aa;
        const auto value=originalCache.GetCSOScaled(cso,i==3?"Marlett":"DFYuanW9-GB",normalized[i],0,cso.expectedFlags);
        Check(value.handle&&value.recipeTall==expected[i]&&value.tall==expected[i],"current CSO original recipe changed");handles[i]=value.handle;
    }
    const auto recipes=cso.added;
    for(int screenHeight:{480,720,768,1080,1440})for(const char* language:{"english","SCHINESE","thai","japanese"}){
        cso.height=screenHeight;cso.width=screenHeight*16/9;schemes.screenTall=screenHeight;
        const auto adaptive=cache.GetHostScaled(surface,schemes,language,"DFYuanW9-GB",11,0,aa);
        const int minimum=std::strcmp(language,"thai")==0?18:std::strcmp(language,"english")==0?0:13;
        Check(adaptive.recipeTall==(std::max)((std::min)(int(11*(screenHeight/480.0f)),127),minimum),"preserved host policy no longer follows screen/language");
        for(int i=0;i<4;++i){
            const auto flags=i==3?vgui::FONTFLAG_SYMBOL:aa;
            const auto value=originalCache.GetCSOScaled(cso,i==3?"Marlett":"DFYuanW9-GB",normalized[i],0,flags);
            Check(value.handle==handles[i]&&value.recipeTall==expected[i],"current CSO font recipe followed live screen/language");
        }
    }
    Check(cso.added==recipes&&cso.screenReads==0,"current CSO font unnecessarily rebuilt/read live viewport");
    cso.expectedFlags=aa;
    Check(originalCache.GetCSOScaled(cso,"DFYuanW9-GB",11,0,aa,false).recipeTall==11,"nonproportional current CSO font scaled");
    Check(originalCache.GetCSOScaled(cso,"DFYuanW9-GB",80,0,aa).recipeTall==128,"current CSO exact maximum128 changed");
    Check(originalCache.GetCSOScaled(cso,"DFYuanW9-GB",100,0,aa).recipeTall==128,"current CSO maximum not clamped");
    cso.metricDelta=2;
    const auto metric=originalCache.GetCSOScaled(cso,"DFYuanW9-GB",10,0,aa);
    Check(metric.recipeTall==16&&metric.tall==18,"current CSO GDI metric replaced by submitted recipe");
    cso.heights[metric.handle]=21;
    Check(originalCache.GetCSOScaled(cso,"DFYuanW9-GB",10,0,aa).tall==21,"current CSO metric not remeasured after resource replacement");
    cso.expectedFlags=vgui::FONTFLAG_SYMBOL;
    const std::string family="Marlett";
    const auto beforeAdded=cso.added,beforeScreenReads=cso.screenReads,beforeMeasured=cso.measured;
    allocation_probe::calls=0;allocation_probe::active=true;
    for(int i=0;i<1000;++i)originalCache.GetCSOScaled(cso,family,11,0,vgui::FONTFLAG_SYMBOL);
    allocation_probe::active=false;
    Check(allocation_probe::calls==0&&cso.added==beforeAdded&&cso.screenReads==beforeScreenReads&&cso.measured==beforeMeasured+1000,"warm current CSO font path allocates/rebuilds/reads viewport or skips real metrics");
    std::cout<<"current CSO SDK recipe checks passed: 9/12/8/11 -> 14/19/12/17; warm1000 allocations="<<allocation_probe::calls<<" glyph registrations="<<cso.added-beforeAdded<<" screen reads="<<cso.screenReads-beforeScreenReads<<'\n';

    // Provider Commit/Abort rebuilds retained HFonts. Reopening the menus must
    // look up those same handles under the committed (or restored) family.
    const std::string originalFamily="DFYuanW9-GB",replacementFamily="Fixture Replacement Family";
    const auto beforeRefreshCreated=cso.next;
    const auto beforeRefreshAdded=cso.added;
    for(int round=0;round<32;++round){
        const auto reopen=[&](const std::string& publishedFamily,int metricOffset){
            for(int i=0;i<3;++i){
                originalCache.RebindFamily(handles[i],publishedFamily);
                cso.heights[handles[i]]=expected[i]+metricOffset;
            }
            for(int i=0;i<4;++i){
                const auto flags=i==3?vgui::FONTFLAG_SYMBOL:aa;
                cso.expectedFlags=flags;
                const auto value=originalCache.GetCSOScaled(cso,i==3?family:publishedFamily,normalized[i],0,flags);
                Check(value.handle==handles[i]&&value.recipeTall==expected[i],"refresh reopen allocated a permanent HFont or changed its recipe");
                Check(value.tall==expected[i]+(i==3?0:metricOffset),"refresh reopen did not measure the retained replacement font");
            }
        };
        reopen(originalFamily,round%3);       // unchanged family, new bytes/metrics
        reopen(replacementFamily,2);          // committed family update
        reopen(originalFamily,0);            // cancelled update restores old family
        Check(cso.next==beforeRefreshCreated&&cso.added==beforeRefreshAdded,"refresh/rollback loop increased CreateFont or AddGlyphSetToFont calls");
    }
    std::cout<<"refresh rounds=32; CreateFont delta="<<cso.next-beforeRefreshCreated<<"; AddGlyphSet delta="<<cso.added-beforeRefreshAdded<<'\n';
    cso.fail=true;
    const auto failed=originalCache.GetCSOScaled(cso,family,7,0,vgui::FONTFLAG_SYMBOL);
    Check(!failed.handle&&failed.tall==0&&failed.recipeTall==11,"current CSO glyph configuration failure hidden");
    cso.fail=false;originalCache.InvalidateFailures();
    const auto recovered=originalCache.GetCSOScaled(cso,family,7,0,vgui::FONTFLAG_SYMBOL);
    Check(recovered.handle&&recovered.recipeTall==11&&recovered.tall==13,"repaired resource did not retry the exact failed CSO recipe");
    Check(originalCache.GetCSOScaled(cso,family,11,0,vgui::FONTFLAG_SYMBOL).handle==handles[3],"failed recipe cleanup discarded successful CSO font cache entries");
    originalCache.Invalidate();
    const auto refreshed=originalCache.GetCSOScaled(cso,family,11,0,vgui::FONTFLAG_SYMBOL);
    Check(refreshed.handle!=handles[3]&&refreshed.recipeTall==17&&refreshed.tall==19,"shutdown invalidation reused a retired recipe handle or assumed metrics");
    std::cout << "host adapter SDK regression passed: immutable resize, language, metrics, refresh, old-code rejection\n";
    std::cout<<"checks="<<checks<<" failures=0\n";
}
