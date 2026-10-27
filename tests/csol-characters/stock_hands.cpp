// SPDX-License-Identifier: GPL-3.0-or-later
// Real client asset/material uploads and scoped model substitution. Engine
// callbacks supply precached models; no game-input or gameplay is simulated.
#include "../../native/csol/client/stock_hands.cpp"
#include "../../native/csol/client/assets.hpp"
#include "../../native/csol/model_codec.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <source_location>
#include <cmath>

namespace fs=std::filesystem;
namespace fixture {
fs::path resources,characters,stock,source,animationOverride;
const csol::Character* character{};
cl_entity_t entity{};
std::array<model_t,30> natives{},aliases{};
std::array<std::vector<std::byte>,30> nativeBytes,aliasBytes;
int checks{},lookups{},sourceReads{},dataReads{},eventCalls{};
studiohdr_t* selectedHeader{};
model_t* expectedEventModel{};
int expectedEventIndex{};
bool throwEvent{};
unsigned long channelFrames{};
double maxChannelError{};
bool advertised=true;
void Check(bool good,const std::source_location where=std::source_location::current())
{++checks;if(!good)throw std::runtime_error("stock hands line "+std::to_string(where.line()));}
std::vector<std::byte> Read(const fs::path& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(path.string());
    auto n=f.tellg();std::vector<std::byte> bytes(size_t(n),std::byte{});f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),n);return bytes;
}
int Find(const char* name){++lookups;if(!advertised)return 0;for(int i=0;i<30;++i)if(name==std::string_view(aliases[i].name))return i+1;return 0;}
model_t* Model(int index){return index>0 && index<=30?&aliases[index-1]:nullptr;}
void* Data(model_t* model){++dataReads;for(int i=0;i<30;++i){if(model==&aliases[i])return aliasBytes[i].data();if(model==&natives[i])return nativeBytes[i].data();}return nullptr;}
const mstudioseqdesc_t* Sequences(model_t* model){
    const auto* header=static_cast<const studiohdr_t*>(Data(model));
    return reinterpret_cast<const mstudioseqdesc_t*>(reinterpret_cast<const char*>(header)+header->seqindex);
}
void ClientEvents(){
    ++eventCalls;Check(entity.model==expectedEventModel && entity.curstate.modelindex==expectedEventIndex);
    Check(selectedHeader==Data(expectedEventModel));
    // The production wrapper must expose the native event descriptor even
    // when its bone animation has a different source frame grid.
    const auto* seq=reinterpret_cast<const mstudioseqdesc_t*>(reinterpret_cast<const char*>(selectedHeader)+selectedHeader->seqindex);
    Check(&seq[entity.curstate.sequence]==&Sequences(expectedEventModel)[entity.curstate.sequence]);
    if(throwEvent)throw 17;
}
int Sample(const std::vector<std::byte>& data,int table,int axis,int frame){
    auto take=[&](int offset,int bytes){Check(offset>=0 && bytes>=0 && size_t(offset)+bytes<=data.size());return data.data()+offset;};
    uint16_t offset{};memcpy(&offset,take(table+axis*2,2),2);if(!offset)return 0;
    int cursor=table+offset;
    for(;;){const auto* run=take(cursor,2);int valid=int(run[0]),total=int(run[1]);Check(valid>0 && valid<=total);
        if(frame<total){int16_t value{};memcpy(&value,take(cursor+2*(1+std::min(frame,valid-1)),2),2);return value;}
        frame-=total;cursor+=2*(valid+1);
    }
}
void CompareAnimation(int i){
    auto original=Read(source/(i==29?"models/v_knife_w.mdl":csol::client::entries[i].native));
    csol::DecodeStudioModel(original);
    const auto* a=reinterpret_cast<const studiohdr_t*>(original.data());
    const auto& generated=aliasBytes[i];const auto* b=reinterpret_cast<const studiohdr_t*>(generated.data());
    Check(a->numbones==b->numbones && a->numseq==b->numseq);
    Check(!memcmp(original.data()+a->boneindex,generated.data()+b->boneindex,a->numbones*sizeof(mstudiobone_t)));
    const auto* sa=reinterpret_cast<const mstudioseqdesc_t*>(original.data()+a->seqindex);
    const auto* sb=reinterpret_cast<const mstudioseqdesc_t*>(generated.data()+b->seqindex);
    for(int s=0;s<a->numseq;++s){
        Check(sa[s].numblends==sb[s].numblends && sa[s].seqgroup==0 && sb[s].seqgroup==0);
        Check(sa[s].numframes==sb[s].numframes);
        for(int table=0;table<a->numbones*sa[s].numblends;++table)for(int axis=0;axis<6;++axis)
            for(int frame=0;frame<sb[s].numframes;++frame){
                const int expected=Sample(original,sa[s].animindex+table*12,axis,frame);
                const auto actual=Sample(generated,sb[s].animindex+table*12,axis,frame);
                const auto error=std::abs(double(actual)-expected);
                Check(error==0);maxChannelError=std::max(maxChannelError,error);++channelFrames;
            }
    }
}
void EventAndFrameGridChecks(const std::vector<csol::Character>& catalog){
    using namespace csol::client;
    const auto* male=&*std::find_if(catalog.begin(),catalog.end(),[](const auto& c){return c.male;});
    const auto* female=&*std::find_if(catalog.begin(),catalog.end(),[](const auto& c){return !c.male;});
    // Real descriptors and the actual scope, including exceptional callbacks.
    for(const auto indices:std::array<std::array<int,2>,3>{{{13,29},{9,9},{20,20}}}){
        const int native=indices[0],alias=indices[1];character=female;ResetStockHands();
        entity={};entity.model=&natives[native];entity.curstate.modelindex=100+native;
        auto* nativeHeader=static_cast<studiohdr_t*>(Data(entity.model));
        auto* aliasHeader=static_cast<studiohdr_t*>(Data(&aliases[alias]));
        bool unequal=false;
        for(int s=0;s<nativeHeader->numseq;++s){
            entity.curstate.sequence=s;entity.latched.prevsequence=s;entity.latched.prevframe=7.25f;
            entity.latched.sequencetime=0;
            const auto a=Sequences(&natives[native])[s],b=Sequences(&aliases[alias])[s];
            unequal|=a.numframes!=b.numframes;
            Check(std::abs(double(a.numframes-1)/a.fps-double(b.numframes-1)/b.fps)<1e-6);
            const auto before=entity.latched;
            {StockHandScope draw(&entity);selectedHeader=aliasHeader;
                Check(entity.model==&aliases[alias]);expectedEventModel=&natives[native];expectedEventIndex=100+native;
                auto count=eventCalls;draw.ClientEvents();Check(eventCalls==count+1);
                Check(entity.model==&aliases[alias] && entity.curstate.modelindex==alias+1 && selectedHeader==aliasHeader);
                Check(!memcmp(&before,&entity.latched,sizeof(before)));
                throwEvent=true;try{draw.ClientEvents();Check(false);}catch(int value){Check(value==17);}throwEvent=false;
                Check(entity.model==&aliases[alias] && entity.curstate.modelindex==alias+1 && selectedHeader==aliasHeader);
                Check(!memcmp(&before,&entity.latched,sizeof(before)));
            }
            Check(entity.model==&natives[native] && entity.curstate.modelindex==100+native);
        }
        Check(unequal);
    }
    const int knife=13,femaleKnife=29,draw=3;
    Check(Sequences(&natives[knife])[draw].numframes==46 && Sequences(&aliases[femaleKnife])[draw].numframes==31);
    auto close=[](float a,float b){Check(std::abs(a-b)<1e-5f);};
    ResetStockHands();entity={};entity.model=&natives[knife];entity.curstate.sequence=draw;entity.curstate.modelindex=113;
    entity.latched.prevsequence=draw;entity.latched.prevframe=20;character=female;
    {StockHandScope scope(&entity);close(entity.latched.prevframe,20);}
    // SetupBones' updated value remains in the rendered units across draws.
    entity.latched.sequencetime=1;
    for(int repeat=0;repeat<4;++repeat){StockHandScope scope(&entity);close(entity.latched.prevframe,20);}
    character=male;{StockHandScope scope(&entity);close(entity.latched.prevframe,30);}
    {StockHandScope scope(&entity);close(entity.latched.prevframe,30);}
    character=female;{StockHandScope scope(&entity);close(entity.latched.prevframe,20);}
    // Both a successful custom draw without an alias and the native renderer
    // fallback must leave the transition in the native grid exactly once.
    character=nullptr;{StockHandScope scope(&entity);close(entity.latched.prevframe,30);}
    NativeHandFrameGrid(&entity);close(entity.latched.prevframe,30);
    character=female;{StockHandScope scope(&entity);close(entity.latched.prevframe,20);}
    NativeHandFrameGrid(&entity);close(entity.latched.prevframe,30);
    NativeHandFrameGrid(&entity);close(entity.latched.prevframe,30);
    // No transition means prevframe is inert; no conversion is allowed.
    entity.latched.sequencetime=0;{StockHandScope scope(&entity);close(entity.latched.prevframe,30);}
    character=male;{StockHandScope scope(&entity);close(entity.latched.prevframe,30);}
    // A different weapon, unknown prior model or cache reset cannot reuse a
    // previous weapon's frame ratio.
    entity.latched.sequencetime=1;character=female;entity.model=&natives[20];entity.curstate.sequence=0;
    {StockHandScope scope(&entity);close(entity.latched.prevframe,30);}
    ResetStockHands();entity.model=&natives[knife];entity.curstate.sequence=draw;
    {StockHandScope scope(&entity);close(entity.latched.prevframe,30);}
    auto other=entity;const auto tracked=frameGrid.rendered;auto reads=dataReads;
    NativeHandFrameGrid(&other);NativeHandFrameGrid(nullptr);Check(frameGrid.rendered==tracked && reads==dataReads);
    entity.model=nullptr;NativeHandFrameGrid(&entity);Check(!frameGrid.entity && reads==dataReads);
    model_t brush{};brush.type=mod_brush;entity.model=&brush;
    {StockHandScope scope(&entity);Check(!frameGrid.entity && reads==dataReads);}
    entity.model=&natives[knife];character=nullptr;selectedHeader=static_cast<studiohdr_t*>(Data(entity.model));
    expectedEventModel=entity.model;expectedEventIndex=entity.curstate.modelindex;
    {StockHandScope scope(&entity);auto calls=eventCalls;scope.ClientEvents();Check(eventCalls==calls+1);}
    ResetStockHands();entity={};character=nullptr;
}
}
namespace csol::client {
cl_enginefunc_t engine{};engine_studio_api_t studio{};
const Character* ViewCharacter(){return fixture::character;}
bool Exists(const char* path){return fs::is_regular_file(fixture::resources/path) || fs::is_regular_file(fixture::characters/path);}
std::vector<std::byte> ReadResource(const char* path){
    for(int i=0;i<30;++i)if(path==std::string_view(entries[i].native)){++fixture::sourceReads;return fixture::nativeBytes[i];}
    if(!fixture::animationOverride.empty() && std::string_view(path).starts_with("models/csol/hands/") &&
        fs::is_regular_file(fixture::animationOverride/fs::path(path).filename()))
        return fixture::Read(fixture::animationOverride/fs::path(path).filename());
    return fixture::Read(fs::is_regular_file(fixture::resources/path)?fixture::resources/path:fixture::characters/path);
}
model_t* LoadEngineModel(const char*){throw std::runtime_error("Unexpected viewmodel sprite dependency");}
}
void Console(const char* format,...){char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(std::string_view(text).starts_with("CSOLSupport: hands models/v_ak47.mdl: adapted "))std::cout<<text;
    else throw std::runtime_error(text);
}
int main(int argc,char** argv){using namespace csol;using namespace csol::client;using fixture::Check;try{
    Check(argc==5 || argc==6);fixture::resources=argv[1];fixture::characters=argv[2];fixture::stock=argv[3];fixture::source=argv[4];
    if(argc==6)fixture::animationOverride=argv[5];
    Check(glfwInit()!=0);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);auto* window=glfwCreateWindow(32,32,"Stock hand GL check",nullptr,nullptr);Check(window!=nullptr);
    glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;Check(glewInit()==GLEW_OK);while(glGetError()){}
    engine.Con_Printf=Console;engine.GetViewModel=[](){return &fixture::entity;};
    event_api_t events{};events.EV_FindModelIndex=fixture::Find;engine.pEventAPI=&events;
    studio.GetModelByIndex=fixture::Model;studio.Mod_Extradata=fixture::Data;
    studio.StudioSetHeader=[](void* header){fixture::selectedHeader=static_cast<studiohdr_t*>(header);};
    studio.StudioClientEvents=fixture::ClientEvents;
    auto bytes=ReadResource("models/csol/characters.json");auto catalog=ParseCharacters({reinterpret_cast<char*>(bytes.data()),bytes.size()});
    for(int i=0;i<30;++i){
        strcpy_s(fixture::natives[i].name,entries[i].native);strcpy_s(fixture::aliases[i].name,entries[i].alias);
        fixture::natives[i].type=fixture::aliases[i].type=mod_studio;
        fixture::nativeBytes[i]=fixture::Read(fixture::stock/entries[i].native);
        fixture::aliasBytes[i]=ReadResource(entries[i].alias);
        Check(Compatible(&fixture::natives[i],&fixture::aliases[i]));
        fixture::CompareAnimation(i);
    }
    fixture::EventAndFrameGridChecks(catalog);
    unsigned male256{},genderModels{};
    // Upload each complete model through production parsing and validate every
    // published role's chosen submodel and real replacement texture handle.
    for(int i=0;i<30;++i){
        auto* asset=GetAsset(&fixture::aliases[i]);Check(asset && asset->special && asset->vao && !asset->batches.empty());
        for(const auto& material:asset->materials)if(material.hand==4 && material.originalHeight==256)++male256;
        for(const auto& c:catalog){
            if(i==13 && !c.male || i==29 && c.male)continue;
            fixture::character=&c;UpdateHands();
            bool hand=false;
            const auto& h=asset->Header();const auto groups=asset->Data<mstudiobodyparts_t>(h.bodypartindex,h.numbodyparts);
            const auto skins=asset->Data<int16_t>(h.skinindex,h.numskinref);
            for(size_t group=0;group<groups.size();++group){
                int chosen=CharacterHandSubmodel(*asset,int(group),0);Check(chosen>=0 && chosen<groups[group].nummodels);
                if(groups[group].nummodels==2)++genderModels;
                for(const auto& batch:asset->batches){
                    if(batch.group!=group || batch.submodel!=chosen)continue;
                    const auto& m=asset->materials[skins[batch.skinref]];if(m.hand<0)continue;
                    hand=true;Check(c.male?(m.hand!=2 && m.hand!=3):(m.hand==2 || m.hand==3));
                    GLuint texture=m.texture;float w=float(m.width),height=float(m.height),uv[2]{};
                    ApplyHand(m,texture,w,height,uv);Check(c.hand.empty()?texture==m.texture:texture!=m.texture);
                    if(m.originalHeight==256)Check(uv[0]==0 && uv[1]==0);
                }
            }
            Check(hand);
        }
        Check(glGetError()==GL_NO_ERROR);ClearAssets();
    }
    Check(male256>0 && genderModels>0);
    // Both genders, every weapon, and exception unwinding restore the native
    // prediction identity. Repeated draws do not repeat precache lookups.
    for(const auto& c:catalog){fixture::character=&c;for(int i=0;i<29;++i){
        auto& entity=fixture::entity;entity.model=&fixture::natives[i];entity.curstate.modelindex=100+i;entity.curstate.sequence=0;
        const int expected=i==13 && !c.male?29:i;
        {StockHandScope draw(&entity);Check(entity.model==&fixture::aliases[expected]);Check(entity.curstate.modelindex==expected+1);}
        Check(entity.model==&fixture::natives[i] && entity.curstate.modelindex==100+i);
        auto before=fixture::lookups,reads=fixture::sourceReads;{StockHandScope draw(&entity);}Check(before==fixture::lookups && reads==fixture::sourceReads);
        try{StockHandScope draw(&entity);throw 1;}catch(int){}
        Check(entity.model==&fixture::natives[i] && entity.curstate.modelindex==100+i);
    }}
    auto& entity=fixture::entity;entity.model=&fixture::natives[0];entity.curstate.modelindex=100;
    fixture::character=nullptr;{StockHandScope draw(&entity);Check(entity.model==&fixture::natives[0]);}
    fixture::character=&catalog.front();fixture::advertised=false;ResetStockHands();
    {StockHandScope draw(&entity);Check(entity.model==&fixture::natives[0]);}
    fixture::advertised=true;ResetStockHands();
    // Servers can replace a stock path with their own gun skin without
    // changing any animation descriptor. Do not replace that gun wholesale.
    const auto* nativeHeader=reinterpret_cast<const studiohdr_t*>(fixture::nativeBytes[0].data());
    const auto* texture=reinterpret_cast<const mstudiotexture_t*>(fixture::nativeBytes[0].data()+nativeHeader->textureindex);
    int weaponTexture=-1;
    for(int i=0;i<nativeHeader->numtextures;++i)if(!std::string_view(texture[i].name).starts_with("view_")){weaponTexture=i;break;}
    Check(weaponTexture>=0);
    const int pixel=texture[weaponTexture].index;const auto beforePixel=fixture::nativeBytes[0][pixel];
    fixture::nativeBytes[0][pixel]^=std::byte{1};
    Check(Compatible(&fixture::natives[0],&fixture::aliases[0]));
    {StockHandScope draw(&entity);Check(entity.model==&fixture::natives[0]);}
    auto* nativeAsset=GetAsset(&fixture::natives[0]);Check(nativeAsset && nativeAsset->bytes[pixel]!=beforePixel);
    auto* customHands=ViewHandAsset(nativeAsset);Check(customHands!=nativeAsset && customHands->adaptedMeshes==6);
    Check(customHands->bytes[pixel]==fixture::nativeBytes[0][pixel]);
    Check(!memcmp(customHands->bytes.data()+sizeof(studiohdr_t),fixture::nativeBytes[0].data()+sizeof(studiohdr_t),fixture::nativeBytes[0].size()-sizeof(studiohdr_t)));
    Check(glGetError()==GL_NO_ERROR);ClearAssets();
    fixture::nativeBytes[0][pixel]=beforePixel;ResetStockHands();
    auto* h=reinterpret_cast<studiohdr_t*>(fixture::aliasBytes[0].data());
    auto* seq=reinterpret_cast<mstudioseqdesc_t*>(fixture::aliasBytes[0].data()+h->seqindex);
    const auto fps=seq[0].fps;seq[0].fps+=1;
    {StockHandScope draw(&entity);Check(entity.model==&fixture::natives[0]);}seq[0].fps=fps;
    ResetStockHands();entity.curstate.sequence=999;
    {StockHandScope draw(&entity);Check(entity.model==&fixture::natives[0]);}
    fixture::character=nullptr;ResetHands();glfwDestroyWindow(window);glfwTerminate();
    std::cout<<"Stock hands: "<<fixture::checks<<" checks; 30 model GPU uploads, "<<catalog.size()<<" roles; MaleOrg256="<<male256
        <<"; channel frames="<<fixture::channelFrames<<" max quantization units="<<fixture::maxChannelError<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
