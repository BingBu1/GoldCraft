// SPDX-License-Identifier: GPL-3.0-or-later
// run_resources.py supplies exact production function extracts through stdin.
#include "precompiled.h"
#include "precache.h"
#include "map_load_timing.h"
#include "csol_studio_resources.h"
#include "sequence_adapter_fixture.hpp"
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace resource_test
{
using namespace adapter_test;
struct Guarded
{
    void* allocation{}; std::byte* data{}; std::size_t size{};
    explicit Guarded(std::size_t bytes):size(bytes)
    {
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        const auto committed=((bytes+64+info.dwPageSize-1)/info.dwPageSize)*info.dwPageSize;
        allocation=VirtualAlloc(nullptr,committed+info.dwPageSize,MEM_RESERVE,PAGE_NOACCESS);
        if(!allocation || !VirtualAlloc(allocation,committed,MEM_COMMIT,PAGE_READWRITE))throw std::bad_alloc();
        data=static_cast<std::byte*>(allocation)+committed-bytes;
        std::memset(data-64,0xAC,64); std::memset(data,0xDA,bytes);
    }
    ~Guarded(){if(allocation)VirtualFree(allocation,0,MEM_RELEASE);}
    void Check(){for(int i=1;i<=64;++i)Require(data[-i]==std::byte{0xAC},"allocation underrun");}
};
std::map<std::string,Bytes> files;
std::map<void*,std::unique_ptr<Guarded>> memory;
std::map<cache_user_t*,std::unique_ptr<Guarded>> caches;
std::vector<std::string> advertisements,reads;
unsigned cacheAllocations{},cacheChecks{},loads{},modelCallbacks{},chainCalls{},fileAllocations{},fileFrees{};
std::function<void()> genericCallback;
struct Registry
{
    bool present{};
    bool isEmpty() const{return !present;}
    void callChain(void(*callback)(model_t*,void*),model_t* model,void* input)
    {++chainCalls; callback(model,input);}
};
struct Hooks{Registry m_Mod_LoadStudioModel,m_PF_precache_generic_I;} hooks;
}

// Registry dispatch, allocation and file access are explicit SDK boundaries.
#define g_RehldsHookchains resource_test::hooks
#include "external/ReHLDS/rehlds/engine/l_studio.cpp"
#include "native/engine/csol_sequence_guard.cpp"
#include "native/csol/sequences.cpp"
// RESOURCE_PRODUCTION_EXTRACTS
#undef g_RehldsHookchains

namespace resource_test
{
template<class T> unsigned Add(Bytes& bytes,const T& value={})
{
    const auto offset=unsigned((bytes.size()+alignof(T)-1)/alignof(T)*alignof(T));
    bytes.resize(offset+sizeof(T)); Put(bytes,offset,value); return offset;
}
Bytes Plain(bool textured=true)
{
    Bytes bytes(sizeof(studiohdr_t)); studiohdr_t h{}; h.id=IDSTUDIOHEADER; h.version=10;
    mstudiobone_t bone{}; bone.parent=-1;
    for(auto& value:bone.bonecontroller)value=-1;
    for(auto& value:bone.scale)value=.01f;
    h.numbones=1; h.boneindex=Add(bytes,bone);
    h.numseq=1; h.seqindex=Add(bytes,mstudioseqdesc_t{});
    h.numseqgroups=2; h.seqgroupindex=Add(bytes,mstudioseqgroup_t{});
    mstudioseqgroup_t group{}; std::strcpy(group.name,"models/group01.mdl"); Add(bytes,group);
    mstudioevent_t event{}; event.event=5004; std::strcpy(event.options,"fixture.wav");
    const auto events=Add(bytes,event); event.event=5008; std::strcpy(event.options,"1"); Add(bytes,event);
    mstudioanim_t animation{}; const auto animationAt=Add(bytes,animation);
    mstudioseqdesc_t seq{}; seq.numframes=1; seq.numblends=1; seq.animindex=animationAt;
    seq.numevents=2; seq.eventindex=events; Put(bytes,h.seqindex,seq);
    h.numskinref=1; h.numskinfamilies=1; h.skinindex=Add(bytes,std::uint16_t{});
    if(textured)
    {
        h.numtextures=1; h.textureindex=Add(bytes,mstudiotexture_t{}); h.texturedataindex=int(bytes.size());
        mstudiotexture_t texture{}; std::strcpy(texture.name,"tex.bmp");
        texture.width=2; texture.height=2; texture.index=int(bytes.size()); Put(bytes,h.textureindex,texture);
        for(unsigned i=0;i<772;++i)Add(bytes,std::uint8_t(i));
    }
    h.length=int(bytes.size()); Put(bytes,0,h); return bytes;
}
Bytes Tagged()
{
    auto bytes=Model(true,true); const auto retained=Retained(bytes),texture=Word(bytes,184);
    bytes.resize(retained+772); Put(bytes,72,unsigned(bytes.size())); Put(bytes,texture+68,2); Put(bytes,texture+72,2);
    std::strcpy(reinterpret_cast<char*>(bytes.data()+texture),"tagged.bmp");
    return bytes;
}
std::pair<Bytes,Bytes> AppendInputs()
{
    auto base=Tagged(); const auto descriptors=Word(base,168);
    Put(base,descriptors-32,0u); Put(base,descriptors-28,0u);
    Bytes seq(8); mstudioevent_t event{}; event.event=5004; std::strcpy(event.options,"appended.wav");
    Add(seq,event); Add(seq,mstudiopivot_t{});
    const auto animation=Word(base,descriptors+124);
    seq.insert(seq.end(),base.begin()+animation,base.begin()+animation+56);
    auto descriptor=*reinterpret_cast<const mstudioseqdesc_t*>(base.data()+descriptors);
    descriptor.numevents=1; descriptor.numpivots=1; descriptor.animindex=1000;
    const auto trailer=Add(seq,descriptor); Put(seq,0,1); Put(seq,4,trailer);
    return {base,seq};
}
void Reset()
{
    GoldCraftEndCsolResources(); GoldCraftStudioMemoryReset();
    for(auto& [_,cache]:caches)cache->Check();
    caches.clear(); Require(memory.empty(),"temporary file retained after pass");
    dependencyCache=goldcraft::csol_resources::DependencyCache{};
    files.clear(); advertisements.clear(); reads.clear(); hooks={}; g_modfuncs={}; genericCallback={};
    gc_precache.modelNames={nullptr}; gc_precache.models={nullptr};
    cacheAllocations=cacheChecks=loads=modelCallbacks=chainCalls=fileAllocations=fileFrees=0;
    g_bIsDedicatedServer=TRUE; sv_auto_precache_sounds_in_models.value=1;
}
void AddModel(model_t& model,const char* readName,const char* advertiseName,const Bytes& bytes)
{
    std::strcpy(model.name,readName); model.type=mod_studio;
    files[readName]=bytes; if(!files.contains(advertiseName))files[advertiseName]=bytes;
    gc_precache.modelNames.push_back(advertiseName); gc_precache.models.push_back(&model);
}
void Resources()
{
    for(const auto* path:{"sound/fixture.wav","sound/appended.wav","sound/updated.wav","sound/live.wav",
        "sound/zombi/oberon/knife_walk1.wav"})files[path]={std::byte{1}};
}
bool Contains(const char* name)
{return std::find(advertisements.begin(),advertisements.end(),name)!=advertisements.end();}
void ComparePasses()
{
    for(int sounds:{0,1})
    {
        std::vector<std::string> expected;
        for(bool current:{false,true})
        {
            Reset(); Resources(); sv_auto_precache_sounds_in_models.value=float(sounds);
            model_t plain{},external{},tagged{},appended{},ignored{};
            AddModel(plain,"models/fixture.mdl","models/fixture.mdl",Plain());
            AddModel(external,"models/readalias.mdl","models/advertise.mdl",Plain(false));
            files["models/advertiseT.mdl"]=Plain();
            AddModel(tagged,"models/tagged.mdl","models/tagged.mdl",Tagged());
            auto [base,seq]=AppendInputs(); files["models/appended1.seq"]=seq;
            AddModel(appended,"models/appended.mdl","models/appended.mdl",base);
            gc_precache.modelNames.push_back(nullptr); gc_precache.models.push_back(&ignored);
            gc_precache.modelNames.push_back("models/ignored.mdl"); gc_precache.models.push_back(nullptr);
            if(current)PrecacheModelSpecifiedFiles(); else LegacyPrecacheModelSpecifiedFiles();
            if(!current)expected=advertisements;
            else Require(advertisements==expected,"new pass changed ordered advertisements");
            Require(Contains("models/advertiseT.mdl"),"advertisement used read-name instead of alias");
            Require(std::find(reads.begin(),reads.end(),"models/readalias.mdl")!=reads.end(),"alias was read from wrong path");
            Require(Contains("sound/zombi/oberon/knife_walk1.wav"),"numeric 5008 closure lost");
            Require(std::count(advertisements.begin(),advertisements.end(),"sound/zombi/oberon/knife_walk1.wav")==1,"5008 batch deduplication lost");
            Require(Contains("sound/appended.wav"),"actual SEQ append resource lost");
            Require(cacheAllocations==(current?0u:4u),"eligible pass unexpectedly allocated model cache");
            Require(loads==(current?0u:4u),"eligible pass entered full loader");
            Require(fileAllocations==fileFrees && memory.empty(),"temporary full decoded files leaked");
            if(current)Require(!plain.cache.data && !external.cache.data && !tagged.cache.data && !appended.cache.data,"temporary read published a cache");
        }
    }
}
void ModelCallback(model_t* model,void*)
{
    ++modelCallbacks;
    auto* h=static_cast<studiohdr_t*>(model->cache.data);
    auto* s=reinterpret_cast<mstudioseqdesc_t*>(reinterpret_cast<byte*>(h)+h->seqindex);
    auto* e=reinterpret_cast<mstudioevent_t*>(reinterpret_cast<byte*>(h)+s->eventindex);
    std::strcpy(e->options,"live.wav");
}
void Gates()
{
    for(unsigned mode=0;mode<5;++mode)
    {
        Reset(); Resources(); model_t model{}; AddModel(model,"models/gate.mdl","models/gate.mdl",Plain());
        if(mode==0)g_bIsDedicatedServer=FALSE;
        if(mode==1)hooks.m_Mod_LoadStudioModel.present=true;
        if(mode==2)g_modfuncs.m_pfnModelLoad=ModelCallback;
        if(mode==3)
        {
            hooks.m_PF_precache_generic_I.present=true;
            genericCallback=[] {sv_auto_precache_sounds_in_models.value=0;};
        }
        if(mode==4)
        {
            auto& bytes=files[model.name]; Put(bytes,offsetof(studiohdr_t,numseqgroups),0);
            Put(bytes,offsetof(studiohdr_t,seqgroupindex),-1);
        }
        PrecacheModelSpecifiedFiles();
        Require(loads==1 && cacheAllocations==1,"gate did not use exactly one real loader");
        Require(fileAllocations==(mode==4?2u:1u),"gate read count changed");
        Require(memory.empty() && fileFrees==fileAllocations,"fallback leaked temporary read");
        if(mode==1)Require(chainCalls==1,"hooked fallback skipped original dispatch");
        if(mode==2)Require(modelCallbacks==1 && Contains("sound/live.wav"),"post-load callback metadata lost");
    }
    Reset(); Resources(); model_t resident{}; AddModel(resident,"models/live.mdl","models/live.mdl",Plain());
    Mod_Extradata(&resident); ModelCallback(&resident,nullptr);
    loads=cacheAllocations=cacheChecks=0; reads.clear();
    PrecacheModelSpecifiedFiles();
    Require(loads==0 && cacheAllocations==0 && cacheChecks==1,"resident bypassed real Mod_Extradata/Cache_Check");
    Require(Contains("sound/live.wav"),"resident live metadata ignored");
}
void Incompatible()
{
    const auto original=Plain(); const auto h=*reinterpret_cast<const studiohdr_t*>(original.data());
    const auto seq=*reinterpret_cast<const mstudioseqdesc_t*>(original.data()+h.seqindex);
    for(unsigned mode=0;mode<9;++mode)
    {
        Reset(); model_t model{}; auto bytes=original;
        switch(mode)
        {
        case 0:Put(bytes,offsetof(studiohdr_t,seqindex),h.texturedataindex); break;
        case 1:Put(bytes,offsetof(studiohdr_t,numseq),INT_MAX); break;
        case 2:Put(bytes,offsetof(studiohdr_t,textureindex),-1); break;
        case 3:Put(bytes,h.seqindex+offsetof(mstudioseqdesc_t,eventindex),h.texturedataindex); break;
        case 4:Put(bytes,h.seqindex+offsetof(mstudioseqdesc_t,numevents),-1); break;
        case 5:std::memset(bytes.data()+h.seqgroupindex+sizeof(mstudioseqgroup_t)+offsetof(mstudioseqgroup_t,name),'x',64); break;
        case 6:std::memset(bytes.data()+seq.eventindex+offsetof(mstudioevent_t,options),'x',64); break;
        case 7:Put(bytes,offsetof(studiohdr_t,numtextures),4097); break;
        case 8:Put(bytes,offsetof(studiohdr_t,length),INT_MAX); break;
        }
        AddModel(model,"models/invalid-layout.mdl","models/invalid-layout.mdl",bytes);
        {GoldCraftStudioResourceFile file; Require(!file.Load(&model),"incompatible metadata entered temporary consumer");}
        Require(advertisements.empty() && cacheAllocations==0 && memory.empty(),"rejected metadata had partial side effects");
    }
}
void TimingAndUnsupported()
{
    for(bool disableDuringGeneric:{false,true})
    {
        Reset(); Resources(); model_t model{}; auto bytes=Plain();
        const auto h=*reinterpret_cast<const studiohdr_t*>(bytes.data());
        const auto seq=*reinterpret_cast<const mstudioseqdesc_t*>(bytes.data()+h.seqindex);
        Put(bytes,seq.eventindex+sizeof(mstudioevent_t)+offsetof(mstudioevent_t,event),0);
        AddModel(model,"models/timing.mdl","models/timing.mdl",bytes);
        if(disableDuringGeneric)
        {
            hooks.m_PF_precache_generic_I.present=true;
            genericCallback=[] {sv_auto_precache_sounds_in_models.value=0;};
        }
        PrecacheModelSpecifiedFiles();
        Require(Contains("sound/fixture.wav")==!disableDuringGeneric,"sound cvar was captured before generic callbacks");
        Require(loads==(disableDuringGeneric?1u:0u),"generic hook failed to gate temporary route");
    }
    Reset(); model_t model{};
    AddModel(model,"models/unsupported.mdl","models/unsupported.mdl",UnsupportedTrack());
    PrecacheModelSpecifiedFiles();
    Require(loads==0 && cacheAllocations==0 && memory.empty(),"unselected unsupported track was not retained under actual policy");
}
void FailuresAndFreshness()
{
    for(unsigned mode=0;mode<7;++mode)
    {
        Reset(); model_t model{}; auto bytes=Plain();
        if(mode==1)bytes.resize(12);
        if(mode==2)Put(bytes,4,99);
        if(mode==3)Put(bytes,180,-1);
        if(mode==4){bytes=Tagged(); Put(bytes,Word(bytes,168)-32,0x51455343u); Put(bytes,Word(bytes,168)-24,99);}
        if(mode==6)Put(bytes,0,0);
        AddModel(model,"models/bad.mdl","models/bad.mdl",bytes);
        if(mode==0)files.clear();
        if(mode==5)files["models/bad1.seq"]={std::byte{0}};
        Rejected([]{PrecacheModelSpecifiedFiles();},"invalid current file falsely advertised successfully");
        Require(advertisements.empty(),"failed file emitted partial resources");
        Require(memory.empty(),"failed file leaked raw input");
    }
    Reset(); Resources(); model_t model{}; AddModel(model,"models/fresh.mdl","models/fresh.mdl",Plain());
    PrecacheModelSpecifiedFiles(); const auto old=advertisements; advertisements.clear();
    auto& bytes=files[model.name]; auto* h=reinterpret_cast<studiohdr_t*>(bytes.data());
    auto* seq=reinterpret_cast<mstudioseqdesc_t*>(bytes.data()+h->seqindex);
    auto* event=reinterpret_cast<mstudioevent_t*>(bytes.data()+seq->eventindex); std::strcpy(event->options,"updated.wav");
    PrecacheModelSpecifiedFiles();
    Require(Contains("sound/updated.wav") && advertisements!=old,"second pass reused stale metadata");
    Require(cacheAllocations==0 && loads==0,"fresh temporary reads populated cache");
    Require(model.flags==0 && model.needload==0,"temporary metadata read published full loader state");
}
void Run()
{
    static_assert(sizeof(void*)==4 && sizeof(studiohdr_t)==244);
    ComparePasses(); Gates(); Incompatible(); TimingAndUnsupported(); FailuresAndFreshness(); Reset();
}
}

qboolean g_bIsDedicatedServer=TRUE;
unsigned char texgammatable[256]{};
modfuncs_t g_modfuncs{};
server_t g_psv{};
GoldCraftPrecache gc_precache;
cvar_t sv_auto_precache_sounds_in_models={"sv_auto_precache_sounds_in_models","1",0,1,nullptr};
int IdentityLong(int value){return value;} int(*LittleLong)(int)=IdentityLong;
void* Cache_Alloc(cache_user_t* owner,int bytes,char*)
{
    using namespace resource_test; ++cacheAllocations; GoldCraftStudioAllocating(owner,bytes);
    auto block=std::make_unique<Guarded>(std::size_t(bytes)); owner->data=block->data;
    caches.insert_or_assign(owner,std::move(block)); return owner->data;
}
void* Cache_Check(cache_user_t* owner){++resource_test::cacheChecks; return owner->data;}
unsigned char* COM_LoadFileForMe(const char* name,int* length)
{
    using namespace resource_test; reads.emplace_back(name); const auto found=files.find(name);
    if(found==files.end()){*length=0; return nullptr;}
    *length=int(found->second.size()); auto block=std::make_unique<Guarded>(*length);
    auto* data=reinterpret_cast<unsigned char*>(block->data); std::memcpy(data,found->second.data(),*length);
    memory.emplace(data,std::move(block)); ++fileAllocations; return data;
}
void Mem_Free(void* data)
{
    using namespace resource_test; auto found=memory.find(data); Require(found!=memory.end(),"wrong raw file release");
    found->second->Check(); memory.erase(found); ++fileFrees;
}
model_t* Mod_LoadModel(model_t* model,qboolean crash,qboolean trackCRC)
{
    using namespace resource_test; ++loads; Require(crash==1 && trackCRC==0,"second pass load flags changed");
    int length{}; auto* raw=COM_LoadFileForMe(model->name,&length);
    if(!raw)Sys_Error("fixture missing model %s",model->name);
    try
    {
        auto bytes=std::span<std::byte>(reinterpret_cast<std::byte*>(raw),std::size_t(length));
        auto expanded=GoldCraftDecodeStudio(bytes,model->name);
        GoldCraftLoadStudioOwned(model,expanded.empty()?bytes:std::span<std::byte>(expanded));
        if(g_modfuncs.m_pfnModelLoad)g_modfuncs.m_pfnModelLoad(model,raw);
    }
    catch(...){Mem_Free(raw); throw;}
    Mem_Free(raw); return model;
}
void NORETURN Sys_Error(const char* format,...)
{va_list args; va_start(args,format); adapter_test::HostFailure(format,args);}
extern "C" void* A_memset(void* out,int value,size_t bytes){return std::memset(out,value,bytes);}
extern "C" void* A_memcpy(void* out,const void* in,size_t bytes){return std::memcpy(out,in,bytes);}
extern "C" char* A_strcpy(char* out,const char* in){return std::strcpy(out,in);}
extern "C" size_t A_strlen(const char* text){return std::strlen(text);}
extern "C" int A_strcmp(const char* a,const char* b){return std::strcmp(a,b);}
extern "C" int A_stricmp(const char* a,const char* b){return _stricmp(a,b);}
CreateInterfaceFn g_FileSystemFactory{}; IFileSystem* g_pFileSystem{};
int FS_FileExists(const char* name){return resource_test::files.contains(name);}
FileHandle_t FS_Open(const char* name,const char*)
{
    auto found=resource_test::files.find(name); if(found==resource_test::files.end())return nullptr;
    resource_test::reads.emplace_back(name); return reinterpret_cast<FileHandle_t>(&found->second);
}
void FS_Close(FileHandle_t){}
unsigned int FS_Size(FileHandle_t file){return unsigned(reinterpret_cast<adapter_test::Bytes*>(file)->size());}
int FS_Read(void* out,int size,int count,FileHandle_t file)
{
    auto& bytes=*reinterpret_cast<adapter_test::Bytes*>(file); auto n=std::min(bytes.size(),std::size_t(size)*count);
    std::memcpy(out,bytes.data(),n); return int(n);
}
int PF_precache_generic_I(const char* name)
{
    resource_test::advertisements.emplace_back(name);
    if(resource_test::genericCallback)resource_test::genericCallback();
    return int(resource_test::advertisements.size());
}
void ForwardSlashes(char* name){for(;*name;++name)if(*name=='\\')*name='/';}
char* va(char* format,...){static char buffer[1024]; va_list args; va_start(args,format); std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args); return buffer;}
void Con_Printf(const char*,...){} void Con_DPrintf(const char*,...){}
void Log_Printf(const char*,...){}
double Sys_FloatTime(){return 0.;}
InterfaceReg* InterfaceReg::s_pInterfaceRegs{};
InterfaceReg::InterfaceReg(InstantiateInterfaceFn factory,const char* name)
    :m_CreateFn(factory),m_pName(name),m_pNext(s_pInterfaceRegs){s_pInterfaceRegs=this;}
int main()
{
    try{resource_test::Run(); std::cout<<"resource pass SDK: "<<adapter_test::checks<<" checks passed; no live game evidence\n"; return 0;}
    catch(const std::exception& error){std::cerr<<"resource pass failure at "<<adapter_test::checks<<": "<<error.what()<<'\n'; return 1;}
}
