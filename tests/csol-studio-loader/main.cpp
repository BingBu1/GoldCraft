// SPDX-License-Identifier: GPL-3.0-or-later
#include "validation.hpp"
#include "../../native/csol/client/resource_refresh.hpp"
#include "../../native/csol/client/sequence_guard.cpp"
#include "../../native/csol/sequence_adapter_fixture.hpp"
#include "../vgui-resource-refresh/stubs.hpp"
#include <map>
#include <fstream>
#include <chrono>
void LoadFixture(model_t*,void*,void (*)(model_t*,void*));
void NativeLoadFixture(model_t*,void*,int,unsigned,void (*)(model_t*,void*));
namespace csol::client {
metahook_api_t* api{};mh_interface_t* interfaces{};cl_enginefunc_t engine{};engine_studio_api_t studio{};
void* Resolve(const char*,mh_gamesymbol_kind_t){throw std::runtime_error("unexpected hook resolution");}
hook_t* Hook(void*,void*,void**){throw std::runtime_error("unexpected hook installation");}
void RemoveHook(hook_t*){throw std::runtime_error("unexpected hook removal");}
void InstallModelCrc(){throw std::runtime_error("unexpected CRC installation");}
bool EnsureGraphics(){throw std::runtime_error("unexpected sprite graphics");}
}
namespace {
using namespace adapter_test;
using namespace csol::client;
struct Files : FileSystemStub {
    std::map<std::string,Bytes> data;
    std::vector<std::string> probes;
    unsigned handles{},opens{},reads{};std::size_t readBytes{};
    bool FileExists(const char* name) override {probes.emplace_back(name);return data.contains(name);}
    FileHandle_t Open(const char* name,const char*,const char*) override {
        ++opens;auto at=data.find(name);if(at==data.end())return nullptr;++handles;return &at->second;
    }
    unsigned Size(FileHandle_t file) override{return unsigned(static_cast<Bytes*>(file)->size());}
    int Read(void* out,int size,FileHandle_t file) override {
        ++reads;auto& bytes=*static_cast<Bytes*>(file);readBytes+=size;if(size<0 || size>bytes.size())return 0;
        std::memcpy(out,bytes.data(),size);return size;
    }
    void Close(FileHandle_t) override {Require(handles!=0,"closing unowned file");--handles;}
} files;
Bytes cacheBytes;unsigned engineLoads{};
void* Allocate(cache_user_t* owner,int size,const char*)
{cacheBytes.assign(size,std::byte{});owner->data=cacheBytes.data();return owner->data;}
void EngineLoad(model_t* model,void* bytes) {
    ++engineLoads;
    auto* header=static_cast<studiohdr_t*>(bytes);
    const auto size=header->textureindex?header->texturedataindex:header->length;
    auto* out=CacheAlloc(&model->cache,size,model->name);std::memcpy(out,bytes,size);
}
[[noreturn]] void Error(const char* format,...)
{va_list args;va_start(args,format);HostFailure(format,args);}
void Quiet(const char*,...){}
void Load(model_t& model,const Bytes& bytes) {
    model.type=mod_studio;std::strcpy(model.name,"models/test.mdl");
    files.data[model.name]=bytes;
    // Actual file bytes are read through production ReadResource; the engine
    // input is only the initial header used by the production version dispatch.
    auto header=bytes;LoadFixture(&model,header.data(),EngineLoad);
    Require(!files.handles,"load leaked filesystem handle");
}
Bytes Sidecar() {
    auto model=Model();const auto descriptor=Word(model,168),animation=Word(model,descriptor+124);
    constexpr unsigned size=56;Bytes bytes(8+size+176);
    Put(bytes,0,1u);Put(bytes,4,8+size);
    std::memcpy(bytes.data()+8,model.data()+animation,size);
    std::memcpy(bytes.data()+8+size,model.data()+descriptor,176);return bytes;
}
void Run() {
    static metahook_api_t host{};host.SysError=Error;api=&host;
    static mh_interface_t ui{};ui.FileSystem=&files;interfaces=&ui;
    engine.Con_DPrintf=Quiet;cacheAlloc=Allocate;
    model_t model{};auto valid=Model();
    files.data["models/test1.seq"]=Bytes(8); // Must never be read for tagged input.
    csol::validationReads=0;Load(model,valid);
    Require(csol::validationReads==1,"packed model validated more than once");
    Require(files.probes.empty(),"packed model probed unused sidecar");
    Require(owners.ModelState(Owner(&model),Base(&model),model.cache.data)==csol::SequenceOwnerCheck::Ready,"valid packed owner missing");
    const auto before=engineLoads;
    const auto reject=[&](const Bytes& bad) {
        const auto original=model.cache.data;Rejected([&]{Load(model,bad);},"bad model entered engine");
        Require(engineLoads==before && model.cache.data==original,"rejected load altered engine cache");
        Require(owners.ModelState(Owner(&model),Base(&model),model.cache.data)==csol::SequenceOwnerCheck::Ready,"rejected load retired prior owner");
        Require(!files.handles,"rejected file handle leaked");
    };
    const auto footer=Word(valid,168)-32;
    auto bad=valid;Put(bad,footer,0u);reject(bad);
    bad=valid;Put(bad,footer+4,0u);reject(bad);
    bad=valid;Put(bad,footer+8,99u);reject(bad);
    bad=valid;Put(bad,footer+20,0xfffffff0u);reject(bad);
    bad=valid;bad[Word(valid,Word(valid,168)+124)+48]=std::byte{};reject(bad);
    bad=valid;bad.resize(244);reject(bad);
    Require(files.probes.empty(),"invalid tagged body probed unused sidecar");
    files.data.erase("models/test1.seq");files.probes.clear();csol::validationReads=0;
    Load(model,Model(false));Require(csol::validationReads==1,"ordinary final validation count differs");
    Require(files.probes==std::vector<std::string>{"models/test1.seq"},"ordinary sidecar search changed");
    files.data["models/test1.seq"]=Sidecar();files.probes.clear();csol::validationReads=0;
    Load(model,Model(false));Require(csol::validationReads==2,"append and final validation count differs");
    Require(files.probes==std::vector<std::string>{"models/test1.seq","models/test2.seq"},"ordered contiguous discovery differs");
    Require(static_cast<studiohdr_t*>(model.cache.data)->numseq==3,"sidecar sequence not appended");
    files.probes.clear();csol::validationReads=0;Load(model,valid);
    Require(csol::validationReads==1 && files.probes.empty(),"reload reused stale format decision");
    auto unsupported=UnsupportedTrack();Load(model,unsupported);
    auto* hdr=static_cast<studiohdr_t*>(model.cache.data);csol::SequenceSample sample;
    auto* desc=reinterpret_cast<mstudioseqdesc_t*>(cacheBytes.data()+hdr->seqindex);
    Require(owners.Select(Owner(&model),Base(&model),hdr,desc+1,sample)==csol::SequenceOwnerCheck::InvalidTrack,"unsupported track bypassed selection guard");
}
void NativeSource() {
    // Host services have static storage, including after Run() returns.
    model_t model{};model.type=mod_studio;std::strcpy(model.name,"models/test.mdl");
    auto valid=Model();const auto unchanged=valid;
    files.data[model.name]=valid;files.opens=files.reads=0;files.readBytes=0;
    csol::validationReads=0;
    NativeLoadFixture(&model,valid.data(),int(valid.size()),0,EngineLoad);
    Require(!files.opens && !files.reads && !files.readBytes,"native source reread the VFS");
    Require(valid==unchanged,"native model input was mutated");
    Require(csol::validationReads==1,"native source skipped or repeated safety validation");
    Require(owners.ModelState(Owner(&model),Base(&model),model.cache.data)==csol::SequenceOwnerCheck::Ready,"native owner missing");
    for(unsigned version:{20u,21u}) {
        auto encoded=valid;Put(encoded,4,version);const auto saved=encoded;
        NativeLoadFixture(&model,encoded.data(),int(encoded.size()),0,EngineLoad);
        Require(encoded==saved && static_cast<const studiohdr_t*>(model.cache.data)->version==10,"native encoded source changed decode semantics");
    }
    const auto before=engineLoads;
    const auto reject=[&](Bytes bytes,int length) {
        const auto previous=model.cache.data;files.opens=files.reads=0;const auto source=bytes;
        Rejected([&]{NativeLoadFixture(&model,bytes.data(),length,0,EngineLoad);},"bad native span entered engine");
        Require(engineLoads==before && model.cache.data==previous,"bad native span changed cache");
        Require(bytes==source && !files.opens && !files.reads,"bad native span mutated or reopened source");
    };
    for(int size:{-1,0,1,4,7,8,243,128*1024*1024+1})reject(valid,size);
    auto bad=valid;Put(bad,72,unsigned(valid.size()+1));reject(bad,int(bad.size()));
    const auto footer=Word(valid,168)-32;
    for(unsigned offset:{0u,4u,8u,20u}) {
        bad=valid;Put(bad,footer+offset,0xfffffff0u);reject(bad,int(bad.size()));
    }
    // Correct native bytes win over a same-name stale/corrupted VFS resource.
    files.data[model.name]=Bytes(244);files.opens=files.reads=0;
    NativeLoadFixture(&model,valid.data(),int(valid.size()),0,EngineLoad);
    Require(!files.opens && !files.reads,"native load depended on a second file image");
    files.data[model.name]=valid;
    for(unsigned mode:{1u,2u,3u}) {
        files.opens=files.reads=0;
        NativeLoadFixture(&model,valid.data(),int(valid.size()),mode,EngineLoad);
        Require(files.opens==1 && files.reads==1,"unmatched caller/thread did not retain VFS fallback");
    }
    for(unsigned mode:{4u,5u,6u}) {
        files.opens=files.reads=0;const auto previousLoads=engineLoads;const auto previous=model.cache.data;
        Rejected([&]{NativeLoadFixture(&model,valid.data(),int(valid.size()),mode,EngineLoad);},"incomplete native read was accepted");
        Require(!files.opens && !files.reads && engineLoads==previousLoads && model.cache.data==previous,"short read reopened or published model");
    }
    // A new file-size frame is used on each call, including nested downstream IO.
    auto ordinary=Model(false);files.data["models/test1.seq"]=Sidecar();files.opens=files.reads=0;
    NativeLoadFixture(&model,ordinary.data(),int(ordinary.size()),0,EngineLoad);
    Require(files.opens==1 && files.reads==1,"native append must only read its actual sidecar");
    Require(static_cast<studiohdr_t*>(model.cache.data)->numseq==3,"native sidecar append changed");
    files.data.erase("models/test1.seq");
#if CSOL_RESOURCE_REFRESH
    resource_refresh_detail::readsAllowed=false;
    Rejected([&]{NativeLoadFixture(&model,valid.data(),int(valid.size()),0,EngineLoad);},"native span bypassed generation retirement");
    resource_refresh_detail::readingNewGeneration=true;files.opens=files.reads=0;
    NativeLoadFixture(&model,valid.data(),int(valid.size()),0,EngineLoad);
    Require(!files.opens && !files.reads,"staged generation reopened native model");
    resource_refresh_detail::readingNewGeneration=false;resource_refresh_detail::readsAllowed=true;
#endif
    NativeLoadFixture(&model,valid.data(),int(valid.size()),0,[](model_t* parent,void* source) {
        auto childBytes=Model();model_t child{};child.type=mod_studio;std::strcpy(child.name,"models/inner.mdl");
        NativeLoadFixture(&child,childBytes.data(),int(childBytes.size()),0,EngineLoad);
        Require(static_cast<const studiohdr_t*>(source)->version==10,"nested load corrupted parent source");
        EngineLoad(parent,source);
    });
    Require(owners.ModelState(Owner(&model),Base(&model),model.cache.data)==csol::SequenceOwnerCheck::Ready,"nested native load lost parent owner");
    auto* pages=static_cast<std::byte*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Require(pages!=nullptr,"guard-page allocation failed");DWORD protection{};
    Require(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&protection)!=0,"guard-page protection failed");
    auto* shortFile=pages+4096-8;std::memcpy(shortFile,valid.data(),8);
    Rejected([&]{NativeLoadFixture(&model,shortFile,8,0,EngineLoad);},"short native header reached engine");
    Require(VirtualFree(pages,0,MEM_RELEASE)!=0,"guard-page release failed");
    Require(!files.handles,"native source leaked handles");
}
}
int main() {
    try {static_assert(sizeof(void*)==4);Run();NativeSource();std::cout<<"studio loader: "<<adapter_test::checks<<" checks passed; production loader/guard, host IO/cache boundaries\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
