// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual loader and dependency collector; filesystem/model registration
// are explicit SDK boundary doubles. No source snapshots or renderer substitutes.
#include "precompiled.h"
#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
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
std::map<std::string,unsigned> existenceQueries;
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
void Reset(){resourceBatch.reset();dependencyCache=DependencyCache{};files.clear();generic.clear();models.clear();reads.clear();existenceQueries.clear();}
void Setup(){
 Reset();files["models/custom/v_fixture.mdl"]=Model();files["models/csol/hands/declared.mdl"]=Model();
 files["models/custom/v_fixture_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
}
unsigned metadataQueries{};
bool failMetadata{};
std::uint64_t metadataRevision{};
int QueryMetadata(const void* owner,const char* name,goldcraft::filesystem::ResourceStamp* result,std::uint32_t size){
 CHECK(owner==g_pFileSystem && name && result && size==sizeof(*result));++metadataQueries;
 if(failMetadata || !files.contains(name))return 0;
 *result={1,2,3,files.at(name).size(),11,22,metadataRevision,0,files.at(name).size(),0};return 1;
}
int OpenedMetadata(const void*,void*,goldcraft::filesystem::ResourceStamp*,std::uint32_t){return 0;}
void MetadataProbeCases(){
 Reset();metadataQueries=0;metadataRevision=1;failMetadata=false;files["present"]=Text("content");
 const goldcraft::filesystem::ResourceStampAPI api{sizeof(api),QueryMetadata,OpenedMetadata};
 ResourceBatch first;first.metadata=&api;
 const auto original=first.Probe("present",true);
 CHECK(original.exists && original.read && original.metadata && original.reusable);
 CHECK(metadataQueries==1 && existenceQueries["present"]==0 && reads.empty());
 CHECK(first.Probe("present",true)==original && first.Exists("present"));CHECK(metadataQueries==1);
 CHECK(!first.Probe("absent",true).exists);CHECK(existenceQueries["absent"]==1 && metadataQueries==2);
 CHECK(!first.Probe("absent",true).exists);CHECK(existenceQueries["absent"]==1 && metadataQueries==2);
 // New transactions must discover newly created optional inputs and replacement identities.
 files["absent"]=Text("now present");++metadataRevision;
 ResourceBatch next;next.metadata=&api;
 CHECK(next.Probe("absent",true).metadata);CHECK(metadataQueries==3 && existenceQueries["absent"]==1);
 CHECK(next.Probe("present",true)!=original);CHECK(metadataQueries==4 && existenceQueries["present"]==0);
 // A failed optional metadata provider still proves content by reading and hashing.
 failMetadata=true;ResourceBatch fallback;fallback.metadata=&api;
 const auto hashed=fallback.Probe("present",true);
 CHECK(hashed.exists && hashed.read && hashed.reusable && !hashed.metadata);
 CHECK(existenceQueries["present"]==1 && reads["present"]==1 && opens==closes);
 failMetadata=false;ResourceBatch legacy;legacy.metadata=nullptr;
 const auto withoutProvider=legacy.Probe("present",true);
 CHECK(withoutProvider.exists && withoutProvider.read && withoutProvider.reusable && !withoutProvider.metadata);
 CHECK(reads["present"]==2 && opens==closes);
 ResourceBatch full;full.metadata=&api;
 for(unsigned i=0;i<65536;++i)full.inputs.emplace("missing/"+std::to_string(i),ResourceBatch::Input{});
 const auto capped=full.Probe("present",true);
 CHECK(capped.exists && capped.read && !capped.reusable && full.inputs.size()==65536);
 CHECK(reads["present"]==3 && opens==closes);
 Reset();
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
 CHECK(GoldCraftStudioSoundResource(nullptr,5004,sample).kind==guest::cof::StudioSoundKind::Native);
 CHECK(GoldCraftStudioSoundResource(view,5004,nullptr).kind==guest::cof::StudioSoundKind::Native);
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
Bytes SoundModel(int event,std::string_view sample){
 auto model=Model();const auto at=model.size();model.resize(at+sizeof(mstudioevent_t));
 Put(model,72,int(model.size()));Put(model,356+48,1);Put(model,356+52,int(at));
 Put(model,at+4,event);CHECK(sample.size()<64);std::memcpy(model.data()+at+12,sample.data(),sample.size());return model;
}
void ShotgunSoundCases(){
 using namespace guest::cof;using namespace guest::cof::resources;
 constexpr char view[]="models/guest/cof/shotgun/v_shotgun.mdl";
 constexpr char missing[]="weapons/syringe/syringe_sleeve.wav";
 const auto absent=StudioLocalSoundDependency(view,5004,missing);
 CHECK(absent.kind==StudioSoundKind::OriginalMissing&&absent.resource.empty());
 CHECK(GoldCraftStudioSoundResource(view,5004,missing).kind==StudioSoundKind::OriginalMissing);
 for(const auto& wave:ShotgunLocalWaves){
  const auto sample="weapons/"+std::string(std::string_view(wave.original).substr(12));
  const auto dependency=GoldCraftStudioSoundResource(view,5004,sample.c_str());
  CHECK(dependency.kind==StudioSoundKind::LocalResource&&dependency.resource==wave.host);
 }
 // Only this exact original cue is optional. Similar paths and other events
 // retain the native required-resource behavior in the real dependency reader.
 for(const auto& [model,event,sample]:std::vector<std::tuple<std::string,int,std::string>>{
     {"models/v_m3.mdl",5004,missing},{view,5005,missing},
     {view,5004,"weapons/syringe/unknown.wav"},{view,5004,"weapons/shotgun/unknown.wav"},
     {"models/guest/cof/shotgun/w_shotgun.mdl",5004,missing}}){
  CHECK(StudioLocalSoundDependency(model,event,sample).kind==StudioSoundKind::Native);
  Setup();files[model]=SoundModel(event,sample);
  files[model.substr(0,model.size()-4)+"_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
  GoldCraftBeginCsolResources();bool rejected=false;try{Precache(model.c_str());}catch(const std::exception&){rejected=true;}
  CHECK(rejected&&models.empty());resourceBatch.reset();
 }
 Setup();files[view]=SoundModel(5004,missing);
 files["models/guest/cof/shotgun/v_shotgun_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
 for(int pass=0;pass<2;++pass){
  models.clear();generic.clear();GoldCraftBeginCsolResources();Precache(view);Precache(view);
  CHECK(resourceBatch->rebuilt==(pass?0u:1u)&&resourceBatch->hits==(pass?2u:1u));
  CHECK(generic==std::vector<std::string>{"models/guest/cof/shotgun/v_shotgun_hands.json"});
  CHECK(!reads.contains("sound/weapons/syringe/syringe_sleeve.wav"));
  CHECK(!reads.contains("sound/guest/cof/weapons_new/syringe/syringe_sleeve.wav"));
  GoldCraftEndCsolResources();
 }
 auto model=files.at(view);generic.clear();PrecacheModelSoundsForFile(view,reinterpret_cast<studiohdr_t*>(model.data()));
 CHECK(generic.empty());
 PrecacheModelSounds(reinterpret_cast<studiohdr_t*>(model.data()));
 CHECK(generic==std::vector<std::string>{"sound/weapons/syringe/syringe_sleeve.wav"});
}
void RealShotgunSoundCases(const char* resourceRoot){
 namespace fs=std::filesystem;
 Reset();const fs::path root(resourceRoot);
 // The runner seals the original model identity and actual authored resources.
 for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file()){
  CHECK(!entry.is_symlink()&&entry.file_size()<=64*1024*1024);
  const auto relative=entry.path().lexically_relative(root).generic_string();
  if(relative=="manifest.json")continue;
  std::ifstream stream(entry.path(),std::ios::binary);CHECK(stream.good());
  const std::vector<char> raw{std::istreambuf_iterator<char>(stream),{}};
  Bytes data(raw.size());std::memcpy(data.data(),raw.data(),raw.size());files[relative]=std::move(data);
 }
 constexpr char view[]="models/guest/cof/shotgun/v_shotgun.mdl";
 auto model=files.at(view);const auto* header=reinterpret_cast<const studiohdr_t*>(model.data());CHECK(header->numseq==20);
 const auto* sequences=reinterpret_cast<const mstudioseqdesc_t*>(model.data()+header->seqindex);
 unsigned draw=0,events=0;
 for(int i=0;i<header->numseq;++i){
  const auto* sounds=reinterpret_cast<const mstudioevent_t*>(model.data()+sequences[i].eventindex);
  for(int j=0;j<sequences[i].numevents;++j)if(sounds[j].event==5004){
   ++events;if(std::string_view(sounds[j].options)=="weapons/syringe/syringe_sleeve.wav"){
    CHECK(i==6&&sounds[j].frame==1);++draw;
   }
  }
 }
 CHECK(events==7&&draw==1);
 // A minimal explicit donor activates the complete production collector. This
 // fixture does not claim to validate real hand retargeting or model rendering.
 files["models/csol/hands/declared.mdl"]=Model();
 files["models/guest/cof/shotgun/v_shotgun_hands.json"]=Text(R"({"version":1,"donor":"models/csol/hands/declared.mdl"})");
 const std::set<std::string> expected{
  "sound/guest/cof/weapons_new/shotgun/insert.wav",
  "sound/guest/cof/weapons_new/shotgun/pump_forward.wav",
  "sound/guest/cof/weapons_new/shotgun/pump_back.wav",
  "sound/guest/cof/weapons_new/shotgun/pump_seq.wav"};
 CHECK(!files.contains("sound/weapons/syringe/syringe_sleeve.wav"));
 CHECK(!files.contains("sound/guest/cof/weapons_new/syringe/syringe_sleeve.wav"));
 for(int pass=0;pass<2;++pass){
  models.clear();generic.clear();GoldCraftBeginCsolResources();Precache(view);Precache(view);
  CHECK(resourceBatch->rebuilt==(pass?0u:1u)&&resourceBatch->hits==(pass?2u:1u));
  std::set<std::string> actual;for(const auto& file:generic)if(file.starts_with("sound/"))actual.insert(file);
  CHECK(actual==expected);GoldCraftEndCsolResources();
  generic.clear();PrecacheModelSoundsForFile(view,reinterpret_cast<studiohdr_t*>(model.data()));
  CHECK(std::set<std::string>(generic.begin(),generic.end())==expected&&generic.size()==6);
 }
 // A required mapped wave disappearing invalidates the warm dependency plan.
 files.erase(*expected.begin());models.clear();GoldCraftBeginCsolResources();
 bool rejected=false;try{Precache(view);}catch(const std::exception&){rejected=true;}
 CHECK(rejected&&models.empty()&&opens==closes);resourceBatch.reset();
}
void RealCofHandCases(const char* runtimeRoot,const char* candidateRoot){
 namespace fs=std::filesystem;
 Reset();
 const auto load=[&](const fs::path& root,const fs::path& relative){
  const auto directory=root/relative;CHECK(fs::is_directory(directory));
  for(const auto& entry:fs::recursive_directory_iterator(directory))if(entry.is_regular_file()){
   CHECK(!entry.is_symlink()&&entry.file_size()<=64*1024*1024);
   std::ifstream stream(entry.path(),std::ios::binary);CHECK(stream.good());
   const std::vector<char> raw{std::istreambuf_iterator<char>(stream),{}};
   Bytes data(raw.size());std::memcpy(data.data(),raw.data(),raw.size());
   files[entry.path().lexically_relative(root).generic_string()]=std::move(data);
  }
 };
 for(const char* path:{"models/guest/cof","sound/guest/cof","events/guest/cof",
     "aurora/guest/cof","gfx/guest/cof","gfx/tattoo","sprites/guest/cof","inventoryitems/guest/cof"})load(runtimeRoot,path);
 load(candidateRoot,"models");
 const std::set<std::string> expected{
  "models/csol/hands/cof/v_glock.mdl","models/csol/hands/cof/v_p345.mdl","models/csol/hands/cof/v_revolver.mdl"};
 for(int pass=0;pass<2;++pass){
  models.clear();generic.clear();GoldCraftBeginCsolResources();
  for(const char* weapon:{"glock","p345","revolver"}){
   const std::string path="models/guest/cof/"+std::string(weapon)+"/v_"+weapon+".mdl";
   Precache(path.c_str());Precache(path.c_str());
   const auto profile=path.substr(0,path.size()-4)+"_hands.json";
   CHECK(std::find(generic.begin(),generic.end(),profile)!=generic.end());
  }
  CHECK(models.empty()&&resourceBatch->registeredHands==expected&&opens==closes);
  CHECK(pass?resourceBatch->rebuilt==0:resourceBatch->rebuilt==3);
  GoldCraftEndCsolResources();CHECK(std::set<std::string>(models.begin(),models.end())==expected&&models.size()==3);
 }
 // A real missing donor must fail the same cold transaction; cached readiness
 // must not turn a previous valid model into a permanent success.
 files.erase("models/csol/hands/cof/v_revolver.mdl");models.clear();GoldCraftBeginCsolResources();
 bool rejected=false;try{Precache("models/guest/cof/revolver/v_revolver.mdl");}catch(const std::exception&){rejected=true;}
 CHECK(rejected&&models.empty()&&opens==closes);resourceBatch.reset();
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
int FS_FileExists(const char* p){++fixture::existenceQueries[p];return fixture::files.contains(p);}
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
void GoldCraftLoadStudioOwned(model_t*,std::span<std::byte>){throw std::runtime_error("unexpected model ownership transfer");}
double Sys_FloatTime(){return 0;}
char loadname[MAX_MODEL_NAME]{};
char* va(char* format,...){static char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);return text;}
int main(int argc,char** argv){try{CHECK(argc==1||argc==3);fixture::MetadataProbeCases();fixture::Cases();fixture::CofSoundCases();fixture::ShotgunSoundCases();if(argc==3){if(std::string_view(argv[1])=="--shotgun-root")fixture::RealShotgunSoundCases(argv[2]);else fixture::RealCofHandCases(argv[1],argv[2]);}std::printf("hand-precache checks=%u; actual loader/collector/cache/native sounds; gameplay=no performance=no\n",fixture::checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s; checks=%u\n",e.what(),fixture::checks);return 1;}}
