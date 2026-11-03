// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual collector, loader and cache through the ReHLDS SDK.
#define main DependencyCacheRegressionMain
#include "../csol-dependency-cache/loader.cpp"
#undef main

int main()
{
    try {
        using namespace fixture;
        constexpr auto model="models/cache.mdl";
        constexpr auto canonical="models/cache_mat.json";
        constexpr auto legacy="models/cache.mat.json";
        constexpr auto local=R"({"Materials":[{"OriginalTexture":"models/cache.mdl/stock.bmp","BaseTexture":"materials/local.tga","BlendMode":"Additive"}]})";
        constexpr auto old=R"({"Materials":[{"OriginalTexture":"models/cache.mdl/stock.bmp","BaseTexture":"materials/legacy.tga","BlendMode":"Translucent"}]})";
        Base();Set(model,Model("weapons/cache_a.wav","stock.bmp"));
        CHECK(csol::ResolveModelMaterialSource(model,Has)==canonical);
        Set(legacy,old);Set("materials/legacy.tga","old-pixels");
        CHECK(csol::ResolveModelMaterialSource(model,Has)==legacy);
        Pass(model,false);CHECK(Contains(legacy));CHECK(Contains("materials/legacy.tga"));
        Pass(model,true);
        Set(canonical,local);Set("materials/local.tga","new-pixels");
        CHECK(csol::ResolveModelMaterialSource(model,Has)==canonical);
        Pass(model,false);CHECK(Contains(canonical));CHECK(Contains("materials/local.tga"));
        CHECK(!Contains(legacy));CHECK(!Contains("materials/legacy.tga"));
        CHECK(!Contains("materials/common.json"));Pass(model,true);
        Set(legacy,"invalid ignored legacy");Pass(model,true);
        Set(canonical,"{\"Materials\":[]}");Pass(model,false);
        CHECK(Contains(canonical));CHECK(!Contains("materials/local.tga"));
        Set(legacy,old);vfs.erase(canonical);Pass(model,false);
        CHECK(Contains(legacy));CHECK(Contains("materials/legacy.tga"));
        Pass(model,true);vfs.erase(legacy);
        CHECK(csol::ResolveModelMaterialSource(model,Has)==canonical);
        GoldCraftBeginCsolResources();ResetCounters();Precache(model);
        CHECK(advertised.empty());GoldCraftEndCsolResources();
        // Adding a canonical table must make an ordinary model eligible too.
        Set(canonical,local);Pass(model,false);CHECK(Contains(canonical));
        Pass(model,true);
        std::printf("material-download checks=%u failures=%u\n",checks,failures);return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"%s; checks=%u failures=%u\n",e.what(),fixture::checks,fixture::failures);return 1;
    }
}
