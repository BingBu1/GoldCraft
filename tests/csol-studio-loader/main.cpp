// SPDX-License-Identifier: GPL-3.0-or-later
#include "validation.hpp"
#include "../../native/csol/client/sequence_guard.cpp"
#include "../../native/csol/sequence_adapter_fixture.hpp"
#include "../vgui-resource-refresh/stubs.hpp"
#include <map>
#include <fstream>
#include <chrono>
void LoadFixture(model_t*,void*,void (*)(model_t*,void*));
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
    unsigned handles{};
    bool FileExists(const char* name) override {probes.emplace_back(name);return data.contains(name);}
    FileHandle_t Open(const char* name,const char*,const char*) override {
        auto at=data.find(name);if(at==data.end())return nullptr;++handles;return &at->second;
    }
    unsigned Size(FileHandle_t file) override{return unsigned(static_cast<Bytes*>(file)->size());}
    int Read(void* out,int size,FileHandle_t file) override {
        auto& bytes=*static_cast<Bytes*>(file);if(size<0 || size>bytes.size())return 0;
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
    metahook_api_t host{};host.SysError=Error;api=&host;
    mh_interface_t ui{};ui.FileSystem=&files;interfaces=&ui;
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
}
int main() {
    try {static_assert(sizeof(void*)==4);Run();std::cout<<"studio loader: "<<adapter_test::checks<<" checks passed; production loader/guard, host IO/cache boundaries\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
