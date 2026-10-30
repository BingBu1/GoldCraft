// SPDX-License-Identifier: GPL-3.0-or-later
// Real x86 SDK layouts. The tested functions are extracted from production.
#include <metahook.h>
#include <com_model.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>

int g_iEngineType{};
#include "production_surface.inc"
#include "legacy_surface.inc"
#include "bsp_models.inc"

namespace {
unsigned checks{},failures{};
void Check(bool value,const std::source_location where=std::source_location::current())
{
    ++checks;
    if(!value){++failures;if(failures<12)std::cerr<<"FAIL line "<<where.line()<<'\n';}
}

template<class Surface>
void TestLayout(int engine,const char* label)
{
    g_iEngineType=engine;
    auto surfaces=std::make_unique<Surface[]>(BspFaceCount);
    glpoly_t poly{};poly.numverts=4;
    texture_t texture{};mtexinfo_t texinfo{};texinfo.texture=&texture;
    for(int index=0;index<BspFaceCount;++index)
    {surfaces[index].polys=&poly;surfaces[index].texinfo=&texinfo;}
    model_t model{};model.surfaces=&surfaces[0];model.numsurfaces=BspFaceCount;
    model.firstmodelsurface=BspModels[0][0];model.nummodelsurfaces=BspModels[0][1];
    decal_t decal{};
    int accepted{},oldAccepted{};
    for(int index=0;index<BspWorldFaces;++index)
    {
        decal.psurface=&surfaces[index];
        const bool result=R_IsDecalSurfaceReady(decal,model);
        Check(result);accepted+=result;
        const bool oldResult=R_IsDecalSurfaceReady_Legacy(decal,model);
        oldAccepted+=oldResult;
        if(engine==ENGINE_GOLDSRC_HL25)
        {
            // This is a defect oracle, independent of the production branch:
            // legacy divides a108-byte index by92 and requires divisibility.
            const auto offset=static_cast<std::size_t>(index)*sizeof(Surface);
            Check(oldResult==(offset%92==0 && offset/92<std::size_t(BspWorldFaces)));
        }
        else Check(oldResult);
        // Every byte inside each element except its first is an invalid owner.
        for(std::size_t byte=1;byte<sizeof(Surface);++byte)
        {
            decal.psurface=reinterpret_cast<msurface_t*>(reinterpret_cast<std::uintptr_t>(&surfaces[index])+byte);
            Check(!R_IsDecalSurfaceReady(decal,model));
        }
    }
    Check(accepted==9219);
    if(engine==ENGINE_GOLDSRC_HL25)
    {
        decal.psurface=&surfaces[3174];Check(R_IsDecalSurfaceReady_Legacy(decal,model));
        decal.psurface=&surfaces[3175];Check(!R_IsDecalSurfaceReady_Legacy(decal,model));
    }
    else Check(oldAccepted==9219);
    for(const auto& range:BspModels)
    {
        model.firstmodelsurface=range[0];model.nummodelsurfaces=range[1];
        for(int index=range[0];index<range[0]+range[1];++index)
        {decal.psurface=&surfaces[index];Check(R_IsDecalSurfaceReady(decal,model));}
        if(range[0]>0){decal.psurface=&surfaces[range[0]-1];Check(!R_IsDecalSurfaceReady(decal,model));}
        decal.psurface=reinterpret_cast<msurface_t*>(reinterpret_cast<std::uintptr_t>(&surfaces[0])+std::size_t(range[0]+range[1])*sizeof(Surface));
        Check(!R_IsDecalSurfaceReady(decal,model));
    }
    model.firstmodelsurface=0;model.nummodelsurfaces=BspWorldFaces;
    decal.psurface=nullptr;Check(!R_IsDecalSurfaceReady(decal,model));
    decal.psurface=reinterpret_cast<msurface_t*>(reinterpret_cast<std::uintptr_t>(model.surfaces)-sizeof(Surface));Check(!R_IsDecalSurfaceReady(decal,model));
    decal.psurface=reinterpret_cast<msurface_t*>(std::numeric_limits<std::uintptr_t>::max());Check(!R_IsDecalSurfaceReady(decal,model));
    decal.psurface=reinterpret_cast<msurface_t*>(std::uintptr_t(1));Check(!R_IsDecalSurfaceReady(decal,model));
    decal.psurface=&surfaces[0];
    model.surfaces=nullptr;Check(!R_IsDecalSurfaceReady(decal,model));model.surfaces=&surfaces[0];
    model.numsurfaces=0;Check(!R_IsDecalSurfaceReady(decal,model));
    model.numsurfaces=-1;Check(!R_IsDecalSurfaceReady(decal,model));model.numsurfaces=BspFaceCount;
    model.firstmodelsurface=-1;Check(!R_IsDecalSurfaceReady(decal,model));
    model.firstmodelsurface=BspFaceCount+1;Check(!R_IsDecalSurfaceReady(decal,model));
    model.firstmodelsurface=std::numeric_limits<int>::max();Check(!R_IsDecalSurfaceReady(decal,model));
    model.firstmodelsurface=0;model.nummodelsurfaces=0;Check(!R_IsDecalSurfaceReady(decal,model));
    model.nummodelsurfaces=-1;Check(!R_IsDecalSurfaceReady(decal,model));
    model.nummodelsurfaces=BspFaceCount+1;Check(!R_IsDecalSurfaceReady(decal,model));
    model.nummodelsurfaces=std::numeric_limits<int>::max();Check(!R_IsDecalSurfaceReady(decal,model));
    model.firstmodelsurface=BspFaceCount;model.nummodelsurfaces=1;Check(!R_IsDecalSurfaceReady(decal,model));
    model.firstmodelsurface=BspFaceCount-1;model.nummodelsurfaces=2;Check(!R_IsDecalSurfaceReady(decal,model));
    model.firstmodelsurface=0;model.nummodelsurfaces=BspWorldFaces;
    surfaces[0].polys=nullptr;Check(!R_IsDecalSurfaceReady(decal,model));surfaces[0].polys=&poly;
    poly.numverts=2;Check(!R_IsDecalSurfaceReady(decal,model));poly.numverts=3;Check(R_IsDecalSurfaceReady(decal,model));
    surfaces[0].texinfo=nullptr;Check(!R_IsDecalSurfaceReady(decal,model));surfaces[0].texinfo=&texinfo;
    texinfo.texture=nullptr;Check(!R_IsDecalSurfaceReady(decal,model));texinfo.texture=&texture;
    Check(R_IsDecalSurfaceReady(decal,model));
    std::cout<<label<<" stride="<<sizeof(Surface)<<" world="<<accepted<<"/"<<BspWorldFaces
        <<" legacy="<<oldAccepted<<"/"<<BspWorldFaces<<" models="<<std::size(BspModels)<<'\n';
}
}
int main()
{
    static_assert(sizeof(void*)==4);
    static_assert(sizeof(msurface_t)==92);
    static_assert(sizeof(msurface_hl25_t)==108);
    TestLayout<msurface_t>(ENGINE_GOLDSRC,"GoldSrc");
    TestLayout<msurface_hl25_t>(ENGINE_GOLDSRC_HL25,"HL25");
    // Sven/CoF use the ordinary layout branch as the production accessor does.
    TestLayout<msurface_t>(ENGINE_SVENGINE,"SvEngine");
    TestLayout<msurface_t>(ENGINE_GOLDSRC_COF,"CoF");
    std::cout<<"Renderer decal surface "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
