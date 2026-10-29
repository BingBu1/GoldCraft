// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequence_guard.hpp"
#include <cstdlib>
#include <cstring>

namespace csol::client
{
namespace
{
SequenceOwnerRegistry owners;
std::uint32_t headerMember{},modelMember{};
studiohdr_t** engineHeader{};
model_t** engineModel{};
cvar_t* boneCache{};
bool studioGuardsInstalled{};
std::array<hook_t*,4> studioHooks{};
std::array<hook_t*,2> clientHooks{};
void* (*cacheAlloc)(cache_user_t*,int,const char*);
void (*memoryInit)(void*,int);
using ClientGetAnim=mstudioanim_t* (__fastcall *)(void*,int,model_t*,mstudioseqdesc_t*);
using ClientRotations=void (__fastcall *)(void*,int,float (*)[3],float (*)[4],mstudioseqdesc_t*,mstudioanim_t*,float);
ClientGetAnim clientGetAnim;
ClientRotations clientRotations;
void (__fastcall *clientSetup)(void*,int);
void (__fastcall *clientMerge)(void*,int,model_t*);
mstudioanim_t* (*engineGetAnim)(model_t*,mstudioseqdesc_t*);
void (*engineRotations)(float (*)[3],float (*)[4],mstudioseqdesc_t*,mstudioanim_t*,float);
void (*engineSetup)();
void (*engineMerge)(model_t*);
using ServerSetup=void (*)(model_t*,float,int,const float*,const float*,const unsigned char*,const unsigned char*,int,const void*);
ServerSetup serverSetup;
int (*computeBounds)(unsigned char*,float*,float*);
int (*getBounds)(const char*,float*,float*);

std::uintptr_t Owner(model_t* model){return reinterpret_cast<std::uintptr_t>(&model->cache);}
std::uintptr_t Base(model_t* model){return reinterpret_cast<std::uintptr_t>(model->cache.data);}
[[noreturn]] void Fatal(model_t* model,const char* step,SequenceOwnerCheck result)
{
    api->SysError("CSOLSupport: studio safety %s: %s (%u)",model?model->name:"<no model>",step,unsigned(result));
    std::abort();
}
template<class T>T* Member(void* object,std::uint32_t offset)
{
    T* result{};std::memcpy(&result,static_cast<const std::byte*>(object)+offset,sizeof(result));return result;
}
struct SamplingScope
{
    SamplingScope* previous{};
    model_t* model{};
    cvar_t* cache{};
    float saved{};
    static thread_local SamplingScope* current;
    SamplingScope(model_t* value,studiohdr_t* header):previous(current),model(value)
    {
        if(!model || model->type!=mod_studio)Fatal(model,"invalid model",SequenceOwnerCheck::MissingOwner);
        const auto state=owners.ModelState(Owner(model),Base(model),header);
        if(state!=SequenceOwnerCheck::Ready && state!=SequenceOwnerCheck::Ordinary)Fatal(model,"setup",state);
        if(state==SequenceOwnerCheck::Ready)
        {
            if(!boneCache)boneCache=engine.pfnGetCvarPointer("r_studio_bone_caches");
            cache=boneCache;
            if(cache){saved=cache->value;cache->value=0;}
        }
        current=this;
    }
    ~SamplingScope(){current=previous;if(cache)cache->value=saved;}
};
thread_local SamplingScope* SamplingScope::current{};

SequenceOwnerCheck Selected(model_t* model,studiohdr_t* header,mstudioseqdesc_t* descriptor,SequenceSample& sample)
{
    if(!model || model->type!=mod_studio)Fatal(model,"animation owner",SequenceOwnerCheck::MissingOwner);
    const auto result=owners.Select(Owner(model),Base(model),header,descriptor,sample);
    if(result!=SequenceOwnerCheck::Ordinary && result!=SequenceOwnerCheck::Ready)Fatal(model,"animation selection",result);
    return result;
}
void CheckRotations(studiohdr_t* header,mstudioseqdesc_t* descriptor,mstudioanim_t* animation,float frame)
{
    auto* scope=SamplingScope::current;
    if(!scope)Fatal(nullptr,"sampler outside setup",SequenceOwnerCheck::MissingOwner);
    SequenceSample sample;
    if(Selected(scope->model,header,descriptor,sample)==SequenceOwnerCheck::Ordinary)return;
    float integer{},fraction{};
    if(!ClientSequenceFrame(frame,sample.selection.frames,integer,fraction))
        Fatal(scope->model,"nonfinite frame",SequenceOwnerCheck::InvalidFrame);
    const auto checked=owners.Sample(sample,Base(scope->model),animation,integer,fraction,true);
    if(checked!=SequenceOwnerCheck::Ready)Fatal(scope->model,"sampler",checked);
}
void* CacheAlloc(cache_user_t* cache,int bytes,const char* name)
{
    try{owners.Allocating(reinterpret_cast<std::uintptr_t>(cache),bytes>0?std::size_t(bytes):0);}
    catch(const std::exception& e){api->SysError("CSOLSupport: cache ownership: %s",e.what());std::abort();}
    return cacheAlloc(cache,bytes,name);
}
void MemoryInit(void* bytes,int size){ResetSequenceOwners();memoryInit(bytes,size);}
void __fastcall ClientSetup(void* self,int)
{
    SamplingScope scope(Member<model_t>(self,modelMember),Member<studiohdr_t>(self,headerMember));
    clientSetup(self,0);
}
void __fastcall ClientMerge(void* self,int,model_t* model)
{
    SamplingScope scope(model,Member<studiohdr_t>(self,headerMember));clientMerge(self,0,model);
}
mstudioanim_t* __fastcall GetClientAnim(void* self,int,model_t* model,mstudioseqdesc_t* descriptor)
{
    SequenceSample sample;Selected(model,Member<studiohdr_t>(self,headerMember),descriptor,sample);
    if(SamplingScope::current)SamplingScope::current->model=model;
    return clientGetAnim(self,0,model,descriptor);
}
void __fastcall ClientCalc(void* self,int,float (*positions)[3],float (*quaternions)[4],
    mstudioseqdesc_t* descriptor,mstudioanim_t* animation,float frame)
{
    CheckRotations(Member<studiohdr_t>(self,headerMember),descriptor,animation,frame);
    clientRotations(self,0,positions,quaternions,descriptor,animation,frame);
}
void EngineSetup(){SamplingScope scope(*engineModel,*engineHeader);engineSetup();}
void EngineMerge(model_t* model){SamplingScope scope(model,*engineHeader);engineMerge(model);}
mstudioanim_t* GetEngineAnim(model_t* model,mstudioseqdesc_t* descriptor)
{
    SequenceSample sample;Selected(model,*engineHeader,descriptor,sample);
    if(SamplingScope::current)SamplingScope::current->model=model;
    return engineGetAnim(model,descriptor);
}
void EngineCalc(float (*positions)[3],float (*quaternions)[4],mstudioseqdesc_t* descriptor,mstudioanim_t* animation,float frame)
{
    CheckRotations(*engineHeader,descriptor,animation,frame);
    engineRotations(positions,quaternions,descriptor,animation,frame);
}
void EngineServerSetup(model_t* model,float frame,int sequence,const float* angles,const float* origin,
    const unsigned char* controller,const unsigned char* blending,int bone,const void* entity)
{
    SamplingScope scope(model,*engineHeader);
    if(owners.ModelState(Owner(model),Base(model),*engineHeader)==SequenceOwnerCheck::Ready)
    {
        auto* header=*engineHeader;
        if(sequence<0 || sequence>=header->numseq)sequence=0;
        auto* descriptor=reinterpret_cast<mstudioseqdesc_t*>(reinterpret_cast<std::byte*>(header)+header->seqindex)+sequence;
        SequenceSample sample;Selected(model,header,descriptor,sample);
        // This fallback uses float multiply, then double 1/256, then float.
        // Unlike the client renderer, it does not normalize out-of-range f.
        const float f=sample.selection.frames>1?float(double(float(sample.selection.frames-1)*frame)*(1.0/256.0)):0;
        if(!std::isfinite(f) || f<0 || f>float(sample.selection.frames-1))
            Fatal(model,"server frame",SequenceOwnerCheck::InvalidFrame);
        const float integer=std::trunc(f),fraction=f-integer;
        const auto* animation=reinterpret_cast<const std::byte*>(header)+sample.selection.animation;
        auto checked=owners.Sample(sample,Base(model),animation,integer,fraction,true);
        if(checked!=SequenceOwnerCheck::Ready)Fatal(model,"server sampler",checked);
        if(sample.selection.blends>1)
        {
            checked=owners.Sample(sample,Base(model),animation+sample.selection.bones*12,integer,fraction,true);
            if(checked!=SequenceOwnerCheck::Ready)Fatal(model,"server second blend",checked);
        }
    }
    serverSetup(model,frame,sequence,angles,origin,controller,blending,bone,entity);
}
int StudioBounds(const char* name,float* mins,float* maxs)
{
    for(int axis=0;axis<3;++axis)mins[axis]=maxs[axis]=0;
    if(!name || !std::strstr(name,"models") || !std::strstr(name,".mdl") || !Exists(name))return 0;
    try
    {
        // Use one owned read for both validation and consumption. Calling the
        // old filename wrapper afterwards would reopen a potentially new file.
        auto bytes=ReadResource(name);
        if(bytes.size()<sizeof(studiohdr_t))return 0;
        const auto* header=reinterpret_cast<const studiohdr_t*>(bytes.data());
        if(header->id!=0x54534449)return 0;
        SequenceSafetyView safety;
        const auto state=ReadSequenceSafety(bytes,safety);
        if(state==SequenceSafetyStatus::Invalid || (state==SequenceSafetyStatus::Ready && header->numbones>128))
            throw std::runtime_error("invalid studio bounds sequence data");
        return computeBounds(reinterpret_cast<unsigned char*>(bytes.data()),mins,maxs);
    }
    catch(const std::exception& e){api->SysError("CSOLSupport: bounds %s: %s",name,e.what());std::abort();}
}
void* ResolveClient(const char* name,mh_gamesymbol_kind_t kind)
{
    void* result{};
    if(api->ResolveGameSymbol(api->GetClientBase(),name,kind,&result)!=MH_GAMESYMBOL_OK || !result)
        throw std::runtime_error(std::string("unavailable matched client symbol: ")+name);
    return result;
}
}

SequenceLoadScope::SequenceLoadScope(model_t* model,std::span<const std::byte> bytes)
    :model_(model),prepared_(PreparedSequenceModel::Read(bytes,SequenceReadPolicy::AllowUnsupportedTracks)),
      token_(owners.BeginLoad(Owner(model))){}
SequenceLoadScope::~SequenceLoadScope(){if(!completed_)owners.CancelLoad(Owner(model_),token_);}
void SequenceLoadScope::Complete()
{
    const auto retained=prepared_.RetainedBytes();
    if(!owners.CompleteLoad(Owner(model_),token_,std::move(prepared_),Base(model_),retained))
        Fatal(model_,"loader receipt",SequenceOwnerCheck::MissingOwner);
    completed_=true;
}
void ResetSequenceOwners()
{
    try{owners.Reset();}
    catch(const std::exception& e){api->SysError("CSOLSupport: studio owner reset: %s",e.what());std::abort();}
}
void InstallSequenceEngineGuards()
{
    engineHeader=static_cast<studiohdr_t**>(Resolve("pstudiohdr",MH_GAMESYMBOL_KIND_GLOBAL));
    engineModel=static_cast<model_t**>(Resolve("r_model",MH_GAMESYMBOL_KIND_GLOBAL));
    Hook(Resolve("CSOL_Cache_Alloc",MH_GAMESYMBOL_KIND_FUNCTION),(void*)CacheAlloc,(void**)&cacheAlloc);
    Hook(Resolve("CSOL_Memory_Init",MH_GAMESYMBOL_KIND_FUNCTION),(void*)MemoryInit,(void**)&memoryInit);
    Hook(Resolve("R_GetAnim",MH_GAMESYMBOL_KIND_FUNCTION),(void*)GetEngineAnim,(void**)&engineGetAnim);
    Hook(Resolve("R_StudioCalcRotations",MH_GAMESYMBOL_KIND_FUNCTION),(void*)EngineCalc,(void**)&engineRotations);
    Hook(Resolve("CSOL_SV_StudioSetupBones",MH_GAMESYMBOL_KIND_FUNCTION),(void*)EngineServerSetup,(void**)&serverSetup);
    computeBounds=reinterpret_cast<decltype(computeBounds)>(Resolve("CSOL_R_StudioComputeBounds",MH_GAMESYMBOL_KIND_FUNCTION));
    Hook(Resolve("CSOL_R_GetStudioBounds",MH_GAMESYMBOL_KIND_FUNCTION),(void*)StudioBounds,(void**)&getBounds);
}
void InstallSequenceClientGuards()
{
    if(api->QueryGameSymbolStructMember(api->GetClientBase(),"CSOL_StudioRenderer_Header",&headerMember)!=MH_GAMESYMBOL_OK ||
        api->QueryGameSymbolStructMember(api->GetClientBase(),"CSOL_StudioRenderer_SetupModel",&modelMember)!=MH_GAMESYMBOL_OK)
        throw std::runtime_error("missing matched studio renderer member contract");
    clientHooks[0]=Hook(ResolveClient("GameStudioRenderer_StudioGetAnim",MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION),(void*)GetClientAnim,(void**)&clientGetAnim);
    clientHooks[1]=Hook(ResolveClient("GameStudioRenderer_StudioCalcRotations",MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION),(void*)ClientCalc,(void**)&clientRotations);
}
void InstallSequenceStudioGuards()
{
    if(studioGuardsInstalled)return;
    // Renderer installs its cache wrappers while negotiating the studio API.
    // The public draw callback calls this after the complete studio negotiation
    // (including wrappers outside our own GetStudioInterface) has returned.
    // An earlier hook could be bypassed by Renderer's cache-hit return.
    studioHooks[0]=Hook(Resolve("R_StudioSetupBones",MH_GAMESYMBOL_KIND_FUNCTION),(void*)EngineSetup,(void**)&engineSetup);
    studioHooks[1]=Hook(Resolve("R_StudioMergeBones",MH_GAMESYMBOL_KIND_FUNCTION),(void*)EngineMerge,(void**)&engineMerge);
    studioHooks[2]=Hook(ResolveClient("GameStudioRenderer_StudioSetupBones",MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION),(void*)ClientSetup,(void**)&clientSetup);
    studioHooks[3]=Hook(ResolveClient("GameStudioRenderer_StudioMergeBones",MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION),(void*)ClientMerge,(void**)&clientMerge);
    studioGuardsInstalled=true;
}
void RemoveSequenceStudioGuards()
{
    for(auto it=studioHooks.rbegin();it!=studioHooks.rend();++it)if(*it){RemoveHook(*it);*it=nullptr;}
    studioGuardsInstalled=false;boneCache=nullptr;
}
void RemoveSequenceClientGuards()
{
    RemoveSequenceStudioGuards();
    for(auto it=clientHooks.rbegin();it!=clientHooks.rend();++it)if(*it){RemoveHook(*it);*it=nullptr;}
    headerMember=modelMember=0;
}
}
