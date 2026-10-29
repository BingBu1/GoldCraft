// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the actual server adapter; model allocation/copy is the host boundary.
#include "../engine/csol_sequence_guard.cpp"
#include "sequence_adapter_fixture.hpp"
#include <functional>
#include <map>

namespace adapter_test
{
std::map<cache_user_t*,Bytes> allocations;
bool skipReceipt{},doubleAllocation{},wrongAllocation{},corruptCopy{};
unsigned loaderCalls{};
std::function<void()> duringLoad;
studiohdr_t* Header(model_t& model){return static_cast<studiohdr_t*>(model.cache.data);}
mstudioseqdesc_t* Descriptor(model_t& model,unsigned index=0)
{auto* h=Header(model);return reinterpret_cast<mstudioseqdesc_t*>(reinterpret_cast<std::byte*>(h)+h->seqindex)+index;}
mstudioanim_t* Animation(model_t& model,unsigned index=0,unsigned offset=0)
{return reinterpret_cast<mstudioanim_t*>(reinterpret_cast<std::byte*>(Header(model))+Descriptor(model,index)->animindex+offset);}
void Load(model_t& model,Bytes bytes)
{model.type=mod_studio;std::strcpy(model.name,"models/engine-adapter.mdl");GoldCraftLoadStudioOwned(&model,bytes);}
void Run()
{
    model_t tagged{},ordinary{},textured{},inner{};auto packed=Model();
    Load(tagged,packed);Load(ordinary,Model(false));Load(textured,Model(true,true));
    Require(GoldCraftStudioTagged(&tagged,Header(tagged)),"tagged engine model classified ordinary");
    Require(!GoldCraftStudioTagged(&ordinary,Header(ordinary)),"ordinary engine model classified tagged");
    Require(GoldCraftStudioTagged(&textured,Header(textured)),"texture repack allocation receipt rejected");
    Require(allocations[&textured.cache].size()==Model(true,true).size()+1280,"server texture allocation formula differs");
    GoldCraftStudioSelected(&tagged,Header(tagged),Descriptor(tagged));
    for(unsigned index:{0u,1u})for(unsigned blend:{0u,24u})
        GoldCraftStudioSample(&tagged,Header(tagged),Descriptor(tagged,index),Animation(tagged,index,blend),1,.5f,true);
    GoldCraftStudioSample(&tagged,Header(tagged),Descriptor(tagged),Animation(tagged,0,12),0,-.005f,false);
    Rejected([&]{GoldCraftStudioSample(&tagged,Header(tagged),Descriptor(tagged),Animation(tagged,0,12),0,0,true);},"whole blend accepted bone offset");
    Rejected([&]{GoldCraftStudioSample(&tagged,Header(tagged),Descriptor(tagged),Animation(tagged),2,.01f,true);},"last-frame positive interpolation accepted");
    Rejected([&]{GoldCraftStudioSample(&tagged,Header(tagged),Descriptor(tagged),Animation(tagged),std::numeric_limits<float>::infinity(),0,true);},"nonfinite server frame accepted");
    GoldCraftStudioSample(&ordinary,Header(ordinary),nullptr,nullptr,std::numeric_limits<float>::infinity(),0,true);
    Require(true,"ordinary sampler pass-through");
    auto* desc=Descriptor(tagged);const auto anim=desc->animindex;desc->animindex+=12;
    Rejected([&]{GoldCraftStudioSelected(&tagged,Header(tagged),desc);},"mutated descriptor selected");desc->animindex=anim;
    auto moved=allocations[&tagged.cache];auto* stale=Header(tagged);tagged.cache.data=moved.data();
    Rejected([&]{GoldCraftStudioSelected(&tagged,stale,desc);},"stale cache relocation header accepted");
    GoldCraftStudioSample(&tagged,Header(tagged),Descriptor(tagged),Animation(tagged),1,0,true);
    tagged.cache.data=allocations[&tagged.cache].data();
    duringLoad=[&]{Load(inner,Model());};Load(tagged,packed);
    Require(GoldCraftStudioTagged(&tagged,Header(tagged)) && GoldCraftStudioTagged(&inner,Header(inner)),"nested engine loads lost owner");
    for(int mode=0;mode<4;++mode)
    {
        skipReceipt=mode==0;doubleAllocation=mode==1;wrongAllocation=mode==2;corruptCopy=mode==3;
        Rejected([&]{Load(tagged,packed);},"bad allocation/copy receipt accepted");
        Rejected([&]{GoldCraftStudioTagged(&tagged,Header(tagged));},"failed load left previous proof live");
        skipReceipt=doubleAllocation=wrongAllocation=corruptCopy=false;Load(tagged,packed);
    }
    GoldCraftStudioAllocating(&tagged.cache,int(allocations[&tagged.cache].size()));
    Rejected([&]{GoldCraftStudioTagged(&tagged,Header(tagged));},"replacement allocation retained proof");Load(tagged,packed);
    // Exercise the actual EXPOSE_SINGLE_INTERFACE-generated service factory.
    auto* entry=InterfaceReg::s_pInterfaceRegs;
    while(entry && std::strcmp(entry->m_pName,csol::SequenceServiceVersion))entry=entry->m_pNext;
    Require(entry!=nullptr,"sequence service was not registered");
    auto* service=static_cast<csol::ISequenceService*>(entry->m_CreateFn());
    Require(service->Tagged(&tagged,Header(tagged)),"service Tagged not wired");
    service->Select(&tagged,Header(tagged),Descriptor(tagged));
    service->Sample(&tagged,Header(tagged),Descriptor(tagged),Animation(tagged),1,0,true);
    Rejected([&]{service->Sample(&tagged,Header(tagged),Descriptor(tagged),Animation(tagged),-1,0,true);},"service failed to reject bad frame");
    GoldCraftCheckStudioBounds(packed);GoldCraftCheckStudioBounds(Model(false));
    auto bad=packed;Put(bad,72,0x7fffffffu);Rejected([&]{GoldCraftCheckStudioBounds(bad);},"bounds trusted header length");
    auto partial=UnsupportedTrack();Load(tagged,partial);
    Require(service->Tagged(&tagged,Header(tagged)),"explicit partial-load policy not applied");
    service->Sample(&tagged,Header(tagged),Descriptor(tagged,0),Animation(tagged,0),0,0,true);
    Rejected([&]{service->Select(&tagged,Header(tagged),Descriptor(tagged,1));},"unsupported service selection accepted");
    Rejected([&]{service->Sample(&tagged,Header(tagged),Descriptor(tagged,1),Animation(tagged,1),0,0,true);},"unsupported service sample accepted");
    Rejected([&]{GoldCraftCheckStudioBounds(partial);},"engine bounds silently allowed unsupported track");
    GoldCraftStudioMemoryReset();Rejected([&]{service->Tagged(&tagged,Header(tagged));},"reset retained service owner");
    Require(loaderCalls>=12,"actual server loader adapter was not exercised");
}
}

void Sys_Error(const char* format,...)
{va_list args;va_start(args,format);adapter_test::HostFailure(format,args);}
void Mod_LoadStudioModel(model_t* model,void* buffer)
{
    using namespace adapter_test;++loaderCalls;auto* h=static_cast<studiohdr_t*>(buffer);
    const std::size_t size=std::size_t(h->length)+1280*std::size_t(h->numtextures);
    if(!skipReceipt)GoldCraftStudioAllocating(&model->cache,int(size)+(wrongAllocation?4:0));
    if(doubleAllocation)GoldCraftStudioAllocating(&model->cache,int(size));
    auto& allocated=allocations[&model->cache];allocated.resize(size);std::memcpy(allocated.data(),buffer,h->length);
    model->cache.data=allocated.data();if(corruptCopy)reinterpret_cast<studiohdr_t*>(allocated.data())->numseq++;
    if(duringLoad){auto work=std::move(duringLoad);work();}
}
// The factory-list host boundary; the production registration macro and
// SequenceService implementation above are exercised without loading a DLL.
InterfaceReg* InterfaceReg::s_pInterfaceRegs{};
InterfaceReg::InterfaceReg(InstantiateInterfaceFn factory,const char* name)
    :m_CreateFn(factory),m_pName(name),m_pNext(s_pInterfaceRegs){s_pInterfaceRegs=this;}
int main()
{
    try{static_assert(sizeof(void*)==4);adapter_test::Run();std::cout<<"engine adapter: "<<adapter_test::checks<<" assertions passed; real production TU, host boundary stubs, no game hooks\n";return 0;}
    catch(const std::exception& e){std::cerr<<"engine adapter failed: "<<e.what()<<'\n';return 1;}
}
