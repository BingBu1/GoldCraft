// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include "../model_json.hpp"
#include "../body_effects.hpp"
#include "../image_codec.hpp"
#include "../material_state.hpp"
#include "../model_source.hpp"
#include "../hand_adapter.hpp"
#include <cstring>

namespace csol::client
{
template<class T> std::span<const T> Range(const std::vector<std::byte>& data, int offset, int count=1)
{
    if(offset<0 || count<0 || size_t(offset)>data.size() || size_t(count)>(data.size()-offset)/sizeof(T))
        throw std::runtime_error("model data outside file");
    return {reinterpret_cast<const T*>(data.data()+offset),size_t(count)};
}
template<size_t N> std::string Name(const char (&text)[N]) { return {text,strnlen(text,N)}; }
struct Material : MaterialPolicy
{
    GLuint texture{}, environment{};
    int width{},height{},flags{},hand=-1,originalHeight{},sprite=-1,fade=-1;
    float environmentAdd{};
};
struct Vertex { float position[3],normal[3],uv[2]; int bone,normalBone; };
struct Batch { unsigned offset{},count{}; int group{},submodel{},skinref{}; };
struct Asset
{
    std::string name;
    ModelSource source;
    std::vector<std::byte> bytes;
    std::vector<Material> materials;
    std::vector<model_t*> sprites;
    std::vector<Batch> batches;
    std::vector<std::array<int,2>> handModels;
    std::vector<HandBodygroup> handBodygroups;
    ModelJSON config;
    BodyEffectProfile profile{};
    bool special{},revive{};
    bool handAdaptationChecked{};
    int adaptedMeshes{},adaptedBones{};
    std::unique_ptr<Asset> adaptedHands;
    GLuint vao{},vbo{},ibo{};
    ~Asset();
    const studiohdr_t& Header() const { return Range<studiohdr_t>(bytes,0)[0]; }
    template<class T> auto Data(int offset,int count=1) const { return Range<T>(bytes,offset,count); }
};
// GoldSrc hardware sprite layout verified separately from the dedicated server.
struct EngineSpriteFrame { int width,height; float up,down,left,right; int texture; };
struct EngineSpriteDesc { int type; EngineSpriteFrame* frame; };
struct EngineSprite { short type,format; int width,height,frames,palette; float beam; void* cache; EngineSpriteDesc frame[1]; };
static_assert(sizeof(EngineSprite)==36 && sizeof(EngineSpriteFrame)==28);
EngineSpriteFrame* SpriteFrameAt(model_t* model,int frame,double time=0);
Asset* GetAsset(model_t* model);
Asset* ViewHandAsset(Asset* asset);
std::pair<bool,Asset*> CachedAsset(model_t* model);
GLuint UploadImage(const Image& image);
void ClearAssets(bool deleteGraphics=true);
size_t AssetCount();
void EnsureGeometry(Asset& asset);
void InitializeHands();
void UpdateHands();
void ApplyHand(const Material& material,GLuint& texture,float& width,float& height,float* uv);
void ResetHands(bool deleteGraphics=true);
int CharacterHandSubmodel(const Asset& asset,int group,int original,int body=0);
void FindHandModels(Asset& asset);
}
