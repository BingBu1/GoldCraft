// SPDX-License-Identifier: GPL-3.0-or-later
// Include the real production TU so its private adapters execute unchanged.
// Only external host services below are substituted; no game DLL is loaded.
#include "client/sequence_guard.cpp"
#include "sequence_adapter_fixture.hpp"
#include <functional>
#include <map>

namespace adapter_test
{
struct Object{std::array<std::byte,128> data{};};
struct HookRecord{std::string name;void* address{};void* replacement{};bool removed{};};
std::vector<std::unique_ptr<HookRecord>> hooks;
std::vector<std::string> removed;
std::map<std::string,void*> symbols;
std::map<cache_user_t*,Bytes> allocations;
studiohdr_t* header{};model_t* model{};
cvar_t cache{};
bool negotiated{},nested{},throwSetup{};
Object* inner{};
std::function<void()> nestedWork;
unsigned clientCalls{},engineCalls{},serverCalls{},cacheHits{},readCalls{},boundsCalls{},originalBoundsCalls{};
float rawFrame{},receivedFrame{};
int selectedIndex{},receivedSequence{},receivedBone{};
const void* receivedEntity{};
const float* receivedAngles{};const float* receivedOrigin{};
const unsigned char* receivedController{};const unsigned char* receivedBlending{};
Bytes resource;const std::byte* delivered{};
void* Address(auto function){return reinterpret_cast<void*>(function);}
template<class Function> Function Installed(const char* name)
{
    for(auto it=hooks.rbegin();it!=hooks.rend();++it)
        if(!(*it)->removed && (*it)->name==name)return reinterpret_cast<Function>((*it)->replacement);
    throw std::runtime_error(std::string("missing installed host hook: ")+name);
}
[[noreturn]] void HostErrorFunction(const char* format,...)
{va_list args;va_start(args,format);HostFailure(format,args);}
void* HostAllocate(cache_user_t* owner,int size,const char*)
{auto& data=allocations[owner];data.assign(size,std::byte{});owner->data=data.data();return owner->data;}
void HostMemory(void*,int){}
cvar_t* HostCvar(const char* name){Require(std::string(name)=="r_studio_bone_caches","wrong bone-cache cvar");return &cache;}
model_t* ObjectModel(void* self){model_t* out;std::memcpy(&out,static_cast<std::byte*>(self)+0x34,4);return out;}
studiohdr_t* ObjectHeader(void* self){studiohdr_t* out;std::memcpy(&out,static_cast<std::byte*>(self)+0x44,4);return out;}
void Bind(Object& object,model_t& value)
{auto* p=&value;auto* h=static_cast<studiohdr_t*>(value.cache.data);std::memcpy(object.data.data()+0x34,&p,4);std::memcpy(object.data.data()+0x44,&h,4);}
mstudioseqdesc_t* Descriptor(studiohdr_t* h,int index=0)
{return reinterpret_cast<mstudioseqdesc_t*>(reinterpret_cast<std::byte*>(h)+h->seqindex)+index;}
mstudioanim_t* __fastcall HostClientAnim(void* self,int,model_t*,mstudioseqdesc_t* desc)
{return reinterpret_cast<mstudioanim_t*>(reinterpret_cast<std::byte*>(ObjectHeader(self))+desc->animindex);}
mstudioanim_t* HostEngineAnim(model_t*,mstudioseqdesc_t* desc)
{return reinterpret_cast<mstudioanim_t*>(reinterpret_cast<std::byte*>(header)+desc->animindex);}
void __fastcall HostClientCalc(void*,int,float (*)[3],float (*)[4],mstudioseqdesc_t*,mstudioanim_t*,float frame)
{++clientCalls;receivedFrame=frame;}
void HostEngineCalc(float (*)[3],float (*)[4],mstudioseqdesc_t*,mstudioanim_t*,float frame)
{++engineCalls;receivedFrame=frame;}
void __fastcall HostClientSetup(void* self,int)
{
    using namespace csol::client;
    if(cache.value!=0){++cacheHits;return;} // Simulate Renderer cache early return.
    Require(SamplingScope::current && SamplingScope::current->model==ObjectModel(self),"client outer scope missing");
    auto* before=SamplingScope::current;
    if(nested && inner){nested=false;ClientSetup(inner,0);Require(SamplingScope::current==before,"nested setup lost outer scope");Require(cache.value==0,"nested setup restored cache too early");}
    if(nestedWork){auto work=std::move(nestedWork);work();Require(SamplingScope::current==before,"nested callback lost outer scope");}
    if(throwSetup)throw HostError("host setup failure");
    auto* d=Descriptor(ObjectHeader(self),selectedIndex);
    auto* a=Installed<decltype(&GetClientAnim)>("GameStudioRenderer_StudioGetAnim")(self,0,ObjectModel(self),d);
    Installed<decltype(&ClientCalc)>("GameStudioRenderer_StudioCalcRotations")(self,0,nullptr,nullptr,d,a,rawFrame);
}
void __fastcall HostClientMerge(void* self,int,model_t* value)
{
    Require(csol::client::SamplingScope::current->model==value,"merge ignored explicit model");
    auto* d=Descriptor(ObjectHeader(self));auto* a=Installed<decltype(&csol::client::GetClientAnim)>("GameStudioRenderer_StudioGetAnim")(self,0,value,d);
    Installed<decltype(&csol::client::ClientCalc)>("GameStudioRenderer_StudioCalcRotations")(self,0,nullptr,nullptr,d,a,rawFrame);
}
void HostEngineSetup()
{
    if(cache.value!=0){++cacheHits;return;}
    auto* d=Descriptor(header);auto* a=Installed<decltype(&csol::client::GetEngineAnim)>("R_GetAnim")(model,d);
    Installed<decltype(&csol::client::EngineCalc)>("R_StudioCalcRotations")(nullptr,nullptr,d,a,rawFrame);
}
void HostEngineMerge(model_t* value)
{
    Require(csol::client::SamplingScope::current->model==value,"engine merge ignored model argument");
    auto* d=Descriptor(header);auto* a=Installed<decltype(&csol::client::GetEngineAnim)>("R_GetAnim")(value,d);
    Installed<decltype(&csol::client::EngineCalc)>("R_StudioCalcRotations")(nullptr,nullptr,d,a,rawFrame);
}
void HostServer(model_t* value,float frame,int sequence,const float* angles,const float* origin,
    const unsigned char* controller,const unsigned char* blending,int bone,const void* entity)
{
    Require(csol::client::SamplingScope::current->model==value,"SV scope missing");
    ++serverCalls;receivedFrame=frame;receivedSequence=sequence;receivedAngles=angles;receivedOrigin=origin;
    receivedController=controller;receivedBlending=blending;receivedBone=bone;receivedEntity=entity;
}
int HostOriginalBounds(const char*,float*,float*){++originalBoundsCalls;return -1;}
int HostCompute(unsigned char* bytes,float* mins,float* maxs)
{
    ++boundsCalls;Require(reinterpret_cast<const std::byte*>(bytes)==delivered,"bounds consumed a different buffer");
    Require(std::memcmp(bytes,resource.data(),resource.size())==0,"bounds bytes changed after reading");
    mins[0]=-9;maxs[2]=13;return 1;
}
void* HostClientBase(){return reinterpret_cast<void*>(0x10000000);}
mh_gamesymbol_status_t HostResolve(void*,const char* name,mh_gamesymbol_kind_t,void** output)
{*output=symbols.at(name);return MH_GAMESYMBOL_OK;}
mh_gamesymbol_status_t HostMember(void*,const char* name,std::uint32_t* output)
{*output=std::string(name)=="CSOL_StudioRenderer_Header"?0x44:0x34;return MH_GAMESYMBOL_OK;}
void Initialize()
{
    static metahook_api_t host{};host.SysError=HostErrorFunction;host.GetClientBase=HostClientBase;
    host.ResolveGameSymbol=HostResolve;host.QueryGameSymbolStructMember=HostMember;
    csol::client::api=&host;csol::client::engine.pfnGetCvarPointer=HostCvar;cache.value=7;
    symbols={{"pstudiohdr",&header},{"r_model",&model},{"CSOL_Cache_Alloc",Address(HostAllocate)},
        {"CSOL_Memory_Init",Address(HostMemory)},{"R_GetAnim",Address(HostEngineAnim)},
        {"R_StudioCalcRotations",Address(HostEngineCalc)},{"CSOL_SV_StudioSetupBones",Address(HostServer)},
        {"CSOL_R_StudioComputeBounds",Address(HostCompute)},{"CSOL_R_GetStudioBounds",Address(HostOriginalBounds)},
        {"R_StudioSetupBones",Address(HostEngineSetup)},{"R_StudioMergeBones",Address(HostEngineMerge)},
        {"GameStudioRenderer_StudioGetAnim",Address(HostClientAnim)},{"GameStudioRenderer_StudioCalcRotations",Address(HostClientCalc)},
        {"GameStudioRenderer_StudioSetupBones",Address(HostClientSetup)},{"GameStudioRenderer_StudioMergeBones",Address(HostClientMerge)}};
    csol::client::InstallSequenceEngineGuards();csol::client::InstallSequenceClientGuards();
    Require(hooks.size()==8,"pre-studio hook set differs");negotiated=true;csol::client::InstallSequenceStudioGuards();
    Require(hooks.size()==12,"studio outer hook count differs");csol::client::InstallSequenceStudioGuards();Require(hooks.size()==12,"double studio installation");
}
void Load(model_t& value,const Bytes& bytes)
{
    value.type=mod_studio;std::strcpy(value.name,"models/adapter.mdl");
    csol::client::SequenceLoadScope load(&value,bytes);
    const auto size=Retained(bytes);auto* memory=Installed<decltype(&csol::client::CacheAlloc)>("CSOL_Cache_Alloc")(&value.cache,int(size),value.name);
    std::memcpy(memory,bytes.data(),size);load.Complete();
}
void Run()
{
    using namespace csol::client;
    Initialize();model_t a{},b{},ordinary{};auto packed=Model();Load(a,packed);Load(b,Model(true,true));Load(ordinary,Model(false));
    Object oa{},ob{},oo{};Bind(oa,a);Bind(ob,b);Bind(oo,ordinary);inner=&ob;
    rawFrame=1.5f;nested=true;Installed<decltype(&ClientSetup)>("GameStudioRenderer_StudioSetupBones")(&oa,0);Require(clientCalls==2,"tagged setup skipped native sampler");
    Require(cache.value==7 && !SamplingScope::current,"nested setup leaked cache/scope");
    const auto oldHits=cacheHits;ClientSetup(&oo,0);Require(cacheHits==oldHits+1,"ordinary cache behavior changed");
    Require(clientCalls==2,"ordinary cache hit forced sampler");
    cache.value=0;ClientSetup(&oo,0);Require(clientCalls==3,"ordinary cache-miss path blocked");cache.value=7;
    for(float frame:{-2.f,-0.005f,0.f,1.5f,2.f,3.f}){rawFrame=frame;ClientSetup(&oa,0);Require(receivedFrame==frame,"adapter changed raw client frame");}
    rawFrame=std::numeric_limits<float>::quiet_NaN();const auto before=clientCalls;
    Rejected([&]{ClientSetup(&oa,0);},"NaN reached client sampler");Require(clientCalls==before,"NaN forwarded");
    Require(cache.value==7 && !SamplingScope::current,"error leaked scope/cache");rawFrame=1;
    throwSetup=true;Rejected([&]{ClientSetup(&oa,0);},"host throw not delivered");throwSetup=false;
    Require(cache.value==7 && !SamplingScope::current,"host throw leaked scope/cache");
    nestedWork=[&]{ClientSetup(&oo,0);};ClientSetup(&oa,0);Require(cache.value==7,"nested ordinary setup leaked cache");
    // The self model is deliberately ordinary: Merge must use its explicit model.
    model=&ordinary;std::memcpy(oa.data.data()+0x34,&model,4);Installed<decltype(&ClientMerge)>("GameStudioRenderer_StudioMergeBones")(&oa,0,&a);Bind(oa,a);
    header=static_cast<studiohdr_t*>(a.cache.data);model=&a;Installed<decltype(&EngineSetup)>("R_StudioSetupBones")();Require(engineCalls==1,"engine setup not wired");
    model=&ordinary;Installed<decltype(&EngineMerge)>("R_StudioMergeBones")(&a);Require(engineCalls==2,"engine merge not wired");
    const float angles[3]{1,2,3},origin[3]{4,5,6};const unsigned char controllers[4]{1,2,3,4},blends[2]{5,6};int entity=17;
    Installed<decltype(&EngineServerSetup)>("CSOL_SV_StudioSetupBones")(&a,128,1,angles,origin,controllers,blends,1,&entity);
    Require(serverCalls==1 && receivedFrame==128 && receivedSequence==1 && receivedBone==1 && receivedEntity==&entity,"SV scalar/ninth arguments changed");
    Require(receivedAngles==angles && receivedOrigin==origin && receivedController==controllers && receivedBlending==blends,"SV pointer arguments changed");
    EngineServerSetup(&a,256,-7,angles,origin,controllers,blends,-1,&entity);Require(receivedSequence==0,"SV invalid sequence fallback differs");
    const auto serverBefore=serverCalls;Rejected([&]{EngineServerSetup(&a,257,0,angles,origin,controllers,blends,-1,&entity);},"SV out-of-range frame normalized as client");
    Rejected([&]{EngineServerSetup(&a,std::numeric_limits<float>::infinity(),0,angles,origin,controllers,blends,-1,&entity);},"SV infinity accepted");
    Require(serverCalls==serverBefore,"unsafe SV frame forwarded");
    auto* d=Descriptor(header);auto* anim=HostEngineAnim(&a,d);
    Rejected([&]{EngineCalc(nullptr,nullptr,d,anim,0);},"sampler outside setup accepted");
    auto original=d->animindex;d->animindex+=12;Rejected([&]{ClientSetup(&oa,0);},"mutated descriptor accepted");d->animindex=original;
    // Cache relocation preserves ownership but stale renderer headers do not.
    auto moved=allocations[&a.cache];a.cache.data=moved.data();Rejected([&]{ClientSetup(&oa,0);},"stale header after relocation accepted");
    Bind(oa,a);ClientSetup(&oa,0);a.cache.data=allocations[&a.cache].data();Bind(oa,a);
    CacheAlloc(&a.cache,int(Retained(packed)),"replacement");Rejected([&]{ClientSetup(&oa,0);},"cache replacement retained previous proof");Load(a,packed);Bind(oa,a);
    {SequenceLoadScope cancelled(&a,packed);}Rejected([&]{ClientSetup(&oa,0);},"cancelled load published receipt");Load(a,packed);Bind(oa,a);
    model_t pending{};pending.type=mod_studio;
    {SequenceLoadScope noAllocation(&pending,packed);pending.cache.data=packed.data();Rejected([&]{noAllocation.Complete();},"load without allocation receipt accepted");}
    float mins[3]{8,8,8},maxs[3]{8,8,8};resource=packed;
    Require(Installed<decltype(&StudioBounds)>("CSOL_R_GetStudioBounds")("models/test.mdl",mins,maxs)==1,"valid tagged bounds failed");Require(readCalls==1 && boundsCalls==1 && !originalBoundsCalls,"bounds reopened original wrapper");
    Require(mins[0]==-9 && maxs[2]==13,"bounds outputs lost");resource=Model(false);
    Require(StudioBounds("models/ordinary.mdl",mins,maxs)==1 && readCalls==2,"ordinary bounds single read failed");
    for(const char* path:{"textures/test.mdl","models/test.txt","models/missing.mdl"})
    {Require(StudioBounds(path,mins,maxs)==0,"invalid/missing path accepted");Require(mins[0]==0 && maxs[2]==0,"failure outputs not cleared");}
    Require(readCalls==2,"invalid/missing path read file");resource=Bytes(20);Require(StudioBounds("models/short.mdl",mins,maxs)==0,"short bounds input accepted");
    resource=packed;Put(resource,72,0x7fffffffu);Rejected([&]{StudioBounds("models/bad.mdl",mins,maxs);},"bounds trusted header length");
    model_t partial{};Object partialObject{};auto partialBytes=UnsupportedTrack();Load(partial,partialBytes);Bind(partialObject,partial);
    selectedIndex=0;ClientSetup(&partialObject,0);const auto partialBefore=clientCalls;
    selectedIndex=1;Rejected([&]{ClientSetup(&partialObject,0);},"unsupported track reached native sampler");
    Require(clientCalls==partialBefore,"unsupported native sampler was called");selectedIndex=0;
    resource=partialBytes;const auto boundsBefore=boundsCalls;
    Rejected([&]{StudioBounds("models/partial.mdl",mins,maxs);},"bounds accepted an unsupported track");
    Require(boundsCalls==boundsBefore,"unsupported bounds reached original compute");
    Installed<decltype(&MemoryInit)>("CSOL_Memory_Init")(nullptr,0);Rejected([&]{ClientSetup(&oa,0);},"memory reset left owner live");
    RemoveSequenceClientGuards();
    const std::vector<std::string> expected{"GameStudioRenderer_StudioMergeBones","GameStudioRenderer_StudioSetupBones","R_StudioMergeBones","R_StudioSetupBones","GameStudioRenderer_StudioCalcRotations","GameStudioRenderer_StudioGetAnim"};
    Require(removed==expected,"outer hooks not removed before client samplers in reverse order");
    const auto count=removed.size();RemoveSequenceClientGuards();Require(removed.size()==count,"double remove");
    Require(!studioGuardsInstalled && !boneCache && !headerMember && !modelMember,"remove retained installation state");
}
}

namespace csol::client
{
metahook_api_t* api{};cl_enginefunc_t engine{};
void* Resolve(const char* name,mh_gamesymbol_kind_t){return adapter_test::symbols.at(name);}
hook_t* Hook(void* address,void* replacement,void** original)
{
    using namespace adapter_test;auto record=std::make_unique<HookRecord>();record->address=address;record->replacement=replacement;
    for(const auto& [name,value]:symbols)if(value==address){record->name=name;break;}
    Require(!record->name.empty(),"unrecognized hook target");
    if(record->name.find("SetupBones")!=std::string::npos && record->name!="CSOL_SV_StudioSetupBones")Require(negotiated,"outer guard installed before Renderer negotiation");
    *original=address;auto* result=record.get();hooks.push_back(std::move(record));return reinterpret_cast<hook_t*>(result);
}
void RemoveHook(hook_t* hook)
{auto* record=reinterpret_cast<adapter_test::HookRecord*>(hook);adapter_test::Require(!record->removed,"removed dead hook");record->removed=true;adapter_test::removed.push_back(record->name);}
bool Exists(const char* name){return std::string(name)!="models/missing.mdl";}
std::vector<std::byte> ReadResource(const char*)
{++adapter_test::readCalls;auto result=adapter_test::resource;adapter_test::delivered=result.data();return result;}
}
int main()
{
    try{static_assert(sizeof(void*)==4);adapter_test::Run();std::cout<<"client adapter: "<<adapter_test::checks<<" assertions passed; real production TU, host boundary stubs, no game hooks\n";return 0;}
    catch(const std::exception& e){std::cerr<<"client adapter failed: "<<e.what()<<'\n';return 1;}
}
