#include "precompiled.h"
#include "csol_loader.h"
#include "../csol/model_codec.hpp"
#include "../csol/sprite_codec.hpp"
#include "../csol/dependencies.hpp"
#include "../csol/parts.hpp"

void GoldCraftPrecacheCsolResources(const char* name,studiohdr_t* header)
{
    static_assert(offsetof(studiohdr_t,numtextures)==180 && offsetof(studiohdr_t,numseq)==164);
    static_assert(sizeof(mstudiotexture_t)==80 && sizeof(mstudioseqdesc_t)==176 && sizeof(mstudioevent_t)==76);
    const std::string model=name,json=model.substr(0,model.size()-4)+".json";
    bool special=FS_FileExists(json.c_str()) || !csol::PartsProfile(model).Actions.empty();
    if(header->textureindex>0 && header->numtextures>0 && header->textureindex<=header->length &&
        size_t(header->numtextures)<=(header->length-header->textureindex)/sizeof(mstudiotexture_t))
    {
        const auto* textures=(const mstudiotexture_t*)((const byte*)header+header->textureindex);
        for(int i=0;i<header->numtextures;++i)special|=textures[i].name[0]=='$' || textures[i].name[0]=='#' || textures[i].name[0]=='@' || !strncmp(textures[i].name,"cso_",4);
    }
    if(!special)return;
    try
    {
        auto exists=[](const std::string& file){return FS_FileExists(file.c_str())!=0;};
        auto read=[](const std::string& name)
        {
            auto file=FS_Open(name.c_str(),"rb");if(!file)throw std::runtime_error("missing resource: "+name);
            const auto size=FS_Size(file);if(size<1 || size>128*1024*1024){FS_Close(file);throw std::runtime_error("resource size outside budget");}
            std::vector<std::byte> bytes;
            try{bytes.resize(size);const auto got=FS_Read(bytes.data(),size,1,file);if(got!=size)throw std::runtime_error("short resource read");}
            catch(...){FS_Close(file);throw;}FS_Close(file);return bytes;
        };
        const auto files=csol::WeaponDependencies(model,read,exists);
        for(const auto& file:files)if(file!=model)PF_precache_generic_I(file.c_str());
        Con_Printf("CSOL: %s dependencies=%u added to downloads\n",name,unsigned(files.size()-1));
    }
    catch(const std::exception& error){Sys_Error("CSOL dependency preparation failed: %s: %s",name,error.what());}
}

void GoldCraftDecodeStudio(std::span<std::byte> bytes, const char* name)
{
    try
    {
        if (csol::DecodeStudioModel(bytes))
            Con_DPrintf("CSOL: decoded model %s\n", name);
    }
    catch (const std::exception& error)
    {
        Sys_Error("CSOL: cannot load model %s: %s", name, error.what());
    }
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
