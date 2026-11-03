// SPDX-License-Identifier: GPL-3.0-or-later
// Actual ReHLDS SDK, complete production loader and real dependency collector.
// Only filesystem, console, fatal reporting and the injected CNG failure are doubles.
#include "precompiled.h"
#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <stdexcept>
#include <vector>
#include <memory>
#include <chrono>
#include <filesystem>
#include <fstream>

CreateInterfaceFn g_FileSystemFactory{};
IFileSystem* g_pFileSystem{};

static bool failHash;
static unsigned hashCalls;
static NTSTATUS WINAPI FixtureHash(BCRYPT_ALG_HANDLE algorithm, PUCHAR secret, ULONG secretSize,
    PUCHAR input, ULONG inputSize, PUCHAR output, ULONG outputSize)
{
    ++hashCalls;
    if (failHash) return static_cast<NTSTATUS>(0xc0000001u);
    return BCryptHash(algorithm, secret, secretSize, input, inputSize, output, outputSize);
}
#define BCryptHash FixtureHash
#include "../../native/engine/csol_loader.cpp"
#undef BCryptHash
#include "../../native/engine/precache.h"
#include "../../native/csol/weapon_resources.hpp"
#define GoldCraftCsolSequenceDelta GoldCraftCsolSequenceDeltaBefore
#define GoldCraftPrecacheCsolResources GoldCraftPrecacheCsolResourcesBefore
#define GoldCraftDecodeStudio GoldCraftDecodeStudioBefore
#define GoldCraftLoadCsolSprite GoldCraftLoadCsolSpriteBefore
#include "../../native/engine/before_csol_loader.cpp"
#undef GoldCraftCsolSequenceDelta
#undef GoldCraftPrecacheCsolResources
#undef GoldCraftDecodeStudio
#undef GoldCraftLoadCsolSprite

namespace fixture {
using Bytes = std::vector<std::byte>;
unsigned checks, failures, opens, closes;
struct Fatal : std::runtime_error {using std::runtime_error::runtime_error;};
struct Entry {Bytes bytes; std::size_t virtualSize{}; unsigned char fill{}; std::uint64_t generation{};};
std::map<std::string,Entry> vfs, pack;
bool packFirst;
std::set<std::string> cannotOpen, shortRead;
std::map<std::string,unsigned> existsCalls, readCalls;
std::vector<std::string> advertised, printed;
std::vector<std::string> batchTrace;
bool recordBatch;
std::filesystem::path common;
std::map<std::string,std::pair<std::size_t,std::array<unsigned char,32>>> realInputs;
struct Handle {std::string name;Entry entry;bool packed{};};
std::uint64_t generation;
bool metadataMode;
std::string mutateOpened;

void Check(bool ok, const char* text, unsigned line)
{
    ++checks;
    if (!ok) {++failures;throw std::runtime_error(std::string("check failed at ")+std::to_string(line)+": "+text);}
}
#define CHECK(x) fixture::Check(bool(x), #x, __LINE__)

Bytes Text(std::string_view text)
{return {reinterpret_cast<const std::byte*>(text.data()),reinterpret_cast<const std::byte*>(text.data()+text.size())};}
void Put(Bytes& bytes,std::size_t at,std::uint32_t value)
{CHECK(at+4<=bytes.size());std::memcpy(bytes.data()+at,&value,4);}
void String(Bytes& bytes,std::size_t at,std::string_view text)
{CHECK(at+text.size()<bytes.size());std::memcpy(bytes.data()+at,text.data(),text.size());}
Bytes Model(std::string_view sound="weapons/cache_a.wav",std::string_view texture="cso_fixture.bmp")
{
    // One bone, one frame with zero animation offsets, one sound, one 1x1 texture.
    Bytes result(1473);
    Put(result,0,0x54534449);Put(result,4,10);Put(result,72,unsigned(result.size()));
    Put(result,140,1);Put(result,144,244);Put(result,164,1);Put(result,168,356);
    Put(result,180,1);Put(result,184,624);Put(result,188,704);
    Put(result,356+48,1);Put(result,356+52,532);Put(result,356+56,1);
    Put(result,356+120,1);Put(result,356+124,608);
    Put(result,532+4,5004);String(result,532+12,sound);
    String(result,624,texture);Put(result,624+68,1);Put(result,624+72,1);Put(result,624+76,704);
    return result;
}
Bytes Sequence(std::string_view sound)
{
    Bytes result(8+76+16+176);
    Put(result,0,1);Put(result,4,100);Put(result,8+4,5004);String(result,8+12,sound);
    Put(result,100+48,1);Put(result,100+56,1);Put(result,100+120,1);Put(result,100+124,84);
    return result;
}
void Set(std::string name,Bytes bytes){vfs[std::move(name)]={std::move(bytes),0,0,++generation};}
void Set(std::string name,std::string_view bytes){Set(std::move(name),Text(bytes));}
const Entry* Locate(const std::string& name)
{
    if(packFirst)if(auto it=pack.find(name);it!=pack.end())return &it->second;
    if(auto it=vfs.find(name);it!=vfs.end())return &it->second;
    if(!packFirst)if(auto it=pack.find(name);it!=pack.end())return &it->second;
    return nullptr;
}
Bytes ReadDisk(const std::string& name)
{
    // Match the existing resource exporter's generated HUD and source aliases.
    // Raw common intentionally does not contain our versioned deployment paths.
    Bytes bytes;
    if(auto generated=csol::GeneratedWeaponResource(name);!generated.empty())bytes=Text(generated);
    else {
        std::ifstream file(common/std::filesystem::path(csol::WeaponResourceSource(name)),std::ios::binary);
        if(!file)throw std::runtime_error("fixture missing "+name);
        file.seekg(0,std::ios::end);const auto n=file.tellg();file.seekg(0);
        bytes.resize(static_cast<std::size_t>(n));file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(!file)throw std::runtime_error("fixture short "+name);
    }
    std::array<unsigned char,32> digest{};
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),32)>=0);
    auto value=std::make_pair(bytes.size(),digest);
    if(auto it=realInputs.find(name);it!=realInputs.end())CHECK(it->second==value);
    else realInputs.emplace(name,value);
    return bytes;
}
Bytes Raw(const std::string& name)
{
    if(const auto* entry=Locate(name))return entry->bytes;
    if(!common.empty())return ReadDisk(name);
    throw std::runtime_error("fixture missing "+name);
}
bool Has(const std::string& name)
{return Locate(name) || (!common.empty() && (!csol::GeneratedWeaponResource(name).empty() ||
    std::filesystem::is_regular_file(common/std::filesystem::path(csol::WeaponResourceSource(name)))));}
void ResetCounters()
{existsCalls.clear();readCalls.clear();advertised.clear();printed.clear();opens=closes=hashCalls=0;}
void Reset()
{
    resourceBatch.reset();dependencyCache=DependencyCache{};vfs.clear();pack.clear();
    packFirst=false;failHash=false;common.clear();realInputs.clear();cannotOpen.clear();shortRead.clear();ResetCounters();
    g_FileSystemFactory=nullptr;g_pFileSystem=nullptr;metadataMode=false;mutateOpened.clear();
}
void Base()
{
    Reset();Set("models/cache.mdl",Model());Set("sound/weapons/cache_a.wav","A");
    Set("sound/weapons/cache_b.wav","B");Set("sound/weapons/cache_c.wav","C");
    Set("materials/common.json","{}");
}
Files Oracle(const std::string& model)
{return csol::WeaponDependencies(model,Raw,Has);}
void Precache(const std::string& model)
{
    auto bytes=Raw(model);csol::DecodeStudioModel(bytes);
    GoldCraftPrecacheCsolResources(model.c_str(),reinterpret_cast<studiohdr_t*>(bytes.data()));
}
bool Contains(std::string_view path)
{return std::find(advertised.begin(),advertised.end(),path)!=advertised.end();}
void Exact(const std::string& model)
{
    auto expected=Oracle(model);expected.erase(std::remove(expected.begin(),expected.end(),model),expected.end());
    CHECK(advertised==expected);
}
void Pass(const std::string& model,bool hit)
{
    ResetCounters();GoldCraftBeginCsolResources();Precache(model);
    CHECK(resourceBatch && resourceBatch->models==1);
    CHECK(resourceBatch->hits==unsigned(hit));CHECK(resourceBatch->rebuilt==unsigned(!hit));
    Exact(model);CHECK(opens==closes);GoldCraftEndCsolResources();
    CHECK(!resourceBatch);CHECK(printed.size()==1);CHECK(printed[0].find("dependencies models=1")!=std::string::npos);
}
template<class Fn> void Reject(Fn fn,const char* expected)
{
    bool caught=false;try {fn();}catch(const std::exception& e){caught=true;CHECK(std::string(e.what()).find(expected)!=std::string::npos);}
    CHECK(caught);CHECK(opens==closes);
}
void ColdWarm()
{
    Base();Set("models/second.mdl",Model());
    auto first=Oracle("models/cache.mdl"),second=Oracle("models/second.mdl");
    Files expected;std::set<std::string> seen;
    for(const auto& [model,files]:std::vector<std::pair<std::string,Files>>{{"models/cache.mdl",first},{"models/second.mdl",second}})
        for(const auto& file:files)if(file!=model && seen.insert(file).second)expected.push_back(file);
    for(int pass=0;pass<2;++pass){
        ResetCounters();GoldCraftBeginCsolResources();Precache("models/cache.mdl");Precache("models/second.mdl");Precache("models/cache.mdl");
        CHECK(advertised==expected);CHECK(resourceBatch->models==3);CHECK(resourceBatch->hits==(pass?3u:1u));
        CHECK(resourceBatch->rebuilt==(pass?0u:2u));CHECK(dependencyCache.Size()==2);
        CHECK(readCalls["materials/common.json"]==1);CHECK(readCalls["models/cache.mdl"]==1);CHECK(readCalls["models/second.mdl"]==1);
        CHECK(existsCalls["models/cache1.seq"]==1);CHECK(existsCalls["models/cache.json"]==1);
        CHECK(existsCalls["models/cache_hands.json"]==1);CHECK(opens==closes);
        GoldCraftEndCsolResources();CHECK(!resourceBatch);CHECK(printed.size()==1);
    }
    // Standalone use has no cross-call advertised suppression or retained input bodies.
    ResetCounters();Precache("models/cache.mdl");const auto count=advertised.size();Precache("models/cache.mdl");
    CHECK(advertised.size()==count*2);CHECK(!resourceBatch);CHECK(printed.empty());CHECK(readCalls["models/cache.mdl"]==2);
}
void Invalidation()
{
    Base();Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);
    // Equal length edits; VFS exposes no physical path or timestamps to loader.
    Set("models/cache.mdl",Model("weapons/cache_b.wav"));Pass("models/cache.mdl",false);CHECK(Contains("sound/weapons/cache_b.wav"));
    CHECK(!Contains("sound/weapons/cache_a.wav"));Pass("models/cache.mdl",true);
    pack["models/cache.mdl"]={Model("weapons/cache_c.wav")};packFirst=true;
    Pass("models/cache.mdl",false);CHECK(Contains("sound/weapons/cache_c.wav"));Pass("models/cache.mdl",true);
    packFirst=false;Pass("models/cache.mdl",false);CHECK(Contains("sound/weapons/cache_b.wav"));

    Set("sprites/cache.spr","sprite");
    Set("models/cache.json",R"({"bindSpr":[{"texIndex":0,"sprName":"sprites/cache.spr","sequence":[]}]})");
    Pass("models/cache.mdl",false);CHECK(Contains("sprites/cache.spr"));Pass("models/cache.mdl",true);
    vfs.erase("models/cache.json");Pass("models/cache.mdl",false);CHECK(!Contains("sprites/cache.spr"));
    Set("models/texture/cache.tga","image");
    Set("materials/common.json",R"({"Materials":[{"OriginalTexture":"models/cache.mdl/cso_fixture.bmp","BaseTexture":"models/texture/cache.tga"}]})");
    Pass("models/cache.mdl",false);CHECK(Contains("models/texture/cache.tga"));CHECK(Contains("materials/common.json"));
    Set("materials/common.json","{}");Pass("models/cache.mdl",false);CHECK(!Contains("materials/common.json"));
    vfs.erase("materials/common.json");Pass("models/cache.mdl",false);Set("materials/common.json","{}");Pass("models/cache.mdl",false);

    Set("models/cache1.seq",Sequence("weapons/cache_a.wav"));Pass("models/cache.mdl",false);CHECK(Contains("models/cache1.seq"));
    CHECK(Contains("sound/weapons/cache_a.wav"));Pass("models/cache.mdl",true);
    Set("models/cache2.seq",Sequence("weapons/cache_c.wav"));Pass("models/cache.mdl",false);CHECK(Contains("models/cache2.seq"));
    CHECK(Contains("sound/weapons/cache_c.wav"));vfs.erase("models/cache1.seq");Pass("models/cache.mdl",false);
    CHECK(!Contains("models/cache2.seq"));vfs.erase("models/cache2.seq");

    auto descriptor=csol::SerializeModelSource({"models/cache.mdl","models/base.mdl","models/cache.mdl","red_","",true});
    Set("models/cache_csol.json",descriptor);Set("models/texture/red_cso_fixture.bmp","red");
    Pass("models/cache.mdl",false);CHECK(Contains("models/texture/red_cso_fixture.bmp"));
    descriptor=csol::SerializeModelSource({"models/cache.mdl","models/base.mdl","models/cache.mdl","blu_","",true});
    Set("models/cache_csol.json",descriptor);Set("models/texture/blu_cso_fixture.bmp","blu");
    Pass("models/cache.mdl",false);CHECK(Contains("models/texture/blu_cso_fixture.bmp"));
    CHECK(!Contains("models/texture/red_cso_fixture.bmp"));vfs.erase("models/cache_csol.json");Pass("models/cache.mdl",false);

    // An optional dollar texture falls back to embedded until supplied.
    Set("models/cache.mdl",Model("weapons/cache_b.wav","$cache.bmp"));Pass("models/cache.mdl",false);
    CHECK(!Contains("models/$cache.tga"));Set("models/$cache.tga","tga");Pass("models/cache.mdl",false);
    CHECK(Contains("models/$cache.tga"));vfs.erase("models/$cache.tga");Pass("models/cache.mdl",false);
}
void Failures()
{
    Base();failHash=true;Pass("models/cache.mdl",false);CHECK(dependencyCache.Size()==0);
    failHash=false;Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);
    Base();shortRead.insert("models/cache.mdl");GoldCraftBeginCsolResources();
    Reject([]{Precache("models/cache.mdl");},"short resource read");CHECK(dependencyCache.Size()==0);GoldCraftEndCsolResources();
    shortRead.clear();Pass("models/cache.mdl",false);
    Base();cannotOpen.insert("models/cache.mdl");GoldCraftBeginCsolResources();
    Reject([]{Precache("models/cache.mdl");},"missing resource");CHECK(dependencyCache.Size()==0);GoldCraftEndCsolResources();
    cannotOpen.clear();Pass("models/cache.mdl",false);
    Base();Set("models/cache.json","{broken");GoldCraftBeginCsolResources();
    Reject([]{Precache("models/cache.mdl");},"invalid model JSON");CHECK(dependencyCache.Size()==0);GoldCraftEndCsolResources();
    Set("models/cache.json","{}");Pass("models/cache.mdl",false);
    Base();Pass("models/cache.mdl",false);vfs.erase("sound/weapons/cache_a.wav");GoldCraftBeginCsolResources();
    Reject([]{Precache("models/cache.mdl");},"missing weapon dependency");CHECK(dependencyCache.Size()==0);GoldCraftEndCsolResources();
    Set("sound/weapons/cache_a.wav","A");Pass("models/cache.mdl",false);
    Base();Pass("models/cache.mdl",false);shortRead.insert("models/cache.mdl");GoldCraftBeginCsolResources();
    Reject([]{Precache("models/cache.mdl");},"short resource read");GoldCraftEndCsolResources();
    shortRead.clear();Pass("models/cache.mdl",true);
}
void Budgets()
{
    Reset();ResourceBatch batch;
    constexpr std::size_t limit=64u*1024u*1024u;
    vfs["big"]={{},limit,7};auto bytes=batch.Read("big");CHECK(bytes.size()==limit);bytes.clear();bytes.shrink_to_fit();
    CHECK(batch.retained==limit);CHECK(batch.inputs.at("big").bytes->size()==limit);
    Set("small","abc");CHECK(batch.Read("small")==Text("abc"));CHECK(batch.retained==limit);
    CHECK(!batch.inputs.at("small").bytes);CHECK(batch.Read("small")==Text("abc"));CHECK(readCalls["small"]==2);
    Set("small","def");Reject([&]{batch.Read("small");},"resource changed during dependency preparation");
    vfs["huge"]={{},128u*1024u*1024u+1,0};Reject([&]{batch.Read("huge");},"resource size outside budget");
    Set("empty","");Reject([&]{batch.Read("empty");},"resource size outside budget");
    batch=ResourceBatch{};CHECK(batch.retained==0);CHECK(batch.inputs.empty());
    for(unsigned i=0;i<65536;++i)CHECK(!batch.Exists("absent/"+std::to_string(i)));
    Set("overflow","overflow");CHECK(batch.Exists("overflow"));CHECK(batch.Read("overflow")==Text("overflow"));
    const auto stamp=batch.Probe("overflow",true);CHECK(stamp.exists && stamp.read && !stamp.reusable);
    CHECK(batch.inputs.size()==65536);CHECK(batch.retained==0);CHECK(opens==closes);
}
void CacheLimits()
{
    DependencyCache cache;const Stamp present{true};auto probe=[&](const std::string&,bool){return present;};
    auto result=std::make_shared<const Files>(Files{"resource"});
    for(unsigned i=0;i<512;++i)cache.Store("key"+std::to_string(i),result,Inputs{{"input",present}},probe);
    CHECK(cache.Size()==512);auto held=cache.Find("key1",probe);CHECK(held==result);
    CHECK(cache.Find("key0",probe)==result);cache.Store("key512",result,Inputs{{"input",present}},probe);
    CHECK(cache.Size()==512);CHECK(!cache.Find("key2",probe));CHECK(cache.Find("key0",probe));CHECK(cache.Find("key1",probe));CHECK(*held==Files{"resource"});
    auto before=cache.Bytes();cache.Store("key0",result,Inputs{},probe);CHECK(!cache.Find("key0",probe));CHECK(cache.Bytes()<before);
    Inputs many;for(unsigned i=0;i<8193;++i)many.emplace("i"+std::to_string(i),present);
    before=cache.Size();cache.Store("too-many",result,std::move(many),probe);CHECK(cache.Size()==before);
    Stamp unusable{true,true,false};cache.Store("unusable",result,Inputs{{"input",unusable}},probe);CHECK(!cache.Find("unusable",probe));
    Inputs wrong{{"input",Stamp{false}}};cache.Store("changed",result,std::move(wrong),probe);CHECK(!cache.Find("changed",probe));
    cache=DependencyCache{};
    auto large=std::make_shared<Files>(Files{std::string(256*1024,'x')});
    for(unsigned i=0;i<100;++i){cache.Store("large"+std::to_string(i),large,Inputs{{"input",present}},probe);CHECK(cache.Bytes()<=16u*1024u*1024u);}
    CHECK(cache.Size()<100);CHECK(cache.Size()>1);CHECK(!cache.Find("large0",probe));CHECK(cache.Find("large99",probe));
    auto oversized=std::make_shared<Files>(Files{std::string(16u*1024u*1024u,'x')});
    before=cache.Size();cache.Store("oversized",oversized,Inputs{{"input",present}},probe);CHECK(cache.Size()==before);
}
void RealResources(const char* path)
{
    Reset();common=std::filesystem::path(path);
    const std::vector<std::string> models{"models/v_skull5.mdl","models/v_dragonsword.mdl","models/v_skullaxe.mdl",
        "models/v_k3.mdl","models/v_cannon.mdl","models/v_balrog11.mdl","models/v_revivegun.mdl",
        "models/player/speed_zombi_host/speed_zombi_host.mdl"};
    Files expected;std::set<std::string> seen;
    for(const auto& model:models){auto bytes=Raw(model);csol::DecodeStudioModel(bytes);
        GoldCraftPrecacheCsolResourcesBefore(model.c_str(),reinterpret_cast<studiohdr_t*>(bytes.data()));}
    const auto expectedModels=printed.size();CHECK(expectedModels>0);
    for(const auto& file:advertised)if(seen.insert(file).second)expected.push_back(file);
    std::printf("real-baseline supplied=%zu special=%zu unique-downloads=%zu\n",models.size(),expectedModels,expected.size());
    std::size_t coldReads=0;
    for(int pass=0;pass<2;++pass){
        ResetCounters();const auto start=std::chrono::steady_clock::now();GoldCraftBeginCsolResources();
        for(const auto& model:models)Precache(model);
        CHECK(advertised==expected);CHECK(resourceBatch->models==expectedModels);
        CHECK(resourceBatch->hits==(pass?expectedModels:0));CHECK(resourceBatch->rebuilt==(pass?0:expectedModels));
        CHECK(readCalls["materials/common.json"]==1);CHECK(opens==closes);
        if(!pass)coldReads=resourceBatch->reads;else CHECK(resourceBatch->reads<=coldReads);
        GoldCraftEndCsolResources();CHECK(!resourceBatch);CHECK(printed.size()==1);
        std::printf("real-resource-pass=%d seconds=%.6f %s",pass,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),printed[0].c_str());
    }
    for(const auto& [path,value]:realInputs){
        ReadDisk(path);std::printf("resource %s %zu ",path.c_str(),value.first);
        for(auto byte:value.second)std::printf("%02x",unsigned(byte));std::printf("\n");
    }
}
}

GoldCraftPrecache gc_precache;
cvar_t sv_auto_precache_sounds_in_models{};
void* Mod_Extradata(model_t* model){CHECK(model && model->cache.data);return model->cache.data;}
void PrecacheModelTexture(const char*,studiohdr_t*){CHECK(resourceBatch);fixture::batchTrace.emplace_back("textures");}
void PrecacheModelSeqGroups(studiohdr_t*){CHECK(resourceBatch);fixture::batchTrace.emplace_back("groups");}
void PrecacheModelSounds(studiohdr_t*){CHECK(resourceBatch);fixture::batchTrace.emplace_back("sounds");}
#include "batch_function.inc"

namespace fixture {
void BatchAdapter()
{
    Base();Set("models/second.mdl",Model());
    auto first=Raw("models/cache.mdl"),second=Raw("models/second.mdl");
    model_t a{},b{},sprite{};a.type=b.type=mod_studio;sprite.type=mod_sprite;
    a.cache.data=first.data();b.cache.data=second.data();
    gc_precache.modelNames={nullptr,"missing",nullptr,"models/cache.mdl","models/second.mdl","models/sprite.spr"};
    gc_precache.models={nullptr,nullptr,nullptr,&a,&b,&sprite};
    sv_auto_precache_sounds_in_models.value=1;recordBatch=true;batchTrace.clear();
    PrecacheModelSpecifiedFiles();CHECK(!resourceBatch);CHECK(printed.size()==1);CHECK(dependencyCache.Size()==2);
    CHECK((batchTrace==std::vector<std::string>{"textures","groups","generic:sound/weapons/cache_a.wav","sounds","textures","groups","sounds"}));
    CHECK(printed[0].find("cache_hits=0 rebuilt=2")!=std::string::npos);
    ResetCounters();batchTrace.clear();sv_auto_precache_sounds_in_models.value=0;
    PrecacheModelSpecifiedFiles();CHECK(!resourceBatch);CHECK(printed.size()==1);
    CHECK((batchTrace==std::vector<std::string>{"textures","groups","generic:sound/weapons/cache_a.wav","textures","groups"}));
    CHECK(printed[0].find("cache_hits=2 rebuilt=0")!=std::string::npos);
    gc_precache.modelNames.clear();gc_precache.models.clear();ResetCounters();batchTrace.clear();
    PrecacheModelSpecifiedFiles();CHECK(!resourceBatch);CHECK(printed.empty());CHECK(batchTrace.empty());recordBatch=false;
}
}

#include "metadata.inc"

int FS_FileExists(const char* name){++fixture::existsCalls[name];return fixture::Has(name);}
FileHandle_t FS_Open(const char* name,const char* options)
{
    CHECK(std::string(options)=="rb");if(fixture::cannotOpen.contains(name))return nullptr;
    const auto* entry=fixture::Locate(name);fixture::Entry data;
    if(entry)data=*entry;else {if(!fixture::Has(name))return nullptr;data.bytes=fixture::ReadDisk(name);}
    const auto packed=fixture::pack.find(name);
    ++fixture::opens;return reinterpret_cast<FileHandle_t>(new fixture::Handle{name,std::move(data),packed!=fixture::pack.end() && entry==&packed->second});
}
void FS_Close(FileHandle_t handle){CHECK(handle);++fixture::closes;delete reinterpret_cast<fixture::Handle*>(handle);}
unsigned FS_Size(FileHandle_t handle)
{auto& e=reinterpret_cast<fixture::Handle*>(handle)->entry;return unsigned(e.virtualSize?e.virtualSize:e.bytes.size());}
int FS_Read(void* output,int size,int count,FileHandle_t handle)
{
    auto& h=*reinterpret_cast<fixture::Handle*>(handle);++fixture::readCalls[h.name];CHECK(count==1);CHECK(size==int(FS_Size(handle)));
    if(h.entry.virtualSize)std::memset(output,h.entry.fill,size);else std::memcpy(output,h.entry.bytes.data(),size);
    if(fixture::mutateOpened==h.name)++h.entry.generation;
    return fixture::shortRead.contains(h.name)?size-1:size;
}
int PF_precache_generic_I(const char* name){fixture::advertised.emplace_back(name);if(fixture::recordBatch)fixture::batchTrace.emplace_back(std::string("generic:")+name);return int(fixture::advertised.size());}
void Con_Printf(const char* format,...)
{char text[2048];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);fixture::printed.emplace_back(text);}
void Con_DPrintf(const char*,...) {}
void NORETURN Sys_Error(const char* format,...)
{char text[2048];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);throw fixture::Fatal(text);}
char loadname[MAX_MODEL_NAME]{};
void* Hunk_AllocName(int,const char*){throw std::runtime_error("unexpected sprite allocator in dependency test");}

int main(int argc,char** argv)
{
    try {
        if(argc==2 && std::string_view(argv[1])=="--mixed-only"){
            fixture::MetadataMixedMethods();return 0;
        }
        if(argc==3 && std::string_view(argv[1])=="--common-only"){
            fixture::RealResources(argv[2]);std::printf("dependency-cache checks=%u failures=%u\n",fixture::checks,fixture::failures);return 0;
        }
        fixture::ColdWarm();fixture::Invalidation();fixture::Failures();fixture::Budgets();fixture::CacheLimits();fixture::BatchAdapter();
        fixture::MetadataCases();
        fixture::MetadataMixedMethods();
        if(argc==2)fixture::RealResources(argv[1]);
        std::printf("dependency-cache checks=%u failures=%u\n",fixture::checks,fixture::failures);return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"%s; checks=%u failures=%u\n",e.what(),fixture::checks,fixture::failures);return 1;
    }
}
