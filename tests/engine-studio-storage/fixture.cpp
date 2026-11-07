// SPDX-License-Identifier: GPL-3.0-or-later
// Production loader/guard compile directly from the selected working tree.
// Cache allocation and public hook dispatch are explicit host boundaries.
#include "precompiled.h"
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <vector>
#include "sequence_adapter_fixture.hpp"

#include "storage-under-test.inc"
namespace single_plan_test { inline unsigned plans{}; }
namespace csol
{
StudioStorage CountedPlanStudioStorage(std::span<const std::byte> bytes,bool dedicated)
{ ++single_plan_test::plans; return PlanStudioStorage(bytes,dedicated); }
}

namespace loader_test
{
using namespace adapter_test;
struct Guarded
{
    void* reserve{};
    std::byte* data{};
    std::size_t bytes{};
    explicit Guarded(std::size_t size):bytes(size)
    {
        SYSTEM_INFO info{};GetSystemInfo(&info);
        const std::size_t page=info.dwPageSize,committed=((size+64+page-1)/page)*page;
        reserve=VirtualAlloc(nullptr,committed+page,MEM_RESERVE,PAGE_NOACCESS);
        if(!reserve || !VirtualAlloc(reserve,committed,MEM_COMMIT,PAGE_READWRITE))throw std::bad_alloc();
        data=static_cast<std::byte*>(reserve)+committed-size;
        std::memset(data-64,0xAC,64);std::memset(data,0xDA,size);
    }
    ~Guarded(){if(reserve)VirtualFree(reserve,0,MEM_RELEASE);}
    Guarded(const Guarded&)=delete;
    void Check() const{for(unsigned i=1;i<=64;++i)Require(data[-int(i)]==std::byte{0xAC},"cache underrun");}
};
std::map<cache_user_t*,std::unique_ptr<Guarded>> allocations;
bool skipReceipt{},doubleReceipt{},wrongReceipt{},corruptCopy{};
unsigned calls{};
std::function<void()> duringLoad, duringAllocation;
std::function<void(model_t*,void*)> beforeLoad;
bool hookPresent=true, skipOriginal=false;
model_t* redirectModel{};
struct HookRegistry
{
    bool isEmpty() const { return !hookPresent; }
    void callChain(void(*callback)(model_t*,void*),model_t* model,void* bytes)
    {
        ++calls;
        if(beforeLoad)beforeLoad(model,bytes);
        if(!skipOriginal)callback(redirectModel?redirectModel:model,bytes);
        if(corruptCopy)static_cast<studiohdr_t*>(model->cache.data)->numseq++;
        if(duringLoad){auto work=std::move(duringLoad);work();}
    }
};
struct HookBoundary{HookRegistry m_Mod_LoadStudioModel;} hooks;
}

// The actual public wrapper and internal loader body run unchanged. Only the
// external registry object is supplied by this controlled host boundary.
#define PlanStudioStorage CountedPlanStudioStorage
#define g_RehldsHookchains loader_test::hooks
#include "loader-under-test.inc"
#include "guard-under-test.inc"
#undef g_RehldsHookchains
#undef PlanStudioStorage
#include "sequences-under-test.inc"
#ifdef CHECK_DEPENDENCIES
#include "dependency-loader-under-test.inc"
#include "advertisers-under-test.inc"
#endif

namespace loader_test
{
template<class T> unsigned Add(Bytes& bytes,const T& value={})
{const auto at=unsigned((bytes.size()+alignof(T)-1)/alignof(T)*alignof(T));bytes.resize(at+sizeof(T));Put(bytes,at,value);return at;}

Bytes Ordinary(bool textures=true)
{
    Bytes bytes(sizeof(studiohdr_t));studiohdr_t h{};
    h.id=0x54534449;h.version=10;h.flags=0x1234;
    std::strcpy(h.name,"fixture-complete-model");
    mstudiobone_t bone{};std::strcpy(bone.name,"root");bone.parent=-1;
    for(int& index:bone.bonecontroller)index=-1;
    for(float& scale:bone.scale)scale=.01f;
    h.numbones=1;h.boneindex=Add(bytes,bone);
    mstudiobonecontroller_t controller{};controller.bone=0;controller.type=STUDIO_X;
    h.numbonecontrollers=1;h.bonecontrollerindex=Add(bytes,controller);
    mstudiobbox_t box{};box.bone=0;box.group=1;box.bbmin[0]=-1;box.bbmax[0]=1;
    h.numhitboxes=1;h.hitboxindex=Add(bytes,box);
    h.numseq=2;h.seqindex=Add(bytes,mstudioseqdesc_t{});Add(bytes,mstudioseqdesc_t{});
    h.numseqgroups=2;h.seqgroupindex=Add(bytes,mstudioseqgroup_t{});
    mstudioseqgroup_t group{};std::strcpy(group.name,"models/fixture01.mdl");Add(bytes,group);
    mstudioattachment_t attachment{};attachment.bone=0;attachment.org[0]=123;
    h.numattachments=1;h.attachmentindex=Add(bytes,attachment);
    mstudiobodyparts_t body{};body.base=1;body.nummodels=1;
    h.numbodyparts=1;h.bodypartindex=Add(bytes,body);
    mstudiomodel_t geometry{};geometry.numverts=1;geometry.numnorms=1;geometry.nummesh=1;
    const auto modelAt=Add(bytes,geometry);body.modelindex=modelAt;Put(bytes,h.bodypartindex,body);
    geometry.vertinfoindex=Add(bytes,std::uint8_t{0});
    geometry.vertindex=unsigned(bytes.size());for(float value:{1.f,2.f,3.f})Add(bytes,value);
    geometry.norminfoindex=Add(bytes,std::uint8_t{0});
    geometry.normindex=unsigned(bytes.size());for(float value:{0.f,0.f,1.f})Add(bytes,value);
    geometry.meshindex=Add(bytes,mstudiomesh_t{});Put(bytes,modelAt,geometry);
    mstudioevent_t event{};event.event=5004;std::strcpy(event.options,"fixture.wav");
    const auto events=Add(bytes,event);event.event=5008;std::strcpy(event.options,"1");Add(bytes,event);
    h.numtransitions=2;h.transitionindex=unsigned(bytes.size());for(unsigned char v:{1,2,2,1})Add(bytes,v);
    h.numskinref=2;h.numskinfamilies=1;h.skinindex=Add(bytes,std::uint16_t{0});Add(bytes,std::uint16_t{1});
    mstudioanim_t animation{};animation.offset[0]=12;
    const auto animationAt=Add(bytes,animation);
    for(unsigned char value:{1,2,77,0,1,1,80,0})Add(bytes,value);
    mstudioseqdesc_t seq{};std::strcpy(seq.label,"inline");seq.fps=20;seq.numframes=2;
    seq.numblends=1;seq.animindex=animationAt;seq.numevents=2;seq.eventindex=events;
    Put(bytes,h.seqindex,seq);
    std::strcpy(seq.label,"external");seq.seqgroup=1;seq.animindex=0x7ffff0;
    Put(bytes,h.seqindex+sizeof(seq),seq);
    if(textures)
    {
        h.numtextures=2;h.textureindex=Add(bytes,mstudiotexture_t{});Add(bytes,mstudiotexture_t{});
        h.texturedataindex=int(bytes.size());
        for(int i=0;i<2;++i)
        {
            mstudiotexture_t texture{};
            std::strcpy(texture.name,i?"#second":"CSO_HighQ_fixture");
            texture.width=2;texture.height=2;texture.index=int(bytes.size());
            Put(bytes,h.textureindex+i*sizeof(texture),texture);
            for(unsigned value=0;value<4;++value)Add(bytes,std::uint8_t(value+17*i));
            for(unsigned value=0;value<768;++value)Add(bytes,std::uint8_t(value+13*i));
        }
    }
    h.length=int(bytes.size());Put(bytes,0,h);return bytes;
}

Bytes Tagged()
{
    auto bytes=Model(true,true);const auto retained=Retained(bytes),texture=Word(bytes,184);
    bytes.resize(retained+4+768);Put(bytes,72,unsigned(bytes.size()));
    Put(bytes,texture+68,2);Put(bytes,texture+72,2);
    for(unsigned i=0;i<4+768;++i)bytes[retained+i]=std::byte(i%256);
    return bytes;
}
Bytes Appended()
{
    auto base=Tagged();
    const auto descriptors=Word(base,168);
    // Remove only the local tag so AppendSequences executes the real producer.
    Put(base,descriptors-32,0u);Put(base,descriptors-28,0u);
    Bytes seq(8);mstudioevent_t event{};event.event=5004;std::strcpy(event.options,"appended.wav");
    Add(seq,event);Add(seq,mstudiopivot_t{});
    const auto animation=Word(base,descriptors+124);
    seq.insert(seq.end(),base.begin()+animation,base.begin()+animation+56);
    auto descriptor=*reinterpret_cast<const mstudioseqdesc_t*>(base.data()+descriptors);
    descriptor.numevents=1;descriptor.numpivots=1;descriptor.animindex=1000;
    std::strcpy(descriptor.label,"appended-seq");
    const auto trailer=Add(seq,descriptor);Put(seq,0,1);Put(seq,4,trailer);
    return csol::AppendSequences(base,{seq});
}
studiohdr_t* Header(model_t& model){return static_cast<studiohdr_t*>(model.cache.data);}
mstudioseqdesc_t* Descriptor(model_t& model,unsigned i=0)
{return reinterpret_cast<mstudioseqdesc_t*>(static_cast<std::byte*>(model.cache.data)+Header(model)->seqindex)+i;}
void Load(model_t& model,const Bytes& bytes)
{
    Guarded source(bytes.size());std::memcpy(source.data,bytes.data(),bytes.size());
    std::strcpy(model.name,"models/loader-fixture.mdl");
    GoldCraftLoadStudioOwned(&model,{source.data,bytes.size()});
    source.Check();allocations.at(&model.cache)->Check();
}
void ExpectedPalette(model_t& model,const Bytes& source)
{
    const auto* input=reinterpret_cast<const studiohdr_t*>(source.data());
    const auto* textures=reinterpret_cast<const mstudiotexture_t*>(source.data()+input->textureindex);
    auto expected=source;expected.resize(source.size()+1280*input->numtextures);
    std::size_t from=input->texturedataindex,to=from;
    for(int i=0;i<input->numtextures;++i)
    {
        Put(expected,input->textureindex+i*sizeof(mstudiotexture_t)+offsetof(mstudiotexture_t,index),int(to));
        const auto count=textures[i].width*textures[i].height;
        for(int p=0;p<count;++p)expected[to++]=source[from++];
        for(int color=0;color<256;++color)
        {
            for(int channel=0;channel<3;++channel)
            {const auto value=std::uint16_t(texgammatable[std::to_integer<unsigned>(source[from++])]);Put(expected,to,value);to+=2;}
            Put(expected,to,std::uint16_t{});to+=2;
        }
    }
    Require(allocations.at(&model.cache)->bytes==expected.size(),"non-dedicated allocation changed");
    Require(std::memcmp(model.cache.data,expected.data(),expected.size())==0,"non-dedicated converted bytes changed");
}
void StorageBoundaries()
{
    const auto valid=Ordinary();
    const auto header=*reinterpret_cast<const studiohdr_t*>(valid.data());
    Require(csol::PlanStudioStorage(valid,true).prefix,"valid complete prefix ineligible");
    Require(!csol::PlanStudioStorage(valid,false).prefix,"non-dedicated incorrectly shrunk");
    const auto badArray=[&](std::size_t countAt,std::size_t offsetAt)
    {
        for(int offset:{-1,header.texturedataindex,INT_MAX})
        {
            auto bytes=valid;Put(bytes,countAt,1);Put(bytes,offsetAt,offset);
            const auto result=csol::PlanStudioStorage(bytes,true);
            Require(!result.prefix && result.bytes==header.length+2560,"out-of-prefix array shrunk");
        }
        auto bytes=valid;Put(bytes,countAt,-1);
        Require(!csol::PlanStudioStorage(bytes,true).prefix,"negative metadata count shrunk");
        bytes=valid;Put(bytes,countAt,INT_MAX);
        Require(!csol::PlanStudioStorage(bytes,true).prefix,"metadata multiplication overflow shrunk");
    };
    badArray(offsetof(studiohdr_t,numbones),offsetof(studiohdr_t,boneindex));
    badArray(offsetof(studiohdr_t,numbonecontrollers),offsetof(studiohdr_t,bonecontrollerindex));
    badArray(offsetof(studiohdr_t,numhitboxes),offsetof(studiohdr_t,hitboxindex));
    badArray(offsetof(studiohdr_t,numseq),offsetof(studiohdr_t,seqindex));
    badArray(offsetof(studiohdr_t,numseqgroups),offsetof(studiohdr_t,seqgroupindex));
    badArray(offsetof(studiohdr_t,numattachments),offsetof(studiohdr_t,attachmentindex));
    badArray(offsetof(studiohdr_t,numbodyparts),offsetof(studiohdr_t,bodypartindex));
    badArray(offsetof(studiohdr_t,numtransitions),offsetof(studiohdr_t,transitionindex));
    badArray(offsetof(studiohdr_t,numskinref),offsetof(studiohdr_t,skinindex));
    const auto seq=std::size_t(header.seqindex);
    badArray(seq+offsetof(mstudioseqdesc_t,numevents),seq+offsetof(mstudioseqdesc_t,eventindex));
    badArray(seq+offsetof(mstudioseqdesc_t,numpivots),seq+offsetof(mstudioseqdesc_t,pivotindex));
    for(int group:{-1,2,MAXSTUDIOGROUPS})
    {auto bytes=valid;Put(bytes,seq+offsetof(mstudioseqdesc_t,seqgroup),group);Require(!csol::PlanStudioStorage(bytes,true).prefix,"invalid external group shrunk");}
    for(int at:{0,header.texturedataindex,INT_MAX})
    {auto bytes=valid;Put(bytes,seq+offsetof(mstudioseqdesc_t,animindex),at);Require(!csol::PlanStudioStorage(bytes,true).prefix,"invalid local animation shrunk");}
    auto noTextureRows=valid;Put(noTextureRows,180,0);
    Require(csol::PlanStudioStorage(noTextureRows,true).prefix,"zero texture rows with table offset changed prefix contract");
    // Exercise one compatible old path all the way through real allocation,
    // palette conversion and owner receipt. This does not bless its metadata
    // for later gameplay consumption; it verifies fallback parity only.
    auto fallback=valid;Put(fallback,offsetof(studiohdr_t,hitboxindex),header.texturedataindex);
    model_t model{};g_bIsDedicatedServer=TRUE;Load(model,fallback);ExpectedPalette(model,fallback);
    auto zero=valid;Put(zero,180,0);Load(model,zero);
    Require(allocations.at(&model.cache)->bytes==Retained(zero),"zero texture row prefix allocation failed");
    Require(std::memcmp(model.cache.data,zero.data(),Retained(zero))==0,"zero texture row prefix changed");
}
#ifdef CHECK_DEPENDENCIES
std::map<std::string,Bytes> files;
std::vector<std::string> advertised;
std::vector<std::string> fileReads;
std::vector<std::string> Advertise(const char* name,model_t& model)
{
    advertised.clear();
    auto* h=Header(model);
    PrecacheModelTexture(name,h);PrecacheModelSeqGroups(h);
    GoldCraftPrecacheCsolResources(name,h);PrecacheModelSounds(h);
    return advertised;
}
void Dependencies()
{
    auto embedded=Ordinary();const auto external=Ordinary(false);
    auto* head=reinterpret_cast<studiohdr_t*>(embedded.data());
    for(int i=0;i<2;++i)
    {auto* texture=reinterpret_cast<mstudiotexture_t*>(embedded.data()+head->textureindex)+i;std::memset(texture->name,0,64);std::strcpy(texture->name,i?"tex1.bmp":"tex0.bmp");}
    files["models/fixture.mdl"]=embedded;files["models/external.mdl"]=external;
    files["models/externalT.mdl"]=embedded;
    for(const auto* name:{"sound/fixture.wav","sound/zombi/oberon/knife_walk1.wav"})files[name]=Bytes{std::byte{1}};
    std::vector<std::vector<std::string>> baseline;
    for(bool dedicated:{false,true})
    {
        g_bIsDedicatedServer=dedicated;
        dependencyCache=goldcraft::csol_resources::DependencyCache{};
        GoldCraftBeginCsolResources();fileReads.clear();
        unsigned index{};
        for(const auto* name:{"models/fixture.mdl","models/external.mdl"})
        {
            model_t model{};Load(model,files.at(name));
            auto result=Advertise(name,model);
            Require(!result.empty(),"actual advertisement functions produced no paths");
            Require(std::find(result.begin(),result.end(),"models/fixture01.mdl")!=result.end(),"external sequence file not advertised");
            Require(std::find(result.begin(),result.end(),"sound/fixture.wav")!=result.end(),"5004 sound not advertised");
            const auto footstep=std::find(result.begin(),result.end(),"sound/zombi/oberon/knife_walk1.wav");
            Require((footstep!=result.end())==(index==0),"5008 advertisement or batch deduplication changed");
            if(!dedicated)baseline.push_back(result);
            else Require(result==baseline[index],"dedicated prefix changed ordered advertisement results");
            if(index==1)Require(result.front()=="models/externalT.mdl","external T advertisement changed");
            ++index;
        }
        Require(std::find(fileReads.begin(),fileReads.end(),"models/fixture.mdl")!=fileReads.end(),"dependency closure did not read original model");
        Require(std::find(fileReads.begin(),fileReads.end(),"models/externalT.mdl")!=fileReads.end(),"dependency closure did not read original T model");
        GoldCraftEndCsolResources();
    }
}
#endif

void SinglePlanPaths()
{
    using single_plan_test::plans;
    const auto plain=Ordinary(),safe=Tagged(),none=Ordinary(false),appended=Appended();
    const auto captured=[](model_t& model)
    {const auto* p=static_cast<const std::byte*>(model.cache.data);return Bytes(p,p+allocations.at(&model.cache)->bytes);};
    for(bool dedicated:{false,true})for(const auto* source:{&plain,&safe,&none,&appended})
    {
        model_t hooked{},direct{};g_bIsDedicatedServer=dedicated;
        hookPresent=true;plans=0;Load(hooked,*source);
        Require(plans==2,"hooked load must independently plan twice");
        const auto baseline=captured(hooked);
        const auto beforeCalls=calls;hookPresent=false;plans=0;Load(direct,*source);
        Require(plans==1,"unhooked load must plan exactly once");
        Require(calls==beforeCalls,"empty hook registry must not dispatch");
        Require(captured(direct)==baseline,"single-plan output differs from hooked legacy path");
        Require(direct.type==hooked.type && direct.flags==hooked.flags,"single-plan model identity differs");
        Require(GoldCraftStudioTagged(&direct,Header(direct))==GoldCraftStudioTagged(&hooked,Header(hooked)),"single-plan owner classification differs");
    }
    g_bIsDedicatedServer=TRUE;model_t outer{},inner{};
    hookPresent=false;plans=0;
    duringAllocation=[&]{Load(inner,safe);};Load(outer,plain);
    Require(plans==2,"nested empty-chain load must have one plan per model");
    Require(!GoldCraftStudioTagged(&outer,Header(outer)) && GoldCraftStudioTagged(&inner,Header(inner)),"nested direct load lost distinct owner");
    Require(allocations.at(&outer.cache)->bytes==Retained(plain) && allocations.at(&inner.cache)->bytes==Retained(safe),"nested direct load mixed plans");
    hookPresent=true;plans=0;duringLoad=[&]{Load(inner,safe);};Load(outer,plain);
    Require(plans==4,"nested populated chain must retain independent plans");
    beforeLoad=[](model_t*,void* p){static_cast<studiohdr_t*>(p)->flags=0x4567;};plans=0;Load(outer,plain);
    Require(plans==2 && outer.flags==0x4567 && Header(outer)->flags==0x4567,"hook pre-original mutation was bypassed");
    beforeLoad=[](model_t*,void* p){++static_cast<studiohdr_t*>(p)->numseq;};
    Rejected([&]{Load(outer,plain);},"hook metadata mutation escaped receipt check");beforeLoad={};
    skipOriginal=true;Rejected([&]{Load(outer,plain);},"hook skip-original escaped receipt check");skipOriginal=false;
    redirectModel=&inner;Rejected([&]{Load(outer,plain);},"hook changed destination escaped receipt check");redirectModel=nullptr;
    for(bool hooked:{false,true})for(unsigned mode=0;mode<3;++mode)
    {
        hookPresent=hooked;skipReceipt=mode==0;doubleReceipt=mode==1;wrongReceipt=mode==2;
        Rejected([&]{Load(outer,safe);},"direct/hooked invalid allocation receipt accepted");
        Rejected([&]{GoldCraftStudioTagged(&outer,Header(outer));},"failed load retains an owner");
        skipReceipt=doubleReceipt=wrongReceipt=false;Load(outer,safe);
    }
    hookPresent=false;duringAllocation=[] {GoldCraftStudioMemoryReset();};
    Rejected([&]{Load(outer,safe);},"reset during direct load escaped token invalidation");
    Load(outer,safe);Require(GoldCraftStudioTagged(&outer,Header(outer)),"direct load did not recover after reset");
    auto invalid=safe;Put(invalid,4,9u);plans=0;
    Rejected([&]{Load(outer,invalid);},"direct path accepted invalid version");Require(plans==0,"invalid prepared model reached planning");
    for(bool publicEntry:{false,true})
    {
        model_t raw{};auto data=plain;plans=0;const auto oldCalls=calls;
        if(publicEntry)Mod_LoadStudioModel(&raw,data.data());else Mod_LoadStudioModel_internal(&raw,data.data());
        Require(plans==1 && calls==oldCalls+(publicEntry?1u:0u),"ordinary public/internal load dispatch changed");
        Require(captured(raw)==Bytes(plain.begin(),plain.begin()+Retained(plain)),"ordinary public/internal bytes changed");
        data=plain;Put(data,4,9u);plans=0;
        if(publicEntry)Mod_LoadStudioModel(&raw,data.data());else Mod_LoadStudioModel_internal(&raw,data.data());
        Require(plans==1 && Header(raw)->length==244 && !std::strcmp(Header(raw)->name,"bogus") && raw.flags==0 && raw.type==mod_studio,"ordinary version repair changed");
    }
    // Existing behavior sets model identity before a failing public plan.
    auto invalidStorage=plain;Put(invalidStorage,180,-1);model_t failed{};
    bool rejected=false;
    try{Mod_LoadStudioModel_internal(&failed,invalidStorage.data());}
    catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"invalid public storage accepted");
    Require(failed.type==mod_studio && failed.flags==0x1234,"public failing-plan identity ordering changed");
    hookPresent=true;GoldCraftStudioMemoryReset();
}

void Run()
{
    static_assert(sizeof(void*)==4 && sizeof(studiohdr_t)==244);
    static_assert(sizeof(mstudiobone_t)==112 && sizeof(mstudiobonecontroller_t)==24);
    static_assert(sizeof(mstudiobbox_t)==32 && sizeof(mstudioseqgroup_t)==104);
    static_assert(sizeof(mstudioattachment_t)==88 && sizeof(mstudiobodyparts_t)==76);
    static_assert(sizeof(mstudioseqdesc_t)==176 && sizeof(mstudiotexture_t)==80 && sizeof(mstudioevent_t)==76);
    for(unsigned i=0;i<256;++i)texgammatable[i]=static_cast<unsigned char>((i*7+3)%256);
    model_t ordinary{},tagged{},external{},inner{};
    const auto plain=Ordinary(),safe=Tagged(),noTextures=Ordinary(false);
    g_bIsDedicatedServer=TRUE;
    for(const auto* source:{&plain,&safe})
    {
        auto& model=source==&plain?ordinary:tagged;Load(model,*source);
        Require(allocations.at(&model.cache)->bytes==Retained(*source),"dedicated allocation must omit texture payload");
        Require(std::memcmp(model.cache.data,source->data(),Retained(*source))==0,"dedicated prefix bytes changed");
        Require(Header(model)->length==int(source->size()),"original header length changed");
        Require(model.type==mod_studio && model.flags==Header(model)->flags,"loader model identity changed");
    }
    Require(Descriptor(ordinary,1)->seqgroup==1 && Descriptor(ordinary,1)->animindex==0x7ffff0,"external group interpreted as main-file animation");
    Require(!GoldCraftStudioTagged(&ordinary,Header(ordinary)),"ordinary model misclassified");
    Require(GoldCraftStudioTagged(&tagged,Header(tagged)),"tagged model owner lost");
    auto* seq=Descriptor(tagged);
    auto* anim=reinterpret_cast<mstudioanim_t*>(static_cast<std::byte*>(tagged.cache.data)+seq->animindex);
    GoldCraftStudioSample(&tagged,Header(tagged),seq,anim,1,.5f,true);
    Rejected([&]{GoldCraftStudioSample(&tagged,Header(tagged),seq,anim,2,.5f,true);},"invalid sample accepted");
    const auto appended=Appended();Load(inner,appended);
    Require(Header(inner)->numseq==3 && GoldCraftStudioTagged(&inner,Header(inner)),"actual appended model lost track");
    Require(allocations.at(&inner.cache)->bytes==Retained(appended),"appended texture payload retained");
    Require(std::memcmp(inner.cache.data,appended.data(),Retained(appended))==0,"appended metadata changed");
    auto* extra=Descriptor(inner,2);
    const auto* extraEvent=reinterpret_cast<const mstudioevent_t*>(static_cast<const std::byte*>(inner.cache.data)+extra->eventindex);
    Require(extraEvent->event==5004 && !std::strcmp(extraEvent->options,"appended.wav"),"appended sound event lost");
    auto* extraAnimation=reinterpret_cast<mstudioanim_t*>(static_cast<std::byte*>(inner.cache.data)+extra->animindex);
    GoldCraftStudioSample(&inner,Header(inner),extra,extraAnimation,1,.5f,true);
    Load(external,noTextures);
    Require(allocations.at(&external.cache)->bytes==noTextures.size(),"external T model allocation changed");
    Require(std::memcmp(external.cache.data,noTextures.data(),noTextures.size())==0,"external T model bytes changed");
    g_bIsDedicatedServer=FALSE;Load(ordinary,plain);ExpectedPalette(ordinary,plain);Load(tagged,safe);ExpectedPalette(tagged,safe);
    Load(external,noTextures);Require(allocations.at(&external.cache)->bytes==noTextures.size(),"non-dedicated no-texture allocation changed");
    g_bIsDedicatedServer=TRUE;
    duringLoad=[&]{Load(inner,safe);};Load(tagged,safe);
    Require(GoldCraftStudioTagged(&inner,Header(inner)) && GoldCraftStudioTagged(&tagged,Header(tagged)),"nested ownership lost");
    for(unsigned mode=0;mode<4;++mode)
    {
        skipReceipt=mode==0;doubleReceipt=mode==1;wrongReceipt=mode==2;corruptCopy=mode==3;
        Rejected([&]{Load(tagged,safe);},"invalid allocation/copy receipt accepted");
        Rejected([&]{GoldCraftStudioTagged(&tagged,Header(tagged));},"failed load retains owner proof");
        skipReceipt=doubleReceipt=wrongReceipt=corruptCopy=false;Load(tagged,safe);
    }
    auto invalid=safe;Put(invalid,0,0u);Rejected([&]{Load(tagged,invalid);},"bad ID accepted");
    invalid=safe;Put(invalid,72,0x7fffffffu);Rejected([&]{Load(tagged,invalid);},"source length overflow accepted");
    invalid=safe;Put(invalid,180,-1);Rejected([&]{Load(tagged,invalid);},"negative texture count accepted");
    invalid=safe;Put(invalid,188,unsigned(invalid.size()+1));Rejected([&]{Load(tagged,invalid);},"retained bytes outside file accepted");
    Load(tagged,safe);GoldCraftStudioMemoryReset();
    Rejected([&]{GoldCraftStudioTagged(&tagged,Header(tagged));},"reset leaves owner live");
    Require(calls>=12,"public wrapper did not execute");
    StorageBoundaries();
    SinglePlanPaths();
#ifdef CHECK_DEPENDENCIES
    Dependencies();
#endif
}
}

qboolean g_bIsDedicatedServer=TRUE;
unsigned char texgammatable[256]{};
int IdentityLong(int value){return value;}
int(*LittleLong)(int)=IdentityLong;
void* Cache_Alloc(cache_user_t* owner,int bytes,char*)
{
    using namespace loader_test;
    Require(bytes>0,"nonpositive allocation request");
    if(!skipReceipt)GoldCraftStudioAllocating(owner,bytes+(wrongReceipt?4:0));
    if(doubleReceipt)GoldCraftStudioAllocating(owner,bytes);
    auto allocation=std::make_unique<Guarded>(std::size_t(bytes));owner->data=allocation->data;
    allocations.insert_or_assign(owner,std::move(allocation));
    if(duringAllocation){auto work=std::move(duringAllocation);work();}
    return owner->data;
}
void NORETURN Sys_Error(const char* format,...)
{va_list args;va_start(args,format);adapter_test::HostFailure(format,args);}
extern "C" void* A_memset(void* output,int value,size_t bytes){return std::memset(output,value,bytes);}
extern "C" void* A_memcpy(void* output,const void* input,size_t bytes){return std::memcpy(output,input,bytes);}
extern "C" char* A_strcpy(char* output,const char* input){return std::strcpy(output,input);}
#ifdef CHECK_DEPENDENCIES
CreateInterfaceFn g_FileSystemFactory{};
IFileSystem* g_pFileSystem{};
int FS_FileExists(const char* name){return loader_test::files.contains(name);}
FileHandle_t FS_Open(const char* name,const char*)
{auto found=loader_test::files.find(name);if(found==loader_test::files.end())return nullptr;loader_test::fileReads.emplace_back(name);return reinterpret_cast<FileHandle_t>(&found->second);}
void FS_Close(FileHandle_t){}
unsigned int FS_Size(FileHandle_t file){return unsigned(reinterpret_cast<adapter_test::Bytes*>(file)->size());}
int FS_Read(void* output,int size,int count,FileHandle_t file)
{auto& bytes=*reinterpret_cast<adapter_test::Bytes*>(file);const auto n=std::min(bytes.size(),std::size_t(size)*count);std::memcpy(output,bytes.data(),n);return int(n);}
int PF_precache_generic_I(const char* name){loader_test::advertised.emplace_back(name);return int(loader_test::advertised.size());}
void ForwardSlashes(char* name){for(;*name;++name)if(*name=='\\')*name='/';}
char* va(char* format,...){static char buffer[1024];va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);return buffer;}
void Con_Printf(const char*,...){}
void Con_DPrintf(const char*,...){}
extern "C" size_t A_strlen(const char* text){return std::strlen(text);}
extern "C" int A_strcmp(const char* left,const char* right){return std::strcmp(left,right);}
extern "C" int A_stricmp(const char* left,const char* right){return _stricmp(left,right);}
#endif
InterfaceReg* InterfaceReg::s_pInterfaceRegs{};
InterfaceReg::InterfaceReg(InstantiateInterfaceFn factory,const char* name)
    :m_CreateFn(factory),m_pName(name),m_pNext(s_pInterfaceRegs){s_pInterfaceRegs=this;}
int main()
{
    try{loader_test::Run();std::cout<<"actual Studio loader/guard: "<<adapter_test::checks<<" checks passed; guarded SDK boundaries; no live game evidence\n";return 0;}
    catch(const std::exception& error){std::cerr<<"actual Studio loader/guard failure after "<<adapter_test::checks<<": "<<error.what()<<'\n';return 1;}
}
