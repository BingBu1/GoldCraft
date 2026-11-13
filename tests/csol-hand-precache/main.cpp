// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual loader and dependency collector; filesystem/model registration
// are explicit SDK boundary doubles. No source snapshots or renderer substitutes.
#include "precompiled.h"
#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../native/engine/csol_loader.cpp"
#include "native_studio_sounds.inc"
CreateInterfaceFn g_FileSystemFactory{};
IFileSystem* g_pFileSystem{};
namespace fixture {
using Bytes=std::vector<std::byte>;
unsigned checks{},opens{},closes{};
std::map<std::string,Bytes> files;
std::vector<std::string> generic,models;
std::map<std::string,unsigned> reads;
void Check(bool good,int line){++checks;if(!good)throw std::runtime_error("check "+std::to_string(line));}
#define CHECK(x) fixture::Check(bool(x),__LINE__)
void Put(Bytes& b,std::size_t at,int x){CHECK(at+4<=b.size());std::memcpy(b.data()+at,&x,4);}
Bytes Text(std::string_view x){return {reinterpret_cast<const std::byte*>(x.data()),reinterpret_cast<const std::byte*>(x.data()+x.size())};}
Bytes Model(){
 Bytes b(1473);Put(b,0,0x54534449);Put(b,4,10);Put(b,72,int(b.size()));
 Put(b,140,1);Put(b,144,244);Put(b,164,1);Put(b,168,356);
 Put(b,180,1);Put(b,184,624);Put(b,188,704);
 Put(b,356+56,1);Put(b,356+120,1);Put(b,356+124,608);
 std::memcpy(b.data()+624,"plain.bmp",10);Put(b,624+68,1);Put(b,624+72,1);Put(b,624+76,704);
 return b;
}
void Precache(const char* name){
 auto b=files.at(name);GoldCraftPrecacheCsolResources(name,reinterpret_cast<studiohdr_t*>(b.data()));
}
void Reset(){resourceBatch.reset();dependencyCache=DependencyCache{};files.clear();generic.clear();models.clear();reads.clear();}
void Setup(){
 Reset();files["models/custom/v_fixture.mdl"]=Model();files["models/csol/hands/declared.mdl"]=Model();
 files["models/custom/v_fixture_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
}
void CofSoundCases(){
 using namespace guest::cof::resources;
 const char* view=GlockModels[static_cast<unsigned>(GlockModel::View)].host;
 constexpr char sample[]="weapons/glock/glock_magout.wav";
 constexpr char mapped[]="sound/guest/cof/weapons_new/glock/glock_magout.wav";
 CHECK(StudioLocalSoundResource(view,5004,sample)==mapped);
 CHECK(StudioLocalSoundResource("models/v_glock18.mdl",5004,sample).empty());
 CHECK(StudioLocalSoundResource(view,5005,sample).empty());
 CHECK(StudioLocalSoundResource(view,5004,"weapons/glock/unknown.wav").empty());
 CHECK(GoldCraftStudioSoundResource(nullptr,5004,sample)==nullptr);
 CHECK(GoldCraftStudioSoundResource(view,5004,nullptr)==nullptr);
 for(const auto& wave:GlockLocalWaves){
  const std::string original="weapons/"+std::string(std::string_view(wave.original).substr(12));
  CHECK(StudioLocalSoundResource(view,5004,original)==wave.host);
 }
 const char* p345=P345Models[static_cast<unsigned>(P345Model::View)].host;
 for(const auto& wave:P345LocalWaves){
  const std::string original="weapons/"+std::string(std::string_view(wave.original).substr(12));
  CHECK(StudioLocalSoundResource(p345,5004,original)==wave.host);
 }
 Setup();auto model=Model();Put(model,356+48,1);Put(model,356+52,532);
 Put(model,532+4,5004);std::memcpy(model.data()+532+12,sample,sizeof(sample));
 files[view]=model;files[std::string(view).substr(0,std::strlen(view)-4)+"_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
 files[mapped]=Text("PCM endpoint fixture; not audio validation");
 GoldCraftBeginCsolResources();Precache(view);
 CHECK(std::find(generic.begin(),generic.end(),mapped)!=generic.end());
 CHECK(std::find(generic.begin(),generic.end(),"sound/weapons/glock/glock_magout.wav")==generic.end());
 GoldCraftEndCsolResources();CHECK(models==std::vector<std::string>{"models/csol/hands/declared.mdl"});
 generic.clear();PrecacheModelSoundsForFile(view,reinterpret_cast<studiohdr_t*>(model.data()));
 CHECK(generic==std::vector<std::string>{mapped});
 generic.clear();PrecacheModelSounds(reinterpret_cast<studiohdr_t*>(model.data()));
 CHECK(generic==std::vector<std::string>{"sound/weapons/glock/glock_magout.wav"});
 files.erase(mapped);bool rejected=false;GoldCraftBeginCsolResources();
 try{Precache(view);}catch(const std::exception&){rejected=true;}
 CHECK(rejected);resourceBatch.reset();
}
void Cases(){
 Setup();const auto before=files;
 for(int pass=0;pass<2;++pass){
  generic.clear();models.clear();GoldCraftBeginCsolResources();Precache("models/custom/v_fixture.mdl");Precache("models/custom/v_fixture.mdl");
  CHECK(models.empty());
  CHECK(resourceBatch->registeredHands==std::set<std::string>{"models/csol/hands/declared.mdl"});
  CHECK(generic==std::vector<std::string>{"models/custom/v_fixture_hands.json"});
  CHECK(resourceBatch->rebuilt==(pass?0u:1u));CHECK(resourceBatch->hits==(pass?2u:1u));
  CHECK(dependencyCache.Size()==1);CHECK(opens==closes);GoldCraftEndCsolResources();CHECK(models==std::vector<std::string>{"models/csol/hands/declared.mdl"});CHECK(files==before);
 }
 files["models/csol/hands/other.mdl"]=Model();
 files["models/custom/v_fixture_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/other.mdl"})");
 generic.clear();models.clear();GoldCraftBeginCsolResources();Precache("models/custom/v_fixture.mdl");
 CHECK(models.empty());CHECK(resourceBatch->registeredHands==std::set<std::string>{"models/csol/hands/other.mdl"});CHECK(resourceBatch->rebuilt==1);
 GoldCraftEndCsolResources();
 // An unrelated hand directory model is not a declared donor.
 files["models/custom/v_fixture_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
 auto split=Model();const int textureStart=624;
 Bytes textures(244);Put(textures,0,0x54534449);Put(textures,4,10);
 textures.insert(textures.end(),split.begin()+textureStart,split.end());
 for(int field:{180,192,196})Put(textures,field,1);
 for(int field:{184,188})Put(textures,field,(field==184?624:704)-textureStart+244);
 Put(textures,244+76,704-textureStart+244);Put(textures,200,int(textures.size()));
 textures.resize(textures.size()+2);Put(textures,72,int(textures.size()));
 split.resize(textureStart);for(int field=180;field<=200;field+=4)Put(split,field,0);Put(split,72,int(split.size()));
 files["models/csol/hands/declared.mdl"]=split;files["models/csol/hands/declaredT.mdl"]=textures;
 generic.clear();models.clear();GoldCraftBeginCsolResources();Precache("models/custom/v_fixture.mdl");
 CHECK(models.empty());CHECK(resourceBatch->registeredHands==std::set<std::string>{"models/csol/hands/declared.mdl"});
 CHECK(std::find(generic.begin(),generic.end(),"models/csol/hands/declaredT.mdl")!=generic.end());
 GoldCraftEndCsolResources();
 // Missing/invalid donors fail before model registration.
 Setup();files.erase("models/csol/hands/declared.mdl");bool rejected=false;
 GoldCraftBeginCsolResources();try{Precache("models/custom/v_fixture.mdl");}catch(const std::exception&){rejected=true;}
 CHECK(rejected&&models.empty());GoldCraftEndCsolResources();
 Setup();files["models/custom/v_fixture_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/../bad.mdl"})");rejected=false;
 GoldCraftBeginCsolResources();try{Precache("models/custom/v_fixture.mdl");}catch(const std::exception&){rejected=true;}
 CHECK(rejected&&models.empty());GoldCraftEndCsolResources();
 Setup();files.erase("models/custom/v_fixture_hands.json");generic.clear();models.clear();
 GoldCraftBeginCsolResources();Precache("models/custom/v_fixture.mdl");CHECK(models.empty()&&generic.empty());
 CHECK(resourceBatch->models==0);GoldCraftEndCsolResources();
 // Default donor and a donor profile cycle share the bounded exact plan.
 Setup();files["models/csol/hands/v_hegrenade.mdl"]=Model();
 files["models/custom/v_fixture_hands.json"]=Text(R"({"version":1})");
 GoldCraftBeginCsolResources();Precache("models/custom/v_fixture.mdl");
 CHECK(models.empty());GoldCraftEndCsolResources();CHECK(models==std::vector<std::string>{"models/csol/hands/v_hegrenade.mdl"});
 Setup();files["models/csol/hands/declared_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
 GoldCraftBeginCsolResources();Precache("models/custom/v_fixture.mdl");
 CHECK(models.empty());CHECK(resourceBatch->registeredHands==std::set<std::string>{"models/csol/hands/declared.mdl"});
 CHECK(resourceBatch->rebuilt==1);GoldCraftEndCsolResources();
 CHECK(models==std::vector<std::string>{"models/csol/hands/declared.mdl"});
 // Registered models also count toward the same cache memory budget.
 DependencyCache bounded;Stamp stamp{true};auto probe=[&](const std::string&,bool){return stamp;};
 auto one=std::make_shared<const Files>(Files{"one"});
 auto huge=std::make_shared<const Files>(Files{std::string(16u*1024u*1024u,'x')});
 bounded.Store("large-model-list",one,Inputs{{"input",stamp}},probe,huge);CHECK(bounded.Size()==0);
 CHECK(opens==closes);
}
}
int FS_FileExists(const char* p){return fixture::files.contains(p);}
FileHandle_t FS_Open(const char* p,const char* mode){CHECK(std::string_view(mode)=="rb");auto it=fixture::files.find(p);if(it==fixture::files.end())return nullptr;++fixture::opens;return reinterpret_cast<FileHandle_t>(new std::pair<std::string,fixture::Bytes>(it->first,it->second));}
void FS_Close(FileHandle_t p){++fixture::closes;delete reinterpret_cast<std::pair<std::string,fixture::Bytes>*>(p);}
unsigned FS_Size(FileHandle_t p){return unsigned(reinterpret_cast<std::pair<std::string,fixture::Bytes>*>(p)->second.size());}
int FS_Read(void* out,int size,int count,FileHandle_t p){auto& f=*reinterpret_cast<std::pair<std::string,fixture::Bytes>*>(p);CHECK(count==1&&size==int(f.second.size()));++fixture::reads[f.first];std::memcpy(out,f.second.data(),size);return size;}
int PF_precache_generic_I(const char* p){if(std::find(fixture::models.begin(),fixture::models.end(),p)!=fixture::models.end())return 0;fixture::generic.emplace_back(p);return int(fixture::generic.size());}
int PF_precache_model_I(const char* p){CHECK(resourceBatch);fixture::models.emplace_back(p);CHECK(fixture::files.contains(p));return int(fixture::models.size());}
void Con_Printf(const char*,...){}
void Con_DPrintf(const char*,...){}
void NORETURN Sys_Error(const char* format,...){char b[2048];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);throw std::runtime_error(b);}
void Mem_Free(void*){throw std::runtime_error("unexpected owned-model release");}
byte* COM_LoadFileForMe(const char*,int*){throw std::runtime_error("unexpected model file load");}
void* Hunk_AllocName(int,const char*){throw std::runtime_error("unexpected sprite allocation");}
double Sys_FloatTime(){return 0;}
char loadname[MAX_MODEL_NAME]{};
char* va(char* format,...){static char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);return text;}
int main(){try{fixture::Cases();fixture::CofSoundCases();std::printf("hand-precache checks=%u; actual loader/collector/cache/native sounds; gameplay=no performance=no\n",fixture::checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s; checks=%u\n",e.what(),fixture::checks);return 1;}}
