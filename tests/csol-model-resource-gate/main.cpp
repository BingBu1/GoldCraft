// SPDX-License-Identifier: GPL-3.0-or-later
// Actual loader, collector and ReHLDS SDK; reuse explicit filesystem doubles.
#define main DependencyCacheRegressionMain
#include "../csol-dependency-cache/loader.cpp"
#undef main

int main(int argc,char** argv)
{
    try {
        if(argc!=2)return 2;
        using namespace fixture;
        // The SDK maps Q_strnicmp to the x86 CRT _strnicmp on Windows.
        CHECK(Q_strnicmp("CSO_","cso_",4)==0);
        CHECK(Q_strnicmp("cSo_","cso_",4)==0);
        for(const char* texture:{"cso_fixture.bmp","CSO_fixture.bmp","cSo_fixture.bmp"}) {
            Base();Set("models/cache.mdl",Model("weapons/cache_a.wav",texture));
            Pass("models/cache.mdl",false);CHECK(Contains("sound/weapons/cache_a.wav"));
            Pass("models/cache.mdl",true);CHECK(Contains("sound/weapons/cache_a.wav"));
        }
        for(const char* texture:{"stock.bmp","xcso_fixture.bmp","csox_fixture.bmp"}) {
            Base();Set("models/cache.mdl",Model("weapons/cache_a.wav",texture));
            GoldCraftBeginCsolResources();Precache("models/cache.mdl");
            CHECK(advertised.empty());CHECK(resourceBatch->models==0);CHECK(dependencyCache.Size()==0);
            GoldCraftEndCsolResources();CHECK(printed.empty());
        }
        Reset();common=std::filesystem::path(argv[1]);
        CHECK(!Has("models/v_skull5.json"));CHECK(!Has("models/v_skull5_hands.json"));
        CHECK(!Has("models/v_skull5_csol.json"));
        for(bool hit:{false,true}) {
            Pass("models/v_skull5.mdl",hit);
            for(const char* file:{"sound/weapons/skull5_draw.wav","sound/weapons/skull5_boltpull.wav",
                "sound/weapons/skull5_clipin.wav","sound/weapons/skull5_clipout.wav",
                "gfx/tattoo/MaleShort.tga","gfx/tattoo/FemaleLong.tga"})CHECK(Contains(file));
        }
        for(const auto& name:advertised)std::printf("advertised %s\n",name.c_str());
        for(const auto& [path,value]:realInputs){
            ReadDisk(path);std::printf("resource %s %zu ",path.c_str(),value.first);
            for(auto byte:value.second)std::printf("%02x",unsigned(byte));std::printf("\n");
        }
        std::printf("resource-gate checks=%u failures=%u\n",checks,failures);return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"%s; checks=%u failures=%u\n",e.what(),fixture::checks,fixture::failures);return 1;}
}
