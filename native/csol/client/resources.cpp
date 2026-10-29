// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "gl_state.hpp"
#include "sequence_guard.hpp"
#include "../model_codec.hpp"
#include "../sprite_codec.hpp"
#include "../sequences.hpp"
#include "../sequence_safety.hpp"
#include <cstdlib>
#include <cstring>

namespace csol::client
{
int* hostTextureCache{};
namespace
{
void (*loadStudio)(model_t*,void*);
void (*loadSprite)(model_t*,void*);
// Matched GoldSrc client ABI, distinct from ReHLDS' software sprite structure.
struct SpriteFrame { int width,height; float up,down,left,right; int texture; };
struct SpriteFrameDesc { int type; SpriteFrame* frame; };
struct SpriteHeader { short type,format; int width,height,frames,palette; float beam; void* cache; SpriteFrameDesc frame[1]; };
static_assert(sizeof(SpriteHeader)==36 && sizeof(SpriteFrame)==28);
void Put32(std::vector<std::byte>& out,size_t offset,uint32_t n){memcpy(out.data()+offset,&n,4);}
void LoadStudio(model_t* model,void* bytes)
{
    const auto* header=(const studiohdr_t*)bytes;
    if(header->version!=10 && header->version!=20 && header->version!=21){loadStudio(model,bytes);return;}
    try
    {
        // Inspect the actual file span even for v10: an already converted model
        // can carry safety records without adjacent SEQ files. A naked buffer
        // and its declared length do not establish the file's readable bounds.
        auto owned=ReadResource(model->name);
        DecodeStudioModel(owned);
        if(owned.size()<sizeof(studiohdr_t))throw std::runtime_error("short studio header");
        const auto* source=reinterpret_cast<const studiohdr_t*>(owned.data());
        if(source->id!=0x54534449 || source->version!=10 || source->length<sizeof(studiohdr_t) ||
            std::size_t(source->length)>owned.size())throw std::runtime_error("unsupported or truncated studio model");
        owned.resize(source->length);
        SequenceSafetyView safety;
        // This loader publishes through SequenceLoadScope; actual selections
        // and bone sampling reject unsupported tracks before stock code runs.
        constexpr auto policy=SequenceReadPolicy::AllowUnsupportedTracks;
        const auto stored=ReadSequenceSafety(owned,safety,policy);
        if(stored==SequenceSafetyStatus::Invalid)throw std::runtime_error("invalid studio sequence safety data");
        const auto sequenceFiles=SequenceFiles(model->name,[](const std::string& path){return Exists(path.c_str());});
        if(stored!=SequenceSafetyStatus::Ready && !sequenceFiles.empty())
        {
            std::vector<std::vector<std::byte>> sequences;
            for(const auto& file:sequenceFiles)sequences.push_back(ReadResource(file.c_str()));
            owned=AppendSequences(owned,sequences,policy);
            if(ReadSequenceSafety(owned,safety,policy)!=SequenceSafetyStatus::Ready)
                throw std::runtime_error("converted studio model lacks valid sequence safety data");
        }
        // The stock client writes attachments into cl_entity_t::attachment[4].
        // CSOLSupport retains the full original list in its owned render asset.
        auto* decoded=reinterpret_cast<studiohdr_t*>(owned.data());
        decoded->numattachments=(std::min)(decoded->numattachments,4);
        SequenceLoadScope sequenceLoad(model,owned);
        loadStudio(model,owned.data());
        sequenceLoad.Complete();
        engine.Con_DPrintf("CSOLSupport: MDL decoded %s\n",model->name);
    }
    catch(const std::exception& e){api->SysError("CSOLSupport: %s: %s",model->name,e.what());std::abort();}
}
void LoadSprite(model_t* model,void* bytes)
{
    uint32_t version{};memcpy(&version,(const std::byte*)bytes+4,4);
    if(version!=3){loadSprite(model,bytes);return;}
    try
    {
        const auto source=DecodeSpriteV3(ReadResource(model->name));
        const size_t pixels=size_t(source.Width)*source.Height;
        const size_t frameBytes=20+pixels;
        std::vector<std::byte> compatible(40+2+768+frameBytes*source.Frames.size());
        memcpy(compatible.data(),bytes,40);Put32(compatible,4,2);
        compatible[41]=std::byte{1}; // 256-entry palette.
        for(size_t i=0;i<source.Frames.size();++i)
        {
            const size_t at=810+i*frameBytes;
            Put32(compatible,at,0);
            Put32(compatible,at+4,uint32_t(-int(source.Width)/2));Put32(compatible,at+8,source.Height/2);
            Put32(compatible,at+12,source.Width);Put32(compatible,at+16,source.Height);
        }
        // Let every downstream engine/plugin loader create its normal ownership
        // and render cache, then replace the dummy indexed images with true RGBA.
        loadSprite(model,compatible.data());
        auto* target=(SpriteHeader*)model->cache.data;
        if(!target || target->frames!=source.Frames.size())throw std::runtime_error("unexpected client sprite layout");
        if(!EnsureGraphics())throw std::runtime_error("OpenGL 3.3 is required for CSOL sprites");
        GLState save;
        for(size_t i=0;i<source.Frames.size();++i)
        {
            auto* frame=target->frame[i].frame;
            if(target->frame[i].type!=0 || !frame || !frame->texture)throw std::runtime_error("invalid engine sprite frame");
            glBindTexture(GL_TEXTURE_2D,frame->texture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,source.Width,source.Height,0,GL_RGBA,GL_UNSIGNED_BYTE,source.Frames[i].RGBA.data());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        }
        engine.Con_DPrintf("CSOLSupport: SPR v3 %s (%u frames)\n",model->name,(unsigned)source.Frames.size());
    }
    catch(const std::exception& e){api->SysError("CSOLSupport: %s: %s",model->name,e.what());}
}
}
bool Exists(const char* path)
{
    return interfaces->FileSystem ? interfaces->FileSystem->FileExists(path) : interfaces->FileSystem_HL25->FileExists(path);
}
model_t* LoadEngineModel(const char* path)
{
    // A downstream loader may use the engine's cached GL_Bind while our own
    // textures are bound. Force its first bind; GLState restores both afterward.
    if(hostTextureCache)*hostTextureCache=-1;
    return studio.Mod_ForName(path,false);
}
std::vector<std::byte> ReadResource(const char* path)
{
    auto* fs=interfaces->FileSystem;auto* fs25=interfaces->FileSystem_HL25;
    auto handle=fs?fs->Open(path,"rb"):fs25->Open(path,"rb");
    if(!handle)throw std::runtime_error(std::string("missing resource: ")+path);
    const auto size=fs?fs->Size(handle):fs25->Size(handle);
    std::vector<std::byte> result;
    try
    {
        if(size<1 || size>128*1024*1024)throw std::runtime_error("resource exceeds size bound");
        result.resize(size);
        const int got=fs?fs->Read(result.data(),size,handle):fs25->Read(result.data(),size,handle);
        if(got!=size)throw std::runtime_error("short resource read");
    }
    catch(...){if(fs)fs->Close(handle);else fs25->Close(handle);throw;}
    if(fs)fs->Close(handle);else fs25->Close(handle);
    return result;
}
void MountResources()
{
    const char* path=std::getenv("CSOL_RESOURCE_ROOT");
    if(!path || !*path)interfaces->CommandLine->CheckParm("-csol_resources",&path);
    if(path && *path)
    {
        if(interfaces->FileSystem)interfaces->FileSystem->AddSearchPathNoWrite(path,"CSOL_RESOURCES");
        else interfaces->FileSystem_HL25->AddSearchPathNoWrite(path,"CSOL_RESOURCES");
    }
}
void InstallLoaders()
{
    if(api->GetEngineType()!=ENGINE_GOLDSRC && api->GetEngineType()!=ENGINE_GOLDSRC_HL25)
        throw std::runtime_error("this build targets the verified CS 1.6 GoldSrc client");
    void* mdl=Resolve("Mod_LoadStudioModel",MH_GAMESYMBOL_KIND_FUNCTION);
    void* spr=Resolve("Mod_LoadSpriteModel",MH_GAMESYMBOL_KIND_FUNCTION);
    hostTextureCache=static_cast<int*>(Resolve("currenttexture",MH_GAMESYMBOL_KIND_GLOBAL));
    Hook(mdl,(void*)LoadStudio,(void**)&loadStudio);
    Hook(spr,(void*)LoadSprite,(void**)&loadSprite);
}
}
