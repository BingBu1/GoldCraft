// Load the complete staged DLL without a game, GL context or injected hooks.
// This covers the real exported factory and ABI; render/engine lifetime tests
// remain separate because MetaHook has not initialized this isolated process.
#include <windows.h>
#include <IMetaRendererWorldEdit.h>
#include <IMetaRendererPostScene.h>
#include <IMetaRendererSceneCulling.h>
#include <IMetaRendererSceneLights.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (argc != 4) return 2;
    struct Modules {
        std::array<HMODULE, 3> handles{};
        ~Modules() {
            for (auto it = handles.rbegin(); it != handles.rend(); ++it)
                if (*it) FreeLibrary(*it);
        }
    } modules;
    for (int i = 0; i < 3; ++i) {
        modules.handles[i] = LoadLibraryExA(argv[i + 1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!modules.handles[i]) {
            std::fprintf(stderr, "Cannot load %s: Win32 %lu\n", argv[i + 1], GetLastError());
            return 1;
        }
    }
    using Factory = void*(__cdecl*)(const char*, int*);
    const auto factory = reinterpret_cast<Factory>(GetProcAddress(modules.handles.back(), "CreateInterface"));
    assert(factory);
    int result = -1;
    assert(factory("MetaRenderer_API_002", &result) && result == 0);
    assert(factory("MetaRendererScene_API_001", &result) && result == 0);
    auto* culling=static_cast<IMetaRendererSceneCulling*>(factory(METARENDERER_SCENE_CULLING_INTERFACE_VERSION,&result));
    assert(culling&&result==0);
    const float origin[3]{};
    assert(!culling->CullShadowBounds(origin,origin)&&!culling->CullShadowBounds(nullptr,nullptr));
    auto* scene=static_cast<IMetaRendererScene*>(factory(METARENDERER_SCENE_INTERFACE_VERSION,&result));
    auto* lights=static_cast<IMetaRendererSceneLights*>(factory(METARENDERER_SCENE_LIGHTS_INTERFACE_VERSION,&result));
    assert(lights&&result==0);
    struct SceneCallback final:IMetaRendererSceneCallbacks{void DrawOpaqueScene(uint32_t)override{assert(false);}} sceneCallback;
    std::array<MetaRendererSceneLight,65> emitters{};
    for(unsigned i=0;i<emitters.size();++i)emitters[i]={{float(i)*128,0,64},480,0xff9cd6ff};
    assert(!lights->UpdateLights(&sceneCallback,emitters.data(),65));
    scene->RegisterSceneCallbacks(&sceneCallback);
    assert(lights->UpdateLights(&sceneCallback,emitters.data(),65));
    assert(lights->UpdateLights(&sceneCallback,emitters.data(),65));
    assert(!lights->UpdateLights(&sceneCallback,nullptr,65));
    assert(!lights->UpdateLights(&sceneCallback,emitters.data(),META_SCENE_LIGHTS_MAX_COUNT+1));
    lights->ClearLights(&sceneCallback);
    assert(lights->UpdateLights(&sceneCallback,nullptr,0));
    scene->UnregisterSceneCallbacks(&sceneCallback);
    assert(!lights->UpdateLights(&sceneCallback,emitters.data(),65));
    auto* post=static_cast<IMetaRendererPostScene*>(factory(METARENDERER_POST_SCENE_INTERFACE_VERSION,&result));
    assert(post&&result==0);
    struct Callback final : IMetaRendererPostSceneCallbacks {
        void DrawPostScene(const MetaRendererPostSceneFrame&) noexcept override {assert(false);}
    } callback;
    assert(!post->RegisterPostSceneCallbacks(nullptr)&&post->RegisterPostSceneCallbacks(&callback));
    assert(post->RegisterPostSceneCallbacks(&callback)&&post->UnregisterPostSceneCallbacks(&callback));
    assert(!post->UnregisterPostSceneCallbacks(&callback));
    auto* base = static_cast<IMetaRendererWorldEdit*>(factory(METARENDERER_WORLD_EDIT_INTERFACE_VERSION, &result));
    assert(base && result == 0);
    auto* targeted = static_cast<IMetaRendererWorldEdit2*>(factory(METARENDERER_WORLD_EDIT2_INTERFACE_VERSION, &result));
    assert(targeted && result == 0 && static_cast<IMetaRendererWorldEdit*>(targeted) == base);
    assert(!factory("GoldCraftMissingInterface", &result) && result != 0);
    assert(base->Poll(0) == MetaWorldEditStatus::Missing && !base->Commit(0));

    // Cross the DLL boundary with the exact fixed-width record layout and full
    // negotiated capacity. No world exists, so this cannot invoke engine/GL.
    std::array<MetaWorldEditTarget, 1024> identities{};
    for (std::uint32_t i = 0; i < identities.size(); ++i)
        identities[i] = {i + 1, 7, 11};
    assert(targeted->UpdateBrushSnapshot(9, 2, 5, identities.data(), 1024));
    assert(targeted->UpdateBrushSnapshot(9, 2, 5, identities.data(), 1024));
    ++identities.back().serial;
    assert(!targeted->UpdateBrushSnapshot(9, 2, 5, identities.data(), 1024));
    assert(targeted->UpdateBrushSnapshot(9, 2, 6, identities.data(), 1024));
    assert(!targeted->UpdateBrushSnapshot(9, 2, 7, identities.data(), 1025));
    assert(targeted->UpdateBrushSnapshot(9, 2, 7, nullptr, 0));
    assert(!targeted->UpdateBrushSnapshot(0, 0, 0, nullptr, 0));
    base->Reset();
    std::puts("Actual Renderer DLL factory, shared API001/API002 identity, 1024 records, rejection/recovery and revoke passed");
}
