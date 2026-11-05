// SPDX-License-Identifier: GPL-3.0-or-later
// The production owner with the actual Renderer/engine types. Native GL and
// scheduling boundaries are observed here; this is not a gameplay/GPU test.
#include "gl_local.h"
#include <goldcraft/precache_api.h>
#include <algorithm>
#include <array>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {
std::vector<model_t*> known;
int connectedModelCount{};
std::vector<std::shared_ptr<CStudioModelRenderData>> g_StudioRenderDataCache;
std::unordered_map<int, CStudioSkinCache*> g_StudioSkinCache;
std::shared_ptr<CStudioModelRenderData> g_CurrentRenderData;
std::deque<std::array<cache_user_t, MAX_SKINS>> model_texture_cache;
std::unordered_map<model_t*, std::shared_ptr<CSpriteModelRenderData>> sprites;
bool g_StudioOutlineMask{};
std::vector<int> removedTextures, removedVaos, removedBuffers;
std::vector<cache_user_t*> freed;
int boneResets{}, waits{}, workDeletes{}, checks{};
HGLRC fakeContext = reinterpret_cast<HGLRC>(1);
HGLRC WINAPI Context() { return fakeContext; }
void Check(bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
}

bool g_bIsRenderingView{};
studiohdr_t* currentHeader{}; studiohdr_t** pstudiohdr = &currentHeader;
mstudiomodel_t* currentSubmodel{}; mstudiomodel_t** psubmodel = &currentSubmodel;
metahook_api_t* g_pMetaHookAPI{};
int EngineGetNumKnownModel() { return connectedModelCount; }
int EngineGetModelIndex(model_t* model) {
    const auto it = std::find(known.begin(), known.end(), model);
    return it == known.end() ? -1 : int(it - known.begin());
}
model_t* EngineGetModelByIndex(int i) { return i >= 0 && i < int(known.size()) ? known[i] : nullptr; }
void GL_DeleteVAO(GLuint value) { removedVaos.push_back(int(value)); }
void GL_DeleteBuffer(GLuint value) { removedBuffers.push_back(int(value)); }
void GL_UnloadTextureByTextureId(int value) { removedTextures.push_back(value); }
void Cache_Free(cache_user_t* value) { Check(value->data, "only allocated renderer cache freed"); freed.push_back(value); value->data = nullptr; }
void R_StudioClearAllBoneCaches() { ++boneResets; }
void R_StudioFlushAllSkins() { for (auto& [key, value] : g_StudioSkinCache) delete value; g_StudioSkinCache.clear(); }
std::shared_ptr<CSpriteModelRenderData> R_GetSpriteRenderDataFromModel(model_t* model) { return sprites[model]; }
void R_FreeSpriteRenderData(model_t* model) { sprites.erase(model); }
CStudioModelRenderData::~CStudioModelRenderData() {
    Check(!hVAO && !hEBO, "queued owner destructor does not retain GPU handles");
    for (auto buffer : hVBO) Check(!buffer, "queued owner VBO retired");
}
InterfaceReg::InterfaceReg(InstantiateInterfaceFn fn, const char* name) : m_CreateFn(fn), m_pName(name), m_pNext(nullptr) {}

const GoldCraftModelOwnersAPI* TestModelOwners() {
    static const GoldCraftModelOwnersAPI api{sizeof(GoldCraftModelOwnersAPI), 1,
        [] { return int(known.size()); },
        [](const model_s* value) { return EngineGetModelIndex(const_cast<model_s*>(value)); },
        EngineGetModelByIndex};
    return &api;
}

#define wglGetCurrentContext Context
#define GoldCraftModelOwnersInterface TestModelOwners
#include "resource_refresh.inc"
#undef GoldCraftModelOwnersInterface
#undef wglGetCurrentContext

class Task : public CGameResourceAsyncLoadTask {
public:
    explicit Task(int& uploads) : uploads_(uploads) { m_hThreadWorkItem = reinterpret_cast<ThreadWorkItemHandle_t>(1); }
    void UploadResource() override { ++uploads_; }
private: int& uploads_;
};

int main() {
    static_assert(sizeof(void*) == 4 && sizeof(model_t) == 392);
    try {
        metahook_api_t api{}; g_pMetaHookAPI = &api;
        api.WaitForWorkItemToComplete = [](ThreadWorkItemHandle_t) { ++waits; };
        api.DeleteWorkItem = [](ThreadWorkItemHandle_t) { ++workDeletes; };
        Check(!ResourceAvailable(), "not available before initialization");
        model_t body{}, texture{}, sprite{}, brush{};
        body.type = texture.type = mod_studio; sprite.type = mod_sprite; brush.type = mod_brush;
        known = {&body, &texture, &sprite, &brush};
        connectedModelCount = int(known.size());
        std::array<model_t*, 3> models{&body, &texture, &sprite};
        struct Header { studiohdr_t header{}; mstudiotexture_t textures[2]{}; } studio;
        studio.header.length = sizeof(studio); studio.header.numtextures = 2; studio.header.textureindex = offsetof(Header, textures);
        studio.textures[0].index = 11; studio.textures[1].index = 12; texture.cache.data = &studio;
        msprite_t spriteData{}; mspriteframe_t single{}, second{};
        single.gl_texturenum = 21; second.gl_texturenum = 22;
        struct Group { int numframes; float* intervals; mspriteframe_t* frames[2]; } group{2, nullptr, {&single, &second}};
        static_assert(offsetof(Group, frames) == offsetof(mspritegroup_t, frames));
        spriteData.numframes = 1; spriteData.frames[0] = {SPR_GROUP, reinterpret_cast<mspriteframe_t*>(&group)}; sprite.cache.data = &spriteData;
        auto data = std::make_shared<CStudioModelRenderData>(&body); data->TextureModel = &texture;
        data->hVAO = 31; data->hEBO = 32; data->hVBO[0] = 33;
        auto material = std::make_shared<CStudioModelRenderMaterial>(); material->textures[0].gltexturenum = 41;
        data->mStudioMaterials[0] = material; g_StudioRenderDataCache = {data}; g_CurrentRenderData = data;
        int uploads{}; data->m_pGameResourceAsyncLoadTask = std::make_shared<Task>(uploads);
        auto displaced = std::make_shared<CStudioModelRenderData>(&body);
        displaced->hVAO = 34; displaced->m_pGameResourceAsyncLoadTask = std::make_shared<Task>(uploads);
        g_CurrentRenderData = displaced;
        auto spriteOwner = std::make_shared<CSpriteModelRenderData>(&sprite);
        auto spriteMaterial = std::make_shared<CSpriteModelRenderMaterial>(); spriteMaterial->textures[0].gltexturenum = 42;
        spriteOwner->vSpriteMaterials.push_back(spriteMaterial); sprites[&sprite] = spriteOwner;
        auto* skins = new CStudioSkinCache{}; skins->skins[0].gl_index = 43; g_StudioSkinCache[7] = skins;
        model_texture_cache.resize(4); model_texture_cache[0][0].data = reinterpret_cast<void*>(1);
        currentHeader = &studio.header; currentSubmodel = reinterpret_cast<mstudiomodel_t*>(1);
        R_ResourceRefreshInit(); Check(ResourceAvailable(), "registered main GL thread");
        Check(ResourceValidate(models.data(), models.size()), "outside frame rejected");
        R_ResourceRefreshFrame(true);
        // A disconnected extended model remains owned even when the active
        // connection exposes only the original model prefix.
        connectedModelCount = 2;
        Check(ResourceValidate(models.data(), 2), "disconnected extra sprite cannot be omitted");
        Check(!ResourceValidate(models.data(), models.size()), "full disconnected owner set remains admissible");
        g_bIsRenderingView = true; Check(ResourceRelease(models.data(), models.size()), "drawing rejected"); g_bIsRenderingView = false;
        Check(ResourceValidate(models.data(), 2), "incomplete dependency collection rejected");
        auto duplicates = models; duplicates[1] = &body;
        Check(ResourceValidate(duplicates.data(), duplicates.size()), "duplicate owner rejected");
        auto foreign = models; foreign[0] = &brush;
        Check(ResourceValidate(foreign.data(), foreign.size()), "brush allocation rejected");
        group.frames[1] = nullptr; Check(ResourceRelease(models.data(), models.size()), "bad sprite group rejected before mutation"); group.frames[1] = &second;
        studio.header.textureindex = -1; Check(ResourceRelease(models.data(), models.size()), "bad header rejected before mutation");
        studio.header.textureindex = offsetof(Header, textures);
        fakeContext = reinterpret_cast<HGLRC>(2); Check(ResourceRelease(models.data(), models.size()), "wrong GL context rejected"); fakeContext = reinterpret_cast<HGLRC>(1);
        bool workerRejected{}; std::thread([&] { workerRejected = ResourceValidate(models.data(), models.size()) != nullptr; }).join();
        Check(workerRejected, "worker thread rejected");
        Check(removedTextures.empty() && !waits && data->hVAO == 31, "preflight failures have no owner side effects");
        Check(!ResourceRelease(models.data(), models.size()), "complete set retired");
        Check(waits == 2 && workDeletes == 2 && !data->m_pGameResourceAsyncLoadTask && !displaced->m_pGameResourceAsyncLoadTask, "cached and displaced workers joined before task removal");
        data->AsyncUploadResouce(); Check(!uploads, "queued upload cannot recreate retired buffers");
        Check(data->bIsClosing && !data->hVAO && !data->hEBO && !data->hVBO[0], "held old owner has zero GPU handles");
        Check(removedTextures == std::vector<int>({11,12,21,22,41,42,43}), "native grouped sprite and external texture entries retired once");
        Check(removedVaos == std::vector<int>({31,34}) && removedBuffers == std::vector<int>({32,33}), "GPU deletion includes displaced current owner");
        Check(boneResets == 1 && freed.size() == 1 && !currentHeader && !currentSubmodel, "dependent caches and header borrows cleared");
        Check(g_StudioRenderDataCache.empty() && !g_CurrentRenderData && sprites.empty() && g_StudioSkinCache.empty(), "old presentation owners unlinked");
        Check(texture.cache.data == &studio && sprite.cache.data == &spriteData, "native cache allocation remains owned by engine");
        R_ResourceRefreshShutdown(); Check(!ResourceAvailable(), "shutdown revokes capability");
        data.reset(); displaced.reset();
        std::cout << "Renderer resource owner: " << checks << " checks, 0 failed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
