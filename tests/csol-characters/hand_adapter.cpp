// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/assets.hpp"
#include "../../native/csol/client/characters.hpp"
#include "../../native/csol/hand_adapter.hpp"
#include "texture_fixture.hpp"
#include "bodygroup_fixture.hpp"
#include <event_api.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <cstdarg>
namespace fs=std::filesystem;
namespace fixture {
fs::path stock,resources;const csol::Character* character{};
std::unordered_map<std::string,std::vector<std::byte>> injected;
int checks{};bool advertised=true;
void Check(bool good,const std::source_location loc=std::source_location::current()) {
    ++checks;if(!good)throw std::runtime_error("adapter test line "+std::to_string(loc.line()));
}
std::vector<std::byte> Read(const fs::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(p.string());
    const auto size=f.tellg();std::vector<std::byte> bytes(size_t(size),std::byte{});f.seekg(0);f.read((char*)bytes.data(),size);return bytes;
}
void Console(const char* format,...){char line[1024];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);std::cout<<line;}
std::vector<std::byte> Bytes(std::string_view s){return {(const std::byte*)s.data(),(const std::byte*)s.data()+s.size()};}
}
namespace csol::client {
cl_enginefunc_t engine{};engine_studio_api_t studio{};
const Character* ViewCharacter(){return fixture::character;}
bool Exists(const char* p){return fixture::injected.contains(p) || fs::is_regular_file(fixture::resources/p) || fs::is_regular_file(fixture::stock/p);}
std::vector<std::byte> ReadResource(const char* p){if(auto i=fixture::injected.find(p);i!=fixture::injected.end())return i->second;
    return fixture::Read(fs::is_regular_file(fixture::resources/p)?fixture::resources/p:fixture::stock/p);}
model_t* LoadEngineModel(const char*){throw std::runtime_error("unexpected sprite");}
}
int main(int argc,char** argv){using namespace csol;using namespace csol::client;using fixture::Check;
try {
    Check(argc==3);fixture::stock=argv[1];fixture::resources=argv[2];
    Check(glfwInit()!=0);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);auto* window=glfwCreateWindow(64,64,"Hand adapter GL",nullptr,nullptr);Check(window);
    glfwMakeContextCurrent(window);glewExperimental=true;Check(glewInit()==GLEW_OK);while(glGetError()){}
    engine.Con_Printf=fixture::Console;event_api_t event{};event.EV_FindModelIndex=[](const char*){return fixture::advertised?1:0;};engine.pEventAPI=&event;
    const auto data=ReadResource("models/csol/characters.json");auto catalog=ParseCharacters({(char*)data.data(),data.size()});
    fixture::character=&catalog.front();const auto donor=ReadResource("models/csol/hands/v_hegrenade.mdl");
    int models{};
    for(const char* file:{"models/zombie_plague/v_grenade_fire.mdl","models/zombie_plague/v_grenade_frost.mdl","models/zombie_plague/v_grenade_flare.mdl","models/v_ak47.mdl","models/v_m4a1.mdl","models/v_glock18.mdl"}) {
        model_t model{};strcpy_s(model.name,file);auto* original=GetAsset(&model);Check(original);
        const auto source=original->bytes;const auto before=original->Header();
        auto merged=AdaptViewmodelHands(source,donor);Check(!merged.bytes.empty());
        // Every byte of the original after the header is untouched, including
        // complete RLE animation channels, original meshes, UVs and texels.
        Check(merged.bytes.size()>source.size());Check(!memcmp(merged.bytes.data()+sizeof(studiohdr_t),source.data()+sizeof(studiohdr_t),source.size()-sizeof(studiohdr_t)));
        auto after=Range<studiohdr_t>(merged.bytes,0)[0];
        Check(after.numbones==before.numbones && after.boneindex==before.boneindex && after.seqindex==before.seqindex && after.numseq==before.numseq);
        Check(after.attachmentindex==before.attachmentindex && after.numattachments==before.numattachments);
        Check(after.numskinfamilies==before.numskinfamilies);
        for(int family=0;family<before.numskinfamilies;++family)Check(!memcmp(source.data()+before.skinindex+family*before.numskinref*2,merged.bytes.data()+after.skinindex+family*after.numskinref*2,before.numskinref*2));
        auto* adapted=ViewHandAsset(original);Check(adapted!=original && adapted->vao && adapted->special);Check(original->bytes==source);
        Check(ViewHandAsset(original)==adapted);
        for(const auto& c:catalog) {
            fixture::character=&c;UpdateHands();const int group=adapted->Header().numbodyparts-1;
            Check(CharacterHandSubmodel(*adapted,group,0)==(c.male?0:1));
            for(const auto& batch:adapted->batches)if(batch.group==group && batch.submodel==(c.male?0:1)) {
                const auto skins=adapted->Data<int16_t>(adapted->Header().skinindex,adapted->Header().numskinref);
                const auto& material=adapted->materials[skins[batch.skinref]];Check(material.hand>=0);
                GLuint texture=material.texture;float w=float(material.width),h=float(material.height),uv[2]{};ApplyHand(material,texture,w,h,uv);
                Check(c.hand.empty()?texture==material.texture:texture!=material.texture);
            }
        }
        fixture::character=nullptr;Check(ViewHandAsset(original)==original);fixture::character=&catalog.front();
        Check(glGetError()==GL_NO_ERROR);ClearAssets();++models;
    }
    // Arbitrary future model paths use the same adapter without a C++ table.
    const auto source=ReadResource("models/zombie_plague/v_grenade_fire.mdl");
    fixture::injected["models/custom/v_future_rifle.mdl"]=source;
    model_t future{};strcpy_s(future.name,"models/custom/v_future_rifle.mdl");
    auto* a=GetAsset(&future);Check(ViewHandAsset(a)!=a);ClearAssets();
    // The same weapon supplied as main + T.mdl must keep its original animation,
    // gun geometry and pixels while loading the character hands through real GL.
    const auto split=texture_fixture::Split(source);
    fixture::injected[future.name]=split.model;
    fixture::injected["models/custom/v_future_rifleT.mdl"]=split.textures;
    a=GetAsset(&future);Check(a && a->Header().numtextures==texture_fixture::Word(source,180));
    Check(!memcmp(a->bytes.data()+244,split.model.data()+244,split.model.size()-244));
    auto* splitHands=ViewHandAsset(a);Check(splitHands!=a && splitHands->vao && splitHands->adaptedMeshes>0);
    for(const auto& c:catalog) {
        fixture::character=&c;UpdateHands();
        Check(CharacterHandSubmodel(*splitHands,splitHands->Header().numbodyparts-1,0)==(c.male?0:1));
    }
    Check(glGetError()==GL_NO_ERROR);ClearAssets();
    const std::string descriptor=R"({"version":1,"requested":"models/custom/v_future_rifle.mdl","physical":"models/custom/v_split_base.mdl","texturePrefix":"","configuration":""})";
    fixture::injected["models/custom/v_future_rifle_csol.json"]=fixture::Bytes(descriptor);
    fixture::injected["models/custom/v_split_baseT.mdl"]=split.textures;
    fixture::injected.erase("models/custom/v_future_rifleT.mdl");
    a=GetAsset(&future);Check(a && ViewHandAsset(a)!=a);ClearAssets();
    fixture::injected.erase("models/custom/v_future_rifle_csol.json");
    fixture::injected.erase("models/custom/v_split_baseT.mdl");
    // Missing companion safely returns this stock model to the host renderer.
    fixture::injected.erase("models/custom/v_future_rifleT.mdl");Check(GetAsset(&future)==nullptr);ClearAssets();
    texture_fixture::Put(fixture::injected[future.name],180,texture_fixture::Word(source,180));
    Check(GetAsset(&future)==nullptr);ClearAssets();
    fixture::injected[future.name]=source;
    const auto splitDonor=texture_fixture::Split(donor);
    fixture::injected["models/csol/hands/v_external.mdl"]=splitDonor.model;
    fixture::injected["models/csol/hands/v_externalT.mdl"]=splitDonor.textures;
    fixture::injected["models/custom/v_future_rifle_hands.json"]=fixture::Bytes(R"({"version":1,"donor":"models/csol/hands/v_external.mdl"})");
    a=GetAsset(&future);Check(a && ViewHandAsset(a)!=a);ClearAssets();
    fixture::injected.erase("models/custom/v_future_rifle_hands.json");
    fixture::advertised=false;a=GetAsset(&future);Check(ViewHandAsset(a)==a);ClearAssets();fixture::advertised=true;
    // Bone names alone are insufficient: reject changed lengths/topology and
    // mixed weapon/hand material. An explicit local mapping handles custom rigs.
    auto changed=source;auto& h=*reinterpret_cast<studiohdr_t*>(changed.data());
    auto* bones=reinterpret_cast<mstudiobone_t*>(changed.data()+h.boneindex);int bone=-1;
    for(int i=0;i<h.numbones;++i)if(std::string_view(bones[i].name)=="Bone_Righthand")bone=i;
    Check(bone>=0);bones[bone].value[0]+=1;Check(AdaptViewmodelHands(changed,donor).bytes.empty());
    changed=source;bones=reinterpret_cast<mstudiobone_t*>(changed.data()+h.boneindex);strcpy_s(bones[bone].name,"CustomRightPalm");
    Check(AdaptViewmodelHands(changed,donor).bytes.empty());
    HandAdapterOptions options;options.bones.push_back({"bone_righthand","customrightpalm"});Check(!AdaptViewmodelHands(changed,donor,options).bytes.empty());
    options.bones[0].transform[0]=2;Check(AdaptViewmodelHands(changed,donor,options).bytes.empty());
    fixture::injected[future.name]=changed;
    fixture::injected["models/custom/v_future_rifle_hands.json"]=fixture::Bytes(R"({"version":1,"bones":[{"donor":"Bone_Righthand","target":"CustomRightPalm"}]})");
    a=GetAsset(&future);Check(ViewHandAsset(a)!=a);ClearAssets();
    fixture::injected["models/custom/v_future_rifle_hands.json"]=fixture::Bytes(R"({"version":1,"donor":"../bad.mdl"})");
    a=GetAsset(&future);Check(ViewHandAsset(a)==a);ClearAssets();
    options={};options.textures={"view_glove.bmp","view_skin.bmp","view_finger.bmp","gren_one.bmp"};Check(AdaptViewmodelHands(source,donor,options).bytes.empty());
    // Real weapon geometry supplies full/empty/left/right hand states, with an
    // unrelated weapon bodygroup that must keep its original encoded selection.
    const auto conditional=bodygroup_fixture::Make(source);
    const auto converted=AdaptViewmodelHands(conditional,donor);
    if(converted.bytes.empty())throw std::runtime_error(converted.reason);
    Check(converted.bodygroups.size()==3 && converted.bodygroups[0].visibility==std::vector<uint8_t>({2,0}) && converted.bodygroups[1].visibility==std::vector<uint8_t>({1,0}));
    Check(!memcmp(conditional.data()+244,converted.bytes.data()+244,conditional.size()-244));
    fixture::injected.erase("models/custom/v_future_rifle_hands.json");fixture::injected[future.name]=conditional;
    a=GetAsset(&future);auto* switched=ViewHandAsset(a);Check(switched!=a && switched->vao);
    const auto& sh=switched->Header();const auto outputGroups=switched->Data<mstudiobodyparts_t>(sh.bodypartindex,sh.numbodyparts);
    const auto inputHeader=Range<studiohdr_t>(conditional,0)[0];
    const auto inputGroups=Range<mstudiobodyparts_t>(conditional,inputHeader.bodypartindex,inputHeader.numbodyparts);
    for(int g=0;g<3;++g) {
        Check(outputGroups[g].base==inputGroups[g].base && outputGroups[g].nummodels==inputGroups[g].nummodels);
        const auto beforeModels=Range<mstudiomodel_t>(conditional,inputGroups[g].modelindex,inputGroups[g].nummodels);
        const auto afterModels=switched->Data<mstudiomodel_t>(outputGroups[g].modelindex,outputGroups[g].nummodels);
        for(int s=0;s<outputGroups[g].nummodels;++s) {
            const auto originalMeshes=Range<mstudiomesh_t>(conditional,beforeModels[s].meshindex,beforeModels[s].nummesh);
            for(const auto& mesh:switched->Data<mstudiomesh_t>(afterModels[s].meshindex,afterModels[s].nummesh))
                Check(std::any_of(originalMeshes.begin(),originalMeshes.end(),[&](const auto& original){return !memcmp(&mesh,&original,sizeof(mesh));}));
        }
    }
    const auto switchedBones=switched->Data<mstudiobone_t>(sh.boneindex,sh.numbones);
    model_t donorModel{};strcpy_s(donorModel.name,"models/csol/hands/v_hegrenade.mdl");
    auto* donorAsset=GetAsset(&donorModel);Check(donorAsset);EnsureGeometry(*donorAsset);
    for(int gender=0;gender<2;++gender) {
        const auto originalTriangles=bodygroup_fixture::Triangles(*donorAsset,1,gender);
        const auto complete=bodygroup_fixture::Triangles(*switched,3,gender*4+3);
        Check(!originalTriangles.empty() && complete==originalTriangles);
        auto separate=bodygroup_fixture::Triangles(*switched,3,gender*4+1);
        auto right=bodygroup_fixture::Triangles(*switched,3,gender*4+2);
        Check(!separate.empty() && !right.empty());separate.insert(separate.end(),right.begin(),right.end());std::sort(separate.begin(),separate.end());
        Check(separate==complete);Check(bodygroup_fixture::Triangles(*switched,3,gender*4).empty());
    }
    const int masks[]{3,1,2,0};
    for(const auto& c:catalog) {
        fixture::character=&c;UpdateHands();
        for(int body=0;body<8;++body) {
            const int selected=CharacterHandSubmodel(*switched,3,0,body),mask=masks[body%4];
            Check(selected==(c.male?0:4)+mask);
            for(int g=0;g<3;++g)Check(CharacterHandSubmodel(*switched,g,(body/inputGroups[g].base)%inputGroups[g].nummodels,body)==(body/inputGroups[g].base)%inputGroups[g].nummodels);
            const auto selectedModel=switched->Data<mstudiomodel_t>(outputGroups[3].modelindex,8)[selected];
            const auto vertexBones=switched->Data<uint8_t>(selectedModel.vertinfoindex,selectedModel.numverts);
            int observed{};
            for(const auto& mesh:switched->Data<mstudiomesh_t>(selectedModel.meshindex,selectedModel.nummesh)) {
                int cursor=mesh.triindex,triangles{};
                for(;;) {
                    const int count=switched->Data<int16_t>(cursor)[0];cursor+=2;if(!count)break;
                    Check(count==3);auto stream=switched->Data<int16_t>(cursor,12);cursor+=24;++triangles;
                    for(int i=0;i<3;++i) {
                        int b=vertexBones[stream[i*4]];while(switchedBones[b].parent>=0)b=switchedBones[b].parent;
                        int side=Name(switchedBones[b].name)=="Bone01"?1:Name(switchedBones[b].name)=="Bone04"?2:0;
                        Check(side && (mask&side));observed|=side;
                    }
                }
                Check(triangles==mesh.numtris);
            }
            Check(observed==mask);
        }
    }
    fixture::character=nullptr;Check(ViewHandAsset(a)==a);Check(CharacterHandSubmodel(*switched,3,5,0)==5);
    fixture::character=&catalog.front();Check(glGetError()==GL_NO_ERROR);ClearAssets();
    // A finger-only conditional state must not acquire a whole visible arm.
    auto partial=conditional;auto* pHeader=reinterpret_cast<studiohdr_t*>(partial.data());
    auto* pGroups=reinterpret_cast<mstudiobodyparts_t*>(partial.data()+pHeader->bodypartindex);
    auto* pModels=reinterpret_cast<mstudiomodel_t*>(partial.data()+pGroups[0].modelindex);
    pModels[0].nummesh=1;
    Check(AdaptViewmodelHands(partial,donor).bytes.empty());
    std::cout<<"PASS "<<fixture::checks<<" checks, "<<models<<" actual models, "<<catalog.size()<<" roles; GL "<<glGetString(GL_RENDERER)<<'\n';
    glfwDestroyWindow(window);glfwTerminate();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
