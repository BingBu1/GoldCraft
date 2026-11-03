#include "precompiled.h"
#include "csol_loader.h"
#include "csol_dependency_cache.hpp"
#include "../csol/model_codec.hpp"
#include "../csol/sprite_codec.hpp"
#include "../csol/dependencies.hpp"
#include "../csol/parts.hpp"
#include "../csol/sequences.hpp"
#include "../csol/sequence_safety.hpp"
#include "../csol/material_source.hpp"
#include <chrono>
#include <set>
#ifdef _WIN32
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#endif

namespace
{
using namespace goldcraft::csol_resources;
DependencyCache dependencyCache;

// A batch is one synchronous PrecacheModelSpecifiedFiles pass. Its raw data
// is released at the end; only bounded dependency lists survive a map change.
struct ResourceBatch
{
    struct Input {Stamp stamp;std::shared_ptr<const std::vector<std::byte>> bytes;};
    std::unordered_map<std::string,Input> inputs;
    std::set<std::string> advertised;
    std::size_t retained{},reads{},readBytes{},models{},hits{},rebuilt{};
    const goldcraft::filesystem::ResourceStampAPI* metadata=nullptr;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();

    ResourceBatch()
    {
        if(g_FileSystemFactory)
        {
            const auto* api=reinterpret_cast<const goldcraft::filesystem::ResourceStampAPI*>(
                g_FileSystemFactory(goldcraft::filesystem::ResourceStampVersion,nullptr));
            if(api && api->size==sizeof(*api) && api->query && api->opened)metadata=api;
        }
    }

    Stamp Metadata(const std::string& name,FileHandle_t file=nullptr) const
    {
        Stamp stamp{true,true,false};
        if(metadata)
        {
            const bool ok=file?metadata->opened(g_pFileSystem,file,&stamp.identity,sizeof(stamp.identity))!=0:
                metadata->query(g_pFileSystem,name.c_str(),&stamp.identity,sizeof(stamp.identity))!=0;
            stamp.metadata=stamp.reusable=ok;
        }
        return stamp;
    }

    bool Exists(const std::string& name)
    {
        const auto found=inputs.find(name);
        if(found!=inputs.end())return found->second.stamp.exists;
        const bool exists=FS_FileExists(name.c_str())!=0;
        if(inputs.size()<65536)inputs.emplace(name,Input{Stamp{exists},{}});
        return exists;
    }

    std::vector<std::byte> Read(const std::string& name)
    {
        auto found=inputs.find(name);
        if(found!=inputs.end() && found->second.bytes)return *found->second.bytes;
        auto file=FS_Open(name.c_str(),"rb");
        if(!file)throw std::runtime_error("missing resource: "+name);
        const auto before=Metadata(name,file);
        const auto size=FS_Size(file);
        if(size<1 || size>128*1024*1024){FS_Close(file);throw std::runtime_error("resource size outside budget");}
        std::vector<std::byte> bytes;
        try
        {
            bytes.resize(size);
            if(FS_Read(bytes.data(),size,1,file)!=size)throw std::runtime_error("short resource read");
        }
        catch(...){FS_Close(file);throw;}
        const auto after=Metadata(name,file);
        FS_Close(file);++reads;readBytes+=bytes.size();
        if(before.metadata && after.metadata && before!=after)
            throw std::runtime_error("resource changed during dependency preparation: "+name);
        Stamp stamp=before.metadata && before==after?after:Stamp{true,true,false};
#ifdef _WIN32
        if(!stamp.metadata)stamp.reusable=BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
            reinterpret_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),
            stamp.digest.data(),static_cast<ULONG>(stamp.digest.size()))>=0;
#endif
        if(found!=inputs.end() && found->second.stamp.read && !found->second.stamp.reusable)
            stamp.reusable=false;
        if(found!=inputs.end() && found->second.stamp.read && found->second.stamp.reusable && stamp.reusable)
        {
            // A provider failure changes the validation method, not necessarily
            // the resource. Keep the parsed result but do not retain this entry.
            if(found->second.stamp.metadata!=stamp.metadata)stamp.reusable=false;
            else if(found->second.stamp!=stamp)
                throw std::runtime_error("resource changed during dependency preparation: "+name);
        }
        if(found!=inputs.end() || inputs.size()<65536)
        {
            auto& input=inputs[name];input.stamp=stamp;
            constexpr std::size_t budget=64u*1024u*1024u;
            if(bytes.size()<=budget-retained)
            {
                input.bytes=std::make_shared<const std::vector<std::byte>>(bytes);
                retained+=bytes.size();
            }
        }
        return bytes;
    }

    Stamp Probe(const std::string& name,bool content)
    {
        if(!Exists(name))return Stamp{false};
        if(!content)return Stamp{true};
        auto found=inputs.find(name);
        if(found==inputs.end() || !found->second.stamp.read)
        {
            const auto stamp=Metadata(name);
            if(stamp.metadata && (found!=inputs.end() || inputs.size()<65536))
            {
                inputs[name].stamp=stamp;
                return stamp;
            }
            Read(name);found=inputs.find(name);
        }
        // Hitting a batch budget disables reuse; it never drops dependencies.
        return found==inputs.end()?Stamp{true,true,false}:found->second.stamp;
    }
};
std::unique_ptr<ResourceBatch> resourceBatch;
}

void GoldCraftBeginCsolResources()
{
    resourceBatch=std::make_unique<ResourceBatch>();
}

void GoldCraftEndCsolResources()
{
    if(!resourceBatch)return;
    const auto& batch=*resourceBatch;
    if(batch.models)
        Con_Printf("CSOL: dependencies models=%u cache_hits=%u rebuilt=%u downloads=%u reads=%u (%.1f MiB) time=%.3fs\n",
            unsigned(batch.models),unsigned(batch.hits),unsigned(batch.rebuilt),unsigned(batch.advertised.size()),
            unsigned(batch.reads),double(batch.readBytes)/(1024.0*1024.0),
            std::chrono::duration<double>(std::chrono::steady_clock::now()-batch.started).count());
    resourceBatch.reset();
}

void GoldCraftCsolSequenceDelta(delta_t* delta)
{
    // The server transmits this schema to clients. Extend before delta JIT
    // compilation, otherwise appended player sequences >=256 wrap on the wire.
    for(int i=0;i<delta->fieldCount;++i)
    {
        auto& field=delta->pdd[i];
        if(!Q_strcmp(field.fieldName,"sequence") || !Q_strcmp(field.fieldName,"gaitsequence"))
        {
            field.fieldType=DT_INTEGER;
            field.significant_bits=16;
            field.premultiply=field.postmultiply=1.0f;
        }
    }
}

void GoldCraftPrecacheCsolResources(const char* name,studiohdr_t* header)
{
    static_assert(offsetof(studiohdr_t,numtextures)==180 && offsetof(studiohdr_t,numseq)==164);
    static_assert(sizeof(mstudiotexture_t)==80 && sizeof(mstudioseqdesc_t)==176 && sizeof(mstudioevent_t)==76);
    ResourceBatch standalone;
    auto& batch=resourceBatch?*resourceBatch:standalone;
    const auto exists=[&](const std::string& file){return batch.Exists(file);};
    const std::string model=name,json=model.substr(0,model.size()-4)+".json";
    bool special=model=="models/v_tknifeex2.mdl" || exists(json) || exists(csol::ModelMaterialSource(model)) || exists(csol::ModelSourceDescriptor(model)) || !csol::PartsProfile(model).Actions.empty()
        || !csol::SequenceFiles(model,exists).empty();
    if(header->textureindex>0 && header->numtextures>0 && header->textureindex<=header->length &&
        size_t(header->numtextures)<=(header->length-header->textureindex)/sizeof(mstudiotexture_t))
    {
        const auto* textures=(const mstudiotexture_t*)((const byte*)header+header->textureindex);
        for(int i=0;i<header->numtextures;++i)special|=textures[i].name[0]=='$' || textures[i].name[0]=='#' || textures[i].name[0]=='@' || !strncmp(textures[i].name,"cso_",4);
    }
    if(!special)return;
    try
    {
        ++batch.models;
        const auto probe=[&](const std::string& file,bool content){return batch.Probe(file,content);};
        auto files=dependencyCache.Find(model,probe);
        if(files)++batch.hits;
        else
        {
            ++batch.rebuilt;
            Inputs inputs;
            const auto trackedExists=[&](const std::string& file)
            {
                const auto stamp=probe(file,false);
                if(!inputs[file].read)inputs[file]=stamp;
                return stamp.exists;
            };
            const auto read=[&](const std::string& file)
            {
                auto bytes=batch.Read(file);inputs[file]=probe(file,true);return bytes;
            };
            files=std::make_shared<const Files>(csol::WeaponDependencies(model,read,trackedExists));
            dependencyCache.Store(model,files,std::move(inputs),probe);
        }
        for(const auto& file:*files)
            if(file!=model && batch.advertised.insert(file).second)PF_precache_generic_I(file.c_str());
    }
    catch(const std::exception& error){Sys_Error("CSOL dependency preparation failed: %s: %s",name,error.what());}
}

std::vector<std::byte> GoldCraftDecodeStudio(std::span<std::byte> bytes, const char* name)
{
    try
    {
        if (csol::DecodeStudioModel(bytes))
            Con_DPrintf("CSOL: decoded model %s\n", name);
        csol::SequenceSafetyView safety;
        // Mod_LoadModel passes this owned result to GoldCraftLoadStudioOwned;
        // all supported engine/GameDLL samplers check the selected track.
        constexpr auto policy=csol::SequenceReadPolicy::AllowUnsupportedTracks;
        const auto stored=csol::ReadSequenceSafety(bytes,safety,policy);
        if(stored==csol::SequenceSafetyStatus::Invalid)
            throw std::runtime_error("invalid studio sequence safety data");
        // Already packed files retain their sequence numbering even when the
        // original SEQ files are still present next to them.
        if(stored==csol::SequenceSafetyStatus::Ready)return {};
        const auto files=csol::SequenceFiles(name,[](const std::string& path){return FS_FileExists(path.c_str())!=0;});
        if(files.empty())return {};
        std::vector<std::vector<std::byte>> sequences;
        for(const auto& path:files)
        {
            auto file=FS_Open(path.c_str(),"rb");if(!file)throw std::runtime_error("missing SEQ file");
            const auto size=FS_Size(file);
            if(size<8 || size>128*1024*1024){FS_Close(file);throw std::runtime_error("SEQ size outside budget");}
            std::vector<std::byte> data;
            try{data.resize(size);if(FS_Read(data.data(),size,1,file)!=size)throw std::runtime_error("short SEQ read");}
            catch(...){FS_Close(file);throw;}
            FS_Close(file);sequences.push_back(std::move(data));
        }
        auto result=csol::AppendSequences(bytes,sequences,policy);
        if(csol::ReadSequenceSafety(result,safety,policy)!=csol::SequenceSafetyStatus::Ready)
            throw std::runtime_error("converted studio model lacks valid sequence safety data");
        Con_DPrintf("CSOL: %s appended %u SEQ files\n",name,unsigned(files.size()));
        return result;
    }
    catch (const std::exception& error)
    {
        Sys_Error("CSOL: cannot load model %s: %s", name, error.what());
    }
    return {};
}

bool GoldCraftLoadCsolSprite(model_t* model, std::span<const std::byte> bytes)
{
    if (bytes.size() < 8 || bytes[4] != std::byte{3})
        return false;
    try
    {
        const auto source = csol::DecodeSpriteV3(bytes, false);
        const int count = (int)source.Frames.size();
        const int size = sizeof(msprite_t) + (count - 1) * sizeof(mspriteframedesc_t);
        auto* sprite = static_cast<msprite_t*>(Hunk_AllocName(size, loadname));
        Q_memset(sprite, 0, size);
        sprite->type = source.Type;
        sprite->texFormat = source.Format;
        sprite->maxwidth = source.Width;
        sprite->maxheight = source.Height;
        sprite->numframes = count;
        sprite->beamlength = source.BeamLength;
        for (int i = 0; i < count; ++i)
        {
            auto* frame = static_cast<mspriteframe_t*>(Hunk_AllocName(sizeof(mspriteframe_t), loadname));
            Q_memset(frame, 0, sizeof(*frame));
            frame->width = source.Width;
            frame->height = source.Height;
            frame->left = -(int)source.Width / 2;
            frame->right = frame->left + source.Width;
            frame->up = (int)source.Height / 2;
            frame->down = frame->up - source.Height;
            sprite->frames[i].type = SPR_SINGLE;
            sprite->frames[i].frameptr = frame;
        }
        model->cache.data = sprite;
        model->type = mod_sprite;
        model->synctype = (synctype_t)source.SyncType;
        model->numframes = count;
        model->flags = 0;
        model->mins[0] = model->mins[1] = -(int)source.Width / 2;
        model->maxs[0] = model->maxs[1] = (int)source.Width / 2;
        model->mins[2] = -(int)source.Height / 2;
        model->maxs[2] = (int)source.Height / 2;
        Con_DPrintf("CSOL: loaded SPR v3 %s (%d frames)\n", model->name, count);
    }
    catch (const std::exception& error)
    {
        Sys_Error("CSOL: cannot load sprite %s: %s", model->name, error.what());
    }
    return true;
}
