// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../resource_refresh.hpp"
#include "../../../scenario/core/human_kick.hpp"
#include "body_stains.hpp"
#include "body_stain_atlas.hpp"
#include "../../../scenario/custom/monster_stains.hpp"
#include <IMetaRendererStudioStain.h>
#include <algorithm>

namespace csol::client
{
namespace
{
namespace custom = scenario::custom;
IMetaRendererStudioStain* renderer{};
IMetaRendererStudioContactStain* contactRenderer{};
struct MonsterMarks
{
    unsigned id{}, count{};
    std::array<custom::MonsterStain, custom::MonsterStainMax> hits;
};
std::array<MonsterMarks, custom::BodyStainSlotCount> monsterStains{};
static_assert(custom::MonsterStainMax == goldcraft::studio_stain::MaxContacts);
bool contactQueryInstalled{};
std::array<GLuint, custom::BodyStainLayerCount> textures{};
const cl_entity_t* viewEntity{};
const model_t* viewDrawBaseModel{};
const model_t* snapshotViewModel{};
int snapshotViewIndex{};
custom::BodyStain snapshot{};
bool snapshotPolicy{}, snapshotDrawsArms{};
HGLRC textureContext{};
int textureMap{}, preparationMap{-1};
bool queryInstalled{}, texturesReady{};

bool Query(const cl_entity_t* entity, const model_t* model, int material, const char* name,
    MetaRendererStudioStain& output) noexcept
{
    const auto surface = BodyStainForModel(entity, model);
    if (!surface || material < 0) return false;
    const unsigned arm = surface.surface == custom::BodyStainSurface::ViewWeapon ?
        custom::BodyStainArmMaterial({name, strnlen(name, 64)}) : 0;
    return BodyStainMaterial(surface, static_cast<unsigned>(material), arm, output);
}

void SetActive(bool active) noexcept
{
    if (queryInstalled == active) return;
    renderer->SetQuery(active ? Query : nullptr);
    queryInstalled = active;
}

// Preparation borrows the renderer's decal textures. Preserve both of its
// pixel transfer buffers and row layout while reading and uploading.
constexpr std::array<GLenum, 6> PackFields{GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
    GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES};
constexpr std::array<GLenum, 6> UnpackFields{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
    GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES};

class PixelTransfer
{
public:
    PixelTransfer(GLenum target, GLenum binding, const std::array<GLenum, 6>& fields) noexcept :
        target_(target), fields_(fields)
    {
        glGetIntegerv(binding, &buffer_);
        glBindBuffer(target_, 0);
        for (std::size_t i = 0; i < fields_.size(); ++i)
        {
            glGetIntegerv(fields_[i], &values_[i]);
            glPixelStorei(fields_[i], i == 0 ? 1 : 0);
        }
    }

    ~PixelTransfer()
    {
        for (std::size_t i = 0; i < fields_.size(); ++i) glPixelStorei(fields_[i], values_[i]);
        glBindBuffer(target_, buffer_);
    }

private:
    GLenum target_;
    const std::array<GLenum, 6>& fields_;
    std::array<GLint, 6> values_{};
    GLint buffer_{};
};

bool PrepareTextures()
{
    // Studio already uses texture views. They share one immutable allocation
    // while preserving the existing 2D texture binding in both pipelines.
    if (!EnsureGraphics() || !glGetTextureImage || !glTextureView) return false;
    std::vector<stain::Decal> decals;
    decals.reserve(custom::BodyStainDecals.size());
    {
        PixelTransfer pack(GL_PIXEL_PACK_BUFFER, GL_PIXEL_PACK_BUFFER_BINDING, PackFields);
        std::vector<stain::Pixel> source;
        for (const char* name : custom::BodyStainDecals)
        {
            const GLuint texture = renderer->DecalTexture(name);
            if (!texture) continue;
            GLint width{}, height{};
            glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_WIDTH, &width);
            glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_HEIGHT, &height);
            if (width < 2 || height < 2 || width > 256 || height > 256) continue;
            source.assign(static_cast<std::size_t>(width) * height, {});
            glGetTextureImage(texture, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                static_cast<GLsizei>(source.size() * sizeof(stain::Pixel)), source.data());
            stain::Decal decal{width, height, {}};
            decal.alpha.reserve(source.size());
            for (const auto& pixel : source) decal.alpha.push_back(pixel.alpha);
            const auto [low, high] = std::minmax_element(decal.alpha.begin(), decal.alpha.end());
            if (*low < 16 && *high > 64) decals.push_back(std::move(decal));
        }
    }
    if (decals.empty()) return false;
    const auto pixels = stain::BuildAtlas(decals);

    PixelTransfer unpack(GL_PIXEL_UNPACK_BUFFER, GL_PIXEL_UNPACK_BUFFER_BINDING, UnpackFields);
    GLuint storage{};
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &storage);
    if (!storage) return false;
    constexpr GLsizei levels = std::bit_width(unsigned(stain::Side));
    glTextureStorage3D(storage, levels, GL_RGBA8, stain::Side, stain::Side, GLsizei(textures.size()));
    GLint depth{};
    glGetTextureLevelParameteriv(storage, 0, GL_TEXTURE_DEPTH, &depth);
    if (depth != GLsizei(textures.size()))
    {
        glDeleteTextures(1, &storage);
        return false;
    }
    glTextureSubImage3D(storage, 0, 0, 0, 0, stain::Side, stain::Side, depth,
        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateTextureMipmap(storage);
    glGenTextures(GLsizei(textures.size()), textures.data());
    for (GLuint layer = 0; layer < textures.size(); ++layer)
    {
        const auto view = textures[layer];
        glTextureView(view, GL_TEXTURE_2D, storage, GL_RGBA8, 0, levels, layer, 1);
        glTextureParameteri(view, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(view, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glDeleteTextures(1, &storage);
    return true;
}
}

void InitializeBodyStains() noexcept
{
    if (renderer || !api || !api->GetPluginModuleHandleByBaseFileName) return;
    auto module = api->GetPluginModuleHandleByBaseFileName("Renderer");
    if (!module) module = api->GetPluginModuleHandleByBaseFileName("Renderer_AVX2");
    const auto factory = module ? Sys_GetFactory(module) : nullptr;
    if (factory)
    {
        renderer = static_cast<IMetaRendererStudioStain*>(factory(METARENDERER_STUDIO_STAIN_INTERFACE_VERSION, nullptr));
        contactRenderer = static_cast<IMetaRendererStudioContactStain*>(
            factory(METARENDERER_STUDIO_CONTACT_STAIN_INTERFACE_VERSION, nullptr));
    }
}

void ResetBodyStains() noexcept
{
    SetActive(false);
    if (contactQueryInstalled) contactRenderer->SetContactQuery(nullptr);
    contactQueryInstalled = false;
    textureMap = 0;
    preparationMap = -1;
    viewEntity = nullptr;
    viewDrawBaseModel = snapshotViewModel = nullptr;
    snapshotViewIndex = 0;
    snapshot = {};
    snapshotPolicy = snapshotDrawsArms = false;
}

void ShutdownBodyStains() noexcept
{
    ResetBodyStains();
    ResetMonsterStains();
    if (texturesReady && textureContext == wglGetCurrentContext())
        glDeleteTextures(GLsizei(textures.size()), textures.data());
    textures = {};
    texturesReady = false;
    textureContext = {};
    renderer = nullptr;
    contactRenderer = nullptr;
}

void ReadLocalBodyStain(const entity_state_t* state, const clientdata_t* client) noexcept
{
    // Use the decoded model index and stain from the same server snapshot.
    const auto stain = state ? custom::DecodeBodyStainField(state->fuser4) : custom::BodyStain{};
    snapshotPolicy = stain.enabled;
    const int viewIndex = client && client->health > 0 && !client->iuser1 &&
        (stain.body || stain.weapon) ? client->viewmodel : 0;
    if (snapshotViewIndex != viewIndex) snapshotViewModel = nullptr;
    snapshotViewIndex = viewIndex;
    snapshot = viewIndex > 0 ? stain : custom::BodyStain{};
}

void BodyStainsFrame() noexcept
{
    if (!renderer) return;
    const bool visible = snapshotPolicy && ResourceReadsAllowed();
    if (contactRenderer && contactQueryInstalled != visible)
    {
        contactRenderer->SetContactQuery(visible ? ProjectMonsterStains : nullptr);
        contactQueryInstalled = visible;
    }
    if (!visible)
    {
        SetActive(false);
        return;
    }

    const auto context = wglGetCurrentContext();
    if (!context)
    {
        SetActive(false);
        return;
    }

    if (textureContext != context)
    {
        SetActive(false);
        // GL names from the retired context must never be deleted in its replacement.
        textureContext = context;
        preparationMap = -1;
        texturesReady = false;
        textures = {};
    }
    const int map = renderer->DecalGeneration();
    if (textureMap != map)
    {
        SetActive(false);
        if (contactQueryInstalled) contactRenderer->SetContactQuery(ProjectMonsterStains);
        textureMap = map;
    }
    if (!texturesReady && preparationMap != map)
    {
        preparationMap = map;
        try { texturesReady = PrepareTextures(); }
        catch (const std::exception&) { texturesReady = false; }
    }
    SetActive(texturesReady);

    viewEntity = engine.GetViewModel ? engine.GetViewModel() : nullptr;
    if (texturesReady && !snapshotViewModel && (snapshot.body || snapshot.weapon) && studio.GetModelByIndex)
    {
        snapshotViewModel = studio.GetModelByIndex(snapshotViewIndex);
        snapshotDrawsArms = snapshotViewModel && std::strcmp(snapshotViewModel->name, scenario::human_kick::FootModel) != 0;
    }
}

void ResetMonsterStains() noexcept
{
    for (auto& monster : monsterStains) monster.id = monster.count = 0;
}

bool ConsumeMonsterStains(int size, const void* payload) noexcept
{
    if (!payload || size <= 0) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    if (bytes[0] != custom::MonsterStainSubtype) return false;
    if (size <= int(custom::MonsterStainHeaderSize) || size > int(custom::MonsterStainPacketMax)) return true;
    const unsigned slot = unsigned(bytes[1]) | (unsigned(bytes[2]) << 8);
    const unsigned id = unsigned(bytes[3]) | (unsigned(bytes[4]) << 8) | (unsigned(bytes[5]) << 16);
    const unsigned begin = bytes[6], dataSize = size - custom::MonsterStainHeaderSize;
    const unsigned count = dataSize / custom::MonsterStainMarkSize;
    if (!slot || slot >= monsterStains.size() || !id || id > custom::MonsterStainIdMax ||
        begin + count > custom::MonsterStainMax || dataSize % custom::MonsterStainMarkSize) return true;

    auto& entry = monsterStains[slot];
    if (id < entry.id || (id != entry.id ? begin != 0 : begin > entry.count)) return true;
    if (id != entry.id) { entry.id = id; entry.count = 0; }
    for (unsigned i = 0; i < count; ++i)
        entry.hits[begin + i] = custom::ReadMonsterStain(std::span<const std::uint8_t, custom::MonsterStainMarkSize>(
            bytes + custom::MonsterStainHeaderSize + i * custom::MonsterStainMarkSize, custom::MonsterStainMarkSize));
    entry.count = std::max(entry.count, begin + count);
    return true;
}

unsigned ProjectMonsterStains(const cl_entity_t* entity, const model_t* model, const float (*bones)[3][4],
    unsigned boneCount, goldcraft::studio_stain::Contact* output) noexcept
{
    if (!snapshotPolicy || !entity || entity == viewEntity || entity->player || model != entity->model ||
        (entity->curstate.effects & EF_NODRAW)) return 0;
    const auto field = custom::MonsterStainField(entity->curstate.fuser4);
    const auto slot = static_cast<unsigned>(entity->index);
    if (!field || !slot || slot >= monsterStains.size() || !engine.GetEntityByIndex ||
        engine.GetEntityByIndex(entity->index) != entity) return 0;
    const auto& entry = monsterStains[slot];
    if (entry.id != custom::MonsterStainId(field)) return 0;
    const auto count = std::min(entry.count, custom::MonsterStainCount(field));
    unsigned written{};
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& hit = entry.hits[i];
        if (hit.bone >= boneCount || !(hit.normal[0] || hit.normal[1] || hit.normal[2])) continue;
        const auto& bone = bones[hit.bone];
        auto& projection = output[written++];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const auto& row = bone[axis];
            projection.point[axis] = (row[0] * hit.point[0] + row[1] * hit.point[1] +
                row[2] * hit.point[2]) * (1.f / custom::MonsterStainPositionScale) + row[3];
            projection.normal[axis] = (row[0] * hit.normal[0] + row[1] * hit.normal[1] +
                row[2] * hit.normal[2]) * (1.f / custom::MonsterStainNormalScale);
        }
        const auto seed = (entry.id * 0x9e3779b9u + i * 0x85ebca6bu) >> 24;
        projection.point[3] = 3.5f + float(seed & 15u) * (1.f / 15.f);
        projection.normal[3] = float(seed) * (1.f / 255.f);
    }
    return written;
}

BodyStainViewScope::BodyStainViewScope(const cl_entity_t* entity) noexcept : previous_(viewDrawBaseModel)
{
    if (entity && entity == viewEntity) viewDrawBaseModel = entity->model;
}

BodyStainViewScope::~BodyStainViewScope() { viewDrawBaseModel = previous_; }

BodyStainSurface BodyStainForModel(const cl_entity_t* entity, const model_t* model) noexcept
{
    if (!queryInstalled || !snapshotPolicy || !entity || !model ||
        textureMap != renderer->DecalGeneration()) return {};

    using Surface = custom::BodyStainSurface;
    if (entity == viewEntity)
    {
        // Match the precached model before the studio call substitutes custom
        // hands. Both the stock and adapted models draw gun and arm materials.
        const auto* base = viewDrawBaseModel ? viewDrawBaseModel : entity->model;
        return snapshotDrawsArms && base == snapshotViewModel && model == entity->model ?
            BodyStainSurface{snapshot.body, snapshot.weapon, Surface::ViewWeapon} : BodyStainSurface{};
    }

    const auto stain = custom::DecodeBodyStainField(entity->curstate.fuser4);
    if (!(stain.body || stain.weapon) || entity->index <= 0 ||
        (entity->curstate.effects & EF_NODRAW) || !engine.GetEntityByIndex ||
        engine.GetEntityByIndex(entity->index) != entity) return {};
    if (!entity->player)
    {
        if (model != entity->model) return {};
        return BodyStainSurface{0, stain.weapon, Surface::HeldWeapon};
    }

    const int held = entity->curstate.weaponmodel;
    return stain.weapon && model != entity->model && held > 0 &&
        studio.GetModelByIndex && studio.GetModelByIndex(held) == model ?
        BodyStainSurface{0, stain.weapon, Surface::HeldWeapon} : BodyStainSurface{};
}

bool BodyStainMaterial(BodyStainSurface surface, unsigned material, unsigned armMaterial,
    MetaRendererStudioStain& output) noexcept
{
    using Surface = custom::BodyStainSurface;
    if (surface.surface == Surface::ViewWeapon && armMaterial)
    {
        surface.surface = Surface::ViewArms;
        material = armMaterial;
    }
    const bool body = surface.surface == Surface::Body || surface.surface == Surface::ViewArms;
    const unsigned mark = body ? surface.body : surface.weapon;
    if (!mark) return false;
    const auto placement = custom::PlaceBodyStain(mark, material, surface.surface);
    const auto texture = textures[placement.layer];
    if (!texture) return false;
    output = {texture, {placement.scale[0], placement.scale[1]}, {placement.offset[0], placement.offset[1]}};
    return true;
}
}
